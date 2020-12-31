// Edit AutoCompletion

#include <windows.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include "SciCall.h"
#include "Helpers.h"
#include "Edit.h"
#include "Styles.h"
#include "resource.h"
#include "EditAutoC_Data0.h"

#define NP2_AUTOC_USE_STRING_ORDER	1
// scintilla/src/AutoComplete.h AutoComplete::maxItemLen
#define NP2_AUTOC_MAX_WORD_LENGTH	(1024 - 3 - 1 - 16)	// SP + '(' + ')' + '\0'
#define NP2_AUTOC_INIT_BUF_SIZE		(4096)
#define NP2_AUTOC_MAX_BUF_COUNT		20
#define NP2_AUTOC_INIT_CACHE_BYTES	(4096)
#define NP2_AUTOC_MAX_CACHE_COUNT	18
/*
word buffer:
(2**20 - 1)*4096 => 4 GiB

node cache:
a = [4096*2**i for i in range(18)] => 1 GiB
x64: sum(i//40 for i in a) => 26843434 nodes
x86: sum(i//24 for i in a) => 44739063 nodes
*/

struct WordNode;
struct WordList {
	char wordBuf[1024];
	int (__cdecl *WL_strcmp)(LPCSTR, LPCSTR);
	int (__cdecl *WL_strncmp)(LPCSTR, LPCSTR, size_t);
#if NP2_AUTOC_USE_STRING_ORDER
	UINT (*WL_OrderFunc)(const void *, unsigned int);
#endif
	struct WordNode *pListHead;
	LPCSTR pWordStart;

	char *bufferList[NP2_AUTOC_MAX_BUF_COUNT];
	char *buffer;
	int bufferCount;
	UINT offset;
	UINT capacity;

	UINT nWordCount;
	UINT nTotalLen;
	UINT orderStart;
	int iStartLen;
	int iMaxLength;

	struct WordNode *nodeCacheList[NP2_AUTOC_MAX_CACHE_COUNT];
	struct WordNode *nodeCache;
	int cacheCount;
	UINT cacheIndex;
	UINT cacheCapacity;
	UINT cacheBytes;
};

// TODO: replace _stricmp() and _strnicmp() with other functions
// which correctly case insensitively compares UTF-8 string and ANSI string.

#if NP2_AUTOC_USE_STRING_ORDER
#define NP2_AUTOC_ORDER_LENGTH	4
#define NP2_AUTOC_MAX_ORDER_LENGTH	4

UINT WordList_Order(const void *pWord, unsigned int len) {
#if 0
	unsigned int high = 0;
	const unsigned char *ptr = (const unsigned char *)pWord;
	len = min_u(len, 4);
	while (len) {
		high = (high << 8) | *ptr++;
		--len;
	}
#else
	unsigned int high = *((const unsigned int *)pWord);
	if (len < NP2_AUTOC_ORDER_LENGTH) {
		high &= ((1U << len * 8) - 1);
	}
	high = bswap32(high);
#endif
	return high;
}

UINT WordList_OrderCase(const void *pWord, unsigned int len) {
	unsigned int high = 0;
	const unsigned char *ptr = (const unsigned char *)pWord;
	len = min_u(len, 4);
	while (len) {
		unsigned char ch = *ptr++;
		// convert to lower case to match _stricmp() / strcasecmp().
		if (ch >= 'A' && ch <= 'Z') {
			ch = ch + 'a' - 'A';
		}
		high = (high << 8) | ch;
		--len;
	}
	return high;
}
#endif

// Tree
struct WordNode {
	union {
		struct WordNode *link[2];
		struct {
			struct WordNode *left;
			struct WordNode *right;
		};
	};
	char *word;
#if NP2_AUTOC_USE_STRING_ORDER
	UINT order;
#endif
	int len;
	int level;
};

#define NP2_TREE_HEIGHT_LIMIT	32
// TODO: since the tree is sorted, nodes greater than some level can be deleted to reduce total words.
// or only limit word count in WordList_GetList().

// Andersson Tree, source from https://www.eternallyconfuzzled.com/tuts/datastructures/jsw_tut_andersson.aspx
// see also https://en.wikipedia.org/wiki/AA_tree
#define aa_tree_skew(t) \
	if ((t)->level && (t)->left && (t)->level == (t)->left->level) {\
		struct WordNode *save = (t)->left;					\
		(t)->left = save->right;							\
		save->right = (t);									\
		(t) = save;											\
	}
#define aa_tree_split(t) \
	if ((t)->level && (t)->right && (t)->right->right && (t)->level == (t)->right->right->level) {\
		struct WordNode *save = (t)->right;					\
		(t)->right = save->left;							\
		save->left = (t);									\
		(t) = save;											\
		++(t)->level;										\
	}

static inline void WordList_AddBuffer(struct WordList *pWList) {
	char *buffer = (char *)NP2HeapAlloc(pWList->capacity);
	char *align = (char *)align_ptr(buffer);
	pWList->bufferList[pWList->bufferCount] = buffer;
	pWList->buffer = buffer;
	pWList->bufferCount++;
	pWList->offset = (UINT)(align - buffer);
}

static inline void WordList_AddCache(struct WordList *pWList) {
	struct WordNode *node = (struct WordNode *)NP2HeapAlloc(pWList->cacheBytes);
	pWList->nodeCacheList[pWList->cacheCount] = node;
	pWList->nodeCache = node;
	pWList->cacheCount++;
	pWList->cacheIndex = 0;
	pWList->cacheCapacity = pWList->cacheBytes / (sizeof(struct WordNode));
}

void WordList_AddWord(struct WordList *pWList, LPCSTR pWord, int len) {
	struct WordNode *root = pWList->pListHead;
#if NP2_AUTOC_USE_STRING_ORDER
	const UINT order = (pWList->iStartLen > NP2_AUTOC_MAX_ORDER_LENGTH) ? 0 : pWList->WL_OrderFunc(pWord, len);
#endif
	if (root == NULL) {
		struct WordNode *node;
		node = pWList->nodeCache + pWList->cacheIndex++;
		node->word = pWList->buffer + pWList->offset;

		CopyMemory(node->word, pWord, len);
#if NP2_AUTOC_USE_STRING_ORDER
		node->order = order;
#endif
		node->len = len;
		node->level = 1;
		root = node;
	} else {
		struct WordNode *iter = root;
		struct WordNode *path[NP2_TREE_HEIGHT_LIMIT] = { NULL };
		int top = 0;
		int dir;

		// find a spot and save the path
		for (;;) {
			path[top++] = iter;
#if NP2_AUTOC_USE_STRING_ORDER
			dir = (int)(iter->order - order);
			if (dir == 0 && (len > NP2_AUTOC_ORDER_LENGTH || iter->len > NP2_AUTOC_ORDER_LENGTH)) {
				dir = pWList->WL_strcmp(iter->word, pWord);
			}
#else
			dir = pWList->WL_strcmp(iter->word, pWord);
#endif
			if (dir == 0) {
				return;
			}
			dir = dir < 0;
			if (iter->link[dir] == NULL) {
				break;
			}
			iter = iter->link[dir];
		}

		if (pWList->cacheIndex + 1 > pWList->cacheCapacity) {
			pWList->cacheBytes <<= 1;
			WordList_AddCache(pWList);
		}
		if (pWList->capacity < pWList->offset + len + 1) {
			pWList->capacity <<= 1;
			WordList_AddBuffer(pWList);
		}

		struct WordNode *node = pWList->nodeCache + pWList->cacheIndex++;
		node->word = pWList->buffer + pWList->offset;

		CopyMemory(node->word, pWord, len);
#if NP2_AUTOC_USE_STRING_ORDER
		node->order = order;
#endif
		node->len = len;
		node->level = 1;
		iter->link[dir] = node;

		// walk back and rebalance
		while (--top >= 0) {
			// which child?
			if (top != 0) {
				dir = path[top - 1]->right == path[top];
			}
			aa_tree_skew(path[top]);
			aa_tree_split(path[top]);
			// fix the parent
			if (top != 0) {
				path[top - 1]->link[dir] = path[top];
			} else {
				root = path[top];
			}
		}
	}

	pWList->pListHead = root;
	pWList->nWordCount++;
	pWList->nTotalLen += len + 1;
	pWList->offset += align_up(len + 1);
	if (len > pWList->iMaxLength) {
		pWList->iMaxLength = len;
	}
}

void WordList_Free(struct WordList *pWList) {
	for (int i = 0; i < pWList->cacheCount; i++) {
		NP2HeapFree(pWList->nodeCacheList[i]);
	}
	for (int i = 0; i < pWList->bufferCount; i++) {
		NP2HeapFree(pWList->bufferList[i]);
	}
}

char* WordList_GetList(struct WordList *pWList) {
	struct WordNode *root = pWList->pListHead;
	struct WordNode *path[NP2_TREE_HEIGHT_LIMIT] = { NULL };
	int top = 0;
	char *buf = (char *)NP2HeapAlloc(pWList->nTotalLen + 1);// additional separator
	char * const pList = buf;

	while (root || top > 0) {
		if (root) {
			path[top++] = root;
			root = root->left;
		} else {
			root = path[--top];
			CopyMemory(buf, root->word, root->len);
			buf += root->len;
			*buf++ = '\n'; // the separator char
			root = root->right;
		}
	}
	// trim last separator char
	if (buf != pList) {
		*(--buf) = '\0';
	}
	return pList;
}

struct WordList *WordList_Alloc(LPCSTR pRoot, int iRootLen, BOOL bIgnoreCase) {
	struct WordList *pWList = (struct WordList *)NP2HeapAlloc(sizeof(struct WordList));
	pWList->pListHead = NULL;
	pWList->pWordStart = pRoot;
	pWList->nWordCount = 0;
	pWList->nTotalLen = 0;
	pWList->iStartLen = iRootLen;
	pWList->iMaxLength = iRootLen;

	if (bIgnoreCase) {
		pWList->WL_strcmp = _stricmp;
		pWList->WL_strncmp = _strnicmp;
#if NP2_AUTOC_USE_STRING_ORDER
		pWList->WL_OrderFunc = WordList_OrderCase;
#endif
	} else {
		pWList->WL_strcmp = strcmp;
		pWList->WL_strncmp = strncmp;
#if NP2_AUTOC_USE_STRING_ORDER
		pWList->WL_OrderFunc = WordList_Order;
#endif
	}
#if NP2_AUTOC_USE_STRING_ORDER
	pWList->orderStart = pWList->WL_OrderFunc(pRoot, iRootLen);
#endif

	pWList->capacity = NP2_AUTOC_INIT_BUF_SIZE;
	WordList_AddBuffer(pWList);
	pWList->cacheBytes = NP2_AUTOC_INIT_CACHE_BYTES;
	WordList_AddCache(pWList);
	return pWList;
}

static inline void WordList_UpdateRoot(struct WordList *pWList, LPCSTR pRoot, int iRootLen) {
	pWList->pWordStart = pRoot;
	pWList->iStartLen = iRootLen;
	pWList->iMaxLength = (pWList->nWordCount == 0) ? iRootLen : max_i(iRootLen, pWList->iMaxLength);
#if NP2_AUTOC_USE_STRING_ORDER
	pWList->orderStart = pWList->WL_OrderFunc(pRoot, iRootLen);
#endif
}

