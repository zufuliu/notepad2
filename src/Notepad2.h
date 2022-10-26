/******************************************************************************
*
*
* Notepad2
*
* Notepad2.h
*   Global definitions and declarations
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

//==== Main Window ============================================================
#define WC_NOTEPAD2 L"Notepad2"
#define MY_APPUSERMODELID	L"Notepad2 Text Editor"

typedef enum TripleBoolean {
	TripleBoolean_False = 0,
	TripleBoolean_True,
	TripleBoolean_NotSet,
} TripleBoolean;

enum {
	ReadOnlyMode_None = 0,
	ReadOnlyMode_Current,
	ReadOnlyMode_AllFile,
};

typedef enum FileWatchingMode {
	FileWatchingMode_None = 0,
	FileWatchingMode_ShowMessage,
	FileWatchingMode_AutoReload,
} FileWatchingMode;

//==== Data Type for WM_COPYDATA ==============================================
#define DATA_NOTEPAD2_PARAMS 0xFB10
typedef struct NP2PARAMS {
	bool	flagFileSpecified;
	bool	flagReadOnlyMode;
	bool	flagLexerSpecified;
	bool	flagQuietCreate;
	bool	flagTitleExcerpt;
	bool	flagJumpTo;
	TripleBoolean	flagChangeNotify;
	int		iInitialLexer;
	Sci_Line		iInitialLine;
	Sci_Position	iInitialColumn;
	int		iSrcEncoding;
	int		flagSetEncoding;
	int		flagSetEOLMode;
	WCHAR wchData;
} NP2PARAMS, *LPNP2PARAMS;

//==== Toolbar Style ==========================================================
#define WS_TOOLBAR (WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |				\
					TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | TBSTYLE_ALTDRAG |		\
					TBSTYLE_LIST | CCS_NODIVIDER | CCS_NOPARENTALIGN |		\
					CCS_ADJUSTABLE)

//==== ReBar Style ============================================================
#define WS_REBAR (WS_CHILD | WS_CLIPCHILDREN | WS_BORDER | RBS_VARHEIGHT |	\
				  RBS_BANDBORDERS | CCS_NODIVIDER | CCS_NOPARENTALIGN)

//==== Ids ====================================================================
#define IDC_STATUSBAR		0xFB00
#define IDC_TOOLBAR			0xFB01
#define IDC_REBAR			0xFB02
#define IDC_EDIT			0xFB03
#define IDC_EDITFRAME		0xFB04
#define IDC_FILENAME		0xFB05
#define IDC_REUSELOCK		0xFB06

// submenu in popup menu, IDR_POPUPMENU
#define IDP_POPUP_SUBMENU_EDIT	0
#define IDP_POPUP_SUBMENU_BAR	1
#define IDP_POPUP_SUBMENU_TRAY	2
#define IDP_POPUP_SUBMENU_FOLD	3

//==== Statusbar ==============================================================
enum {
	StatusItem_Line,
	StatusItem_Column,
	StatusItem_Character,
	StatusItem_Selection,
	StatusItem_SelectedLine,
	StatusItem_Find,
	StatusItem_Empty,
	StatusItem_Lexer,
	StatusItem_Encoding,
	StatusItem_EolMode,
	StatusItem_OvrMode,
	StatusItem_Zoom,
	StatusItem_DocSize,
	StatusItem_ItemCount,
};
#define STATUS_HELP			(255 | SBT_NOBORDERS)

/**
 * App message used to center MessageBox to the window of the program.
 */
#define APPM_CENTER_MESSAGE_BOX		(WM_APP + 1)
#define APPM_CHANGENOTIFY			(WM_APP + 2)	// file change notifications
//#define APPM_CHANGENOTIFYCLEAR	(WM_APP + 3)
#define APPM_TRAYMESSAGE			(WM_APP + 4)	// callback message from system tray
#define APPM_POST_HOTSPOTCLICK		(WM_APP + 5)
// TODO: WM_COPYDATA is blocked by the User Interface Privilege Isolation
// https://www.codeproject.com/tips/1017834/how-to-send-data-from-one-process-to-another-in-cs
#define APPM_COPYDATA				(WM_APP + 6)

