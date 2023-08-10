// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for TOML.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"

using namespace Lexilla;

namespace {

struct EscapeSequence {
	int outerState = SCE_TOML_DEFAULT;
	int digitsLeft = 0;

	// highlight any character as escape sequence.
	bool resetEscapeState(int state, int chNext) noexcept {
		if (IsEOLChar(chNext)) {
			return false;
		}
		outerState = state;
		digitsLeft = 1;
		if (chNext == 'x') {
			digitsLeft = 3;
		} else if (chNext == 'u') {
			digitsLeft = 5;
		} else if (chNext == 'U') {
			digitsLeft = 9;
		}
		return true;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsHexDigit(ch);
	}
};

constexpr bool IsTripleString(int state) noexcept {
	return state > SCE_TOML_STRING_DQ;
}

constexpr bool IsDoubleQuoted(int state) noexcept {
	if constexpr (SCE_TOML_STRING_DQ & 1) {
		return state & true;
	} else {
		return (state & 1) == 0;
	}
}

constexpr int GetStringQuote(int state) noexcept {
	return IsDoubleQuoted(state) ? '\"' : '\'';
}

constexpr bool IsTOMLOperator(int ch) noexcept {
	return AnyOf(ch, '[', ']', '{', '}', ',', '=', '.', '+', '-');
}

constexpr bool IsTOMLUnquotedKey(int ch) noexcept {
	return IsIdentifierChar(ch) || ch == '-';
}

bool IsTOMLKey(StyleContext& sc, int braceCount, const WordList *kwList) {
	if (braceCount) {
		const int chNext = sc.GetDocNextChar();
		if (chNext == '=' || chNext == '.' || chNext == '-') {
			sc.ChangeState(SCE_TOML_KEY);
			return true;
		}
	}
	if (sc.state == SCE_TOML_IDENTIFIER) {
		char s[8];
		sc.GetCurrentLowered(s, sizeof(s));
#if defined(__clang__)
		__builtin_assume(kwList != nullptr); // suppress [clang-analyzer-core.CallAndMessage]
#endif
		if (kwList->InList(s)) {
			sc.ChangeState(SCE_TOML_KEYWORD);
		}
	}
	sc.SetState(SCE_TOML_DEFAULT);
	return false;
}

enum class TOMLLineType {
	None = 0,
	Table,
	CommentLine,
};

enum class TOMLKeyState {
	Unquoted = 0,
	Literal,
	Quoted,
	End,
};

void ColouriseTOMLDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int visibleChars = 0;
	int tableLevel = 0;
	int braceCount = 0;
	TOMLLineType lineType = TOMLLineType::None;
	TOMLKeyState keyState = TOMLKeyState::Unquoted;
	EscapeSequence escSeq;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		const int lineState = styler.GetLineState(sc.currentLine - 1);
		/*
		2: lineType
		8: tableLevel
		8: braceCount
		*/
		braceCount = (lineState >> 10) & 0xff;
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_TOML_OPERATOR:
			sc.SetState(SCE_TOML_DEFAULT);
			break;

		case SCE_TOML_NUMBER:
			if (!IsDecimalNumber(sc.chPrev, sc.ch, sc.chNext)) {
				if (IsISODateTime(sc.ch, sc.chNext)) {
					sc.ChangeState(SCE_TOML_DATETIME);
				} else if (IsTOMLKey(sc, braceCount, nullptr)) {
					continue;
				}
			}
			break;

		case SCE_TOML_DATETIME:
			if (!(IsIdentifierChar(sc.ch) || IsISODateTime(sc.ch, sc.chNext))) {
				if (IsTOMLKey(sc, braceCount, nullptr)) {
					continue;
				}
			}
			break;

		case SCE_TOML_IDENTIFIER:
			if (!IsIdentifierChar(sc.ch)) {
				if (IsTOMLKey(sc, braceCount, &keywordLists[0])) {
					continue;
				}
			}
			break;