static inline BOOL WordList_StartsWith(const struct WordList *pWList, LPCSTR pWord) {
#if NP2_AUTOC_USE_STRING_ORDER
	if (pWList->iStartLen > NP2_AUTOC_ORDER_LENGTH) {
		return pWList->WL_strncmp(pWList->pWordStart, pWord, pWList->iStartLen) == 0;
	}
	if (pWList->orderStart != pWList->WL_OrderFunc(pWord, pWList->iStartLen)) {
		return FALSE;
	}
	return TRUE;
#else
	return pWList->WL_strncmp(pWList->pWordStart, pWord, pWList->iStartLen) == 0;
#endif
}

void WordList_AddListEx(struct WordList *pWList, LPCSTR pList) {
	char *word = pWList->wordBuf;
	const int iStartLen = pWList->iStartLen;
	int len = 0;
	BOOL ok = FALSE;
	do {
		const char *sub = strpbrk(pList, " \t.,();^\n\r");
		if (sub) {
			int lenSub = (int)(sub - pList);
			lenSub = min_i(NP2_AUTOC_MAX_WORD_LENGTH - len, lenSub);
			memcpy(word + len, pList, lenSub);
			len += lenSub;
			if (len >= iStartLen) {
				if (*sub == '(') {
					word[len++] = '(';
					word[len++] = ')';
				}
				word[len] = 0;
				if (ok || WordList_StartsWith(pWList, word)) {
					WordList_AddWord(pWList, word, len);
					ok = *sub == '.';
				}
			}
			if (*sub == '^') {
				word[len++] = ' ';
			} else if (!ok && *sub != '.') {
				len = 0;
			} else {
				word[len++] = '.';
			}
			pList = ++sub;
		} else {
			int lenSub = (int)strlen(pList);
			lenSub = min_i(NP2_AUTOC_MAX_WORD_LENGTH - len, lenSub);
			if (len) {
				memcpy(word + len, pList, lenSub);
				len += lenSub;
				word[len] = '\0';
				pList = word;
			} else {
				len = lenSub;
			}
			if (len >= iStartLen) {
				if (ok || WordList_StartsWith(pWList, pList)) {
					WordList_AddWord(pWList, pList, len);
				}
			}
			break;
		}
	} while (*pList);
}

static inline void WordList_AddList(struct WordList *pWList, LPCSTR pList) {
	if (StrNotEmptyA(pList)) {
		WordList_AddListEx(pWList, pList);
	}
}

void WordList_AddSubWord(struct WordList *pWList, LPSTR pWord, int wordLength, int iRootLen) {
	/*
	when pRoot is 'b', split 'bugprone-branch-clone' as following:
	1. first hyphen: 'bugprone-branch-clone' => 'bugprone', 'branch-clone'.
	2. second hyphen: 'bugprone-branch-clone' => 'bugprone-branch'; 'branch-clone' => 'branch'.
	*/

	LPCSTR words[8];
	int starts[8];
	UINT count = 0;

	for (int i = 0; i < wordLength - 1; i++) {
		const char ch = pWord[i];
		if (ch == '.' || ch == '-' || ch == ':') {
			if (i >= iRootLen) {
				pWord[i] = '\0';
				WordList_AddWord(pWList, pWord, i);
				for (UINT j = 0; j < count; j++) {
					const int subLen = i - starts[j];
					if (subLen >= iRootLen) {
						WordList_AddWord(pWList, words[j], subLen);
					}
				}
				pWord[i] = ch;
			}
			if (ch != '.' && (pWord[i + 1] == '>' || pWord[i + 1] == ':')) {
				++i;
			}

			const int subLen = wordLength - (i + 1);
			LPCSTR pSubRoot = pWord + i + 1;
			if (subLen >= iRootLen && WordList_StartsWith(pWList, pSubRoot)) {
				WordList_AddWord(pWList, pSubRoot, subLen);
				if (count < COUNTOF(words)) {
					words[count] = pSubRoot;
					starts[count] = i + 1;
					++count;
				}
			}
		}
	}
}


static inline BOOL IsEscapeChar(int ch) {
	return ch == 't' || ch == 'n' || ch == 'r' || ch == 'a' || ch == 'b' || ch == 'v' || ch == 'f'
		|| ch == '0'
		|| ch == '$'; // PHP
	// x u U
}

static inline BOOL IsCppCommentStyle(int style) {
	return style == SCE_C_COMMENT
		|| style == SCE_C_COMMENTLINE
		|| style == SCE_C_COMMENTDOC
		|| style == SCE_C_COMMENTLINEDOC
		|| style == SCE_C_COMMENTDOC_TAG
		|| style == SCE_C_COMMENTDOC_TAG_XML;
}

static inline BOOL IsCppStringStyle(int style) {
	return style == SCE_C_STRING
		|| style == SCE_C_CHARACTER
		|| style == SCE_C_STRINGEOL
		|| style == SCE_C_STRINGRAW
		|| style == SCE_C_VERBATIM
		|| style == SCE_C_DSTRINGX
		|| style == SCE_C_DSTRINGQ
		|| style == SCE_C_DSTRINGT;
}

static inline BOOL IsSpecialStart(int ch) {
	return ch == ':' || ch == '.' || ch == '#' || ch == '@'
		|| ch == '<' || ch == '\\' || ch == '/' || ch == '-'
		|| ch == '>' || ch == '$' || ch == '%';
}

static inline BOOL IsSpecialStartChar(int ch, int chPrev) {
	return (ch == '.')	// member
		|| (ch == '#')	// preprocessor
		|| (ch == '@') // Java/PHP/Doxygen Doc Tag
		// ObjC Keyword, Java Annotation, Python Decorator, Cobra Directive
		|| (ch == '<') // HTML/XML Tag, C# Doc Tag
		|| (ch == '\\')// Doxygen Doc Tag, LaTeX Command
		|| (chPrev == '<' && ch == '/')	// HTML/XML Close Tag
		|| (chPrev == '-' && ch == '>')	// member(C/C++/PHP)
		|| (chPrev == ':' && ch == ':');// namespace(C++), static member(C++/Java8/PHP)
}

//=============================================================================
//
// EditCompleteWord()
// Auto-complete words
//
extern struct EditAutoCompletionConfig autoCompletionConfig;

// CharClassify::SetDefaultCharClasses()
static inline BOOL IsDefaultWordChar(int ch) {
	return ch >= 0x80 || IsAlphaNumeric(ch) || ch == '_';
}

BOOL IsDocWordChar(int ch) {
	if (IsAlphaNumeric(ch) || ch == '_' || ch == '.') {
		return TRUE;
	}

	switch (pLexCurrent->rid) {
	case NP2LEX_TEXTFILE:
	case NP2LEX_2NDTEXTFILE:
	case NP2LEX_ANSI:
	case NP2LEX_CSS:
	case NP2LEX_DOT:
	case NP2LEX_LISP:
	case NP2LEX_SMALI:
		return (ch == '-');

	case NP2LEX_ASM:
	case NP2LEX_FORTRAN:
		return (ch == '#' || ch == '%');
	case NP2LEX_AU3:
		return (ch == '#' || ch == '$' || ch == '@');

	case NP2LEX_BASH:
	case NP2LEX_BATCH:
	case NP2LEX_HTML:
	case NP2LEX_GN:
	case NP2LEX_PHP:
	case NP2LEX_PS1:
		return (ch == '-' || ch == '$');

	case NP2LEX_CIL:
	case NP2LEX_VERILOG:
		return (ch == '$');
	case NP2LEX_CPP:
		return (ch == '#' || ch == '@' || ch == ':');
	case NP2LEX_CSHARP:
		return (ch == '#' || ch == '@');

	case NP2LEX_D:
	case NP2LEX_FSHARP:
	case NP2LEX_HAXE:
	case NP2LEX_INNO:
	case NP2LEX_VB:
		return (ch == '#');

	case NP2LEX_JAVA:
	case NP2LEX_JULIA:
	case NP2LEX_KOTLIN:
	case NP2LEX_RUST:
		return (ch == '$' || ch == '@' || ch == ':');

	case NP2LEX_GROOVY:
	case NP2LEX_SCALA:
	case NP2LEX_PYTHON:
	case NP2LEX_PERL:
	case NP2LEX_RUBY:
	case NP2LEX_SQL:
	case NP2LEX_TCL:
		return (ch == '$' || ch == '@');

	case NP2LEX_LLVM:
	case NP2LEX_WASM:
		return (ch == '@' || ch == '%' || ch == '$' || ch == '-');

	case NP2LEX_MAKE:
	case NP2LEX_NSIS:
		return (ch == '-' || ch == '$' || ch == '!');

	case NP2LEX_XML:
		return (ch == '-' || ch == ':');
	}
	return FALSE;
}

BOOL IsAutoCompletionWordCharacter(int ch) {
	return (ch < 0x80) ? IsDocWordChar(ch) : SciCall_IsAutoCompletionWordCharacter(ch);
}

static inline BOOL IsWordStyleToIgnore(int style) {
	switch (pLexCurrent->iLexer) {
	case SCLEX_CPP:
		return style == SCE_C_WORD
			|| style == SCE_C_WORD2
			|| style == SCE_C_PREPROCESSOR;

	case SCLEX_GO:
		return style == SCE_GO_WORD
			|| style == SCE_GO_WORD2
			|| style == SCE_GO_BUILTIN_FUNC
			|| style == SCE_GO_FORMAT_SPECIFIER;

	case SCLEX_JSON:
		return style == SCE_JSON_KEYWORD;

	case SCLEX_PYTHON:
		return style == SCE_PY_WORD
			|| style == SCE_PY_WORD2
			|| style == SCE_PY_BUILTIN_CONST
			|| style == SCE_PY_BUILTIN_FUNC
			|| style == SCE_PY_ATTR
			|| style == SCE_PY_OBJ_FUNC;

	case SCLEX_SMALI:
		return style == SCE_SMALI_WORD
			|| style == SCE_SMALI_DIRECTIVE
			|| style == SCE_SMALI_INSTRUCTION;
	case SCLEX_SQL:
		return style == SCE_SQL_WORD
			|| style == SCE_SQL_WORD2
			|| style == SCE_SQL_USER1
			|| style == SCE_SQL_HEX			// BLOB Hex
			|| style == SCE_SQL_HEX2;		// BLOB Hex
	}
	return FALSE;
}

// https://en.wikipedia.org/wiki/Printf_format_string
static inline BOOL IsStringFormatChar(int ch, int style) {
	if (!IsAlpha(ch)) {
		return FALSE;
	}
	switch (pLexCurrent->iLexer) {
	case SCLEX_CPP:
		return style != SCE_C_OPERATOR;

	case SCLEX_FSHARP:
		return style != SCE_FSHARP_OPERATOR;

	case SCLEX_JULIA:
		return style != SCE_JULIA_OPERATOR && style != SCE_JULIA_OPERATOR2;

	case SCLEX_LUA:
		return style != SCE_LUA_OPERATOR;

	case SCLEX_MATLAB:
		return style != SCE_MAT_OPERATOR;

	case SCLEX_PERL:
		return style != SCE_PL_OPERATOR;
	case SCLEX_PYTHON:
		return style != SCE_PY_OPERATOR;

	case SCLEX_RUBY:
		return style != SCE_RB_OPERATOR;

	case SCLEX_TCL:
		return style != SCE_TCL_OPERATOR;
	}
	return FALSE;
}

