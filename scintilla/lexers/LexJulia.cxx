// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Julia.

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

struct EscapeSequence {
	int outerState = SCE_JULIA_DEFAULT;
	int digitsLeft = 0;
	bool hex = false;

	// highlight any character as escape sequence.
	bool resetEscapeState(int state, int chNext) noexcept {
		if (IsEOLChar(chNext)) {
			return false;
		}
		outerState = state;
		digitsLeft = 1;
		if (state == SCE_JULIA_RAWSTRING || state == SCE_JULIA_TRIPLE_RAWSTRING) {
			// TODO: only when backslash followed by double quote
			return chNext == '\\' || chNext == '\"';
		}

		hex = true;
		if (chNext == 'x') {
			digitsLeft = 3;
		} else if (chNext == 'u') {
			digitsLeft = 5;
		} else if (chNext == 'U') {
			digitsLeft = 9;
		} else if (IsOctalDigit(chNext)) {
			digitsLeft = 3;
			hex = false;
		}
		return true;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsOctalOrHex(ch, hex);
	}
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_CodeFolding = 1,
	KeywordIndex_Type = 2,
	KeywordIndex_Constant = 3,
	KeywordIndex_BasicFunction = 4,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = SCE_JULIA_DEFAULT,
	Keyword = SCE_JULIA_WORD,
	Type = SCE_JULIA_TYPE,
	Function = SCE_JULIA_FUNCTION_DEFINITION,
	Macro = SCE_JULIA_MACRO,
};

constexpr bool IsJuliaExponent(int base, int ch, int chNext) noexcept {
	return ((base == 10 && AnyOf<'E', 'e', 'F', 'f'>(ch))
		|| (base == 16 && (ch == 'p' || ch == 'P')))
		&& (chNext == '+' || chNext == '-' || IsADigit(chNext));
}

constexpr bool IsJuliaRegexFlag(int ch) noexcept {
	return ch == 'i' || ch == 'm' || ch == 's' || ch == 'x' || ch == 'a';
}

constexpr bool IsFormatSpecifier(char ch) noexcept {
	// copied from LexAwk
	return AnyOf(ch, 'a', 'A',
					'c',
					'd',
					'e', 'E',
					'f', 'F',
					'g', 'G',
					'i',
					'o',
					's',
					'u',
					'x', 'X');
}

// https://docs.julialang.org/en/v1/stdlib/Printf/#Printf.@sprintf
// https://en.cppreference.com/w/c/io/fprintf
Sci_Position CheckFormatSpecifier(const StyleContext &sc, LexAccessor &styler, bool insideUrl) noexcept {
	if (sc.chNext == '%') {
		return 2;
	}
	if (insideUrl && IsHexDigit(sc.chNext)) {
		// percent encoded URL string
		return 0;
	}
	if (IsASpaceOrTab(sc.chNext) && IsADigit(sc.chPrev)) {
		// ignore word after percent: "5% x"
		return 0;
	}

	Sci_PositionU pos = sc.currentPos + 1;
	char ch = styler[pos];
	// flags
	while (AnyOf(ch, '-', '+', ' ', '#', '0')) {
		ch = styler[++pos];
	}
	// width
	if (ch == '*') {
		ch = styler[++pos];
	} else {
		while (IsADigit(ch)) {
			ch = styler[++pos];
		}
	}
	// precision
	if (ch == '.') {
		ch = styler[++pos];
		if (ch == '*') {
			ch = styler[++pos];
		} else {
			while (IsADigit(ch)) {
				ch = styler[++pos];
			}
		}
	}
	// conversion format specifier
	if (IsFormatSpecifier(ch)) {
		return pos - sc.currentPos + 1;
	}
	return 0;
}

static_assert(DefaultNestedStateBaseStyle + 1 == SCE_JULIA_BACKTICKS);
static_assert(DefaultNestedStateBaseStyle + 2 == SCE_JULIA_TRIPLE_BACKTICKS);
static_assert(DefaultNestedStateBaseStyle + 3 == SCE_JULIA_STRING);
static_assert(DefaultNestedStateBaseStyle + 4 == SCE_JULIA_TRIPLE_STRING);

constexpr bool IsInterpolatedString(int state) noexcept {
	return state == SCE_JULIA_STRING
		|| state == SCE_JULIA_TRIPLE_STRING
		|| state == SCE_JULIA_BACKTICKS
		|| state == SCE_JULIA_TRIPLE_BACKTICKS;
}

constexpr bool IsTripleQuotedString(int state) noexcept {
	return (state & 1) == 0;
}

