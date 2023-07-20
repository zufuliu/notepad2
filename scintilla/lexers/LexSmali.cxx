// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for javap, Jasmin, Android Dalvik Smali.

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
#include "StringUtils.h"
#include "LexerModule.h"

using namespace Lexilla;

namespace {

constexpr bool IsSmaliOp(int ch) noexcept {
	return ch == ';' || ch == '{' || ch == '}' || ch == '(' || ch == ')' || ch == '='
		|| ch == ',' || ch == '<' || ch == '>' || ch == '+' || ch == '-' || ch == ':' || ch == '.'
		|| ch == '/' || ch == '&' || ch == '|' || ch == '^' || ch == '!' || ch == '~' || ch == '*' || ch == '%';
}
constexpr bool IsDelimiter(int ch) noexcept {
	return ch != '/' && ch != '$' && (IsASpace(ch) || IsSmaliOp(ch));
}
constexpr bool IsSmaliWordChar(int ch) noexcept {
	return iswordstart(ch) || ch == '-';
}
constexpr bool IsSmaliWordCharX(int ch) noexcept {
	return iswordchar(ch) || ch == '-';
}

constexpr bool IsOpcodeChar(int ch) noexcept {
	return ch == '_' || ch == '-' || IsAlpha(ch);
}

constexpr bool IsJavaTypeChar(int ch) noexcept {
	return ch == 'V'	// void
		|| ch == 'Z'	// boolean
		|| ch == 'B'	// byte
		|| ch == 'S'	// short
		|| ch == 'C'	// char
		|| ch == 'I'	// int
		|| ch == 'J'	// long
		|| ch == 'F'	// float
		|| ch == 'D'	// double
		|| ch == 'L'	// object
		|| ch == '[';	// array
}
bool IsJavaType(int ch, int chPrev, int chNext) noexcept {
	if (chPrev == 'L' || chPrev == '>' || chPrev == '/' || chPrev == '.')
		return false;
	if (ch == '[')
		return IsJavaTypeChar(chNext);
	if (!IsJavaTypeChar(ch))
		return false;
	if (ch == 'L') {
		if (!(iswordstart(chNext) || chNext == '$'))
			return false;
		return IsDelimiter(chPrev) || IsJavaTypeChar(chPrev);
	}
	return IsSmaliOp(chPrev) || IsJavaTypeChar(chPrev) || (IsDelimiter(chPrev) && IsDelimiter(chNext));
}

#define kWordType_Directive 1
#define kWrodType_Field		2
#define kWordType_Method	3

#define MAX_WORD_LENGTH	31
void ColouriseSmaliDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const WordList &keywords = keywordLists[0];
	//const WordList &kwInstruction = keywordLists[10];

	int state = initStyle;
	int ch = 0;
	int chNext = styler[startPos];
	styler.StartAt(startPos);
	styler.StartSegment(startPos);
	const Sci_PositionU endPos = startPos + length;

