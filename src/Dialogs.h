/******************************************************************************
*
*
* Notepad2
*
* Dialogs.h
*   Definitions for Notepad2 dialog boxes
*
* See Readme.txt for more information about this source code.
* Please send me your comments to this work.
*
* See License.txt for details about distribution and modification.
*
*                                              (c) Florian Balmer 1996-2011
*                                                  florian.balmer@gmail.com
*                                               http://www.flos-freeware.ch
*
*
******************************************************************************/
#pragma once

extern BOOL bWindowLayoutRTL;
NP2_inline void InitWindowCommon(HWND hwnd) {
	if (bWindowLayoutRTL) {
		SetWindowLayoutRTL(hwnd, TRUE);
	}
}

int 	MsgBox(UINT uType, UINT uIdMsg, ...);
#define MsgBoxInfo(uType, uIdMsg, ...)		MsgBox(MB_ICONINFORMATION | (uType), (uIdMsg), ##__VA_ARGS__)
#define MsgBoxWarn(uType, uIdMsg, ...)		MsgBox(MB_ICONEXCLAMATION | (uType), (uIdMsg), ##__VA_ARGS__)
#define MsgBoxAsk(uType, uIdMsg, ...)		MsgBox(MB_ICONQUESTION | (uType), (uIdMsg), ##__VA_ARGS__)
#define MsgBoxLastError(uType, uIdMsg, ...)	MsgBox(MB_ICONEXCLAMATION | MB_SERVICE_NOTIFICATION | (uType), (uIdMsg), ##__VA_ARGS__)

void	DisplayCmdLineHelp(HWND hwnd);
void	OpenHelpLink(HWND hwnd, int cmd);
BOOL	GetDirectory(HWND hwndParent, int iTitle, LPWSTR pszFolder, LPCWSTR pszBase);
INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam);
void	RunDlg(HWND hwnd, LPCWSTR lpstrDefault);
BOOL	OpenWithDlg(HWND hwnd, LPCWSTR lpstrFile);
BOOL	FavoritesDlg(HWND hwnd, LPWSTR lpstrFile);
BOOL	AddToFavDlg(HWND hwnd, LPCWSTR lpszName, LPCWSTR lpszTarget);
BOOL	FileMRUDlg(HWND hwnd, LPWSTR lpstrFile);
BOOL	ChangeNotifyDlg(HWND hwnd);
BOOL	ColumnWrapDlg(HWND hwnd, int *iNumber);
BOOL	WordWrapSettingsDlg(HWND hwnd, int *iNumber);
BOOL	LongLineSettingsDlg(HWND hwnd, int *iNumber);
BOOL	TabSettingsDlg(HWND hwnd);
BOOL	SelectDefEncodingDlg(HWND hwnd, int *pidREncoding);
BOOL	SelectEncodingDlg(HWND hwnd, int *pidREncoding, UINT uidLabel);
BOOL	SelectDefLineEndingDlg(HWND hwnd, int *iOption);

struct EditFileIOStatus;
BOOL	WarnLineEndingDlg(HWND hwnd, struct EditFileIOStatus *status);
void	InitZoomLevelComboBox(HWND hwnd, int nCtlId, int zoomLevel);
BOOL	GetZoomLevelComboBoxValue(HWND hwnd, int nCtrId, int *zoomLevel);
void	ZoomLevelDlg(HWND hwnd, BOOL bBottom);
BOOL	AutoCompletionSettingsDlg(HWND hwnd);

INT_PTR InfoBox(UINT uType, LPCWSTR lpstrSetting, UINT uidMessage, ...);
#define InfoBoxInfo(uType, lpstrSetting, uidMessage, ...)	InfoBox(MB_ICONINFORMATION | (uType), (lpstrSetting), (uidMessage), ##__VA_ARGS__)
#define InfoBoxWarn(uType, lpstrSetting, uidMessage, ...)	InfoBox(MB_ICONEXCLAMATION | (uType), (lpstrSetting), (uidMessage), ##__VA_ARGS__)
#define InfoBoxAsk(uType, lpstrSetting, uidMessage, ...)	InfoBox(MB_ICONQUESTION | (uType), (lpstrSetting), (uidMessage), ##__VA_ARGS__)

void	SystemIntegrationDlg(HWND hwnd);