static inline BOOL IsEscapeCharEx(int ch, int style) {
	if (!IsEscapeChar(ch)) {
		return FALSE;
	}
	switch (pLexCurrent->iLexer) {
	case SCLEX_NULL:
	case SCLEX_BATCH:
	case SCLEX_CONF:
	case SCLEX_DIFF:
	case SCLEX_MAKEFILE:
	case SCLEX_PROPERTIES:
		return FALSE;

	case SCLEX_CPP:
		return !(style == SCE_C_STRINGRAW || style == SCE_C_VERBATIM
			|| style == SCE_C_COMMENTDOC_TAG
			|| (pLexCurrent->rid == NP2LEX_JS && style == SCE_C_DSTRINGB));

	case SCLEX_PYTHON:
		return !(style == SCE_PY_RAW_STRING1 || style == SCE_PY_RAW_STRING2
			|| style == SCE_PY_RAW_BYTES1 || style == SCE_PY_RAW_BYTES2
			|| style == SCE_PY_FMT_STRING1 || style == SCE_PY_FMT_STRING2);
	}
	return TRUE;
}

static inline BOOL NeedSpaceAfterKeyword(const char *word, Sci_Position length) {
	const char *p = strstr(
		" if for try using while elseif switch foreach synchronized "
		, word);
	return p != NULL && p[-1] == ' ' && p[length] == ' ';
}

#define HTML_TEXT_BLOCK_TAG		0
#define HTML_TEXT_BLOCK_CDATA	1
#define HTML_TEXT_BLOCK_JS		2
#define HTML_TEXT_BLOCK_VBS		3
#define HTML_TEXT_BLOCK_PYTHON	4
#define HTML_TEXT_BLOCK_PHP		5
#define HTML_TEXT_BLOCK_CSS		6
#define HTML_TEXT_BLOCK_SGML	7

extern EDITLEXER lexCSS;
extern EDITLEXER lexJS;
extern EDITLEXER lexPHP;
extern EDITLEXER lexPython;
extern EDITLEXER lexVBS;
extern HANDLE idleTaskTimer;

static int GetCurrentHtmlTextBlockEx(int iCurrentStyle) {
	if (iCurrentStyle == SCE_H_CDATA) {
		return HTML_TEXT_BLOCK_CDATA;
	}
	if ((iCurrentStyle >= SCE_HJ_START && iCurrentStyle <= SCE_HJ_REGEX)
		|| (iCurrentStyle >= SCE_HJA_START && iCurrentStyle <= SCE_HJA_REGEX)) {
		return HTML_TEXT_BLOCK_JS;
	}
	if ((iCurrentStyle >= SCE_HB_START && iCurrentStyle <= SCE_HB_STRINGEOL)
		|| (iCurrentStyle >= SCE_HBA_START && iCurrentStyle <= SCE_HBA_STRINGEOL)) {
		return HTML_TEXT_BLOCK_VBS;
	}
	if ((iCurrentStyle >= SCE_HP_START && iCurrentStyle <= SCE_HP_IDENTIFIER)
		|| (iCurrentStyle >= SCE_HPA_START && iCurrentStyle <= SCE_HPA_IDENTIFIER)) {
		return HTML_TEXT_BLOCK_PYTHON;
	}
	if ((iCurrentStyle >= SCE_HPHP_DEFAULT && iCurrentStyle <= SCE_HPHP_COMPLEX_VARIABLE)) {
		return HTML_TEXT_BLOCK_PHP;
	}
	if ((iCurrentStyle >= SCE_H_SGML_DEFAULT && iCurrentStyle <= SCE_H_SGML_BLOCK_DEFAULT)) {
		return HTML_TEXT_BLOCK_SGML;
	}
	return HTML_TEXT_BLOCK_TAG;
}

static int GetCurrentHtmlTextBlock(void) {
	const Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	const int iCurrentStyle = SciCall_GetStyleAt(iCurrentPos);
	return GetCurrentHtmlTextBlockEx(iCurrentStyle);
}

static void EscapeRegex(LPSTR pszOut, LPCSTR pszIn) {
	char ch;
	while ((ch = *pszIn++) != '\0') {
		if (ch == '.'		// any character
			|| ch == '^'	// start of line
			|| ch == '$'	// end of line
			|| ch == '?'	// 0 or 1 times
			|| ch == '*'	// 0 or more times
			|| ch == '+'	// 1 or more times
			|| ch == '[' || ch == ']'
			|| ch == '(' || ch == ')'
			) {
			*pszOut++ = '\\';
		}
		*pszOut++ = ch;
	}
	*pszOut++ = '\0';
}

void AutoC_AddDocWord(struct WordList *pWList, BOOL bIgnoreCase, char prefix) {
	LPCSTR const pRoot = pWList->pWordStart;
	const int iRootLen = pWList->iStartLen;

	// optimization for small string
	char onStack[256];
	char *pFind;
	if (iRootLen * 2 + 32 < (int)sizeof(onStack)) {
		ZeroMemory(onStack, sizeof(onStack));
		pFind = onStack;
	} else {
		pFind = (char *)NP2HeapAlloc(iRootLen * 2 + 32);
	}

	if (prefix) {
		char buf[2] = { prefix, '\0' };
		EscapeRegex(pFind, buf);
	}
	if (iRootLen == 0) {
		// find an identifier
		strcat(pFind, "[A-Za-z0-9_]");
		strcat(pFind, "\\i?");
	} else {
		if (IsDefaultWordChar((unsigned char)pRoot[0])) {
			strcat(pFind, "\\h");
		}
		EscapeRegex(pFind + strlen(pFind), pRoot);
		if (IsDefaultWordChar((unsigned char)pRoot[iRootLen - 1])) {
			strcat(pFind, "\\i?");
		} else {
			strcat(pFind, "\\i");
		}
	}

	const Sci_Position iCurrentPos = SciCall_GetCurrentPos() - iRootLen - (prefix ? 1 : 0);
	const Sci_Position iDocLen = SciCall_GetLength();
	const int findFlag = SCFIND_REGEXP | SCFIND_POSIX | (bIgnoreCase ? 0 : SCFIND_MATCHCASE);
	struct Sci_TextToFind ft = { { 0, iDocLen }, pFind, { 0, 0 } };

	Sci_Position iPosFind = SciCall_FindText(findFlag, &ft);
	HANDLE timer = idleTaskTimer;
	WaitableTimer_Set(timer, autoCompletionConfig.dwScanWordsTimeout);

	while (iPosFind >= 0 && iPosFind < iDocLen && WaitableTimer_Continue(timer)) {
		Sci_Position wordEnd = iPosFind + iRootLen;
		const int style = SciCall_GetStyleAt(wordEnd - 1);
		wordEnd = ft.chrgText.cpMax;
		if (iPosFind != iCurrentPos && !IsWordStyleToIgnore(style)) {
			// find all word after '::', '->', '.' and '-'
			BOOL bSubWord = FALSE;
			while (wordEnd < iDocLen) {
				const int ch = SciCall_GetCharAt(wordEnd);
				if (!(ch == ':' || ch == '.' || ch == '-')) {
					if (ch == '!' && pLexCurrent->iLexer == SCLEX_RUST && style == SCE_RUST_MACRO) {
						// macro: println!()
						++wordEnd;
					}
					break;
				}

				const Sci_Position before = wordEnd;
				Sci_Position width = 0;
				int chNext = SciCall_GetCharacterAndWidth(wordEnd + 1, &width);
				if ((ch == '-' && chNext == '>') || (ch == ':' && chNext == ':')) {
					chNext = SciCall_GetCharacterAndWidth(wordEnd + 2, &width);
					if (IsAutoCompletionWordCharacter(chNext)) {
						wordEnd += 2;
					}
				} else if (ch == '.' || (ch == '-' && style == SciCall_GetStyleAt(wordEnd))) {
					if (IsAutoCompletionWordCharacter(chNext)) {
						++wordEnd;
					}
				}
				if (wordEnd == before) {
					break;
				}

				while (wordEnd < iDocLen && !IsDefaultWordChar(chNext)) {
					wordEnd += width;
					chNext = SciCall_GetCharacterAndWidth(wordEnd, &width);
					if (!IsAutoCompletionWordCharacter(chNext)) {
						break;
					}
				}

				wordEnd = SciCall_WordEndPosition(wordEnd, TRUE);
				if (wordEnd - iPosFind > NP2_AUTOC_MAX_WORD_LENGTH) {
					wordEnd = before;
					break;
				}
				bSubWord = TRUE;
			}

			if (wordEnd - iPosFind >= iRootLen) {
				char *pWord = pWList->wordBuf + NP2DefaultPointerAlignment;
				BOOL bChanged = FALSE;
				struct Sci_TextRange tr = { { iPosFind, min_pos(iPosFind + NP2_AUTOC_MAX_WORD_LENGTH, wordEnd) }, pWord };
				int wordLength = (int)SciCall_GetTextRange(&tr);

				Sci_Position before = SciCall_PositionBefore(iPosFind);
				if (before + 1 == iPosFind) {
					const int ch = SciCall_GetCharAt(before);
					if (ch == '\\') { // word after escape char
						before = SciCall_PositionBefore(before);
						const int chPrev = (before + 2 == iPosFind) ? SciCall_GetCharAt(before) : 0;
						if (chPrev != '\\' && IsEscapeCharEx(*pWord, SciCall_GetStyleAt(before))) {
							pWord++;
							--wordLength;
							bChanged = TRUE;
						}
					} else if (ch == '%') { // word after format char
						if (IsStringFormatChar(*pWord, SciCall_GetStyleAt(before))) {
							pWord++;
							--wordLength;
							bChanged = TRUE;
						}
					}
				}
				if (prefix && prefix == *pWord) {
					pWord++;
					--wordLength;
					bChanged = TRUE;
				}

				//if (pLexCurrent->rid == NP2LEX_PHP && wordLength >= 2 && *pWord == '$' && pWord[1] == '$') {
				//	pWord++;
				//	--wordLength;
				//	bChanged = TRUE;
				//}
				while (wordLength > 0 && (pWord[wordLength - 1] == '-' || pWord[wordLength - 1] == ':' || pWord[wordLength - 1] == '.')) {
					--wordLength;
					pWord[wordLength] = '\0';
				}
				if (bChanged) {
					CopyMemory(pWList->wordBuf, pWord, wordLength + 1);
					pWord = pWList->wordBuf;
				}

				bChanged = wordLength >= iRootLen && WordList_StartsWith(pWList, pWord);
				if (bChanged && !(pWord[0] == ':' && pWord[1] != ':')) {
					BOOL space = FALSE;
					if (!(pLexCurrent->iLexer == SCLEX_CPP && style == SCE_C_MACRO)) {
						while (IsASpaceOrTab(SciCall_GetCharAt(wordEnd))) {
							space = TRUE;
							wordEnd++;
						}
					}

					const int chWordEnd = SciCall_GetCharAt(wordEnd);
					if ((pLexCurrent->iLexer == SCLEX_JULIA || pLexCurrent->iLexer == SCLEX_RUST) && chWordEnd == '!') {
						const int chNext = SciCall_GetCharAt(wordEnd + 1);
						if (chNext == '(') {
							wordEnd += 2;
							pWord[wordLength++] = '!';
							pWord[wordLength++] = '(';
							pWord[wordLength++] = ')';
						}
					}
					else if (chWordEnd == '(') {
						if (space && NeedSpaceAfterKeyword(pWord, wordLength)) {
							pWord[wordLength++] = ' ';
						}

						pWord[wordLength++] = '(';
						pWord[wordLength++] = ')';
						wordEnd++;
					}

					if (wordLength >= iRootLen) {
						pWord[wordLength] = '\0';
						WordList_AddWord(pWList, pWord, wordLength);
						if (bSubWord) {
							WordList_AddSubWord(pWList, pWord, wordLength, iRootLen);
						}
					}
				}
			}
		}

		ft.chrg.cpMin = wordEnd;
		iPosFind = SciCall_FindText(findFlag, &ft);
	}

	if (pFind != onStack) {
		NP2HeapFree(pFind);
	}
}