	Sci_Line lineCurrent = styler.GetLine(startPos);
	int curLineState = (lineCurrent > 0) ? styler.GetLineState(lineCurrent - 1) : 0;
	char buf[MAX_WORD_LENGTH + 1] = "";
	int wordLen = 0;
	int visibleChars = 0;
	int nextWordType = 0;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const int chPrev = ch;
		ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);

		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');
		const bool atLineStart = i == static_cast<Sci_PositionU>(styler.LineStart(lineCurrent));

		switch (state) {
		case SCE_SMALI_OPERATOR:
			styler.ColorTo(i, state);
			state = SCE_SMALI_DEFAULT;
			break;
		case SCE_SMALI_NUMBER:
			if (!IsDecimalNumber(chPrev, ch, chNext)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_DEFAULT;
			}
			break;
		case SCE_SMALI_STRING:
		case SCE_SMALI_CHARACTER:
			if (atLineStart) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_DEFAULT;
			} else if (ch == '\\' && (chNext == '\\' || chNext == '\"')) {
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if ((state == SCE_SMALI_STRING && ch == '\"') || (state == SCE_SMALI_CHARACTER && ch == '\'')) {
				styler.ColorTo(i + 1, state);
				state = SCE_SMALI_DEFAULT;
				continue;
			}
			break;
		case SCE_SMALI_DIRECTIVE:
			if (!IsSmaliWordChar(ch)) {
				buf[wordLen] = '\0';
				if (buf[0] == '.') {
					const char *p = buf + 1;
					if (StrEqualsAny(p, "end", "restart", "limit")) {
						nextWordType = kWordType_Directive;
					} else if (StrEqual(p, "field")) {
						nextWordType = kWrodType_Field;
					} else if (StrEqual(p, "method")) {
						nextWordType = kWordType_Method;
					}
					if (StrEqualsAny(p, "annotation", "subannotation")) {
						curLineState = 1;
					} else if (StrEqualsAny(p, "packed-switch", "sparse-switch")) {
						curLineState = 2;
					} else if (StrEqual(p, "end")) {
						curLineState = 0;
					}
				}
				styler.ColorTo(i, state);
				state = SCE_SMALI_DEFAULT;
			} else if (wordLen < MAX_WORD_LENGTH) {
				buf[wordLen++] = static_cast<char>(ch);
			}
			break;
		case SCE_SMALI_REGISTER:
			if (!IsADigit(ch)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_DEFAULT;
			}
			break;
		case SCE_SMALI_INSTRUCTION:
			if (!(IsSmaliWordChar(ch) || ch == '/')) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_DEFAULT;
			}
			break;
		case SCE_SMALI_LABEL:
			if (!iswordchar(ch)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_DEFAULT;
				visibleChars = 0;
			}
			break;
		case SCE_SMALI_TYPE:
			styler.ColorTo(i, state);
			state = SCE_SMALI_DEFAULT;
			break;
		case SCE_SMALI_IDENTIFIER:
			if (!IsSmaliWordChar(ch)) {
				buf[wordLen] = '\0';
				if (nextWordType == kWordType_Directive) {
					nextWordType = 0;
					styler.ColorTo(i, SCE_SMALI_DIRECTIVE);
					state = SCE_SMALI_DEFAULT;
				} else if (ch == ':' && IsDelimiter(chNext)) {
					state = SCE_SMALI_LABEL;
				} else if (keywords.InList(buf)) {
					styler.ColorTo(i, SCE_SMALI_WORD);
					state = SCE_SMALI_DEFAULT;
				} else if (nextWordType == kWrodType_Field) {
					nextWordType = 0;
					styler.ColorTo(i, SCE_SMALI_FIELD);
					if (ch == '$')
						styler.ColorTo(i, SCE_SMALI_OPERATOR);
					state = (ch == ':') ? SCE_SMALI_DEFAULT : SCE_SMALI_FIELD;
				} else if (nextWordType == kWordType_Method) {
					nextWordType = 0;
					styler.ColorTo(i, SCE_SMALI_METHOD);
					if (ch == '$')
						styler.ColorTo(i, SCE_SMALI_OPERATOR);
					state = (ch == '(' || ch == '>') ? SCE_SMALI_DEFAULT : SCE_SMALI_METHOD;
				} else if (/*kwInstruction.InList(buf)*/ wordLen == visibleChars && !curLineState && (IsASpace(ch) || ch == '/')) {
					if (!(IsSmaliWordChar(ch) || ch == '/')) {
						styler.ColorTo(i, SCE_SMALI_INSTRUCTION);
						state = SCE_SMALI_DEFAULT;
					} else {
						state = SCE_SMALI_INSTRUCTION;
					}
				} else {
					Sci_PositionU pos = i;
					while (pos < endPos && IsASpace(styler.SafeGetCharAt(pos))) ++pos;
					if (styler.SafeGetCharAt(pos) == '(') {
						styler.ColorTo(i, SCE_SMALI_METHOD);
					}
					state = SCE_SMALI_DEFAULT;
				}
			} else if (wordLen < MAX_WORD_LENGTH) {
				buf[wordLen++] = static_cast<char>(ch);
			}
			break;
		case SCE_SMALI_LABEL_EOL:	// packed-switch sparse-switch
		case SCE_SMALI_COMMENTLINE:
			if (atLineStart) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_DEFAULT;
			}
			break;
		case SCE_SMALI_FIELD:
		case SCE_SMALI_METHOD:
			if (ch == '$') {
				styler.ColorTo(i, state);
				styler.ColorTo(i + 1, SCE_SMALI_OPERATOR);
			} else if (ch == ':' || ch == '(' || IsASpace(ch)) {
				styler.ColorTo(i, (ch == ':' ? SCE_SMALI_FIELD : SCE_SMALI_METHOD));
				state = SCE_SMALI_DEFAULT;
			}
			break;
		}

		if (state == SCE_SMALI_DEFAULT) {
			if (ch == '#') {
				styler.ColorTo(i, state);
				if (IsADigit(chNext)) { // javap
					state = SCE_SMALI_NUMBER;
				} else {
					state = SCE_SMALI_COMMENTLINE;
				}
			} else if (ch == '/' && chNext == '/') { // javap
				styler.ColorTo(i, state);
				state = SCE_SMALI_COMMENTLINE;
			} else if (ch == ';' && !(chPrev == '>' || iswordchar(chPrev)) && !IsEOLChar(chNext)) { // jasmin
				styler.ColorTo(i, state);
				state = SCE_SMALI_COMMENTLINE;
			} else if (ch == '\"') {
				styler.ColorTo(i, state);
				state = SCE_SMALI_STRING;
			} else if (ch == '\'') {
				styler.ColorTo(i, state);
				state = SCE_SMALI_CHARACTER;
			} else if (IsNumberStart(ch, chNext)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_NUMBER;
			} else if (ch == '.' && visibleChars == 0 && IsAlpha(chNext)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_DIRECTIVE;
				buf[0] = static_cast<char>(ch);
				wordLen = 1;
			} else if ((ch == 'v' || ch == 'p') && IsADigit(chNext)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_REGISTER;
			} else if (!curLineState && visibleChars == 0 && ch == ':' && (chNext == 's' || chNext == 'p')) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_LABEL_EOL; // packed-switch sparse-switch
			} else if (ch == ':' && IsDelimiter(chPrev) && iswordstart(chNext)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_LABEL;
			} else if ((visibleChars > 0 || (curLineState && visibleChars == 0 && ch == 'L')) && IsJavaType(ch, chPrev, chNext)) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_TYPE;
			} else if (chPrev == '-' && ch == '>') { // field/mathod
				styler.ColorTo(i + 1, SCE_SMALI_OPERATOR);
				state = SCE_SMALI_FIELD;
			} else if ((iswordstart(ch) || ch == '$' || ch == '/')) {
				styler.ColorTo(i, state);
				if (ch == '/' || ch == '$') {
					styler.ColorTo(i + 1, SCE_SMALI_OPERATOR);
				}
				state = SCE_SMALI_IDENTIFIER;
				buf[0] = static_cast<char>(ch);
				wordLen = 1;
			} else if (IsSmaliOp(ch) || (ch == '[' || ch == ']')) {
				styler.ColorTo(i, state);
				state = SCE_SMALI_OPERATOR;
			}
		}

		if (atEOL || i == endPos - 1) {
			styler.SetLineState(lineCurrent, curLineState);
			lineCurrent++;
			visibleChars = 0;
			nextWordType = 0;
		}
		if (ch == '.' || (ch == ':' && !IsASpace(chNext)) || IsOpcodeChar(ch) || (IsADigit(ch) && IsOpcodeChar(chPrev))) {
			visibleChars++;
		}
	}

	// Colourise remaining document
	styler.ColorTo(endPos, state);
}

