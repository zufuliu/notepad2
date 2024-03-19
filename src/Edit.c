/******************************************************************************
*
*
* Notepad2
*
* Edit.c
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

#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#if 0
#include <uxtheme.h>
#include <vssym32.h>
#endif
#include <time.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include "SciCall.h"
#include "VectorISA.h"
#include "Helpers.h"
#include "Notepad2.h"
#include "Edit.h"
#include "Styles.h"
#include "Dialogs.h"
#include "resource.h"

extern HWND hwndMain;
extern HWND hwndEdit;
extern DWORD dwLastIOError;
extern HWND hDlgFindReplace;
extern bool bReplaceInitialized;

extern int xFindReplaceDlg;
extern int yFindReplaceDlg;
extern int cxFindReplaceDlg;

extern int iDefaultEOLMode;
extern bool bFixLineEndings;
extern bool bAutoStripBlanks;

// Default Codepage and Character Set
extern int iDefaultCodePage;
extern bool bLoadANSIasUTF8;
extern int iSrcEncoding;
extern int iWeakSrcEncoding;
extern int iCurrentEncoding;

extern MRULIST mruFind;
extern MRULIST mruReplace;

static DStringW wchPrefixSelection;
static DStringW wchAppendSelection;
static DStringW wchPrefixLines;
static DStringW wchAppendLines;

// see TransliterateText()
#if defined(_MSC_VER) && (_WIN32_WINNT >= _WIN32_WINNT_WIN7)
#define NP2_DYNAMIC_LOAD_ELSCORE_DLL	1
#else
#define NP2_DYNAMIC_LOAD_ELSCORE_DLL	1
#endif
#if NP2_DYNAMIC_LOAD_ELSCORE_DLL
static HMODULE hELSCoreDLL = NULL;
#else
#pragma comment(lib, "elscore.lib")
#endif

#define NP2_DYNAMIC_LOAD_wcsftime	1
#if NP2_DYNAMIC_LOAD_wcsftime
static HMODULE hCrtDLL = NULL;

typedef size_t (__cdecl *wcsftimeSig)(wchar_t *str, size_t count, const wchar_t *format, const struct tm *time);
wcsftimeSig GetFunctionPointer_wcsftime(void) {
	if (hCrtDLL == NULL) {
		hCrtDLL = LoadLibraryExW(L"ucrtbase.dll", NULL, kSystemLibraryLoadFlags);
		if (hCrtDLL == NULL) {
			hCrtDLL = LoadLibraryExW(L"msvcrt.dll", NULL, kSystemLibraryLoadFlags);
		}
	}
	return DLLFunction(wcsftimeSig, hCrtDLL, "wcsftime");
}
#endif

void Edit_ReleaseResources(void) {
	DStringW_Free(&wchPrefixSelection);
	DStringW_Free(&wchAppendSelection);
	DStringW_Free(&wchPrefixLines);
	DStringW_Free(&wchAppendLines);
#if NP2_DYNAMIC_LOAD_ELSCORE_DLL
	if (hELSCoreDLL != NULL) {
		FreeLibrary(hELSCoreDLL);
	}
#endif
#if NP2_DYNAMIC_LOAD_wcsftime
	if (hCrtDLL) {
		FreeLibrary(hCrtDLL);
	}
#endif
}

static inline void NotifyRectangleSelection(void) {
	MsgBoxWarn(MB_OK, IDS_SELRECT);
	//ShowNotificationMessage(SC_NOTIFICATIONPOSITION_CENTER, IDS_SELRECT);
}

//=============================================================================
//
// EditSetNewText()
//
extern bool bFreezeAppTitle;
extern bool bReadOnlyMode;
#if defined(_WIN64)
extern bool bLargeFileMode;
#endif
extern int iWrapColumn;
extern int iWordWrapIndent;

void EditSetNewText(LPCSTR lpstrText, DWORD cbText, Sci_Line lineCount) {
	bFreezeAppTitle = true;
	bReadOnlyMode = false;
	iWrapColumn = 0;

	SciCall_SetReadOnly(false);
	SciCall_Cancel();
	SciCall_SetUndoCollection(false);
	SciCall_EmptyUndoBuffer();
	SciCall_ClearAll();
	SciCall_ClearMarker();
	SciCall_SetXOffset(0);

#if defined(_WIN64)
	// enable conversion between line endings
	if (bLargeFileMode || cbText + lineCount >= MAX_NON_UTF8_SIZE) {
		const int mask = SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE;
		const int options = SciCall_GetDocumentOptions();
		if ((options & mask) != mask) {
			HANDLE pdoc = SciCall_CreateDocument(cbText + 1, options | mask);
			EditReplaceDocument(pdoc);
			bLargeFileMode = true;
		}
	}
#endif

	FileVars_Apply(&fvCurFile);

	if (cbText > 0) {
		SendMessage(hwndEdit, WM_SETREDRAW, FALSE, 0);
		SciCall_SetModEventMask(SC_MOD_NONE);
#if 0
		StopWatch watch;
		StopWatch_Start(watch);
#endif
		SciCall_AllocateLines(lineCount);
		SciCall_AppendText(cbText, lpstrText);
#if 0
		StopWatch_Stop(watch);
		StopWatch_ShowLog(&watch, "AddText time");
#endif
		SciCall_SetModEventMask(SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
		SendMessage(hwndEdit, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(hwndEdit, NULL, TRUE);
	}

	SciCall_SetUndoCollection(true);
	SciCall_EmptyUndoBuffer();
	SciCall_SetSavePoint();

	bFreezeAppTitle = false;
}

//=============================================================================
//
// EditConvertText()
//
bool EditConvertText(UINT cpSource, UINT cpDest, bool bSetSavePoint) {
	if (cpSource == cpDest) {
		return true;
	}

	const Sci_Position length = SciCall_GetLength();
	if ((size_t)length >= MAX_NON_UTF8_SIZE) {
		return true;
	}

	char *pchText = NULL;
	int cbText = 0;
	if (length > 0) {
		// DBCS: length -> WCHAR: sizeof(WCHAR) * (length / 2) -> UTF-8: kMaxMultiByteCount * (length / 2)
		pchText = (char *)NP2HeapAlloc((length + 1) * sizeof(WCHAR));
		SciCall_GetText(length, pchText);

		WCHAR *pwchText = (WCHAR *)NP2HeapAlloc((length + 1) * sizeof(WCHAR));
		const int cbwText = MultiByteToWideChar(cpSource, 0, pchText, (int)length, pwchText, (int)(NP2HeapSize(pwchText) / sizeof(WCHAR)));
		cbText = WideCharToMultiByte(cpDest, 0, pwchText, cbwText, pchText, (int)(NP2HeapSize(pchText)), NULL, NULL);
		NP2HeapFree(pwchText);
	}

	bReadOnlyMode = false;
	SciCall_SetReadOnly(false);
	SciCall_Cancel();
	SciCall_SetUndoCollection(false);
	SciCall_EmptyUndoBuffer();
	SciCall_ClearAll();
	SciCall_ClearMarker();
	SciCall_SetCodePage(cpDest);

	if (cbText > 0) {
		SendMessage(hwndEdit, WM_SETREDRAW, FALSE, 0);
		SciCall_SetModEventMask(SC_MOD_NONE);
		SciCall_AppendText(cbText, pchText);
		SciCall_SetModEventMask(SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
		SendMessage(hwndEdit, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(hwndEdit, NULL, TRUE);
	}
	if (pchText != NULL) {
		NP2HeapFree(pchText);
	}

	SciCall_EmptyUndoBuffer();
	SciCall_SetUndoCollection(true);
	if (length == 0 && bSetSavePoint) {
		SciCall_SetSavePoint();
	}
	UpdateLineNumberWidth();
	return true;
}

#if defined(_WIN64)
void EditConvertToLargeMode(void) {
	int options = SciCall_GetDocumentOptions();
	if (options & SC_DOCUMENTOPTION_TEXT_LARGE) {
		return;
	}

	options |= SC_DOCUMENTOPTION_TEXT_LARGE;
	const Sci_Position length = SciCall_GetLength();
	HANDLE pdoc = SciCall_CreateDocument(length + 1, options);
	char *pchText = NULL;
	if (length != 0) {
		pchText = (char *)NP2HeapAlloc(length + 1);
		SciCall_GetText(length, pchText);
	}

	bReadOnlyMode = false;
	SciCall_SetReadOnly(false);
	SciCall_Cancel();
	SciCall_SetUndoCollection(false);
	SciCall_EmptyUndoBuffer();
	SciCall_ClearAll();
	SciCall_ClearMarker();

	EditReplaceDocument(pdoc);
	FileVars_Apply(&fvCurFile);

	if (length > 0) {
		SendMessage(hwndEdit, WM_SETREDRAW, FALSE, 0);
		SciCall_SetModEventMask(SC_MOD_NONE);
		SciCall_AppendText(length, pchText);
		SciCall_SetModEventMask(SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
		SendMessage(hwndEdit, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(hwndEdit, NULL, TRUE);
	}
	if (pchText != NULL) {
		NP2HeapFree(pchText);
	}

	SciCall_SetUndoCollection(true);
	SciCall_EmptyUndoBuffer();
	SciCall_SetSavePoint();

	Style_SetLexer(pLexCurrent, true);
	bLargeFileMode = true;
}
#endif

//=============================================================================
//
// EditGetClipboardText()
//
char* EditGetClipboardText(HWND hwnd) {
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(GetParent(hwnd))) {
		return NULL;
	}

	HANDLE hmem = GetClipboardData(CF_UNICODETEXT);
	LPCWSTR pwch = (LPCWSTR)GlobalLock(hmem);
	const int wlen = lstrlen(pwch);

	const UINT cpEdit = SciCall_GetCodePage();
	const int mlen = WideCharToMultiByte(cpEdit, 0, pwch, wlen + 1, NULL, 0, NULL, NULL) - 1;
	char *pmch = (char *)LocalAlloc(LPTR, mlen + 1);
	char *ptmp = (char *)NP2HeapAlloc(mlen * 4 + 1);

	if (pmch && ptmp) {
		const char *s = pmch;
		char *d = ptmp;

		WideCharToMultiByte(cpEdit, 0, pwch, wlen + 1, pmch, mlen + 1, NULL, NULL);

		const int iEOLMode = SciCall_GetEOLMode();
		for (int i = 0; (i < mlen) && (*s != '\0'); i++) {
			if (*s == '\n' || *s == '\r') {
				switch (iEOLMode) {
				default: // SC_EOL_CRLF
					*d++ = '\r';
					*d++ = '\n';
					break;
				case SC_EOL_LF:
					*d++ = '\n';
					break;
				case SC_EOL_CR:
					*d++ = '\r';
					break;
				}
				if ((*s == '\r') && (i + 1 < mlen) && (*(s + 1) == '\n')) {
					i++;
					s++;
				}
				s++;
			} else {
				*d++ = *s++;
			}
		}

		*d++ = '\0';
		LocalFree(pmch);
		pmch = (char *)LocalAlloc(LPTR, (d - ptmp));
		strcpy(pmch, ptmp);
		NP2HeapFree(ptmp);
	}

	GlobalUnlock(hmem);
	CloseClipboard();

	return pmch;
}

LPWSTR EditGetClipboardTextW(void) {
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwndMain)) {
		return NULL;
	}

	HANDLE hmem = GetClipboardData(CF_UNICODETEXT);
	LPCWSTR pwch = (LPCWSTR)GlobalLock(hmem);
	const int wlen = lstrlen(pwch);
	LPWSTR ptmp = (LPWSTR)NP2HeapAlloc((2*wlen + 1)*sizeof(WCHAR));

	if (pwch && ptmp) {
		LPCWSTR s = pwch;
		LPWSTR d = ptmp;

		const int iEOLMode = SciCall_GetEOLMode();
		for (int i = 0; (i < wlen) && (*s != L'\0'); i++) {
			if (*s == L'\n' || *s == L'\r') {
				switch (iEOLMode) {
				default: // SC_EOL_CRLF
					*d++ = L'\r';
					*d++ = L'\n';
					break;
				case SC_EOL_LF:
					*d++ = L'\n';
					break;
				case SC_EOL_CR:
					*d++ = L'\r';
					break;
				}
				if ((*s == L'\r') && (i + 1 < wlen) && (*(s + 1) == L'\n')) {
					i++;
					s++;
				}
				s++;
			} else {
				*d++ = *s++;
			}
		}

		*d++ = L'\0';
	}

	GlobalUnlock(hmem);
	CloseClipboard();

	return ptmp;
}

//=============================================================================
//
// EditCopyAppend()
//
bool EditCopyAppend(HWND hwnd) {
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
		SciCall_Copy(false);
		return true;
	}

	char *pszText;
	if (!SciCall_IsSelectionEmpty()) {
		if (SciCall_IsRectangleSelection()) {
			NotifyRectangleSelection();
			return false;
		}

		const Sci_Position iSelCount = SciCall_GetSelTextLength();
		pszText = (char *)NP2HeapAlloc(iSelCount + 1);
		SciCall_GetSelText(pszText);
	} else {
		const Sci_Position cchText = SciCall_GetLength();
		pszText = (char *)NP2HeapAlloc(cchText + 1);
		SciCall_GetText(cchText, pszText);
	}

	const UINT cpEdit = SciCall_GetCodePage();
	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, -1, NULL, 0);

	WCHAR *pszTextW = NULL;
	if (cchTextW > 0) {
		const WCHAR *pszSep = L"\r\n\r\n";
		pszTextW = (WCHAR *)NP2HeapAlloc(sizeof(WCHAR) * (CSTRLEN(L"\r\n\r\n") + cchTextW + 1));
		lstrcpy(pszTextW, pszSep);
		MultiByteToWideChar(cpEdit, 0, pszText, -1, StrEnd(pszTextW), (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	}

	NP2HeapFree(pszText);

	bool succ = false;
	if (OpenClipboard(GetParent(hwnd))) {
		HANDLE hOld = GetClipboardData(CF_UNICODETEXT);
		LPCWSTR pszOld = (LPCWSTR)GlobalLock(hOld);

		HANDLE hNew = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
						   sizeof(WCHAR) * (lstrlen(pszOld) + lstrlen(pszTextW) + 1));
		WCHAR *pszNew = (WCHAR *)GlobalLock(hNew);

		lstrcpy(pszNew, pszOld);
		lstrcat(pszNew, pszTextW);

		GlobalUnlock(hOld);
		GlobalUnlock(hNew);

		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hNew);
		CloseClipboard();

		succ = true;
	}

	if (pszTextW != NULL) {
		NP2HeapFree(pszTextW);
	}
	return succ;
}

//=============================================================================
//
// EditDetectEOLMode()
//
void EditDetectEOLMode(LPCSTR lpData, DWORD cbData, EditFileIOStatus *status) {
	/* '\r' and '\n' is not reused (e.g. as trailing byte in DBCS) by any known encoding,
	it's safe to check whole data byte by byte.*/

	size_t lineCountCRLF = 0;
	size_t lineCountCR = 0;
	size_t lineCountLF = 0;
#if 0
	StopWatch watch;
	StopWatch_Start(watch);
#endif

	const uint8_t *ptr = (const uint8_t *)lpData;
	// No NULL-terminated requirement for *ptr == '\n'
#if NP2_USE_SSE2 || NP2_USE_AVX2
	const uint8_t * const end = ptr + cbData;
#else
	const uint8_t * const end = ptr + cbData - 1;
#endif

#if NP2_USE_AVX2
	const __m256i vectCR = _mm256_set1_epi8('\r');
	const __m256i vectLF = _mm256_set1_epi8('\n');
	while (ptr + 2*sizeof(__m256i) < end) {
		// unaligned loading: line starts at random position.
		const __m256i chunk1 = _mm256_loadu_si256((__m256i *)ptr);
		const __m256i chunk2 = _mm256_loadu_si256((__m256i *)(ptr + sizeof(__m256i)));
		ptr += 2*sizeof(__m256i);
		uint64_t maskCR = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectLF));
		maskLF |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m256i);
		maskCR |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m256i);

		if (maskCR) {
			if (_addcarry_u64(0, maskCR, maskCR, &maskCR)) {
				if (*ptr == '\n') {
					// CR+LF across boundary
					++ptr;
					++lineCountCRLF;
				} else {
					++lineCountCR;
				}
			}

			// maskCR and maskLF never have some bit set, after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
			const uint64_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
			maskLF = maskCR_LF & maskLF; // LF alone
			maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
			if (maskCRLF) {
				lineCountCRLF += np2_popcount64(maskCRLF);
			}
			if (maskCR) {
				lineCountCR += np2_popcount64(maskCR);
			}
		}
		if (maskLF) {
			lineCountLF += np2_popcount64(maskLF);
		}
	}

	if (ptr < end) {
		NP2_alignas(32) uint8_t buffer[2*sizeof(__m256i)];
		ZeroMemory_32x2(buffer);
		__movsb(buffer, ptr, end - ptr);

		const __m256i chunk1 = _mm256_load_si256((__m256i *)buffer);
		const __m256i chunk2 = _mm256_load_si256((__m256i *)(buffer + sizeof(__m256i)));
		uint64_t maskCR = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectLF));
		maskLF |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m256i);
		maskCR |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m256i);

		if (maskCR) {
			const uint8_t lastCR = _addcarry_u64(0, maskCR, maskCR, &maskCR);
			_addcarry_u64(lastCR, lineCountCR, 0, &lineCountCR);
			const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
			const uint64_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
			maskLF = maskCR_LF & maskLF; // LF alone
			maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
			if (maskCRLF) {
				lineCountCRLF += np2_popcount64(maskCRLF);
			}
			if (maskCR) {
				lineCountCR += np2_popcount64(maskCR);
			}
		}
		if (maskLF) {
			lineCountLF += np2_popcount64(maskLF);
		}
	}
	// end NP2_USE_AVX2
#elif NP2_USE_SSE2
#if defined(_WIN64)
	const __m128i vectCR = _mm_set1_epi8('\r');
	const __m128i vectLF = _mm_set1_epi8('\n');
	while (ptr + 4*sizeof(__m128i) < end) {
		// unaligned loading: line starts at random position.
		const __m128i chunk1 = _mm_loadu_si128((__m128i *)ptr);
		const __m128i chunk2 = _mm_loadu_si128((__m128i *)(ptr + sizeof(__m128i)));
		const __m128i chunk3 = _mm_loadu_si128((__m128i *)(ptr + 2*sizeof(__m128i)));
		const __m128i chunk4 = _mm_loadu_si128((__m128i *)(ptr + 3*sizeof(__m128i)));
		ptr += 4*sizeof(__m128i);
		uint64_t maskCR = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);
		maskLF |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);
		maskCR |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectCR))) << 2*sizeof(__m128i);
		maskLF |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectLF))) << 2*sizeof(__m128i);
		maskLF |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectLF))) << 3*sizeof(__m128i);
		maskCR |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectCR))) << 3*sizeof(__m128i);

		if (maskCR) {
			if (_addcarry_u64(0, maskCR, maskCR, &maskCR)) {
				if (*ptr == '\n') {
					// CR+LF across boundary
					++ptr;
					++lineCountCRLF;
				} else {
					++lineCountCR;
				}
			}

			// maskCR and maskLF never have some bit set, after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
			const uint64_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
			maskLF = maskCR_LF & maskLF; // LF alone
			maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
			if (maskCRLF) {
				lineCountCRLF += np2_popcount64(maskCRLF);
			}
			if (maskCR) {
				lineCountCR += np2_popcount64(maskCR);
			}
		}
		if (maskLF) {
			lineCountLF += np2_popcount64(maskLF);
		}
	}

	if (ptr < end) {
		NP2_alignas(16) uint8_t buffer[4*sizeof(__m128i)];
		ZeroMemory_16x4(buffer);
		__movsb(buffer, ptr, end - ptr);

		const __m128i chunk1 = _mm_load_si128((__m128i *)buffer);
		const __m128i chunk2 = _mm_load_si128((__m128i *)(buffer + sizeof(__m128i)));
		const __m128i chunk3 = _mm_load_si128((__m128i *)(buffer + 2*sizeof(__m128i)));
		const __m128i chunk4 = _mm_load_si128((__m128i *)(buffer + 3*sizeof(__m128i)));
		uint64_t maskCR = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);
		maskLF |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);
		maskCR |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectCR))) << 2*sizeof(__m128i);
		maskLF |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectLF))) << 2*sizeof(__m128i);
		maskLF |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectLF))) << 3*sizeof(__m128i);
		maskCR |= ((uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectCR))) << 3*sizeof(__m128i);

		if (maskCR) {
			const uint8_t lastCR = _addcarry_u64(0, maskCR, maskCR, &maskCR);
			_addcarry_u64(lastCR, lineCountCR, 0, &lineCountCR);
			const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
			const uint64_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
			maskLF = maskCR_LF & maskLF; // LF alone
			maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
			if (maskCRLF) {
				lineCountCRLF += np2_popcount64(maskCRLF);
			}
			if (maskCR) {
				lineCountCR += np2_popcount64(maskCR);
			}
		}
		if (maskLF) {
			lineCountLF += np2_popcount64(maskLF);
		}
	}
	// end _WIN64 NP2_USE_SSE2
#else
	const __m128i vectCR = _mm_set1_epi8('\r');
	const __m128i vectLF = _mm_set1_epi8('\n');
	while (ptr + 2*sizeof(__m128i) < end) {
		// unaligned loading: line starts at random position.
		const __m128i chunk1 = _mm_loadu_si128((__m128i *)ptr);
		const __m128i chunk2 = _mm_loadu_si128((__m128i *)(ptr + sizeof(__m128i)));
		ptr += 2*sizeof(__m128i);
		uint32_t maskCR = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint32_t maskLF = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskLF |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);
		maskCR |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);

		if (maskCR) {
			if (_addcarry_u32(0, maskCR, maskCR, &maskCR)) {
				if (*ptr == '\n') {
					// CR+LF across boundary
					++ptr;
					++lineCountCRLF;
				} else {
					++lineCountCR;
				}
			}

			// maskCR and maskLF never have some bit set, after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint32_t maskCRLF = maskCR & maskLF; // CR+LF
			const uint32_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
			maskLF = maskCR_LF & maskLF; // LF alone
			maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
			if (maskCRLF) {
				lineCountCRLF += np2_popcount(maskCRLF);
			}
			if (maskCR) {
				lineCountCR += np2_popcount(maskCR);
			}
		}
		if (maskLF) {
			lineCountLF += np2_popcount(maskLF);
		}
	}

	if (ptr < end) {
		NP2_alignas(16) uint8_t buffer[2*sizeof(__m128i)];
		ZeroMemory_16x2(buffer);
		__movsb(buffer, ptr, end - ptr);

		const __m128i chunk1 = _mm_load_si128((__m128i *)buffer);
		const __m128i chunk2 = _mm_load_si128((__m128i *)(buffer + sizeof(__m128i)));
		uint32_t maskCR = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint32_t maskLF = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskLF |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);
		maskCR |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);

		if (maskCR) {
			const uint8_t lastCR = _addcarry_u32(0, maskCR, maskCR, &maskCR);
			_addcarry_u32(lastCR, lineCountCR, 0, &lineCountCR);
			const uint32_t maskCRLF = maskCR & maskLF; // CR+LF
			const uint32_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
			maskLF = maskCR_LF & maskLF; // LF alone
			maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
			if (maskCRLF) {
				lineCountCRLF += np2_popcount(maskCRLF);
			}
			if (maskCR) {
				lineCountCR += np2_popcount(maskCR);
			}
		}
		if (maskLF) {
			lineCountLF += np2_popcount(maskLF);
		}
	}
#endif
	// end NP2_USE_SSE2
#else

#if defined(__clang__) || defined(__GNUC__) || defined(__ICL) || !defined(_MSC_VER)
	while (ptr < end) {
		const uint8_t ch = *ptr++;
		const uint32_t mask = ((1 << '\r') - 1) ^ (1 << '\n');
		if (ch > '\r' || ((mask >> ch) & 1) != 0) {
			continue;
		}
		if (ch == '\n') {
			++lineCountLF;
		} else {
			if (*ptr == '\n') {
				++ptr;
				++lineCountCRLF;
			} else {
				++lineCountCR;
			}
		}
	}
#else
	do {
		// skip to line end
		uint8_t ch = 0;
#if 1
		const uint32_t mask = ((1 << '\r') - 1) ^ (1 << '\n');
		while (ptr < end && ((ch = *ptr++) > '\r' || ((mask >> ch) & 1) != 0)) {
			// nop
		}
#else
		while (ptr < end && ((ch = *ptr++) > '\r' || ch < '\n')) {
			// nop
		}
#endif
		switch (ch) {
		case '\n':
			++lineCountLF;
			break;
		case '\r':
			if (*ptr == '\n') {
				++ptr;
				++lineCountCRLF;
			} else {
				++lineCountCR;
			}
			break;
		}
	} while (ptr < end);
#endif
	if (ptr == end) {
		switch (*ptr) {
		case '\n':
			++lineCountLF;
			break;
		case '\r':
			++lineCountCR;
			break;
		}
	}
#endif

	const size_t linesMax = max_z(max_z(lineCountCRLF, lineCountCR), lineCountLF);
	// values must kept in same order as SC_EOL_CRLF, SC_EOL_CR, SC_EOL_LF
	const size_t linesCount[3] = { lineCountCRLF, lineCountCR, lineCountLF };
	int iEOLMode = status->iEOLMode;
	if (linesMax != linesCount[iEOLMode]) {
		if (linesMax == lineCountCRLF) {
			iEOLMode = SC_EOL_CRLF;
		} else if (linesMax == lineCountLF) {
			iEOLMode = SC_EOL_LF;
		} else {
			iEOLMode = SC_EOL_CR;
		}
	}

#if 0
	StopWatch_Stop(watch);
	StopWatch_ShowLog(&watch, "EOL time");
	printf("%s CR+LF:%u, LF: %u, CR: %u\n", __func__, (UINT)lineCountCRLF, (UINT)lineCountLF, (UINT)lineCountCR);
#endif

	status->iEOLMode = iEOLMode;
	status->bInconsistent = ((!!lineCountCRLF) + (!!lineCountCR) + (!!lineCountLF)) > 1;
	status->totalLineCount = lineCountCRLF + lineCountCR + lineCountLF + 1;
	status->linesCount[0] = lineCountCRLF;
	status->linesCount[1] = lineCountLF;
	status->linesCount[2] = lineCountCR;
}

void EditDetectIndentation(LPCSTR lpData, DWORD cbData, LPFILEVARS lpfv) {
	if ((lpfv->mask & FV_MaskHasFileTabSettings) == FV_MaskHasFileTabSettings) {
		return;
	}
	if (!tabSettings.bDetectIndentation) {
		return;
	}

#if 0
	StopWatch watch;
	StopWatch_Start(watch);
#endif

	// code based on SciTEBase::DiscoverIndentSetting().
	cbData = min_u(cbData, 1*1024*1024);
	const uint8_t *ptr = (const uint8_t *)lpData;
	const uint8_t * const end = ptr + cbData;
	#define MAX_DETECTED_TAB_WIDTH	8
	// line count for ambiguous lines, line indented by 1 to 8 spaces, line starts with tab.
	uint32_t indentLineCount[1 + MAX_DETECTED_TAB_WIDTH + 1] = { 0 };
	int prevIndentCount = 0;
	int prevTabWidth = 0;

#if NP2_USE_AVX2
	const __m256i vectCR = _mm256_set1_epi8('\r');
	const __m256i vectLF = _mm256_set1_epi8('\n');
labelStart:
#elif NP2_USE_SSE2
	const __m128i vectCR = _mm_set1_epi8('\r');
	const __m128i vectLF = _mm_set1_epi8('\n');
labelStart:
#endif
	while (ptr < end) {
		switch (*ptr++) {
		case '\t':
			++indentLineCount[MAX_DETECTED_TAB_WIDTH + 1];
			break;

		case ' ': {
			int indentCount = 1;
			while (ptr < end && *ptr == ' ') {
				++ptr;
				++indentCount;
			}
			if ((indentCount & 1) != 0 && *ptr == '*') {
				// fix alignment space before star in Javadoc style comment: ` * comment content`
				--indentCount;
				if (indentCount == 0) {
					break;
				}
			}
			if (indentCount != prevIndentCount) {
				const int delta = abs(indentCount - prevIndentCount);
				prevIndentCount = indentCount;
				// TODO: fix other (e.g. function argument) alignment spaces.
				if (delta <= MAX_DETECTED_TAB_WIDTH) {
					prevTabWidth = min_i(delta, indentCount);
				} else {
					prevTabWidth = 0;
				}
			}
			++indentLineCount[prevTabWidth];
		} break;

		case '\r':
		case '\n':
			continue;

		default:
			prevIndentCount = 0;
			break;
		}

		// skip to line end
#if NP2_USE_AVX2
		while (ptr + sizeof(__m256i) <= end) {
			const __m256i chunk = _mm256_loadu_si256((__m256i *)ptr);
			const uint32_t mask = _mm256_movemask_epi8(_mm256_or_si256(_mm256_cmpeq_epi8(chunk, vectCR), _mm256_cmpeq_epi8(chunk, vectLF)));
			if (mask != 0) {
				const uint32_t trailing = np2_ctz(mask);
				ptr += trailing + 1;
				goto labelStart;
			}
			ptr += sizeof(__m256i);
		}
#elif NP2_USE_SSE2
		while (ptr + sizeof(__m128i) <= end) {
			const __m128i chunk = _mm_loadu_si128((__m128i *)ptr);
			const uint32_t mask = _mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(chunk, vectCR), _mm_cmpeq_epi8(chunk, vectLF)));
			if (mask != 0) {
				const uint32_t trailing = np2_ctz(mask);
				ptr += trailing + 1;
				goto labelStart;
			}
			ptr += sizeof(__m128i);
		}
#endif
		const uint32_t mask = ((1 << '\r') - 1) ^ (1 << '\n');
		uint8_t ch;
		while (ptr < end && ((ch = *ptr++) > '\r' || ((mask >> ch) & 1) != 0)) {
			// nop
		}
	}

#if NP2_USE_AVX2
	const __m128i chunk1 = _mm_loadu_si128((__m128i *)indentLineCount);
	const __m128i chunk2 = _mm_loadu_si128((__m128i *)(indentLineCount + 4));
	const __m128i chunk3 = _mm_loadl_epi64((__m128i *)(indentLineCount + 8));
	__m128i maxAll = _mm_max_epu32(_mm_max_epu32(chunk1, chunk2), chunk3);
	maxAll = _mm_max_epu32(maxAll, _mm_shuffle_epi32(maxAll, _MM_SHUFFLE(0, 1, 2, 3)));
	maxAll = _mm_max_epu32(maxAll, _mm_shuffle_epi32(maxAll, _MM_SHUFFLE(1, 0, 3, 2)));
	uint32_t mask = _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(maxAll, chunk1)));
	mask |= ((uint32_t)_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(maxAll, chunk2)))) << 4;
	mask |= ((uint32_t)_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(maxAll, chunk3)))) << 8;
	prevTabWidth = np2_ctz(mask);

#else
	prevTabWidth = 0;
	for (int i = 1; i < MAX_DETECTED_TAB_WIDTH + 2; i++) {
		if (indentLineCount[i] > indentLineCount[prevTabWidth]) {
			prevTabWidth = i;
		}
	}
#endif
	if (prevTabWidth != 0) {
		const bool bTabsAsSpaces = prevTabWidth <= MAX_DETECTED_TAB_WIDTH;
		lpfv->mask |= FV_TABSASSPACES;
		lpfv->bTabsAsSpaces = bTabsAsSpaces;
		if (bTabsAsSpaces) {
			lpfv->mask |= FV_MaskHasTabIndentWidth;
			lpfv->iTabWidth = prevTabWidth;
			lpfv->iIndentWidth = prevTabWidth;
		}
	}

#if 0
	StopWatch_Stop(watch);
	const double duration = StopWatch_Get(&watch);
	printf("indentation %u, duration=%.06f, tab width=%d\n", (UINT)cbData, duration, prevTabWidth);
	for (int i = 0; i < MAX_DETECTED_TAB_WIDTH + 2; i++) {
		printf("\tindentLineCount[%d] = %u\n", i, indentLineCount[i]);
	}
#endif
}

//=============================================================================
//
// EditLoadFile()
//
bool EditLoadFile(LPWSTR pszFile, EditFileIOStatus *status) {
	HANDLE hFile = CreateFile(pszFile,
					   GENERIC_READ,
					   FILE_SHARE_READ | FILE_SHARE_WRITE,
					   NULL, OPEN_EXISTING,
					   FILE_ATTRIBUTE_NORMAL,
					   NULL);
	dwLastIOError = GetLastError();

	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}

	LARGE_INTEGER fileSize;
	fileSize.QuadPart = 0;
	if (!GetFileSizeEx(hFile, &fileSize)) {
		dwLastIOError = GetLastError();
		CloseHandle(hFile);
		return false;
	}

	// display real path name
	PathGetRealPath(hFile, pszFile, pszFile);

	// Check if a warning message should be displayed for large files
#if defined(_WIN64)
	// less than 1/2 available physical memory:
	//     1. Buffers we allocated below or when saving file, depends on encoding.
	//     2. Scintilla's content buffer and style buffer, see CellBuffer class.
	//        The style buffer is disabled when using SCLEX_NULL (Text File, 2nd Text File, ANSI Art).
	//        i.e. when default scheme is Text File or 2nd Text File, memory required to load the file
	//        is about fileSize*2, buffers we allocated below can be reused by system to served
	//        as Scintilla's style buffer when calling SciCall_SetLexer() inside Style_SetLexer().
	//     3. Extra memory when moving gaps on editing, it may require more than 2/3 physical memory.
	// large file TODO: https://github.com/zufuliu/notepad2/issues/125
	// [ ] [> 4 GiB] use SetFilePointerEx() and ReadFile()/WriteFile() to read/write file.
	// [-] [> 2 GiB] fix encoding conversion with MultiByteToWideChar() and WideCharToMultiByte().
	LONGLONG maxFileSize = INT64_C(4) << 30;
#else
	// 2 GiB: ptrdiff_t / Sci_Position used in Scintilla
	LONGLONG maxFileSize = INT64_C(2) << 30;
