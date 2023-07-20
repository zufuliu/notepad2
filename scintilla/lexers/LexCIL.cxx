// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for MSIL, CIL

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
#include "CharacterSet.h"
#include "LexerModule.h"

using namespace Lexilla;

namespace {

constexpr bool IsCILOp(int ch) noexcept {
	return ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '=' || ch == '&'
		|| ch == ':' || ch == ',' || ch == '+' || ch == '-' || ch == '.';
}
constexpr bool IsCILWordChar(int ch) noexcept {
	return iswordchar(ch) || ch == '-' || ch == '/' || ch == '$';
}

#define MAX_WORD_LENGTH	31
void ColouriseCILDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const WordList &keywords = keywordLists[0];
	const WordList &keywords2 = keywordLists[1];
	const WordList &kwInstruction = keywordLists[10];

	int state = initStyle;
	int ch = 0;
	int chNext = styler[startPos];
	styler.StartAt(startPos);
	styler.StartSegment(startPos);
	const Sci_PositionU endPos = startPos + length;

	Sci_Line lineCurrent = styler.GetLine(startPos);
	char buf[MAX_WORD_LENGTH + 1] = "";
	int wordLen = 0;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const int chPrev = ch;
		ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);

		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');
		const bool atLineStart = i == static_cast<Sci_PositionU>(styler.LineStart(lineCurrent));
		if (atEOL || i == endPos - 1) {
			lineCurrent++;
		}

		switch (state) {
		case SCE_C_OPERATOR:
			styler.ColorTo(i, state);
			state = SCE_C_DEFAULT;
			break;
		case SCE_C_NUMBER:
			if (!IsDecimalNumber(chPrev, ch, chNext)) {
				styler.ColorTo(i, state);
				state = SCE_C_DEFAULT;
			}
			break;
		case SCE_C_DIRECTIVE:
		case SCE_C_ASM_INSTRUCTION:
			if (!IsCILWordChar(ch)) {
				styler.ColorTo(i, state);
				state = SCE_C_DEFAULT;
			}
			break;
		case SCE_C_IDENTIFIER:
			if (!(iswordstart(ch) || ch == '$')) {
				buf[wordLen] = '\0';
				state = SCE_C_DEFAULT;
				if (keywords.InList(buf)) {
					styler.ColorTo(i, SCE_C_WORD);
				} else if (keywords2.InList(buf)) {
					styler.ColorTo(i, SCE_C_WORD2);
				} else if (kwInstruction.InList(buf)) {
					state = SCE_C_ASM_INSTRUCTION;
				} else if (ch == ':' && chNext != ':') {
					styler.ColorTo(i, SCE_C_LABEL);
				}
			} else if (wordLen < MAX_WORD_LENGTH) {
				buf[wordLen++] = static_cast<char>(ch);
			}
			break;
		case SCE_C_STRING:
		case SCE_C_CHARACTER:
			if (atLineStart) {
				styler.ColorTo(i, state);
				state = SCE_C_DEFAULT;
			} else if (ch == '\\' && (chNext == '\\' || chNext == '\"')) {
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if ((state == SCE_C_STRING && ch == '\"') || (state == SCE_C_CHARACTER && ch == '\'')) {
				styler.ColorTo(i + 1, state);
				state = SCE_C_DEFAULT;
				continue;
			}
			break;
		case SCE_C_COMMENTLINE:
			if (atLineStart) {
				styler.ColorTo(i, state);
				state = SCE_C_DEFAULT;
			}
			break;
		case SCE_C_COMMENT:
			if (ch == '*' && chNext == '/') {
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
				styler.ColorTo(i + 1, state);
				state = SCE_C_DEFAULT;
				continue;
			}
			break;
		}

		if (state == SCE_C_DEFAULT) {
			if (ch == '/' && chNext == '/') {
				styler.ColorTo(i, state);
				state = SCE_C_COMMENTLINE;
			} else if (ch == '/' && chNext == '*') {
				styler.ColorTo(i, state);
				state = SCE_C_COMMENT;
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if (ch == '\"') {
				styler.ColorTo(i, state);
				state = SCE_C_STRING;
			} else if (ch == '\'') {
				styler.ColorTo(i, state);
				state = SCE_C_CHARACTER;
			} else if (IsNumberStart(ch, chNext)) {
				styler.ColorTo(i, state);
				state = SCE_C_NUMBER;
			} else if (ch == '.' && IsAlpha(chNext) && (IsCILOp(chPrev) || IsASpace(chPrev))) {
				styler.ColorTo(i, state);
				state = SCE_C_DIRECTIVE;
			} else if (iswordstart(ch)) {
				styler.ColorTo(i, state);
				state = SCE_C_IDENTIFIER;
				buf[0] = static_cast<char>(ch);
				wordLen = 1;
			} else if (IsCILOp(ch)) {
				styler.ColorTo(i, state);
				state = SCE_C_OPERATOR;
			}
		}
	}

	// Colourise remaining document
	styler.ColorTo(endPos, state);
}

#define IsCommentLine(line)			IsLexCommentLine(styler, line, SCE_C_COMMENTLINE)
#define IsStreamCommentStyle(style)	((style) == SCE_C_COMMENT)
void FoldCILDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList, Accessor &styler) {
	const Sci_PositionU endPos = startPos + length;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0)
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
	int levelNext = levelCurrent;

	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const char ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);
		const int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(i + 1);
		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');

		if (atEOL && IsCommentLine(lineCurrent)) {
			levelNext += IsCommentLine(lineCurrent + 1) - IsCommentLine(lineCurrent - 1);
		}
		if (IsStreamCommentStyle(style) && !IsCommentLine(lineCurrent)) {
			if (!IsStreamCommentStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamCommentStyle(styleNext) && !atEOL) {
				levelNext--;
			}
		}

		if (style == SCE_C_OPERATOR) {
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
		}

		if (atEOL || (i == endPos - 1)) {
			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (levelUse < levelNext)
				lev |= SC_FOLDLEVELHEADERFLAG;
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}
			lineCurrent++;
			levelCurrent = levelNext;
		}
	}
}

}

LexerModule lmCIL(SCLEX_CIL, ColouriseCILDoc, "cil", FoldCILDoc);