void AutoC_AddKeyword(struct WordList *pWList, int iCurrentStyle) {
	for (int i = 0; i < NUMKEYWORD; i++) {
		const char *pKeywords = pLexCurrent->pKeyWords->pszKeyWords[i];
		if (StrNotEmptyA(pKeywords) && !(currentLexKeywordAttr[i] & KeywordAttr_NoAutoComp)) {
			WordList_AddListEx(pWList, pKeywords);
		}
	}

	// additional keywords
	if (np2_LexKeyword && !(pLexCurrent->iLexer == SCLEX_CPP && !IsCppCommentStyle(iCurrentStyle))) {
		WordList_AddList(pWList, (*np2_LexKeyword)[0]);
		WordList_AddList(pWList, (*np2_LexKeyword)[1]);
		WordList_AddList(pWList, (*np2_LexKeyword)[2]);
		WordList_AddList(pWList, (*np2_LexKeyword)[3]);
	}

	// embedded script
	if (pLexCurrent->rid == NP2LEX_HTML) {
		const int block = GetCurrentHtmlTextBlockEx(iCurrentStyle);
		PEDITLEXER pLex = NULL;
		switch (block) {
		case HTML_TEXT_BLOCK_JS:
			pLex = &lexJS;
			break;
		case HTML_TEXT_BLOCK_VBS:
			pLex = &lexVBS;
			break;
		case HTML_TEXT_BLOCK_PYTHON:
			pLex = &lexPython;
			break;
		case HTML_TEXT_BLOCK_PHP:
			pLex = &lexPHP;
			break;
		case HTML_TEXT_BLOCK_CSS:
			pLex = &lexCSS;
			break;
		}
		if (pLex != NULL) {
			for (int i = 0; i < NUMKEYWORD; i++) {
				const char *pKeywords = pLex->pKeyWords->pszKeyWords[i];
				if (StrNotEmptyA(pKeywords)) {
					WordList_AddListEx(pWList, pKeywords);
				}
			}
		}
	}
}

#define AutoC_AddSpecWord_Finish	1
#define AutoC_AddSpecWord_Keyword	2
INT AutoC_AddSpecWord(struct WordList *pWList, int iCurrentStyle, int ch, int chPrev) {
	if (pLexCurrent->iLexer == SCLEX_CPP && IsCppCommentStyle(iCurrentStyle) && np2_LexKeyword) {
		if ((ch == '@' && (np2_LexKeyword == &kwJavaDoc || np2_LexKeyword == &kwPHPDoc || np2_LexKeyword == &kwDoxyDoc))
				|| (ch == '\\' && np2_LexKeyword == &kwDoxyDoc)
				|| ((ch == '<' || chPrev == '<') && np2_LexKeyword == &kwNETDoc)) {
			WordList_AddList(pWList, (*np2_LexKeyword)[0]);
			WordList_AddList(pWList, (*np2_LexKeyword)[1]);
			WordList_AddList(pWList, (*np2_LexKeyword)[2]);
			WordList_AddList(pWList, (*np2_LexKeyword)[3]);
			return AutoC_AddSpecWord_Finish;
		}
	}
	else if ((pLexCurrent->rid == NP2LEX_HTML || pLexCurrent->rid == NP2LEX_XML || pLexCurrent->rid == NP2LEX_CONF)
			   && ((ch == '<') || (chPrev == '<' && ch == '/'))) {
		WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[0]);// Tag
		if (pLexCurrent->rid == NP2LEX_XML) {
			if (np2_LexKeyword) { // XML Tag
				WordList_AddList(pWList, (*np2_LexKeyword)[0]);
			}
		}
		return AutoC_AddSpecWord_Keyword; // application defined tags
	}
	else if ((pLexCurrent->iLexer == SCLEX_CPP && iCurrentStyle == SCE_C_DEFAULT)
			   || (pLexCurrent->iLexer == SCLEX_PYTHON && iCurrentStyle == SCE_PY_DEFAULT)
			   || (pLexCurrent->rid == NP2LEX_VB && iCurrentStyle == SCE_B_DEFAULT)
			   || (pLexCurrent->iLexer == SCLEX_SMALI && iCurrentStyle == SCE_C_DEFAULT)) {
		if (ch == '#' && pLexCurrent->iLexer == SCLEX_CPP) { // #preprocessor
			const char *pKeywords = pLexCurrent->pKeyWords->pszKeyWords[2];
			if (StrNotEmptyA(pKeywords)) {
				WordList_AddListEx(pWList, pKeywords);
				return AutoC_AddSpecWord_Finish;
			}
		} else if (ch == '#' && pLexCurrent->rid == NP2LEX_VB) { // #preprocessor
			const char *pKeywords = pLexCurrent->pKeyWords->pszKeyWords[3];
			if (StrNotEmptyA(pKeywords)) {
				WordList_AddListEx(pWList, pKeywords);
				return AutoC_AddSpecWord_Finish;
			}
		} else if (ch == '@') { // @directive, @annotation, @decorator
			if (pLexCurrent->rid == NP2LEX_CSHARP) { // verbatim identifier
				//WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[0]);
				//WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[1]);
			} else {
				const char *pKeywords = pLexCurrent->pKeyWords->pszKeyWords[3];
				if (StrNotEmptyA(pKeywords)) {
					WordList_AddListEx(pWList, pKeywords);
					// user defined annotation
					return AutoC_AddSpecWord_Keyword;
				}
			}
		} else if (ch == '.' && pLexCurrent->iLexer == SCLEX_SMALI) {
			WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[9]);
			return AutoC_AddSpecWord_Finish;
		}
		//else if (chPrev == ':' && ch == ':') {
		//	WordList_AddList(pWList, "C++/namespace C++/Java8/PHP/static SendMessage()");
		//}
		//else if (chPrev == '-' && ch == '>') {
		//	WordList_AddList(pWList, "C/C++pointer PHP-variable");
		//}
	}
	else if (pLexCurrent->iLexer == SCLEX_JULIA && ch == '@' && iCurrentStyle == SCE_JULIA_DEFAULT) {
		WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[6]); // macro
		return AutoC_AddSpecWord_Keyword;
	}
	else if (pLexCurrent->iLexer == SCLEX_KOTLIN && ch == '@') {
		if (iCurrentStyle == SCE_KOTLIN_DEFAULT) {
			WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[4]); // annotation
			return AutoC_AddSpecWord_Keyword;
		} else if (iCurrentStyle >= SCE_KOTLIN_COMMENTLINE && iCurrentStyle <= SCE_KOTLIN_COMMENTDOCWORD) {
			WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[6]); // KDoc
			return AutoC_AddSpecWord_Finish;
		}
	}
	else if (pLexCurrent->iLexer == SCLEX_DART && ch == '@' && iCurrentStyle == SCE_DART_DEFAULT) {
		WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[4]); // metadata
		return AutoC_AddSpecWord_Keyword;
	}
	else if (pLexCurrent->iLexer == SCLEX_SWIFT && (ch == '@' || ch == '#') && iCurrentStyle == SCE_SWIFT_DEFAULT) {
		WordList_AddList(pWList, pLexCurrent->pKeyWords->pszKeyWords[(ch == '#') ? 1 : 2]); // directive, attribute
		return AutoC_AddSpecWord_Keyword;
	}
	return 0;
}

void EditCompleteUpdateConfig(void) {
	int i = 0;
	const int mask = autoCompletionConfig.fAutoCompleteFillUpMask;
	if (mask & AutoCompleteFillUpSpace) {
		autoCompletionConfig.szAutoCompleteFillUp[i++] = ' ';
	}

	const BOOL punctuation = mask & AutoCompleteFillUpPunctuation;
	int k = 0;
	for (UINT j = 0; j < COUNTOF(autoCompletionConfig.wszAutoCompleteFillUp); j++) {
		const WCHAR c = autoCompletionConfig.wszAutoCompleteFillUp[j];
		if (c == L'\0') {
			break;
		}
		if (IsPunctuation(c)) {
			autoCompletionConfig.wszAutoCompleteFillUp[k++] = c;
			if (punctuation) {
				autoCompletionConfig.szAutoCompleteFillUp[i++] = (char)c;
			}
		}
	}

	autoCompletionConfig.szAutoCompleteFillUp[i] = '\0';
	autoCompletionConfig.wszAutoCompleteFillUp[k] = L'\0';
}