#endif

	MEMORYSTATUSEX statex;
	statex.dwLength = sizeof(statex);
	if (GlobalMemoryStatusEx(&statex)) {
		const ULONGLONG maxMem = statex.ullTotalPhys/2U;
		if (maxMem < (ULONGLONG)maxFileSize) {
			maxFileSize = (LONGLONG)maxMem;
		}
	} else {
		dwLastIOError = GetLastError();
	}

	if (fileSize.QuadPart > maxFileSize) {
		CloseHandle(hFile);
		status->bFileTooBig = true;
		WCHAR tchDocSize[32];
		WCHAR tchMaxSize[32];
		WCHAR tchDocBytes[32];
		WCHAR tchMaxBytes[32];
		StrFormatByteSize(fileSize.QuadPart, tchDocSize, COUNTOF(tchDocSize));
		StrFormatByteSize(maxFileSize, tchMaxSize, COUNTOF(tchMaxSize));
		FormatNumber64(tchDocBytes, fileSize.QuadPart);
		FormatNumber64(tchMaxBytes, maxFileSize);
		MsgBoxWarn(MB_OK, IDS_WARNLOADBIGFILE, pszFile, tchDocSize, tchDocBytes, tchMaxSize, tchMaxBytes);
		return false;
	}

	char *lpData = (char *)NP2HeapAlloc((SIZE_T)(fileSize.QuadPart) + NP2_ENCODING_DETECTION_PADDING);
	DWORD cbData = 0;
	const BOOL bReadSuccess = ReadFile(hFile, lpData, (DWORD)(fileSize.QuadPart), &cbData, NULL);
	dwLastIOError = GetLastError();
	CloseHandle(hFile);

	if (!bReadSuccess) {
		NP2HeapFree(lpData);
		return false;
	}

	status->iEOLMode = GetScintillaEOLMode(iDefaultEOLMode);
	status->bInconsistent = false;
	status->totalLineCount = 1;

	int encodingFlag = EncodingFlag_None;
	int iEncoding = EditDetermineEncoding(pszFile, lpData, cbData, &encodingFlag);
	if (iEncoding == CPI_DEFAULT && encodingFlag == EncodingFlag_UTF7) {
		iEncoding = Encoding_GetAnsiIndex();
	}
	status->iEncoding = iEncoding;
	status->bBinaryFile = encodingFlag & EncodingFlag_Binary;
	UINT uFlags = mEncoding[iEncoding].uFlags;

	if (cbData == 0) {
		SciCall_SetCodePage((uFlags & NCP_DEFAULT) ? iDefaultCodePage : SC_CP_UTF8);
		EditSetEmptyText();
		SciCall_SetEOLMode(status->iEOLMode);
		NP2HeapFree(lpData);
		return true;
	}

	char *lpDataUTF8 = lpData;
	if (uFlags & NCP_UNICODE) {
		// cbData/2 => WCHAR, WCHAR*3 => UTF-8
		lpDataUTF8 = (char *)NP2HeapAlloc((cbData + 1)*sizeof(WCHAR));
		LPCWSTR pszTextW = (uFlags & NCP_UNICODE_BOM) ? ((LPWSTR)lpData + 1) : (LPWSTR)lpData;
		// NOTE: requires two extra trailing NULL bytes.
		const DWORD cchTextW = (uFlags & NCP_UNICODE_BOM) ? (cbData / sizeof(WCHAR)) : ((cbData / sizeof(WCHAR)) + 1);
		if ((uFlags & NCP_UNICODE_REVERSE) != 0 && encodingFlag != EncodingFlag_Reversed) {
			_swab(lpData, lpData, cbData);
		}
		cbData = WideCharToMultiByte(CP_UTF8, 0, pszTextW, cchTextW, lpDataUTF8, (int)NP2HeapSize(lpDataUTF8), NULL, NULL);
		if (cbData == 0) {
			const UINT legacyACP = mEncoding[CPI_DEFAULT].uCodePage;
			cbData = WideCharToMultiByte(legacyACP, 0, pszTextW, -1, lpDataUTF8, (int)NP2HeapSize(lpDataUTF8), NULL, NULL);
			status->bUnicodeErr = true;
		}
		if (cbData != 0) {
			// remove the NULL terminator.
			cbData -= 1;
		}

		NP2HeapFree(lpData);
		lpData = lpDataUTF8;
		FileVars_Init(lpData, cbData, &fvCurFile);
	} else if (uFlags & NCP_UTF8) {
		if (uFlags & NCP_UTF8_SIGN) {
			lpDataUTF8 += 3;
			cbData -= 3;
		}
	} else if (uFlags & (NCP_8BIT | NCP_7BIT)) {
		if (encodingFlag != EncodingFlag_UTF7 || (uFlags & NCP_7BIT) != 0) {
			const UINT uCodePage = mEncoding[iEncoding].uCodePage;
			lpDataUTF8 = RecodeAsUTF8(lpData, &cbData, uCodePage, 0);
			NP2HeapFree(lpData);
			lpData = lpDataUTF8;
		}
	} else if (cbData < MAX_NON_UTF8_SIZE && (encodingFlag & (EncodingFlag_Binary | EncodingFlag_Invalid)) == 0
		&& ((bLoadANSIasUTF8 && !(iSrcEncoding == CPI_DEFAULT || iWeakSrcEncoding == CPI_DEFAULT))
		|| (GetACP() == CP_UTF8))) {
		// try to load ANSI / unknown encoding as UTF-8
		DWORD back = cbData;
		const UINT legacyACP = mEncoding[CPI_DEFAULT].uCodePage;
		char * const result = RecodeAsUTF8(lpData, &back, legacyACP, MB_ERR_INVALID_CHARS);
		if (result) {
			NP2HeapFree(lpData);
			lpDataUTF8 = result;
			lpData = result;
			cbData = back;
			uFlags = 0;
			status->iEncoding = Encoding_GetIndex(legacyACP);
		}
	}

	if (cbData) {
		EditDetectEOLMode(lpDataUTF8, cbData, status);
		EditDetectIndentation(lpDataUTF8, cbData, &fvCurFile);
	}
	SciCall_SetCodePage((uFlags & NCP_DEFAULT) ? iDefaultCodePage : SC_CP_UTF8);
	EditSetNewText(lpDataUTF8, cbData, status->totalLineCount);

	NP2HeapFree(lpData);
	return true;
}

//=============================================================================
//
// EditSaveFile()
//
bool EditSaveFile(HWND hwnd, LPCWSTR pszFile, int saveFlag, EditFileIOStatus *status) {
	HANDLE hFile = CreateFile(pszFile,
					   GENERIC_READ | GENERIC_WRITE,
					   FILE_SHARE_READ | FILE_SHARE_WRITE,
					   NULL, OPEN_ALWAYS,
					   FILE_ATTRIBUTE_NORMAL,
					   NULL);
	dwLastIOError = GetLastError();

	// failure could be due to missing attributes (Windows 2000, XP)
	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD dwAttributes = GetFileAttributes(pszFile);
		if (dwAttributes != INVALID_FILE_ATTRIBUTES) {
			dwAttributes = dwAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
			hFile = CreateFile(pszFile,
							   GENERIC_READ | GENERIC_WRITE,
							   FILE_SHARE_READ | FILE_SHARE_WRITE,
							   NULL,
							   OPEN_ALWAYS,
							   FILE_ATTRIBUTE_NORMAL | dwAttributes,
							   NULL);
			dwLastIOError = GetLastError();
		}
	}

	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}

	if (!(saveFlag & FileSaveFlag_EndSession) && !bReadOnlyMode) {
		// ensure consistent line endings
		if (bFixLineEndings) {
			EditEnsureConsistentLineEndings();
		}

		// strip trailing blanks
		if (bAutoStripBlanks) {
			EditStripTrailingBlanks(hwnd, true);
		}
	}

	BOOL bWriteSuccess;
	// get text
	DWORD cbData = (DWORD)SciCall_GetLength();
	char *lpData = NULL;
	int iEncoding = status->iEncoding;
	UINT uFlags = mEncoding[iEncoding].uFlags;

	if (cbData == 0) {
		bWriteSuccess = SetEndOfFile(hFile);
		// write encoding BOM
		DWORD dwBytesWritten;
		if (uFlags & NCP_UNICODE_BOM) {
			if (uFlags & NCP_UNICODE_REVERSE) {
				bWriteSuccess = WriteFile(hFile, (LPCVOID)"\xFE\xFF", 2, &dwBytesWritten, NULL);
			} else {
				bWriteSuccess = WriteFile(hFile, (LPCVOID)"\xFF\xFE", 2, &dwBytesWritten, NULL);
			}
		} else if (uFlags & NCP_UTF8_SIGN) {
			bWriteSuccess = WriteFile(hFile, (LPCVOID)"\xEF\xBB\xBF", 3, &dwBytesWritten, NULL);
		}
		dwLastIOError = GetLastError();
	} else {
		if (cbData >= MAX_NON_UTF8_SIZE) {
			// save as UTF-8 or ANSI
			if (!(uFlags & (NCP_DEFAULT | NCP_UTF8))) {
				if (uFlags & NCP_UNICODE_BOM) {
					iEncoding = CPI_UTF8SIGN;
				} else {
					iEncoding = CPI_UTF8;
				}
				uFlags = mEncoding[iEncoding].uFlags;
			}
		}

		lpData = (char *)NP2HeapAlloc(cbData + 1);
		SciCall_GetText(cbData, lpData);
#if 0
		// FIXME: move checks in front of disk file access
		if ((uFlags & (NCP_UNICODE | NCP_UTF8_SIGN)) == 0) {
			FILEVARS fv;
			FileVars_Init(lpData, cbData, &fv);
			const int iAltEncoding = FileVars_GetEncoding(&fv);
			if (iAltEncoding >= CPI_FIRST && iAltEncoding != iEncoding
				&& !((uFlags & NCP_UTF8) && (mEncoding[iAltEncoding].uFlags & NCP_UTF8))) {
				Encoding_GetLabel(iAltEncoding);
				Encoding_GetLabel(iEncoding);
				InfoBoxWarn(MB_OK, L"MsgEncodingMismatch", IDS_ENCODINGMISMATCH,
					mEncoding[iAltEncoding].wchLabel,
					mEncoding[iEncoding].wchLabel);
			}
		}
#endif

		DWORD dwBytesWritten;
		if (uFlags & NCP_UNICODE) {
			SetEndOfFile(hFile);

			LPWSTR lpDataWide = (LPWSTR)NP2HeapAlloc(cbData * sizeof(WCHAR) + 16);
			const int cbDataWide = MultiByteToWideChar(CP_UTF8, 0, lpData, cbData, lpDataWide, (int)(NP2HeapSize(lpDataWide) / sizeof(WCHAR)));

			if (uFlags & NCP_UNICODE_BOM) {
				if (uFlags & NCP_UNICODE_REVERSE) {
					WriteFile(hFile, (LPCVOID)"\xFE\xFF", 2, &dwBytesWritten, NULL);
				} else {
					WriteFile(hFile, (LPCVOID)"\xFF\xFE", 2, &dwBytesWritten, NULL);
				}
			}

			if (uFlags & NCP_UNICODE_REVERSE) {
				_swab((char *)lpDataWide, (char *)lpDataWide, (int)(cbDataWide * sizeof(WCHAR)));
			}

			bWriteSuccess = WriteFile(hFile, lpDataWide, cbDataWide * sizeof(WCHAR), &dwBytesWritten, NULL);
			dwLastIOError = GetLastError();

			NP2HeapFree(lpDataWide);
		} else if (uFlags & NCP_UTF8) {
			SetEndOfFile(hFile);

			if (uFlags & NCP_UTF8_SIGN) {
				WriteFile(hFile, (LPCVOID)"\xEF\xBB\xBF", 3, &dwBytesWritten, NULL);
			}

			bWriteSuccess = WriteFile(hFile, lpData, cbData, &dwBytesWritten, NULL);
			dwLastIOError = GetLastError();
		} else if (uFlags & (NCP_8BIT | NCP_7BIT)) {
			BOOL bCancelDataLoss = FALSE;
			const UINT uCodePage = mEncoding[iEncoding].uCodePage;

			LPWSTR lpDataWide = (LPWSTR)NP2HeapAlloc(cbData * sizeof(WCHAR) + 16);
			const int cbDataWide = MultiByteToWideChar(CP_UTF8, 0, lpData, cbData, lpDataWide, (int)(NP2HeapSize(lpDataWide) / sizeof(WCHAR)));

			if (IsZeroFlagsCodePage(uCodePage)) {
				NP2HeapFree(lpData);
				lpData = (char *)NP2HeapAlloc(NP2HeapSize(lpDataWide) * 2);
			} else {
				memset(lpData, 0, NP2HeapSize(lpData));
				cbData = WideCharToMultiByte(uCodePage, WC_NO_BEST_FIT_CHARS, lpDataWide, cbDataWide, lpData, (int)NP2HeapSize(lpData), NULL, &bCancelDataLoss);
			}

			if (!bCancelDataLoss) {
				cbData = WideCharToMultiByte(uCodePage, 0, lpDataWide, cbDataWide, lpData, (int)NP2HeapSize(lpData), NULL, NULL);
			}
			NP2HeapFree(lpDataWide);

			if (!bCancelDataLoss || InfoBoxWarn(MB_OKCANCEL, L"MsgConv3", IDS_ERR_UNICODE2) == IDOK) {
				SetEndOfFile(hFile);
				bWriteSuccess = WriteFile(hFile, lpData, cbData, &dwBytesWritten, NULL);
				dwLastIOError = GetLastError();
			} else {
				bWriteSuccess = FALSE;
				status->bCancelDataLoss = true;
			}
		} else {
			SetEndOfFile(hFile);
			bWriteSuccess = WriteFile(hFile, lpData, cbData, &dwBytesWritten, NULL);
			dwLastIOError = GetLastError();
		}
	}

	if (lpData != NULL) {
		NP2HeapFree(lpData);
	}

	CloseHandle(hFile);
	if (bWriteSuccess) {
		if (!(saveFlag & FileSaveFlag_SaveCopy)) {
			SciCall_SetSavePoint();
		}
		return true;
	}

	return false;
}

void EditReplaceRange(Sci_Position iSelStart, Sci_Position iSelEnd, Sci_Position cchText, LPCSTR pszText) {
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();

	if (iAnchorPos > iCurPos) {
		iCurPos = iSelStart;
		iAnchorPos = iSelStart + cchText;
	} else {
		iAnchorPos = iSelStart;
		iCurPos = iSelStart + cchText;
	}

	SciCall_SetTargetRange(iSelStart, iSelEnd);
	SciCall_ReplaceTarget(cchText, pszText);
	SciCall_SetSel(iAnchorPos, iCurPos);
}

void EditReplaceMainSelection(Sci_Position cchText, LPCSTR pszText) {
	EditReplaceRange(SciCall_GetSelectionStart(), SciCall_GetSelectionEnd(), cchText, pszText);
}

static inline char *EditGetTextRange(Sci_Position iStartPos, Sci_Position iEndPos) {
	const Sci_Position len = iEndPos - iStartPos;
	if (len <= 0) {
		return NULL;
	}

	char *mszBuf = (char *)NP2HeapAlloc(len + 1);
	const struct Sci_TextRangeFull tr = { { iStartPos, iEndPos }, mszBuf };
	SciCall_GetTextRangeFull(&tr);
	return mszBuf;
}

//=============================================================================
//
// EditInvertCase()
//
void EditInvertCase(void) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	char *pszText = (char *)NP2HeapAlloc(iSelCount*kMaxMultiByteCount + 1);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	SciCall_GetSelText(pszText);
	const UINT cpEdit = SciCall_GetCodePage();
	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));

	bool bChanged = false;
	for (int i = 0; i < cchTextW; i++) {
		if (IsCharUpper(pszTextW[i])) {
			pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
			bChanged = true;
		} else if (IsCharLower(pszTextW[i])) {
			pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
			bChanged = true;
		}
	}

	if (bChanged) {
		const int cchText = WideCharToMultiByte(cpEdit, 0, pszTextW, cchTextW, pszText, (int)NP2HeapSize(pszText), NULL, NULL);
		EditReplaceMainSelection(cchText, pszText);
	}

	NP2HeapFree(pszText);
	NP2HeapFree(pszTextW);
}

// https://docs.microsoft.com/en-us/windows/win32/intl/transliteration-services
#include <elscore.h>
#if defined(__MINGW32__)
#if defined(__has_include) && __has_include(<elssrvc.h>)
#include <elssrvc.h>
#else
// {A3A8333B-F4FC-42f6-A0C4-0462FE7317CB}
static const GUID ELS_GUID_TRANSLITERATION_HANT_TO_HANS =
	{ 0xA3A8333B, 0xF4FC, 0x42f6, { 0xA0, 0xC4, 0x04, 0x62, 0xFE, 0x73, 0x17, 0xCB } };

// {3CACCDC8-5590-42dc-9A7B-B5A6B5B3B63B}
static const GUID ELS_GUID_TRANSLITERATION_HANS_TO_HANT =
	{ 0x3CACCDC8, 0x5590, 0x42dc, { 0x9A, 0x7B, 0xB5, 0xA6, 0xB5, 0xB3, 0xB6, 0x3B } };

// {D8B983B1-F8BF-4a2b-BCD5-5B5EA20613E1}
static const GUID ELS_GUID_TRANSLITERATION_MALAYALAM_TO_LATIN =
	{ 0xD8B983B1, 0xF8BF, 0x4a2b, { 0xBC, 0xD5, 0x5B, 0x5E, 0xA2, 0x06, 0x13, 0xE1 } };

// {C4A4DCFE-2661-4d02-9835-F48187109803}
static const GUID ELS_GUID_TRANSLITERATION_DEVANAGARI_TO_LATIN =
	{ 0xC4A4DCFE, 0x2661, 0x4d02, { 0x98, 0x35, 0xF4, 0x81, 0x87, 0x10, 0x98, 0x03 } };

// {3DD12A98-5AFD-4903-A13F-E17E6C0BFE01}
static const GUID ELS_GUID_TRANSLITERATION_CYRILLIC_TO_LATIN =
	{ 0x3DD12A98, 0x5AFD, 0x4903, { 0xA1, 0x3F, 0xE1, 0x7E, 0x6C, 0x0B, 0xFE, 0x01 } };

// {F4DFD825-91A4-489f-855E-9AD9BEE55727}
static const GUID ELS_GUID_TRANSLITERATION_BENGALI_TO_LATIN =
	{ 0xF4DFD825, 0x91A4, 0x489f, { 0x85, 0x5E, 0x9A, 0xD9, 0xBE, 0xE5, 0x57, 0x27 } };
#endif // __MINGW32__
#else
#include <elssrvc.h>
#endif

// {4BA2A721-E43D-41b7-B330-536AE1E48863}
static const GUID WIN10_ELS_GUID_TRANSLITERATION_HANGUL_DECOMPOSITION =
	{ 0x4BA2A721, 0xE43D, 0x41b7, { 0xB3, 0x30, 0x53, 0x6A, 0xE1, 0xE4, 0x88, 0x63 } };

static int TransliterateText(const GUID *pGuid, LPCWSTR pszTextW, int cchTextW, LPWSTR *pszMappedW) {
#if NP2_DYNAMIC_LOAD_ELSCORE_DLL
typedef HRESULT (WINAPI *MappingGetServicesSig)(PMAPPING_ENUM_OPTIONS pOptions, PMAPPING_SERVICE_INFO *prgServices, DWORD *pdwServicesCount);
typedef HRESULT (WINAPI *MappingFreeServicesSig)(PMAPPING_SERVICE_INFO pServiceInfo);
typedef HRESULT (WINAPI *MappingRecognizeTextSig)(PMAPPING_SERVICE_INFO pServiceInfo, LPCWSTR pszText, DWORD dwLength, DWORD dwIndex, PMAPPING_OPTIONS pOptions, PMAPPING_PROPERTY_BAG pbag);
typedef HRESULT (WINAPI *MappingFreePropertyBagSig)(PMAPPING_PROPERTY_BAG pBag);

	static int triedLoadingELSCore = 0;
	static MappingGetServicesSig pfnMappingGetServices;
	static MappingFreeServicesSig pfnMappingFreeServices;
	static MappingRecognizeTextSig pfnMappingRecognizeText;
	static MappingFreePropertyBagSig pfnMappingFreePropertyBag;

	if (triedLoadingELSCore == 0) {
		triedLoadingELSCore = 1;
		hELSCoreDLL = LoadLibraryExW(L"elscore.dll", NULL, kSystemLibraryLoadFlags);
		if (hELSCoreDLL != NULL) {
			pfnMappingGetServices = DLLFunction(MappingGetServicesSig, hELSCoreDLL, "MappingGetServices");
			pfnMappingFreeServices = DLLFunction(MappingFreeServicesSig, hELSCoreDLL, "MappingFreeServices");
			pfnMappingRecognizeText = DLLFunction(MappingRecognizeTextSig, hELSCoreDLL, "MappingRecognizeText");
			pfnMappingFreePropertyBag = DLLFunction(MappingFreePropertyBagSig, hELSCoreDLL, "MappingFreePropertyBag");
			if (pfnMappingGetServices == NULL || pfnMappingFreeServices == NULL || pfnMappingRecognizeText == NULL || pfnMappingFreePropertyBag == NULL) {
				FreeLibrary(hELSCoreDLL);
				hELSCoreDLL = NULL;
				return 0;
			}
			triedLoadingELSCore = 2;
		}
	}
	if (triedLoadingELSCore != 2) {
		return 0;
	}
#endif

	MAPPING_ENUM_OPTIONS enumOptions;
	PMAPPING_SERVICE_INFO prgServices = NULL;
	DWORD dwServicesCount = 0;

	memset(&enumOptions, 0, sizeof(MAPPING_ENUM_OPTIONS));
	enumOptions.Size = sizeof(MAPPING_ENUM_OPTIONS);
	enumOptions.pGuid = (GUID *)pGuid;

#if NP2_DYNAMIC_LOAD_ELSCORE_DLL
	HRESULT hr = pfnMappingGetServices(&enumOptions, &prgServices, &dwServicesCount);
#else
	HRESULT hr = MappingGetServices(&enumOptions, &prgServices, &dwServicesCount);
#endif
	dwServicesCount = 0;
	if (SUCCEEDED(hr)) {
		MAPPING_PROPERTY_BAG bag;
		memset(&bag, 0, sizeof(MAPPING_PROPERTY_BAG));
		bag.Size = sizeof (MAPPING_PROPERTY_BAG);
#if NP2_DYNAMIC_LOAD_ELSCORE_DLL
		hr = pfnMappingRecognizeText(prgServices, pszTextW, cchTextW, 0, NULL, &bag);
#else
		hr = MappingRecognizeText(prgServices, pszTextW, cchTextW, 0, NULL, &bag);
#endif
		if (SUCCEEDED(hr)) {
			const DWORD dwDataSize = bag.prgResultRanges[0].dwDataSize;
			dwServicesCount = dwDataSize/sizeof(WCHAR);
			pszTextW = (LPCWSTR)bag.prgResultRanges[0].pData;
			if (dwServicesCount != 0 && pszTextW[0] != L'\0') {
				LPWSTR pszConvW = (LPWSTR)NP2HeapAlloc(dwDataSize + sizeof(WCHAR));
				memcpy(pszConvW, pszTextW, dwDataSize);
				*pszMappedW = pszConvW;
			}
#if NP2_DYNAMIC_LOAD_ELSCORE_DLL
			pfnMappingFreePropertyBag(&bag);
#else
			MappingFreePropertyBag(&bag);
#endif
		}
#if NP2_DYNAMIC_LOAD_ELSCORE_DLL
		pfnMappingFreeServices(prgServices);
#else
		MappingFreeServices(prgServices);
#endif
	}

	return dwServicesCount;
}

#if _WIN32_WINNT < _WIN32_WINNT_WIN7
static bool EditTitleCase(LPWSTR pszTextW, int cchTextW) {
	bool bChanged = false;
#if 1
	// BOOKMARK_EDITION
	//Slightly enhanced function to make Title Case:
	//Added some '-characters and bPrevWasSpace makes it better (for example "'Don't'" will now work)
	bool bNewWord = true;
	bool bPrevWasSpace = true;
	for (int i = 0; i < cchTextW; i++) {
		const WCHAR ch = pszTextW[i];
		if (!IsCharAlphaNumeric(ch) && (!(ch == L'\'' || ch == L'`' || ch == 0xB4 || ch == 0x0384 || ch == 0x2019) || bPrevWasSpace)) {
			bNewWord = true;
		} else {
			if (bNewWord) {
				if (IsCharLower(ch)) {
					pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = true;
				}
			} else {
				if (IsCharUpper(ch)) {
					pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = true;
				}
			}
			bNewWord = false;
		}

		bPrevWasSpace = IsASpace(ch) || ch == L'[' || ch == L']' || ch == L'(' || ch == L')' || ch == L'{' || ch == L'}';
	}
#else
	bool bNewWord = true;
	bool bWordEnd = true;
	for (int i = 0; i < cchTextW; i++) {
		const WCHAR ch = pszTextW[i];
		const BOOL bAlphaNumeric = IsCharAlphaNumeric(ch);
		if (!bAlphaNumeric && (!(ch == L'\'' || ch == L'`' || ch == 0xB4 || ch == 0x0384 || ch == 0x2019) || bWordEnd)) {
			bNewWord = true;
		} else {
			if (bNewWord) {
				if (IsCharLower(ch)) {
					pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = true;
				}
			} else {
				if (IsCharUpper(ch)) {
					pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = true;
				}
			}
			bNewWord = false;
		}
		bWordEnd = !bAlphaNumeric;
	}
#endif

	return bChanged;
}
#endif

//=============================================================================
//
// EditMapTextCase()
//
void EditMapTextCase(int menu) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	DWORD flags = 0;
	const GUID *pGuid = NULL;
	switch (menu) {
	case IDM_EDIT_TITLECASE:
		flags = IsWin7AndAbove() ? (LCMAP_LINGUISTIC_CASING | LCMAP_TITLECASE) : 0;
		break;
	case IDM_EDIT_MAP_FULLWIDTH:
		flags = LCMAP_FULLWIDTH;
		break;
	case IDM_EDIT_MAP_HALFWIDTH:
		flags = LCMAP_HALFWIDTH;
		break;
	case IDM_EDIT_MAP_SIMPLIFIED_CHINESE:
		flags = LCMAP_SIMPLIFIED_CHINESE;
		pGuid = &ELS_GUID_TRANSLITERATION_HANT_TO_HANS;
		break;
	case IDM_EDIT_MAP_TRADITIONAL_CHINESE:
		flags = LCMAP_TRADITIONAL_CHINESE;
		pGuid = &ELS_GUID_TRANSLITERATION_HANS_TO_HANT;
		break;
	case IDM_EDIT_MAP_HIRAGANA:
		flags = LCMAP_HIRAGANA;
		break;
	case IDM_EDIT_MAP_KATAKANA:
		flags = LCMAP_KATAKANA;
		break;
	case IDM_EDIT_MAP_MALAYALAM_LATIN:
		pGuid = &ELS_GUID_TRANSLITERATION_MALAYALAM_TO_LATIN;
		break;
	case IDM_EDIT_MAP_DEVANAGARI_LATIN:
		pGuid = &ELS_GUID_TRANSLITERATION_DEVANAGARI_TO_LATIN;
		break;
	case IDM_EDIT_MAP_CYRILLIC_LATIN:
		pGuid = &ELS_GUID_TRANSLITERATION_CYRILLIC_TO_LATIN;
		break;
	case IDM_EDIT_MAP_BENGALI_LATIN:
		pGuid = &ELS_GUID_TRANSLITERATION_BENGALI_TO_LATIN;
		break;
	case IDM_EDIT_MAP_HANGUL_DECOMPOSITION:
		pGuid = &WIN10_ELS_GUID_TRANSLITERATION_HANGUL_DECOMPOSITION;
		break;
	case IDM_EDIT_MAP_HANJA_HANGUL:
		// implemented in ScintillaWin::SelectionToHangul().
		SendMessage(hwndEdit, WM_IME_KEYDOWN, VK_HANJA, 0);
		return;
	default:
		NP2_unreachable();
	}

	char *pszText = (char *)NP2HeapAlloc(iSelCount*kMaxMultiByteCount + 1);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	SciCall_GetSelText(pszText);
	const UINT cpEdit = SciCall_GetCodePage();
	int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));

	bool bChanged = false;
	if (flags != 0 || pGuid != NULL) {
		int charsConverted = 0;
		LPWSTR pszMappedW = NULL;
		if (pGuid != NULL && IsWin7AndAbove()) {
			charsConverted = TransliterateText(pGuid, pszTextW, cchTextW, &pszMappedW);
		}
		if (pszMappedW == NULL && flags != 0) {
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
			charsConverted = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, pszTextW, cchTextW, NULL, 0, NULL, NULL, 0);
#else
			charsConverted = LCMapString(LOCALE_USER_DEFAULT, flags, pszTextW, cchTextW, NULL, 0);
#endif
			if (charsConverted) {
				pszMappedW = (LPWSTR)NP2HeapAlloc((charsConverted + 1)*sizeof(WCHAR));
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
				charsConverted = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, pszTextW, cchTextW, pszMappedW, charsConverted, NULL, NULL, 0);
#else
				charsConverted = LCMapString(LOCALE_USER_DEFAULT, flags, pszTextW, cchTextW, pszMappedW, charsConverted);
#endif
			}
		}

		bChanged = !(charsConverted == 0 || StrIsEmpty(pszMappedW) || StrEqual(pszTextW, pszMappedW));
		if (bChanged) {
			NP2HeapFree(pszTextW);
			pszTextW = pszMappedW;
			cchTextW = charsConverted;
			if (charsConverted > iSelCount) {
				NP2HeapFree(pszText);
				pszText = (char *)NP2HeapAlloc(charsConverted*kMaxMultiByteCount + 1);
			}
		} else if (pszMappedW != NULL) {
			NP2HeapFree(pszMappedW);
		}
	}

#if _WIN32_WINNT < _WIN32_WINNT_WIN7
	else if (menu == IDM_EDIT_TITLECASE) {
		bChanged = EditTitleCase(pszTextW, cchTextW);
	}
#endif

	if (bChanged) {
		const int cchText = WideCharToMultiByte(cpEdit, 0, pszTextW, cchTextW, pszText, (int)NP2HeapSize(pszText), NULL, NULL);
		EditReplaceMainSelection(cchText, pszText);
	}

	NP2HeapFree(pszText);
	NP2HeapFree(pszTextW);
}

//=============================================================================
//
// EditSentenceCase()
//
void EditSentenceCase(void) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	char *pszText = (char *)NP2HeapAlloc(iSelCount*kMaxMultiByteCount + 1);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	SciCall_GetSelText(pszText);
	const UINT cpEdit = SciCall_GetCodePage();
	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));

	bool bNewSentence = true;
	bool bChanged = false;
	for (int i = 0; i < cchTextW; i++) {
		const WCHAR ch = pszTextW[i];
		if (ch == L'.' || ch == L';' || ch == L'!' || ch == L'?' || ch == L'\r' || ch == L'\n') {
			bNewSentence = true;
		} else {
			if (IsCharAlphaNumeric(ch)) {
				if (bNewSentence) {
					if (IsCharLower(ch)) {
						pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
						bChanged = true;
					}
					bNewSentence = false;
				} else {
					if (IsCharUpper(ch)) {
						pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
						bChanged = true;
					}
				}
			}
		}
	}

	if (bChanged) {
		const int cchText = WideCharToMultiByte(cpEdit, 0, pszTextW, cchTextW, pszText, (int)NP2HeapSize(pszText), NULL, NULL);
		EditReplaceMainSelection(cchText, pszText);
	}

	NP2HeapFree(pszText);
	NP2HeapFree(pszTextW);
}

#ifndef URL_ESCAPE_AS_UTF8		// NTDDI_VERSION >= NTDDI_WIN7
#define URL_ESCAPE_AS_UTF8		0x00040000
#endif
#ifndef URL_UNESCAPE_AS_UTF8	// NTDDI_VERSION >= NTDDI_WIN8
#define URL_UNESCAPE_AS_UTF8	URL_ESCAPE_AS_UTF8
#endif

//=============================================================================
//
// EditURLEncode()
//
LPWSTR EditURLEncodeSelection(int *pcchEscaped) {
	*pcchEscaped = 0;
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount == 0) {
		return NULL;
	}

	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1);
	SciCall_GetSelText(pszText);

	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));
	const UINT cpEdit = SciCall_GetCodePage();
	MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	NP2HeapFree(pszText);
	// TODO: trim all C0 and C1 control characters.
	StrTrim(pszTextW, L" \a\b\f\n\r\t\v");
	if (StrIsEmpty(pszTextW)) {
		NP2HeapFree(pszTextW);
		return NULL;
	}

	// https://docs.microsoft.com/en-us/windows/desktop/api/shlwapi/nf-shlwapi-urlescapew
	LPWSTR pszEscapedW = (LPWSTR)NP2HeapAlloc(NP2HeapSize(pszTextW) * kMaxMultiByteCount * 3); // '&', H1, H0

	DWORD cchEscapedW = (int)NP2HeapSize(pszEscapedW) / sizeof(WCHAR);
	UrlEscape(pszTextW, pszEscapedW, &cchEscapedW, URL_ESCAPE_AS_UTF8);
	if (!IsWin7AndAbove()) {
		// TODO: encode some URL parts as UTF-8 then percent-escape these UTF-8 bytes.
		//ParseURL(pszEscapedW, &ppu);
	}

	NP2HeapFree(pszTextW);
	*pcchEscaped = cchEscapedW;
	return pszEscapedW;
}

void EditURLEncode(void) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	int cchEscapedW;
	LPWSTR pszEscapedW = EditURLEncodeSelection(&cchEscapedW);
	if (pszEscapedW == NULL) {
		return;
	}

	const UINT cpEdit = SciCall_GetCodePage();
	char *pszEscaped = (char *)NP2HeapAlloc(cchEscapedW * kMaxMultiByteCount);
	const int cchEscaped = WideCharToMultiByte(cpEdit, 0, pszEscapedW, cchEscapedW, pszEscaped, (int)NP2HeapSize(pszEscaped), NULL, NULL);
	EditReplaceMainSelection(cchEscaped, pszEscaped);

	NP2HeapFree(pszEscaped);
	NP2HeapFree(pszEscapedW);
}

//=============================================================================
//
// EditURLDecode()
//
void EditURLDecode(void) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	SciCall_GetSelText(pszText);
	const UINT cpEdit = SciCall_GetCodePage();
	MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));

	char *pszUnescaped = (char *)NP2HeapAlloc(NP2HeapSize(pszText) * 3);
	LPWSTR pszUnescapedW = (LPWSTR)NP2HeapAlloc(NP2HeapSize(pszTextW) * 3);

	DWORD cchUnescapedW = (DWORD)(NP2HeapSize(pszUnescapedW) / sizeof(WCHAR));
	int cchUnescaped = cchUnescapedW;
	UrlUnescape(pszTextW, pszUnescapedW, &cchUnescapedW, URL_UNESCAPE_AS_UTF8);
	if (!IsWin8AndAbove()) {
		char *ptr = pszUnescaped;
		WCHAR *t = pszUnescapedW;
		WCHAR ch;
		while ((ch = *t++) != 0) {
			if (ch > 0xff) {
				break;
			}
			*ptr++ = (char)ch;
		}
		*ptr = '\0';
		if (ptr == pszUnescaped + cchUnescapedW && IsUTF8(pszUnescaped, cchUnescapedW)) {
			cchUnescapedW = MultiByteToWideChar(CP_UTF8, 0, pszUnescaped, cchUnescapedW, pszUnescapedW, cchUnescaped);
		}
	}

	cchUnescaped = WideCharToMultiByte(cpEdit, 0, pszUnescapedW, cchUnescapedW, pszUnescaped, (int)NP2HeapSize(pszUnescaped), NULL, NULL);
	EditReplaceMainSelection(cchUnescaped, pszUnescaped);

	NP2HeapFree(pszText);
	NP2HeapFree(pszTextW);
	NP2HeapFree(pszUnescaped);
	NP2HeapFree(pszUnescapedW);
}

//=============================================================================
//
// EditEscapeCChars()
//
void EditEscapeCChars(HWND hwnd) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	EDITFINDREPLACE efr;
	memset(&efr, 0, sizeof(efr));
	efr.hwnd = hwnd;
	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "\\");
	strcpy(efr.szReplace, "\\\\");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "\"");
	strcpy(efr.szReplace, "\\\"");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "\'");
	strcpy(efr.szReplace, "\\\'");
	EditReplaceAllInSelection(hwnd, &efr, false);

	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditUnescapeCChars()
//
void EditUnescapeCChars(HWND hwnd) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	EDITFINDREPLACE efr;
	memset(&efr, 0, sizeof(efr));
	efr.hwnd = hwnd;
	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "\\\\");
	strcpy(efr.szReplace, "\\");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "\\\"");
	strcpy(efr.szReplace, "\"");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "\\\'");
	strcpy(efr.szReplace, "\'");
	EditReplaceAllInSelection(hwnd, &efr, false);

	SciCall_EndUndoAction();
}

// XML/HTML predefined entity
// https://en.wikipedia.org/wiki/List_of_XML_and_HTML_character_entity_references
// &quot;	["]
// &amp;	[&]
// &apos;	[']
// &lt;		[<]
// &gt;		[>]
// &nbsp;	[ ]
// &emsp;	[\t]
//=============================================================================
//
// EditEscapeXHTMLChars()
//
void EditEscapeXHTMLChars(HWND hwnd) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	EDITFINDREPLACE efr;
	memset(&efr, 0, sizeof(efr));
	efr.hwnd = hwnd;
	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "&");
	strcpy(efr.szReplace, "&amp;");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "\"");
	strcpy(efr.szReplace, "&quot;");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "\'");
	strcpy(efr.szReplace, "&apos;");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "<");
	strcpy(efr.szReplace, "&lt;");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, ">");
	strcpy(efr.szReplace, "&gt;");
	EditReplaceAllInSelection(hwnd, &efr, false);

	if (pLexCurrent->iLexer != SCLEX_XML) {
		strcpy(efr.szFind, " ");
		strcpy(efr.szReplace, "&nbsp;");
		EditReplaceAllInSelection(hwnd, &efr, false);

		strcpy(efr.szFind, "\t");
		strcpy(efr.szReplace, "&emsp;");
		EditReplaceAllInSelection(hwnd, &efr, false);
	}

	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditUnescapeXHTMLChars()
//
void EditUnescapeXHTMLChars(HWND hwnd) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	EDITFINDREPLACE efr;
	memset(&efr, 0, sizeof(efr));
	efr.hwnd = hwnd;
	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "&quot;");
	strcpy(efr.szReplace, "\"");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "&apos;");
	strcpy(efr.szReplace, "\'");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "&lt;");
	strcpy(efr.szReplace, "<");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "&gt;");
	strcpy(efr.szReplace, ">");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "&nbsp;");
	strcpy(efr.szReplace, " ");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "&amp;");
	strcpy(efr.szReplace, "&");
	EditReplaceAllInSelection(hwnd, &efr, false);

	strcpy(efr.szFind, "&emsp;");
	strcpy(efr.szReplace, "\t");
	EditReplaceAllInSelection(hwnd, &efr, false);
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditChar2Hex()
//

/*				C/C++	C#	Java	JS	JSON	Python	PHP	Lua		Go
\ooo		3	1			1					1		1	1/ddd	1
\xHH		2	1		1			1			1		1			1
\uHHHH		4	1			1		1	1		1					1
\UHHHHHHHH	8	1								1					1
\xHHHH		4			1
\uHHHHHH	6				1
*/
#define BMP_UNICODE_HEX_DIGIT	4
#define MAX_UNICODE_HEX_DIGIT	8