		case SCE_TOML_TABLE:
		case SCE_TOML_KEY:
			if (sc.atLineStart) {
				sc.SetState(SCE_TOML_DEFAULT);
			} else {
				switch (keyState) {
				case TOMLKeyState::Literal:
					if (sc.ch == '\'') {
						sc.Forward();
						keyState = TOMLKeyState::Unquoted;
					}
					break;
				case TOMLKeyState::Quoted:
					if (sc.ch == '\\') {
						sc.Forward();
					} else if (sc.ch == '\"') {
						sc.Forward();
						keyState = TOMLKeyState::Unquoted;
					}
					break;
				default:
					break;
				}
				if (keyState == TOMLKeyState::Unquoted) {
					if (sc.ch == '\'') {
						keyState = TOMLKeyState::Literal;
					} else if (sc.ch == '\"') {
						keyState = TOMLKeyState::Quoted;
					} else if (sc.ch == '.') {
						if (sc.state == SCE_TOML_KEY) {
							sc.SetState(SCE_TOML_OPERATOR);
							sc.ForwardSetState(SCE_TOML_KEY);
						} else {
							++tableLevel;
						}
					} else if (sc.state == SCE_TOML_KEY && sc.ch == '=') {
						keyState = TOMLKeyState::End;
						sc.SetState(SCE_TOML_OPERATOR);
					} else if (sc.state == SCE_TOML_TABLE && sc.ch == ']') {
						keyState = TOMLKeyState::End;
						sc.Forward();
						if (sc.ch == ']') {
							sc.Forward();
						}
						const int chNext = sc.GetLineNextChar();
						if (chNext == '#') {
							sc.SetState(SCE_TOML_DEFAULT);
						}
					} else if (sc.state == SCE_TOML_KEY && !IsTOMLUnquotedKey(sc.ch)) {
						const int chNext = sc.GetLineNextChar();
						if (!AnyOf(chNext, '\'', '\"', '.', '=')) {
							sc.ChangeState(SCE_TOML_ERROR);
							continue;
						}
					}
				}
			}
			break;

		case SCE_TOML_STRING_SQ:
		case SCE_TOML_STRING_DQ:
		case SCE_TOML_TRIPLE_STRING_SQ:
		case SCE_TOML_TRIPLE_STRING_DQ:
			if (sc.atLineStart && !IsTripleString(sc.state)) {
				sc.SetState(SCE_TOML_DEFAULT);
			} else if (sc.ch == '\\' && IsDoubleQuoted(sc.state)) {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_TOML_ESCAPECHAR);
					sc.Forward();
				}
			} else if (sc.ch == GetStringQuote(sc.state) && (!IsTripleString(sc.state) || sc.MatchNext())) {
				if (IsTripleString(sc.state)) {
					sc.Advance(2);
				}
				sc.Forward();
				if (!IsTripleString(sc.state) && IsTOMLKey(sc, braceCount, nullptr)) {
					continue;
				}
				sc.SetState(SCE_TOML_DEFAULT);
			}
			break;

		case SCE_TOML_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_TOML_ERROR:
			if (sc.atLineStart) {
				sc.SetState(SCE_TOML_DEFAULT);
			} else if (sc.ch == '#') {
				sc.SetState(SCE_TOML_COMMENT);
			} else if (sc.ch == ',') {
				sc.SetState(SCE_TOML_OPERATOR);
				sc.ForwardSetState(SCE_TOML_ERROR);
			}
			break;

		case SCE_TOML_COMMENT:
			if (sc.atLineStart) {
				sc.SetState(SCE_TOML_DEFAULT);
			}
			break;
		}

		if (sc.state == SCE_TOML_DEFAULT) {
			if (visibleChars == 0 && !braceCount) {
				if (sc.ch == '#') {
					sc.SetState(SCE_TOML_COMMENT);
					lineType = TOMLLineType::CommentLine;
				} else if (sc.ch == '[') {
					tableLevel = 0;
					sc.SetState(SCE_TOML_TABLE);
					if (sc.chNext == '[') {
						sc.Forward();
					}
					keyState = TOMLKeyState::Unquoted;
					lineType = TOMLLineType::Table;
				} else if (sc.ch == '\'' || sc.ch == '\"') {
					sc.SetState(SCE_TOML_KEY);
					keyState = (sc.ch == '\'')? TOMLKeyState::Literal : TOMLKeyState::Quoted;
				} else if (IsTOMLUnquotedKey(sc.ch)) {
					sc.SetState(SCE_TOML_KEY);
					keyState = TOMLKeyState::Unquoted;
				} else if (!isspacechar(sc.ch)) {
					// each line must be: key = value
					sc.SetState(SCE_TOML_ERROR);
				}
			} else {
				if (sc.ch == '#') {
					sc.SetState(SCE_TOML_COMMENT);
					if (visibleChars == 0) {
						lineType = TOMLLineType::CommentLine;
					}
				} else if (sc.ch == '\'') {
					if (sc.MatchNext('\'', '\'')) {
						sc.SetState(SCE_TOML_TRIPLE_STRING_SQ);
						sc.Advance(2);
					} else {
						sc.SetState(SCE_TOML_STRING_SQ);
					}
				} else if (sc.ch == '"') {
					if (sc.MatchNext('"', '"')) {
						sc.SetState(SCE_TOML_TRIPLE_STRING_DQ);
						sc.Advance(2);
					} else {
						sc.SetState(SCE_TOML_STRING_DQ);
					}
				} else if (IsADigit(sc.ch)) {
					sc.SetState(SCE_TOML_NUMBER);
				} else if (IsLowerCase(sc.ch)) {
					sc.SetState(SCE_TOML_IDENTIFIER);
				} else if (IsTOMLOperator(sc.ch)) {
					sc.SetState(SCE_TOML_OPERATOR);
					if (AnyOf<'[', ']', '{', '}'>(sc.ch)) {
						braceCount += (('[' + ']')/2 + (sc.ch & 32)) - sc.ch;
					}
				} else if (braceCount && IsTOMLUnquotedKey(sc.ch)) {
					// Inline Table
					sc.SetState(SCE_TOML_KEY);
					keyState = TOMLKeyState::Unquoted;
				}
			}
		}

		if (visibleChars == 0 && !isspacechar(sc.ch)) {
			++visibleChars;
		}
		if (sc.atLineEnd) {
			const int lineState = (tableLevel << 2) | (braceCount << 10) | static_cast<int>(lineType);
			styler.SetLineState(sc.currentLine, lineState);
			lineType = TOMLLineType::None;
			visibleChars = 0;
			tableLevel = 0;
			keyState = TOMLKeyState::Unquoted;
		}
		sc.Forward();
	}

	sc.Complete();
}