static BOOL EditCompleteWordCore(int iCondition, BOOL autoInsert) {
	const Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	const int iCurrentStyle = SciCall_GetStyleAt(iCurrentPos);
	const Sci_Line iLine = SciCall_LineFromPosition(iCurrentPos);
	const Sci_Position iLineStartPos = SciCall_PositionFromLine(iLine);

	// word before current position
	Sci_Position iStartWordPos = iCurrentPos;
	do {
		Sci_Position before = iStartWordPos;
		iStartWordPos = SciCall_WordStartPosition(before, TRUE);
		const BOOL nonWord = iStartWordPos == before;
		before = SciCall_PositionBefore(iStartWordPos);
		if (nonWord) {
			// non-word
			if (before + 1 != iStartWordPos) {
				break;
			}

			const int ch = SciCall_GetCharAt(before);
			if (!IsDocWordChar(ch) || IsSpecialStartChar(ch, '\0')) {
				break;
			}

			iStartWordPos = before;
		} else {
			const Sci_Position iPos = SciCall_WordEndPosition(before, TRUE);
			if (iPos == iStartWordPos) {
				// after CJK word
				break;
			}
		}
	} while (iStartWordPos > iLineStartPos);
	if (iStartWordPos == iCurrentPos) {
		return FALSE;
	}

	// beginning of word
	int ch = SciCall_GetCharAt(iStartWordPos);

	int chPrev = '\0';
	int chPrev2 = '\0';
	if (ch < 0x80 && iStartWordPos > iLineStartPos) {
		Sci_Position before = SciCall_PositionBefore(iStartWordPos);
		if (before + 1 == iStartWordPos) {
			chPrev = SciCall_GetCharAt(before);
			const Sci_Position before2 = SciCall_PositionBefore(before);
			if (before2 >= iLineStartPos && before2 + 1 == before) {
				chPrev2 = SciCall_GetCharAt(before2);
			}
			if ((chPrev == '\\' && chPrev2 != '\\' && IsEscapeCharEx(ch, SciCall_GetStyleAt(before))) // word after escape char
				// word after format char
				|| (chPrev == '%' && IsStringFormatChar(ch, SciCall_GetStyleAt(before)))) {
				++iStartWordPos;
				ch = SciCall_GetCharAt(iStartWordPos);
				chPrev = '\0';
			}
		}
	}

	int iRootLen = autoCompletionConfig.iMinWordLength;
	if (ch >= '0' && ch <= '9') {
		if (autoCompletionConfig.iMinNumberLength <= 0) { // ignore number
			return FALSE;
		}

		iRootLen = autoCompletionConfig.iMinNumberLength;
		if (ch == '0') {
			// number prefix
			const int chNext = SciCall_GetCharAt(iStartWordPos + 1) | 0x20;
			if (chNext == 'x' || chNext == 'b' || chNext == 'o') {
				iRootLen += 2;
			}
		}
	}

	if (iCurrentPos - iStartWordPos < iRootLen) {
		return FALSE;
	}

	// preprocessor like: # space preprocessor
	if (pLexCurrent->rid == NP2LEX_CPP && (chPrev == '#' || IsASpaceOrTab(chPrev))) {
		Sci_Position before = iStartWordPos - 1;
		if (chPrev != '#') {
			while (before >= iLineStartPos) {
				chPrev = SciCall_GetCharAt(before);
				if (!IsASpaceOrTab(chPrev)) {
					break;
				}
				--before;
			}
		}
		if (chPrev == '#') {
			if (before > iLineStartPos) {
				--before;
				while (before >= iLineStartPos && IsASpaceOrTab(SciCall_GetCharAt(before))) {
					--before;
				}
				if (before >= iLineStartPos) {
					chPrev = '\0';
				}
			}
			ch = chPrev;
		}
		chPrev = '\0';
	} else if (IsSpecialStartChar(chPrev, chPrev2)) {
		ch = chPrev;
		chPrev = chPrev2;
	}

	// optimization for small string
	char onStack[128];
	char *pRoot;
	if (iCurrentPos - iStartWordPos + 1 < (Sci_Position)sizeof(onStack)) {
		ZeroMemory(onStack, sizeof(onStack));
		pRoot = onStack;
	} else {
		pRoot = (char *)NP2HeapAlloc(iCurrentPos - iStartWordPos + 1);
	}

	struct Sci_TextRange tr = { { iStartWordPos, iCurrentPos }, pRoot };
	SciCall_GetTextRange(&tr);
	iRootLen = (int)strlen(pRoot);

#if 0
	StopWatch watch;
	StopWatch_Start(watch);
#endif

	BOOL bIgnore = iRootLen != 0 && (pRoot[0] >= '0' && pRoot[0] <= '9'); // number
	const BOOL bIgnoreCase = bIgnore || autoCompletionConfig.bIgnoreCase;
	struct WordList *pWList = WordList_Alloc(pRoot, iRootLen, bIgnoreCase);
	BOOL bIgnoreDoc = FALSE;
	char prefix = '\0';

	if (!bIgnore && IsSpecialStartChar(ch, chPrev)) {
		const int result = AutoC_AddSpecWord(pWList, iCurrentStyle, ch, chPrev);
		if (result == AutoC_AddSpecWord_Finish) {
			bIgnore = TRUE;
			bIgnoreDoc = TRUE;
		} else if (result == AutoC_AddSpecWord_Keyword) {
			bIgnore = TRUE;
			// HTML/XML Tag
			if (ch == '/' || ch == '>') {
				ch = '<';
			}
			prefix = (char)ch;
		}
	}

	BOOL retry = FALSE;
	const BOOL bScanWordsInDocument = autoCompletionConfig.bScanWordsInDocument;
	do {
		if (!bIgnore) {
			// keywords
			AutoC_AddKeyword(pWList, iCurrentStyle);
		}
		if (bScanWordsInDocument) {
			if (!bIgnoreDoc || pWList->nWordCount == 0) {
				AutoC_AddDocWord(pWList, bIgnoreCase, prefix);
			}
			if (prefix && pWList->nWordCount == 0) {
				prefix = '\0';
				AutoC_AddDocWord(pWList, bIgnoreCase, prefix);
			}
		}

		retry = FALSE;
		if (pWList->nWordCount == 0 && iRootLen != 0) {
			const char *pSubRoot = strpbrk(pWList->pWordStart, ":.#@<\\/->$%");
			if (pSubRoot) {
				while (IsSpecialStart(*pSubRoot)) {
					pSubRoot++;
				}
				if (*pSubRoot) {
					iRootLen = (int)strlen(pSubRoot);
					WordList_UpdateRoot(pWList, pSubRoot, iRootLen);
					retry = TRUE;
					bIgnore = FALSE;
					bIgnoreDoc = FALSE;
					prefix = '\0';
				}
			}
		}
	} while (retry);

#if 0
	StopWatch_Stop(watch);
	const double elapsed = StopWatch_Get(&watch);
	printf("Notepad2 AddDocWord(%u, %u): %.6f\n", pWList->nWordCount, pWList->nTotalLen, elapsed);
#endif

	const BOOL bShow = pWList->nWordCount > 0 && !(pWList->nWordCount == 1 && pWList->iMaxLength == iRootLen);
	const BOOL bUpdated = (autoCompletionConfig.iPreviousItemCount == 0)
		// deleted some words. leave some words that no longer matches current input at the top.
		|| (iCondition == AutoCompleteCondition_OnCharAdded && autoCompletionConfig.iPreviousItemCount - pWList->nWordCount > autoCompletionConfig.iVisibleItemCount)
		// added some words. TODO: check top matched items before updating, if top items not changed, delay the update.
		|| (iCondition == AutoCompleteCondition_OnCharDeleted && autoCompletionConfig.iPreviousItemCount < pWList->nWordCount);

	if (bShow && bUpdated) {
		autoCompletionConfig.iPreviousItemCount = pWList->nWordCount;
		char *pList = WordList_GetList(pWList);
		SciCall_AutoCSetOrder(SC_ORDER_PRESORTED); // pre-sorted
		SciCall_AutoCSetIgnoreCase(bIgnoreCase); // case sensitivity
		//if (bIgnoreCase) {
		//	SciCall_AutoCSetCaseInsensitiveBehaviour(SC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
		//}
		SciCall_AutoCSetSeparator('\n');
		SciCall_AutoCSetFillUps(autoCompletionConfig.szAutoCompleteFillUp);
		//SciCall_AutoCSetDropRestOfWord(TRUE); // delete orginal text: pRoot
		SciCall_AutoCSetMaxWidth(pWList->iMaxLength << 1); // width columns, default auto
		SciCall_AutoCSetMaxHeight(min_u(pWList->nWordCount, autoCompletionConfig.iVisibleItemCount)); // visible rows
		SciCall_AutoCSetCancelAtStart(FALSE); // don't cancel the list when deleting character
		SciCall_AutoCSetChooseSingle(autoInsert);
		SciCall_AutoCShow(pWList->iStartLen, pList);
		NP2HeapFree(pList);
	}

	if (pRoot != onStack) {
		NP2HeapFree(pRoot);
	}
	WordList_Free(pWList);
	NP2HeapFree(pWList);
	return bShow;
}

void EditCompleteWord(int iCondition, BOOL autoInsert) {
	if (iCondition == AutoCompleteCondition_OnCharAdded) {
		if (autoCompletionConfig.iPreviousItemCount <= 2*autoCompletionConfig.iVisibleItemCount) {
			return;
		}
		// too many words in auto-completion list, recreate it.
	}

	if (iCondition == AutoCompleteCondition_Normal) {
		autoCompletionConfig.iPreviousItemCount = 0; // recreate list
	}

	BOOL bShow = EditCompleteWordCore(iCondition, autoInsert);
	if (!bShow) {
		autoCompletionConfig.iPreviousItemCount = 0;
		if (iCondition != AutoCompleteCondition_Normal) {
			SciCall_AutoCCancel();
		}
	}
}

static BOOL CanAutoCloseSingleQuote(int chPrev, int iCurrentStyle) {
	const int iLexer = pLexCurrent->iLexer;
	if (chPrev >= 0x80	// someone's
		|| (iLexer == SCLEX_CPP && iCurrentStyle == SCE_C_NUMBER)
		|| (iLexer == SCLEX_JULIA && iCurrentStyle == SCE_MAT_OPERATOR) // transpose operator
		|| (iLexer == SCLEX_LISP && iCurrentStyle == SCE_C_OPERATOR)
		|| (iLexer == SCLEX_MATLAB && iCurrentStyle == SCE_MAT_OPERATOR) // transpose operator
		|| (iLexer == SCLEX_PERL && iCurrentStyle == SCE_PL_OPERATOR && chPrev == '&') // SCE_PL_IDENTIFIER
		|| ((iLexer == SCLEX_VB || iLexer == SCLEX_VBSCRIPT) && (iCurrentStyle == SCE_B_DEFAULT || iCurrentStyle == SCE_B_COMMENT))
		|| (iLexer == SCLEX_VERILOG && (iCurrentStyle == SCE_V_NUMBER || iCurrentStyle == SCE_V_DEFAULT))
		|| (iLexer == SCLEX_TEXINFO && iCurrentStyle == SCE_L_DEFAULT && chPrev == '@') // SCE_L_SPECIAL
	) {
		return FALSE;
	}

	// someone's, don't
	if (IsAlphaNumeric(chPrev)) {
		// character prefix
		if (pLexCurrent->rid == NP2LEX_CPP || pLexCurrent->rid == NP2LEX_RC || iLexer == SCLEX_PYTHON || iLexer == SCLEX_SQL || iLexer == SCLEX_RUST) {
			const int lower = chPrev | 0x20;
			const int chPrev2 = SciCall_GetCharAt(SciCall_GetCurrentPos() - 3);
			const BOOL bSubWord = chPrev2 >= 0x80 || IsAlphaNumeric(chPrev2);

			switch (iLexer) {
			case SCLEX_CPP:
				return (lower == 'u' || chPrev == 'L') && !bSubWord;

			case SCLEX_PYTHON: {
				const int lower2 = chPrev2 | 0x20;
				return (lower == 'r' || lower == 'u' || lower == 'b' || lower == 'f') && (!bSubWord || (lower != lower2 && (lower2 == 'r' || lower2 == 'u' || lower2 == 'b' || lower2 == 'f')));
			}

			case SCLEX_SQL:
				return (lower == 'q' || lower == 'x' || lower == 'b') && !bSubWord;

			case SCLEX_RUST:
				return chPrev == 'b' && !bSubWord; // bytes
			}
		}
		return FALSE;
	}
	if (iLexer == SCLEX_RUST) {
		// lifetime
		return FALSE;
	}

	return TRUE;
}