inline bool IsStringEnd(const StyleContext &sc) noexcept {
	if (sc.state < SCE_JULIA_STRING) {
		return sc.ch == '`' && (sc.state == SCE_JULIA_BACKTICKS || sc.MatchNext('`', '`'));
	}
	return sc.ch == '\"' && (!IsTripleQuotedString(sc.state) || sc.MatchNext('"', '"'));
}

void ColouriseJuliaDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateLineComment = 0;
	int commentLevel = 0;

	KeywordType kwType = KeywordType::None;
	bool maybeType = false;

	int braceCount = 0;
	std::vector<int> nestedState; // string interpolation "$()"

	int chBeforeIdentifier = 0;
	bool isTransposeOperator = false; // "'"

	bool insideUrl = false;
	int visibleChars = 0;
	int visibleCharsBefore = 0;
	EscapeSequence escSeq;
	int numBase = 0;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		int lineState = styler.GetLineState(sc.currentLine - 1);
		/*
		1: lineStateLineComment
		5: commentLevel
		8: braceCount
		3: nestedState count
		3*4: nestedState
		*/
		commentLevel = (lineState >> 1) & 0x1f;
		braceCount = (lineState >> 6) & 0xff;
		lineState >>= 14;
		if (lineState) {
			UnpackLineState(lineState, nestedState);
		}
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_JULIA_OPERATOR:
		case SCE_JULIA_OPERATOR2:
			sc.SetState(SCE_JULIA_DEFAULT);
			break;

		case SCE_JULIA_NUMBER:
			// strict to avoid color multiply expression as number
			if (!(IsADigitEx(sc.ch, numBase) || sc.ch == '_')) {
				if (IsJuliaExponent(numBase, sc.ch, sc.chNext)) {
					sc.Forward();
				} else if (!(numBase == 10 && sc.ch == '.' && sc.chPrev != '.')) {
					sc.SetState(SCE_JULIA_DEFAULT);
				}
			}
			break;

		case SCE_JULIA_IDENTIFIER:
		case SCE_JULIA_VARIABLE2:
		case SCE_JULIA_VARIABLE:
		case SCE_JULIA_MACRO:
		case SCE_JULIA_SYMBOL:
			if (!IsIdentifierCharEx(sc.ch)) {
				if (sc.state == SCE_JULIA_VARIABLE2) {
					sc.SetState(escSeq.outerState);
					continue;
				}
				if (sc.state == SCE_JULIA_IDENTIFIER) {
					char s[128];
					sc.GetCurrent(s, sizeof(s));
					if (keywordLists[KeywordIndex_Keyword].InList(s)) {
						if (StrEqual(s, "type")) {
							// only in `abstract type` or `primitive type`
							if (kwType == KeywordType::Keyword) {
								sc.ChangeState(SCE_JULIA_WORD);
								kwType = KeywordType::Type;
							}
						} else if (braceCount > 0 || (chBeforeIdentifier == '.' || chBeforeIdentifier == ':' || sc.ch == '.' || sc.ch == ':')) {
							sc.ChangeState(SCE_JULIA_WORD_DEMOTED);
						} else {
							isTransposeOperator = false;
							sc.ChangeState(SCE_JULIA_WORD);
							if (StrEqual(s, "struct")) {
								kwType = KeywordType::Type;
							} else if (StrEqual(s, "macro")) {
								kwType = KeywordType::Macro;
							} else if (StrEqual(s, "function")) {
								kwType = KeywordType::Function;
							} else if (StrEqualsAny(s, "abstract", "primitive")) {
								kwType = KeywordType::Keyword;
							}
						}
					} else if (keywordLists[KeywordIndex_Type].InList(s)) {
						sc.ChangeState(SCE_JULIA_TYPE);
					} else if (keywordLists[KeywordIndex_Constant].InList(s)) {
						sc.ChangeState(SCE_JULIA_CONSTANT);
					} else if (keywordLists[KeywordIndex_BasicFunction].InListPrefixed(s, '(')) {
						sc.ChangeState(SCE_JULIA_BASIC_FUNCTION);
					} else if (kwType != KeywordType::None && kwType != KeywordType::Keyword) {
						sc.ChangeState(static_cast<int>(kwType));
						if (kwType == KeywordType::Function && sc.ch == '!') {
							sc.Forward();
						}
					} else {
						const int chNext = sc.GetDocNextChar();
						if (sc.ch == '!' || chNext == '(') {
							sc.ChangeState(SCE_JULIA_FUNCTION);
							if (sc.ch == '!') {
								sc.Forward();
							}
						} else if (maybeType || chNext == '{') {
							sc.ChangeState(SCE_JULIA_TYPE);
						}
					}
					if (sc.state != SCE_JULIA_WORD) {
						kwType = KeywordType::None;
					}
					maybeType = false;
				}
				sc.SetState(SCE_JULIA_DEFAULT);
			}
			break;

		case SCE_JULIA_STRING:
		case SCE_JULIA_TRIPLE_STRING:
		case SCE_JULIA_BACKTICKS:
		case SCE_JULIA_TRIPLE_BACKTICKS:

		case SCE_JULIA_RAWSTRING:
		case SCE_JULIA_TRIPLE_RAWSTRING:
		case SCE_JULIA_BYTESTRING:
		case SCE_JULIA_TRIPLE_BYTESTRING:
			if (sc.ch == '\\') {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_JULIA_ESCAPECHAR);
					sc.Forward();
				}
			} else if (sc.ch == '%') {
				if (sc.state >= SCE_JULIA_STRING) {
					const Sci_Position length = CheckFormatSpecifier(sc, styler, insideUrl);
					if (length != 0) {
						const int state = sc.state;
						sc.SetState(SCE_JULIA_FORMAT_SPECIFIER);
						sc.Advance(length);
						sc.SetState(state);
						continue;
					}
				}
			} else if (sc.ch == '$') {
				if (IsInterpolatedString(sc.state)) {
					if (sc.chNext == '(') {
						++braceCount;
						nestedState.push_back(sc.state);
						sc.SetState(SCE_JULIA_OPERATOR2);
						sc.Forward();
					} else if (IsIdentifierStartEx(sc.chNext)) {
						escSeq.outerState = sc.state;
						sc.SetState(SCE_JULIA_VARIABLE2);
					}
				}
			} else if (IsStringEnd(sc)) {
				if (IsTripleQuotedString(sc.state)) {
					sc.Advance(2);
				}
				sc.ForwardSetState(SCE_JULIA_DEFAULT);
			} else if (sc.Match(':', '/', '/') && IsLowerCase(sc.chPrev)) {
				insideUrl = true;
			} else if (insideUrl && IsInvalidUrlChar(sc.ch)) {
				insideUrl = false;
			}
			break;

		case SCE_JULIA_CHARACTER:
			if (sc.atLineStart) {
				// multiline when inside backticks
				sc.SetState(SCE_JULIA_DEFAULT);
			} else if (sc.ch == '\\') {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_JULIA_ESCAPECHAR);
					sc.Forward();
				}
			} else if (sc.ch == '\'') {
				sc.ForwardSetState(SCE_JULIA_DEFAULT);
			}
			break;

		case SCE_JULIA_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_JULIA_REGEX:
		case SCE_JULIA_TRIPLE_REGEX:
			if (sc.ch == '\\') {
				sc.Forward();
			//} else if (sc.ch == '#') { // regex comment
			} else if (sc.ch == '\"' && (sc.state == SCE_JULIA_REGEX || sc.MatchNext('"', '"'))) {
				if (sc.state == SCE_JULIA_TRIPLE_REGEX) {
					sc.Advance(2);
				}
				sc.Forward();
				while (IsJuliaRegexFlag(sc.ch)) {
					sc.Forward();
				}
				sc.SetState(SCE_JULIA_DEFAULT);
			}
			break;

		case SCE_JULIA_COMMENTLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_JULIA_DEFAULT);
			} else {
				HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_JULIA_TASKMARKER);
			}
			break;

		case SCE_JULIA_COMMENTBLOCK:
			if (sc.Match('=', '#')) {
				sc.Forward();
				--commentLevel;
				if (commentLevel == 0) {
					sc.ForwardSetState(SCE_JULIA_DEFAULT);
				}
			} else if (sc.Match('#', '=')) {
				sc.Forward();
				++commentLevel;
			} else if (HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_JULIA_TASKMARKER)) {
				continue;
			}
			break;
		}

		if (sc.state == SCE_JULIA_DEFAULT) {
			const bool transposeOperator = isTransposeOperator && sc.ch == '\'';
			const bool symbol = !isTransposeOperator && sc.ch == ':'; // not range expression
			isTransposeOperator = false; // space not allowed before "'"
			if (transposeOperator) {
				sc.SetState(SCE_JULIA_OPERATOR);
			} else if (sc.ch == '#') {
				visibleCharsBefore = visibleChars;
				if (sc.chNext == '=') {
					commentLevel = 1;
					sc.SetState(SCE_JULIA_COMMENTBLOCK);
					sc.Forward();
				} else {
					sc.SetState(SCE_JULIA_COMMENTLINE);
					if (visibleChars == 0) {
						lineStateLineComment = SimpleLineStateMaskLineComment;
					}
				}
			} else if (sc.ch == '\"') {
				insideUrl = false;
				if (sc.MatchNext('"', '"')) {
					sc.SetState(SCE_JULIA_TRIPLE_STRING);
					sc.Advance(2);
				} else {
					sc.SetState(SCE_JULIA_STRING);
				}
			} else if (sc.ch == '\'') {
				sc.SetState(SCE_JULIA_CHARACTER);
			} else if (sc.ch == '`') {
				if (sc.MatchNext('`', '`')) {
					sc.SetState(SCE_JULIA_TRIPLE_BACKTICKS);
					sc.Advance(2);
				} else {
					sc.SetState(SCE_JULIA_BACKTICKS);
				}
			} else if (sc.Match('r', '\"')) {
				sc.SetState(SCE_JULIA_REGEX);
				sc.Forward();
				if (sc.MatchNext('"', '"')) {
					sc.ChangeState(SCE_JULIA_TRIPLE_REGEX);
					sc.Advance(2);
				}
			} else if (sc.Match('r', 'a', 'w', '"')) {
				insideUrl = false;
				sc.SetState(SCE_JULIA_RAWSTRING);
				sc.Advance(3);
				if (sc.MatchNext('"', '"')) {
					sc.ChangeState(SCE_JULIA_TRIPLE_RAWSTRING);
					sc.Advance(2);
				}
			} else if (sc.Match('b', '\"')) {
				insideUrl = false;
				sc.SetState(SCE_JULIA_BYTESTRING);
				sc.Forward();
				if (sc.MatchNext('"', '"')) {
					sc.ChangeState(SCE_JULIA_TRIPLE_BYTESTRING);
					sc.Advance(2);
				}
			} else if (sc.ch == '@' && IsIdentifierStartEx(sc.chNext)) {
				sc.SetState(SCE_JULIA_MACRO);
			} else if (symbol && IsIdentifierStartEx(sc.chNext)) {
				sc.SetState(SCE_JULIA_SYMBOL);
			} else if (sc.ch == '$' && IsIdentifierStartEx(sc.chNext)) {
				isTransposeOperator = true;
				sc.SetState(SCE_JULIA_VARIABLE);
			} else if (sc.ch == '0') {
				numBase = 10;
				isTransposeOperator = true;
				sc.SetState(SCE_JULIA_NUMBER);
				const int chNext = UnsafeLower(sc.chNext);
				if (chNext == 'x') {
					numBase = 16;
				} else if (chNext == 'b') {
					numBase = 2;
				} else if (chNext == 'o') {
					numBase = 8;
				}
				if (numBase != 10) {
					sc.Forward();
				}
			} else if (IsNumberStart(sc.ch, sc.chNext)) {
				numBase = 10;
				isTransposeOperator = true;
				sc.SetState(SCE_JULIA_NUMBER);
			} else if (IsIdentifierStartEx(sc.ch)) {
				chBeforeIdentifier = sc.chPrev;
				isTransposeOperator = true;
				sc.SetState(SCE_JULIA_IDENTIFIER);
			} else if (isoperator(sc.ch) || sc.ch == '@' || sc.ch == '\\' || sc.ch == '$') {
				if (sc.ch == '{' || sc.ch == '[' || sc.ch == '(') {
					++braceCount;
				} else if (sc.ch == '}' || sc.ch == ']' || sc.ch == ')') {
					--braceCount;
					isTransposeOperator = true;
				}

				sc.SetState(SCE_JULIA_OPERATOR);
				if (!nestedState.empty()) {
					if (sc.ch == '(') {
						nestedState.push_back(SCE_JULIA_DEFAULT);
					} else if (sc.ch == ')') {
						const int outerState = TakeAndPop(nestedState);
						if (outerState != SCE_JULIA_DEFAULT) {
							sc.ChangeState(SCE_JULIA_OPERATOR2);
						}
						sc.ForwardSetState(outerState);
						continue;
					}
				}

				if (sc.chNext == ':' && (sc.ch == ':' || sc.ch == '<' || sc.ch == '>')) {
					// type after ::, <:, >:
					maybeType = true;
					sc.Forward();
				}
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
		}
		if (sc.atLineEnd) {
			int lineState = (braceCount << 6) | (commentLevel << 1) | lineStateLineComment;
			if (!nestedState.empty()) {
				lineState |= PackLineState(nestedState) << 14;
			}
			styler.SetLineState(sc.currentLine, lineState);
			lineStateLineComment = 0;
			visibleChars = 0;
			visibleCharsBefore = 0;
			insideUrl = false;
		}
		sc.Forward();
	}

	sc.Complete();
}

