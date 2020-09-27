// Scintilla source code edit control
// Encoding: UTF-8
/** @file LexCSS.cxx
 ** Lexer for Cascading Style Sheets
 ** Written by Jakub Vrána
 ** Improved by Philippe Lhoste (CSS2)
 ** Improved by Ross McKay (SCSS mode; see https://sass-lang.com/ )
 **/
// Copyright 1998-2002 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cassert>
#include <cstring>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"

using namespace Scintilla;


static constexpr bool IsAWordChar(int ch) noexcept {
	/* FIXME:
	 * The CSS spec allows "ISO 10646 characters U+00A1 and higher" to be treated as word chars.
	 * Unfortunately, we are only getting string bytes here, and not full unicode characters. We cannot guarantee
	 * that our byte is between U+0080 - U+00A0 (to return false), so we have to allow all characters U+0080 and higher
	 */
	return ch >= 0x80 || IsAlphaNumeric(ch) || ch == '-' || ch == '_';
}

static constexpr bool IsCssOperator(int ch) noexcept {
	return ch == '{' || ch == '}' || ch == ':' || ch == ',' || ch == ';' ||
			ch == '.' || ch == '#' || ch == '!' || ch == '@' ||
			/* CSS2 */
			ch == '*' || ch == '>' || ch == '+' || ch == '=' || ch == '~' || ch == '|' ||
			ch == '[' || ch == ']' || ch == '(' || ch == ')';
}

// look behind (from start of document to our start position) to determine current nesting level
static int NestingLevelLookBehind(Sci_PositionU startPos, Accessor &styler) noexcept {
	int nestingLevel = 0;

	for (Sci_PositionU i = 0; i < startPos; i++) {
		const char ch = styler.SafeGetCharAt(i);
		if (ch == '{')
			nestingLevel++;
		else if (ch == '}')
			nestingLevel--;
	}

	return nestingLevel;
}

/*static const char * const cssWordListDesc[] = {
	"CSS1 Properties",
	"Pseudo-classes",
	"CSS2 Properties",
	"CSS3 Properties",
	"Pseudo-elements",
	"Browser-Specific CSS Properties",
	"Browser-Specific Pseudo-classes",
	"Browser-Specific Pseudo-elements",
	0
};*/