void EditChar2Hex(void) {
	Sci_Position count = SciCall_GetSelTextLength();
	if (count == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	count *= 2 + BMP_UNICODE_HEX_DIGIT;
	count += 1;
	char *ch = (char *)NP2HeapAlloc(count + 10);
	WCHAR *wch = (WCHAR *)NP2HeapAlloc(count * sizeof(WCHAR));
	SciCall_GetSelText(ch);

	int outLen = 0;
	if (ch[0] == '\0') {
		outLen = 4;
		strcpy(ch, "\\x00");
	} else {
		const UINT cpEdit = SciCall_GetCodePage();
		count = MultiByteToWideChar(cpEdit, 0, ch, -1, wch, (int)count) - 1; // '\0'
		for (Sci_Position i = 0; i < count; i++) {
			const WCHAR c = wch[i];
			if (c <= 0xFF) {
				outLen += sprintf(ch + outLen, "\\x%02X", c); // \xHH
			} else {
				outLen += sprintf(ch + outLen, "\\u%04X", c); // \uHHHH
			}
		}
		if (count == 2 && IS_SURROGATE_PAIR(wch[0], wch[1])) {
			const UINT value = UTF16_TO_UTF32(wch[0], wch[1]);
			outLen += sprintf(ch + outLen, " U+%X", value);
		}
	}

	EditReplaceMainSelection(outLen, ch);
	NP2HeapFree(ch);
	NP2HeapFree(wch);
}

//=============================================================================
//
// EditHex2Char()
//
void EditHex2Char(void) {
	Sci_Position count = SciCall_GetSelTextLength();
	if (count == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	count *= 2 + BMP_UNICODE_HEX_DIGIT;
	count += 1;
	char *ch = (char *)NP2HeapAlloc(count);
	WCHAR *wch = (WCHAR *)NP2HeapAlloc(count * sizeof(WCHAR));
	const UINT cpEdit = SciCall_GetCodePage();

	SciCall_GetSelText(ch);
	MultiByteToWideChar(cpEdit, 0, ch, -1, wch, (int)count);

	const WCHAR *p = wch;
	WCHAR *t = wch;
	bool changed = false;
	while (*p) {
		UINT wc = *p++;
		if ((wc == L'\\' && (*p == L'x' || UnsafeLower(*p) == 'u')) || (wc == L'U' && *p == L'+')) {
			const int digitCount = (wc == L'U' || *p == L'U') ? MAX_UNICODE_HEX_DIGIT : BMP_UNICODE_HEX_DIGIT;
			UINT value = 0;
			int ucc = 1;
			p++;
			for (; ucc <= digitCount && *p; ucc++) {
				const int hex = GetHexDigit(*p);
				if (hex < 0) {
					break;
				}
				value = (value << 4) | hex;
				p++;
			}
			if (value != 0 && value <= MAX_UNICODE) {
				changed = true;
				// see UTF16FromUTF32Character() in UniConversion.h
				if (value < SUPPLEMENTAL_PLANE_FIRST) {
					wc = value;
				} else {
					*t++ = (WCHAR)(((value - SUPPLEMENTAL_PLANE_FIRST) >> 10) + SURROGATE_LEAD_FIRST);
					wc = (value & 0x3ff) + SURROGATE_TRAIL_FIRST;
				}
			} else {
				p -= ucc;
			}
		}
		*t++ = (WCHAR)wc;
	}

	if (changed) {
		*t = L'\0';
		count = WideCharToMultiByte(cpEdit, 0, wch, (int)(t - wch), ch, (int)count, NULL, NULL);
		EditReplaceMainSelection(count, ch);
	}

	NP2HeapFree(ch);
	NP2HeapFree(wch);
}

void EditShowHex(void) {
	const Sci_Position count = SciCall_GetSelTextLength();
	if (count == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	char *ch = (char *)NP2HeapAlloc(count + 1);
	char *cch = (char *)NP2HeapAlloc(count * 3 + 3);
	SciCall_GetSelBytes(ch);
	const uint8_t *p = (const uint8_t *)ch;
	const uint8_t * const end = p + count;
	char *t = cch;
	*t++ = '[';
	do {
		const uint8_t c = *p++;
		*t++ = "0123456789ABCDEF"[c >> 4];
		*t++ = "0123456789ABCDEF"[c & 15];
		*t++ = ' ';
	} while (p < end);
	t[-1] = ']';

	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	SciCall_InsertText(iSelEnd, cch);
	SciCall_SetSel(iSelEnd, iSelEnd + (t - cch));
	NP2HeapFree(ch);
	NP2HeapFree(cch);
}

void EditBase64Encode(Base64EncodingFlag encodingFlag){
	const size_t len = SciCall_GetSelTextLength();
	if (len == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()){
		NotifyRectangleSelection();
		return;
	}

	char *input = (char *)NP2HeapAlloc(len + 1);
	SciCall_GetSelBytes(input);
	size_t outLen = (len*4)/3 + 4 + MAX_PATH*2;
	char *output = (char *)NP2HeapAlloc(outLen);
	outLen = 0;
	if (encodingFlag == Base64EncodingFlag_HtmlEmbeddedImage) {
		memcpy(output, "<img src=\"data:image/", CSTRLEN("<img src=\"data:image/"));
		outLen = CSTRLEN("<img src=\"data:image/");
		LPCWSTR suffix = PathFindExtension(szCurFile);
		if (*suffix == L'.') {
			// image file extension should be ASCII
			++suffix;
			while (*suffix) {
				output[outLen++] = (char)ToLowerA(*suffix++);
			}
		}
		memcpy(output + outLen, ";base64,", CSTRLEN(";base64,"));
		outLen += CSTRLEN(";base64,");
	}
	outLen += Base64Encode(output + outLen, (const uint8_t *)input, len, encodingFlag == Base64EncodingFlag_UrlSafe);
	if (encodingFlag == Base64EncodingFlag_HtmlEmbeddedImage) {
		memcpy(output + outLen, "\" />", CSTRLEN("\" />"));
		outLen += CSTRLEN("\" />");
	}

	EditReplaceMainSelection(outLen, output);
	NP2HeapFree(input);
	NP2HeapFree(output);
}

void EditBase64Decode(bool decodeAsHex) {
	size_t len = SciCall_GetSelTextLength();
	if (len == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()){
		NotifyRectangleSelection();
		return;
	}

	char *input = (char *)NP2HeapAlloc(len + 1);
	SciCall_GetSelText(input);
	size_t outLen = (len*3)/4 + 4;
	uint8_t *output = (uint8_t *)NP2HeapAlloc(outLen);
	outLen = Base64Decode(output, (const uint8_t *)input, len);
	NP2HeapFree(input);
	if (outLen != 0) {
		if(decodeAsHex) {
			const int iEOLMode = SciCall_GetEOLMode();
			len = outLen*3 + outLen/8;
			input = (char *)NP2HeapAlloc(len + 1);
			char *t = input;
			size_t i = 0;
			do {
				const uint8_t c = output[i++];
				*t++ = "0123456789ABCDEF"[c >> 4];
				*t++ = "0123456789ABCDEF"[c & 15];
				*t++ = ' ';
				if ((i & 15) == 0) {
					--t;
					switch (iEOLMode) {
					default: // SC_EOL_CRLF
						*t++ = '\r';
						*t++ = '\n';
						break;
					case SC_EOL_LF:
						*t++ = '\n';
						break;
					case SC_EOL_CR:
						*t++ = '\r';
						break;
					}
				}
			} while (i < outLen);
			if ((i & 15) != 0) {
				--t;
			}
			outLen = t - input;
			NP2HeapFree(output);
			output = (uint8_t *)input;
		}
		EditReplaceMainSelection(outLen, (char *)output);
	}
	NP2HeapFree(output);
}

// indent function for json
static void append_indent(char *result, int number) {
	for (int i = 0; i < number * JSON_INDENT; ++i) {
		result[strlen(result)] = ' ';
	}
}

// format or compress json
void EditJSONFormat(bool isFormat) {
	const size_t len = SciCall_GetSelTextLength();
	if (len == 0) {
		return;
	}

	char *json = (char *)NP2HeapAlloc(len + 1);
	SciCall_GetSelBytes(json);

	if (isFormat) {
		long long length = strlen(json);
		bool is_escape_mode = false;
		int indent_number = 0;
		char current_char;
		char wrap_char = '\0';
		long long size = (length * 2) + 1;
		char *result = (char *)malloc(size);
		memset(result, 0, size);

		long long result_index = 0;

		for (long i = 0; i < length; i++) {
			current_char = json[i];

			if (wrap_char == '\0' &&
				(current_char == '\t' || current_char == '\n' || current_char == '\r' || current_char == ' ')) {
				continue;
			}
			if (wrap_char == '\0' && current_char == ':') {
				result[result_index++] = current_char;
				result[result_index++] = ' ';
				continue;
			}

			if (current_char == '"') {
				if (wrap_char == '\0') {
					wrap_char = current_char;
				} else if (is_escape_mode) {
					is_escape_mode = false;
				} else {
					wrap_char = '\0';
				}

				result[result_index++] = current_char;
				continue;
			}

			if (current_char == '\\') {
				if (wrap_char != '\0') {
					is_escape_mode = !is_escape_mode;
				}
				result[result_index++] = current_char;
				continue;
			}

			if (wrap_char != '\0') {
				result[result_index++] = current_char;
				continue;
			}

			if (current_char == '[' || current_char == '{') {
				result[result_index++] = current_char;
				result[result_index++] = '\n';
				indent_number++;
				append_indent(result + result_index, indent_number);
				result_index += indent_number * JSON_INDENT;
				continue;
			}

			if (current_char == ']' || current_char == '}') {
				result[result_index++] = '\n';
				indent_number--;
				append_indent(result + result_index, indent_number);
				result_index += indent_number * JSON_INDENT;
				result[result_index++] = current_char;
				continue;
			}

			if (current_char == ',') {
				result[result_index++] = current_char;
				result[result_index++] = '\n';
				append_indent(result + result_index, indent_number);
				result_index += indent_number * JSON_INDENT;
				continue;
			}

			result[result_index++] = current_char;

			if (result_index + 1 >= size) {
				size = result_index += size / 2;
				char *new_result = realloc(result, size);
				if (new_result == NULL) {
					fprintf(stderr, "Error: Failed to reallocate memory in format_json() loop.\n");
					free(result);
					return;
				} else {
					result = new_result;
				}
			}
		}

		char *new_result = realloc(result, result_index);
		if (new_result == NULL) {
			fprintf(stderr, "Error: Failed to reallocate memory in format_json().\n");
			free(result);
			return;
		} else {
			result[result_index] = '\0';
			result = new_result;
		}

		EditReplaceMainSelection(strlen(result), result);
		NP2HeapFree(json);
	} else {
		long long length = strlen(json);
		bool isWarp = false;
		char *result = (char *)malloc(length + 1); // 分配额外空间，同时留一个位置给字符串结束符'\0'
		memset(result, 0, length + 1);

		char current_char;
		long long result_index = 0;
		for (int i = 0; i < length; i++) {
			current_char = json[i];

			if (current_char == '"') {
				if (i > 0 && json[i - 1] != '\\' || (i > 1 && json[i - 2] == '\\')) {
					isWarp = !isWarp;
				}
			}

			if (!isWarp) {
				if (current_char == '\t' || current_char == '\n' || current_char == '\r' || current_char == ' ') {
					continue;
				}
			}

			result[result_index++] = current_char;
		}

		result[result_index] = '\0';
		char *new_result = realloc(result, result_index + 1);
		if (new_result == NULL) {
			fprintf(stderr, "Error: Failed to reallocate memory in compress_json().\n");
			free(result);
			return;
		} else {
			result = new_result;
		}

		EditReplaceMainSelection(strlen(result), result);
		NP2HeapFree(json);
	}
}

//=============================================================================
//
// EditConvertNumRadix()
//
static int ConvertNumRadix(char *tch, uint64_t num, int radix) {
	switch (radix) {
	case 16:
		return sprintf(tch, "0x%" PRIx64, num);

	case 10:
		return sprintf(tch, "%" PRIu64, num);

	case 8: {
		char buf[2 + 22 + 1] = "";
		int index = 2 + 22;
		int length = 0;
		while (num) {
			const int bit = (int)(num & 7);
			num >>= 3;
			buf[index--] = (char)('0' + bit);
			++length;
		}
		if (length == 0) {
			buf[index--] = '0';
			++length;
		}
		buf[index--] = 'O';
		buf[index] = '0';
		length += 2;
		strcat(tch, buf + index);
		return length;
	}
	break;

	case 2: {
		char buf[2 + 64 + 8 + 1] = "";
		int index = 2 + 64 + 8;
		int length = 0;
		int bit_count = 0;
		while (num) {
			const int bit = (int)(num & 1);
			num >>= 1;
			buf[index--] = (char)('0' + bit);
			++bit_count;
			++length;
			if (num && (bit_count & 7) == 0) {
				buf[index--] = '_';
				++length;
			}
		}
		if (length == 0) {
			buf[index--] = '0';
			++length;
		}
		buf[index--] = 'b';
		buf[index] = '0';
		length += 2;
		strcat(tch, buf + index);
		return length;
	}
	break;

	}
	return 0;
}

void EditConvertNumRadix(int radix) {
	const Sci_Position count = SciCall_GetSelTextLength();
	if (count == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	char *ch = (char *)NP2HeapAlloc(count + 1);
	char *tch = (char *)NP2HeapAlloc(2 + count * 4 + 8 + 1);
	Sci_Position cch = 0;
	char *p = ch;
	uint64_t value = 0;

	SciCall_GetSelText(ch);

	while (*p) {
		if (*p == '0') {
			value = 0;
			p++;
			const char prefix = UnsafeLower(*p);
			if (prefix == 'x' && radix != 16) {
				p++;
				while (*p) {
					if (*p == '_') {
						p++;
					} else {
						const int hex = GetHexDigit(*p);
						if (hex < 0) {
							break;
						}
						value = (value << 4) | hex;
						p++;
					}
				}
				cch += ConvertNumRadix(tch + cch, value, radix);
			} else if (prefix == 'o' && radix != 8) {
				p++;
				while (*p) {
					if (*p >= '0' && *p <= '7') {
						value <<= 3;
						value += (*p++ - '0');
					} else if (*p == '_') {
						p++;
					} else {
						break;
					}
				}
				cch += ConvertNumRadix(tch + cch, value, radix);
			} else if (prefix == 'b' && radix != 2) {
				p++;
				while (*p) {
					if (*p == '0') {
						value <<= 1;
						p++;
					} else if (*p == '1') {
						value <<= 1;
						value |= 1;
						p++;
					} else if (*p == '_') {
						p++;
					} else {
						break;
					}
				}
				cch += ConvertNumRadix(tch + cch, value, radix);
			} else if ((*p >= '0' && *p <= '9') && radix != 10) {
				value = *p++ - '0';
				while (*p) {
					if (*p >= '0' && *p <= '9') {
						value *= 10;
						value += (*p++ - '0');
					} else if (*p == '_') {
						p++;
					} else {
						break;
					}
				}
				cch += ConvertNumRadix(tch + cch, value, radix);
			} else {
				tch[cch++] = '0';
			}
		} else if ((*p >= '1' && *p <= '9') && radix != 10) {
			value = *p++ - '0';
			while (*p) {
				if (*p >= '0' && *p <= '9') {
					value *= 10;
					value += (*p++ - '0');
				} else if (*p == '_') {
					p++;
				} else {
					break;
				}
			}
			cch += ConvertNumRadix(tch + cch, value, radix);
		} else if (IsAlphaNumeric(*p) || *p == '_') {
			// radix and number prefix matches, no conversion
			tch[cch++] = *p++;
			while (IsAlphaNumeric(*p) || *p == '_') {
				tch[cch++] = *p++;
			}
		} else {
			tch[cch++] = *p++;
		}
	}
	tch[cch] = '\0';

	EditReplaceMainSelection(cch, tch);
	NP2HeapFree(ch);
	NP2HeapFree(tch);
}

//=============================================================================
//
// EditModifyNumber()
//
void EditModifyNumber(bool bIncrease) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	if (iSelCount < 32) {
		char chNumber[32] = "";
		SciCall_GetSelText(chNumber);

		const char *ptr = strpbrk(chNumber, "xX");
		const int radix = (ptr != NULL) ? 16 : 10;
		char *end;
		unsigned iNumber = strtoul(chNumber, &end, radix);
		if (end == chNumber) {
			return;
		}

		if (bIncrease && iNumber < UINT_MAX) {
			iNumber++;
		} else if (!bIncrease && iNumber > 0) {
			iNumber--;
		}

		int iWidth = (int)strlen(chNumber) - ((ptr != NULL) ? 2 : 0);
		if (ptr != NULL) {
			if (*ptr == 'X') {
				iWidth = sprintf(chNumber, "%#0*X", iWidth, iNumber);
			} else {
				iWidth = sprintf(chNumber, "%#0*x", iWidth, iNumber);
			}
		} else {
			iWidth = sprintf(chNumber, "%0*u", iWidth, iNumber);
		}
		EditReplaceMainSelection(iWidth, chNumber);
	}
}

//=============================================================================
//
// EditTabsToSpaces()
//
void EditTabsToSpaces(int nTabWidth, bool bOnlyIndentingWS) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	const Sci_Line iLine = SciCall_LineFromPosition(iSelStart);
	iSelStart = SciCall_PositionFromLine(iLine);

	const Sci_Position iSelCount = iSelEnd - iSelStart;
	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	const struct Sci_TextRangeFull tr = { { iSelStart, iSelEnd }, pszText};
	SciCall_GetTextRangeFull(&tr);

	const UINT cpEdit = SciCall_GetCodePage();
	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	NP2HeapFree(pszText);

	LPWSTR pszConvW = (LPWSTR)NP2HeapAlloc(cchTextW * sizeof(WCHAR) * nTabWidth + 2);
	int cchConvW = 0;

	bool bIsLineStart = true;
	bool bModified = false;
	// Contributed by Homam, Thank you very much!
	int i = 0;
	for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
		const WCHAR w = pszTextW[iTextW];
		if (w == L'\t' && (!bOnlyIndentingWS || bIsLineStart)) {
			for (int j = 0; j < nTabWidth - (i % nTabWidth); j++) {
				pszConvW[cchConvW++] = L' ';
			}
			i = 0;
			bModified = true;
		} else {
			i++;
			if (w == L'\n' || w == L'\r') {
				i = 0;
				bIsLineStart = true;
			} else if (w != L' ') {
				bIsLineStart = false;
			}
			pszConvW[cchConvW++] = w;
		}
	}

	NP2HeapFree(pszTextW);

	if (bModified) {
		pszText = (char *)NP2HeapAlloc(cchConvW * kMaxMultiByteCount);
		const int cchText = WideCharToMultiByte(cpEdit, 0, pszConvW, cchConvW, pszText, (int)NP2HeapSize(pszText), NULL, NULL);
		EditReplaceRange(iSelStart, iSelEnd, cchText, pszText);
		NP2HeapFree(pszText);
	}

	NP2HeapFree(pszConvW);
}

//=============================================================================
//
// EditSpacesToTabs()
//
void EditSpacesToTabs(int nTabWidth, bool bOnlyIndentingWS) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	const Sci_Line iLine = SciCall_LineFromPosition(iSelStart);
	iSelStart = SciCall_PositionFromLine(iLine);

	const Sci_Position iSelCount = iSelEnd - iSelStart;
	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	const struct Sci_TextRangeFull tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRangeFull(&tr);

	const UINT cpEdit = SciCall_GetCodePage();

	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	NP2HeapFree(pszText);

	LPWSTR pszConvW = (LPWSTR)NP2HeapAlloc(cchTextW * sizeof(WCHAR) + 2);
	int cchConvW = 0;

	bool bIsLineStart = true;
	bool bModified = false;
	// Contributed by Homam, Thank you very much!
	int i = 0;
	int j = 0;
	WCHAR space[256];
	for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
		const WCHAR w = pszTextW[iTextW];
		if ((w == L' ' || w == L'\t') && (!bOnlyIndentingWS || bIsLineStart)) {
			space[j++] = w;
			if (j == nTabWidth - (i % nTabWidth) || w == L'\t') {
				if (j > 1 || pszTextW[iTextW + 1] == L' ' || pszTextW[iTextW + 1] == L'\t') {
					pszConvW[cchConvW++] = L'\t';
				} else {
					pszConvW[cchConvW++] = w;
				}
				i = j = 0;
				bModified = bModified || (w != pszConvW[cchConvW - 1]);
			}
		} else {
			i += j + 1;
			if (j > 0) {
				//space[j] = '\0';
				for (int t = 0; t < j; t++) {
					pszConvW[cchConvW++] = space[t];
				}
				j = 0;
			}
			if (w == L'\n' || w == L'\r') {
				i = 0;
				bIsLineStart = true;
			} else {
				bIsLineStart = false;
			}
			pszConvW[cchConvW++] = w;
		}
	}

	if (j > 0) {
		for (int t = 0; t < j; t++) {
			pszConvW[cchConvW++] = space[t];
		}
	}

	NP2HeapFree(pszTextW);

	if (bModified || cchConvW != cchTextW) {
		pszText = (char *)NP2HeapAlloc(cchConvW * kMaxMultiByteCount + 1);
		const int cchText = WideCharToMultiByte(cpEdit, 0, pszConvW, cchConvW, pszText, (int)NP2HeapSize(pszText), NULL, NULL);
		EditReplaceRange(iSelStart, iSelEnd, cchText, pszText);
		NP2HeapFree(pszText);
	}

	NP2HeapFree(pszConvW);
}

//=============================================================================
//
// EditMoveUp()
//
void EditMoveUp(void) {
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();
	Sci_Line iCurLine = SciCall_LineFromPosition(iCurPos);
	Sci_Line iAnchorLine = SciCall_LineFromPosition(iAnchorPos);

	if (iCurLine == iAnchorLine) {
		if (iCurLine > 0) {
			const Sci_Position iLineStart = SciCall_PositionFromLine(iCurLine);
			const Sci_Position iLineCurPos = iCurPos - iLineStart;
			const Sci_Position iLineAnchorPos = iAnchorPos - iLineStart;
			SciCall_LineTranspose();
			const Sci_Position iLineDestStart = SciCall_PositionFromLine(iCurLine - 1);
			SciCall_SetSel(iLineDestStart + iLineAnchorPos, iLineDestStart + iLineCurPos);
			SciCall_ChooseCaretX();
		}
		return;
	}

	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	const Sci_Line iLineSrc = min_pos(iCurLine, iAnchorLine) - 1;
	if (iLineSrc >= 0) {
		Sci_Line iLineDest = max_pos(iCurLine, iAnchorLine);
		if (max_pos(iCurPos, iAnchorPos) <= SciCall_PositionFromLine(iLineDest)) {
			iLineDest--;
		}

		SciCall_MoveSelectedLinesUp();
		if (iCurPos < iAnchorPos) {
			iCurLine = iCurLine - 1;
			iAnchorLine = iLineDest;
		} else {
			iAnchorLine = iAnchorLine - 1;
			iCurLine = iLineDest;
		}

		iAnchorPos = SciCall_PositionFromLine(iAnchorLine);
		iCurPos = SciCall_PositionFromLine(iCurLine);
		SciCall_SetSel(iAnchorPos, iCurPos);
	}
}

//=============================================================================
//
// EditMoveDown()
//
void EditMoveDown(void) {
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();
	Sci_Line iCurLine = SciCall_LineFromPosition(iCurPos);
	Sci_Line iAnchorLine = SciCall_LineFromPosition(iAnchorPos);

	if (iCurLine == iAnchorLine) {
		iCurLine += 1;
		if (iCurLine < SciCall_GetLineCount()) {
			const Sci_Position iLineStart = SciCall_PositionFromLine(iAnchorLine);
			const Sci_Position iLineCurPos = iCurPos - iLineStart;
			const Sci_Position iLineAnchorPos = iAnchorPos - iLineStart;
			SciCall_GotoLine(iCurLine);
			SciCall_LineTranspose();
			const Sci_Position iLineDestStart = SciCall_PositionFromLine(iCurLine);
			SciCall_SetSel(iLineDestStart + iLineAnchorPos, iLineDestStart + iLineCurPos);
			SciCall_ChooseCaretX();
		}
		return;
	}

	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	Sci_Line iLineSrc = max_pos(iCurLine, iAnchorLine) + 1;
	if (max_pos(iCurPos, iAnchorPos) <= SciCall_PositionFromLine(iLineSrc - 1)) {
		iLineSrc--;
	}

	const Sci_Line iLineEnd = SciCall_GetLineCount() - 1;
	if (iLineSrc <= iLineEnd) {
		const bool bLastLine = (iLineSrc == iLineEnd);

		if (bLastLine &&
				(SciCall_GetLineEndPosition(iLineSrc) == SciCall_PositionFromLine(iLineSrc)) &&
				(SciCall_GetLineEndPosition(iLineSrc - 1) == SciCall_PositionFromLine(iLineSrc - 1))) {
			return;
		}

		if (bLastLine) {
			SciCall_BeginUndoAction();
			const int iEOLMode = SciCall_GetEOLMode();
			LPCSTR lineEnd = (iEOLMode == SC_EOL_LF) ? "\n" : "\r\n";
			SciCall_AppendText((iEOLMode == SC_EOL_CRLF) ? 2 : 1, lineEnd);
		}

		SciCall_MoveSelectedLinesDown();

		if (bLastLine) {
			const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(SciCall_GetLineCount() - 2);
			SciCall_DeleteRange(iLineEndPos, SciCall_GetLength() - iLineEndPos);
			SciCall_EndUndoAction();
		}

		if (iCurPos < iAnchorPos) {
			iCurLine = iCurLine + 1;
			iAnchorLine = iLineSrc + 1;
		} else {
			iAnchorLine = iAnchorLine + 1;
			iCurLine = iLineSrc + 1;
		}

		iAnchorPos = SciCall_PositionFromLine(iAnchorLine);
		iCurPos = SciCall_PositionFromLine(iCurLine);
		SciCall_SetSel(iAnchorPos, iCurPos);
	}
}

// only convert CR+LF
static void ConvertWinEditLineEndingsEx(char *s, int iEOLMode, int *lineCount) {
	int count = 0;
	if (iEOLMode != SC_EOL_CRLF) {
		char *p = s;
		const char chaEOL = (iEOLMode == SC_EOL_LF) ? '\n' : '\r';
		while (*s) {
			switch (*s) {
			case '\r':
				++count;
				if (s[1] == '\n') {
					++s;
					*p++ = chaEOL;
				} else {
					*p++ = '\r';
				}
				++s;
				break;

			case '\n':
				++count;
				*p++ = '\n';
				++s;
				break;

			default:
				*p++ = *s++;
				break;
			}
		}

		*p = '\0';
		if (lineCount != NULL) {
			*lineCount = count;
		}
	} else if (lineCount != NULL) {
		while (*s) {
			switch (*s++) {
			case '\r':
				++count;
				if (*s == '\n') {
					++s;
				}
				break;

			case '\n':
				++count;
				break;
			}
		}
		*lineCount = count;
	}
}

static inline void ConvertWinEditLineEndings(char *s, int iEOLMode) {
	ConvertWinEditLineEndingsEx(s, iEOLMode, NULL);
}

enum {
	EditModifyLinesSubstitution_None = '\0',
	EditModifyLinesSubstitution_LineNumber = 'L',
	EditModifyLinesSubstitution_NumberOne = 'N',
	EditModifyLinesSubstitution_NumberZero = 'I',
};

typedef struct EditModifyLinesText {
	int length;
	int lineCount;
	uint8_t substitution;
	bool padZero;
	int numWidth;
	Sci_Line number;
	char *mszPrefix;
	const char *mszSuffix;
} EditModifyLinesText;

static void EditModifyLinesText_Parse(EditModifyLinesText *text, LPCWSTR pszTextW, Sci_Line iLineStart, Sci_Line iLineEnd, UINT cpEdit, int iEOLMode) {
	memset(text, 0, sizeof(EditModifyLinesText));
	int length = lstrlen(pszTextW);
	if (length == 0) {
		return;
	}

	length = length * kMaxMultiByteCount + 1;
	char *mszPrefix = (char *)NP2HeapAlloc(length);
	WideCharToMultiByte(cpEdit, 0, pszTextW, -1, mszPrefix, length, NULL, NULL);
	ConvertWinEditLineEndingsEx(mszPrefix, iEOLMode, &(text->lineCount));

	text->length = length;
	text->mszPrefix = mszPrefix;
	char *p = mszPrefix;
	while ((p = strstr(p, "$(")) != NULL) {
		char * const back = p;
		p += CSTRLEN("$(");
		const bool padZero = *p == '0';
		if (padZero) {
			p++;
		}
		if ((*p == 'L' || *p == 'N' || *p == 'I') && p[1] == ')') {
			*back = '\0';
			Sci_Line number = 0;
			Sci_Line lineCount;
			const uint8_t substitution = *p;
			if (substitution == EditModifyLinesSubstitution_LineNumber) {
				lineCount = iLineEnd + 1;
			} else {
				number = substitution == EditModifyLinesSubstitution_NumberOne;
				lineCount = iLineEnd - iLineStart + number;
			}
			int numWidth = 1;
			while (lineCount >= 10) {
				++numWidth;
				lineCount /= 10;
			}

			text->substitution = substitution;
			text->padZero = padZero;
			text->numWidth = numWidth;
			text->number = number;
			text->mszSuffix = p + 2;
			break;
		}
	}
}

static void EditModifyLinesText_Insert(EditModifyLinesText *text, char *mszInsert, Sci_Line iLine, Sci_Position position) {
	strcpy(mszInsert, text->mszPrefix);
	if (text->substitution != EditModifyLinesSubstitution_None) {
		char tchNum[64];
		const int numWidth = text->numWidth;
		const Sci_Line number = (text->substitution == EditModifyLinesSubstitution_LineNumber) ? iLine + 1 : text->number;
#if defined(_WIN64)
		if (text->padZero) {
			sprintf(tchNum, "%0*" PRId64, numWidth, number);
		} else {
			sprintf(tchNum, "%*" PRId64, numWidth, number);
		}
#else
		if (text->padZero) {
			sprintf(tchNum, "%0*d", numWidth, (int)(number));
		} else {
			sprintf(tchNum, "%*d", numWidth, (int)(number));
		}
#endif
		strcat(mszInsert, tchNum);
		strcat(mszInsert, text->mszSuffix);
		text->number++;
	}

	SciCall_SetTargetRange(position, position);
	SciCall_ReplaceTarget(-1, mszInsert);
}

//=============================================================================
//
// EditModifyLines()
//
void EditModifyLines(LPCWSTR pwszPrefix, LPCWSTR pwszAppend, bool skipEmptyLine) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}
	if (StrIsEmpty(pwszPrefix) && StrIsEmpty(pwszAppend)) {
		return;
	}

	BeginWaitCursor();

	const UINT cpEdit = SciCall_GetCodePage();
	const int iEOLMode = SciCall_GetEOLMode();
	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	const Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
	Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);

	//if (iSelStart > SciCall_PositionFromLine(iLineStart)) {
	//	iLineStart++;
	//}

	if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
		if (iLineEnd - iLineStart >= 1) {
			iLineEnd--;
		}
	}

	EditModifyLinesText prefix;
	EditModifyLinesText suffix;
	EditModifyLinesText_Parse(&prefix, pwszPrefix, iLineStart, iLineEnd, cpEdit, iEOLMode);
	EditModifyLinesText_Parse(&suffix, pwszAppend, iLineStart, iLineEnd, cpEdit, iEOLMode);

	char *mszInsert = (char *)NP2HeapAlloc(prefix.length + suffix.length + 64);
	SciCall_BeginUndoAction();
	for (Sci_Line iLine = iLineStart, iLineDest = iLineStart; iLine <= iLineEnd; iLine++, iLineDest++) {
		const Sci_Position iStartPos = SciCall_PositionFromLine(iLineDest);
		Sci_Position iEndPos = SciCall_GetLineEndPosition(iLineDest);
		if (skipEmptyLine && iStartPos == iEndPos) {
			continue;
		}
		if (prefix.length != 0) {
			iLineDest += prefix.lineCount;
			EditModifyLinesText_Insert(&prefix, mszInsert, iLine, iStartPos);
		}
		if (suffix.length != 0) {
			if (prefix.length != 0) {
				iEndPos = SciCall_GetLineEndPosition(iLineDest);
			}
			iLineDest += suffix.lineCount;
			EditModifyLinesText_Insert(&suffix, mszInsert, iLine, iEndPos);
		}
	}
	SciCall_EndUndoAction();

	//// Fix selection
	//if (iSelStart != iSelEnd && SciCall_GetTargetEnd() > SciCall_GetSelectionEnd()) {
	//	Sci_Position iCurPos = SciCall_GetCurrentPos();
	//	Sci_Position iAnchorPos = SciCall_GetAnchor();
	//	Sci_Position iTargetEnd = SciCall_GetTargetEnd();
	//	if (iCurPos > iAnchorPos) {
	//		iCurPos = iTargetEnd;
	//	} else {
	//		iAnchorPos = iTargetEnd;
	//	}
	//	SciCall_SetSel(iAnchorPos, iCurPos);
	//}

	// extend selection to start of first line
	// the above code is not required when last line has been excluded
	if (iSelStart != iSelEnd) {
		Sci_Position iCurPos = SciCall_GetCurrentPos();
		Sci_Position iAnchorPos = SciCall_GetAnchor();
		if (iCurPos < iAnchorPos) {
			iCurPos = iLineStart;
			iAnchorPos = iLineEnd + 1;
		} else {
			iAnchorPos = iLineStart;
			iCurPos = iLineEnd + 1;
		}
		iAnchorPos = SciCall_PositionFromLine(iAnchorPos);
		iCurPos = SciCall_PositionFromLine(iCurPos);
		SciCall_SetSel(iAnchorPos, iCurPos);
	}

	EndWaitCursor();
	NP2HeapFree(prefix.mszPrefix);
	NP2HeapFree(suffix.mszPrefix);
	NP2HeapFree(mszInsert);
}

//=============================================================================
//
// EditAlignText()
//
void EditAlignText(EditAlignMode nMode) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