#define ID_WATCHTIMER				0xA000	// file watch timer
#define ID_PASTEBOARDTIMER			0xA001	// paste board timer
#define ID_AUTOSAVETIMER			0xA002	// AutoSave timer

#define REUSEWINDOWLOCKTIMEOUT		1000	// Reuse Window Lock Timeout

// Settings Version
enum {
// No version
NP2SettingsVersion_None = 0,
/*
1. `ZoomLevel` and `PrintZoom` changed from relative font size in point to absolute percentage.
2. `HighlightCurrentLine` changed to outline frame of subline, regardless of any previous settings.
*/
NP2SettingsVersion_V1 = 1,
NP2SettingsVersion_Current = NP2SettingsVersion_V1,
};

typedef enum EscFunction {
	EscFunction_None = 0,
	EscFunction_Minimize,
	EscFunction_Exit,
} EscFunction;

typedef enum TitlePathNameFormat {
	TitlePathNameFormat_NameOnly = 0,
	TitlePathNameFormat_NameFirst,
	TitlePathNameFormat_FullPath,
} TitlePathNameFormat;

typedef enum PrintHeaderOption {
	PrintHeaderOption_FilenameAndDateTime = 0,
	PrintHeaderOption_FilenameAndDate,
	PrintHeaderOption_Filename,
	PrintHeaderOption_LeaveBlank,
} PrintHeaderOption;

typedef enum PrintFooterOption {
	PrintFooterOption_PageNumber = 0,
	PrintFooterOption_LeaveBlank,
} PrintFooterOption;

#define INI_SECTION_NAME_NOTEPAD2				L"Notepad2"
#define INI_SECTION_NAME_SETTINGS				L"Settings"
#define INI_SECTION_NAME_FLAGS					L"Settings2"
#define INI_SECTION_NAME_WINDOW_POSITION		L"Window Position"
#define INI_SECTION_NAME_TOOLBAR_LABELS			L"Toolbar Labels"
#define INI_SECTION_NAME_TOOLBAR_IMAGES			L"Toolbar Images"
#define INI_SECTION_NAME_SUPPRESSED_MESSAGES	L"Suppressed Messages"

#define MRU_KEY_RECENT_FILES					L"Recent Files"
#define MRU_KEY_RECENT_FIND						L"Recent Find"
#define MRU_KEY_RECENT_REPLACE					L"Recent Replace"
#define MRU_MAX_RECENT_FILES					32
#define MRU_MAX_RECENT_FIND						32
#define MRU_MAX_RECENT_REPLACE					32

#define MAX_INI_SECTION_SIZE_SETTINGS			(8 * 1024)
#define MAX_INI_SECTION_SIZE_FLAGS				(4 * 1024)
#define MAX_INI_SECTION_SIZE_TOOLBAR_LABELS		(2 * 1024)

extern WCHAR szCurFile[MAX_PATH + 40];

//==== Function Declarations ==================================================
BOOL InitApplication(HINSTANCE hInstance);
void InitInstance(HINSTANCE hInstance, int nCmdShow);
bool ActivatePrevInst(void);
void GetRelaunchParameters(LPWSTR szParameters, LPCWSTR lpszFile, bool newWind, bool emptyWind);
bool RelaunchMultiInst(void);
bool RelaunchElevated(void);
void SnapToDefaultPos(HWND hwnd);
void ShowNotifyIcon(HWND hwnd, bool bAdd);
void SetNotifyIconTitle(HWND hwnd);

void ShowNotificationA(int notifyPos, LPCSTR lpszText);
void ShowNotificationW(int notifyPos, LPCWSTR lpszText);
void ShowNotificationMessage(int notifyPos, UINT uidMessage, ...);

void InstallFileWatching(bool terminate);
void CALLBACK WatchTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void CALLBACK PasteBoardTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

void LoadSettings(void);
void SaveSettingsNow(bool bOnlySaveStyle, bool bQuiet);
void SaveSettings(bool bSaveSettingsNow);
void SaveWindowPosition(WCHAR *pIniSectionBuf);
void ClearWindowPositionHistory(void);
void ParseCommandLine(void);
void LoadFlags(void);

