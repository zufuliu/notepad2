/******************************************************************************
*
*
* Notepad2
*
* Edit.h
*   Text File Editing Helper Stuff
*
* See Readme.txt for more information about this source code.
* Please send me your comments to this work.
*
* See License.txt for details about distribution and modification.
*
*                                              (c) Florian Balmer 1996-2011
*                                                  florian.balmer@gmail.com
*                                              https://www.flos-freeware.ch
*
*
******************************************************************************/
#pragma once

// WideCharToMultiByte, UTF8 encoding of U+0800 to U+FFFF
#define kMaxMultiByteCount	3

#define NP2_FIND_REPLACE_LIMIT	2048
#define NP2_LONG_LINE_LIMIT		4096

//#define NP2_RegexDefaultFlags	(SCFIND_REGEXP | SCFIND_CXX11REGEX) // use std::regex
#define NP2_RegexDefaultFlags	(SCFIND_REGEXP | SCFIND_POSIX) // use builtin regex
#define NP2_InvalidSearchFlags	(-1)
#define NP2_MarkAllMultiline	0x00001000
#define NP2_MarkAllBookmark		0x00002000
#define NP2_MarkAllSelectAll	0x00004000
#define NP2_FromFindAll			0x00008000
#define JSON_INDENT				4

typedef struct EDITFINDREPLACE {
	char	szFind[512];
	char	szReplace[512];
	char	szFindUTF8[512 * kMaxMultiByteCount];
	char	szReplaceUTF8[512 * kMaxMultiByteCount];
	HWND	hwnd;
	UINT	fuFlags;
	bool	bTransformBS;
	bool	bFindClose;
	bool	bReplaceClose;
	bool	bNoFindWrap;
	bool	bWildcardSearch;
} EDITFINDREPLACE, *LPEDITFINDREPLACE;

typedef const EDITFINDREPLACE * LPCEDITFINDREPLACE;

typedef enum EditAlignMode {
	EditAlignMode_Left = 0,
	EditAlignMode_Right = 1,
	EditAlignMode_Center = 2,
	EditAlignMode_Justify = 3,
	EditAlignMode_JustifyEx = 4,
} EditAlignMode;

typedef enum EditSortFlag {
	EditSortFlag_Ascending = 0,
	EditSortFlag_Descending = 1,
	EditSortFlag_IgnoreCase = 2,
	EditSortFlag_LogicalNumber = 4,
	EditSortFlag_ColumnSort = 8,
	EditSortFlag_GroupByFileType = 16,
	EditSortFlag_DontSort = 32,
	EditSortFlag_Shuffle = 64,
	EditSortFlag_MergeDuplicate = 128,
	EditSortFlag_RemoveDuplicate = 256,
	EditSortFlag_RemoveUnique = 512,
} EditSortFlag;

// wrap indent
enum {
	EditWrapIndent_None = 0,
	EditWrapIndent_OneCharacter = 1,
	EditWrapIndent_TwoCharacter = 2,
	EditWrapIndent_OneLevel = 3,
	EditWrapIndent_TwoLevel = 4,

	EditWrapIndent_SameAsSubline = 5,
	EditWrapIndent_OneLevelThanSubline = 6,
	EditWrapIndent_TwoLevelThanSubline = 7,

	EditWrapIndent_MaxValue = EditWrapIndent_TwoLevelThanSubline,
	EditWrapIndent_DefaultValue = EditWrapIndent_None,
};

// wrap symbol
enum {
	EditWrapSymbolBefore_None = 0,
	EditWrapSymbolBefore_NearText = 1,
	EditWrapSymbolBefore_NearBorder = 2,
	EditWrapSymbolBefore_MaxValue = EditWrapSymbolBefore_NearBorder,

	EditWrapSymbolAfter_None = 0,
	EditWrapSymbolAfter_NearText = 1,
	EditWrapSymbolAfter_NearBorder = 2,
	EditWrapSymbolAfter_LineNumberMargin = 3,
	EditWrapSymbolAfter_MaxValue = EditWrapSymbolAfter_LineNumberMargin,

	EditWrapSymbol_MaxValue = EditWrapSymbolBefore_MaxValue + 10 * EditWrapSymbolAfter_MaxValue,
	EditWrapSymbol_DefaultValue = EditWrapSymbolBefore_NearBorder,
};