#define IsCommentLine(line)		IsLexCommentLine(styler, line, SCE_SMALI_COMMENTLINE)
inline bool IsFoldWord(const char *word) noexcept {
	return StrEqualsAny(word, "method", "annotation", "subannotation", "packed-switch", "sparse-switch", "array-data")
		// not used in Smali and Jasmin or javap
		|| StrEqualsAny(word, "tableswitch", "lookupswitch", "constant-pool", "attribute");
}
// field/parameter with annotation?
bool IsAnnotationLine(LexAccessor &styler, Sci_Line line) noexcept {
	Sci_Line scan_line = 10;
	while (scan_line-- > 0) {
		const Sci_Position startPos = styler.LineStart(line);
		const Sci_Position endPos = styler.LineStart(line + 1) - 1;
		const Sci_Position pos = LexSkipSpaceTab(styler, startPos, endPos);
		if (styler[pos] == '.') {
			return styler.StyleAt(pos) == SCE_SMALI_DIRECTIVE && styler[pos + 1] == 'a';
		}
		++line;
	}
	return false;
}

void FoldSmaliDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList, Accessor &styler) {
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

		if (iswordchar(ch) && style == SCE_SMALI_DIRECTIVE && stylePrev != SCE_SMALI_DIRECTIVE) {
			char buf[MAX_WORD_LENGTH + 1];
			LexGetRange(styler, i, IsSmaliWordCharX, buf, sizeof(buf));
			if (buf[0] == '.' && (IsFoldWord(buf + 1) || (StrEqual(buf + 1,"field") && IsAnnotationLine(styler, lineCurrent + 1)))) {
				levelNext++;
			} else if (buf[0] != '.' && (IsFoldWord(buf) || StrEqual(buf, "field"))) {
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

LexerModule lmSmali(SCLEX_SMALI, ColouriseSmaliDoc, "smali", FoldSmaliDoc);
