// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Batch.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>
#include <vector>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "StringUtils.h"
#include "LexerModule.h"
#include "LexerUtils.h"

using namespace Lexilla;

namespace {

enum {
	BatchLineStateMaskEmptyLine = 1 << 0,
	BatchLineStateLineContinuation = 1 << 1,
};

enum class Command {
	None,
	For,
	Keyword,
	Echo,
	Call,
	Goto,
	Set,
	SetValue,
	Escape,
	Argument,
};

constexpr bool IsDrive(int chPrev, int chNext) noexcept {
	return (chNext == '\\' || chNext == '/') && IsAlpha(chPrev);
}

constexpr bool IsBatchOperator(int ch, Command command) noexcept {
	return AnyOf(ch, '&', '|', '<', '>')
		|| (command != Command::Echo && AnyOf(ch, '=', '@', ',', ';', '?', '*'));
}

constexpr bool IsFileNameChar(int ch) noexcept {
	// https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#naming-conventions
	return IsGraphic(ch) && !AnyOf(ch, '<', '>', ':', '"', '|', '?', '*', // reserved characters
								'&', '(', ')', ',', ';');
}

constexpr bool IsLabelStart(int ch) noexcept {
	// ! is not allowed with SetLocal EnableDelayedExpansion
	//return !AnyOf(ch, '\r', '\n', ' ', '\t', '|', '&', '%', '!', ',', ';', '=', '+', '<', '>');
	return IsIdentifierChar(ch) || ch == '.' || ch == '-';
}

constexpr bool IsLabelChar(int ch) noexcept {
	//return !AnyOf(ch, '\r', '\n', ' ', '\t', '|', '&', '%', '!', ',', ';', '=');
	return IsIdentifierChar(ch) || ch == '.' || ch == '-';
}

constexpr bool IsVariableChar(int ch, char quote, Command command, int parenCount) noexcept {
	return IsGraphic(ch) && !(AnyOf(ch, '%', '&', '<', '>', '|', '"', quote)
		|| (parenCount != 0 && ch == ')')
		|| (quote == '\0' && command == Command::For && AnyOf(ch, ',', ';', '=')));
}

constexpr bool IsVariableEscapeChar(int ch, char quote, Command command) noexcept {
	return IsGraphic(ch) && !(ch == '%' || ch == quote
		|| (quote == '\0' && command == Command::For && AnyOf(ch, ',', ';', '=')));
}

constexpr bool IsStringStyle(int style) noexcept {
	return style == SCE_BAT_STRINGDQ || style == SCE_BAT_STRINGNQ || style == SCE_BAT_STRINGSQ || style == SCE_BAT_STRINGBT;
}

constexpr char GetStringQuote(int style) noexcept {
	switch (style) {
	case SCE_BAT_STRINGDQ: return '\"';
	case SCE_BAT_STRINGSQ: return '\'';
	case SCE_BAT_STRINGBT: return '`';
	default: return '\0';
	}
}

bool IsStringArgumentEnd(const StyleContext &sc, int outerStyle, Command command, int parenCount) noexcept {
	const int ch = (command == Command::SetValue) ? sc.GetLineNextChar() : sc.ch;
	return AnyOf(ch, '\0', '\n', '\r', ' ', '\t', '&', '|', '<', '>')
		|| (parenCount != 0 && ch == ')')
		|| ch == GetStringQuote(outerStyle);
}

bool DetectBatchEscapeChar(StyleContext &sc, int &outerStyle, Command command) {
	// Escape Characters https://www.robvanderwoude.com/escapechars.php
	int length = 0;
	const int state = (sc.state == SCE_BAT_ESCAPECHAR) ? outerStyle : sc.state;

	switch (sc.ch) {
	case '^':
		if (IsEOLChar(sc.chNext)) {
			outerStyle = IsStringStyle(state) ? state : SCE_BAT_DEFAULT;
			sc.SetState(SCE_BAT_LINE_CONTINUATION);
			return true;
		}
		if (sc.chNext == '^') {
			length = (sc.GetRelative(2) == '!') ? 2 : 1;
		} else if (IsPunctuation(sc.chNext)) {
			length = 1;
		}
		break;

	case '\"':
		// Inside the search pattern of FIND
		if (sc.chNext == '"' && state == SCE_BAT_STRINGDQ) {
			length = 1;
		}
		break;

	case '\\':
		// Inside the regex pattern of FINDSTR
		if (command == Command::Escape && (sc.chNext == '\\' || sc.chNext == '\"')) {
			length = 1;
		}
		break;
	}

	if (length != 0) {
		outerStyle = IsStringStyle(state) ? state : SCE_BAT_DEFAULT;
		sc.SetState(SCE_BAT_ESCAPECHAR);
		sc.Forward(length);
		return true;
	}
	return false;
}

constexpr bool IsTildeExpansion(int ch) noexcept {
	return AnyOf(ch, 'f', 'd', 'p', 'n', 'x', 's', 'a', 't', 'z');
}

bool DetectBatchVariable(StyleContext &sc, int &outerStyle, int &varQuoteChar, Command command, int parenCount) {
	varQuoteChar = '\0';
	if (!IsGraphic(sc.chNext) || (sc.ch == '!' && (sc.chNext == '%' || sc.chNext == '!'))) {
		return false;
	}

	outerStyle = IsStringStyle(sc.state) ? sc.state : SCE_BAT_DEFAULT;
	sc.SetState(SCE_BAT_VARIABLE);
	if (sc.ch == '!') {
		varQuoteChar = '!';
		return true;
	}
	if (sc.chNext == '*' || IsADigit(sc.chNext)) {
		// %*, %0 ... %9
		sc.Forward();
	} else if (sc.chNext == '~' || sc.chNext == '%') {
		// TODO: detect for loop to get valid variables in current scope.
		const char quote = GetStringQuote(outerStyle);
		sc.Forward();
		if (sc.ch == '%') {
			if (!IsVariableChar(sc.chNext, quote, command, parenCount)) {
				sc.ChangeState(SCE_BAT_ESCAPECHAR);
				return true;
			}
			sc.Forward();
		}
		if (sc.ch == '~' && IsVariableChar(sc.chNext, quote, command, parenCount)) {
			// see help for CALL and FOR commands
			sc.Forward();
			while (IsTildeExpansion(sc.ch) && IsVariableChar(sc.chNext, quote, command, parenCount)) {
				sc.Forward();
			}
			if (sc.ch == '$' && IsGraphic(sc.chNext)) {
				varQuoteChar = ':';
				return true;
			}
		}
		if (sc.ch == '^' && IsVariableEscapeChar(sc.chNext, quote, command)) {
			// for %%^" in (1 2 3) do echo %%^"
			// see https://www.robvanderwoude.com/clevertricks.php
			sc.Forward();
		}
	} else {
		varQuoteChar = '%';
	}
	return true;
}

static_assert(DefaultNestedStateBaseStyle + 1 == SCE_BAT_STRINGDQ);
static_assert(DefaultNestedStateBaseStyle + 2 == SCE_BAT_STRINGSQ);
static_assert(DefaultNestedStateBaseStyle + 3 == SCE_BAT_STRINGBT);

void ColouriseBatchDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const bool fold = styler.GetPropertyInt("fold", 1) & true;
	int varQuoteChar = '\0'; // %var% or !var! after SetLocal EnableDelayedExpansion
	int outerStyle = SCE_BAT_DEFAULT;
	int logicalVisibleChars = 0;
	int lineVisibleChars = 0;
	int prevLineState = 0;
	Command command = Command::None;
	int parenCount = 0;
	int chPrevNonWhite = 0;
	int stylePrevNonWhite = SCE_BAT_DEFAULT;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	std::vector<int> nestedState;
	int levelCurrent = SC_FOLDLEVELBASE;
	if (sc.currentLine > 0) {
		levelCurrent = styler.LevelAt(sc.currentLine - 1) >> 16;
		int lineState = styler.GetLineState(sc.currentLine - 1);
		prevLineState = lineState;
		/*
		1: empty line
		1: line continuation
		2: unused
		4: command
		8: parenCount
		3*4: nestedState
		*/
		parenCount = (lineState >> 8) & 0xff;
		if (lineState & BatchLineStateLineContinuation) {
			++logicalVisibleChars;
			command = static_cast<Command>((lineState >> 4) & 15);
		}
		lineState >>= 16;
		if (lineState) {
			UnpackLineState(lineState, nestedState);
		}
	}
	if (startPos != 0 && initStyle == SCE_BAT_DEFAULT) {
		LookbackNonWhite(styler, startPos, SCE_BAT_DEFAULT, chPrevNonWhite, stylePrevNonWhite);
	}