void	Edit_ReleaseResources(void);
void	EditCreate(HWND hwndParent);
void	EditSetNewText(LPCSTR lpstrText, DWORD cbText, Sci_Line lineCount);

static inline void EditSetEmptyText(void) {
	EditSetNewText("", 0, 1);
}

bool	EditConvertText(UINT cpSource, UINT cpDest, bool bSetSavePoint);
void	EditConvertToLargeMode(void);
void	EditReplaceDocument(HANDLE pdoc);

char*	EditGetClipboardText(HWND hwnd); // LocalFree()
bool	EditCopyAppend(HWND hwnd);

static inline int GetScintillaEOLMode(int mode) {
	const UINT mask = (SC_EOL_CRLF << 2*0) | (SC_EOL_LF << 2*1) | (SC_EOL_CR << 2*2);
	return (mask >> (mode << 1)) & 3;
}
static inline int GetSettingsEOLMode(int mode) {
	const UINT mask = (0 << 2*SC_EOL_CRLF) | (1 << 2*SC_EOL_LF) | (2 << 2*SC_EOL_CR);
	return (mask >> (mode << 1)) & 3;
}

struct EditFileIOStatus;
void 	EditDetectEOLMode(LPCSTR lpData, DWORD cbData, struct EditFileIOStatus *status);
bool	EditLoadFile(LPWSTR pszFile, struct EditFileIOStatus *status);
bool	EditSaveFile(HWND hwnd, LPCWSTR pszFile, int saveFlag, struct EditFileIOStatus *status);

void	EditInvertCase(void);
void	EditMapTextCase(int menu);
void	EditSentenceCase(void);

void	EditURLEncode(void);
void	EditURLDecode(void);
void	EditEscapeCChars(HWND hwnd);
void	EditUnescapeCChars(HWND hwnd);
void	EditEscapeXHTMLChars(HWND hwnd);
void	EditUnescapeXHTMLChars(HWND hwnd);
void	EditChar2Hex(void);
void	EditHex2Char(void);
void	EditShowHex(void);

typedef enum Base64EncodingFlag {
	Base64EncodingFlag_Default,
	Base64EncodingFlag_UrlSafe,
	Base64EncodingFlag_HtmlEmbeddedImage,
} Base64EncodingFlag;
void	EditBase64Encode(Base64EncodingFlag encodingFlag);
void	EditBase64Decode(bool decodeAsHex);
void	EditJSONFormat(bool isFormat);
void	EditConvertNumRadix(int radix);
void	EditModifyNumber(bool bIncrease);

void	EditTabsToSpaces(int nTabWidth, bool bOnlyIndentingWS);
void	EditSpacesToTabs(int nTabWidth, bool bOnlyIndentingWS);

void	EditMoveUp(void);
void	EditMoveDown(void);
void	EditModifyLines(LPCWSTR pwszPrefix, LPCWSTR pwszAppend, bool skipEmptyLine);
void	EditAlignText(EditAlignMode nMode);
void	EditEncloseSelection(LPCWSTR pwszOpen, LPCWSTR pwszClose);
void	EditToggleLineComments(LPCWSTR pwszComment, int commentFlag);
void	EditPadWithSpaces(bool bSkipEmpty, bool bNoUndoGroup);
void	EditStripFirstCharacter(void);
void	EditStripLastCharacter(void);
void	EditStripTrailingBlanks(HWND hwnd, bool bIgnoreSelection);
void	EditStripLeadingBlanks(HWND hwnd, bool bIgnoreSelection);
void	EditCompressSpaces(void);
void	EditRemoveBlankLines(bool bMerge);
void	EditWrapToColumn(int nColumn/*, int nTabWidth*/);
void	EditJoinLinesEx(void);
void	EditSortLines(EditSortFlag iSortFlags);

void	EditJumpTo(Sci_Line iNewLine, Sci_Position iNewCol);
void	EditSelectEx(Sci_Position iAnchorPos, Sci_Position iCurrentPos);
void	EditFixPositions(void);
void	EditEnsureSelectionVisible(void);
void	EditEnsureConsistentLineEndings(void);
void	EditGetExcerpt(LPWSTR lpszExcerpt, DWORD cchExcerpt);