#define BUFSIZE_ALIGN 1024

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();
	const UINT cpEdit = SciCall_GetCodePage();

	const Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
	Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);

	if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
		if (iLineEnd - iLineStart >= 1) {
			iLineEnd--;
		}
	}

	Sci_Position iMinIndent = BUFSIZE_ALIGN;
	Sci_Position iMaxLength = 0;
	for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
		Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iLine);
		const Sci_Position iLineIndentPos = SciCall_GetLineIndentPosition(iLine);

		if (iLineIndentPos < iLineEndPos) {
			while (iLineEndPos >= iLineIndentPos) {
				iLineEndPos--;
				const int ch = SciCall_GetCharAt(iLineEndPos);
				if (!IsASpaceOrTab(ch)) {
					break;
				}
			}

			++iLineEndPos;
			const Sci_Position iEndCol = SciCall_GetColumn(iLineEndPos);
			const Sci_Position iIndentCol = SciCall_GetLineIndentation(iLine);
			iMinIndent = min_pos(iMinIndent, iIndentCol);
			iMaxLength = max_pos(iMaxLength, iEndCol);
		}
	}

	if (iMaxLength < BUFSIZE_ALIGN) {
		typedef struct EditAlignTextVar {
			char tchLineBuf[BUFSIZE_ALIGN * kMaxMultiByteCount];
			WCHAR wchLineBuf[BUFSIZE_ALIGN];
			LPWSTR pWords[BUFSIZE_ALIGN];
			WCHAR wchNewLineBuf[BUFSIZE_ALIGN * 3];
		} EditAlignTextVar;
		EditAlignTextVar * const var = (EditAlignTextVar *)NP2HeapAlloc(sizeof(EditAlignTextVar));
		SciCall_BeginUndoAction();
		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iIndentPos = SciCall_GetLineIndentPosition(iLine);
			const Sci_Position iEndPos = SciCall_GetLineEndPosition(iLine);

			if (iIndentPos == iEndPos && iEndPos > 0) {
				const Sci_Position iStartPos = SciCall_PositionFromLine(iLine);
				SciCall_DeleteRange(iStartPos, iEndPos - iStartPos);
			} else {
				char* const tchLineBuf = var->tchLineBuf;
				WCHAR* const wchLineBuf = var->wchLineBuf;
				LPWSTR* const pWords = var->pWords;
				WCHAR * const wchNewLineBuf = var->wchNewLineBuf;
				WCHAR *p = wchLineBuf;

				Sci_Position iWordsLength = 0;
				const Sci_Position cchLine = SciCall_GetLine(iLine, tchLineBuf);

				int iWords = MultiByteToWideChar(cpEdit, 0, tchLineBuf, (int)cchLine, wchLineBuf, COUNTOF(var->wchLineBuf));
				wchLineBuf[iWords] = L'\0';
				iWords = 0;
				StrTrim(wchLineBuf, L"\r\n\t ");

				while (*p) {
					if (*p != L' ' && *p != L'\t') {
						pWords[iWords++] = p++;
						iWordsLength++;
						while (*p && *p != L' ' && *p != L'\t') {
							p++;
							iWordsLength++;
						}
					} else {
						*p++ = L'\0';
					}
				}

				if (iWords > 0) {
					if (nMode == EditAlignMode_Justify || nMode == EditAlignMode_JustifyEx) {
						bool bNextLineIsBlank = false;
						if (nMode == EditAlignMode_JustifyEx) {
							if (SciCall_GetLineCount() <= iLine + 1) {
								bNextLineIsBlank = true;
							} else {
								const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iLine + 1);
								const Sci_Position iLineIndentPos = SciCall_GetLineIndentPosition(iLine + 1);
								if (iLineIndentPos == iLineEndPos) {
									bNextLineIsBlank = true;
								}
							}
						}

						if ((nMode == EditAlignMode_Justify || nMode == EditAlignMode_JustifyEx) &&
								iWords > 1 && iWordsLength >= 2 &&
								((nMode != EditAlignMode_JustifyEx || !bNextLineIsBlank || iLineStart == iLineEnd) ||
								 (bNextLineIsBlank && iWordsLength*4 > (iMaxLength - iMinIndent)*3))) {
							const int iGaps = iWords - 1;
							const Sci_Position iSpacesPerGap = (iMaxLength - iMinIndent - iWordsLength) / iGaps;
							const Sci_Position iExtraSpaces = (iMaxLength - iMinIndent - iWordsLength) % iGaps;

							lstrcpy(wchNewLineBuf, pWords[0]);
							p = StrEnd(wchNewLineBuf);

							for (int i = 1; i < iWords; i++) {
								for (Sci_Position j = 0; j < iSpacesPerGap; j++) {
									*p++ = L' ';
								}
								if (i > iGaps - iExtraSpaces) {
									*p++ = L' ';
								}
								*p = L'\0';
								lstrcat(p, pWords[i]);
								p = StrEnd(p);
							}

							WideCharToMultiByte(cpEdit, 0, wchNewLineBuf, -1, tchLineBuf, COUNTOF(var->tchLineBuf), NULL, NULL);

							SciCall_SetTargetRange(SciCall_PositionFromLine(iLine), SciCall_GetLineEndPosition(iLine));
							SciCall_ReplaceTarget(-1, tchLineBuf);
							SciCall_SetLineIndentation(iLine, iMinIndent);
						} else {
							lstrcpy(wchNewLineBuf, pWords[0]);
							p = StrEnd(wchNewLineBuf);

							for (int i = 1; i < iWords; i++) {
								*p++ = L' ';
								*p = L'\0';
								lstrcat(wchNewLineBuf, pWords[i]);
								p = StrEnd(p);
							}

							WideCharToMultiByte(cpEdit, 0, wchNewLineBuf, -1, tchLineBuf, COUNTOF(var->tchLineBuf), NULL, NULL);

							SciCall_SetTargetRange(SciCall_PositionFromLine(iLine), SciCall_GetLineEndPosition(iLine));
							SciCall_ReplaceTarget(-1, tchLineBuf);
							SciCall_SetLineIndentation(iLine, iMinIndent);
						}
					} else {
						const Sci_Position iExtraSpaces = iMaxLength - iMinIndent - iWordsLength - iWords + 1;
						Sci_Position iOddSpaces = iExtraSpaces % 2;

						p = wchNewLineBuf;
						if (nMode == EditAlignMode_Right) {
							for (Sci_Position i = 0; i < iExtraSpaces; i++) {
								*p++ = L' ';
							}
						}

						if (nMode == EditAlignMode_Center) {
							for (Sci_Position i = 1; i < iExtraSpaces - iOddSpaces; i += 2) {
								*p++ = L' ';
							}
						}

						*p = L'\0';
						for (int i = 0; i < iWords; i++) {
							lstrcat(p, pWords[i]);
							if (i < iWords - 1) {
								lstrcat(p, L" ");
							}
							if (nMode == EditAlignMode_Center && iWords > 1 && iOddSpaces > 0 && i + 1 >= iWords / 2) {
								lstrcat(p, L" ");
								iOddSpaces--;
							}
							p = StrEnd(p);
						}

						WideCharToMultiByte(cpEdit, 0, wchNewLineBuf, -1, tchLineBuf, COUNTOF(var->tchLineBuf), NULL, NULL);

						Sci_Position iPos;
						if (nMode == EditAlignMode_Right || nMode == EditAlignMode_Center) {
							SciCall_SetLineIndentation(iLine, iMinIndent);
							iPos = SciCall_GetLineIndentPosition(iLine);
						} else {
							iPos = SciCall_PositionFromLine(iLine);
						}

						SciCall_SetTargetRange(iPos, SciCall_GetLineEndPosition(iLine));
						SciCall_ReplaceTarget(-1, tchLineBuf);
						if (nMode == EditAlignMode_Left) {
							SciCall_SetLineIndentation(iLine, iMinIndent);
						}
					}
				}
			}
		}
		SciCall_EndUndoAction();
		NP2HeapFree(var);
	} else {
		MsgBoxInfo(MB_OK, IDS_BUFFERTOOSMALL);
	}

	if (iCurPos < iAnchorPos) {
		iCurPos = iLineStart;
		iAnchorPos = iLineEnd + 1;
	} else {
		iAnchorPos = iLineStart;
		iCurPos = iLineEnd + 1;
	}
	iAnchorPos = SciCall_PositionFromLine(iAnchorPos);
	iCurPos = SciCall_PositionFromLine(iCurPos);
	SciCall_SetSel(iAnchorPos, iCurPos);
}

//=============================================================================
//
// EditEncloseSelection()
//
void EditEncloseSelection(LPCWSTR pwszOpen, LPCWSTR pwszClose) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}
	if (StrIsEmpty(pwszOpen) && StrIsEmpty(pwszClose)) {
		return;
	}

	BeginWaitCursor();

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	const UINT cpEdit = SciCall_GetCodePage();
	const int iEOLMode = SciCall_GetEOLMode();

	char *mszOpen = NULL;
	int len = lstrlen(pwszOpen);
	if (len != 0) {
		const int size = kMaxMultiByteCount * len + 1;
		mszOpen = (char *)NP2HeapAlloc(size);
		WideCharToMultiByte(cpEdit, 0, pwszOpen, -1, mszOpen, size, NULL, NULL);
		ConvertWinEditLineEndings(mszOpen, iEOLMode);
	}

	char *mszClose = NULL;
	len = lstrlen(pwszClose);
	if (len != 0) {
		const int size = kMaxMultiByteCount * len + 1;
		mszClose = (char *)NP2HeapAlloc(size);
		WideCharToMultiByte(cpEdit, 0, pwszClose, -1, mszClose, size, NULL, NULL);
		ConvertWinEditLineEndings(mszClose, iEOLMode);
	}

	SciCall_BeginUndoAction();
	len = 0;

	if (StrNotEmptyA(mszOpen)) {
		len = (int)strlen(mszOpen);
		SciCall_SetTargetRange(iSelStart, iSelStart);
		SciCall_ReplaceTarget(len, mszOpen);
	}

	if (StrNotEmptyA(mszClose)) {
		SciCall_SetTargetRange(iSelEnd + len, iSelEnd + len);
		SciCall_ReplaceTarget(-1, mszClose);
	}

	SciCall_EndUndoAction();

	// Fix selection
	if (iSelStart == iSelEnd) {
		SciCall_SetSel(iSelStart + len, iSelStart + len);
	} else {
		Sci_Position iCurPos = SciCall_GetCurrentPos();
		Sci_Position iAnchorPos = SciCall_GetAnchor();

		if (iCurPos < iAnchorPos) {
			iCurPos = iSelStart + len;
			iAnchorPos = iSelEnd + len;
		} else {
			iAnchorPos = iSelStart + len;
			iCurPos = iSelEnd + len;
		}
		SciCall_SetSel(iAnchorPos, iCurPos);
	}

	EndWaitCursor();
	if (mszOpen != NULL) {
		NP2HeapFree(mszOpen);
	}
	if (mszClose != NULL) {
		NP2HeapFree(mszClose);
	}
}

//=============================================================================
//
// EditToggleLineComments()
//
void EditToggleLineComments(LPCWSTR pwszComment, int commentFlag) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	BeginWaitCursor();

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	Sci_Position iCurPos = SciCall_GetCurrentPos();

	char mszComment[32] = "";
	const UINT cpEdit = SciCall_GetCodePage();
	int cchComment = WideCharToMultiByte(cpEdit, 0, pwszComment, -1, mszComment, COUNTOF(mszComment), NULL, NULL) - 1;
	const char commentEnd = mszComment[cchComment - 1];
	char commentPad = ' ';
	if (commentEnd == ' ') {
		cchComment -= 1;
	} else if ((commentFlag & AutoInsertMask_SpaceAfterComment) == 0) {
		commentPad = '\0';
	}

	const Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
	Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);

	if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
		if (iLineEnd - iLineStart >= 1) {
			iLineEnd--;
		}
	}

	Sci_Position iCommentCol = 0;
	if ((commentFlag & AutoInsertMask_CommentAtStart) == 0) {
		iCommentCol = 1024 - 1 - cchComment;
		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iLine);
			const Sci_Position iLineIndentPos = SciCall_GetLineIndentPosition(iLine);

			if (iLineIndentPos != iLineEndPos) {
				const Sci_Position iIndentColumn = SciCall_GetColumn(iLineIndentPos);
				iCommentCol = min_pos(iCommentCol, iIndentColumn);
			}
		}
	}

	enum CommentAction {
		CommentAction_None,
		CommentAction_Add,
		CommentAction_Delete,
	} iAction = CommentAction_None;

	SciCall_BeginUndoAction();
	for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
		const Sci_Position iIndentPos = SciCall_GetLineIndentPosition(iLine);
		const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iLine);

		char tchBuf[32] = "";
		const struct Sci_TextRangeFull tr = { { iIndentPos, min_pos(iIndentPos + 31, iLineEndPos) }, tchBuf };
		SciCall_GetTextRangeFull(&tr);

		if (StrStartsWithCaseEx(tchBuf, mszComment, cchComment) && (commentEnd != ' ' || (uint8_t)(tchBuf[cchComment]) <= ' ')) {
			switch (iAction) {
			case CommentAction_None:
				iAction = CommentAction_Delete;
				FALLTHROUGH_ATTR;
				// fall through
			case CommentAction_Delete: {
				Sci_Position iCommentPos = iIndentPos;
				Sci_Position iEndPos = iIndentPos + cchComment;
				// a line with [space/tab] comment only
				if (commentPad == ' ' && tchBuf[cchComment] == ' ') {
					// TODO: detect indentation space and alignment space
					++iEndPos;
				}
				if (iEndPos == iLineEndPos) {
					iCommentPos = SciCall_PositionFromLine(iLine);
				}
				SciCall_DeleteRange(iCommentPos, iEndPos - iCommentPos);
			} break;
			case CommentAction_Add: {
				const Sci_Position iCommentPos = SciCall_FindColumn(iLine, iCommentCol);
				if (iCommentPos != iIndentPos) {
					mszComment[cchComment] = commentPad;
					SciCall_InsertText(iCommentPos, mszComment);
				}
			} break;
			}
		} else {
			switch (iAction) {
			case CommentAction_None:
				iAction = CommentAction_Add;
				FALLTHROUGH_ATTR;
				// fall through
			case CommentAction_Add: {
				const Sci_Position iCommentPos = SciCall_FindColumn(iLine, iCommentCol);
				if (iCommentCol == 0 || iLineStart == iLineEnd || iIndentPos != iLineEndPos) {
					mszComment[cchComment] = (iCommentPos == iLineEndPos) ? '\0' : commentPad;
					SciCall_InsertText(iCommentPos, mszComment);
				} else {
					char tchComment[1024] = "";
					Sci_Position tab = 0;
					Sci_Position count = iCommentCol;
					if (!fvCurFile.bTabsAsSpaces) {
						const int tabWidth = fvCurFile.iTabWidth;
						tab = count / tabWidth;
						count %= tabWidth;
						memset(tchComment, '\t', tab);
					}
					memset(tchComment + tab, ' ', count);
					memcpy(tchComment + tab + count, mszComment, cchComment);
					SciCall_InsertText(iCommentPos, tchComment);
				}
			} break;
			case CommentAction_Delete:
				break;
			}
		}
	}

	SciCall_EndUndoAction();
	if (iSelStart != iSelEnd) {
		Sci_Position iAnchorPos;
		if (iCurPos == iSelStart) {
			iCurPos = iLineStart;
			iAnchorPos = iLineEnd + 1;
		} else {
			iAnchorPos = iLineStart;
			iCurPos = iLineEnd + 1;
		}
		iAnchorPos = SciCall_PositionFromLine(iAnchorPos);
		iCurPos = SciCall_PositionFromLine(iCurPos);
		SciCall_SetSel(iAnchorPos, iCurPos);
	}

	EndWaitCursor();
}

//=============================================================================
//
// EditPadWithSpaces()
//
void EditPadWithSpaces(bool bSkipEmpty, bool bNoUndoGroup) {
	Sci_Position iMaxColumn = 0;
	bool bReducedSelection = false;

	Sci_Position iSelStart = 0;
	Sci_Position iSelEnd = 0;

	Sci_Line iLineStart = 0;
	Sci_Line iLineEnd = 0;

	Sci_Line iRcCurLine = 0;
	Sci_Line iRcAnchorLine = 0;
	Sci_Position iRcCurCol = 0;
	Sci_Position iRcAnchorCol = 0;

	const bool bIsRectangular = SciCall_IsRectangleSelection();
	if (!bIsRectangular ) {
		iSelStart = SciCall_GetSelectionStart();
		iSelEnd = SciCall_GetSelectionEnd();

		iLineStart = SciCall_LineFromPosition(iSelStart);
		iLineEnd = SciCall_LineFromPosition(iSelEnd);

		if (iLineStart == iLineEnd) {
			iLineStart = 0;
			iLineEnd = SciCall_GetLineCount() - 1;
		} else {
			if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
				if (iLineEnd - iLineStart >= 1) {
					iLineEnd--;
					bReducedSelection = true;
				}
			}
		}

		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iPos = SciCall_GetLineEndPosition(iLine);
			iMaxColumn = max_pos(iMaxColumn, SciCall_GetColumn(iPos));
		}
	} else {
		const Sci_Position iCurPos = SciCall_GetCurrentPos();
		const Sci_Position iAnchorPos = SciCall_GetAnchor();

		iRcCurLine = SciCall_LineFromPosition(iCurPos);
		iRcAnchorLine = SciCall_LineFromPosition(iAnchorPos);

		iRcCurCol = SciCall_GetColumn(iCurPos);
		iRcAnchorCol = SciCall_GetColumn(iAnchorPos);

		iLineStart = 0;
		iLineEnd = SciCall_GetLineCount() - 1;

		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iPos = SciCall_GetLineSelEndPosition(iLine);
			if (iPos >= 0) {
				iMaxColumn = max_pos(iMaxColumn, SciCall_GetColumn(iPos));
			}
		}
	}

	char *pmszPadStr = (char *)NP2HeapAlloc((iMaxColumn + 1) * sizeof(char));
	if (pmszPadStr) {
		memset(pmszPadStr, ' ', iMaxColumn);
		if (!bNoUndoGroup) {
			SciCall_BeginUndoAction();
		}

		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iLineSelEndPos = SciCall_GetLineSelEndPosition(iLine);
			if (bIsRectangular && iLineSelEndPos < 0) {
				continue;
			}

			const Sci_Position iPos = SciCall_GetLineEndPosition(iLine);
			if (bIsRectangular && iPos > iLineSelEndPos) {
				continue;
			}

			if (bSkipEmpty && SciCall_PositionFromLine(iLine) >= iPos) {
				continue;
			}

			const Sci_Position iPadLen = iMaxColumn - SciCall_GetColumn(iPos);

			SciCall_SetTargetRange(iPos, iPos);
			SciCall_ReplaceTarget(iPadLen, pmszPadStr);
		}

		NP2HeapFree(pmszPadStr);
		if (!bNoUndoGroup) {
			SciCall_EndUndoAction();
		}
	}

	if (!bIsRectangular && SciCall_LineFromPosition(iSelStart) != SciCall_LineFromPosition(iSelEnd)) {
		Sci_Position iCurPos = SciCall_GetCurrentPos();
		Sci_Position iAnchorPos = SciCall_GetAnchor();
		iRcCurCol = SciCall_PositionFromLine(iLineStart);
		if (!bReducedSelection) {
			iRcAnchorCol = SciCall_GetLineEndPosition(iLineEnd);
		} else {
			iRcAnchorCol = SciCall_PositionFromLine(iLineEnd + 1);
		}
		if (iCurPos < iAnchorPos) {
			iCurPos = iRcCurCol;
			iAnchorPos = iRcAnchorCol;
		} else {
			iAnchorPos = iRcCurCol;
			iCurPos = iRcAnchorCol;
		}
		SciCall_SetSel(iAnchorPos, iCurPos);
	} else if (bIsRectangular) {
		const Sci_Position iCurPos = SciCall_FindColumn(iRcCurLine, iRcCurCol);
		const Sci_Position iAnchorPos = SciCall_FindColumn(iRcAnchorLine, iRcAnchorCol);

		SciCall_SetRectangularSelectionCaret(iCurPos);
		SciCall_SetRectangularSelectionAnchor(iAnchorPos);
	}
}

//=============================================================================
//
// EditStripFirstCharacter()
//
void EditStripFirstCharacter(void) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
	Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);

	if (iLineStart != iLineEnd) {
		if (iSelStart > SciCall_PositionFromLine(iLineStart)) {
			iLineStart++;
		}

		if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
			iLineEnd--;
		}
	}

	SciCall_BeginUndoAction();
	for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
		const Sci_Position iPos = SciCall_PositionFromLine(iLine);
		if (SciCall_GetLineEndPosition(iLine) > iPos) {
			SciCall_DeleteRange(iPos, SciCall_PositionAfter(iPos) - iPos);
		}
	}
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditStripLastCharacter()
//
void EditStripLastCharacter(void) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
	Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);

	if (iLineStart != iLineEnd) {
		if (iSelStart >= SciCall_GetLineEndPosition(iLineStart)) {
			iLineStart++;
		}

		if (iSelEnd < SciCall_GetLineEndPosition(iLineEnd)) {
			iLineEnd--;
		}
	}

	SciCall_BeginUndoAction();
	for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
		const Sci_Position iStartPos = SciCall_PositionFromLine(iLine);
		const Sci_Position iEndPos = SciCall_GetLineEndPosition(iLine);

		if (iEndPos > iStartPos) {
			const Sci_Position iPos = SciCall_PositionBefore(iEndPos);
			SciCall_DeleteRange(iPos, iEndPos - iPos);
		}
	}
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditStripTrailingBlanks()
//
void EditStripTrailingBlanks(HWND hwnd, bool bIgnoreSelection) {
	// Check if there is any selection... simply use a regular expression replace!
	if (!bIgnoreSelection && !SciCall_IsSelectionEmpty()) {
		if (!SciCall_IsRectangleSelection()) {
			EDITFINDREPLACE efrTrim;
			memset(&efrTrim, 0, sizeof(efrTrim));
			memcpy(efrTrim.szFind, "[ \t]+$", CSTRLEN("[ \t]+$"));
			efrTrim.hwnd = hwnd;
			efrTrim.fuFlags = SCFIND_REGEXP;
			if (EditReplaceAllInSelection(hwnd, &efrTrim, false)) {
				return;
			}
		}
	}

	// Code from SciTE...
	SciCall_BeginUndoAction();
	const Sci_Line maxLines = SciCall_GetLineCount();
	for (Sci_Line line = 0; line < maxLines; line++) {
		const Sci_Position lineStart = SciCall_PositionFromLine(line);
		const Sci_Position lineEnd = SciCall_GetLineEndPosition(line);
		Sci_Position i = lineEnd;
		while (i >= lineStart) {
			i--;
			const int ch = SciCall_GetCharAt(i);
			if (!IsASpaceOrTab(ch)) {
				break;
			}
		}
		i++;
		if (i < lineEnd) {
			SciCall_DeleteRange(i, lineEnd - i);
		}
	}
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditStripLeadingBlanks()
//
void EditStripLeadingBlanks(HWND hwnd, bool bIgnoreSelection) {
	// Check if there is any selection... simply use a regular expression replace!
	if (!bIgnoreSelection && !SciCall_IsSelectionEmpty()) {
		if (!SciCall_IsRectangleSelection()) {
			EDITFINDREPLACE efrTrim;
			memset(&efrTrim, 0, sizeof(efrTrim));
			memcpy(efrTrim.szFind, "^[ \t]+", CSTRLEN("^[ \t]+"));
			efrTrim.hwnd = hwnd;
			efrTrim.fuFlags = SCFIND_REGEXP;
			if (EditReplaceAllInSelection(hwnd, &efrTrim, false)) {
				return;
			}
		}
	}

	// Code from SciTE...
	SciCall_BeginUndoAction();
	const Sci_Line maxLines = SciCall_GetLineCount();
	for (Sci_Line line = 0; line < maxLines; line++) {
		const Sci_Position lineStart = SciCall_PositionFromLine(line);
		const Sci_Position lineEnd = SciCall_GetLineIndentPosition(line);
		if (lineEnd > lineStart) {
			SciCall_DeleteRange(lineStart, lineEnd - lineStart);
		}
	}
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditCompressSpaces()
//
void EditCompressSpaces(void) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();

	char *pszIn;
	char *pszOut;
	bool bIsLineStart;
	bool bIsLineEnd;

	if (iSelStart != iSelEnd) {
		const Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
		const Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);
		const Sci_Position cch = SciCall_GetSelTextLength() + 1;
		pszIn = (char *)NP2HeapAlloc(cch);
		pszOut = (char *)NP2HeapAlloc(cch);
		SciCall_GetSelText(pszIn);
		bIsLineStart = (iSelStart == SciCall_PositionFromLine(iLineStart));
		bIsLineEnd = (iSelEnd == SciCall_GetLineEndPosition(iLineEnd));
	} else {
		const Sci_Position cch = SciCall_GetLength() + 1;
		pszIn = (char *)NP2HeapAlloc(cch);
		pszOut = (char *)NP2HeapAlloc(cch);
		SciCall_GetText(cch, pszIn);
		bIsLineStart = true;
		bIsLineEnd = true;
	}

	bool bModified = false;
	char *ci;
	char *co = pszOut;
	for (ci = pszIn; *ci; ci++) {
		if (*ci == ' ' || *ci == '\t') {
			if (*ci == '\t') {
				bModified = true;
			}
			while (*(ci + 1) == ' ' || *(ci + 1) == '\t') {
				ci++;
				bModified = true;
			}
			if (!bIsLineStart && (*(ci + 1) != '\n' && *(ci + 1) != '\r')) {
				*co++ = ' ';
			} else {
				bModified = true;
			}
		} else {
			bIsLineStart = (*ci == '\n' || *ci == '\r');
			*co++ = *ci;
		}
	}
	if (bIsLineEnd && co > pszOut && *(co - 1) == ' ') {
		*--co = 0;
		bModified = true;
	}

	if (bModified) {
		if (iSelStart != iSelEnd) {
			SciCall_TargetFromSelection();
		} else {
			SciCall_TargetWholeDocument();
		}
		SciCall_ReplaceTarget(-1, pszOut);
		const Sci_Position iTargetStart = SciCall_GetTargetStart();
		const Sci_Position iTargetEnd = SciCall_GetTargetEnd();
		if (iCurPos > iAnchorPos) {
			iCurPos = iTargetEnd;
			iAnchorPos = iTargetStart;
		} else {
			iCurPos = iTargetStart;
			iAnchorPos = iTargetEnd;
		}
		SciCall_SetSel(iAnchorPos, iCurPos);
	}

	NP2HeapFree(pszIn);
	NP2HeapFree(pszOut);
}

//=============================================================================
//
// EditRemoveBlankLines()
//
void EditRemoveBlankLines(bool bMerge) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	Sci_Position iSelStart = SciCall_GetSelectionStart();
	Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	if (iSelStart == iSelEnd) {
		iSelStart = 0;
		iSelEnd = SciCall_GetLength();
	}

	Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
	Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);

	if (iSelStart > SciCall_PositionFromLine(iLineStart)) {
		iLineStart++;
	}

	if (iSelEnd <= SciCall_PositionFromLine(iLineEnd) && iLineEnd != SciCall_GetLineCount() - 1) {
		iLineEnd--;
	}

	SciCall_BeginUndoAction();
	for (Sci_Line iLine = iLineStart; iLine <= iLineEnd;) {
		Sci_Line nBlanks = 0;
		while (iLine + nBlanks <= iLineEnd && SciCall_PositionFromLine(iLine + nBlanks) == SciCall_GetLineEndPosition(iLine + nBlanks)) {
			nBlanks++;
		}

		if (nBlanks == 0 || (nBlanks == 1 && bMerge)) {
			iLine += nBlanks + 1;
		} else {
			if (bMerge) {
				nBlanks--;
			}

			const Sci_Position iTargetStart = SciCall_PositionFromLine(iLine);
			const Sci_Position iTargetEnd = SciCall_PositionFromLine(iLine + nBlanks);
			SciCall_DeleteRange(iTargetStart, iTargetEnd - iTargetStart);

			if (bMerge) {
				iLine++;
			}
			iLineEnd -= nBlanks;
		}
	}
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditWrapToColumn()
//
void EditWrapToColumn(int nColumn/*, int nTabWidth*/) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	const Sci_Line iLine = SciCall_LineFromPosition(iSelStart);
	iSelStart = SciCall_PositionFromLine(iLine);

	const Sci_Position iSelCount = iSelEnd - iSelStart;
	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1 + 2);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1 + 2) * sizeof(WCHAR));

	const struct Sci_TextRangeFull tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRangeFull(&tr);

	const UINT cpEdit = SciCall_GetCodePage();
	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	NP2HeapFree(pszText);

	LPWSTR pszConvW = (LPWSTR)NP2HeapAlloc(cchTextW * sizeof(WCHAR) * 3 + 2);

	const int iEOLMode = SciCall_GetEOLMode();
	LPCWSTR wszEOL = (iEOLMode == SC_EOL_LF) ? L"\n" : L"\r\n";
	const int cchEOL = (iEOLMode == SC_EOL_CRLF) ? 2 : 1;

#define ISDELIMITER(wc) StrChr(L",;.:-+%&\xA6|/*?!\"\'~\xB4#=", wc)
#define ISWHITE(wc)		IsASpaceOrTab(wc)
#define ISWORDEND(wc)	(/*ISDELIMITER(wc) ||*/ IsASpace(wc))

	int cchConvW = 0;
	int iLineLength = 0;
	bool bModified = false;
	for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
		const WCHAR w = pszTextW[iTextW];

		//if (ISDELIMITER(w)) {
		//	int iNextWordLen = 0;
		//	WCHAR w2 = pszTextW[iTextW + 1];
		//
		//	if (iLineLength + iNextWordLen + 1 > nColumn) {
		//		memcpy(pszConvW + cchConvW, wszEOL, 2*sizeof(WCHAR));
		//		cchConvW += cchEOL;
		//		iLineLength = 0;
		//		bModified = true;
		//	}
		//
		//	while (w2 != L'\0' && !ISWORDEND(w2)) {
		//		iNextWordLen++;
		//		w2 = pszTextW[iTextW + iNextWordLen + 1];
		//	}
		//
		//	if (ISDELIMITER(w2) && iNextWordLen > 0) // delimiters go with the word
		//		iNextWordLen++;
		//
		//	pszConvW[cchConvW++] = w;
		//	iLineLength++;
		//
		//	if (iNextWordLen > 0) {
		//		if (iLineLength + iNextWordLen + 1 > nColumn) {
		//			memcpy(pszConvW + cchConvW, wszEOL, 2*sizeof(WCHAR));
		//			cchConvW += cchEOL;
		//			iLineLength = 0;
		//			bModified = true;
		//		}
		//	}
		//}

		if (ISWHITE(w)) {
			while (IsASpaceOrTab(pszTextW[iTextW + 1])) {
				iTextW++;
				bModified = true;
			} // Modified: left out some whitespaces

			WCHAR w2 = pszTextW[iTextW + 1];
			int iNextWordLen = 0;

			while (w2 != L'\0' && !ISWORDEND(w2)) {
				iNextWordLen++;
				w2 = pszTextW[iTextW + iNextWordLen + 1];
			}

			//if (ISDELIMITER(w2) /*&& iNextWordLen > 0*/) // delimiters go with the word
			//	iNextWordLen++;
			if (iNextWordLen > 0) {
				if (iLineLength + iNextWordLen + 1 > nColumn) {
					memcpy(pszConvW + cchConvW, wszEOL, 2*sizeof(WCHAR));
					cchConvW += cchEOL;
					iLineLength = 0;
					bModified = true;
				} else {
					if (iLineLength > 0) {
						pszConvW[cchConvW++] = L' ';
						iLineLength++;
					}
				}
			}
		} else {
			pszConvW[cchConvW++] = w;
			if (w == L'\r' || w == L'\n') {
				iLineLength = 0;
			} else {
				iLineLength++;
			}
		}
	}

	NP2HeapFree(pszTextW);

	if (bModified) {
		pszText = (char *)NP2HeapAlloc(cchConvW * kMaxMultiByteCount);
		const int cchText = WideCharToMultiByte(cpEdit, 0, pszConvW, cchConvW, pszText, (int)NP2HeapSize(pszText), NULL, NULL);
		EditReplaceRange(iSelStart, iSelEnd, cchText, pszText);
		NP2HeapFree(pszText);
	}

	NP2HeapFree(pszConvW);
}

//=============================================================================
//
// EditJoinLinesEx()
//
void EditJoinLinesEx(void) {
	if (SciCall_IsSelectionEmpty()) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	const Sci_Line iLine = SciCall_LineFromPosition(iSelStart);
	iSelStart = SciCall_PositionFromLine(iLine);

	const Sci_Position iSelCount = iSelEnd - iSelStart;
	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1 + 2);
	char *pszJoin = (char *)NP2HeapAlloc(NP2HeapSize(pszText));

	const struct Sci_TextRangeFull tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRangeFull(&tr);

	const int iEOLMode = SciCall_GetEOLMode();
	LPCSTR szEOL = (iEOLMode == SC_EOL_LF) ? "\n" : "\r\n";
	const int cchEOL = (iEOLMode == SC_EOL_CRLF) ? 2 : 1;

	Sci_Position cchJoin = 0;
	bool bModified = false;
	for (Sci_Position i = 0; i < iSelCount; i++) {
		if (IsEOLChar(pszText[i])) {
			if (pszText[i] == '\r' && pszText[i + 1] == '\n') {
				i++;
			}
			if (!IsEOLChar(pszText[i + 1]) && pszText[i + 1] != '\0') {
				pszJoin[cchJoin++] = ' ';
				bModified = true;
			} else {
				while (IsEOLChar(pszText[i + 1])) {
					i++;
					bModified = true;
				}
				if (pszText[i + 1] != '\0') {
					if (cchJoin != 0) {
						memcpy(pszJoin + cchJoin, szEOL, 2);
						cchJoin += cchEOL;
					}
					memcpy(pszJoin + cchJoin, szEOL, 2);
					cchJoin += cchEOL;
				}
			}
		} else {
			pszJoin[cchJoin++] = pszText[i];
		}
	}

	NP2HeapFree(pszText);

	if (bModified) {
		EditReplaceRange(iSelStart, iSelEnd, cchJoin, pszJoin);
	}

	NP2HeapFree(pszJoin);
}

//=============================================================================
//
// EditSortLines()
//
typedef struct SORTLINE {
	LPCWSTR pwszLine;
	LPCWSTR pwszSortEntry;
	LPCWSTR pwszSortLine;
	int iLine;
	EditSortFlag iSortFlags;
} SORTLINE;

static int __cdecl CmpSortLine(const void *p1, const void *p2) {
	const SORTLINE *s1 = (const SORTLINE *)p1;
	const SORTLINE *s2 = (const SORTLINE *)p2;
	const EditSortFlag iSortFlags = s1->iSortFlags;
	int cmp = 0;
	if (iSortFlags & EditSortFlag_LogicalNumber) {
		cmp = StrCmpLogicalW(s1->pwszSortEntry, s2->pwszSortEntry);
		if (cmp == 0 && (iSortFlags & (EditSortFlag_ColumnSort | EditSortFlag_GroupByFileType))) {
			cmp = StrCmpLogicalW(s1->pwszSortLine, s2->pwszSortLine);
		}
	}
	if (cmp == 0) {
		cmp = wcscmp(s1->pwszSortEntry, s2->pwszSortEntry);
		if (cmp == 0 && (iSortFlags & (EditSortFlag_ColumnSort | EditSortFlag_GroupByFileType))) {
			cmp = wcscmp(s1->pwszSortLine, s2->pwszSortLine);
		}
		if (cmp == 0) {
			// stable sort for duplicate lines
			cmp = s1->iLine - s2->iLine;
			if (iSortFlags & EditSortFlag_MergeDuplicate) {
				cmp = -cmp; // reverse order to keep first line
			}
		}
	}
	if (iSortFlags & EditSortFlag_Descending) {
		cmp = -cmp;
	}
	return cmp;
}

static int __cdecl DontSortLine(const void *p1, const void *p2) {
	const SORTLINE *s1 = (const SORTLINE *)p1;
	const SORTLINE *s2 = (const SORTLINE *)p2;
	return s1->iLine - s2->iLine;
}