	int levelNext = levelCurrent;
	int parenBefore = parenCount;

	while (sc.More()) {
		switch (sc.state) {
		case SCE_BAT_OPERATOR:
			sc.SetState(SCE_BAT_DEFAULT);
			break;

		case SCE_BAT_IDENTIFIER:
			if (sc.ch == '.') {
				if (sc.LengthCurrent() == 4 && styler.MatchIgnoreCase(styler.GetStartSegment(), "echo")) {
					parenBefore = parenCount;
					command = Command::Echo;
					sc.ChangeState(SCE_BAT_WORD);
					sc.ForwardSetState(SCE_BAT_DEFAULT);
				}
			} else if (sc.ch == '^' || sc.ch == '%' || sc.ch == '!') {
				const bool begin = logicalVisibleChars == sc.LengthCurrent();
				const bool handled = (sc.ch == '^') ? DetectBatchEscapeChar(sc, outerStyle, command)
					: DetectBatchVariable(sc, outerStyle, varQuoteChar, command, parenCount);
				if (handled) {
					if (begin && command == Command::None) {
						command = Command::Argument;
					}
				}
			} else if (sc.ch == ':' && sc.LengthCurrent() == 1 && IsDrive(sc.chPrev, sc.chNext)) {
				++logicalVisibleChars;
				sc.Forward();
			} else if (!IsFileNameChar(sc.ch)) {
				char s[32];
				sc.GetCurrentLowered(s, sizeof(s));
				if (StrEqual(s, "rem")) {
					sc.ChangeState(SCE_BAT_COMMENT);
				} else {
					if (keywordLists[0]->InList(s)) {
						command = Command::Keyword;
						parenBefore = parenCount;
						sc.ChangeState(SCE_BAT_WORD);
						if (StrEqualsAny(s, "echo", "title")) {
							command = Command::Echo;
						} else if (StrEqualsAny(s, "do", "else")) {
							logicalVisibleChars = 0;
						} else if (StrEqualsAny(s, "exit", "goto")) {
							if (StrEqual(s, "goto")) {
								command = Command::Goto;
							}
							if (levelCurrent == SC_FOLDLEVELBASE + 1 && parenCount == 0 &&
								(logicalVisibleChars == 4 || (logicalVisibleChars == 5 &&
								LexGetPrevChar(sc.currentPos - 4, sc.styler) == '@'))) {
								levelNext--;
							}
						} else if (StrEqual(s, "call")) {
							command = Command::Call;
							logicalVisibleChars = 0;
						} else if (StrEqual(s, "set")) {
							command = Command::Set;
						} else if (StrEqual(s, "for")) {
							command = Command::For;
						}
					} else if (keywordLists[1]->InList(s)) {
						command = Command::Argument;
						parenBefore = parenCount;
						sc.ChangeState(SCE_BAT_WORD);
					} else if (logicalVisibleChars == sc.LengthCurrent()) {
						command = StrEqualsAny(s, "findstr", "reg") ? Command::Escape : Command::Argument;
						parenBefore = parenCount;
						sc.ChangeState(SCE_BAT_COMMAND);
						if (sc.ch == ':') {
							sc.Forward();
						}
					}
					sc.SetState(SCE_BAT_DEFAULT);
				}
			}
			break;

		case SCE_BAT_COMMENT:
			if (sc.atLineStart) {
				sc.SetState(SCE_BAT_DEFAULT);
			}
			break;

		case SCE_BAT_LABEL:
			if (!IsLabelChar(sc.ch)) {
				if (sc.ch == ':') {
					sc.Forward();
				}
				sc.SetState(SCE_BAT_DEFAULT);
			}
			break;

		case SCE_BAT_LABEL_LINE:
			// :[label][<Space|Tab|:>comments]
			// https://ss64.com/nt/goto.html
			if (sc.atLineEnd) {
				levelCurrent = SC_FOLDLEVELBASE;
				levelNext = SC_FOLDLEVELBASE + 1;
				sc.SetState(SCE_BAT_DEFAULT);
			} else if (IsSpaceOrTab(sc.ch) || sc.ch == ':') {
				levelCurrent = SC_FOLDLEVELBASE;
				levelNext = SC_FOLDLEVELBASE + 1;
				sc.SetState(SCE_BAT_COMMENT);
			} else if (IsGraphic(sc.ch) && !IsLabelChar(sc.ch)) {
				sc.ChangeState(SCE_BAT_NOT_BATCH);
				levelNext++;
			}
			break;

		case SCE_BAT_VARIABLE:
			if (varQuoteChar) {
				if (sc.ch == varQuoteChar) {
					varQuoteChar = '\0';
					sc.Forward();
					if (sc.chPrev == ':') {
						const char quote = GetStringQuote(outerStyle);
						if (sc.ch == '^' && IsVariableEscapeChar(sc.chNext, quote, command)) {
							sc.Forward(2);
						} else if (IsVariableChar(sc.ch, quote, command, parenCount)) {
							sc.Forward();
						}
					}
				} else if (IsEOLChar(sc.ch) || sc.ch == GetStringQuote(outerStyle)) {
					// something went wrong, e.g. ! without EnableDelayedExpansion.
					varQuoteChar = '\0';
					sc.ChangeState(outerStyle);
					sc.Rewind();
					sc.Forward();
				}
			}
			if (varQuoteChar == '\0') {
				sc.SetState(outerStyle);
				continue;
			}
			break;

		case SCE_BAT_STRINGDQ:
		case SCE_BAT_STRINGNQ:
		case SCE_BAT_STRINGSQ:
		case SCE_BAT_STRINGBT:
			if (DetectBatchEscapeChar(sc, outerStyle, command)) {
				// nop
			} else if (sc.ch == '%' || sc.ch == '!') {
				DetectBatchVariable(sc, outerStyle, varQuoteChar, command, parenCount);
			} else if (sc.ch == '\"') {
				int state = sc.state;
				if (state == SCE_BAT_STRINGDQ) {
					state = TryTakeAndPop(nestedState);
					sc.Forward();
					if ((command == Command::SetValue || command == Command::Argument || command == Command::Escape)
						&& !IsStringArgumentEnd(sc, state, command, parenCount)) {
						state = SCE_BAT_STRINGNQ;
					}
				} else {
					if (state != SCE_BAT_STRINGNQ) {
						nestedState.push_back(state);
					}
					state = SCE_BAT_STRINGDQ;
				}
				sc.SetState(state);
				if (state != SCE_BAT_STRINGDQ) {
					continue;
				}
			} else if (sc.state == SCE_BAT_STRINGDQ) {
				if (sc.ch == '=' && command == Command::Set) {
					command = Command::SetValue;
					sc.SetState(SCE_BAT_OPERATOR);
					sc.ForwardSetState(SCE_BAT_STRINGDQ);
					continue;
				}
			} else if (sc.state == SCE_BAT_STRINGNQ) {
				const int state = nestedState.empty() ? SCE_BAT_DEFAULT : nestedState.back();
				if (IsStringArgumentEnd(sc, state, command, parenCount)) {
					sc.SetState(state);
					continue;
				}
			} else if ((sc.state == SCE_BAT_STRINGSQ && sc.ch == '\'') || (sc.state == SCE_BAT_STRINGBT && sc.ch == '`')) {
				sc.Forward();
				const int chNext = sc.GetDocNextChar();
				if (chNext == ')') {
					const int state = TryTakeAndPop(nestedState);
					sc.SetState(state);
				}
			}
			break;

		case SCE_BAT_ESCAPECHAR:
			if (!DetectBatchEscapeChar(sc, outerStyle, command)) {
				sc.SetState(outerStyle);
				continue;
			}
			break;

		case SCE_BAT_NOT_BATCH:
			if (lineVisibleChars == 0 && sc.ch == ':') {
				// resume batch parsing on new label
				if (IsLabelStart(sc.chNext)) {
					sc.SetState(SCE_BAT_LABEL_LINE);
					levelNext--;
				}
			}
			break;
		}

		if (sc.state == SCE_BAT_DEFAULT) {
			if (sc.Match(':', ':') && command != Command::Echo) {
				sc.SetState(SCE_BAT_COMMENT);
			} else if (sc.atLineStart && sc.Match('#', '!')) {
				// shell shebang starts embedded script
				parenCount = 0;
				sc.SetState(SCE_BAT_NOT_BATCH);
				levelNext++;
			} else if (lineVisibleChars == 0 && sc.ch == ':') {
				parenCount = 0;
				if (IsLabelStart(sc.chNext) || IsWhiteSpace(sc.chNext)) {
					sc.SetState(SCE_BAT_LABEL_LINE);
				} else {
					// unreachable label starts skipped block
					sc.SetState(SCE_BAT_NOT_BATCH);
					levelNext++;
				}
			} else if (DetectBatchEscapeChar(sc, outerStyle, command) ||
				((sc.ch == '%' || sc.ch == '!') && DetectBatchVariable(sc, outerStyle, varQuoteChar, command, parenCount))) {
				if (logicalVisibleChars == 0 && command == Command::None) {
					command = Command::Argument;
				}
			} else if (sc.ch == '\"') {
				if (logicalVisibleChars == 0 && command == Command::None) {
					command = Command::Argument;
				}
				sc.SetState(SCE_BAT_STRINGDQ);
			} else if ((sc.ch == '\'' || sc.ch == '`') && (chPrevNonWhite == '(' && stylePrevNonWhite == SCE_BAT_OPERATOR)) {
				parenBefore = parenCount;
				sc.SetState((sc.ch == '\'') ? SCE_BAT_STRINGSQ : SCE_BAT_STRINGBT);
			} else if (sc.ch == '(' || sc.ch == ')') {
				const bool handled = (sc.ch == '(') ? (command != Command::Echo) : (parenCount > 0);
				if (handled || logicalVisibleChars == 0) {
					sc.SetState(SCE_BAT_OPERATOR);
					if (sc.ch == '(') {
						parenCount++;
						levelNext++;
					} else if (parenCount > 0) {
						parenCount--;
						levelNext--;
						if (parenCount < parenBefore) {
							command = Command::None;
						}
					}
				}
			} else if (IsBatchOperator(sc.ch, command)) {
				sc.SetState(SCE_BAT_OPERATOR);
				switch (sc.ch) {
				case '|':
				case '&':
					if (sc.ch == sc.chNext) {
						// cmd1 || cmd2, cmd1 && cmd2
						sc.Forward();
					}
					sc.ForwardSetState(SCE_BAT_DEFAULT);
					logicalVisibleChars = 0;
					command = Command::None;
					continue;

				case '>':
					if (sc.chNext == '&') {
						// output redirect: 2>&1
						sc.Forward();
					}
					command = Command::None;
					break;

				case '=':
					if (command == Command::Set) {
						command = Command::SetValue;
					}
					break;

				case '@':
					if (logicalVisibleChars == 0 && IsFileNameChar(sc.chNext)) {
						sc.Forward();
						if (sc.MatchIgnoreCase("rem") && !IsGraphic(sc.GetRelative(3))) {
							sc.ChangeState(SCE_BAT_COMMENT);
						} else {
							sc.SetState(SCE_BAT_IDENTIFIER);
						}
					}
					break;

				default:
					break;
				}
			} else if ((command == Command::Call && sc.ch == ':') || (command == Command::Goto && IsGraphic(sc.ch))) {
				command = Command::Argument;
				sc.SetState(SCE_BAT_LABEL);
			} else if ((logicalVisibleChars == 0 || command <= Command::Keyword) && IsFileNameChar(sc.ch)) {
				sc.SetState(SCE_BAT_IDENTIFIER);
			}
		}

		if (!isspacechar(sc.ch)) {
			logicalVisibleChars++;
			lineVisibleChars++;
			if (sc.state != SCE_BAT_DEFAULT) {
				chPrevNonWhite = sc.ch;
				stylePrevNonWhite = sc.state;
			}
		}
		if (sc.atLineEnd) {
			int lineState = parenCount << 8;
			if (stylePrevNonWhite == SCE_BAT_LINE_CONTINUATION) {
				lineState |= BatchLineStateLineContinuation;
				lineState |= static_cast<int>(command) << 4;
				sc.SetState(outerStyle);
			} else {
				lineState |= (lineVisibleChars == 0 || sc.state == SCE_BAT_COMMENT) ? BatchLineStateMaskEmptyLine : 0;
				command = Command::None;
				logicalVisibleChars = 0;
			}
			if (IsStringStyle(sc.state)) {
				if (stylePrevNonWhite != SCE_BAT_LINE_CONTINUATION && (sc.state == SCE_BAT_STRINGDQ || sc.state == SCE_BAT_STRINGNQ)) {
					const int state = TryTakeAndPop(nestedState);
					sc.SetState(state);
				}
				if (!nestedState.empty()) {
					lineState |= PackLineState(nestedState) << 16;
				}
			}

			styler.SetLineState(sc.currentLine, lineState);
			varQuoteChar = '\0';
			outerStyle = SCE_BAT_DEFAULT;
			lineVisibleChars = 0;

			if (fold) {
				if (sc.state == SCE_BAT_LABEL_LINE) {
					if (prevLineState & BatchLineStateMaskEmptyLine) {
						styler.SetLevel(sc.currentLine - 1, SC_FOLDLEVELBASE | (SC_FOLDLEVELBASE << 16));
					}
				}

				const int levelUse = levelCurrent;
				int lev = levelUse | levelNext << 16;
				if (levelUse < levelNext) {
					lev |= SC_FOLDLEVELHEADERFLAG;
				}
				if (lev != styler.LevelAt(sc.currentLine)) {
					styler.SetLevel(sc.currentLine, lev);
				}
				levelCurrent = levelNext;
				prevLineState = lineState;
			}
		}
		sc.Forward();
	}

	sc.Complete();
}

}

LexerModule lmBatch(SCLEX_BATCH, ColouriseBatchDoc, "batch");