void	EditSelectWord(void);
void	EditSelectLines(bool currentBlock, bool lineSelection);
HWND	EditFindReplaceDlg(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bReplace);
void	EditFindNext(LPCEDITFINDREPLACE lpefr, bool fExtendSelection);
void	EditFindPrev(LPCEDITFINDREPLACE lpefr, bool fExtendSelection);
void	EditFindAll(LPCEDITFINDREPLACE lpefr, bool selectAll);
bool	EditReplace(HWND hwnd, LPCEDITFINDREPLACE lpefr);
bool	EditReplaceAll(HWND hwnd, LPCEDITFINDREPLACE lpefr, bool bShowInfo);
bool	EditReplaceAllInSelection(HWND hwnd, LPCEDITFINDREPLACE lpefr, bool bShowInfo);
bool	EditLineNumDlg(HWND hwnd);
void	EditModifyLinesDlg(HWND hwnd);
void	EditEncloseSelectionDlg(HWND hwnd);
void	EditInsertTagDlg(HWND hwnd);
void	EditInsertDateTime(bool bShort);
void	EditUpdateTimestampMatchTemplate(HWND hwnd);
void	EditInsertUnicodeControlCharacter(int menu);
void	EditShowUnicodeControlCharacter(bool bShow);
bool	EditSortDlg(HWND hwnd, EditSortFlag *piSortFlags);
bool	EditAlignDlg(HWND hwnd, EditAlignMode *piAlignMode);
void	EditSelectionAction(int action);
void	TryBrowseFile(HWND hwnd, LPCWSTR pszFile, bool bWarn);

typedef enum OpenSelectionType {
	OpenSelectionType_None,
	OpenSelectionType_Link,
	OpenSelectionType_File,
	OpenSelectionType_Folder,
	OpenSelectionType_ContainingFolder,
} OpenSelectionType;
void EditOpenSelection(OpenSelectionType type);

// in Bridge.cpp
#ifdef __cplusplus
extern "C" {
#endif

bool	EditPrint(HWND hwnd, LPCWSTR pszDocTitle, BOOL bDefault);
void	EditPrintSetup(HWND hwnd);
void	EditCopyAsRTF(HWND hwnd);

#ifdef __cplusplus
}
#endif

enum {
	MarkerNumber_Bookmark = 0,

	// [0, INDICATOR_CONTAINER) are reserved for lexer.
	IndicatorNumber_MarkOccurrence = INDICATOR_CONTAINER + 0,
	IndicatorNumber_MatchBrace = INDICATOR_CONTAINER + 1,
	IndicatorNumber_MatchBraceError = INDICATOR_CONTAINER + 2,
	// [INDICATOR_IME, INDICATOR_IME_MAX] are reserved for IME.

	MarginNumber_LineNumber = 0,
	MarginNumber_Bookmark = 1,
	MarginNumber_CodeFolding = 2,

	MarkerBitmask_Bookmark = 1 << MarkerNumber_Bookmark,
};

typedef struct EditMarkAllStatus {
	bool pending;
	bool ignoreSelectionUpdate;
	bool bookmarkForFindAll;
	int findFlag;
	int incrementSize;			// increment search size
	Sci_Position iSelCount;		// length for pszText
	LPSTR pszText;				// pattern or text to find
	double duration;			// search duration in milliseconds
	Sci_Position matchCount;	// total match count
	Sci_Position lastMatchPos;	// last matching position
	Sci_Position iStartPos;		// previous stop position
	Sci_Line bookmarkLine;		// previous bookmark line
	StopWatch watch;			// used to dynamic compute increment size
} EditMarkAllStatus;

void EditMarkAll_ClearEx(int findFlag, Sci_Position iSelCount, LPSTR pszText);
NP2_inline void EditMarkAll_Clear(void) {
	EditMarkAll_ClearEx(0, 0, NULL);
}
void EditMarkAll_Start(BOOL bChanged, int findFlag, Sci_Position iSelCount, LPSTR pszText);
void EditMarkAll_Continue(EditMarkAllStatus *status, HANDLE timer);
void EditMarkAll(BOOL bChanged, bool matchCase, bool wholeWord, bool bookmark);
void EditToggleBookmarkAt(Sci_Position iPos);
void EditBookmarkSelectAll(void);