BOOL EditIsOpenBraceMatched(Sci_Position pos, Sci_Position startPos) {
	// SciCall_GetEndStyled() is SciCall_GetCurrentPos() - 1
#if 0
	// style current line, ensure brace matching on current line matched with style
	const Sci_Line iLine = SciCall_LineFromPosition(pos);
	SciCall_EnsureStyledTo(SciCall_PositionFromLine(iLine + 1));
#else
	// only find close brace with same style in next 1KiB text
	const Sci_Position iDocLen = SciCall_GetLength();
	SciCall_EnsureStyledTo(min_pos(iDocLen, pos + 1024));
#endif
	// find next close brace
	const Sci_Position iPos = SciCall_BraceMatchNext(pos, startPos);
	if (iPos != -1) {
		// style may not matched when iPos > SciCall_GetEndStyled() (e.g. iPos on next line), see Document::BraceMatch()
#if 0
		SciCall_EnsureStyledTo(iPos + 1);
#endif
		// TODO: retry when style not matched
		if (SciCall_GetStyleAt(pos) == SciCall_GetStyleAt(iPos)) {
			// check whether next close brace already matched
			return pos == 0 || SciCall_BraceMatchNext(iPos, SciCall_PositionBefore(pos)) == -1;
		}
	}
	return FALSE;
}

void EditAutoCloseBraceQuote(int ch) {
	const Sci_Position iCurPos = SciCall_GetCurrentPos();
	const int chPrev = SciCall_GetCharAt(iCurPos - 2);
	const int chNext = SciCall_GetCharAt(iCurPos);
	const int iPrevStyle = SciCall_GetStyleAt(iCurPos - 2);
	const int iNextStyle = SciCall_GetStyleAt(iCurPos);

	if (pLexCurrent->iLexer == SCLEX_CPP) {
		// within char
		if (iPrevStyle == SCE_C_CHARACTER && iNextStyle == SCE_C_CHARACTER && pLexCurrent->rid != NP2LEX_PHP) {
			return;
		}
		if (ch == '`' && !IsCppStringStyle(iPrevStyle)) {
			return;
		}
	}

	// escape sequence
	if (ch != ',' && (chPrev == '\\' || (pLexCurrent->iLexer == SCLEX_BATCH && chPrev == '^'))) {
		return;
	}

	const int mask = autoCompletionConfig.fAutoInsertMask;
	char fillChar = '\0';
	BOOL closeBrace = FALSE;
	switch (ch) {
	case '(':
		if (mask & AutoInsertParenthesis) {
			fillChar = ')';
			closeBrace = TRUE;
		}
		break;
	case '[':
		if ((mask & AutoInsertSquareBracket) && !(pLexCurrent->rid == NP2LEX_SMALI)) { // JVM array type
			fillChar = ']';
			closeBrace = TRUE;
		}
		break;
	case '{':
		if (mask & AutoInsertBrace) {
			fillChar = '}';
			closeBrace = TRUE;
		}
		break;
	case '<':
		if ((mask & AutoInsertAngleBracket) && (pLexCurrent->rid == NP2LEX_CPP || pLexCurrent->rid == NP2LEX_CSHARP || pLexCurrent->rid == NP2LEX_JAVA)) {
			// geriatric type, template
			if (iPrevStyle == SCE_C_CLASS || iPrevStyle == SCE_C_INTERFACE || iPrevStyle == SCE_C_STRUCT) {
				fillChar = '>';
			}
		}
		break;
	case '\"':
		if ((mask & AutoInsertDoubleQuote)) {
			fillChar = '\"';
		}
		break;
	case '\'':
		if ((mask & AutoInsertSingleQuote) && CanAutoCloseSingleQuote(chPrev, iPrevStyle)) {
			fillChar = '\'';
		}
		break;
	case '`':
		//if (pLexCurrent->iLexer == SCLEX_BASH
		//|| pLexCurrent->rid == NP2LEX_JULIA
		//|| pLexCurrent->iLexer == SCLEX_MAKEFILE
		//|| pLexCurrent->iLexer == SCLEX_SQL
		//) {
		//	fillChar = '`';
		//} else if (0) {
		//	fillChar = '\'';
		//}
		if (mask & AutoInsertBacktick) {
			fillChar = '`';
		}
		break;
	case ',':
		if ((mask & AutoInsertSpaceAfterComma) && !(chNext == ' ' || chNext == '\t' || (chPrev == '\'' && chNext == '\'') || (chPrev == '\"' && chNext == '\"'))) {
			fillChar = ' ';
		}
		break;
	default:
		break;
	}

	if (fillChar) {
		if (closeBrace && EditIsOpenBraceMatched(iCurPos - 1, iCurPos)) {
			return;
		}
		// TODO: auto escape quotes inside string
		//else if (ch == fillChar) {
		//}

		const char tchIns[4] = { fillChar };
		SciCall_BeginUndoAction();
		SciCall_ReplaceSel(tchIns);
		const Sci_Position iCurrentPos = (ch == ',') ? iCurPos + 1 : iCurPos;
		SciCall_SetSel(iCurrentPos, iCurrentPos);
		SciCall_EndUndoAction();
		if (closeBrace) {
			// fix brace matching
			SciCall_EnsureStyledTo(iCurPos + 1);
		}
	}
}

static inline BOOL IsHtmlVoidTag(const char *word, int length) {
	// see classifyTagHTML() in LexHTML.cxx
	const char *p = StrStrIA(
		// void elements
		" area base basefont br col command embed frame hr img input isindex keygen link meta param source track wbr "
		, word);
	return p != NULL && p[-1] == ' ' && p[length] == ' ';
}

void EditAutoCloseXMLTag(void) {
	char tchBuf[512];
	const Sci_Position iCurPos = SciCall_GetCurrentPos();
	const Sci_Position iStartPos = max_pos(0, iCurPos - (COUNTOF(tchBuf) - 1));
	const Sci_Position iSize = iCurPos - iStartPos;
	BOOL shouldAutoClose = iSize >= 3;
	BOOL autoClosed = FALSE;

	if (shouldAutoClose && pLexCurrent->iLexer == SCLEX_CPP) {
		int iCurrentStyle = SciCall_GetStyleAt(iCurPos);
		if (iCurrentStyle == SCE_C_OPERATOR || iCurrentStyle == SCE_C_DEFAULT) {
			shouldAutoClose = FALSE;
		} else {
			const Sci_Line iLine = SciCall_LineFromPosition(iCurPos);
			Sci_Position iCurrentLinePos = SciCall_PositionFromLine(iLine);
			while (iCurrentLinePos < iCurPos && IsASpaceOrTab(SciCall_GetCharAt(iCurrentLinePos))) {
				iCurrentLinePos++;
			}
			iCurrentStyle = SciCall_GetStyleAt(iCurrentLinePos);
			if (SciCall_GetCharAt(iCurrentLinePos) == '#' && iCurrentStyle == SCE_C_PREPROCESSOR) {
				shouldAutoClose = FALSE;
			}
		}
	}

	if (shouldAutoClose) {
		struct Sci_TextRange tr = { { iStartPos, iCurPos }, tchBuf };
		SciCall_GetTextRange(&tr);

		if (tchBuf[iSize - 2] != '/') {
			char tchIns[516] = "</";
			int cchIns = 2;
			const char *pBegin = tchBuf;
			const char *pCur = tchBuf + iSize - 2;

			while (pCur > pBegin && *pCur != '<' && *pCur != '>') {
				--pCur;
			}

			if (*pCur == '<') {
				pCur++;
				while (IsHtmlTagChar(*pCur)) {
					tchIns[cchIns++] = *pCur;
					pCur++;
				}
			}

			tchIns[cchIns++] = '>';
			tchIns[cchIns] = '\0';

			shouldAutoClose = cchIns > 3;
			if (shouldAutoClose && pLexCurrent->iLexer == SCLEX_HTML) {
				tchIns[cchIns - 1] = '\0';
				shouldAutoClose = !IsHtmlVoidTag(tchIns + 2, cchIns - 3);
			}
			if (shouldAutoClose) {
				tchIns[cchIns - 1] = '>';
				autoClosed = TRUE;
				SciCall_BeginUndoAction();
				SciCall_ReplaceSel(tchIns);
				SciCall_SetSel(iCurPos, iCurPos);
				SciCall_EndUndoAction();
			}
		}
	}
	if (!autoClosed && autoCompletionConfig.bCompleteWord) {
		const Sci_Position iPos = SciCall_GetCurrentPos();
		if (SciCall_GetCharAt(iPos - 2) == '-') {
			EditCompleteWord(AutoCompleteCondition_Normal, FALSE); // obj->field, obj->method
		}
	}
}

BOOL IsIndentKeywordStyle(int style) {
	switch (pLexCurrent->iLexer) {
	//case SCLEX_AU3:
	//	return style == SCE_AU3_KEYWORD;
	case SCLEX_BASH:
		return style == SCE_BAT_WORD;

	case SCLEX_CMAKE:
		return style == SCE_CMAKE_WORD;
	//case SCLEX_CPP:
	//	return style == SCE_C_PREPROCESSOR;

	//case SCLEX_INNOSETUP:
	//	return style == SCE_INNO_KEYWORD_PASCAL;
	case SCLEX_JULIA:
		return style == SCE_JULIA_WORD;
	case SCLEX_LUA:
		return style == SCE_LUA_WORD;
	case SCLEX_MAKEFILE:
		return style == SCE_MAKE_PREPROCESSOR;
	case SCLEX_MATLAB:
		return style == SCE_MAT_KEYWORD;
	//case SCLEX_NSIS:
	//	return style == SCE_C_WORD || style == SCE_C_PREPROCESSOR;

	//case SCLEX_PASCAL:
	case SCLEX_RUBY:
		return style == SCE_RB_WORD;
	case SCLEX_SQL:
		return style == SCE_SQL_WORD;

	//case SCLEX_VB:
	//case SCLEX_VBSCRIPT:
	//	return style == SCE_B_KEYWORD;
	//case SCLEX_VERILOG:
	//	return style == SCE_V_WORD;
	//case SCLEX_VHDL:
	//	return style == SCE_VHDL_KEYWORD;
	}
	return FALSE;
}