constexpr int GetLineCommentState(int lineState) noexcept {
	return lineState & SimpleLineStateMaskLineComment;
}

constexpr bool IsMultilineStringStyle(int style) noexcept {
	return style == SCE_JULIA_STRING
		|| style == SCE_JULIA_TRIPLE_STRING
		|| style == SCE_JULIA_BACKTICKS
		|| style == SCE_JULIA_TRIPLE_BACKTICKS
		|| style == SCE_JULIA_RAWSTRING
		|| style == SCE_JULIA_TRIPLE_RAWSTRING
		|| style == SCE_JULIA_BYTESTRING
		|| style == SCE_JULIA_TRIPLE_BYTESTRING
		|| style == SCE_JULIA_OPERATOR2
		|| style == SCE_JULIA_VARIABLE2
		|| style == SCE_JULIA_ESCAPECHAR
		|| style == SCE_JULIA_FORMAT_SPECIFIER;
}

void FoldJuliaDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	int lineCommentPrev = 0;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		lineCommentPrev = GetLineCommentState(styler.GetLineState(lineCurrent - 1));
	}

	int levelNext = levelCurrent;
	int lineCommentCurrent = GetLineCommentState(styler.GetLineState(lineCurrent));
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);
	Sci_PositionU lineEndPos = sci::min(lineStartNext, endPos);

	char buf[12]; // baremodule
	constexpr int MaxFoldWordLength = sizeof(buf) - 1;
	int wordLen = 0;

	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;

	while (startPos < endPos) {
		const char ch = chNext;
		const int stylePrev = style;
		style = styleNext;
		chNext = styler[++startPos];
		styleNext = styler.StyleAt(startPos);

		switch (style) {
		case SCE_JULIA_COMMENTBLOCK: {
			const int level = (ch == '#' && chNext == '=') ? 1 : ((ch == '=' && chNext == '#') ? -1 : 0);
			if (level != 0) {
				levelNext += level;
				startPos++;
				style = styleNext;
				chNext = styler[startPos];
				styleNext = styler.StyleAt(startPos);
			}
		} break;

		case SCE_JULIA_WORD:
			if (wordLen < MaxFoldWordLength) {
				buf[wordLen++] = ch;
			}
			if (styleNext != SCE_JULIA_WORD) {
				buf[wordLen] = '\0';
				wordLen = 0;
				if (StrEqual(buf, "end")) {
					levelNext--;
				} else if (keywordLists[KeywordIndex_CodeFolding].InList(buf)) {
					levelNext++;
				}
			}
			break;

		case SCE_JULIA_OPERATOR:
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
			break;

		case SCE_JULIA_STRING:
		case SCE_JULIA_TRIPLE_STRING:
		case SCE_JULIA_BACKTICKS:
		case SCE_JULIA_TRIPLE_BACKTICKS:
		case SCE_JULIA_RAWSTRING:
		case SCE_JULIA_TRIPLE_RAWSTRING:
		case SCE_JULIA_BYTESTRING:
		case SCE_JULIA_TRIPLE_BYTESTRING:
			if (!IsMultilineStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsMultilineStringStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_JULIA_REGEX:
		case SCE_JULIA_TRIPLE_REGEX:
			if (style != stylePrev) {
				levelNext++;
			} else if (style != styleNext) {
				levelNext--;
			}
			break;
		}

		if (startPos == lineEndPos) {
			const int lineCommentNext = GetLineCommentState(styler.GetLineState(lineCurrent + 1));
			if (lineCommentCurrent) {
				levelNext += lineCommentNext - lineCommentPrev;
			}

			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (levelUse < levelNext) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}

			lineCurrent++;
			lineStartNext = styler.LineStart(lineCurrent + 1);
			lineEndPos = sci::min(lineStartNext, endPos);
			levelCurrent = levelNext;
			lineCommentPrev = lineCommentCurrent;
			lineCommentCurrent = lineCommentNext;
		}
	}
}

}

LexerModule lmJulia(SCLEX_JULIA, ColouriseJuliaDoc, "julia", FoldJuliaDoc);