// auto completion fill-up characters
#define MAX_AUTO_COMPLETION_FILLUP_LENGTH	32		// Only 32 ASCII punctuation
#define AUTO_COMPLETION_FILLUP_DEFAULT		L";,()[]{}\\/"
// timeout for scanning words in document
#define AUTOC_SCAN_WORDS_MIN_TIMEOUT		50
#define AUTOC_SCAN_WORDS_DEFAULT_TIMEOUT	500
enum {
	AutoCompleteScope_None = 0,
	AutoCompleteScope_Commont = 1,
	AutoCompleteScope_String = 2,
	AutoCompleteScope_PlainText = 4,
	AutoCompleteScope_Other = 8,
	AutoCompleteScope_Default = 0xff,
};
enum {
	AutoCompleteFillUpMask_Enter = 1,
	AutoCompleteFillUpMask_Tab = 2,
	AutoCompleteFillUpMask_Space = 4,
	AutoCompleteFillUpMask_Punctuation = 8,
	// default settings
	AutoCompleteFillUpMask_Default = 15,
};

// auto insert
typedef enum AutoInsertCharacter {
	AutoInsertCharacter_Parenthesis,
	AutoInsertCharacter_Brace,
	AutoInsertCharacter_SquareBracket,
	AutoInsertCharacter_AngleBracket,
	AutoInsertCharacter_DoubleQuote,
	AutoInsertCharacter_SingleQuote,
	AutoInsertCharacter_Backtick,
	AutoInsertCharacter_Comma,
} AutoInsertCharacter;
enum {
	AutoInsertMask_Parenthesis = 1,			// ()
	AutoInsertMask_Brace = 2,				// {}
	AutoInsertMask_SquareBracket = 4,		// []
	AutoInsertMask_AngleBracket = 8,		// <>
	AutoInsertMask_DoubleQuote = 16,		// ""
	AutoInsertMask_SingleQuote = 32,		// ''
	AutoInsertMask_Backtick = 64,			// ``
	AutoInsertMask_SpaceAfterComma = 128,	// ', '
	AutoInsertMask_CommentAtStart = true,	// '// '
	AutoInsertMask_SpaceAfterComment = 256,	// '// '
	// default settings
	AutoInsertMask_Default = 511,
};

// asm line comment
enum {
	AsmLineCommentChar_Semicolon = 0,	// ';'
	AsmLineCommentChar_Sharp = 1,		// '#'
	AsmLineCommentChar_Slash = 2,		// '//'
	AsmLineCommentChar_At = 3,			// '@'
};

#define MIN_AUTO_COMPLETION_VISIBLE_ITEM_COUNT	8
#define MIN_AUTO_COMPLETION_WORD_LENGTH			1
#define MIN_AUTO_COMPLETION_NUMBER_LENGTH		0

typedef struct EditAutoCompletionConfig {
	bool bIndentText;
	bool bCloseTags;
	bool bCompleteWord;
	bool bScanWordsInDocument;
	int fCompleteScope;
	int fScanWordScope;
	UINT dwScanWordsTimeout;
	bool bEnglistIMEModeOnly;
	bool bIgnoreCase;
	bool bLaTeXInputMethod;
	UINT iVisibleItemCount;
	int iMinWordLength;
	int iMinNumberLength;
	int fAutoCompleteFillUpMask;
	int fAutoInsertMask;
	int iAsmLineCommentChar;
	UINT iPreviousItemCount;		// status
	char szAutoCompleteFillUp[MAX_AUTO_COMPLETION_FILLUP_LENGTH + 4];
	WCHAR wszAutoCompleteFillUp[MAX_AUTO_COMPLETION_FILLUP_LENGTH];
} EditAutoCompletionConfig;

enum {
	AutoCompleteCondition_Normal = 0,
	AutoCompleteCondition_OnCharAdded = 1,
	AutoCompleteCondition_OnCharDeleted = 2,
};

// in EditAutoC.c
void	EditCompleteUpdateConfig(void);
bool	IsDocWordChar(uint32_t ch);
bool	IsAutoCompletionWordCharacter(uint32_t ch);
void	EditCompleteWord(int iCondition, bool autoInsert);
bool	EditIsOpenBraceMatched(Sci_Position pos, Sci_Position startPos);
void	EditAutoCloseBraceQuote(int ch, AutoInsertCharacter what);
void	EditAutoCloseXMLTag(void);
void	EditAutoIndent(void);
void	EditToggleCommentLine(bool alternative);
void	EditToggleCommentBlock(bool alternative);
void	EditInsertScriptShebangLine(void);