void EditSortLines(EditSortFlag iSortFlags) {
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();
	if (iCurPos == iAnchorPos) {
		return;
	}

	Sci_Line iRcCurLine = 0;
	Sci_Line iRcAnchorLine = 0;
	Sci_Position iRcCurCol = 0;
	Sci_Position iRcAnchorCol = 0;

	Sci_Position iSelStart = 0;
	Sci_Line iLineStart;
	Sci_Line iLineEnd;
	Sci_Position iSortColumn;

	const bool bIsRectangular = SciCall_IsRectangleSelection();
	if (bIsRectangular) {
		iRcCurLine = SciCall_LineFromPosition(iCurPos);
		iRcAnchorLine = SciCall_LineFromPosition(iAnchorPos);

		iRcCurCol = SciCall_GetColumn(iCurPos);
		iRcAnchorCol = SciCall_GetColumn(iAnchorPos);

		iLineStart = min_pos(iRcCurLine, iRcAnchorLine);
		iLineEnd = max_pos(iRcCurLine, iRcAnchorLine);
		iSortColumn = min_pos(iRcCurCol, iRcAnchorCol);
	} else {
		iSelStart = SciCall_GetSelectionStart();
		const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

		const Sci_Line iLine = SciCall_LineFromPosition(iSelStart);
		iSelStart = SciCall_PositionFromLine(iLine);

		iLineStart = SciCall_LineFromPosition(iSelStart);
		iLineEnd = SciCall_LineFromPosition(iSelEnd);

		if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
			iLineEnd--;
		}

		iSortColumn = SciCall_GetColumn(iCurPos);
	}

	const Sci_Line iLineCount = iLineEnd - iLineStart + 1;
	if (iLineCount < 2) {
		return;
	}

	SciCall_BeginUndoAction();
	if (bIsRectangular) {
		EditPadWithSpaces(!(iSortFlags & EditSortFlag_Shuffle), true);
	}

	const UINT cpEdit = SciCall_GetCodePage();
	Sci_Position iTargetStart = SciCall_PositionFromLine(iLineStart);
	Sci_Position iTargetEnd = SciCall_PositionFromLine(iLineEnd + 1);
	const size_t cbPmszBuf = iTargetEnd - iTargetStart + 2*iLineCount + 1; // 2 for CR LF
	size_t cchTextW = cbPmszBuf*sizeof(WCHAR) + iLineCount*NP2_alignof(WCHAR *);
	if (iSortFlags & EditSortFlag_IgnoreCase) {
		cchTextW += cchTextW;
	}
	char * const pmszBuf = (char *)NP2HeapAlloc(cbPmszBuf);
	SORTLINE * const pLines = (SORTLINE *)NP2HeapAlloc(sizeof(SORTLINE) * iLineCount);
	WCHAR * const pszTextW = (WCHAR *)NP2HeapAlloc(cchTextW);
	size_t cchTotal = NP2_alignof(WCHAR *)/sizeof(WCHAR); // first pointer reserved for empty line

	for (Sci_Line i = 0, iLine = iLineStart; iLine <= iLineEnd; i++, iLine++) {
		SciCall_GetLine(iLine, pmszBuf);
		const Sci_Position cbLine = SciCall_GetLineLength(iLine);

		// remove EOL
		char *p = pmszBuf + cbLine - 1;
		if (*p == '\n' || *p == '\r') {
			*p-- = '\0';
		}
		if (*p == '\r') {
			*p-- = '\0';
		}

		if (p >= pmszBuf) {
			LPWSTR pwszLine = pszTextW + cchTotal;
			const UINT cchLine = MultiByteToWideChar(cpEdit, 0, pmszBuf, -1, pwszLine, (int)cbPmszBuf);
			cchTotal += NP2_align_up(cchLine, NP2_alignof(WCHAR *)/sizeof(WCHAR));
			pLines[i].pwszLine = pwszLine;
			if (iSortFlags & EditSortFlag_IgnoreCase) {
				// convert to uppercase for case insensitive comparison
				// https://learn.microsoft.com/en-us/dotnet/api/system.string.toupper?view=net-7.0#system-string-toupper
				LPWSTR pwszSortLine = pszTextW + cchTotal;
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
				const UINT charsConverted = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, pwszLine, cchLine, pwszSortLine, (int)cbPmszBuf, NULL, NULL, 0);
#else
				const UINT charsConverted = LCMapString(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, pwszLine, cchLine, pwszSortLine, (int)cbPmszBuf);
#endif
				cchTotal += NP2_align_up(charsConverted, NP2_alignof(WCHAR *)/sizeof(WCHAR));
				pwszLine = pwszSortLine;
			}

			pLines[i].pwszSortLine = pwszLine;
			if (iSortFlags & EditSortFlag_ColumnSort) {
				const int tabWidth = fvCurFile.iTabWidth;
				Sci_Position col = 0;
				Sci_Position tabs = tabWidth;
				while (*pwszLine) {
					if (*pwszLine == L'\t') {
						if (col + tabs <= iSortColumn) {
							col += tabs;
							tabs = tabWidth;
							++pwszLine;
						} else {
							break;
						}
					} else if (col < iSortColumn) {
						col++;
						if (--tabs == 0) {
							tabs = tabWidth;
						}
						pwszLine += 1 + IS_SURROGATE_PAIR(*pwszLine, pwszLine[1]);
					} else {
						break;
					}
				}
			} else if (iSortFlags & EditSortFlag_GroupByFileType) {
				pwszLine = PathFindExtension(pwszLine);
			}
			pLines[i].pwszSortEntry = pwszLine;
			pLines[i].iLine = (int)iLine;
			pLines[i].iSortFlags = iSortFlags;
		} else {
			pLines[i].pwszLine = pszTextW;
			pLines[i].pwszSortEntry = pszTextW;
			pLines[i].pwszSortLine = pszTextW;
			pLines[i].iLine = (int)iLine;
			pLines[i].iSortFlags = iSortFlags;
		}
	}

	if (iSortFlags & EditSortFlag_Shuffle) {
		srand(GetTickCount());
		for (Sci_Line i = iLineCount - 1; i > 0; i--) {
			const Sci_Line j = rand() % i;
			const SORTLINE sLine = pLines[i];
			pLines[i] = pLines[j];
			pLines[j] = sLine;
		}
	} else {
		qsort(pLines, iLineCount, sizeof(SORTLINE), CmpSortLine);
		if (iSortFlags > EditSortFlag_Shuffle) {
			bool bLastDup = false;
			for (Sci_Line i = 0; i < iLineCount; i++) {
				BOOL bDropLine;
				if (i + 1 < iLineCount && wcscmp(pLines[i].pwszSortLine, pLines[i + 1].pwszSortLine) == 0) {
					bLastDup = true;
					bDropLine = EditSortFlag_MergeDuplicate | EditSortFlag_RemoveDuplicate;
				} else {
					bDropLine = bLastDup ? EditSortFlag_RemoveDuplicate : EditSortFlag_RemoveUnique;
					bLastDup = false;
				}
				if (iSortFlags & bDropLine) {
					pLines[i].pwszLine = NULL;
				}
			}
		}
		if (iSortFlags & EditSortFlag_DontSort) {
			qsort(pLines, iLineCount, sizeof(SORTLINE), DontSortLine);
		}
	}

	const int iEOLMode = SciCall_GetEOLMode();
	char *pszOut = pmszBuf;
	cchTotal = 0;
	for (Sci_Line i = 0; i < iLineCount; i++) {
		LPCWSTR pwszLine = pLines[i].pwszLine;
		if (pwszLine) {
			const UINT cbLine = WideCharToMultiByte(cpEdit, 0, pwszLine, -1, pszOut, (int)cbPmszBuf, NULL, NULL);
			cchTotal += cbLine;
			pszOut += cbLine - 1;
			switch (iEOLMode) {
			default: // SC_EOL_CRLF
				++cchTotal;
				*pszOut++ = '\r';
				*pszOut++ = '\n';
				break;
			case SC_EOL_LF:
				*pszOut++ = '\n';
				break;
			case SC_EOL_CR:
				*pszOut++ = '\r';
				break;
			}
		}
	}
	if (cchTotal != 0) {
		const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iLineEnd);
		if (iLineEndPos == iTargetEnd) {
			// no EOL on last line
			cchTotal -= (iEOLMode == SC_EOL_CRLF) ? 2 : 1;
		}
	}

	NP2HeapFree(pLines);
	NP2HeapFree(pszTextW);
	SciCall_SetTargetRange(iTargetStart, iTargetEnd);
	SciCall_ReplaceTarget(cchTotal, pmszBuf);
	SciCall_EndUndoAction();
	NP2HeapFree(pmszBuf);

	if (!bIsRectangular) {
		if (iAnchorPos > iCurPos) {
			iCurPos = iSelStart;
			iAnchorPos = iSelStart + cchTotal;
		} else {
			iAnchorPos = iSelStart;
			iCurPos = iSelStart + cchTotal;
		}
		SciCall_SetSel(iAnchorPos, iCurPos);
	} else {
		iTargetStart = SciCall_GetTargetStart();
		iTargetEnd = SciCall_GetTargetEnd();
		SciCall_ClearSelections();
		if (iTargetStart != iTargetEnd) {
			iTargetEnd -= (iEOLMode == SC_EOL_CRLF) ? 2 : 1;
			iLineStart = SciCall_LineFromPosition(iTargetStart);
			iLineEnd = SciCall_LineFromPosition(iTargetEnd);
			if (iRcAnchorLine > iRcCurLine) {
				iCurPos = SciCall_FindColumn(iLineStart, iRcCurCol);
				iAnchorPos = SciCall_FindColumn(iLineEnd, iRcAnchorCol);
			} else {
				iCurPos = SciCall_FindColumn(iLineEnd, iRcCurCol);
				iAnchorPos = SciCall_FindColumn(iLineStart, iRcAnchorCol);
			}
			if (iCurPos != iAnchorPos) {
				SciCall_SetRectangularSelectionCaret(iCurPos);
				SciCall_SetRectangularSelectionAnchor(iAnchorPos);
			} else {
				SciCall_SetSel(iTargetStart, iTargetStart);
			}
		} else {
			SciCall_SetSel(iTargetStart, iTargetStart);
		}
	}
}

//=============================================================================
//
// EditJumpTo()
//
void EditJumpTo(Sci_Line iNewLine, Sci_Position iNewCol) {
	// Jumpt to end with line set to -1
	if (iNewLine < 0 || iNewLine > SciCall_GetLineCount()) {
		iNewCol = SciCall_GetLength();
	} else {
		--iNewLine;
		const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iNewLine);
		iNewCol = min_pos(iNewCol, iLineEndPos);
		iNewCol = SciCall_FindColumn(iNewLine, iNewCol - 1);
	}

	EditSelectEx(iNewCol, iNewCol);
	SciCall_ChooseCaretX();
}

//=============================================================================
//
// EditSelectEx()
//
void EditSelectEx(Sci_Position iAnchorPos, Sci_Position iCurrentPos) {
	const Sci_Line iNewLine = SciCall_LineFromPosition(iCurrentPos);
	const Sci_Line iAnchorLine = (iAnchorPos == iCurrentPos)? iNewLine : SciCall_LineFromPosition(iAnchorPos);

	SciCall_EnsureVisible(iAnchorLine);
	if (iAnchorLine == iNewLine) {
		// TODO: center current line on screen when it's not visible
	} else {
		// Ensure that the first and last lines of a selection are always unfolded
		// This needs to be done *before* the SciCall_SetSel() message
		SciCall_EnsureVisible(iNewLine);
	}

	SciCall_SetXCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 50);
	SciCall_SetYCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 5);
	if (iAnchorPos == iCurrentPos) {
		SciCall_GotoPos(iAnchorPos);
	} else {
		SciCall_SetSel(iAnchorPos, iCurrentPos);
	}
	SciCall_SetXCaretPolicy(CARET_SLOP | CARET_EVEN, 50);
	SciCall_SetYCaretPolicy(CARET_EVEN, 0);
}

//=============================================================================
//
// EditFixPositions()
//
void EditFixPositions(void) {
	const Sci_Position iMaxPos = SciCall_GetLength();
	Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	const Sci_Position iAnchorPos = SciCall_GetAnchor();

	if (iCurrentPos > 0 && iCurrentPos < iMaxPos) {
		const Sci_Position iNewPos = SciCall_PositionAfter(SciCall_PositionBefore(iCurrentPos));
		if (iNewPos != iCurrentPos) {
			SciCall_SetCurrentPos(iNewPos);
			iCurrentPos = iNewPos;
		}
	}

	if (iAnchorPos != iCurrentPos && iAnchorPos > 0 && iAnchorPos < iMaxPos) {
		const Sci_Position iNewPos = SciCall_PositionAfter(SciCall_PositionBefore(iAnchorPos));
		if (iNewPos != iAnchorPos) {
			SciCall_SetAnchor(iNewPos);
		}
	}
}

//=============================================================================
//
// EditEnsureSelectionVisible()
//
void EditEnsureSelectionVisible(void) {
	const Sci_Position iAnchorPos = SciCall_GetAnchor();
	const Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	EditSelectEx(iAnchorPos, iCurrentPos);
}

void EditEnsureConsistentLineEndings(void) {
	const int iEOLMode = SciCall_GetEOLMode();
	if (iEOLMode == SC_EOL_CRLF) {
#if defined(_WIN64)
		const int options = SciCall_GetDocumentOptions();
		if (!(options & SC_DOCUMENTOPTION_TEXT_LARGE)) {
			const Sci_Position dwLength = SciCall_GetLength() + SciCall_GetLineCount();
			if (dwLength >= INT_MAX) {
				return;
			}
		}
#else
		const DWORD dwLength = (DWORD)SciCall_GetLength() + (DWORD)SciCall_GetLineCount();
		if (dwLength >= INT_MAX) {
			return;
		}
#endif
	}

	SciCall_ConvertEOLs(iEOLMode);
	EditFixPositions();
}

//=============================================================================
//
// EditGetExcerpt()
//
void EditGetExcerpt(LPWSTR lpszExcerpt, DWORD cchExcerpt) {
	if (SciCall_IsSelectionEmpty() || SciCall_IsRectangleSelection()) {
		StrCpyExW(lpszExcerpt, L"");
		return;
	}

	WCHAR tch[256] = L"";
	DWORD cch = 0;
	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = min_pos(min_pos(SciCall_GetSelectionEnd(), iSelStart + COUNTOF(tch)), SciCall_GetLength());
	const Sci_Position iSelCount = iSelEnd - iSelStart;

	char *pszText = (char *)NP2HeapAlloc(iSelCount + 2);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	const struct Sci_TextRangeFull tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRangeFull(&tr);
	const UINT cpEdit = SciCall_GetCodePage();
	MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));

	for (WCHAR *p = pszTextW; *p && cch < COUNTOF(tch) - 1; p++) {
		if (*p == L'\r' || *p == L'\n' || *p == L'\t' || *p == L' ') {
			tch[cch++] = L' ';
			while (*(p + 1) == L'\r' || *(p + 1) == L'\n' || *(p + 1) == L'\t' || *(p + 1) == L' ') {
				p++;
			}
		} else {
			tch[cch++] = *p;
		}
	}
	tch[cch++] = L'\0';
	StrTrim(tch, L" ");

	if (cch == 1) {
		lstrcpy(tch, L" ... ");
	}

	if (cch > cchExcerpt) {
		tch[cchExcerpt - 2] = L'.';
		tch[cchExcerpt - 3] = L'.';
		tch[cchExcerpt - 4] = L'.';
	}
	lstrcpyn(lpszExcerpt, tch, cchExcerpt);

	NP2HeapFree(pszText);
	NP2HeapFree(pszTextW);
}

void EditSelectWord(void) {
	const Sci_Position iPos = SciCall_GetCurrentPos();

	if (SciCall_IsSelectionEmpty()) {
		Sci_Position iWordStart = SciCall_WordStartPosition(iPos, true);
		Sci_Position iWordEnd = SciCall_WordEndPosition(iPos, true);

		if (iWordStart == iWordEnd) {// we are in whitespace salad...
			iWordStart = SciCall_WordEndPosition(iPos, false);
			iWordEnd = SciCall_WordEndPosition(iWordStart, true);
			if (iWordStart != iWordEnd) {
				//if (IsDocWordChar(SciCall_GetCharAt(iWordStart - 1))) {
				//	--iWordStart;
				//}
				SciCall_SetSel(iWordStart, iWordEnd);
			}
		} else {
			//if (IsDocWordChar(SciCall_GetCharAt(iWordStart - 1))) {
			//	--iWordStart;
			//}
			SciCall_SetSel(iWordStart, iWordEnd);
		}

		if (!SciCall_IsSelectionEmpty()) {
			return;
		}
	}

	const Sci_Line iLine = SciCall_LineFromPosition(iPos);
	const Sci_Position iLineStart = SciCall_GetLineIndentPosition(iLine);
	const Sci_Position iLineEnd = SciCall_GetLineEndPosition(iLine);
	SciCall_SetSel(iLineStart, iLineEnd);
}

void EditSelectLines(bool currentBlock, bool lineSelection) {
	if (lineSelection && !currentBlock) {
		SciCall_SetSelectionMode(SC_SEL_LINES);
		return;
	}

	// see Editor::LineSelectionRange()
	Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();
	bool backward = (iCurrentPos < iAnchorPos);

	Sci_Line iLineAnchorPos = SciCall_LineFromPosition(iAnchorPos);
	Sci_Line iLineCurPos = SciCall_LineFromPosition(iCurrentPos);

	if (currentBlock) {
		Sci_Line iLineStart = iLineCurPos;
		const int level = SciCall_GetFoldLevel(iLineStart);
		if (!(level & SC_FOLDLEVELHEADERFLAG)) {
			iLineStart = SciCall_GetFoldParent(iLineStart);
			if (iLineStart < 0) {
				SciCall_SelectAll();
				return;
			}
		}

		const Sci_Line iLineEnd = SciCall_GetLastChild(iLineStart);
		backward = backward || (iCurrentPos == iAnchorPos && iLineEnd - iLineCurPos > iLineCurPos - iLineStart);
		if (backward) {
			iLineCurPos = iLineStart;
			iLineAnchorPos = iLineEnd;
		} else {
			iLineCurPos = iLineEnd;
			iLineAnchorPos = iLineStart;
		}
	}

	const Sci_Line offset = lineSelection ? 0 : 1;
	if (backward) {
		iLineAnchorPos = iLineAnchorPos + offset;
	} else {
		iLineCurPos = iLineCurPos + offset;
	}

	iAnchorPos = SciCall_PositionFromLine(iLineAnchorPos);
	iCurrentPos = SciCall_PositionFromLine(iLineCurPos);
	SciCall_SetSel(iAnchorPos, iCurrentPos);
	if (lineSelection) {
		SciCall_SetSelectionMode(SC_SEL_LINES);
	}
	SciCall_ChooseCaretX();
}

static LRESULT CALLBACK AddBackslashEditProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
	UNREFERENCED_PARAMETER(dwRefData);

	switch (umsg) {
	case WM_PASTE: {
		bool done = false;
		LPWSTR lpsz = EditGetClipboardTextW();
		if (StrNotEmpty(lpsz)) {
			const int len = lstrlen(lpsz);
			LPWSTR lpszEsc = (LPWSTR)NP2HeapAlloc((2*len + 1)*sizeof(WCHAR));
			if (lpszEsc != NULL) {
				AddBackslashW(lpszEsc, lpsz);
				SendMessage(hwnd, EM_REPLACESEL, TRUE, (LPARAM)(lpszEsc));
				NP2HeapFree(lpszEsc);
				done = true;
			}
		}
		if (lpsz != NULL) {
			NP2HeapFree(lpsz);
		}
		if (done) {
			return TRUE;
		}
	}
	break;

	case WM_NCDESTROY:
		RemoveWindowSubclass(hwnd, AddBackslashEditProc, uIdSubclass);
		break;
	}

	return DefSubclassProc(hwnd, umsg, wParam, lParam);
}

void AddBackslashComboBoxSetup(HWND hwndDlg, int nCtlId) {
	HWND hwnd = GetDlgItem(hwndDlg, nCtlId);
	COMBOBOXINFO info;
	info.cbSize = sizeof(COMBOBOXINFO);
	if (GetComboBoxInfo(hwnd, &info)) {
		SetWindowSubclass(info.hwndItem, AddBackslashEditProc, 0, 0);
	}
}

extern bool bFindReplaceTransparentMode;
extern int iFindReplaceOpacityLevel;
extern bool bFindReplaceUseMonospacedFont;
extern bool bFindReplaceFindAllBookmark;
extern int iSelectOption;

static void FindReplaceSetFont(HWND hwnd, bool monospaced, HFONT *hFontFindReplaceEdit) {
	HWND hwndFind = GetDlgItem(hwnd, IDC_FINDTEXT);
	HWND hwndRepl = GetDlgItem(hwnd, IDC_REPLACETEXT);
	HFONT font = NULL;
	if (monospaced) {
		font = *hFontFindReplaceEdit;
		if (font == NULL) {
			*hFontFindReplaceEdit = font = Style_CreateCodeFont(g_uCurrentDPI);
		}
	}
	if (font == NULL) {
		// use font from parent window
		font = GetWindowFont(hwnd);
	}
	SetWindowFont(hwndFind, font, TRUE);
	if (hwndRepl) {
		SetWindowFont(hwndRepl, font, TRUE);
	}
}

static bool CopySelectionAsFindText(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bFirstTime) {
	const Sci_Position cchSelection = SciCall_GetSelTextLength();
	char *lpszSelection = NULL;

	if (cchSelection != 0 && cchSelection <= NP2_FIND_REPLACE_LIMIT && (iSelectOption & SelectOption_CopySelectionAsFindText)) {
		lpszSelection = (char *)NP2HeapAlloc(cchSelection + 1);
		SciCall_GetSelText(lpszSelection);
	}

	// only for manually selected text
	const bool hasFindText = StrNotEmptyA(lpszSelection);

	// First time you bring up find/replace dialog,
	// copy content from clipboard to find box when nothing is selected in the editor.
	if (!hasFindText && bFirstTime && (iSelectOption & SelectOption_CopyPasteBufferAsFindText)) {
		char *pClip = EditGetClipboardText(hwnd);
		if (pClip != NULL) {
			const size_t len = strlen(pClip);
			if (len > 0 && len <= NP2_FIND_REPLACE_LIMIT) {
				NP2HeapFree(lpszSelection);
				lpszSelection = (char *)NP2HeapAlloc(len + 2);
				strcpy(lpszSelection, pClip);
			}
			LocalFree(pClip);
		}
	}

	if (StrNotEmptyA(lpszSelection)) {
		const UINT cpEdit = SciCall_GetCodePage();
		// Check lpszSelection and truncate bad chars
		//char *lpsz = strpbrk(lpszSelection, "\r\n\t");
		//if (lpsz) {
		//	*lpsz = '\0';
		//}

		char *lpszEscSel = (char *)NP2HeapAlloc((2 * NP2_FIND_REPLACE_LIMIT));
		if (AddBackslashA(lpszEscSel, lpszSelection)) {
			lpefr->bTransformBS = !(lpefr->fuFlags & SCFIND_REGEXP);
		}

		SetDlgItemTextA2W(cpEdit, hwnd, IDC_FINDTEXT, lpszEscSel);
		NP2HeapFree(lpszEscSel);
	}

	if (lpszSelection != NULL) {
		NP2HeapFree(lpszSelection);
	}
	return hasFindText;
}

//=============================================================================
//
// EditFindReplaceDlgProc()
//
static INT_PTR CALLBACK EditFindReplaceDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	static int bSwitchedFindReplace = 0;
	static int xFindReplaceDlgSave;
	static int yFindReplaceDlgSave;
	static EDITFINDREPLACE efrSave;
	static HFONT hFontFindReplaceEdit;

	WCHAR tch[NP2_FIND_REPLACE_LIMIT + 32];

	switch (umsg) {
	case WM_INITDIALOG: {
		SetWindowLongPtr(hwnd, DWLP_USER, lParam);
		ResizeDlg_InitX(hwnd, cxFindReplaceDlg, IDC_RESIZEGRIP2);

		HWND hwndFind = GetDlgItem(hwnd, IDC_FINDTEXT);
		AddBackslashComboBoxSetup(hwnd, IDC_FINDTEXT);

		// Load MRUs
		MRU_AddToCombobox(&mruFind, hwndFind);

		LPEDITFINDREPLACE lpefr = (LPEDITFINDREPLACE)lParam;
		// don't copy selection after toggle find & replace on this window.
		bool hasFindText = false;
		if (bSwitchedFindReplace != 3) {
			hasFindText = CopySelectionAsFindText(hwnd, lpefr, true);
		}
		if (!GetWindowTextLength(hwndFind)) {
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8);
		}

		ComboBox_LimitText(hwndFind, NP2_FIND_REPLACE_LIMIT);
		ComboBox_SetExtendedUI(hwndFind, TRUE);

		HWND hwndRepl = GetDlgItem(hwnd, IDC_REPLACETEXT);
		if (hwndRepl) {
			AddBackslashComboBoxSetup(hwnd, IDC_REPLACETEXT);
			MRU_AddToCombobox(&mruReplace, hwndRepl);
			ComboBox_LimitText(hwndRepl, NP2_FIND_REPLACE_LIMIT);
			ComboBox_SetExtendedUI(hwndRepl, TRUE);
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_REPLACETEXT, lpefr->szReplaceUTF8);
		}

		// focus on replace box when selected text is not empty.
		PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)((hasFindText && hwndRepl)? hwndRepl : hwndFind), TRUE);

		if (lpefr->fuFlags & SCFIND_MATCHCASE) {
			CheckDlgButton(hwnd, IDC_FINDCASE, BST_CHECKED);
		}

		if (lpefr->fuFlags & SCFIND_WHOLEWORD) {
			CheckDlgButton(hwnd, IDC_FINDWORD, BST_CHECKED);
		}

		if (lpefr->fuFlags & SCFIND_WORDSTART) {
			CheckDlgButton(hwnd, IDC_FINDSTART, BST_CHECKED);
		}

		if (lpefr->fuFlags & SCFIND_REGEXP) {
			CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_CHECKED);
		}

		if (lpefr->bTransformBS) {
			CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_CHECKED);
		}

		if (lpefr->bWildcardSearch) {
			CheckDlgButton(hwnd, IDC_WILDCARDSEARCH, BST_CHECKED);
			CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
		}

		if (lpefr->bNoFindWrap) {
			CheckDlgButton(hwnd, IDC_NOWRAP, BST_CHECKED);
		}

		if (hwndRepl) {
			if (bSwitchedFindReplace) {
				if (lpefr->bFindClose) {
					CheckDlgButton(hwnd, IDC_FINDCLOSE, BST_CHECKED);
				}
			} else {
				if (lpefr->bReplaceClose) {
					CheckDlgButton(hwnd, IDC_FINDCLOSE, BST_CHECKED);
				}
			}
		} else {
			if (bSwitchedFindReplace) {
				if (lpefr->bReplaceClose) {
					CheckDlgButton(hwnd, IDC_FINDCLOSE, BST_CHECKED);
				}
			} else {
				if (lpefr->bFindClose) {
					CheckDlgButton(hwnd, IDC_FINDCLOSE, BST_CHECKED);
				}
			}
		}

		if (!bSwitchedFindReplace) {
			if (xFindReplaceDlg == 0 || yFindReplaceDlg == 0) {
				CenterDlgInParent(hwnd);
			} else {
				SetDlgPos(hwnd, xFindReplaceDlg, yFindReplaceDlg);
			}
		} else {
			SetDlgPos(hwnd, xFindReplaceDlgSave, yFindReplaceDlgSave);
			bSwitchedFindReplace = 0;
			memcpy(lpefr, &efrSave, sizeof(EDITFINDREPLACE));
		}

		if (bFindReplaceTransparentMode) {
			CheckDlgButton(hwnd, IDC_TRANSPARENT, BST_CHECKED);
		}
		if (bFindReplaceFindAllBookmark) {
			CheckDlgButton(hwnd, IDC_FINDALLBOOKMARK, BST_CHECKED);
		}
		if (bFindReplaceUseMonospacedFont) {
			CheckDlgButton(hwnd, IDC_USEMONOSPACEDFONT, BST_CHECKED);
			FindReplaceSetFont(hwnd, TRUE, &hFontFindReplaceEdit);
		}
	}
	return TRUE;

	case APPM_COPYDATA: {
		HWND hwndFind = GetDlgItem(hwnd, IDC_FINDTEXT);
		HWND hwndRepl = GetDlgItem(hwnd, IDC_REPLACETEXT);
		LPEDITFINDREPLACE lpefr = (LPEDITFINDREPLACE)GetWindowLongPtr(hwnd, DWLP_USER);

		const bool hasFindText = CopySelectionAsFindText(hwnd, lpefr, false);
		if (!GetWindowTextLength(hwndFind)) {
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8);
		}
		if (lpefr->bTransformBS) {
			CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_CHECKED);
		}
		// focus on replace box when selected text is not empty.
		PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)((hasFindText && hwndRepl)? hwndRepl : hwndFind), TRUE);
	}
	break;

	case WM_DESTROY:
		if (hFontFindReplaceEdit) {
			DeleteObject(hFontFindReplaceEdit);
			hFontFindReplaceEdit = NULL;
		}
		ResizeDlg_Destroy(hwnd, &cxFindReplaceDlg, NULL);
		return FALSE;

	case WM_SIZE: {
		int dx;

		const bool isReplace = GetDlgItem(hwnd, IDC_REPLACETEXT) != NULL;
		ResizeDlg_Size(hwnd, lParam, &dx, NULL);
		HDWP hdwp = BeginDeferWindowPos(isReplace ? 13 : 9);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_RESIZEGRIP2, dx, 0, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDOK, dx, 0, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_FINDTEXT, dx, 0, SWP_NOMOVE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_CLEAR_FIND, dx, 0, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_FINDPREV, dx, 0, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_SAVEPOSITION, dx, 0, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_RESETPOSITION, dx, 0, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_FINDALL, dx, 0, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_REPLACEALL, dx, 0, SWP_NOSIZE);
		if (isReplace) {
			hdwp = DeferCtlPos(hdwp, hwnd, IDC_REPLACETEXT, dx, 0, SWP_NOMOVE);
			hdwp = DeferCtlPos(hdwp, hwnd, IDC_CLEAR_REPLACE, dx, 0, SWP_NOSIZE);
			hdwp = DeferCtlPos(hdwp, hwnd, IDC_REPLACE, dx, 0, SWP_NOSIZE);
			hdwp = DeferCtlPos(hdwp, hwnd, IDC_REPLACEINSEL, dx, 0, SWP_NOSIZE);
		}
		EndDeferWindowPos(hdwp);
	}
	return TRUE;

	case WM_GETMINMAXINFO:
		ResizeDlg_GetMinMaxInfo(hwnd, lParam);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_FINDTEXT:
		case IDC_REPLACETEXT: {
			HWND hwndFind = GetDlgItem(hwnd, IDC_FINDTEXT);
			const BOOL bEnable = ComboBox_HasText(hwndFind);

			EnableWindow(GetDlgItem(hwnd, IDOK), bEnable);
			EnableWindow(GetDlgItem(hwnd, IDC_FINDPREV), bEnable);
			EnableWindow(GetDlgItem(hwnd, IDC_FINDALL), bEnable);
			EnableWindow(GetDlgItem(hwnd, IDC_REPLACE), bEnable);
			EnableWindow(GetDlgItem(hwnd, IDC_REPLACEALL), bEnable);
			EnableWindow(GetDlgItem(hwnd, IDC_REPLACEINSEL), bEnable);

			if (HIWORD(wParam) == CBN_CLOSEUP) {
				HWND hwndCtl = GetDlgItem(hwnd, LOWORD(wParam));
				const DWORD lSelEnd = ComboBox_GetEditSelEnd(hwndCtl);
				ComboBox_SetEditSel(hwndCtl, lSelEnd, lSelEnd);
			}
		}
		break;

		case IDC_FINDREGEXP:
			if (IsButtonChecked(hwnd, IDC_FINDREGEXP)) {
				CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_UNCHECKED);
				// Can not use wildcard search together with regexp
				CheckDlgButton(hwnd, IDC_WILDCARDSEARCH, BST_UNCHECKED);
			}
			break;

		case IDC_FINDTRANSFORMBS:
			if (IsButtonChecked(hwnd, IDC_FINDTRANSFORMBS)) {
				CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
			}
			break;

		case IDC_WILDCARDSEARCH:
			CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
			//if (IsButtonChecked(hwnd, IDC_FINDWILDCARDS))
			//	CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
			break;

		case IDC_TRANSPARENT:
			bFindReplaceTransparentMode = IsButtonChecked(hwnd, IDC_TRANSPARENT);
			break;

		case IDC_FINDALLBOOKMARK:
			bFindReplaceFindAllBookmark = IsButtonChecked(hwnd, IDC_FINDALLBOOKMARK);
			break;

		case IDOK:
		case IDC_FINDPREV:
		case IDC_FINDALL:
		case IDC_REPLACE:
		case IDC_REPLACEALL:
		case IDC_REPLACEINSEL:
		case IDACC_SELTONEXT:
		case IDACC_SELTOPREV: {
			LPEDITFINDREPLACE lpefr = (LPEDITFINDREPLACE)GetWindowLongPtr(hwnd, DWLP_USER);
			HWND hwndFind = GetDlgItem(hwnd, IDC_FINDTEXT);
			HWND hwndRepl = GetDlgItem(hwnd, IDC_REPLACETEXT);
			const bool bIsFindDlg = (hwndRepl == NULL);
			// Get current code page for Unicode conversion
			const UINT cpEdit = SciCall_GetCodePage();

			if (!GetDlgItemTextA2W(cpEdit, hwnd, IDC_FINDTEXT, lpefr->szFind, COUNTOF(lpefr->szFind))) {
				EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
				EnableWindow(GetDlgItem(hwnd, IDC_FINDPREV), FALSE);
				EnableWindow(GetDlgItem(hwnd, IDC_FINDALL), FALSE);
				EnableWindow(GetDlgItem(hwnd, IDC_REPLACE), FALSE);
				EnableWindow(GetDlgItem(hwnd, IDC_REPLACEALL), FALSE);
				EnableWindow(GetDlgItem(hwnd, IDC_REPLACEINSEL), FALSE);
				return TRUE;
			}

			if (!bIsFindDlg) {
				GetDlgItemTextA2W(cpEdit, hwnd, IDC_REPLACETEXT, lpefr->szReplace, COUNTOF(lpefr->szReplace));
			}

			lpefr->bWildcardSearch = IsButtonChecked(hwnd, IDC_WILDCARDSEARCH);
			lpefr->fuFlags = 0;

			if (IsButtonChecked(hwnd, IDC_FINDCASE)) {
				lpefr->fuFlags |= SCFIND_MATCHCASE;
			}

			if (IsButtonChecked(hwnd, IDC_FINDWORD)) {
				lpefr->fuFlags |= SCFIND_WHOLEWORD;
			}

			if (IsButtonChecked(hwnd, IDC_FINDSTART)) {
				lpefr->fuFlags |= SCFIND_WORDSTART;
			}

			if (IsButtonChecked(hwnd, IDC_FINDREGEXP)) {
				lpefr->fuFlags |= NP2_RegexDefaultFlags;
			}

			lpefr->bTransformBS = IsButtonChecked(hwnd, IDC_FINDTRANSFORMBS);
			lpefr->bNoFindWrap = IsButtonChecked(hwnd, IDC_NOWRAP);

			if (bIsFindDlg) {
				lpefr->bFindClose = IsButtonChecked(hwnd, IDC_FINDCLOSE);
			} else {
				lpefr->bReplaceClose = IsButtonChecked(hwnd, IDC_FINDCLOSE);
			}

			// Save MRUs
			if (StrNotEmptyA(lpefr->szFind)) {
				if (GetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8, COUNTOF(lpefr->szFindUTF8))) {
					ComboBox_GetText(hwndFind, tch, COUNTOF(tch));
					MRU_AddMultiline(&mruFind, tch);
				}
			}
			if (StrNotEmptyA(lpefr->szReplace)) {
				if (GetDlgItemTextA2W(CP_UTF8, hwnd, IDC_REPLACETEXT, lpefr->szReplaceUTF8, COUNTOF(lpefr->szReplaceUTF8))) {
					ComboBox_GetText(hwndRepl, tch, COUNTOF(tch));
					MRU_AddMultiline(&mruReplace, tch);
				}
			} else {
				strcpy(lpefr->szReplaceUTF8, "");
			}

			bool bCloseDlg;
			if (bIsFindDlg) {
				bCloseDlg = lpefr->bFindClose;
			} else {
				if (LOWORD(wParam) == IDOK) {
					bCloseDlg = false;
				} else {
					bCloseDlg = lpefr->bReplaceClose;
				}
			}

			// Reload MRUs
			ComboBox_ResetContent(hwndFind);
			ComboBox_ResetContent(hwndRepl);
			MRU_AddToCombobox(&mruFind, hwndFind);
			MRU_AddToCombobox(&mruReplace, hwndRepl);

			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8);
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_REPLACETEXT, lpefr->szReplaceUTF8);

			SendMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetFocus()), TRUE);

			if (bCloseDlg) {
				DestroyWindow(hwnd);
				hDlgFindReplace = NULL;
			}

			switch (LOWORD(wParam)) {
			case IDOK: // find next
			case IDACC_SELTONEXT:
				if (!bIsFindDlg) {
					bReplaceInitialized = true;
				}
				EditFindNext(lpefr, LOWORD(wParam) == IDACC_SELTONEXT || KeyboardIsKeyDown(VK_SHIFT));
				break;

			case IDC_FINDPREV: // find previous
			case IDACC_SELTOPREV:
				if (!bIsFindDlg) {
					bReplaceInitialized = true;
				}
				EditFindPrev(lpefr, LOWORD(wParam) == IDACC_SELTOPREV || KeyboardIsKeyDown(VK_SHIFT));
				break;

			case IDC_REPLACE:
				bReplaceInitialized = true;
				EditReplace(lpefr->hwnd, lpefr);
				break;

			case IDC_FINDALL:
				EditFindAll(lpefr, false);
				break;

			case IDC_REPLACEALL:
				if (bIsFindDlg) {
					EditFindAll(lpefr, true);
				} else {
					bReplaceInitialized = true;
					EditReplaceAll(lpefr->hwnd, lpefr, true);
				}
				break;

			case IDC_REPLACEINSEL:
				bReplaceInitialized = true;
				EditReplaceAllInSelection(lpefr->hwnd, lpefr, true);
				break;
			}
		}
		break;

		case IDCANCEL:
			DestroyWindow(hwnd);
			break;

		case IDACC_FIND:
			PostWMCommand(hwndMain, IDM_EDIT_FIND);
			break;

		case IDACC_REPLACE:
			PostWMCommand(hwndMain, IDM_EDIT_REPLACE);
			break;

		case IDACC_FINDNEXT:
			PostWMCommand(hwnd, IDOK);
			break;

		case IDACC_FINDPREV:
			PostWMCommand(hwnd, IDC_FINDPREV);
			break;

		case IDACC_REPLACENEXT:
			if (GetDlgItem(hwnd, IDC_REPLACETEXT) != NULL) {
				PostWMCommand(hwnd, IDC_REPLACE);
			}
			break;

		case IDACC_SAVEFIND: {
			SendWMCommand(hwndMain, IDM_EDIT_SAVEFIND);
			LPCEDITFINDREPLACE lpefr = (LPCEDITFINDREPLACE)GetWindowLongPtr(hwnd, DWLP_USER);
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8);
			CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
			CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_UNCHECKED);
			PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, IDC_FINDTEXT)), TRUE);
		}
		break;

		case IDC_TOGGLEFINDREPLACE: {
			bSwitchedFindReplace |= 2;
			LPEDITFINDREPLACE lpefr = (LPEDITFINDREPLACE)GetWindowLongPtr(hwnd, DWLP_USER);
			GetDlgPos(hwnd, &xFindReplaceDlgSave, &yFindReplaceDlgSave);
			memcpy(&efrSave, lpefr, sizeof(EDITFINDREPLACE));
			GetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8, COUNTOF(lpefr->szFindUTF8));
			if (!GetDlgItemTextA2W(CP_UTF8, hwnd, IDC_REPLACETEXT, lpefr->szReplaceUTF8, COUNTOF(lpefr->szReplaceUTF8))) {
				strcpy(lpefr->szReplaceUTF8, "");
			}
		}
		break;

		case IDC_USEMONOSPACEDFONT:
			bFindReplaceUseMonospacedFont = IsButtonChecked(hwnd, IDC_USEMONOSPACEDFONT);
			FindReplaceSetFont(hwnd, bFindReplaceUseMonospacedFont, &hFontFindReplaceEdit);
			break;
		}
		return TRUE;

	case WM_NOTIFY: {
		LPNMHDR pnmhdr = (LPNMHDR)lParam;
		switch (pnmhdr->code) {
		case NM_CLICK:
		case NM_RETURN:
			switch (pnmhdr->idFrom) {
			case IDC_TOGGLEFINDREPLACE:
				bSwitchedFindReplace = 1;
				if (GetDlgItem(hwnd, IDC_REPLACETEXT)) {
					PostWMCommand(hwndMain, IDM_EDIT_FIND);
				} else {
					PostWMCommand(hwndMain, IDM_EDIT_REPLACE);
				}
				break;

			// Display help messages in the find/replace windows
			case IDC_BACKSLASHHELP:
				MsgBoxInfo(MB_OK, IDS_BACKSLASHHELP);
				//ShowNotificationMessage(SC_NOTIFICATIONPOSITION_CENTER, IDS_BACKSLASHHELP);
				break;

			case IDC_REGEXPHELP:
				MsgBoxInfo(MB_OK, IDS_REGEXPHELP);
				//ShowNotificationMessage(SC_NOTIFICATIONPOSITION_CENTER, IDS_REGEXPHELP);
				break;

			case IDC_WILDCARDHELP:
				MsgBoxInfo(MB_OK, IDS_WILDCARDHELP);
				//ShowNotificationMessage(SC_NOTIFICATIONPOSITION_CENTER, IDS_WILDCARDHELP);
				break;

			case IDC_CLEAR_FIND:
			case IDC_CLEAR_REPLACE: {
				HWND hwndFind = GetDlgItem(hwnd, (pnmhdr->idFrom == IDC_CLEAR_FIND) ? IDC_FINDTEXT : IDC_REPLACETEXT);
				ComboBox_GetText(hwndFind, tch, COUNTOF(tch));
				ComboBox_ResetContent(hwndFind);
				MRU_Empty((pnmhdr->idFrom == IDC_CLEAR_FIND) ? &mruFind : &mruReplace, true);
				ComboBox_SetText(hwndFind, tch);
			}
			break;

			case IDC_SAVEPOSITION:
				GetDlgPos(hwnd, &xFindReplaceDlg, &yFindReplaceDlg);
				break;

			case IDC_RESETPOSITION:
				CenterDlgInParent(hwnd);
				xFindReplaceDlg = yFindReplaceDlg = 0;
				break;
			}
			break;
		}
	}
	break;

	case WM_ACTIVATE :
		SetWindowTransparentMode(hwnd, (LOWORD(wParam) == WA_INACTIVE && bFindReplaceTransparentMode), iFindReplaceOpacityLevel);
		break;
	}

	return FALSE;
}