const char *EditKeywordIndent(const char *head, int *indent) {
	char word[64] = "";
	char word_low[64] = "";
	int length = 0;
	const char *endPart = NULL;
	*indent = 0;

	while (*head && length < 63 && IsAlpha(*head)) {
		word[length] = *head;
		word_low[length] = (char)((*head) | 0x20);
		++length;
		++head;
	}

	switch (pLexCurrent->iLexer) {
	//case SCLEX_AU3:
	case SCLEX_BASH:
		if (np2LexLangIndex == IDM_LEXER_CSHELL) {
			if (!strcmp(word, "if")) {
				*indent = 2;
				endPart = "endif";
			} else if (!strcmp(word, "switch")) {
				*indent = 2;
				endPart = "endsw";
			} else if (!strcmp(word, "foreach") || !strcmp(word, "while")) {
				*indent = 2;
				endPart = "end";
			}
		} else {
			if (!strcmp(word, "if")) {
				*indent = 2;
				endPart = "fi";
			} else if (!strcmp(word, "case")) {
				*indent = 2;
				endPart = "esac";
			} else if (!strcmp(word, "do")) {
				*indent = 2;
				endPart = "done";
			}
		}
		break;

	case SCLEX_CMAKE:
		if (!strcmp(word, "function")) {
			*indent = 2;
			endPart = "endfunction()";
		} else if (!strcmp(word, "macro")) {
			*indent = 2;
			endPart = "endmacro()";
		} else if (!strcmp(word, "if")) {
			*indent = 2;
			endPart = "endif()";
		} else if (!strcmp(word, "foreach")) {
			*indent = 2;
			endPart = "endforeach()";
		} else if (!strcmp(word, "while")) {
			*indent = 2;
			endPart = "endwhile()";
		}
		break;
	//case SCLEX_CPP:

	//case SCLEX_INNOSETUP:

	case SCLEX_JULIA:
		if (!strcmp(word, "function")
			|| !strcmp(word, "begin")
			|| !strcmp(word, "do")
			|| !strcmp(word, "for")
			|| !strcmp(word, "if")
			|| !strcmp(word, "let")
			|| !strcmp(word, "macro")
			|| !strcmp(word, "module")
			|| !strcmp(word, "quote")
			|| !strcmp(word, "struct")
			|| !strcmp(word, "try")
			|| !strcmp(word, "type")
			|| !strcmp(word, "while")
			|| !strcmp(word, "baremodule")) {
			*indent = 2;
			endPart = "end";
		}
		break;

	case SCLEX_LUA:
		if (!strcmp(word, "function") || !strcmp(word, "if") || !strcmp(word, "do")) {
			*indent = 2;
			endPart = "end";
		}
		break;

	case SCLEX_MAKEFILE:
		if (!strcmp(word, "if")) {
			*indent = 2;
			endPart = "endif";
		} else if (!strcmp(word, "define")) {
			*indent = 2;
			endPart = "endef";
		} else if (!strcmp(word, "for")) {
			*indent = 2;
			endPart = "endfor";
		}
		break;
	case SCLEX_MATLAB:
		if (!strcmp(word, "function")) {
			*indent = 1;
			// 'end' is optional
		} else if (!strcmp(word, "if") || !strcmp(word, "for") || !strcmp(word, "while") || !strcmp(word, "switch") || !strcmp(word, "try")) {
			*indent = 2;
			if (pLexCurrent->rid == NP2LEX_OCTAVE || np2LexLangIndex == IDM_LEXER_OCTAVE) {
				if (strcmp(word, "if") == 0) {
					endPart = "endif";
				} else if (strcmp(word, "for") == 0) {
					endPart = "endfor";
				} else if (strcmp(word, "while") == 0) {
					endPart = "endwhile";
				} else if (strcmp(word, "switch") == 0) {
					endPart = "endswitch";
				} else if (strcmp(word, "try") == 0) {
					endPart = "end_try_catch";
				}
			}
			if (endPart == NULL) {
				endPart = "end";
			}
		}
		break;

	//case SCLEX_NSIS:
	//case SCLEX_PASCAL:
	case SCLEX_RUBY:
		if (!strcmp(word, "if") || !strcmp(word, "do") || !strcmp(word, "while") || !strcmp(word, "for")) {
			*indent = 2;
			endPart = "end";
		}
		break;

	case SCLEX_SQL:
		if (!strcmp(word_low, "if")) {
			*indent = 2;
			endPart = "END IF;";
		} else if (!strcmp(word_low, "while")) {
			*indent = 2;
			endPart = "END WHILE;";
		} else if (!strcmp(word_low, "repeat")) {
			*indent = 2;
			endPart = "END REPEAT;";
		} else if (!strcmp(word_low, "loop") || !strcmp(word_low, "for")) {
			*indent = 2;
			endPart = "END LOOP;";
		} else if (!strcmp(word_low, "case")) {
			*indent = 2;
			endPart = "END CASE;";
		} else if (!strcmp(word_low, "begin")) {
			*indent = 2;
			if (StrStrIA(head, "transaction") != NULL) {
				endPart = "COMMIT;";
			} else {
				endPart = "END";
			}
		} else if (!strcmp(word_low, "start")) {
			if (StrStrIA(head, "transaction") != NULL) {
				*indent = 2;
				endPart = "COMMIT;";
			}
		}
		break;

	//case SCLEX_VB:
	//case SCLEX_VBSCRIPT:
	//case SCLEX_VERILOG:
	//case SCLEX_VHDL:
	}
	return endPart;
}

extern BOOL	bTabsAsSpaces;
extern BOOL	bTabIndents;
extern int	iTabWidth;
extern int	iIndentWidth;

void EditAutoIndent(void) {
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	//const Sci_Position iAnchorPos = SciCall_GetAnchor();
	const Sci_Line iCurLine = SciCall_LineFromPosition(iCurPos);
	//const Sci_Position iLineLength = SciCall_GetLineLength(iCurLine);
	//const Sci_Position iIndentBefore = SciCall_GetLineIndentation(iCurLine - 1);

	// Move bookmark along with line if inserting lines (pressing return at beginning of line) because Scintilla does not do this for us
	if (iCurLine > 0) {
		const Sci_Position iPrevLineLength = SciCall_GetLineEndPosition(iCurLine - 1) - SciCall_PositionFromLine(iCurLine - 1);
		if (iPrevLineLength == 0) {
			const Sci_MarkerMask bitmask = SciCall_MarkerGet(iCurLine - 1);
			if (bitmask & MarkerBitmask_Bookmark) {
				SciCall_MarkerDelete(iCurLine - 1, MarkerNumber_Bookmark);
				SciCall_MarkerAdd(iCurLine, MarkerNumber_Bookmark);
			}
		}
	}

	if (iCurLine > 0/* && iLineLength <= 2*/) {
		const Sci_Position iPrevLineLength = SciCall_GetLineLength(iCurLine - 1);
		if (iPrevLineLength < 2) {
			return;
		}
		char *pLineBuf = (char *)NP2HeapAlloc(2 * iPrevLineLength + 1 + iIndentWidth * 2 + 2 + 64);
		if (pLineBuf == NULL) {
			return;
		}

		const int iEOLMode = SciCall_GetEOLMode();
		int indent = 0;
		Sci_Position iIndentLen = 0;
		int commentStyle = 0;
		SciCall_GetLine(iCurLine - 1, pLineBuf);
		pLineBuf[iPrevLineLength] = '\0';

		int ch = pLineBuf[iPrevLineLength - 2];
		if (ch == '\r') {
			ch = pLineBuf[iPrevLineLength - 3];
			iIndentLen = 1;
		}
		if (ch == '{' || ch == '[' || ch == '(') {
			indent = 2;
		} else if (ch == ':') { // case label/Python
			indent = 1;
		} else if (ch == '*' || ch == '!') { // indent block comment
			iIndentLen = iPrevLineLength - (2 + iIndentLen);
			if (iIndentLen >= 2 && pLineBuf[iIndentLen - 2] == '/' && pLineBuf[iIndentLen - 1] == '*') {
				indent = 1;
				commentStyle = 1;
			}
		}

		iIndentLen = 0;
		ch = SciCall_GetCharAt(SciCall_PositionFromLine(iCurLine));
		const BOOL closeBrace = (ch == '}' || ch == ']' || ch == ')');
		if (indent == 2 && !closeBrace) {
			indent = 1;
		}

		char *pPos;
		const char *endPart = NULL;
		for (pPos = pLineBuf; *pPos; pPos++) {
			if (*pPos != ' ' && *pPos != '\t') {
				if (!indent && IsAlpha(*pPos)) { // indent on keywords
					const int style = SciCall_GetStyleAt(SciCall_PositionFromLine(iCurLine - 1) + iIndentLen);
					if (IsIndentKeywordStyle(style)) {
						endPart = EditKeywordIndent(pPos, &indent);
					}
				}
				if (indent) {
					ZeroMemory(pPos, iPrevLineLength - iIndentLen);
				}
				*pPos = '\0';
				break;
			}
			iIndentLen += 1;
		}

		if (indent == 2 && endPart) {
			const int level = SciCall_GetFoldLevel(iCurLine);
			if (!(level & SC_FOLDLEVELHEADERFLAG)) {
				const Sci_Line parent = SciCall_GetFoldParent(iCurLine);
				if (parent >= 0 && parent + 1 == iCurLine) {
					const Sci_Line child = SciCall_GetLastChild(parent);
					// TODO: check endPart is on this line
					if (SciCall_GetLineLength(child)) {
						indent = 1;
					}
				} else {
					indent = 0;
				}
			}
		}

		Sci_Position iIndentPos = iCurPos;
		if (indent) {
			int pad = iIndentWidth;
			iIndentPos += iIndentLen;
			ch = ' ';
			if (bTabIndents) {
				if (bTabsAsSpaces) {
					pad = iTabWidth;
					ch = ' ';
				} else {
					pad = 1;
					ch = '\t';
				}
			}
			if (commentStyle) {
				iIndentPos += 2;
				*pPos++ = ' ';
				*pPos++ = '*';
			} else {
				iIndentPos += pad;
				while (pad-- > 0) {
					*pPos++ = (char)ch;
				}
			}
			if (indent == 2) {
				switch (iEOLMode) {
				case SC_EOL_CRLF:
					*pPos++ = '\r';
					*pPos++ = '\n';
					break;
				case SC_EOL_LF:
					*pPos++ = '\n';
					break;
				case SC_EOL_CR:
					*pPos++ = '\r';
					break;
				}
				strncpy(pPos, pLineBuf, iIndentLen + 1);
				pPos += iIndentLen;
				if (endPart) {
					iIndentLen = strlen(endPart);
					memcpy(pPos, endPart, iIndentLen);
					pPos += iIndentLen;
				}
			}
			*pPos = '\0';
		}

		if (*pLineBuf) {
			SciCall_BeginUndoAction();
			SciCall_AddText(strlen(pLineBuf), pLineBuf);
			if (indent) {
				SciCall_SetSel(iIndentPos, iIndentPos);
			}
			SciCall_EndUndoAction();

			//const Sci_Position iPrevLineStartPos = SciCall_PositionFromLine(iCurLine - 1);
			//const Sci_Position iPrevLineEndPos = SciCall_GetLineEndPosition(iCurLine - 1);
			//const Sci_Position iPrevLineIndentPos = SciCall_GetLineIndentPosition(iCurLine - 1);

			//if (iPrevLineEndPos == iPrevLineIndentPos) {
			//	SciCall_BeginUndoAction();
			//	SciCall_SetTargetRange(iPrevLineStartPos, iPrevLineEndPos);
			//	SciCall_ReplaceTarget(0, "");
			//	SciCall_EndUndoAction();
			//}
		}

		NP2HeapFree(pLineBuf);
		//const Sci_Position iIndent = SciCall_GetLineIndentation(iCurLine);
		//SciCall_SetLineIndentation(iCurLine, iIndentBefore);
		//iIndentLen = SciCall_GetLineIndentation(iCurLine);
		//if (iIndentLen > 0)
		//	SciCall_SetSel(iAnchorPos + iIndentLen, iCurPos + iIndentLen);
	}
}