typedef enum CallTipType {
	CallTipType_None,
	CallTipType_Notification,
	CallTipType_ColorHex,
} CallTipType;

typedef enum ShowCallTip {
	ShowCallTip_None,
	ShowCallTip_ColorRGBA,
	ShowCallTip_ColorARGB,
	ShowCallTip_ColorBGRA,
	ShowCallTip_ColorABGR,
} ShowCallTip;

struct CallTipInfo {
	ShowCallTip showCallTip;
	CallTipType type;
	Sci_Position startPos;
	Sci_Position endPos;
	Sci_Position hexStart;
	COLORREF currentColor;
	//COLORREF backColor;
	//COLORREF foreColor;
};
void	EditShowCallTip(Sci_Position position);
void	EditClickCallTip(HWND hwnd);

#define NCP_DEFAULT					1
#define NCP_UTF8					2
#define NCP_UTF8_SIGN				4
#define NCP_UNICODE					8
#define NCP_UNICODE_REVERSE			16
#define NCP_UNICODE_BOM				32
#define NCP_8BIT					64
#define NCP_INTERNAL				(NCP_DEFAULT | NCP_UTF8 | NCP_UTF8_SIGN | NCP_UNICODE | NCP_UNICODE_REVERSE | NCP_UNICODE_BOM)
#define NCP_RECODE					128
#define NCP_7BIT					256		// encoded in ASCII with escapes: UTF-7, ISO-2022, HZ-GB-2312
#define CPI_NONE					(-1)
#define CPI_FIRST					0
#define CPI_DEFAULT					0
#define CPI_OEM						1
#define CPI_UNICODEBOM				2
#define CPI_UNICODEBEBOM			3
#define CPI_UNICODE					4
#define CPI_UNICODEBE				5
#define CPI_UTF8					6
#define CPI_UTF8SIGN				7
#define CPI_UTF7					8
// global default encoding
#define CPI_GLOBAL_DEFAULT			CPI_UTF8

#define MAX_ENCODING_LABEL_SIZE		32
// MultiByteToWideChar() and WideCharToMultiByte() uses int as length.
#define MAX_NON_UTF8_SIZE	((1U << 31) - 16)
// added 32 bytes padding as encoding detection may read beyond cbData.
#define NP2_ENCODING_DETECTION_PADDING	32

enum {
	EncodingFlag_None = 0,
	EncodingFlag_Binary = 1,
	EncodingFlag_UTF7 = 2,
	EncodingFlag_Reversed = 4,
	EncodingFlag_Invalid = 8,
};

typedef struct NP2ENCODING {
	const UINT uFlags;
	/*const*/UINT uCodePage;
	// string format: [normal name + ','] + [lower case parse name + ',']+
	const char * const pszParseNames;
	const UINT idsName;
	LPWSTR wchLabel;
} NP2ENCODING;

// see UniConversion.h and https://www.unicode.org/faq/utf_bom.html
#define SURROGATE_LEAD_FIRST		0xD800
#define SURROGATE_TRAIL_FIRST		0xDC00
#define SUPPLEMENTAL_PLANE_FIRST	0x10000
#define MAX_UNICODE					0x10ffff
#define SURROGATE_OFFSET			(0x10000 - (0xD800 << 10) - 0xDC00)
#define UTF16_TO_UTF32(lead, trail)	(((lead) << 10) + (trail) + SURROGATE_OFFSET)

// https://docs.microsoft.com/en-us/windows/win32/intl/locale_iuseutf8legacyacp
#ifndef LOCALE_IUSEUTF8LEGACYACP
#define LOCALE_IUSEUTF8LEGACYACP     0x00000666	// NTDDI_VERSION >= NTDDI_WIN10_MN
#endif
// https://docs.microsoft.com/en-us/windows/win32/intl/locale_iuseutf8legacyoemcp
#ifndef LOCALE_IUSEUTF8LEGACYOEMCP
#define LOCALE_IUSEUTF8LEGACYOEMCP   0x00000999	// NTDDI_VERSION >= NTDDI_WIN10_MN
#endif
// https://docs.microsoft.com/en-us/windows/win32/intl/locale-sname
#ifndef LOCALE_SNAME
#define LOCALE_SNAME				0x0000005c	// _WIN32_WINNT >= _WIN32_WINNT_VISTA
#endif