//=============================================================================
//
// EditFindReplaceDlg()
//
HWND EditFindReplaceDlg(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bReplace) {
	lpefr->hwnd = hwnd;
	HWND hDlg = CreateThemedDialogParam(g_hInstance,
								   (bReplace) ? MAKEINTRESOURCE(IDD_REPLACE) : MAKEINTRESOURCE(IDD_FIND),
								   GetParent(hwnd),
								   EditFindReplaceDlgProc,
								   (LPARAM)lpefr);

	ShowWindow(hDlg, SW_SHOW);
	return hDlg;
}

// Wildcard search uses the regexp engine to perform a simple search with * ?
// as wildcards instead of more advanced and user-unfriendly regexp syntax.
static void EscapeWildcards(char *szFind2) {
	char szWildcardEscaped[NP2_FIND_REPLACE_LIMIT];
	int iSource = 0;
	int iDest = 0;

	while (szFind2[iSource]) {
		const char c = szFind2[iSource];
		if (c == '*') {
			szWildcardEscaped[iDest++] = '.';
			szWildcardEscaped[iDest] = '*';
		} else if (c == '?') {
			szWildcardEscaped[iDest] = '.';
		} else {
			if (c == '.' || c == '^' || c == '$' || c == '\\' || c == '[' || c == ']' || c == '+') {
				szWildcardEscaped[iDest++] = '\\';
			}
			szWildcardEscaped[iDest] = c;
		}
		iSource++;
		iDest++;
	}
	szWildcardEscaped[iDest] = 0;
	strncpy(szFind2, szWildcardEscaped, COUNTOF(szWildcardEscaped));
}

int EditPrepareFind(char *szFind2, LPCEDITFINDREPLACE lpefr) {
	if (StrIsEmptyA(lpefr->szFind)) {
		return NP2_InvalidSearchFlags;
	}

	int searchFlags = lpefr->fuFlags;
	strncpy(szFind2, lpefr->szFind, NP2_FIND_REPLACE_LIMIT);
	if (lpefr->bTransformBS) {
		const UINT cpEdit = SciCall_GetCodePage();
		TransformBackslashes(szFind2, (searchFlags & SCFIND_REGEXP), cpEdit);
	}
	if (StrIsEmptyA(szFind2)) {
		InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
		return NP2_InvalidSearchFlags;
	}
	if (lpefr->bWildcardSearch) {
		EscapeWildcards(szFind2);
		searchFlags |= SCFIND_REGEXP;
	} else if (!(searchFlags & (SCFIND_REGEXP | SCFIND_MATCHCASE))) {
		const BOOL sensitive = IsStringCaseSensitiveA(szFind2);
		//printf("%s sensitive=%d\n", __func__, sensitive);
		searchFlags |= ((sensitive - TRUE) & SCFIND_MATCHCASE);
	}
	return searchFlags;
}

int EditPrepareReplace(HWND hwnd, char *szFind2, char **pszReplace2, BOOL *bReplaceRE, LPCEDITFINDREPLACE lpefr) {
	const int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return searchFlags;
	}

	*bReplaceRE = (searchFlags & SCFIND_REGEXP);
	if (StrEqualExA(lpefr->szReplace, "^c")) {
		*bReplaceRE = FALSE;
		*pszReplace2 = EditGetClipboardText(hwnd);
	} else {
		*pszReplace2 = StrDupA(lpefr->szReplace);
		if (lpefr->bTransformBS) {
			const UINT cpEdit = SciCall_GetCodePage();
			TransformBackslashes(*pszReplace2, *bReplaceRE, cpEdit);
		}
	}

	if (*pszReplace2 == NULL) {
		*pszReplace2 = StrDupA("");
	}
	return searchFlags;
}

//=============================================================================
//
// EditFindNext()
//
void EditFindNext(LPCEDITFINDREPLACE lpefr, bool fExtendSelection) {
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	const int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return;
	}

	const Sci_Position iSelPos = SciCall_GetCurrentPos();
	const Sci_Position iSelAnchor = SciCall_GetAnchor();

	struct Sci_TextToFindFull ttf = { { SciCall_GetSelectionEnd(), SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iPos = SciCall_FindTextFull(searchFlags, &ttf);
	bool bSuppressNotFound = false;

	if (iPos < 0 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap && !fExtendSelection) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap1", IDS_FIND_WRAPFW)) {
			ttf.chrg.cpMin = 0;
			iPos = SciCall_FindTextFull(searchFlags, &ttf);
		} else {
			bSuppressNotFound = true;
		}
	}

	if (iPos < 0) {
		// not found
		if (!bSuppressNotFound) {
			InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
		}
	} else {
		if (!fExtendSelection) {
			EditSelectEx(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
		} else {
			EditSelectEx(min_pos(iSelAnchor, iSelPos), ttf.chrgText.cpMax);
		}
	}

}

//=============================================================================
//
// EditFindPrev()
//
void EditFindPrev(LPCEDITFINDREPLACE lpefr, bool fExtendSelection) {
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	const int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return;
	}

	const Sci_Position iSelPos = SciCall_GetCurrentPos();
	const Sci_Position iSelAnchor = SciCall_GetAnchor();

	struct Sci_TextToFindFull ttf = { { SciCall_GetSelectionStart(), 0 }, szFind2, { 0, 0 } };
	Sci_Position iPos = SciCall_FindTextFull(searchFlags, &ttf);
	const Sci_Position iLength = SciCall_GetLength();
	bool bSuppressNotFound = false;

	if (iPos < 0 && ttf.chrg.cpMin < iLength && !lpefr->bNoFindWrap && !fExtendSelection) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap2", IDS_FIND_WRAPRE)) {
			ttf.chrg.cpMin = iLength;
			iPos = SciCall_FindTextFull(searchFlags, &ttf);
		} else {
			bSuppressNotFound = true;
		}
	}

	if (iPos < 0) {
		// not found
		if (!bSuppressNotFound) {
			InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
		}
	} else {
		if (!fExtendSelection) {
			EditSelectEx(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
		} else {
			EditSelectEx(max_pos(iSelPos, iSelAnchor), ttf.chrgText.cpMin);
		}
	}
}

//=============================================================================
//
// EditReplace()
//
bool EditReplace(HWND hwnd, LPCEDITFINDREPLACE lpefr) {
	BOOL bReplaceRE;
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	char *pszReplace2;
	const int searchFlags = EditPrepareReplace(hwnd, szFind2, &pszReplace2, &bReplaceRE, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return false;
	}

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	struct Sci_TextToFindFull ttf = { { iSelStart, SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iPos = SciCall_FindTextFull(searchFlags, &ttf);
	bool bSuppressNotFound = false;

	if (iPos < 0 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap1", IDS_FIND_WRAPFW)) {
			ttf.chrg.cpMin = 0;
			iPos = SciCall_FindTextFull(searchFlags, &ttf);
		} else {
			bSuppressNotFound = true;
		}
	}

	if (iPos < 0) {
		// not found
		LocalFree(pszReplace2);
		if (!bSuppressNotFound) {
			InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
		}
		return false;
	}

	if (iSelStart != ttf.chrgText.cpMin || iSelEnd != ttf.chrgText.cpMax) {
		LocalFree(pszReplace2);
		EditSelectEx(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
		return false;
	}

	SciCall_SetTargetRange(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
	SciCall_ReplaceTargetEx(bReplaceRE, -1, pszReplace2);

	ttf.chrg.cpMin = SciCall_GetTargetEnd();
	ttf.chrg.cpMax = SciCall_GetLength();

	iPos = SciCall_FindTextFull(searchFlags, &ttf);
	bSuppressNotFound = false;

	if (iPos < 0 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap1", IDS_FIND_WRAPFW)) {
			ttf.chrg.cpMin = 0;
			iPos = SciCall_FindTextFull(searchFlags, &ttf);
		} else {
			bSuppressNotFound = true;
		}
	}

	if (iPos >= 0) {
		EditSelectEx(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
	} else {
		iPos = SciCall_GetTargetEnd();
		EditSelectEx(iPos, iPos); //?
		if (!bSuppressNotFound) {
			InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
		}
	}

	LocalFree(pszReplace2);
	return true;
}

//=============================================================================
//
// EditMarkAll()
// Mark all occurrences of the text currently selected (originally by Aleksandar Lekov)
//

extern EditMarkAllStatus editMarkAllStatus;
extern HANDLE idleTaskTimer;
#define EditMarkAll_MeasuredSize		(1024*1024)
#define EditMarkAll_MinDuration			1
// (100 / 64) => 2 MiB on second search.
// increment search size will return to normal after several runs
// when selection no longer changed, this make continuous selecting smooth.
#define EditMarkAll_DefaultDuration		64
#define EditMarkAll_RangeCacheCount		256
//static UINT EditMarkAll_Runs;

void EditMarkAll_ClearEx(int findFlag, Sci_Position iSelCount, LPSTR pszText) {
	if (editMarkAllStatus.matchCount != 0) {
		// clear existing indicator
		SciCall_SetIndicatorCurrent(IndicatorNumber_MarkOccurrence);
		SciCall_IndicatorClearRange(0, SciCall_GetLength());
		UpdateStatusBarCache(StatusItem_Find);
	}
	if (editMarkAllStatus.bookmarkLine >= 0 || editMarkAllStatus.bookmarkForFindAll) {
		if ((findFlag | editMarkAllStatus.findFlag) & NP2_MarkAllBookmark) {
			if ((findFlag & (NP2_MarkAllBookmark | NP2_FromFindAll)) != 0 || !editMarkAllStatus.bookmarkForFindAll) {
				SciCall_MarkerDeleteAll(MarkerNumber_Bookmark);
			}
		}
	}
	if (editMarkAllStatus.pszText) {
		NP2HeapFree(editMarkAllStatus.pszText);
	}

	editMarkAllStatus.pending = false;
	editMarkAllStatus.ignoreSelectionUpdate = false;
	editMarkAllStatus.findFlag = findFlag;
	editMarkAllStatus.incrementSize = 1;
	editMarkAllStatus.iSelCount= iSelCount;
	editMarkAllStatus.pszText = pszText;
	// timing for increment search is only useful for current search.
	editMarkAllStatus.duration = EditMarkAll_DefaultDuration;
	editMarkAllStatus.matchCount = 0;
	editMarkAllStatus.lastMatchPos = 0;
	editMarkAllStatus.iStartPos = 0;
	editMarkAllStatus.bookmarkLine = -1;
}

void EditMarkAll_Start(BOOL bChanged, int findFlag, Sci_Position iSelCount, LPSTR pszText) {
	if (!bChanged && (findFlag == editMarkAllStatus.findFlag
		&& iSelCount == editMarkAllStatus.iSelCount
		// _stricmp() is not safe for DBCS string.
		&& memcmp(pszText, editMarkAllStatus.pszText, iSelCount) == 0)) {
		NP2HeapFree(pszText);
		return;
	}

	EditMarkAll_ClearEx(findFlag, iSelCount, pszText);
	if ((findFlag & SCFIND_REGEXP) && iSelCount == 1) {
		const char ch = *pszText;
		if (ch == '^' || ch == '$') {
			const Sci_Line lineCount = SciCall_GetLineCount();
			editMarkAllStatus.matchCount = lineCount - (ch == '^');
			UpdateStatusBarCache(StatusItem_Find);
			UpdateStatusbar();
			return;
		}
	}

	//EditMarkAll_Runs = 0;
	if (findFlag & NP2_MarkAllBookmark) {
		editMarkAllStatus.bookmarkForFindAll = (findFlag & NP2_FromFindAll) != 0;
		Style_SetBookmark();
	}
	EditMarkAll_Continue(&editMarkAllStatus, idleTaskTimer);
}

static Sci_Line EditMarkAll_Bookmark(Sci_Line bookmarkLine, const Sci_Position *ranges, UINT index, int findFlag, Sci_Position matchCount) {
	if (findFlag & NP2_MarkAllSelectAll) {
		UINT i = 0;
		if (matchCount == (Sci_Position)(index/2)) {
			i = 2;
			SciCall_SetSelection(ranges[0] + ranges[1], ranges[0]);
		}
		for (; i < index; i += 2) {
			SciCall_AddSelection(ranges[i] + ranges[i + 1], ranges[i]);
		}
	} else {
		for (UINT i = 0; i < index; i += 2) {
			SciCall_IndicatorFillRange(ranges[i], ranges[i + 1]);
		}
	}
	if (!(findFlag & NP2_MarkAllBookmark)) {
		return bookmarkLine;
	}
	if (findFlag & NP2_MarkAllMultiline) {
		for (UINT i = 0; i < index; i += 2) {
			Sci_Line line = SciCall_LineFromPosition(ranges[i]);
			const Sci_Line lineEnd = SciCall_LineFromPosition(ranges[i] + ranges[i + 1]);
			line = max_pos(bookmarkLine + 1, line);
			while (line <= lineEnd) {
				SciCall_MarkerAdd(line, MarkerNumber_Bookmark);
				++line;
			}
			bookmarkLine = lineEnd;
		}
	} else {
		for (UINT i = 0; i < index; i += 2) {
			const Sci_Line line = SciCall_LineFromPosition(ranges[i]);
			if (line != bookmarkLine) {
				SciCall_MarkerAdd(line, MarkerNumber_Bookmark);
				bookmarkLine = line;
			}
		}
	}
	return bookmarkLine;
}

void EditMarkAll_Continue(EditMarkAllStatus *status, HANDLE timer) {
	// use increment search to ensure FindText() terminated in expected time.
	//++EditMarkAll_Runs;
	//printf("match %3u %s\n", EditMarkAll_Runs, GetCurrentLogTime());
	QueryPerformanceCounter(&status->watch.begin);
	const Sci_Position iLength = SciCall_GetLength();
	Sci_Position iStartPos = status->iStartPos;
	Sci_Position iMaxLength = status->incrementSize * EditMarkAll_MeasuredSize;
	iMaxLength += iStartPos + status->iSelCount;
	iMaxLength = min_pos(iMaxLength, iLength);
	if (iMaxLength < iLength) {
		// match on whole line to avoid rewinding.
		iMaxLength = SciCall_PositionFromLine(SciCall_LineFromPosition(iMaxLength) + 1);
		if (iMaxLength + EditMarkAll_MeasuredSize >= iLength) {
			iMaxLength = iLength;
		}
	}

	// rewind start position
	const int findFlag = status->findFlag;
	if (findFlag & NP2_MarkAllMultiline) {
		iStartPos = max_pos(iStartPos - status->iSelCount + 1, status->lastMatchPos);
	}

	Sci_Position cpMin = iStartPos;
	struct Sci_TextToFindFull ttf = { { cpMin, iMaxLength }, status->pszText, { 0, 0 } };

	Sci_Position matchCount = status->matchCount;
	UINT index = 0;
	Sci_Position ranges[EditMarkAll_RangeCacheCount*2];
	Sci_Line bookmarkLine = status->bookmarkLine;

	SciCall_SetIndicatorCurrent(IndicatorNumber_MarkOccurrence);
	WaitableTimer_Set(timer, WaitableTimer_IdleTaskTimeSlot);
	while (cpMin < iMaxLength && WaitableTimer_Continue(timer)) {
		ttf.chrg.cpMin = cpMin;
		const Sci_Position iPos = SciCall_FindTextFull(findFlag, &ttf);
		if (iPos < 0) {
			iStartPos = iMaxLength;
			break;
		}

		++matchCount;
		const Sci_Position iSelCount = ttf.chrgText.cpMax - iPos;
		if (iSelCount == 0) {
			// empty regex
			cpMin = SciCall_PositionAfter(iPos);
			continue;
		}

		if (index != 0 && iPos == cpMin && (findFlag & NP2_MarkAllSelectAll) == 0) {
			// merge adjacent indicator ranges
			ranges[index - 1] += iSelCount;
		} else {
			ranges[index] = iPos;
			ranges[index + 1] = iSelCount;
			index += 2;
			if (index == COUNTOF(ranges)) {
				bookmarkLine = EditMarkAll_Bookmark(bookmarkLine, ranges, index, findFlag, matchCount);
				index = 0;
			}
		}
		cpMin = ttf.chrgText.cpMax;
	}
	if (index) {
		bookmarkLine = EditMarkAll_Bookmark(bookmarkLine, ranges, index, findFlag, matchCount);
	}

	iStartPos = max_pos(iStartPos, cpMin);
	const bool pending = iStartPos < iLength;
	if (pending) {
		// dynamic compute increment search size, see ActionDuration in Scintilla.
		QueryPerformanceCounter(&status->watch.end);
		const double period = StopWatch_Get(&status->watch);
		iMaxLength = iStartPos - status->iStartPos;
		const double durationOne = (EditMarkAll_MeasuredSize * period) / iMaxLength;
		const double alpha = 0.25;
		const double duration_ = alpha * durationOne + (1.0 - alpha) * status->duration;
		const double duration = max_d(duration_, EditMarkAll_MinDuration);
		const int incrementSize = 1 + (int)(WaitableTimer_IdleTaskTimeSlot / duration);
		//printf("match %3u (%zd, %zd) length=%.3f / %zd, one=%.3f, duration=%.3f / %.3f, increment=%d\n", EditMarkAll_Runs,
		//	status->iStartPos, iStartPos, period, iMaxLength, durationOne, duration, duration_, incrementSize);
		status->incrementSize = incrementSize;
		status->duration = duration;
	}

	status->pending = pending;
	status->ignoreSelectionUpdate = matchCount && (findFlag & NP2_MarkAllSelectAll);
	status->lastMatchPos = cpMin;
	status->iStartPos = iStartPos;
	status->bookmarkLine = bookmarkLine;
	if (!pending || matchCount != status->matchCount) {
		status->matchCount = matchCount;
		UpdateStatusBarCache(StatusItem_Find);
		UpdateStatusbar();
	}
}

void EditMarkAll(BOOL bChanged, bool matchCase, bool wholeWord, bool bookmark) {
	// get current selection
	Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	Sci_Position iSelCount = iSelEnd - iSelStart;

	// if nothing selected or multiple lines are selected exit
	if (iSelCount == 0 || SciCall_LineFromPosition(iSelStart) != SciCall_LineFromPosition(iSelEnd)) {
		EditMarkAll_Clear();
		return;
	}

	iSelCount = SciCall_GetSelTextLength();
	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1);
	SciCall_GetSelText(pszText);

	// exit if selection is not a word and Match whole words only is enabled
	if (wholeWord) {
		const UINT cpEdit = SciCall_GetCodePage();
		const bool dbcs = !(cpEdit == CP_UTF8 || cpEdit == 0);
		// CharClassify::SetDefaultCharClasses()
		for (iSelStart = 0; iSelStart < iSelCount; ++iSelStart) {
			const unsigned char ch = pszText[iSelStart];
			if (dbcs && IsDBCSLeadByteEx(cpEdit, ch)) {
				++iSelStart;
			} else if (!IsDocWordChar(ch)) {
				NP2HeapFree(pszText);
				EditMarkAll_Clear();
				return;
			}
		}
	}
	if (!matchCase) {
		const bool sensitive = IsStringCaseSensitiveA(pszText);
		//printf("%s sensitive=%d\n", __func__, sensitive);
		matchCase = !sensitive;
	}

	const int findFlag = (((int)matchCase) * SCFIND_MATCHCASE)
		| (((int)wholeWord) * SCFIND_WHOLEWORD)
		| (((int)bookmark) * NP2_MarkAllBookmark);
	EditMarkAll_Start(bChanged, findFlag, iSelCount, pszText);
}

void EditFindAll(LPCEDITFINDREPLACE lpefr, bool selectAll) {
	char *szFind2 = (char *)NP2HeapAlloc(NP2_FIND_REPLACE_LIMIT);
	int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		NP2HeapFree(szFind2);
		return;
	}

	searchFlags |= (((int)bFindReplaceFindAllBookmark) * NP2_MarkAllBookmark)
		| (((int)selectAll) * NP2_MarkAllSelectAll)
		| NP2_FromFindAll;
	// rewind start position when transform backslash is checked,
	// all other searching doesn't across lines.
	// NOTE: complex fix is needed when multiline regex is supported.
	if (lpefr->bTransformBS && strpbrk(szFind2, "\r\n") != NULL) {
		searchFlags |= NP2_MarkAllMultiline;
	}
	EditMarkAll_Start(FALSE, searchFlags, strlen(szFind2), szFind2);
}

void EditToggleBookmarkAt(Sci_Position iPos) {
	if (iPos < 0) {
		iPos = SciCall_GetCurrentPos();
	}

	const Sci_Line iLine = SciCall_LineFromPosition(iPos);
	const Sci_MarkerMask bitmask = SciCall_MarkerGet(iLine);
	if (bitmask & MarkerBitmask_Bookmark) {
		SciCall_MarkerDelete(iLine, MarkerNumber_Bookmark);
	} else {
		Style_SetBookmark();
		SciCall_MarkerAdd(iLine, MarkerNumber_Bookmark);
	}
}

void EditBookmarkSelectAll(void) {
	Sci_Line line = SciCall_MarkerNext(0, MarkerBitmask_Bookmark);
	if (line >= 0) {
		editMarkAllStatus.ignoreSelectionUpdate = true;
		const Sci_Line iCurLine = SciCall_LineFromPosition(SciCall_GetCurrentPos());
		SciCall_SetSelection(SciCall_PositionFromLine(line), SciCall_PositionFromLine(line + 1));
		// set main selection near current line to ensure caret is visible after delete selected lines.
		size_t main = 0;
		size_t selection = 0;
		Sci_Line minDiff = abs_pos(line - iCurLine);
		while ((line = SciCall_MarkerNext(line + 1, MarkerBitmask_Bookmark)) >= 0) {
			SciCall_AddSelection(SciCall_PositionFromLine(line), SciCall_PositionFromLine(line + 1));
			++selection;
			const Sci_Line diff = abs_pos(line - iCurLine);
			if (diff < minDiff) {
				minDiff = diff;
				main = selection;
			}
		}
		SciCall_SetMainSelection(main);
	}
}

static void ShwowReplaceCount(Sci_Position iCount) {
	if (iCount > 0) {
		WCHAR tchNum[32];
		FormatNumber(tchNum, iCount);
		InfoBoxInfo(MB_OK, L"MsgReplaceCount", IDS_REPLCOUNT, tchNum);
	} else {
		InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
	}
}

//=============================================================================
//
// EditReplaceAll()
//
bool EditReplaceAll(HWND hwnd, LPCEDITFINDREPLACE lpefr, bool bShowInfo) {
	BOOL bReplaceRE;
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	char *pszReplace2;
	const int searchFlags = EditPrepareReplace(hwnd, szFind2, &pszReplace2, &bReplaceRE, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return false;
	}

	// Show wait cursor...
	BeginWaitCursor();
	SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
#if 0
	StopWatch watch;
	StopWatch_Start(watch);
#endif

	const bool bRegexStartOfLine = bReplaceRE && (szFind2[0] == '^');
	struct Sci_TextToFindFull ttf = { { 0, SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iCount = 0;
	while (SciCall_FindTextFull(searchFlags, &ttf) >= 0) {
		if (++iCount == 1) {
			SciCall_BeginUndoAction();
		}

		SciCall_SetTargetRange(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
		const Sci_Position iReplacedLen = SciCall_ReplaceTargetEx(bReplaceRE, -1, pszReplace2);

		ttf.chrg.cpMin = (ttf.chrgText.cpMin + iReplacedLen);
		// document length change: iReplacedLen - (ttf.chrgText.cpMax - ttf.chrgText.cpMin)
		ttf.chrg.cpMax += ttf.chrg.cpMin - ttf.chrgText.cpMax;

		if (ttf.chrg.cpMin == ttf.chrg.cpMax) {
			break;
		}

		if (ttf.chrgText.cpMin == ttf.chrgText.cpMax && !bRegexStartOfLine) {
			// move to next line after the replacement.
			ttf.chrg.cpMin = SciCall_PositionAfter(ttf.chrg.cpMin);
		}

		if (bRegexStartOfLine) {
			const Sci_Line iLine = SciCall_LineFromPosition(ttf.chrg.cpMin);
			const Sci_Position ilPos = SciCall_PositionFromLine(iLine);

			if (ilPos == ttf.chrg.cpMin) {
				ttf.chrg.cpMin = SciCall_PositionFromLine(iLine + 1);
			}
			if (ttf.chrg.cpMin == ttf.chrg.cpMax) {
				break;
			}
		}
	}

#if 0
	StopWatch_Stop(watch);
	StopWatch_ShowLog(&watch, "EditReplaceAll() time");
#endif
	SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
	if (iCount) {
		EditEnsureSelectionVisible();
		SciCall_EndUndoAction();
		InvalidateRect(hwnd, NULL, TRUE);
	}

	// Remove wait cursor
	EndWaitCursor();

	if (bShowInfo) {
		ShwowReplaceCount(iCount);
	}

	LocalFree(pszReplace2);
	return true;
}

//=============================================================================
//
// EditReplaceAllInSelection()
//
bool EditReplaceAllInSelection(HWND hwnd, LPCEDITFINDREPLACE lpefr, bool bShowInfo) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return false;
	}

	BOOL bReplaceRE;
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	char *pszReplace2;
	const int searchFlags = EditPrepareReplace(hwnd, szFind2, &pszReplace2, &bReplaceRE, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return false;
	}

	// Show wait cursor...
	BeginWaitCursor();
	SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);

	const bool bRegexStartOfLine = bReplaceRE && (szFind2[0] == '^');
	struct Sci_TextToFindFull ttf = { { SciCall_GetSelectionStart(), SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iCount = 0;
	while (SciCall_FindTextFull(searchFlags, &ttf) >= 0) {
		if (ttf.chrgText.cpMax <= SciCall_GetSelectionEnd()) {
			if (++iCount == 1) {
				SciCall_BeginUndoAction();
			}

			SciCall_SetTargetRange(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
			const Sci_Position iReplacedLen = SciCall_ReplaceTargetEx(bReplaceRE, -1, pszReplace2);

			ttf.chrg.cpMin = (ttf.chrgText.cpMin + iReplacedLen);
			// document length change: iReplacedLen - (ttf.chrgText.cpMax - ttf.chrgText.cpMin)
			ttf.chrg.cpMax += ttf.chrg.cpMin - ttf.chrgText.cpMax;

			if (ttf.chrg.cpMin == ttf.chrg.cpMax) {
				break;
			}

			if (ttf.chrgText.cpMin == ttf.chrgText.cpMax && !bRegexStartOfLine) {
				// move to next line after the replacement.
				ttf.chrg.cpMin = SciCall_PositionAfter(ttf.chrg.cpMin);
			}

			if (bRegexStartOfLine) {
				const Sci_Line iLine = SciCall_LineFromPosition(ttf.chrg.cpMin);
				const Sci_Position ilPos = SciCall_PositionFromLine(iLine);

				if (ilPos == ttf.chrg.cpMin) {
					ttf.chrg.cpMin = SciCall_PositionFromLine(iLine + 1);
				}
				if (ttf.chrg.cpMin == ttf.chrg.cpMax) {
					break;
				}
			}
		} else { // gone across selection, cancel
			break;
		}
	}

	SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
	if (iCount) {
		const Sci_Position iPos = SciCall_GetTargetEnd();
		if (SciCall_GetSelectionEnd() < iPos) {
			Sci_Position iAnchorPos = SciCall_GetAnchor();
			Sci_Position iCurrentPos = SciCall_GetCurrentPos();

			if (iAnchorPos > iCurrentPos) {
				iAnchorPos = iPos;
			} else {
				iCurrentPos = iPos;
			}

			EditSelectEx(iAnchorPos, iCurrentPos);
		}

		SciCall_EndUndoAction();
		InvalidateRect(hwnd, NULL, TRUE);
	}

	// Remove wait cursor
	EndWaitCursor();

	if (bShowInfo) {
		ShwowReplaceCount(iCount);
	}

	LocalFree(pszReplace2);
	return true;
}

//=============================================================================
//
// EditLineNumDlgProc()
//
static INT_PTR CALLBACK EditLineNumDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	UNREFERENCED_PARAMETER(lParam);

	switch (umsg) {
	case WM_INITDIALOG: {
		const Sci_Line iCurLine = SciCall_LineFromPosition(SciCall_GetCurrentPos()) + 1;
		const Sci_Line iMaxLine = SciCall_GetLineCount();
		const Sci_Position iLength = SciCall_GetLength();

		SendDlgItemMessage(hwnd, IDC_LINENUM, EM_LIMITTEXT, 20, 0);
		SendDlgItemMessage(hwnd, IDC_COLNUM, EM_LIMITTEXT, 20, 0);

		WCHAR tchLn[32];
		WCHAR tchLines[64];
		WCHAR tchFmt[64];

		PosToStrW(iCurLine, tchLn);
		SetDlgItemText(hwnd, IDC_LINENUM, tchLn);

		FormatNumber(tchLn, iMaxLine);
		GetDlgItemText(hwnd, IDC_LINE_RANGE, tchFmt, COUNTOF(tchFmt));
		wsprintf(tchLines, tchFmt, tchLn);
		SetDlgItemText(hwnd, IDC_LINE_RANGE, tchLines);

		FormatNumber(tchLn, iLength);
		GetDlgItemText(hwnd, IDC_COLUMN_RANGE, tchFmt, COUNTOF(tchFmt));
		wsprintf(tchLines, tchFmt, tchLn);
		SetDlgItemText(hwnd, IDC_COLUMN_RANGE, tchLines);

		CenterDlgInParent(hwnd);
	}
	return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			bool fTranslated;
			bool fTranslated2;
			WCHAR tchLn[32];
			Sci_Line iNewLine = 0;
			Sci_Position iNewCol = 0;

			// Extract line number from the text entered
			// For example: "5410:" will result in 5410
			GetDlgItemText(hwnd, IDC_LINENUM, tchLn, COUNTOF(tchLn));
#if defined(_WIN64)
			int64_t iLine = 0;
			fTranslated = CRTStrToInt64(tchLn, &iLine);
#else
			int iLine = 0;
			fTranslated = CRTStrToInt(tchLn, &iLine);
#endif
			iNewLine = iLine;

			if (SendDlgItemMessage(hwnd, IDC_COLNUM, WM_GETTEXTLENGTH, 0, 0) > 0) {
				GetDlgItemText(hwnd, IDC_COLNUM, tchLn, COUNTOF(tchLn));
#if defined(_WIN64)
				fTranslated2 = CRTStrToInt64(tchLn, &iLine);
#else
				fTranslated2 = CRTStrToInt(tchLn, &iLine);
#endif
				iNewCol = iLine;
			} else {
				iNewCol = 1;
				fTranslated2 = fTranslated;
			}

			if (!(fTranslated || fTranslated2)) {
				PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, IDC_LINENUM)), 1);
				return TRUE;
			}

			const Sci_Line iMaxLine = SciCall_GetLineCount();
			const Sci_Position iLength = SciCall_GetLength();
			// directly goto specific position
			if (fTranslated2 && !fTranslated) {
				if (iNewCol > 0 && iNewCol <= iLength) {
					--iNewCol;
					EditSelectEx(iNewCol, iNewCol);
					SciCall_ChooseCaretX();
					EndDialog(hwnd, IDOK);
				} else {
					PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, IDC_COLNUM)), TRUE);
				}
			} else if (iNewLine > 0 && iNewLine <= iMaxLine) {
				EditJumpTo(iNewLine, iNewCol);
				EndDialog(hwnd, IDOK);
			} else {
				PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, ((iNewCol > 0) ? IDC_LINENUM : IDC_COLNUM))), TRUE);
			}
		}
		break;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}

		return TRUE;
	}

	return FALSE;
}

//=============================================================================
//
// EditLinenumDlg()
//
bool EditLineNumDlg(HWND hwnd) {
	const INT_PTR iResult = ThemedDialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_LINENUM), GetParent(hwnd), EditLineNumDlgProc, (LPARAM)hwnd);
	return iResult == IDOK;
}

//=============================================================================
//
// EditModifyLinesDlg()
//
//
extern int cxModifyLinesDlg;
extern int cyModifyLinesDlg;
extern int cxEncloseSelectionDlg;
extern int cyEncloseSelectionDlg;
extern int cxInsertTagDlg;
extern int cyInsertTagDlg;