bool CheckIniFile(LPWSTR lpszFile, LPCWSTR lpszModule);
bool CheckIniFileRedirect(LPWSTR lpszFile, LPCWSTR lpszModule, LPCWSTR redirectKey);
bool FindIniFile(void);
bool TestIniFile(void);
bool CreateIniFile(LPCWSTR lpszIniFile);
void FindExtraIniFile(LPWSTR lpszIniFile, LPCWSTR defaultName, LPCWSTR redirectKey);

void UpdateWindowTitle(void);
void UpdateStatusbar(void);
void UpdateStatusBarCache(int item);
void UpdateToolbar(void);
void UpdateFoldMarginWidth(void);
void UpdateLineNumberWidth(void);
void UpdateBookmarkMarginWidth(void);

enum {
	FullScreenMode_OnStartup = 1,
	FullScreenMode_HideCaption = 2,
	FullScreenMode_HideMenu = 4,

	FullScreenMode_Default = FullScreenMode_HideCaption,
};

void ToggleFullScreenMode(void);

typedef struct EditFileIOStatus {
	int iEncoding;		// load output, save input
	int iEOLMode;		// load output

	bool bFileTooBig;	// load output
	bool bUnicodeErr;	// load output
	bool bCancelDataLoss;// save output

	// inconsistent line endings
	bool bLineEndingsDefaultNo; // set default button to "No"
	bool bInconsistent;	// load output
	Sci_Line totalLineCount; // load output, sum(linesCount) + 1
	Sci_Line linesCount[3];	// load output: CR+LF, LF, CR
} EditFileIOStatus;

typedef enum FileLoadFlag {
	FileLoadFlag_Default = 0,
	FileLoadFlag_DontSave = 1,
	FileLoadFlag_New = 2,
	FileLoadFlag_Reload = 4,
} FileLoadFlag;

typedef enum FileSaveFlag {
	FileSaveFlag_Default = 0,
	FileSaveFlag_SaveAlways = 1,
	FileSaveFlag_Ask = 2,
	FileSaveFlag_SaveAs = 4,
	FileSaveFlag_SaveCopy = 8,
	FileSaveFlag_EndSession = 16,
} FileSaveFlag;

bool FileIO(bool fLoad, LPWSTR pszFile, int flag, EditFileIOStatus *status);
bool FileLoad(FileLoadFlag loadFlag, LPCWSTR lpszFile);
bool FileSave(FileSaveFlag saveFlag);
BOOL OpenFileDlg(HWND hwnd, LPWSTR lpstrFile, int cchFile, LPCWSTR lpstrInitialDir);
BOOL SaveFileDlg(HWND hwnd, bool Untitled, LPWSTR lpstrFile, int cchFile, LPCWSTR lpstrInitialDir);

enum {
	AutoSaveOption_None = 0,
	AutoSaveOption_Periodic = 1,
	AutoSaveOption_Suspend = 2,
	AutoSaveOption_Shutdown = 4,
	AutoSaveOption_Default = AutoSaveOption_Suspend | AutoSaveOption_Shutdown,
	AutoSaveDefaultPeriod = 5000,
};

void	AutoSave_Start(bool reset);
void	AutoSave_Stop(BOOL keepBackup);
void	AutoSave_DoWork(bool keepBackup);
LPCWSTR AutoSave_GetDefaultFolder(void);

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam);
LRESULT MsgCreate(HWND hwnd, WPARAM wParam, LPARAM lParam);
void	CreateBars(HWND hwnd, HINSTANCE hInstance);
void	MsgDPIChanged(HWND hwnd, WPARAM wParam, LPARAM lParam);
void	MsgThemeChanged(HWND hwnd, WPARAM wParam, LPARAM lParam);
void	MsgSize(HWND hwnd, WPARAM wParam, LPARAM lParam);
void	MsgInitMenu(HWND hwnd, WPARAM wParam, LPARAM lParam);
LRESULT MsgCommand(HWND hwnd, WPARAM wParam, LPARAM lParam);
LRESULT MsgNotify(HWND hwnd, WPARAM wParam, LPARAM lParam);