static inline BOOL GetLegacyACP(UINT *acp) {
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
	return GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IUSEUTF8LEGACYACP | LOCALE_RETURN_NUMBER,
		(LPWSTR)(acp), sizeof(UINT) / sizeof(WCHAR));
#else
	return GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IUSEUTF8LEGACYACP | LOCALE_RETURN_NUMBER,
		(LPWSTR)(acp), sizeof(UINT) / sizeof(WCHAR));
#endif
}

static inline BOOL GetLegacyOEMCP(UINT *oemcp) {
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
	return GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IUSEUTF8LEGACYOEMCP | LOCALE_RETURN_NUMBER,
		(LPWSTR)(oemcp), sizeof(UINT) / sizeof(WCHAR));
#else
	return GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IUSEUTF8LEGACYOEMCP | LOCALE_RETURN_NUMBER,
		(LPWSTR)(oemcp), sizeof(UINT) / sizeof(WCHAR));
#endif
}

// 932 Shift-JIS, 936 GBK, 949 UHC, 950 Big5, 1361 Johab
static inline bool IsDBCSCodePage(UINT page) {
	return page == 932 || page == 936 || page == 949 || page == 950 || page == 1361;
}

// dwFlags must be 0 for MultiByteToWideChar() and WideCharToMultiByte()
static inline bool IsZeroFlagsCodePage(UINT page) {
	return page == CP_UTF8
		|| page == CP_UTF7
		|| page == 54936	// GB18030
		|| page == 50220	// ISO-2022-JP
		|| page == 50221	// CS ISO-2022-JP
		|| page == 50222	// ISO-2022-JP SO/SI
		|| page == 50225	// ISO-2022-KR
		|| page == 50227	// ISO-2022-CN Simplified
		|| page == 50229	// ISO-2022-CN Traditional
		//|| (page >= 57002 && page <= 57011) // ISCII
		//|| page == 42		// Symbol
		;
}

// in EditEncoding.c
extern NP2ENCODING mEncoding[];
static inline bool Encoding_IsUnicode(int iEncoding) {
	return iEncoding == CPI_UNICODEBOM
		|| iEncoding == CPI_UNICODEBEBOM
		|| iEncoding == CPI_UNICODE
		|| iEncoding == CPI_UNICODEBE;
}

static inline bool Encoding_IsUTF8(int iEncoding) {
	return iEncoding == CPI_UTF8
		|| iEncoding == CPI_UTF8SIGN;
}

void	Encoding_ReleaseResources(void);
bool	EditSetNewEncoding(int iEncoding, int iNewEncoding, BOOL bNoUI, bool bSetSavePoint);
void	EditOnCodePageChanged(UINT oldCodePage, bool showControlCharacter, LPEDITFINDREPLACE lpefr);
const char* GetFoldDisplayEllipsis(UINT cpEdit, UINT acp);
void	Encoding_InitDefaults(void);
int 	Encoding_MapIniSetting(bool bLoad, UINT iSetting);
void	Encoding_GetLabel(int iEncoding);
int 	Encoding_Match(LPCWSTR pwszTest);
int 	Encoding_MatchA(LPCSTR pchTest);
bool	Encoding_IsValid(int iEncoding);
int		Encoding_GetIndex(UINT codePage);
int		Encoding_GetAnsiIndex(void);
void	Encoding_AddToTreeView(HWND hwnd, int idSel, bool bRecodeOnly);
bool	Encoding_GetFromTreeView(HWND hwnd, int *pidEncoding, bool bQuiet);
#if 0
void	Encoding_AddToListView(HWND hwnd, int idSel, bool bRecodeOnly);
bool	Encoding_GetFromListView(HWND hwnd, int *pidEncoding);
void	Encoding_AddToComboboxEx(HWND hwnd, int idSel, bool bRecodeOnly);
bool	Encoding_GetFromComboboxEx(HWND hwnd, int *pidEncoding);
#endif

UINT	CodePageFromCharSet(UINT uCharSet);
bool	IsUTF8(const char *pTest, DWORD nLength);
bool	IsUTF7(const char *pTest, DWORD nLength);

#define BOM_UTF8		0xBFBBEF
#define BOM_UTF16LE		0xFEFF
#define BOM_UTF16BE		0xFFFE
static inline bool IsUTF8Signature(const char *p) {
	//return p[0] == '\xEF' && p[1] == '\xBB' && p[2] == '\xBF';
	return (*((const UINT *)p) & 0xFFFFFF) == BOM_UTF8;
}