static INT_PTR CALLBACK EditModifyLinesDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	static DWORD id_hover;
	static DWORD id_capture;
	static bool skipEmptyLine;
	static HFONT hFontHover;

	switch (umsg) {
	case WM_INITDIALOG: {
		ResizeDlg_InitY2(hwnd, cxModifyLinesDlg, cyModifyLinesDlg, IDC_RESIZEGRIP2, IDC_MODIFY_LINE_PREFIX, IDC_MODIFY_LINE_APPEND);

		id_hover = 0;
		id_capture = 0;

		HFONT hFontNormal = (HFONT)SendDlgItemMessage(hwnd, IDC_MODIFY_LINE_DLN_NP, WM_GETFONT, 0, 0);
		if (hFontNormal == NULL) {
			hFontNormal = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		}

		LOGFONT lf;
		GetObject(hFontNormal, sizeof(LOGFONT), &lf);
		lf.lfUnderline = TRUE;
		hFontHover = CreateFontIndirect(&lf);

		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_PREFIX);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_PREFIX, wchPrefixLines.buffer);
		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_APPEND);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_APPEND, wchAppendLines.buffer);
		if (skipEmptyLine) {
			CheckDlgButton(hwnd, IDC_MODIFY_LINE_SKIP_EMPTY, BST_CHECKED);
		}
		CenterDlgInParent(hwnd);
	}
	return TRUE;

	case WM_DESTROY:
		ResizeDlg_Destroy(hwnd, &cxModifyLinesDlg, &cyModifyLinesDlg);
		DeleteObject(hFontHover);
		return FALSE;

	case WM_SIZE: {
		int dx;
		int dy;

		ResizeDlg_Size(hwnd, lParam, &dx, &dy);
		const int cy = ResizeDlg_CalcDeltaY2(hwnd, dy, 50, IDC_MODIFY_LINE_PREFIX, IDC_MODIFY_LINE_APPEND);
		HDWP hdwp = BeginDeferWindowPos(15);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_RESIZEGRIP2, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDOK, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDCANCEL, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_PREFIX, dx, cy, SWP_NOMOVE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_APPEND, 0, cy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_TIP2, 0, cy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_SKIP_EMPTY, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_DLN_NP, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_DLN_ZP, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_TIP_DLN, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_CN_NP, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_CN_ZP, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_TIP_CN, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_ZCN_NP, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_ZCN_ZP, 0, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_TIP_ZCN, 0, dy, SWP_NOSIZE);
		EndDeferWindowPos(hdwp);
		ResizeDlgCtl(hwnd, IDC_MODIFY_LINE_APPEND, dx, dy - cy);
	}
	return TRUE;

	case WM_GETMINMAXINFO:
		ResizeDlg_GetMinMaxInfo(hwnd, lParam);
		return TRUE;

	case WM_NCACTIVATE:
		if (!wParam) {
			if (id_hover != 0) {
				//int _id_hover = id_hover;
				id_hover = 0;
				id_capture = 0;
				//InvalidateRect(GetDlgItem(hwnd, id_hover), NULL, FALSE);
			}
		}
		return FALSE;

	case WM_CTLCOLORSTATIC: {
		const DWORD dwId = GetWindowLong((HWND)lParam, GWL_ID);

		if (dwId >= IDC_MODIFY_LINE_DLN_NP && dwId <= IDC_MODIFY_LINE_ZCN_ZP) {
			HDC hdc = (HDC)wParam;
			SetBkMode(hdc, TRANSPARENT);
			if (GetSysColorBrush(COLOR_HOTLIGHT)) {
				SetTextColor(hdc, GetSysColor(COLOR_HOTLIGHT));
			} else {
				SetTextColor(hdc, RGB(0, 0, 255));
			}
			SelectObject(hdc, /*dwId == id_hover?*/hFontHover/*:hFontNormal*/);
			return (LONG_PTR)GetSysColorBrush(COLOR_BTNFACE);
		}
	}
	break;

	case WM_MOUSEMOVE: {
		const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		HWND hwndHover = ChildWindowFromPoint(hwnd, pt);
		const DWORD dwId = GetWindowLong(hwndHover, GWL_ID);

		if (GetActiveWindow() == hwnd) {
			if (dwId >= IDC_MODIFY_LINE_DLN_NP && dwId <= IDC_MODIFY_LINE_ZCN_ZP) {
				if (id_capture == dwId || id_capture == 0) {
					if (id_hover != id_capture || id_hover == 0) {
						id_hover = dwId;
						//InvalidateRect(GetDlgItem(hwnd, dwId), NULL, FALSE);
					}
				} else if (id_hover != 0) {
					//int _id_hover = id_hover;
					id_hover = 0;
					//InvalidateRect(GetDlgItem(hwnd, _id_hover), NULL, FALSE);
				}
			} else if (id_hover != 0) {
				//int _id_hover = id_hover;
				id_hover = 0;
				//InvalidateRect(GetDlgItem(hwnd, _id_hover), NULL, FALSE);
			}
			SetCursor(LoadCursor(NULL, (id_hover ? IDC_HAND : IDC_ARROW)));
		}
	}
	break;

	case WM_LBUTTONDOWN: {
		const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		HWND hwndHover = ChildWindowFromPoint(hwnd, pt);
		const DWORD dwId = GetWindowLong(hwndHover, GWL_ID);

		if (dwId >= IDC_MODIFY_LINE_DLN_NP && dwId <= IDC_MODIFY_LINE_ZCN_ZP) {
			GetCapture();
			id_hover = dwId;
			id_capture = dwId;
			//InvalidateRect(GetDlgItem(hwnd, dwId), NULL, FALSE);
		}
		SetCursor(LoadCursor(NULL, (id_hover ? IDC_HAND : IDC_ARROW)));
	}
	break;

	case WM_LBUTTONUP: {
		//const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		//HWND hwndHover = ChildWindowFromPoint(hwnd, pt);
		//const DWORD dwId = GetWindowLong(hwndHover, GWL_ID);

		if (id_capture != 0) {
			ReleaseCapture();
			if (id_hover == id_capture) {
				const DWORD id_focus = GetWindowLong(GetFocus(), GWL_ID);
				if (id_focus == IDC_MODIFY_LINE_PREFIX || id_focus == IDC_MODIFY_LINE_APPEND) {
					WCHAR wch[8];
					GetDlgItemText(hwnd, id_capture, wch, COUNTOF(wch));
					SendDlgItemMessage(hwnd, id_focus, EM_SETSEL, 0, -1);
					SendDlgItemMessage(hwnd, id_focus, EM_REPLACESEL, TRUE, (LPARAM)wch);
					PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetFocus()), TRUE);
				}
			}
			id_capture = 0;
		}
		SetCursor(LoadCursor(NULL, (id_hover ? IDC_HAND : IDC_ARROW)));
	}
	break;

	case WM_CANCELMODE:
		if (id_capture != 0) {
			ReleaseCapture();
			id_hover = 0;
			id_capture = 0;
			SetCursor(LoadCursor(NULL, IDC_ARROW));
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			DStringW_GetDlgItemText(&wchPrefixLines, hwnd, IDC_MODIFY_LINE_PREFIX);
			DStringW_GetDlgItemText(&wchAppendLines, hwnd, IDC_MODIFY_LINE_APPEND);
			skipEmptyLine = IsButtonChecked(hwnd, IDC_MODIFY_LINE_SKIP_EMPTY);
			EditModifyLines(wchPrefixLines.buffer, wchAppendLines.buffer, skipEmptyLine);
			EndDialog(hwnd, IDOK);
		}
		break;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}
		return TRUE;
	}

	return FALSE;
}

//=============================================================================
//
// EditModifyLinesDlg()
//
void EditModifyLinesDlg(HWND hwnd) {
	ThemedDialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_MODIFYLINES), hwnd, EditModifyLinesDlgProc, 0);
}

//=============================================================================
//
// EditAlignDlgProc()
//
//
static INT_PTR CALLBACK EditAlignDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	switch (umsg) {
	case WM_INITDIALOG: {
		SetWindowLongPtr(hwnd, DWLP_USER, lParam);
		const int iAlignMode = *((int *)lParam);
		CheckRadioButton(hwnd, IDC_ALIGN_LEFT, IDC_ALIGN_JUSTIFY_PAR, iAlignMode + IDC_ALIGN_LEFT);
		CenterDlgInParent(hwnd);
	}
	return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			int *piAlignMode = (int *)GetWindowLongPtr(hwnd, DWLP_USER);
			const int iAlignMode = GetCheckedRadioButton(hwnd, IDC_ALIGN_LEFT, IDC_ALIGN_JUSTIFY_PAR) - IDC_ALIGN_LEFT;
			*piAlignMode = iAlignMode;
			EndDialog(hwnd, IDOK);
		}
		break;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}
		return TRUE;
	}

	return FALSE;
}

//=============================================================================
//
// EditAlignDlg()
//
bool EditAlignDlg(HWND hwnd, EditAlignMode *piAlignMode) {
	const INT_PTR iResult = ThemedDialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_ALIGN), hwnd, EditAlignDlgProc, (LPARAM)piAlignMode);
	return iResult == IDOK;
}

//=============================================================================
//
// EditEncloseSelectionDlgProc()
//
//
static INT_PTR CALLBACK EditEncloseSelectionDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	switch (umsg) {
	case WM_INITDIALOG: {
		ResizeDlg_InitY2(hwnd, cxEncloseSelectionDlg, cyEncloseSelectionDlg, IDC_RESIZEGRIP2, IDC_MODIFY_LINE_PREFIX, IDC_MODIFY_LINE_APPEND);

		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_PREFIX);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_PREFIX, wchPrefixSelection.buffer);
		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_APPEND);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_APPEND, wchAppendSelection.buffer);
		CenterDlgInParent(hwnd);
	}
	return TRUE;

	case WM_DESTROY:
		ResizeDlg_Destroy(hwnd, &cxEncloseSelectionDlg, &cyEncloseSelectionDlg);
		return FALSE;

	case WM_SIZE: {
		int dx;
		int dy;

		ResizeDlg_Size(hwnd, lParam, &dx, &dy);
		const int cy = ResizeDlg_CalcDeltaY2(hwnd, dy, 50, IDC_MODIFY_LINE_PREFIX, IDC_MODIFY_LINE_APPEND);
		HDWP hdwp = BeginDeferWindowPos(6);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_RESIZEGRIP2, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDOK, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDCANCEL, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_PREFIX, dx, cy, SWP_NOMOVE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_APPEND, 0, cy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_TIP2, 0, cy, SWP_NOSIZE);
		EndDeferWindowPos(hdwp);
		ResizeDlgCtl(hwnd, IDC_MODIFY_LINE_APPEND, dx, dy - cy);
	}
	return TRUE;

	case WM_GETMINMAXINFO:
		ResizeDlg_GetMinMaxInfo(hwnd, lParam);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			DStringW_GetDlgItemText(&wchPrefixSelection, hwnd, IDC_MODIFY_LINE_PREFIX);
			DStringW_GetDlgItemText(&wchAppendSelection, hwnd, IDC_MODIFY_LINE_APPEND);

			EditEncloseSelection(wchPrefixSelection.buffer, wchAppendSelection.buffer);
			EndDialog(hwnd, IDOK);
		}
		break;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}
		return TRUE;
	}

	return FALSE;
}

//=============================================================================
//
// EditEncloseSelectionDlg()
//
void EditEncloseSelectionDlg(HWND hwnd) {
	ThemedDialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_ENCLOSESELECTION), hwnd, EditEncloseSelectionDlgProc, 0);
}

//=============================================================================
//
// EditInsertTagDlgProc()
//
//
static INT_PTR CALLBACK EditInsertTagDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	switch (umsg) {
	case WM_INITDIALOG: {
		ResizeDlg_InitY2(hwnd, cxInsertTagDlg, cyInsertTagDlg, IDC_RESIZEGRIP2, IDC_MODIFY_LINE_PREFIX, IDC_MODIFY_LINE_APPEND);

		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_PREFIX);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_PREFIX, L"<tag>");
		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_APPEND);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_APPEND, L"</tag>");

		HWND hwndCtl = GetDlgItem(hwnd, IDC_MODIFY_LINE_PREFIX);
		SetFocus(hwndCtl);
		PostMessage(hwndCtl, EM_SETSEL, 1, 4);
		CenterDlgInParent(hwnd);
	}
	return FALSE;

	case WM_DESTROY:
		ResizeDlg_Destroy(hwnd, &cxInsertTagDlg, &cyInsertTagDlg);
		return FALSE;

	case WM_SIZE: {
		int dx;
		int dy;

		ResizeDlg_Size(hwnd, lParam, &dx, &dy);
		const int cy = ResizeDlg_CalcDeltaY2(hwnd, dy, 75, IDC_MODIFY_LINE_PREFIX, IDC_MODIFY_LINE_APPEND);
		HDWP hdwp = BeginDeferWindowPos(6);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_RESIZEGRIP2, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDOK, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDCANCEL, dx, dy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_PREFIX, dx, cy, SWP_NOMOVE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_APPEND, 0, cy, SWP_NOSIZE);
		hdwp = DeferCtlPos(hdwp, hwnd, IDC_MODIFY_LINE_TIP2, 0, cy, SWP_NOSIZE);
		EndDeferWindowPos(hdwp);
		ResizeDlgCtl(hwnd, IDC_MODIFY_LINE_APPEND, dx, dy - cy);
	}
	return TRUE;

	case WM_GETMINMAXINFO:
		ResizeDlg_GetMinMaxInfo(hwnd, lParam);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_MODIFY_LINE_PREFIX: {
			if (HIWORD(wParam) == EN_CHANGE) {
				DStringW wszOpen = DSTRINGW_INIT;
				bool bClear = true;

				DStringW_GetDlgItemText(&wszOpen, hwnd, IDC_MODIFY_LINE_PREFIX);
				const int len = lstrlen(wszOpen.buffer);
				if (len >= 3) {
					LPCWSTR pwCur = StrChr(wszOpen.buffer, L'<');
					if (pwCur != NULL) {
						LPWSTR wchIns = (LPWSTR)NP2HeapAlloc((len + 5) * sizeof(WCHAR));
						StrCpyExW(wchIns, L"</");
						int	cchIns = 2;

						++pwCur;
						while (IsHtmlTagChar(*pwCur)) {
							wchIns[cchIns++] = *pwCur++;
						}

						while (*pwCur && *pwCur != L'>') {
							pwCur++;
						}

						if (*pwCur == L'>' && *(pwCur - 1) != L'/') {
							wchIns[cchIns++] = L'>';
							wchIns[cchIns] = L'\0';

							if (cchIns > 3 && !(
									StrCaseEqual(wchIns, L"</base>") &&
									StrCaseEqual(wchIns, L"</bgsound>") &&
									StrCaseEqual(wchIns, L"</br>") &&
									StrCaseEqual(wchIns, L"</embed>") &&
									StrCaseEqual(wchIns, L"</hr>") &&
									StrCaseEqual(wchIns, L"</img>") &&
									StrCaseEqual(wchIns, L"</input>") &&
									StrCaseEqual(wchIns, L"</link>") &&
									StrCaseEqual(wchIns, L"</meta>"))) {

								SetDlgItemText(hwnd, IDC_MODIFY_LINE_APPEND, wchIns);
								bClear = false;
							}
						}
						NP2HeapFree(wchIns);
					}
				}

				if (bClear) {
					SetDlgItemText(hwnd, IDC_MODIFY_LINE_APPEND, L"");
				}
				DStringW_Free(&wszOpen);
			}
		}
		break;

		case IDOK: {
			DStringW wszOpen = DSTRINGW_INIT;
			DStringW wszClose = DSTRINGW_INIT;
			DStringW_GetDlgItemText(&wszOpen, hwnd, IDC_MODIFY_LINE_PREFIX);
			DStringW_GetDlgItemText(&wszClose, hwnd, IDC_MODIFY_LINE_APPEND);

			EditEncloseSelection(wszOpen.buffer, wszClose.buffer);
			DStringW_Free(&wszOpen);
			DStringW_Free(&wszClose);
			EndDialog(hwnd, IDOK);
		}
		break;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}
		return TRUE;
	}

	return FALSE;
}

//=============================================================================
//
// EditInsertTagDlg()
//
void EditInsertTagDlg(HWND hwnd) {
	ThemedDialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_INSERTTAG), hwnd, EditInsertTagDlgProc, 0);
}

static inline int GetDaylightSavingTimeFlag(void) {
	// https://en.cppreference.com/w/c/chrono/tm
	// https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-gettimezoneinformation
#if 0//_WIN32_WINNT >= _WIN32_WINNT_VISTA
	DYNAMIC_TIME_ZONE_INFORMATION info;
	return GetDynamicTimeZoneInformation(&info) - 1;
#else
	TIME_ZONE_INFORMATION info;
	return GetTimeZoneInformation(&info) - 1;
#endif
}

void EditInsertDateTime(bool bShort) {
	WCHAR tchDateTime[256];
	WCHAR tchTemplate[256];

	SYSTEMTIME st;
	GetLocalTime(&st);

	if (IniGetString(INI_SECTION_NAME_FLAGS, bShort ? L"DateTimeShort" : L"DateTimeLong", L"", tchTemplate, COUNTOF(tchTemplate))) {
		struct tm sst;
		sst.tm_isdst	= GetDaylightSavingTimeFlag();
		sst.tm_sec		= (int)st.wSecond;
		sst.tm_min		= (int)st.wMinute;
		sst.tm_hour		= (int)st.wHour;
		sst.tm_mday		= (int)st.wDay;
		sst.tm_mon		= (int)st.wMonth - 1;
		sst.tm_year		= (int)st.wYear - 1900;
		sst.tm_wday		= (int)st.wDayOfWeek;
#if NP2_DYNAMIC_LOAD_wcsftime
		wcsftimeSig pfn = GetFunctionPointer_wcsftime();
		pfn(tchDateTime, COUNTOF(tchDateTime), tchTemplate, &sst);
#else
		wcsftime(tchDateTime, COUNTOF(tchDateTime), tchTemplate, &sst);
#endif
	} else {
		WCHAR tchDate[128];
		WCHAR tchTime[128];
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
		GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, bShort ? DATE_SHORTDATE : DATE_LONGDATE, &st, NULL, tchDate, COUNTOF(tchDate), NULL);
		GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, tchTime, COUNTOF(tchTime));
#else
		GetDateFormat(LOCALE_USER_DEFAULT, bShort ? DATE_SHORTDATE : DATE_LONGDATE, &st, NULL, tchDate, COUNTOF(tchDate));
		GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, tchTime, COUNTOF(tchTime));
#endif
		wsprintf(tchDateTime, L"%s %s", tchTime, tchDate);
	}

	const UINT cpEdit = SciCall_GetCodePage();
	char mszBuf[256 * kMaxMultiByteCount];
	WideCharToMultiByte(cpEdit, 0, tchDateTime, -1, mszBuf, COUNTOF(mszBuf), NULL, NULL);
	SciCall_ReplaceSel(mszBuf);
}

void EditUpdateTimestampMatchTemplate(HWND hwnd) {
	WCHAR wchFind[256] = {0};
	IniGetString(INI_SECTION_NAME_FLAGS, L"TimeStamp", L"\\$Date:[^\\$]+\\$ | $Date: %Y/%m/%d %H:%M:%S $", wchFind, COUNTOF(wchFind));

	WCHAR wchTemplate[256] = {0};
	LPWSTR pwchSep = StrChr(wchFind, L'|');
	if (pwchSep != NULL) {
		lstrcpy(wchTemplate, pwchSep + 1);
		*pwchSep = L'\0';
	}

	StrTrim(wchFind, L" ");
	StrTrim(wchTemplate, L" ");

	if (StrIsEmpty(wchFind) || StrIsEmpty(wchTemplate)) {
		return;
	}

	SYSTEMTIME st;
	struct tm sst;
	GetLocalTime(&st);
	sst.tm_isdst = GetDaylightSavingTimeFlag();
	sst.tm_sec	 = (int)st.wSecond;
	sst.tm_min	 = (int)st.wMinute;
	sst.tm_hour	 = (int)st.wHour;
	sst.tm_mday	 = (int)st.wDay;
	sst.tm_mon	 = (int)st.wMonth - 1;
	sst.tm_year	 = (int)st.wYear - 1900;
	sst.tm_wday	 = (int)st.wDayOfWeek;

	WCHAR wchReplace[256];
#if NP2_DYNAMIC_LOAD_wcsftime
	wcsftimeSig pfn = GetFunctionPointer_wcsftime();
	pfn(wchReplace, COUNTOF(wchReplace), wchTemplate, &sst);
#else
	wcsftime(wchReplace, COUNTOF(wchReplace), wchTemplate, &sst);
#endif

	const UINT cpEdit = SciCall_GetCodePage();
	EDITFINDREPLACE efrTS;
	memset(&efrTS, 0, sizeof(efrTS));
	efrTS.hwnd = hwnd;
	efrTS.fuFlags = SCFIND_REGEXP;

	WideCharToMultiByte(cpEdit, 0, wchFind, -1, efrTS.szFind, COUNTOF(efrTS.szFind), NULL, NULL);
	WideCharToMultiByte(cpEdit, 0, wchReplace, -1, efrTS.szReplace, COUNTOF(efrTS.szReplace), NULL, NULL);

	if (!SciCall_IsSelectionEmpty()) {
		EditReplaceAllInSelection(hwnd, &efrTS, true);
	} else {
		EditReplaceAll(hwnd, &efrTS, true);
	}
}

typedef struct UnicodeControlCharacter {
	char uccUTF8[4];
	char representation[5];
} UnicodeControlCharacter;

// https://en.wikipedia.org/wiki/Unicode_control_characters
// https://www.unicode.org/charts/PDF/U2000.pdf
// scintilla/scripts/GenerateCharTable.py
static const UnicodeControlCharacter kUnicodeControlCharacterTable[] = {
	{ "\xe2\x80\x8e", "LRM" },	// U+200E	LRM		Left-to-right mark
	{ "\xe2\x80\x8f", "RLM" },	// U+200F	RLM		Right-to-left mark
	{ "\xe2\x80\x8d", "ZWJ" },	// U+200D	ZWJ		Zero width joiner
	{ "\xe2\x80\x8c", "ZWNJ" },	// U+200C	ZWNJ	Zero width non-joiner
	{ "\xe2\x80\xaa", "LRE" },	// U+202A	LRE		Start of left-to-right embedding
	{ "\xe2\x80\xab", "RLE" },	// U+202B	RLE		Start of right-to-left embedding
	{ "\xe2\x80\xad", "LRO" },	// U+202D	LRO		Start of left-to-right override
	{ "\xe2\x80\xae", "RLO" },	// U+202E	RLO		Start of right-to-left override
	{ "\xe2\x80\xac", "PDF" },	// U+202C	PDF		Pop directional formatting
	{ "\xe2\x81\xae", "NADS" },	// U+206E	NADS	National digit shapes substitution
	{ "\xe2\x81\xaf", "NODS" },	// U+206F	NODS	Nominal (European) digit shapes
	{ "\xe2\x81\xab", "ASS" },	// U+206B	ASS		Activate symmetric swapping
	{ "\xe2\x81\xaa", "ISS" },	// U+206A	ISS		Inhibit symmetric swapping
	{ "\xe2\x81\xad", "AAFS" },	// U+206D	AAFS	Activate Arabic form shaping
	{ "\xe2\x81\xac", "IAFS" },	// U+206C	IAFS	Inhibit Arabic form shaping
	// Scintilla built-in, Editor::SetRepresentations()
	{ "\x1e", "" },			// U+001E	RS		Record Separator (Block separator)
	{ "\x1f", "" },			// U+001F	US		Unit Separator (Segment separator)
	{ "\xe2\x80\xa8", "" },	// U+2028	LS		Line Separator
	{ "\xe2\x80\xa9", "" },	// U+2029	PS		Paragraph Separator
	// Other
	{ "\xe2\x80\x8b", "ZWSP" },	// U+200B	ZWSP	Zero width space
	{ "\xe2\x81\xa0", "WJ" },	// U+2060	WJ		Word joiner
	{ "\xe2\x81\xa6", "LRI" },	// U+2066	LRI		Left-to-right isolate
	{ "\xe2\x81\xa7", "RLI" },	// U+2067	RLI		Right-to-left isolate
	{ "\xe2\x81\xa8", "FSI" },	// U+2068	FSI		First strong isolate
	{ "\xe2\x81\xa9", "PDI" },	// U+2069	PDI		Pop directional isolate
	{ "\xd8\x9c", "ALM" },		// U+061C	ALM		Arabic letter mark
};

void EditInsertUnicodeControlCharacter(int menu) {
	menu = menu - IDM_INSERT_UNICODE_LRM;
	const UnicodeControlCharacter ucc = kUnicodeControlCharacterTable[menu];
	if (ucc.uccUTF8[1] != '\0' && iCurrentEncoding == CPI_DEFAULT) {
		// TODO: convert from UTF-8 to ANSI.
		return;
	}
	SciCall_ReplaceSel(ucc.uccUTF8);
}

void EditShowUnicodeControlCharacter(bool bShow) {
	for (UINT i = 0; i < COUNTOF(kUnicodeControlCharacterTable); i++) {
		const UnicodeControlCharacter ucc = kUnicodeControlCharacterTable[i];
		if (StrIsEmptyA(ucc.representation)) {
			// built-in
			continue;
		}
		if (bShow) {
			SciCall_SetRepresentation(ucc.uccUTF8, ucc.representation);
		} else {
			SciCall_ClearRepresentation(ucc.uccUTF8);
		}
	}
}

//=============================================================================
//
// EditSortDlgProc()
//
//
static INT_PTR CALLBACK EditSortDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	switch (umsg) {
	case WM_INITDIALOG: {
		SetWindowLongPtr(hwnd, DWLP_USER, lParam);
		int *piSortFlags = (int *)lParam;
		const int iSortFlags = *piSortFlags;

		if (iSortFlags & EditSortFlag_Shuffle) {
			CheckRadioButton(hwnd, IDC_SORT_NONE, IDC_SORT_SHUFFLE, IDC_SORT_SHUFFLE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_UNIQUE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_IGNORE_CASE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_LOGICAL_NUMBER), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_GROUPBY_FILE_TYPE), FALSE);
		} else {
			const int button = (iSortFlags & EditSortFlag_DontSort) ? IDC_SORT_NONE : (IDC_SORT_ASC + (iSortFlags & EditSortFlag_Descending));
			CheckRadioButton(hwnd, IDC_SORT_NONE, IDC_SORT_SHUFFLE, button);
		}

		if (iSortFlags & EditSortFlag_MergeDuplicate) {
			CheckDlgButton(hwnd, IDC_SORT_MERGE_DUP, BST_CHECKED);
		}

		if (iSortFlags & EditSortFlag_RemoveDuplicate) {
			CheckDlgButton(hwnd, IDC_SORT_REMOVE_DUP, BST_CHECKED);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), FALSE);
		}

		if (iSortFlags & EditSortFlag_RemoveUnique) {
			CheckDlgButton(hwnd, IDC_SORT_REMOVE_UNIQUE, BST_CHECKED);
		}

		if (iSortFlags & EditSortFlag_IgnoreCase) {
			CheckDlgButton(hwnd, IDC_SORT_IGNORE_CASE, BST_CHECKED);
		}

		if (iSortFlags & EditSortFlag_LogicalNumber) {
			CheckDlgButton(hwnd, IDC_SORT_LOGICAL_NUMBER, BST_CHECKED);
		}
		if (iSortFlags & EditSortFlag_GroupByFileType) {
			CheckDlgButton(hwnd, IDC_SORT_GROUPBY_FILE_TYPE, BST_CHECKED);
		}

		if (!SciCall_IsRectangleSelection()) {
			*piSortFlags &= ~EditSortFlag_ColumnSort;
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_COLUMN), FALSE);
		} else {
			*piSortFlags |= EditSortFlag_ColumnSort;
			CheckDlgButton(hwnd, IDC_SORT_COLUMN, BST_CHECKED);
		}
		CenterDlgInParent(hwnd);
	}
	return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			int *piSortFlags = (int *)GetWindowLongPtr(hwnd, DWLP_USER);
			int iSortFlags = EditSortFlag_Ascending;
			if (IsButtonChecked(hwnd, IDC_SORT_NONE)) {
				iSortFlags |= EditSortFlag_DontSort;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_DESC)) {
				iSortFlags |= EditSortFlag_Descending;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_SHUFFLE)) {
				iSortFlags |= EditSortFlag_Shuffle;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_MERGE_DUP)) {
				iSortFlags |= EditSortFlag_MergeDuplicate;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_REMOVE_DUP)) {
				iSortFlags |= EditSortFlag_RemoveDuplicate;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_REMOVE_UNIQUE)) {
				iSortFlags |= EditSortFlag_RemoveUnique;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_IGNORE_CASE)) {
				iSortFlags |= EditSortFlag_IgnoreCase;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_LOGICAL_NUMBER)) {
				iSortFlags |= EditSortFlag_LogicalNumber;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_GROUPBY_FILE_TYPE)) {
				iSortFlags |= EditSortFlag_GroupByFileType;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_COLUMN)) {
				iSortFlags |= EditSortFlag_ColumnSort;
			}
			*piSortFlags = iSortFlags;
			EndDialog(hwnd, IDOK);
		}
		break;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;

		case IDC_SORT_NONE:
		case IDC_SORT_ASC:
		case IDC_SORT_DESC:
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), !IsButtonChecked(hwnd, IDC_SORT_REMOVE_UNIQUE));
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_DUP), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_UNIQUE), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_IGNORE_CASE), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_LOGICAL_NUMBER), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_GROUPBY_FILE_TYPE), TRUE);
			break;

		case IDC_SORT_SHUFFLE:
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_UNIQUE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_IGNORE_CASE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_LOGICAL_NUMBER), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_GROUPBY_FILE_TYPE), FALSE);
			break;

		case IDC_SORT_REMOVE_DUP:
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), !IsButtonChecked(hwnd, IDC_SORT_REMOVE_DUP));
			break;
		}
		return TRUE;
	}

	return FALSE;
}

//=============================================================================
//
// EditSortDlg()
//
bool EditSortDlg(HWND hwnd, EditSortFlag *piSortFlags) {
	const INT_PTR iResult = ThemedDialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_SORT), hwnd, EditSortDlgProc, (LPARAM)piSortFlags);
	return iResult == IDOK;
}

void EditSelectionAction(int action) {
	const LPCWSTR kActionKeys[] = {
		L"GoogleSearchUrl",
		L"BingSearchUrl",
		L"WikiSearchUrl",
		L"CustomAction1",
		L"CustomAction2",
	};

	WCHAR szCmdTemplate[256];
	action -= CMD_ONLINE_SEARCH_GOOGLE;
	LPCWSTR actionKey = kActionKeys[action];
	BOOL bCmdEnabled = IniGetString(INI_SECTION_NAME_FLAGS, actionKey, L"", szCmdTemplate, COUNTOF(szCmdTemplate));
	if (!bCmdEnabled && action < 3) {
		bCmdEnabled = GetString(IDS_GOOGLE_SEARCH_URL + action, szCmdTemplate, COUNTOF(szCmdTemplate));
	}
	if (!bCmdEnabled) {
		return;
	}

	int cchEscapedW;
	LPWSTR pszEscapedW = EditURLEncodeSelection(&cchEscapedW);
	if (pszEscapedW == NULL) {
		return;
	}

	LPWSTR lpszCommand = (LPWSTR)NP2HeapAlloc(sizeof(WCHAR) * (cchEscapedW + COUNTOF(szCmdTemplate) + MAX_PATH + 32));
	const size_t cbCommand = NP2HeapSize(lpszCommand);
	wsprintf(lpszCommand, szCmdTemplate, pszEscapedW);

	LPWSTR lpszArgs = (LPWSTR)NP2HeapAlloc(cbCommand);
	ExtractFirstArgument(lpszCommand, lpszCommand, lpszArgs);
	ExpandEnvironmentStringsEx(lpszArgs, (DWORD)(cbCommand / sizeof(WCHAR)));

	WCHAR wchDirectory[MAX_PATH] = L"";
	if (StrNotEmpty(szCurFile)) {
		lstrcpy(wchDirectory, szCurFile);
		PathRemoveFileSpec(wchDirectory);
	}

	SHELLEXECUTEINFO sei;
	memset(&sei, 0, sizeof(SHELLEXECUTEINFO));
	sei.cbSize = sizeof(SHELLEXECUTEINFO);
	sei.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_NOZONECHECKS;
	sei.hwnd = NULL;
	sei.lpVerb = NULL;
	sei.lpFile = lpszCommand;
	sei.lpParameters = lpszArgs;
	sei.lpDirectory = wchDirectory;
	sei.nShow = SW_SHOWNORMAL;

	ShellExecuteEx(&sei);

	NP2HeapFree(pszEscapedW);
	NP2HeapFree(lpszCommand);
	NP2HeapFree(lpszArgs);
}

void TryBrowseFile(HWND hwnd, LPCWSTR pszFile, bool bWarn) {
	WCHAR tchParam[MAX_PATH + 4] = L"";
	WCHAR tchExeFile[MAX_PATH + 4] = L"";
	WCHAR tchTemp[MAX_PATH + 4];

	if (IniGetString(INI_SECTION_NAME_FLAGS, L"filebrowser.exe", L"", tchTemp, COUNTOF(tchTemp))) {
		ExtractFirstArgument(tchTemp, tchExeFile, tchParam);
	}
	if (StrIsEmpty(tchExeFile)) {
		lstrcpy(tchExeFile, L"metapath.exe");
	}
	if (PathIsRelative(tchExeFile)) {
		GetProgramRealPath(tchTemp, COUNTOF(tchTemp));
		PathRemoveFileSpec(tchTemp);
		PathAppend(tchTemp, tchExeFile);
		if (PathIsFile(tchTemp)) {
			lstrcpy(tchExeFile, tchTemp);
		}
	}

	if (StrNotEmpty(tchParam) && StrNotEmpty(pszFile)) {
		StrCatBuff(tchParam, L" ", COUNTOF(tchParam));
	}

	if (StrNotEmpty(pszFile)) {
		lstrcpy(tchTemp, pszFile);
		PathQuoteSpaces(tchTemp);
		StrCatBuff(tchParam, tchTemp, COUNTOF(tchParam));
	}

	SHELLEXECUTEINFO sei;
	memset(&sei, 0, sizeof(SHELLEXECUTEINFO));

	sei.cbSize = sizeof(SHELLEXECUTEINFO);
	sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS;
	sei.hwnd = hwnd;
	sei.lpVerb = NULL;
	sei.lpFile = tchExeFile;
	sei.lpParameters = tchParam;
	sei.lpDirectory = NULL;
	sei.nShow = SW_SHOWNORMAL;

	ShellExecuteEx(&sei);

	if ((INT_PTR)sei.hInstApp < 32) {
		if (bWarn) {
			if (MsgBoxWarn(MB_YESNO, IDS_ERR_BROWSE) == IDYES) {
				OpenHelpLink(hwnd, IDM_HELP_LATEST_RELEASE);
			}
		} else if (StrNotEmpty(pszFile)) {
			OpenContainingFolder(hwnd, pszFile, false);
		}
	}
}

char* EditGetStringAroundCaret(LPCSTR delimiters) {
	const Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	const Sci_Line iLine = SciCall_LineFromPosition(iCurrentPos);
	Sci_Position iLineStart = SciCall_PositionFromLine(iLine);
	Sci_Position iLineEnd = SciCall_GetLineEndPosition(iLine);
	if (iLineStart == iLineEnd) {
		// empty line
		return NULL;
	}

	const int style = SciCall_GetStyleIndexAt(iCurrentPos);
	if (SciCall_StyleGetHotSpot(style)) {
		iLineStart = iCurrentPos - 1;
		while (SciCall_GetStyleIndexAt(iLineStart) == style) {
			--iLineStart;
		}
		++iLineStart;
		iLineEnd = iCurrentPos + 1;
		while (SciCall_GetStyleIndexAt(iLineEnd) == style) {
			++iLineEnd;
		}

		return EditGetTextRange(iLineStart, iLineEnd);
	}

	struct Sci_TextToFindFull ft = { { iCurrentPos, 0 }, delimiters, { 0, 0 } };
	const int findFlag = NP2_RegexDefaultFlags;

	// forward
	if (iCurrentPos < iLineEnd) {
		ft.chrg.cpMax = iLineEnd;
		Sci_Position iPos = SciCall_FindTextFull(findFlag, &ft);
		if (iPos >= 0) {
			iPos = ft.chrgText.cpMax;
			// keep column in filename(line,column): warning
			const int chPrev = SciCall_GetCharAt(iPos - 1);
			const int ch = SciCall_GetCharAt(iPos);
			if (chPrev == ',' && (ch >= '0' && ch <= '9')) {
				iLineEnd = SciCall_WordEndPosition(iPos, true);
			} else {
				iLineEnd = SciCall_PositionBefore(iPos);
			}
		}
	}

	// backward
	if (iCurrentPos > iLineStart) {
		ft.chrg.cpMax = iLineStart;
		const Sci_Position iPos = SciCall_FindTextFull(findFlag, &ft);
		if (iPos >= 0) {
			iLineStart = SciCall_PositionAfter(ft.chrgText.cpMin);
		}
	}

	// Markdown URL: [alt](url)
	Sci_Position iStartPos = iLineStart;
	Sci_Position iEndPos = iLineEnd;
	ft.chrg.cpMax = iLineEnd;
	ft.lpstrText = "\\(\\w*:?\\.*/";
	while (iStartPos < iEndPos) {
		ft.chrg.cpMin = iStartPos;
		Sci_Position iPos = SciCall_FindTextFull(findFlag, &ft);
		if (iPos < 0) {
			break;
		}

		iStartPos = iPos + 1;
		iPos = SciCall_BraceMatchNext(iPos, ft.chrgText.cpMax);
		iEndPos = (iPos < 0) ? iLineEnd : iPos;
		if (iCurrentPos >= iStartPos && iCurrentPos <= iEndPos) {
			iLineStart = iStartPos;
			iLineEnd = iEndPos;
			break;
		}
		iStartPos = ft.chrgText.cpMax;
	}

	return EditGetTextRange(iLineStart, iLineEnd);
}

extern bool bOpenFolderWithMetapath;

static DWORD EditOpenSelectionCheckFile(LPCWSTR link, LPWSTR path, int cchFilePath, LPWSTR wchDirectory) {
	if (StrHasPrefix(link, L"//")) {
		// issue #454, treat as link
		lstrcpy(path, L"http:");
		lstrcpy(path + CSTRLEN(L"http:"), link);
		return 0;
	}

	DWORD dwAttributes = GetFileAttributes(link);
	if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
		if (StrNotEmpty(szCurFile)) {
			lstrcpy(wchDirectory, szCurFile);
			PathRemoveFileSpec(wchDirectory);
			PathCombine(path, wchDirectory, link);
			dwAttributes = GetFileAttributes(path);
		}
		if (dwAttributes == INVALID_FILE_ATTRIBUTES && GetFullPathName(link, cchFilePath, path, NULL)) {
			dwAttributes = GetFileAttributes(path);
		}
	} else {
		if (!GetFullPathName(link, cchFilePath, path, NULL)) {
			lstrcpy(path, link);
		}
	}
	return dwAttributes;
}