constexpr TOMLLineType GetLineType(int lineState) noexcept {
	return static_cast<TOMLLineType>(lineState & 3);
}

constexpr int GetTableLevel(int lineState) noexcept {
	return (lineState >> 2) & 0xff;
}

// code folding based on LexProps
void FoldTOMLDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int /*initStyle*/, LexerWordList, Accessor &styler) {
	const Sci_Line endPos = startPos + lengthDoc;
	const Sci_Line maxLines = styler.GetLine((endPos == styler.Length()) ? endPos : endPos - 1);

	Sci_Line lineCurrent = styler.GetLine(startPos);

	int prevLevel = SC_FOLDLEVELBASE;
	TOMLLineType prevType = TOMLLineType::None;
	TOMLLineType prev2Type = TOMLLineType::None;
	if (lineCurrent > 0) {
		prevLevel = styler.LevelAt(lineCurrent - 1);
		prevType = GetLineType(styler.GetLineState(lineCurrent - 1));
		prev2Type = GetLineType(styler.GetLineState(lineCurrent - 2));
	}

	bool commentHead = (prevType == TOMLLineType::CommentLine) && (prevLevel & SC_FOLDLEVELHEADERFLAG);
	while (lineCurrent <= maxLines) {
		int nextLevel;
		const int lineState = styler.GetLineState(lineCurrent);
		const TOMLLineType lineType = GetLineType(lineState);

		if (lineType == TOMLLineType::CommentLine) {
			if (prevLevel & SC_FOLDLEVELHEADERFLAG) {
				nextLevel = (prevLevel & SC_FOLDLEVELNUMBERMASK) + 1;
			} else {
				nextLevel = prevLevel;
			}
			commentHead = prevType != TOMLLineType::CommentLine;
			nextLevel |= commentHead ? SC_FOLDLEVELHEADERFLAG : 0;
		} else {
			if (lineType == TOMLLineType::Table) {
				nextLevel = SC_FOLDLEVELBASE + GetTableLevel(lineState);
				if ((prevType == TOMLLineType::CommentLine) && prevLevel <= nextLevel) {
					// comment above nested table
					commentHead = true;
					prevLevel = nextLevel - 1;
				} else if ((prevType == TOMLLineType::Table) && (prevLevel & SC_FOLDLEVELNUMBERMASK) >= nextLevel) {
					commentHead = true; // empty table
				}
				nextLevel |= SC_FOLDLEVELHEADERFLAG;
			} else {
				if (commentHead) {
					nextLevel = prevLevel & SC_FOLDLEVELNUMBERMASK;
				} else if (prevLevel & SC_FOLDLEVELHEADERFLAG) {
					nextLevel = (prevLevel & SC_FOLDLEVELNUMBERMASK) + 1;
				} else if ((prevType == TOMLLineType::CommentLine) && (prev2Type == TOMLLineType::CommentLine)) {
					nextLevel = prevLevel - 1;
				} else {
					nextLevel = prevLevel;
				}
			}

			if (commentHead) {
				commentHead = false;
				styler.SetLevel(lineCurrent - 1, prevLevel & SC_FOLDLEVELNUMBERMASK);
			}
		}

		if (nextLevel != styler.LevelAt(lineCurrent)) {
			styler.SetLevel(lineCurrent, nextLevel);
		}

		prevLevel = nextLevel;
		prev2Type = prevType;
		prevType = lineType;
		lineCurrent++;
	}
}

}

LexerModule lmTOML(SCLEX_TOML, ColouriseTOMLDoc, "toml", FoldTOMLDoc);