static inline BOOL Encoding_HasBOM(int iEncoding) {
	return (iEncoding == CPI_UNICODEBOM
		|| iEncoding == CPI_UNICODEBEBOM
		|| iEncoding == CPI_UTF8SIGN) ? iEncoding : FALSE;
}

LPSTR RecodeAsUTF8(LPSTR lpData, DWORD *cbData, UINT codePage, DWORD flags);
int EditDetermineEncoding(LPCWSTR pszFile, char *lpData, DWORD cbData, int *encodingFlag);
bool IsStringCaseSensitiveW(LPCWSTR pszTextW);
bool IsStringCaseSensitiveA(LPCSTR pszText);

//void SciInitThemes(HWND hwnd);

#define FV_TABWIDTH				1
#define FV_INDENTWIDTH			2
#define FV_TABSASSPACES			4
#define FV_TABINDENTS			8
#define FV_WORDWRAP				16
#define FV_LONGLINESLIMIT		32
#define FV_ENCODING				64
#define FV_MODE					128
#define FV_MaskHasTabIndentWidth	(FV_TABWIDTH | FV_INDENTWIDTH)
#define FV_MaskHasFileTabSettings	(FV_TABWIDTH | FV_INDENTWIDTH | FV_TABSASSPACES)

typedef struct EditTabSettings {
	int 	globalTabWidth;
	int 	globalIndentWidth;
	bool	globalTabsAsSpaces;
	bool	bTabIndents;
	uint8_t	bBackspaceUnindents;
	bool	bDetectIndentation;

	int		schemeTabWidth;
	int		schemeIndentWidth;
	bool	schemeTabsAsSpaces;
	bool	schemeUseGlobalTabSettings;
} EditTabSettings;

typedef struct FILEVARS {
	int 	mask;
	int 	iTabWidth;
	int 	iIndentWidth;
	bool	bTabsAsSpaces;
	bool	bTabIndents;
	bool	fWordWrap;
	int 	iLongLinesLimit;
	int 	iEncoding;
	char	tchEncoding[32];
	char	tchMode[32];
} FILEVARS, *LPFILEVARS;

typedef const FILEVARS * LPCFILEVARS;
extern EditTabSettings tabSettings;
extern FILEVARS fvCurFile;

void	EditSetWrapStartIndent(int tabWidth, int indentWidth);
void	EditSetWrapIndentMode(int tabWidth, int indentWidth);
void	FileVars_Init(LPCSTR lpData, DWORD cbData, LPFILEVARS lpfv);
void	FileVars_Apply(LPFILEVARS lpfv);
bool	FileVars_ParseInt(LPCSTR pszData, LPCSTR pszName, int *piValue);
bool	FileVars_ParseStr(LPCSTR pszData, LPCSTR pszName, char *pszValue, int cchValue);
static inline int FileVars_GetEncoding(LPCFILEVARS lpfv) {
	return (lpfv->mask & FV_ENCODING) ? lpfv->iEncoding : CPI_NONE;
}

typedef enum {
	FOLD_ACTION_FOLD	= SC_FOLDACTION_CONTRACT,
	FOLD_ACTION_EXPAND	= SC_FOLDACTION_EXPAND,
	FOLD_ACTION_SNIFF	= SC_FOLDACTION_TOGGLE,
} FOLD_ACTION;

void FoldExpandRange(Sci_Line lineStart, Sci_Line lineEnd);
void FoldToggleAll(FOLD_ACTION action);
void FoldToggleLevel(int lev, FOLD_ACTION action);
void FoldToggleCurrentBlock(FOLD_ACTION action);
void FoldToggleCurrentLevel(FOLD_ACTION action);
void FoldToggleDefault(FOLD_ACTION action);
void FoldClickAt(Sci_Position pos, int mode);
void FoldAltArrow(int key, int mode);
void EditGotoBlock(int menu);

enum SelectOption {
	SelectOption_None,
	SelectOption_EnableMultipleSelection = 1,
	SelectOption_CopySelectionAsFindText = 2,
	SelectOption_CopyPasteBufferAsFindText = 4,
	SelectOption_Default = 7,
};

enum LineSelectionMode {
	LineSelectionMode_None,
	LineSelectionMode_VisualStudio,
	LineSelectionMode_Normal,
	LineSelectionMode_OldVisualStudio,
};