void EditToggleCommentLine(void) {
	switch (pLexCurrent->iLexer) {
	case SCLEX_ASM: {
		LPCWSTR ch;
		switch (autoCompletionConfig.iAsmLineCommentChar) {
		case AsmLineCommentCharSemicolon:
		default:
			ch = L";";
			break;
		case AsmLineCommentCharSharp:
			ch = L"# ";
			break;
		case AsmLineCommentCharSlash:
			ch = L"//";
			break;
		case AsmLineCommentCharAt:
			ch = L"@ ";
			break;
		}
		EditToggleLineComments(ch, FALSE);
	}
	break;

	case SCLEX_AU3:
	case SCLEX_INNOSETUP:
	case SCLEX_LISP:
	case SCLEX_LLVM:
	case SCLEX_PROPERTIES:
		EditToggleLineComments(L";", FALSE);
		break;

	case SCLEX_BASH:
		EditToggleLineComments(((np2LexLangIndex == IDM_LEXER_M4)? L"dnl " : L"#"), FALSE);
		break;

	case SCLEX_BATCH:
		EditToggleLineComments(L"@rem ", TRUE);
		break;

	case SCLEX_CIL:
	case SCLEX_CSS: // for SCSS, LESS, HSS
	case SCLEX_DART:
	case SCLEX_FSHARP:
	case SCLEX_GO:
	case SCLEX_GRAPHVIZ:
	case SCLEX_JSON:
	case SCLEX_KOTLIN:
	case SCLEX_PASCAL:
	case SCLEX_RUST:
	case SCLEX_SWIFT:
	case SCLEX_VERILOG:
		EditToggleLineComments(L"//", FALSE);
		break;

	case SCLEX_AVS:
	case SCLEX_CMAKE:
	case SCLEX_CONF:
	case SCLEX_GN:
	case SCLEX_JULIA:
	case SCLEX_MAKEFILE:
	case SCLEX_NSIS:
	case SCLEX_PERL:
	case SCLEX_POWERSHELL:
	case SCLEX_PYTHON:
	case SCLEX_R:
	case SCLEX_RUBY:
	case SCLEX_SMALI:
	case SCLEX_TCL:
	case SCLEX_TOML:
	case SCLEX_YAML:
		EditToggleLineComments(L"#", FALSE);
		break;

	case SCLEX_CPP:
		switch (pLexCurrent->rid) {
		case NP2LEX_AWK:
		case NP2LEX_JAM:
			EditToggleLineComments(L"#", FALSE);
			break;
		default:
			EditToggleLineComments(L"//", FALSE);
			break;
		}
		break;

	case SCLEX_FORTRAN:
		EditToggleLineComments(L"!", FALSE);
		break;

	case SCLEX_HTML:
	case SCLEX_XML: {
		const int block = GetCurrentHtmlTextBlock();
		switch (block) {
		case HTML_TEXT_BLOCK_VBS:
			EditToggleLineComments(L"'", FALSE);
			break;

		case HTML_TEXT_BLOCK_PYTHON:
			EditToggleLineComments(L"#", FALSE);
			break;

		case HTML_TEXT_BLOCK_CDATA:
		case HTML_TEXT_BLOCK_JS:
		case HTML_TEXT_BLOCK_PHP:
			EditToggleLineComments(L"//", FALSE);
			break;
		}
	}
	break;

	case SCLEX_LATEX:
		EditToggleLineComments(L"%", FALSE);
		break;

	case SCLEX_LUA:
	case SCLEX_VHDL:
		EditToggleLineComments(L"--", FALSE);
		break;

	case SCLEX_MATLAB:
		if (pLexCurrent->rid == NP2LEX_SCILAB || np2LexLangIndex == IDM_LEXER_SCILAB) {
			EditToggleLineComments(L"//", FALSE);
		} else {
			EditToggleLineComments(L"%", FALSE);
		}
		break;

	case SCLEX_SQL:
		EditToggleLineComments(L"-- ", FALSE); // extra space
		break;

	case SCLEX_TEXINFO:
		EditToggleLineComments(L"@c ", FALSE);
		break;

	case SCLEX_VB:
	case SCLEX_VBSCRIPT:
		EditToggleLineComments(L"'", FALSE);
		break;

	case SCLEX_VIM:
		EditToggleLineComments(L"\"", FALSE);
		break;

	case SCLEX_WASM:
		EditToggleLineComments(L";;", FALSE);
		break;
	}
}

void EditEncloseSelectionNewLine(LPCWSTR pwszOpen, LPCWSTR pwszClose) {
	WCHAR start[64] = L"";
	WCHAR end[64] = L"";
	const int iEOLMode = SciCall_GetEOLMode();
	LPCWSTR lineEnd = (iEOLMode == SC_EOL_LF) ? L"\n" : ((iEOLMode == SC_EOL_CR) ? L"\r" : L"\r\n");

	Sci_Position pos = SciCall_GetSelectionStart();
	Sci_Line line = SciCall_LineFromPosition(pos);
	if (pos != SciCall_PositionFromLine(line)) {
		lstrcat(start, lineEnd);
	}
	lstrcat(start, pwszOpen);
	lstrcat(start, lineEnd);

	pos = SciCall_GetSelectionEnd();
	line = SciCall_LineFromPosition(pos);
	if (pos != SciCall_PositionFromLine(line)) {
		lstrcat(end, lineEnd);
	}
	lstrcat(end, pwszClose);
	lstrcat(end, lineEnd);
	EditEncloseSelection(start, end);
}

void EditToggleCommentBlock(void) {
	switch (pLexCurrent->iLexer) {
	case SCLEX_ASM:
	case SCLEX_AVS:
	case SCLEX_CIL:
	case SCLEX_CSS:
	case SCLEX_DART:
	case SCLEX_GO:
	case SCLEX_GRAPHVIZ:
	case SCLEX_JSON:
	case SCLEX_KOTLIN:
	case SCLEX_NSIS:
	case SCLEX_RUST:
	case SCLEX_SQL:
	case SCLEX_SWIFT:
	case SCLEX_VERILOG:
	case SCLEX_VHDL:
		EditEncloseSelection(L"/*", L"*/");
		break;

	case SCLEX_AU3:
		EditEncloseSelectionNewLine(L"#cs", L"#ce");
		break;

	case SCLEX_CMAKE:
		EditEncloseSelection(L"#[[", L"]]");
		break;

	case SCLEX_CPP:
		switch (pLexCurrent->rid) {
		case NP2LEX_AWK:
		case NP2LEX_JAM:
			break;

		default:
			EditEncloseSelection(L"/*", L"*/");
			break;
		}
		break;

	case SCLEX_FORTRAN:
		EditEncloseSelectionNewLine(L"#if 0", L"#endif");
		break;

	case SCLEX_FSHARP:
		EditEncloseSelection(L"(*", L"*)");
		break;

	case SCLEX_HTML:
	case SCLEX_XML: {
		const int block = GetCurrentHtmlTextBlock();
		switch (block) {
		case HTML_TEXT_BLOCK_TAG:
			EditEncloseSelection(L"<!--", L"-->");
			break;

		case HTML_TEXT_BLOCK_CDATA:
		case HTML_TEXT_BLOCK_JS:
		case HTML_TEXT_BLOCK_PHP:
		case HTML_TEXT_BLOCK_CSS:
			EditEncloseSelection(L"/*", L"*/");
			break;

		case HTML_TEXT_BLOCK_SGML:
			// A brief SGML tutorial
			// https://www.w3.org/TR/WD-html40-970708/intro/sgmltut.html
			EditEncloseSelection(L"--", L"--");
			break;
		}
	}
	break;

	case SCLEX_INNOSETUP:
	case SCLEX_PASCAL:
		EditEncloseSelection(L"{", L"}");
		break;

	case SCLEX_JULIA:
		EditEncloseSelection(L"#=", L"=#");
		break;

	case SCLEX_LATEX:
		EditEncloseSelectionNewLine(L"\\begin{comment}", L"\\end{comment}");
		break;

	case SCLEX_LISP:
		EditEncloseSelection(L"#|", L"|#");
		break;

	case SCLEX_LUA:
		EditEncloseSelection(L"--[[", L"--]]");
		break;

	case SCLEX_MATLAB:
		if (pLexCurrent->rid == NP2LEX_SCILAB || np2LexLangIndex == IDM_LEXER_SCILAB) {
			EditEncloseSelection(L"/*", L"*/");
		} else {
			EditEncloseSelectionNewLine(L"%{", L"%}");
		}
		break;

	case SCLEX_POWERSHELL:
		EditEncloseSelection(L"<#", L"#>");
		break;

	case SCLEX_R:
		EditEncloseSelectionNewLine(L"if (FALSE) {", L"}");
		break;

	case SCLEX_TCL:
		EditEncloseSelectionNewLine(L"if (0) {", L"}");
		break;

	case SCLEX_WASM:
		EditEncloseSelection(L"(;", L";)");
		break;
	}
}

// see Style_SniffShebang() in Styles.c
void EditInsertScriptShebangLine(void) {
	const char *prefix = "#!/usr/bin/env ";
	const char *name = NULL;

	switch (pLexCurrent->iLexer) {
	case SCLEX_BASH:
		switch (np2LexLangIndex) {
		case IDM_LEXER_CSHELL:
			prefix = "#!/bin/csh";
			break;

		case IDM_LEXER_M4:
			name = "m4";
			break;

		default:
			prefix = "#!/bin/bash";
			break;
		}
		break;

	case SCLEX_CPP:
		switch (pLexCurrent->rid) {
		case NP2LEX_AWK:
			name = "awk";
			break;

		case NP2LEX_GROOVY:
			name = "groovy";
			break;

		case NP2LEX_JS:
			name = "node";
			break;

		case NP2LEX_PHP:
			name = "php";
			break;

		case NP2LEX_SCALA:
			name = "scala";
			break;
		}
		break;

	//case SCLEX_KOTLIN:
	//	name = "kotlin";
	//	break;

	case SCLEX_LUA:
		name = "lua";
		break;

	case SCLEX_PERL:
		name = "perl";
		break;

	case SCLEX_PYTHON:
		name = "python3";
		break;

	case SCLEX_RUBY:
		name = "ruby";
		break;

	//case SCLEX_RUST:
	//	name = "rust";
	//	break;

	case SCLEX_TCL:
		name = "wish";
		break;
	}

	char line[128];
	strcpy(line, prefix);
	if (name != NULL) {
		strcat(line, name);
	}

	const Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	if (iCurrentPos == 0 && (name != NULL || pLexCurrent->iLexer == SCLEX_BASH)) {
		const int iEOLMode = SciCall_GetEOLMode();
		LPCSTR lineEnd = (iEOLMode == SC_EOL_LF) ? "\n" : ((iEOLMode == SC_EOL_CR) ? "\r" : "\r\n");
		strcat(line, lineEnd);
	}
	SciCall_ReplaceSel(line);
}

void EditShowCallTips(Sci_Position position) {
	const Sci_Line iLine = SciCall_LineFromPosition(position);
	const Sci_Position iDocLen = SciCall_GetLineLength(iLine);
	char *pLine = (char *)NP2HeapAlloc(iDocLen + 1);
	SciCall_GetLine(iLine, pLine);
	StrTrimA(pLine, " \t\r\n");
	char *text = (char *)NP2HeapAlloc(iDocLen + 1 + 128);
#if defined(_WIN64)
	sprintf(text, "ShowCallTips(%" PRId64 ", %" PRId64 ", %" PRId64 ")\n\n\002%s", iLine + 1, position, iDocLen, pLine);
#else
	sprintf(text, "ShowCallTips(%d, %d, %d)\n\n\002%s", (int)(iLine + 1), (int)position, (int)iDocLen, pLine);
#endif
	SciCall_CallTipUseStyle(iTabWidth);
	SciCall_CallTipShow(position, text);
	NP2HeapFree(pLine);
	NP2HeapFree(text);
}