static void ColouriseCssDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	const WordList &css1Props = *keywordLists[0];
	const WordList &pseudoClasses = *keywordLists[1];
	const WordList &css2Props = *keywordLists[2];
	const WordList &css3Props = *keywordLists[3];
	const WordList &pseudoElements = *keywordLists[4];
	const WordList &exProps = *keywordLists[5];
	const WordList &exPseudoClasses = *keywordLists[6];
	const WordList &exPseudoElements = *keywordLists[7];

	StyleContext sc(startPos, length, initStyle, styler);

	int lastState = -1; // before operator
	int lastStateC = -1; // before comment
	int lastStateS = -1; // before single-quoted/double-quoted string
	int lastStateVar = -1; // before variable (SCSS)
	int lastStateVal = -1; // before value (SCSS)
	int op = ' '; // last operator
	int opPrev = ' '; // last operator
	bool insideParentheses = false; // true if currently in a CSS url() or similar construct

	// property lexer.css.scss.language
	//	Set to 1 for Sassy CSS (.scss)
	const bool isScssDocument = styler.GetPropertyInt("lexer.css.scss.language") != 0;

	// property lexer.css.less.language
	// Set to 1 for Less CSS (.less)
	const bool isLessDocument = styler.GetPropertyInt("lexer.css.less.language") != 0;

	// property lexer.css.hss.language
	// Set to 1 for HSS (.hss)
	const bool isHssDocument = styler.GetPropertyInt("lexer.css.hss.language") != 0;

	// SCSS/LESS/HSS have the concept of variable
	const bool hasVariables = isScssDocument || isLessDocument || isHssDocument;
	char varPrefix = 0;
	if (hasVariables)
		varPrefix = isLessDocument ? '@' : '$';

	// SCSS/LESS/HSS support single-line comments
	enum CommentMode {
		eCommentBlock = 0, eCommentLine = 1
	};
	CommentMode comment_mode = eCommentBlock;
	const bool hasSingleLineComments = isScssDocument || isLessDocument || isHssDocument;

	// must keep track of nesting level in document types that support it (SCSS/LESS/HSS)
	bool hasNesting = false;
	int nestingLevel = 0;
	if (isScssDocument || isLessDocument || isHssDocument) {
		hasNesting = true;
		nestingLevel = NestingLevelLookBehind(startPos, styler);
	}

	// "the loop"
	for (; sc.More(); sc.Forward()) {
		if (sc.state == SCE_CSS_COMMENT && ((comment_mode == eCommentBlock && sc.Match('*', '/')) || (comment_mode == eCommentLine && sc.atLineEnd))) {
			if (lastStateC == -1) {
				// backtrack to get last state:
				// comments are like whitespace, so we must return to the previous state
				Sci_PositionU i = startPos;
				for (; i > 0; i--) {
					if ((lastStateC = styler.StyleAt(i - 1)) != SCE_CSS_COMMENT) {
						if (lastStateC == SCE_CSS_OPERATOR) {
							op = styler.SafeGetCharAt(i - 1);
							opPrev = styler.SafeGetCharAt(i - 2);
							while (--i) {
								lastState = styler.StyleAt(i - 1);
								if (lastState != SCE_CSS_OPERATOR && lastState != SCE_CSS_COMMENT)
									break;
							}
							if (i == 0)
								lastState = SCE_CSS_DEFAULT;
						}
						break;
					}
				}
				if (i == 0)
					lastStateC = SCE_CSS_DEFAULT;
			}
			if (comment_mode == eCommentBlock) {
				sc.Forward();
				sc.ForwardSetState(lastStateC);
			} else /* eCommentLine */ {
				sc.SetState(lastStateC);
			}
		}

		if (sc.state == SCE_CSS_COMMENT)
			continue;

		if (sc.state == SCE_CSS_DOUBLESTRING || sc.state == SCE_CSS_SINGLESTRING) {
			if (sc.ch != (sc.state == SCE_CSS_DOUBLESTRING ? '\"' : '\''))
				continue;
			Sci_PositionU i = sc.currentPos;
			while (i && styler[i - 1] == '\\')
				i--;
			if ((sc.currentPos - i) % 2 == 1)
				continue;
			sc.ForwardSetState(lastStateS);
		}

		if (sc.state == SCE_CSS_OPERATOR) {
			if (op == ' ') {
				Sci_PositionU i = startPos;
				op = styler.SafeGetCharAt(i - 1);
				opPrev = styler.SafeGetCharAt(i - 2);
				while (--i) {
					lastState = styler.StyleAt(i - 1);
					if (lastState != SCE_CSS_OPERATOR && lastState != SCE_CSS_COMMENT)
						break;
				}
			}
			switch (op) {
			case '@':
				if (lastState == SCE_CSS_DEFAULT || hasNesting)
					sc.SetState(SCE_CSS_DIRECTIVE);
				break;
			case '>':
			case '+':
				if (lastState == SCE_CSS_TAG || lastState == SCE_CSS_CLASS || lastState == SCE_CSS_ID ||
					lastState == SCE_CSS_PSEUDOCLASS || lastState == SCE_CSS_EXTENDED_PSEUDOCLASS || lastState == SCE_CSS_UNKNOWN_PSEUDOCLASS)
					sc.SetState(SCE_CSS_DEFAULT);
				break;
			case '[':
				if (lastState == SCE_CSS_TAG || lastState == SCE_CSS_DEFAULT || lastState == SCE_CSS_CLASS || lastState == SCE_CSS_ID ||
					lastState == SCE_CSS_PSEUDOCLASS || lastState == SCE_CSS_EXTENDED_PSEUDOCLASS || lastState == SCE_CSS_UNKNOWN_PSEUDOCLASS)
					sc.SetState(SCE_CSS_ATTRIBUTE);
				break;
			case ']':
				if (lastState == SCE_CSS_ATTRIBUTE)
					sc.SetState(SCE_CSS_TAG);
				break;
			case '{':
				nestingLevel++;
				switch (lastState) {
				case SCE_CSS_MEDIA:
					sc.SetState(SCE_CSS_DEFAULT);
					break;
				case SCE_CSS_TAG:
				case SCE_CSS_DIRECTIVE:
					sc.SetState(SCE_CSS_IDENTIFIER);
					break;
				}
				break;
			case '}':
				if (--nestingLevel < 0)
					nestingLevel = 0;
				switch (lastState) {
				case SCE_CSS_DEFAULT:
				case SCE_CSS_VALUE:
				case SCE_CSS_IMPORTANT:
				case SCE_CSS_IDENTIFIER:
				case SCE_CSS_IDENTIFIER2:
				case SCE_CSS_IDENTIFIER3:
					if (hasNesting)
						sc.SetState(nestingLevel > 0 ? SCE_CSS_IDENTIFIER : SCE_CSS_DEFAULT);
					else
						sc.SetState(SCE_CSS_DEFAULT);
					break;
				}
				break;
			case '(':
				if (lastState == SCE_CSS_PSEUDOCLASS)
					sc.SetState(SCE_CSS_TAG);
				else if (lastState == SCE_CSS_EXTENDED_PSEUDOCLASS)
					sc.SetState(SCE_CSS_EXTENDED_PSEUDOCLASS);
				break;
			case ')':
				if (lastState == SCE_CSS_TAG || lastState == SCE_CSS_DEFAULT || lastState == SCE_CSS_CLASS || lastState == SCE_CSS_ID ||
					lastState == SCE_CSS_PSEUDOCLASS || lastState == SCE_CSS_EXTENDED_PSEUDOCLASS || lastState == SCE_CSS_UNKNOWN_PSEUDOCLASS ||
					lastState == SCE_CSS_PSEUDOELEMENT || lastState == SCE_CSS_EXTENDED_PSEUDOELEMENT)
					sc.SetState(SCE_CSS_TAG);
				break;
			case ':':
				switch (lastState) {
				case SCE_CSS_TAG:
				case SCE_CSS_DEFAULT:
				case SCE_CSS_CLASS:
				case SCE_CSS_ID:
				case SCE_CSS_PSEUDOCLASS:
				case SCE_CSS_EXTENDED_PSEUDOCLASS:
				case SCE_CSS_UNKNOWN_PSEUDOCLASS:
				case SCE_CSS_PSEUDOELEMENT:
				case SCE_CSS_EXTENDED_PSEUDOELEMENT:
					sc.SetState(SCE_CSS_PSEUDOCLASS);
					break;
				case SCE_CSS_IDENTIFIER:
				case SCE_CSS_IDENTIFIER2:
				case SCE_CSS_IDENTIFIER3:
				case SCE_CSS_EXTENDED_IDENTIFIER:
				case SCE_CSS_UNKNOWN_IDENTIFIER:
				case SCE_CSS_VARIABLE:
					sc.SetState(SCE_CSS_VALUE);
					lastStateVal = lastState;
					break;
				}
				break;
			case '.':
				if (lastState == SCE_CSS_TAG || lastState == SCE_CSS_DEFAULT || lastState == SCE_CSS_CLASS || lastState == SCE_CSS_ID ||
					lastState == SCE_CSS_PSEUDOCLASS || lastState == SCE_CSS_EXTENDED_PSEUDOCLASS || lastState == SCE_CSS_UNKNOWN_PSEUDOCLASS)
					sc.SetState(SCE_CSS_CLASS);
				break;
			case '#':
				if (lastState == SCE_CSS_TAG || lastState == SCE_CSS_DEFAULT || lastState == SCE_CSS_CLASS || lastState == SCE_CSS_ID ||
					lastState == SCE_CSS_PSEUDOCLASS || lastState == SCE_CSS_EXTENDED_PSEUDOCLASS || lastState == SCE_CSS_UNKNOWN_PSEUDOCLASS)
					sc.SetState(SCE_CSS_ID);
				break;
			case ',':
			case '|':
			case '~':
				if (lastState == SCE_CSS_TAG)
					sc.SetState(SCE_CSS_DEFAULT);
				break;
			case ';':
				switch (lastState) {
				case SCE_CSS_DIRECTIVE:
					if (hasNesting) {
						sc.SetState(nestingLevel > 0 ? SCE_CSS_IDENTIFIER : SCE_CSS_DEFAULT);
					} else {
						sc.SetState(SCE_CSS_DEFAULT);
					}
					break;
				case SCE_CSS_VALUE:
				case SCE_CSS_IMPORTANT:
					// data URLs can have semicolons; simplistically check for wrapping parentheses and move along
					if (insideParentheses) {
						sc.SetState(lastState);
					} else {
						if (lastStateVal == SCE_CSS_VARIABLE) {
							sc.SetState(SCE_CSS_DEFAULT);
						} else {
							sc.SetState(SCE_CSS_IDENTIFIER);
						}
					}
					break;
				case SCE_CSS_VARIABLE:
					if (lastStateVar == SCE_CSS_VALUE) {
						// data URLs can have semicolons; simplistically check for wrapping parentheses and move along
						if (insideParentheses) {
							sc.SetState(SCE_CSS_VALUE);
						} else {
							sc.SetState(SCE_CSS_IDENTIFIER);
						}
					} else {
						sc.SetState(SCE_CSS_DEFAULT);
					}
					break;
				}
				break;
			case '!':
				if (lastState == SCE_CSS_VALUE)
					sc.SetState(SCE_CSS_IMPORTANT);
				break;
			}
		}

		if (sc.ch == '*' && sc.state == SCE_CSS_DEFAULT) {
			sc.SetState(SCE_CSS_TAG);
			continue;
		}

		// check for inside parentheses (whether part of an "operator" or not)
		if (sc.ch == '(')
			insideParentheses = true;
		else if (sc.ch == ')')
			insideParentheses = false;

		// SCSS special modes
		if (hasVariables) {
			// variable name
			if (sc.ch == varPrefix) {
				switch (sc.state) {
				case SCE_CSS_DEFAULT:
					if (isLessDocument) // give priority to pseudo elements
						break;
					[[fallthrough]];
					// fall through
				case SCE_CSS_VALUE:
					lastStateVar = sc.state;
					sc.SetState(SCE_CSS_VARIABLE);
					continue;
				}
			}
			if (sc.state == SCE_CSS_VARIABLE) {
				if (IsAWordChar(sc.ch)) {
					// still looking at the variable name
					continue;
				}
				if (lastStateVar == SCE_CSS_VALUE) {
					// not looking at the variable name any more, and it was part of a value
					sc.SetState(SCE_CSS_VALUE);
				}
			}

			// nested rule parent selector
			if (sc.ch == '&') {
				switch (sc.state) {
				case SCE_CSS_DEFAULT:
				case SCE_CSS_IDENTIFIER:
					sc.SetState(SCE_CSS_TAG);
					continue;
				}
			}
		}

		// nesting rules that apply to SCSS and Less
		if (hasNesting) {
			// check for nested rule selector
			if (sc.state == SCE_CSS_IDENTIFIER && (IsAWordChar(sc.ch) || sc.ch == ':' || sc.ch == '.' || sc.ch == '#')) {
				// look ahead to see whether { comes before next ; and }
				const Sci_PositionU endPos = startPos + length;

				for (Sci_PositionU i = sc.currentPos; i < endPos; i++) {
					const char ch = styler.SafeGetCharAt(i);
					if (ch == ';' || ch == '}')
						break;
					if (ch == '{') {
						sc.SetState(SCE_CSS_DEFAULT);
						continue;
					}
				}
			}

		}

		if (IsAWordChar(sc.ch)) {
			if (sc.state == SCE_CSS_DEFAULT)
				sc.SetState(SCE_CSS_TAG);
			continue;
		}

		if (IsAWordChar(sc.chPrev) && (
			sc.state == SCE_CSS_IDENTIFIER || sc.state == SCE_CSS_IDENTIFIER2 ||
			sc.state == SCE_CSS_IDENTIFIER3 || sc.state == SCE_CSS_EXTENDED_IDENTIFIER ||
			sc.state == SCE_CSS_UNKNOWN_IDENTIFIER ||
			sc.state == SCE_CSS_PSEUDOCLASS || sc.state == SCE_CSS_PSEUDOELEMENT ||
			sc.state == SCE_CSS_EXTENDED_PSEUDOCLASS || sc.state == SCE_CSS_EXTENDED_PSEUDOELEMENT ||
			sc.state == SCE_CSS_UNKNOWN_PSEUDOCLASS ||
			sc.state == SCE_CSS_IMPORTANT ||
			sc.state == SCE_CSS_DIRECTIVE
			)) {
			char s[100];
			sc.GetCurrentLowered(s, sizeof(s));
			char *s2 = s;
			while (*s2 && !IsAWordChar(static_cast<unsigned char>(*s2)))
				s2++;
			switch (sc.state) {
			case SCE_CSS_IDENTIFIER:
			case SCE_CSS_IDENTIFIER2:
			case SCE_CSS_IDENTIFIER3:
			case SCE_CSS_EXTENDED_IDENTIFIER:
			case SCE_CSS_UNKNOWN_IDENTIFIER:
				if (css1Props.InList(s2))
					sc.ChangeState(SCE_CSS_IDENTIFIER);
				else if (css2Props.InList(s2))
					sc.ChangeState(SCE_CSS_IDENTIFIER2);
				else if (css3Props.InList(s2))
					sc.ChangeState(SCE_CSS_IDENTIFIER3);
				else if (exProps.InList(s2))
					sc.ChangeState(SCE_CSS_EXTENDED_IDENTIFIER);
				else
					sc.ChangeState(SCE_CSS_UNKNOWN_IDENTIFIER);
				break;
			case SCE_CSS_PSEUDOCLASS:
			case SCE_CSS_PSEUDOELEMENT:
			case SCE_CSS_EXTENDED_PSEUDOCLASS:
			case SCE_CSS_EXTENDED_PSEUDOELEMENT:
			case SCE_CSS_UNKNOWN_PSEUDOCLASS:
				if (op == ':' && opPrev != ':' && pseudoClasses.InList(s2))
					sc.ChangeState(SCE_CSS_PSEUDOCLASS);
				else if (opPrev == ':' && pseudoElements.InList(s2))
					sc.ChangeState(SCE_CSS_PSEUDOELEMENT);
				else if ((op == ':' || (op == '(' && lastState == SCE_CSS_EXTENDED_PSEUDOCLASS)) && opPrev != ':' && exPseudoClasses.InList(s2))
					sc.ChangeState(SCE_CSS_EXTENDED_PSEUDOCLASS);
				else if (opPrev == ':' && exPseudoElements.InList(s2))
					sc.ChangeState(SCE_CSS_EXTENDED_PSEUDOELEMENT);
				else
					sc.ChangeState(SCE_CSS_UNKNOWN_PSEUDOCLASS);
				break;
			case SCE_CSS_IMPORTANT:
				if (strcmp(s2, "important") != 0)
					sc.ChangeState(SCE_CSS_VALUE);
				break;
			case SCE_CSS_DIRECTIVE:
				if (op == '@' && strcmp(s2, "media") == 0)
					sc.ChangeState(SCE_CSS_MEDIA);
				break;
			}
		}

		if (sc.ch != '.' && sc.ch != ':' && sc.ch != '#' && (
			sc.state == SCE_CSS_CLASS || sc.state == SCE_CSS_ID ||
			(sc.ch != '(' && sc.ch != ')' && ( /* This line of the condition makes it possible to extend pseudo-classes with parentheses */
				sc.state == SCE_CSS_PSEUDOCLASS || sc.state == SCE_CSS_PSEUDOELEMENT ||
				sc.state == SCE_CSS_EXTENDED_PSEUDOCLASS || sc.state == SCE_CSS_EXTENDED_PSEUDOELEMENT ||
				sc.state == SCE_CSS_UNKNOWN_PSEUDOCLASS
				))
			))
			sc.SetState(SCE_CSS_TAG);

		if (sc.Match('/', '*')) {
			lastStateC = sc.state;
			comment_mode = eCommentBlock;
			sc.SetState(SCE_CSS_COMMENT);
			sc.Forward();
		} else if (hasSingleLineComments && sc.Match('/', '/') && !insideParentheses) {
			// note that we've had to treat ([...]// as the start of a URL not a comment, e.g. url(http://example.com), url(//example.com)
			lastStateC = sc.state;
			comment_mode = eCommentLine;
			sc.SetState(SCE_CSS_COMMENT);
			sc.Forward();
		} else if ((sc.state == SCE_CSS_VALUE || sc.state == SCE_CSS_ATTRIBUTE)
			&& (sc.ch == '\"' || sc.ch == '\'')) {
			lastStateS = sc.state;
			sc.SetState((sc.ch == '\"' ? SCE_CSS_DOUBLESTRING : SCE_CSS_SINGLESTRING));
		} else if (IsCssOperator(sc.ch)
			&& (sc.state != SCE_CSS_ATTRIBUTE || sc.ch == ']')
			&& (sc.state != SCE_CSS_VALUE || sc.ch == ';' || sc.ch == '}' || sc.ch == '!')
			&& ((sc.state != SCE_CSS_DIRECTIVE && sc.state != SCE_CSS_MEDIA) || sc.ch == ';' || sc.ch == '{')
			) {
			if (sc.state != SCE_CSS_OPERATOR)
				lastState = sc.state;
			sc.SetState(SCE_CSS_OPERATOR);
			op = sc.ch;
			opPrev = sc.chPrev;
		}
	}

	sc.Complete();
}