void EditOpenSelection(OpenSelectionType type) {
	Sci_Position cchSelection = SciCall_GetSelTextLength();
	char *mszSelection = NULL;
	if (cchSelection != 0) {
		mszSelection = (char *)NP2HeapAlloc(cchSelection + 1);
		SciCall_GetSelText(mszSelection);
		char *lpsz = strpbrk(mszSelection, "\r\n\t");
		if (lpsz) {
			*lpsz = '\0';
		}
	} else {
		// string terminated by space or quotes
		mszSelection = EditGetStringAroundCaret("[\\s'`\"<>|*,;]");
	}

	if (mszSelection == NULL) {
		return;
	}
	cchSelection = strlen(mszSelection);
	if (cchSelection == 0) {
		NP2HeapFree(mszSelection);
		return;
	}

	LPWSTR wszSelection = (LPWSTR)NP2HeapAlloc((max_pos(MAX_PATH, cchSelection) + 32) * sizeof(WCHAR));
	LPWSTR link = wszSelection + 16;
	const UINT cpEdit = SciCall_GetCodePage();
	MultiByteToWideChar(cpEdit, 0, mszSelection, -1, link, (int)cchSelection);
	NP2HeapFree(mszSelection);

	/* remove quotes, spaces and some invalid filename characters (except '/', '\' and '?') */
	StrTrim(link, L" \t\r\n'`\"<>|:*,;");
	const int cchTextW = lstrlen(link);

	if (cchTextW != 0) {
		// scan line and column after file name.
		LPCWSTR line = NULL;
		LPCWSTR column = L"";
		LPWSTR back = link + cchTextW - 1;

		LPWSTR p = back;
		if (*p == L'.') {
			*p = L'\0';
			--p;
			--back;
		}
		if (*p == L')') {
			--p;
			--back;
		}
		while (*p >= L'0' && *p <= L'9') {
			--p;
		}
		if (p != back && (*p == L':' || *p == L',' || *p == L'(')) {
			line = p + 1;
			back = p;
			if (*p == L',') {
				*p = L'\0';
			}
			if (*p != L'(') {
				--p;
				while (*p >= L'0' && *p <= L'9') {
					--p;
				}
				if (p != back - 1) {
					column = line;
					line = p + 1;
					if (*p == L':' && *back == L':') {
						// filename:line:column: warning
						*back = L'\0';
						*p = L'\0';
					}
					back = p;
				}
			}
		}

		// link or full path, ignore any character before scheme name
		p = StrChr(link, L':');
		if (p != NULL) {
			--p;
			while (p != link && IsSchemeNameChar(*p)) {
				--p;
			}
			while (!IsAlpha(*p)) {
				++p;
			}
			link = p;
		}

		WCHAR path[MAX_PATH * 2];
		WCHAR wchDirectory[MAX_PATH];
		DWORD dwAttributes = EditOpenSelectionCheckFile(link, path, COUNTOF(path), wchDirectory);
		if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
			if (line != NULL) {
				const WCHAR ch = *back;
				*back = L'\0';
				dwAttributes = EditOpenSelectionCheckFile(link, path, COUNTOF(path), wchDirectory);
				if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
					// line is port number or the file not exists
					*back = ch;
				} else {
					link = path;
				}
			}
		} else {
			link = path;
			if (dwAttributes == 0) {
				dwAttributes = INVALID_FILE_ATTRIBUTES;
			}
			if (*back != L'\0') {
				line = NULL;
			}
		}

		if (type == OpenSelectionType_ContainingFolder) {
			if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
				type = OpenSelectionType_None;
			}
		} else if (dwAttributes != INVALID_FILE_ATTRIBUTES) {
			if (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				type = OpenSelectionType_Folder;
			} else {
				const bool can = line != NULL || Style_CanOpenFile(link);
				// open supported file in a new window
				type = can ? OpenSelectionType_File : OpenSelectionType_Link;
			}
		} else if (StrChr(link, L':')) { // link
			// TODO: check scheme
			type = OpenSelectionType_Link;
		} else if (StrChr(link, L'@')) { // email
			lstrcpy(wszSelection, L"mailto:");
			lstrcpy(wszSelection + CSTRLEN(L"mailto:"), link);
			type = OpenSelectionType_Link;
			link = wszSelection;
		} else if (StrHasPrefix(link, L"www.")) {
			lstrcpy(wszSelection, L"http://"); // browser should auto switch to https
			lstrcpy(wszSelection + CSTRLEN(L"http://"), link);
			type = OpenSelectionType_Link;
			link = wszSelection;
		}

		switch (type) {
		case OpenSelectionType_Link:
			ShellExecute(hwndMain, L"open", link, NULL, NULL, SW_SHOWNORMAL);
			break;

		case OpenSelectionType_File: {
			WCHAR szModuleName[MAX_PATH];
			GetModuleFileName(NULL, szModuleName, COUNTOF(szModuleName));

			lstrcpyn(wchDirectory, link, COUNTOF(wchDirectory));
			PathRemoveFileSpec(wchDirectory);
			PathQuoteSpaces(link);

			LPWSTR lpParameters = link;
			if (line != NULL) {
				// TODO: improve the code when column is actually character index
				lpParameters = (LPWSTR)NP2HeapAlloc(sizeof(path));
				wsprintf(lpParameters, L"-g %s,%s %s", line, column, link);
			}

			SHELLEXECUTEINFO sei;
			memset(&sei, 0, sizeof(SHELLEXECUTEINFO));
			sei.cbSize = sizeof(SHELLEXECUTEINFO);
			sei.fMask = SEE_MASK_NOZONECHECKS;
			sei.hwnd = hwndMain;
			sei.lpVerb = NULL;
			sei.lpFile = szModuleName;
			sei.lpParameters = lpParameters;
			sei.lpDirectory = wchDirectory;
			sei.nShow = SW_SHOWNORMAL;

			ShellExecuteEx(&sei);
			if (line != NULL) {
				NP2HeapFree(lpParameters);
			}
		}
		break;

		case OpenSelectionType_Folder:
			if (bOpenFolderWithMetapath) {
				TryBrowseFile(hwndMain, link, false);
			} else {
				OpenContainingFolder(hwndMain, link, false);
			}
			break;

		case OpenSelectionType_ContainingFolder:
			OpenContainingFolder(hwndMain, link, true);
			break;

		default:
			if (cchTextW > 1 && link[0] == L'#') {
				// regex find link anchor in current document
				// html, markdown: (id | name) = [' | "] anchor [' | "]
				mszSelection = (char *)NP2HeapAlloc(2*cchSelection + 32);
				strcpy(mszSelection, "name\\s*=\\s*[\'\"]?");
				char *lpstrText = mszSelection + CSTRLEN("name\\s*=\\s*[\'\"]?");
				char* const lpszArgs = lpstrText + cchSelection;
				WideCharToMultiByte(cpEdit, 0, link + 1, cchTextW - 1, lpszArgs, (int)(cchSelection + 16), NULL, NULL);
				EscapeRegex(lpstrText, lpszArgs);
				strcat(lpstrText, "[\'\"]?");

				struct Sci_TextToFindFull ft = { { 0, SciCall_GetLength() }, mszSelection, { 0, 0 } };
				Sci_Position iPos = SciCall_FindTextFull(NP2_RegexDefaultFlags, &ft);
				if (iPos < 0) {
					lpstrText = mszSelection + 2;
					lpstrText[0] = 'i';
					lpstrText[1] = 'd';
					ft.lpstrText = lpstrText;
					iPos = SciCall_FindTextFull(NP2_RegexDefaultFlags, &ft);
				}
				NP2HeapFree(mszSelection);
				if (iPos >= 0) {
					EditSelectEx(ft.chrgText.cpMin, ft.chrgText.cpMax);
				}
			}
			break;
		}
	}

	NP2HeapFree(wszSelection);
}

//=============================================================================
//
// FileVars_Init()
//

extern bool bNoEncodingTags;
extern bool fNoFileVariables;
extern bool fWordWrapG;
extern int iWordWrapMode;
extern int iLongLinesLimitG;

void FileVars_Init(LPCSTR lpData, DWORD cbData, LPFILEVARS lpfv) {
	memset(lpfv, 0, sizeof(FILEVARS));
	// see FileVars_Apply() for other Tab settings.
	tabSettings.schemeUseGlobalTabSettings = true;
	lpfv->bTabIndents = tabSettings.bTabIndents;
	lpfv->fWordWrap = fWordWrapG;
	lpfv->iLongLinesLimit = iLongLinesLimitG;

	if (lpData == NULL || cbData == 0 || (fNoFileVariables && bNoEncodingTags)) {
		return;
	}

	char tch[512 + 1];
	const DWORD len = min_u(cbData, sizeof(tch) - 1);
	memcpy(tch, lpData, len);
	tch[len] = '\0';
	const bool utf8Sig = IsUTF8Signature(tch);

	// parse file variables at the beginning or end of the file.
	bool beginning = true;
	int mask = 0;
	while (true) {
		if (!fNoFileVariables) {
			// Emacs file variables
			int i;
			if (FileVars_ParseInt(tch, "enable-local-variables", &i) && (!i)) {
				break;
			}

			if (FileVars_ParseInt(tch, "tab-width", &i)) {
				lpfv->iTabWidth = clamp_i(i, TAB_WIDTH_MIN, TAB_WIDTH_MAX);
				mask |= FV_TABWIDTH;
			}

			if (FileVars_ParseInt(tch, "*basic-indent", &i) ||
				FileVars_ParseInt(tch, "*basic-offset", &i)) {
				lpfv->iIndentWidth = clamp_i(i, INDENT_WIDTH_MIN, INDENT_WIDTH_MAX);
				mask |= FV_INDENTWIDTH;
			}

			if (FileVars_ParseInt(tch, "indent-tabs-mode", &i)) {
				lpfv->bTabsAsSpaces = i == 0;
				mask |= FV_TABSASSPACES;
			}

			if (FileVars_ParseInt(tch, "*tab-always-indent", &i)) {
				lpfv->bTabIndents = i != 0;
				mask |= FV_TABINDENTS;
			}

			if (FileVars_ParseInt(tch, "truncate-lines", &i)) {
				lpfv->fWordWrap = i == 0;
				mask |= FV_WORDWRAP;
			}

			if (FileVars_ParseInt(tch, "fill-column", &i)) {
				lpfv->iLongLinesLimit = clamp_i(i, 0, NP2_LONG_LINE_LIMIT);
				mask |= FV_LONGLINESLIMIT;
			}

			if (FileVars_ParseStr(tch, "mode", lpfv->tchMode, COUNTOF(lpfv->tchMode))) {
				mask |= FV_MODE;
			}
		}

		if (!utf8Sig && !bNoEncodingTags) {
			if (FileVars_ParseStr(tch, "encoding", lpfv->tchEncoding, COUNTOF(lpfv->tchEncoding)) || // XML
				FileVars_ParseStr(tch, "charset", lpfv->tchEncoding, COUNTOF(lpfv->tchEncoding)) || // HTML
				FileVars_ParseStr(tch, "coding", lpfv->tchEncoding, COUNTOF(lpfv->tchEncoding)) || // Emacs
				FileVars_ParseStr(tch, "fileencoding", lpfv->tchEncoding, COUNTOF(lpfv->tchEncoding)) || // Vim
				FileVars_ParseStr(tch, "/*!40101 SET NAMES ", lpfv->tchEncoding, COUNTOF(lpfv->tchEncoding))) {
				// MySQL dump: /*!40101 SET NAMES utf8mb4 */;
				// CSS @charset "UTF-8"; is not supported.
				mask |= FV_ENCODING;
			}
		}

		if (beginning && mask == 0 && cbData > sizeof(tch) - 1) {
			memcpy(tch, lpData + cbData - sizeof(tch) + 1, sizeof(tch) - 1);
			tch[sizeof(tch) - 1] = '\0';
			beginning = false;
		} else {
			break;
		}
	}

	const int has = mask & FV_MaskHasTabIndentWidth;
	if (has == FV_TABWIDTH || has == FV_INDENTWIDTH) {
		if (has == FV_TABWIDTH) {
			lpfv->iIndentWidth = lpfv->iTabWidth;
		} else {
			lpfv->iTabWidth = lpfv->iIndentWidth;
		}
		mask |= FV_MaskHasTabIndentWidth;
	}
	if (mask & FV_ENCODING) {
		int iEncoding = Encoding_MatchA(lpfv->tchEncoding);
		// should never match UTF-16 or UTF-32.
		if (Encoding_IsUnicode(iEncoding)) {
			iEncoding = CPI_NONE;
		}
		lpfv->iEncoding = iEncoding;
	}
	lpfv->mask = mask;
}

void EditSetWrapStartIndent(int tabWidth, int indentWidth) {
	int indent = 0;
	switch (iWordWrapIndent) {
	case EditWrapIndent_OneCharacter:
		indent = 1;
		break;
	case EditWrapIndent_TwoCharacter:
		indent = 2;
		break;
	case EditWrapIndent_OneLevel:
		indent = indentWidth ? indentWidth : tabWidth;
		break;
	case EditWrapIndent_TwoLevel:
		indent = indentWidth ? 2 * indentWidth : 2 * tabWidth;
		break;
	}
	SciCall_SetWrapStartIndent(indent);
}

void EditSetWrapIndentMode(int tabWidth, int indentWidth) {
	int indentMode;
	switch (iWordWrapIndent) {
	case EditWrapIndent_SameAsSubline:
		indentMode = SC_WRAPINDENT_SAME;
		break;
	case EditWrapIndent_OneLevelThanSubline:
		indentMode = SC_WRAPINDENT_INDENT;
		break;
	case EditWrapIndent_TwoLevelThanSubline:
		indentMode = SC_WRAPINDENT_DEEPINDENT;
		break;
	default:
		indentMode = SC_WRAPINDENT_FIXED;
		EditSetWrapStartIndent(tabWidth, indentWidth);
		break;
	}
	SciCall_SetWrapIndentMode(indentMode);
}

//=============================================================================
//
// FileVars_Apply()
//
void FileVars_Apply(LPFILEVARS lpfv) {
	const int mask = lpfv->mask;
	if (tabSettings.schemeUseGlobalTabSettings) {
		if (!(mask & FV_TABWIDTH)) {
			lpfv->iTabWidth = tabSettings.globalTabWidth;
		}
		if (!(mask & FV_INDENTWIDTH)) {
			lpfv->iIndentWidth = tabSettings.globalIndentWidth;
		}
		if (!(mask & FV_TABSASSPACES)) {
			lpfv->bTabsAsSpaces = tabSettings.globalTabsAsSpaces;
		}
	} else {
		if (!(mask & FV_TABWIDTH)) {
			lpfv->iTabWidth = tabSettings.schemeTabWidth;
		}
		if (!(mask & FV_INDENTWIDTH)) {
			lpfv->iIndentWidth = tabSettings.schemeIndentWidth;
		}
		if (!(mask & FV_TABSASSPACES)) {
			lpfv->bTabsAsSpaces = tabSettings.schemeTabsAsSpaces;
		}
	}

	SciCall_SetTabWidth(lpfv->iTabWidth);
	SciCall_SetIndent(lpfv->iIndentWidth);
	SciCall_SetUseTabs(!lpfv->bTabsAsSpaces);
	SciCall_SetTabIndents(lpfv->bTabIndents);
	SciCall_SetBackSpaceUnIndents(tabSettings.bBackspaceUnindents);

	SciCall_SetWrapMode(lpfv->fWordWrap ? iWordWrapMode : SC_WRAP_NONE);
	EditSetWrapIndentMode(lpfv->iTabWidth, lpfv->iIndentWidth);

	SciCall_SetEdgeColumn(lpfv->iLongLinesLimit);
}

static LPCSTR FileVars_Find(LPCSTR pszData, LPCSTR pszName) {
	const bool suffix = *pszName == '*';
	if (suffix) {
		++pszName;
	}

	LPCSTR pvStart = pszData;
	while ((pvStart = strstr(pvStart, pszName)) != NULL) {
		const unsigned char chPrev = (pvStart > pszData) ? *(pvStart - 1) : 0;
		const size_t len = strlen(pszName);
		pvStart += len;
		// match full name or suffix after hyphen
		if (!(IsAlphaNumeric(chPrev) || chPrev == '-' || chPrev == '_' || chPrev == '.')
			|| (suffix && chPrev == '-')) {
			while (*pvStart == ' ' || *pvStart == '\t') {
				pvStart++;
			}
			if (*pvStart == ':' || *pvStart == '=' || pszName[len - 1] == ' ') {
				break;
			}
		}
	}

	return pvStart;
}

//=============================================================================
//
// FileVars_ParseInt()
//
bool FileVars_ParseInt(LPCSTR pszData, LPCSTR pszName, int *piValue) {
	LPCSTR pvStart = FileVars_Find(pszData, pszName);
	if (pvStart) {
		while (*pvStart == ':' || *pvStart == '=' || *pvStart == '\"' || *pvStart == '\'' || *pvStart == ' ' || *pvStart == '\t') {
			pvStart++;
		}

		char *pvEnd;
		*piValue = (int)strtol(pvStart, &pvEnd, 10);
		if (pvEnd != pvStart) {
			return true;
		}

		switch (*pvStart) {
		case 't': // true
		case 'y': // yes
			*piValue = 1;
			return true;
		case 'f': // false
		case 'n': // no
			*piValue = 0;
			return true;
		}
	}

	return false;
}

//=============================================================================
//
// FileVars_ParseStr()
//
bool FileVars_ParseStr(LPCSTR pszData, LPCSTR pszName, char *pszValue, int cchValue) {
	LPCSTR pvStart = FileVars_Find(pszData, pszName);
	if (pvStart) {
		bool bQuoted = false;

		while (*pvStart == ':' || *pvStart == '=' || *pvStart == '\"' || *pvStart == '\'' || *pvStart == ' ' || *pvStart == '\t') {
			if (*pvStart == '\'' || *pvStart == '"') {
				bQuoted = true;
			}
			pvStart++;
		}

		char tch[32];
		strncpy(tch, pvStart, COUNTOF(tch) - 1);

		char *pvEnd = tch;
		while (IsAlphaNumeric(*pvEnd) || *pvEnd == '+' || *pvEnd == '-' || *pvEnd == '/' || *pvEnd == '_' || (bQuoted && *pvEnd == ' ')) {
			pvEnd++;
		}
		*pvEnd = '\0';
		StrTrimA(tch, ":=\"\' \t"); // ASCII, should not fail.

		*pszValue = '\0';
		strncpy(pszValue, tch, cchValue);
		return true;
	}

	return false;
}

#if 0
//=============================================================================
//
// SciInitThemes()
//
static WNDPROC pfnSciWndProc = NULL;

static LRESULT CALLBACK SciThemedWndProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam);

void SciInitThemes(HWND hwnd) {
	pfnSciWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)SciThemedWndProc);
}

//=============================================================================
//
// SciThemedWndProc()
//
LRESULT CALLBACK SciThemedWndProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam) {
	static RECT rcContent;

	switch (umsg) {
	case WM_NCCALCSIZE:
		if (wParam) {
			const LRESULT lresult = CallWindowProc(pfnSciWndProc, hwnd, WM_NCCALCSIZE, wParam, lParam);
			if (IsAppThemed()) {
				HTHEME hTheme = OpenThemeData(hwnd, L"edit");
				if (hTheme) {
					NCCALCSIZE_PARAMS *csp = (NCCALCSIZE_PARAMS *)lParam;
					RECT rcClient;
					const HRESULT hr = GetThemeBackgroundContentRect(hTheme, NULL, EP_EDITTEXT, ETS_NORMAL, &csp->rgrc[0], &rcClient);
					const bool bSuccess = hr == S_OK;
					if (bSuccess) {
						InflateRect(&rcClient, -1, -1);

						rcContent.left = rcClient.left - csp->rgrc[0].left;
						rcContent.top = rcClient.top - csp->rgrc[0].top;
						rcContent.right = csp->rgrc[0].right - rcClient.right;
						rcContent.bottom = csp->rgrc[0].bottom - rcClient.bottom;

						CopyRect(&csp->rgrc[0], &rcClient);
					}
					CloseThemeData(hTheme);

					if (bSuccess) {
						return WVR_REDRAW;
					}
				}
			}
			return lresult;
		}
		break;

	case WM_NCPAINT: {
		const LRESULT lresult = CallWindowProc(pfnSciWndProc, hwnd, WM_NCPAINT, wParam, lParam);
		if (IsAppThemed()) {
			HTHEME hTheme = OpenThemeData(hwnd, L"edit");
			if (hTheme) {
				HDC hdc = GetWindowDC(hwnd);

				RECT rcBorder;
				GetWindowRect(hwnd, &rcBorder);
				OffsetRect(&rcBorder, -rcBorder.left, -rcBorder.top);

				RECT rcClient;
				CopyRect(&rcClient, &rcBorder);
				rcClient.left += rcContent.left;
				rcClient.top += rcContent.top;
				rcClient.right -= rcContent.right;
				rcClient.bottom -= rcContent.bottom;

				ExcludeClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);

				if (IsThemeBackgroundPartiallyTransparent(hTheme, EP_EDITTEXT, ETS_NORMAL)) {
					DrawThemeParentBackground(hwnd, hdc, &rcBorder);
				}

				int nState;
				if (!IsWindowEnabled(hwnd)) {
					nState = ETS_DISABLED;
				} else if (GetFocus() == hwnd) {
					nState = ETS_FOCUSED;
				} else if (SciCall_GetReadOnly()) {
					nState = ETS_READONLY;
				} else {
					nState = ETS_NORMAL;
				}

				DrawThemeBackground(hTheme, hdc, EP_EDITTEXT, nState, &rcBorder, NULL);
				CloseThemeData(hTheme);

				ReleaseDC(hwnd, hdc);
				return 0;
			}
		}
		return lresult;
	}
	}

	return CallWindowProc(pfnSciWndProc, hwnd, umsg, wParam, lParam);
}

#endif


//==============================================================================
//
// Folding Functions
//
//
#define FOLD_CHILDREN SCMOD_CTRL
#define FOLD_SIBLINGS SCMOD_SHIFT
// max position to find function definition style on a line.
#define MAX_FUNCTION_DEFINITION_POSITION	64
// max level for Toggle Folds -> Current Level for indentation based lexers.
#define MAX_EDIT_TOGGLE_FOLD_LEVEL		63
struct FoldLevelStack {
	int levelCount; // 1-based level number at current header line
	int levelStack[MAX_EDIT_TOGGLE_FOLD_LEVEL];
};

static void FoldLevelStack_Push(struct FoldLevelStack *levelStack, int level) {
	while (levelStack->levelCount != 0 && level <= levelStack->levelStack[levelStack->levelCount - 1]) {
		--levelStack->levelCount;
	}

	levelStack->levelStack[levelStack->levelCount] = level;
	++levelStack->levelCount;
}

static FOLD_ACTION FoldToggleNode(Sci_Line line, FOLD_ACTION expanding) {
	const BOOL fExpanded = SciCall_GetFoldExpanded(line);
	FOLD_ACTION action = expanding;
	if (action == FOLD_ACTION_SNIFF) {
		action = fExpanded ? FOLD_ACTION_FOLD : FOLD_ACTION_EXPAND;
	}

	if ((int)action != fExpanded) {
		SciCall_FoldLine(line, (int)action);
		if (expanding == FOLD_ACTION_SNIFF) {
			// header without children not changed after toggle (issue #48).
			const BOOL after = SciCall_GetFoldExpanded(line);
			if (fExpanded != after) {
				expanding = action;
			}
		}
	}
	return expanding;
}

static void FinishBatchFold(void) {
	SendMessage(hwndEdit, WM_SETREDRAW, TRUE, 0);
	SciCall_SetXCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 50);
	SciCall_SetYCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 5);
	SciCall_ScrollCaret();
	SciCall_SetXCaretPolicy(CARET_SLOP | CARET_EVEN, 50);
	SciCall_SetYCaretPolicy(CARET_EVEN, 0);
	InvalidateRect(hwndEdit, NULL, TRUE);
}

bool EditIsLineContainsStyle(Sci_Line line, int style) {
	Sci_Position lineStart = SciCall_PositionFromLine(line);
	Sci_Position lineEnd = SciCall_PositionFromLine(line + 1);
	lineEnd = min_pos(lineEnd, lineStart + MAX_FUNCTION_DEFINITION_POSITION);
	do {
		const int value = SciCall_GetStyleIndexAt(lineStart);
		if (value == style) {
			return true;
		}
		++lineStart;
	} while (lineStart < lineEnd);
	return false;
}

void FoldExpandRange(Sci_Line lineStart, Sci_Line lineEnd) {
	for (Sci_Line line = lineStart; line <= lineEnd; line++) {
		const int level = SciCall_GetFoldLevel(line);
		if ((level & SC_FOLDLEVELHEADERFLAG) != 0 && !SciCall_GetFoldExpanded(line)) {
			SciCall_ExpandChildren(line, level);
			line = SciCall_GetLastChildEx(line, level);
		}
	}
}

void FoldToggleAll(FOLD_ACTION action) {
	//SciCall_ColouriseAll();
	SendMessage(hwndEdit, WM_SETREDRAW, FALSE, 0);
#if 0
	StopWatch watch;
	StopWatch_Start(watch);
#endif
	SciCall_FoldAll((int)action | SC_FOLDACTION_CONTRACT_EVERY_LEVEL);
#if 0
	StopWatch_Stop(watch);
	StopWatch_ShowLog(&watch, __func__);
#endif
	FinishBatchFold();
}

void FoldToggleLevel(int lev, FOLD_ACTION action) {
	SciCall_ColouriseAll();
	const Sci_Line lineCount = SciCall_GetLineCount();
	Sci_Line line = 0;

	SendMessage(hwndEdit, WM_SETREDRAW, FALSE, 0);
#if 0
	StopWatch watch;
	StopWatch_Start(watch);
#endif
	if (pLexCurrent->lexerAttr & LexerAttr_IndentBasedFolding) {
		struct FoldLevelStack levelStack = { 0, { 0 }};
		++lev;
		while (line < lineCount) {
			int level = SciCall_GetFoldLevel(line);
			if (level & SC_FOLDLEVELHEADERFLAG) {
				level &= SC_FOLDLEVELNUMBERMASK;
				FoldLevelStack_Push(&levelStack, level);
				if (lev == levelStack.levelCount) {
					action = FoldToggleNode(line, action);
					line = SciCall_GetLastChildEx(line, level);
				}
			}
			++line;
		}
	} else {
		lev += SC_FOLDLEVELBASE;
		while (line < lineCount) {
			int level = SciCall_GetFoldLevel(line);
			if (level & SC_FOLDLEVELHEADERFLAG) {
				level &= SC_FOLDLEVELNUMBERMASK;
				if (lev == level) {
					action = FoldToggleNode(line, action);
					line = SciCall_GetLastChildEx(line, level);
				}
			}
			++line;
		}
	}

#if 0
	StopWatch_Stop(watch);
	StopWatch_ShowLog(&watch, __func__);
#endif
	FinishBatchFold();
}

void FoldToggleCurrentBlock(FOLD_ACTION action) {
	Sci_Line line = SciCall_LineFromPosition(SciCall_GetCurrentPos());
	const int level = SciCall_GetFoldLevel(line);

	if (!(level & SC_FOLDLEVELHEADERFLAG)) {
		line = SciCall_GetFoldParent(line);
		if (line < 0) {
			return;
		}
	}

	FoldToggleNode(line, action);
	FinishBatchFold();
}

void FoldToggleCurrentLevel(FOLD_ACTION action) {
	Sci_Line line = SciCall_LineFromPosition(SciCall_GetCurrentPos());
	int level = SciCall_GetFoldLevel(line);

	if (!(level & SC_FOLDLEVELHEADERFLAG)) {
		line = SciCall_GetFoldParent(line);
		if (line < 0) {
			return;
		}
		level = SciCall_GetFoldLevel(line);
	}

	level &= SC_FOLDLEVELNUMBERMASK;
	level -= SC_FOLDLEVELBASE;

	if (level != 0 && (pLexCurrent->lexerAttr & LexerAttr_IndentBasedFolding)) {
		level = 0;
		while (line != 0 && level < MAX_EDIT_TOGGLE_FOLD_LEVEL - 1) {
			line = SciCall_GetFoldParent(line);
			if (line < 0) {
				break;
			}
			++level;
		}
	}

	FoldToggleLevel(level, action);
}

void FoldToggleDefault(FOLD_ACTION action) {
	SciCall_ColouriseAll();
	const int ignoreInner = pLexCurrent->defaultFoldIgnoreInner;
	const UINT levelMask = pLexCurrent->defaultFoldLevelMask;
	const int maxLevel = np2_bsr(levelMask);
	const Sci_Line lineCount = SciCall_GetLineCount();
	Sci_Line line = 0;

	SendMessage(hwndEdit, WM_SETREDRAW, FALSE, 0);
#if 0
	StopWatch watch;
	StopWatch_Start(watch);
#endif
	if (pLexCurrent->lexerAttr & LexerAttr_IndentBasedFolding) {
		struct FoldLevelStack levelStack = { 0, { 0 }};
		while (line < lineCount) {
			int level = SciCall_GetFoldLevel(line);
			if (level & SC_FOLDLEVELHEADERFLAG) {
				level &= SC_FOLDLEVELNUMBERMASK;
				FoldLevelStack_Push(&levelStack, level);
				const int lev = levelStack.levelCount;
				if (levelMask & (1U << lev)) {
					action = FoldToggleNode(line, action);
					if (lev == maxLevel || (ignoreInner && EditIsLineContainsStyle(line, ignoreInner))) {
						line = SciCall_GetLastChildEx(line, level);
					}
				}
			}
			++line;
		}
	} else {
		while (line < lineCount) {
			int level = SciCall_GetFoldLevel(line);
			if (level & SC_FOLDLEVELHEADERFLAG) {
				level &= SC_FOLDLEVELNUMBERMASK;
				const int lev = level - SC_FOLDLEVELBASE;
				if (levelMask & (1U << lev)) {
					action = FoldToggleNode(line, action);
					if (lev == maxLevel || (ignoreInner && EditIsLineContainsStyle(line, ignoreInner))) {
						line = SciCall_GetLastChildEx(line, level);
					}
				}
			}
			++line;
		}
	}

#if 0
	StopWatch_Stop(watch);
	StopWatch_ShowLog(&watch, __func__);
#endif
	FinishBatchFold();
}

static void FoldPerformAction(Sci_Line ln, int mode, FOLD_ACTION action) {
	if (mode & (FOLD_CHILDREN | FOLD_SIBLINGS)) {
		// ln/lvNode: line and level of the source of this fold action
		const Sci_Line lnNode = ln;
		const int lvNode = SciCall_GetFoldLevel(lnNode) & SC_FOLDLEVELNUMBERMASK;
		const Sci_Line lnTotal = SciCall_GetLineCount();

		// lvStop: the level over which we should not cross
		int lvStop = lvNode;

		if (mode & FOLD_SIBLINGS) {
			ln = SciCall_GetFoldParent(lnNode) + 1;	 // -1 + 1 = 0 if no parent
			--lvStop;
		}

		for (; ln < lnTotal; ++ln) {
			int lv = SciCall_GetFoldLevel(ln);
			const bool fHeader = (lv & SC_FOLDLEVELHEADERFLAG) != 0;
			lv &= SC_FOLDLEVELNUMBERMASK;

			if (lv < lvStop || (lv == lvStop && fHeader && ln != lnNode)) {
				return;
			}
			if (fHeader && (lv == lvNode || (lv > lvNode && (mode & FOLD_CHILDREN)))) {
				action = FoldToggleNode(ln, action);
			}
		}
	} else {
		FoldToggleNode(ln, action);
	}
}

void FoldClickAt(Sci_Position pos, int mode) {
	static struct {
		Sci_Line ln;
		int mode;
		DWORD dwTickCount;
	} prev;

	BOOL fGotoFoldPoint = mode & FOLD_SIBLINGS;

	Sci_Line ln = SciCall_LineFromPosition(pos);
	if (!(SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG)) {
		// Not a fold point: need to look for a double-click
		if (prev.ln == ln && prev.mode == mode && (GetTickCount() - prev.dwTickCount <= GetDoubleClickTime())) {
			prev.ln = -1; // Prevent re-triggering on a triple-click
			ln = SciCall_GetFoldParent(ln);

			if (ln >= 0 && SciCall_GetFoldExpanded(ln)) {
				fGotoFoldPoint = TRUE;
			} else {
				return;
			}
		} else {
			// Save the info needed to match this click with the next click
			prev.ln = ln;
			prev.mode = mode;
			prev.dwTickCount = GetTickCount();
			return;
		}
	}

	FoldPerformAction(ln, mode, FOLD_ACTION_SNIFF);
	if (fGotoFoldPoint) {
		EditJumpTo(ln + 1, 0);
	}
}

void FoldAltArrow(int key, int mode) {
	// Because Alt-Shift is already in use (and because the sibling fold feature
	// is not as useful from the keyboard), only the Ctrl modifier is supported

	if ((mode & (SCMOD_ALT | SCMOD_SHIFT)) == SCMOD_ALT) {
		Sci_Line ln = SciCall_LineFromPosition(SciCall_GetCurrentPos());

		// Jump to the next visible fold point
		if (key == SCK_DOWN && !(mode & SCMOD_CTRL)) {
			const Sci_Line lnTotal = SciCall_GetLineCount();
			for (ln = ln + 1; ln < lnTotal; ++ln) {
				if ((SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG) && SciCall_GetLineVisible(ln)) {
					EditJumpTo(ln + 1, 0);
					return;
				}
			}
		} else if (key == SCK_UP && !(mode & SCMOD_CTRL)) {// Jump to the previous visible fold point
			for (ln = ln - 1; ln >= 0; --ln) {
				if ((SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG) && SciCall_GetLineVisible(ln)) {
					EditJumpTo(ln + 1, 0);
					return;
				}
			}
		} else if (SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG) {// Perform a fold/unfold operation
			if (key == SCK_LEFT) {
				FoldPerformAction(ln, mode, FOLD_ACTION_FOLD);
			}
			if (key == SCK_RIGHT) {
				FoldPerformAction(ln, mode, FOLD_ACTION_EXPAND);
			}
		}
	}
}

void EditGotoBlock(int menu) {
	const Sci_Position iCurPos = SciCall_GetCurrentPos();
	const Sci_Line iCurLine = SciCall_LineFromPosition(iCurPos);

	Sci_Line iLine = iCurLine;
	int level = SciCall_GetFoldLevel(iLine);
	if (!(level & SC_FOLDLEVELHEADERFLAG)) {
		iLine = SciCall_GetFoldParent(iLine);
	}

	switch (menu) {
	case IDM_EDIT_GOTO_BLOCK_START:
		break;

	case IDM_EDIT_GOTO_BLOCK_END:
		if (iLine >= 0) {
			iLine = SciCall_GetLastChild(iLine);
		}
		break;

	case IDM_EDIT_GOTO_PREVIOUS_BLOCK:
	case IDM_EDIT_GOTO_PREV_SIBLING_BLOCK: {
		bool sibling = menu == IDM_EDIT_GOTO_PREV_SIBLING_BLOCK;
		Sci_Line line = iCurLine - 1;
		Sci_Line first = -1;
		level &= SC_FOLDLEVELNUMBERMASK;

		while (line >= 0) {
			const int lev = SciCall_GetFoldLevel(line);
			 if ((lev & SC_FOLDLEVELHEADERFLAG) && line != iLine) {
				if (sibling) {
					if (first < 0) {
						first = line;
					}
					if (level >= (lev & SC_FOLDLEVELNUMBERMASK)) {
						iLine = line;
						sibling = false;
						break;
					}
					line = SciCall_GetFoldParent(line);
					continue;
				}

				iLine = line;
				break;
			}
			--line;
		}
		if (sibling && first >= 0) {
			iLine = first;
		}
	}
	break;

	case IDM_EDIT_GOTO_NEXT_BLOCK:
	case IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK: {
		const Sci_Line lineCount = SciCall_GetLineCount();
		if (iLine >= 0) {
			iLine = SciCall_GetLastChild(iLine);
		}

		bool sibling = menu == IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK;
		Sci_Line line = iCurLine + 1;
		Sci_Line first = -1;
		if (sibling && iLine > 0 && (level & SC_FOLDLEVELHEADERFLAG)) {
			line = iLine + 1;
		}
		level &= SC_FOLDLEVELNUMBERMASK;

		while (line < lineCount) {
			const int lev = SciCall_GetFoldLevel(line);
			if (lev & SC_FOLDLEVELHEADERFLAG) {
				if (sibling) {
					if (first < 0) {
						first = line;
					}
					if (level >= (lev & SC_FOLDLEVELNUMBERMASK)) {
						iLine = line;
						sibling = false;
						break;
					}
					line = SciCall_GetLastChildEx(line, lev);
				} else {
					iLine = line;
					break;
				}
			}
			++line;
		}
		if (sibling && first >= 0) {
			iLine = first;
		}
	}
	break;
	}

	if (iLine >= 0 && iLine != iCurLine) {
		const Sci_Position column = SciCall_GetColumn(iCurPos);
		EditJumpTo(iLine + 1, column + 1);
	}
}