static void FoldCSSDoc(Sci_PositionU startPos, Sci_Position length, int, LexerWordList, Accessor &styler) {
	const bool foldComment = styler.GetPropertyInt("fold.comment", 1) != 0;
	const Sci_PositionU endPos = startPos + length;
	Sci_Position lineCurrent = styler.GetLine(startPos);
	int levelPrev = styler.LevelAt(lineCurrent) & SC_FOLDLEVELNUMBERMASK;
	int levelCurrent = levelPrev;
	char chNext = styler[startPos];
	bool inComment = (styler.StyleAt(startPos - 1) == SCE_CSS_COMMENT);
	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const char ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);
		const int style = styler.StyleAt(i);
		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');
		if (foldComment) {
			if (!inComment && (style == SCE_CSS_COMMENT))
				levelCurrent++;
			else if (inComment && (style != SCE_CSS_COMMENT))
				levelCurrent--;
			inComment = (style == SCE_CSS_COMMENT);
		}
		if (style == SCE_CSS_OPERATOR) {
			if (ch == '{') {
				levelCurrent++;
			} else if (ch == '}') {
				levelCurrent--;
			}
		}
		if (atEOL) {
			int lev = levelPrev;
			if ((levelCurrent > levelPrev))
				lev |= SC_FOLDLEVELHEADERFLAG;
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}
			lineCurrent++;
			levelPrev = levelCurrent;
		}
	}
	// Fill in the real level of the next line, keeping the current flags as they will be filled in later
	const int flagsNext = styler.LevelAt(lineCurrent) & ~SC_FOLDLEVELNUMBERMASK;
	styler.SetLevel(lineCurrent, levelPrev | flagsNext);
}

LexerModule lmCss(SCLEX_CSS, ColouriseCssDoc, "css", FoldCSSDoc);
