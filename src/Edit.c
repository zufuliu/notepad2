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
*                                               http://www.flos-freeware.ch
*
*
******************************************************************************/

#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
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
extern DWORD dwLastIOError;
extern HWND hDlgFindReplace;
extern UINT cpLastFind;
extern BOOL bReplaceInitialized;

extern int xFindReplaceDlg;
extern int yFindReplaceDlg;
extern int cxFindReplaceDlg;

extern int iDefaultEncoding;
extern int iDefaultEOLMode;
extern BOOL bFixLineEndings;
extern BOOL bAutoStripBlanks;

// Default Codepage and Character Set
extern int iDefaultCodePage;
extern BOOL bLoadANSIasUTF8;
extern BOOL bLoadASCIIasUTF8;
extern BOOL bLoadNFOasOEM;
extern int iSrcEncoding;
extern int iWeakSrcEncoding;

extern int g_DOSEncoding;

extern LPMRULIST mruFind;
extern LPMRULIST mruReplace;

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
#if _WIN32_WINNT < _WIN32_WINNT_WIN8
extern DWORD kSystemLibraryLoadFlags;
#else
#define kSystemLibraryLoadFlags		LOAD_LIBRARY_SEARCH_SYSTEM32
#endif
static HMODULE hELSCoreDLL = NULL;
#else
#pragma comment(lib, "elscore.lib")
#endif

#define MAX_NON_UTF8_SIZE	(UINT_MAX/2 - 16)

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
}

static inline void NotifyRectangleSelection(void) {
	MsgBoxWarn(MB_OK, IDS_SELRECT);
	//ShowNotificationMessage(SC_NOTIFICATIONPOSITION_CENTER, IDS_SELRECT);
}

//=============================================================================
//
// EditSetNewText()
//
extern BOOL bFreezeAppTitle;
extern BOOL bLockedForEditing;
#if defined(_WIN64)
extern BOOL bLargeFileMode;
#endif
extern FILEVARS fvCurFile;
extern EditTabSettings tabSettings;
extern int iWrapColumn;
extern int iWordWrapIndent;

void EditSetNewText(LPCSTR lpstrText, DWORD cbText, Sci_Line lineCount) {
	bFreezeAppTitle = TRUE;
	bLockedForEditing = FALSE;
	iWrapColumn = 0;

	SciCall_SetReadOnly(FALSE);
	SciCall_Cancel();
	SciCall_SetUndoCollection(FALSE);
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
			bLargeFileMode = TRUE;
		}
	}
#endif

	FileVars_Apply(&fvCurFile);

	if (cbText > 0) {
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
	}

	SciCall_SetUndoCollection(TRUE);
	SciCall_EmptyUndoBuffer();
	SciCall_SetSavePoint();

	bFreezeAppTitle = FALSE;
}

//=============================================================================
//
// EditConvertText()
//
BOOL EditConvertText(UINT cpSource, UINT cpDest, BOOL bSetSavePoint) {
	if (cpSource == cpDest) {
		return TRUE;
	}

	const Sci_Position length = SciCall_GetLength();
	if (length >= (int)MAX_NON_UTF8_SIZE) {
		return TRUE;
	}

	char *pchText = NULL;
	int cbText = 0;
	if (length > 0) {
		// DBCS: length -> WCHAR: sizeof(WCHAR) * (length / 2) -> UTF-8: kMaxMultiByteCount * (length / 2)
		pchText = (char *)NP2HeapAlloc((length + 1) * sizeof(WCHAR));
		SciCall_GetText(NP2HeapSize(pchText), pchText);

		WCHAR *pwchText = (WCHAR *)NP2HeapAlloc((length + 1) * sizeof(WCHAR));
		const int cbwText = MultiByteToWideChar(cpSource, 0, pchText, (int)length, pwchText, (int)(NP2HeapSize(pwchText) / sizeof(WCHAR)));
		cbText = WideCharToMultiByte(cpDest, 0, pwchText, cbwText, pchText, (int)(NP2HeapSize(pchText)), NULL, NULL);
		NP2HeapFree(pwchText);
	}

	bLockedForEditing = FALSE;
	SciCall_SetReadOnly(FALSE);
	SciCall_Cancel();
	SciCall_SetUndoCollection(FALSE);
	SciCall_EmptyUndoBuffer();
	SciCall_ClearAll();
	SciCall_ClearMarker();
	SciCall_SetCodePage(cpDest);

	if (cbText > 0) {
		SciCall_SetModEventMask(SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
		SciCall_AppendText(cbText, pchText);
	}
	if (pchText != NULL) {
		NP2HeapFree(pchText);
	}

	SciCall_EmptyUndoBuffer();
	SciCall_SetUndoCollection(TRUE);
	if (length == 0 && bSetSavePoint) {
		SciCall_SetSavePoint();
	}
	return TRUE;
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
	if (length > 0) {
		pchText = (char *)NP2HeapAlloc(length + 1);
		SciCall_GetText(NP2HeapSize(pchText), pchText);
	}

	bLockedForEditing = FALSE;
	SciCall_SetReadOnly(FALSE);
	SciCall_Cancel();
	SciCall_SetUndoCollection(FALSE);
	SciCall_EmptyUndoBuffer();
	SciCall_ClearAll();
	SciCall_ClearMarker();

	EditReplaceDocument(pdoc);
	FileVars_Apply(&fvCurFile);

	if (length > 0) {
		SciCall_SetModEventMask(SC_MOD_NONE);
		SciCall_AppendText(length, pchText);
		SciCall_SetModEventMask(SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
	}
	if (pchText != NULL) {
		NP2HeapFree(pchText);
	}

	SciCall_SetUndoCollection(TRUE);
	SciCall_EmptyUndoBuffer();
	SciCall_SetSavePoint();

	Style_SetLexer(pLexCurrent, TRUE);
	bLargeFileMode = TRUE;
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
		for (int i = 0; (i < mlen) && (*s != 0); i++) {
			if (*s == '\n' || *s == '\r') {
				if (iEOLMode == SC_EOL_CR) {
					*d++ = '\r';
				} else if (iEOLMode == SC_EOL_LF) {
					*d++ = '\n';
				} else { // iEOLMode == SC_EOL_CRLF
					*d++ = '\r';
					*d++ = '\n';
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

		*d++ = 0;
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
		for (int i = 0; (i < wlen) && (*s != 0); i++) {
			if (*s == '\n' || *s == '\r') {
				if (iEOLMode == SC_EOL_CR) {
					*d++ = '\r';
				} else if (iEOLMode == SC_EOL_LF) {
					*d++ = '\n';
				} else { // iEOLMode == SC_EOL_CRLF
					*d++ = '\r';
					*d++ = '\n';
				}
				if ((*s == '\r') && (i + 1 < wlen) && (*(s + 1) == '\n')) {
					i++;
					s++;
				}
				s++;
			} else {
				*d++ = *s++;
			}
		}

		*d++ = 0;
	}

	GlobalUnlock(hmem);
	CloseClipboard();

	return ptmp;
}

//=============================================================================
//
// EditCopyAppend()
//
BOOL EditCopyAppend(HWND hwnd) {
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
		SciCall_Copy(FALSE);
		return TRUE;
	}

	char *pszText;
	if (!SciCall_IsSelectionEmpty()) {
		if (SciCall_IsRectangleSelection()) {
			NotifyRectangleSelection();
			return FALSE;
		}

		const Sci_Position iSelCount = SciCall_GetSelTextLength();
		pszText = (char *)NP2HeapAlloc(iSelCount);
		SciCall_GetSelText(pszText);
	} else {
		const Sci_Position cchText = SciCall_GetLength();
		pszText = (char *)NP2HeapAlloc(cchText + 1);
		SciCall_GetText(NP2HeapSize(pszText), pszText);
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

	BOOL succ = FALSE;
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

		succ = TRUE;
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
	const uint8_t * const end = ptr + cbData - 1;

#if NP2_USE_AVX2
	const uint64_t LAST_CR_MASK = (UINT64_C(1) << (2*sizeof(__m256i) - 1));
	const __m256i vectCR = _mm256_set1_epi8('\r');
	const __m256i vectLF = _mm256_set1_epi8('\n');
	while (ptr + 2*sizeof(__m256i) <= end) {
		// unaligned loading: line starts at random position.
		const __m256i chunk1 = _mm256_loadu_si256((__m256i *)ptr);
		const __m256i chunk2 = _mm256_loadu_si256((__m256i *)(ptr + sizeof(__m256i)));
		uint64_t maskCR = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m256i);
		maskLF |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m256i);

		ptr += 2*sizeof(__m256i);
		if (maskCR) {
			if (maskCR & LAST_CR_MASK) {
				maskCR &= LAST_CR_MASK - 1;
				if (*ptr == '\n') {
					// CR+LF across boundary
					++ptr;
					++lineCountCRLF;
				} else {
					// clear highest bit (last CR) to avoid using following code:
					// maskCR = (maskCR_LF ^ maskLF) | (maskCR & LAST_CR_MASK);
					++lineCountCR;
				}
			}

			// maskCR and maskLF never have some bit set. after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint64_t maskCRLF = (maskCR << 1) & maskLF; // CR+LF
			const uint64_t maskCR_LF = (maskCR << 1) ^ maskLF;// CR alone or LF alone
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
		__movsb(buffer, ptr, end - ptr + 1);
		ptr = end + 1;

		const __m256i chunk1 = _mm256_load_si256((__m256i *)buffer);
		const __m256i chunk2 = _mm256_load_si256((__m256i *)(buffer + sizeof(__m256i)));
		uint64_t maskCR = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m256i);
		maskLF |= ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m256i);

		if (maskCR) {
			if (maskCR & LAST_CR_MASK) {
				maskCR &= LAST_CR_MASK - 1;
				++lineCountCR;
			}

			// maskCR and maskLF never have some bit set. after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint64_t maskCRLF = (maskCR << 1) & maskLF; // CR+LF
			const uint64_t maskCR_LF = (maskCR << 1) ^ maskLF;// CR alone or LF alone
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
	const uint64_t LAST_CR_MASK = (UINT64_C(1) << (4*sizeof(__m128i) - 1));
	const __m128i vectCR = _mm_set1_epi8('\r');
	const __m128i vectLF = _mm_set1_epi8('\n');
	while (ptr + 4*sizeof(__m128i) <= end) {
		// unaligned loading: line starts at random position.
		const __m128i chunk1 = _mm_loadu_si128((__m128i *)ptr);
		const __m128i chunk2 = _mm_loadu_si128((__m128i *)(ptr + sizeof(__m128i)));
		const __m128i chunk3 = _mm_loadu_si128((__m128i *)(ptr + 2*sizeof(__m128i)));
		const __m128i chunk4 = _mm_loadu_si128((__m128i *)(ptr + 3*sizeof(__m128i)));
		uint64_t maskCR = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);
		maskLF |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);
		maskCR |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectCR))) << 2*sizeof(__m128i);
		maskLF |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectLF))) << 2*sizeof(__m128i);
		maskCR |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectCR))) << 3*sizeof(__m128i);
		maskLF |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectLF))) << 3*sizeof(__m128i);

		ptr += 4*sizeof(__m128i);
		if (maskCR) {
			if (maskCR & LAST_CR_MASK) {
				maskCR &= LAST_CR_MASK - 1;
				if (*ptr == '\n') {
					// CR+LF across boundary
					++ptr;
					++lineCountCRLF;
				} else {
					// clear highest bit (last CR) to avoid using following code:
					// maskCR = (maskCR_LF ^ maskLF) | (maskCR & LAST_CR_MASK);
					++lineCountCR;
				}
			}

			// maskCR and maskLF never have some bit set. after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint64_t maskCRLF = (maskCR << 1) & maskLF; // CR+LF
			const uint64_t maskCR_LF = (maskCR << 1) ^ maskLF;// CR alone or LF alone
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
		__movsb(buffer, ptr, end - ptr + 1);
		ptr = end + 1;

		const __m128i chunk1 = _mm_load_si128((__m128i *)buffer);
		const __m128i chunk2 = _mm_load_si128((__m128i *)(buffer + sizeof(__m128i)));
		const __m128i chunk3 = _mm_load_si128((__m128i *)(buffer + 2*sizeof(__m128i)));
		const __m128i chunk4 = _mm_load_si128((__m128i *)(buffer + 3*sizeof(__m128i)));
		uint64_t maskCR = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint64_t maskLF = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);
		maskLF |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);
		maskCR |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectCR))) << 2*sizeof(__m128i);
		maskLF |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectLF))) << 2*sizeof(__m128i);
		maskCR |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectCR))) << 3*sizeof(__m128i);
		maskLF |= ((uint64_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectLF))) << 3*sizeof(__m128i);

		if (maskCR) {
			if (maskCR & LAST_CR_MASK) {
				maskCR &= LAST_CR_MASK - 1;
				++lineCountCR;
			}

			// maskCR and maskLF never have some bit set. after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint64_t maskCRLF = (maskCR << 1) & maskLF; // CR+LF
			const uint64_t maskCR_LF = (maskCR << 1) ^ maskLF;// CR alone or LF alone
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
	const uint32_t LAST_CR_MASK = (1U << (2*sizeof(__m128i) - 1));
	const __m128i vectCR = _mm_set1_epi8('\r');
	const __m128i vectLF = _mm_set1_epi8('\n');
	while (ptr + 2*sizeof(__m128i) <= end) {
		// unaligned loading: line starts at random position.
		const __m128i chunk1 = _mm_loadu_si128((__m128i *)ptr);
		const __m128i chunk2 = _mm_loadu_si128((__m128i *)(ptr + sizeof(__m128i)));
		uint32_t maskCR = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint32_t maskLF = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);
		maskLF |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);

		ptr += 2*sizeof(__m128i);
		if (maskCR) {
			if (maskCR & LAST_CR_MASK) {
				maskCR &= LAST_CR_MASK - 1;
				if (*ptr == '\n') {
					// CR+LF across boundary
					++ptr;
					++lineCountCRLF;
				} else {
					// clear highest bit (last CR) to avoid using following code:
					// maskCR = (maskCR_LF ^ maskLF) | (maskCR & LAST_CR_MASK);
					++lineCountCR;
				}
			}

			// maskCR and maskLF never have some bit set. after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint32_t maskCRLF = (maskCR << 1) & maskLF; // CR+LF
			const uint32_t maskCR_LF = (maskCR << 1) ^ maskLF;// CR alone or LF alone
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
		__movsb(buffer, ptr, end - ptr + 1);
		ptr = end + 1;

		const __m128i chunk1 = _mm_load_si128((__m128i *)buffer);
		const __m128i chunk2 = _mm_load_si128((__m128i *)(buffer + sizeof(__m128i)));
		uint32_t maskCR = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
		uint32_t maskLF = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
		maskCR |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m128i);
		maskLF |= ((uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m128i);

		if (maskCR) {
			if (maskCR & LAST_CR_MASK) {
				maskCR &= LAST_CR_MASK - 1;
				++lineCountCR;
			}

			// maskCR and maskLF never have some bit set. after shifting maskCR by 1 bit,
			// the bits both set in maskCR and maskLF represents CR+LF;
			// the bits only set in maskCR or maskLF represents individual CR or LF.
			const uint32_t maskCRLF = (maskCR << 1) & maskLF; // CR+LF
			const uint32_t maskCR_LF = (maskCR << 1) ^ maskLF;// CR alone or LF alone
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

	const uint32_t mask = (1 << '\r') | (1 << '\n');
	do {
		// skip to line end
		uint8_t ch = 0;
		while (ptr < end && ((ch = *ptr++) > '\r' || ((mask >> ch) & 1) == 0)) {
			// nop
		}
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

	//StopWatch watch;
	//StopWatch_Start(watch);

	// code based on SciTEBase::DiscoverIndentSetting().
	cbData = min_u(cbData, 1*1024*1024);
	const uint8_t *ptr = (const uint8_t *)lpData;
	const uint8_t * const end = ptr + cbData;
	#define MAX_DETECTED_TAB_WIDTH	8
	// line count for ambiguous lines, line indented by 1 to 8 spaces, line starts with tab.
	int indentLineCount[1 + MAX_DETECTED_TAB_WIDTH + 1] = { 0 };
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
		const uint32_t mask = (1 << '\r') | (1 << '\n');
		uint8_t ch;
		while (ptr < end && ((ch = *ptr++) > '\r' || ((mask >> ch) & 1) == 0)) {
			// nop
		}
	}

	prevTabWidth = 0;
	for (int i = 1; i < MAX_DETECTED_TAB_WIDTH + 2; i++) {
		if (indentLineCount[i] > indentLineCount[prevTabWidth]) {
			prevTabWidth = i;
		}
	}
	if (prevTabWidth != 0) {
		const BOOL bTabsAsSpaces = prevTabWidth <= MAX_DETECTED_TAB_WIDTH;
		lpfv->mask |= FV_TABSASSPACES;
		lpfv->bTabsAsSpaces = bTabsAsSpaces;
		if (bTabsAsSpaces) {
			lpfv->mask |= FV_MaskHasTabIndentWidth;
			lpfv->iTabWidth = prevTabWidth;
			lpfv->iIndentWidth = prevTabWidth;
		}
	}

	//StopWatch_Stop(watch);
	//const double duration = StopWatch_Get(&watch);
	//printf("indentation %u, duration=%.06f, tab width=%d\n", (UINT)cbData, duration, prevTabWidth);
	//for (int i = 0; i < MAX_DETECTED_TAB_WIDTH + 2; i++) {
	//	printf("\tindentLineCount[%d] = %d\n", i, indentLineCount[i]);
	//}
}

int EditDetermineEncoding(LPCWSTR pszFile, char *lpData, DWORD cbData, BOOL bSkipEncodingDetection, LPBOOL lpbBOM) {
	BOOL bPreferOEM = FALSE;
	if (bLoadNFOasOEM) {
		LPCWSTR const pszExt = pszFile + lstrlen(pszFile) - 4;
		if (pszExt >= pszFile && (StrCaseEqual(pszExt, L".nfo") || StrCaseEqual(pszExt, L".diz"))) {
			bPreferOEM = TRUE;
		}
	}

	if (!Encoding_IsValid(iDefaultEncoding)) {
		iDefaultEncoding = CPI_UTF8;
	}

	int _iDefaultEncoding = bPreferOEM ? g_DOSEncoding : iDefaultEncoding;
	if (iWeakSrcEncoding != -1 && Encoding_IsValid(iWeakSrcEncoding)) {
		_iDefaultEncoding = iWeakSrcEncoding;
	}

	int iEncoding = CPI_DEFAULT;
	// default encoding for empty file.
	if (cbData == 0) {
		FileVars_Init(NULL, 0, &fvCurFile);

		if (iSrcEncoding == -1) {
			if ((bLoadANSIasUTF8 || bLoadASCIIasUTF8) && !bPreferOEM) {
				iEncoding = CPI_UTF8;
			} else {
				iEncoding = _iDefaultEncoding;
			}
		} else {
			iEncoding = iSrcEncoding;
		}
		return iEncoding;
	}

	BOOL utf8Sig = IsUTF8Signature(lpData);
	BOOL bBOM = FALSE;
	BOOL bReverse = FALSE;

	// check Unicode / UTF-16
	// file large than 2 GiB is loaded without encoding conversion, i.e. loaded as UTF-8 or ANSI only.
	if (cbData < MAX_NON_UTF8_SIZE && (
		(iSrcEncoding == CPI_UNICODE || iSrcEncoding == CPI_UNICODEBE) // reload as UTF-16
		|| (!bSkipEncodingDetection && iSrcEncoding == -1 && !utf8Sig && IsUnicode(lpData, cbData, &bBOM, &bReverse))
	)) {
		if (iSrcEncoding == CPI_UNICODE) {
			bBOM = (lpData[0] == '\xFF' && lpData[1] == '\xFE');
			bReverse = FALSE;
		} else if (iSrcEncoding == CPI_UNICODEBE) {
			bBOM = (lpData[0] == '\xFE' && lpData[1] == '\xFF');
		}

		if (iSrcEncoding == CPI_UNICODEBE || bReverse) {
			_swab(lpData, lpData, cbData);
			iEncoding = bBOM ? CPI_UNICODEBEBOM : CPI_UNICODEBE;
		} else {
			iEncoding = bBOM ? CPI_UNICODEBOM : CPI_UNICODE;
		}

		*lpbBOM = bBOM;
		return iEncoding;
	}

	FileVars_Init(lpData, cbData, &fvCurFile);
	if (iSrcEncoding == -1) {
		iSrcEncoding = FileVars_GetEncoding(&fvCurFile);
	}

	iEncoding = iSrcEncoding;
	if (iEncoding == -1) {
		if (fvCurFile.mask & FV_ENCODING) {
			iEncoding = CPI_DEFAULT;
		} else {
			if ((iWeakSrcEncoding != -1) && (mEncoding[iWeakSrcEncoding].uFlags & NCP_INTERNAL)) {
				iEncoding = iDefaultEncoding;
			} else {
				iEncoding = _iDefaultEncoding;
			}
		}
	}

	// check UTF-8
	bBOM = utf8Sig;
	utf8Sig = (iSrcEncoding == CPI_UTF8 || iSrcEncoding == CPI_UTF8SIGN); // reload as UTF-8 or UTF-8 filevar
	if (iSrcEncoding == -1) {
		if (bLoadANSIasUTF8 && !bPreferOEM) { // load ANSI as UTF-8
			utf8Sig = TRUE;
		} else if (!bSkipEncodingDetection || cbData >= MAX_NON_UTF8_SIZE) {
			if (!bBOM && !bLoadASCIIasUTF8 && cbData < MAX_NON_UTF8_SIZE && IsUTF7(lpData, cbData)) {
				// 7-bit / any encoding
				return iEncoding;
			}
			utf8Sig = bBOM || IsUTF8(lpData, cbData);
		}
	}

	if (utf8Sig) {
		*lpbBOM = bBOM;
		iEncoding = bBOM ? CPI_UTF8SIGN : CPI_UTF8;
		return iEncoding;
	}

	if (cbData < MAX_NON_UTF8_SIZE && iEncoding != CPI_DEFAULT) {
		const UINT uFlags = mEncoding[iEncoding].uFlags;
		if ((uFlags & NCP_8BIT) || ((uFlags & NCP_7BIT) && IsUTF7(lpData, cbData))) {
			return iEncoding;
		}
	}

	// ANSI / unknown encoding
	return CPI_DEFAULT;
}

//=============================================================================
//
// EditLoadFile()
//
BOOL EditLoadFile(LPWSTR pszFile, BOOL bSkipEncodingDetection, EditFileIOStatus *status) {
	HANDLE hFile = CreateFile(pszFile,
					   GENERIC_READ,
					   FILE_SHARE_READ | FILE_SHARE_WRITE,
					   NULL, OPEN_EXISTING,
					   FILE_ATTRIBUTE_NORMAL,
					   NULL);
	dwLastIOError = GetLastError();

	if (hFile == INVALID_HANDLE_VALUE) {
		iSrcEncoding = -1;
		iWeakSrcEncoding = -1;
		return FALSE;
	}

	LARGE_INTEGER fileSize;
	fileSize.QuadPart = 0;
	if (!GetFileSizeEx(hFile, &fileSize)) {
		dwLastIOError = GetLastError();
		CloseHandle(hFile);
		iSrcEncoding = -1;
		iWeakSrcEncoding = -1;
		return FALSE;
	}

	// display real path name
	{
		WCHAR path[MAX_PATH] = L"";
		// since Windows Vista
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
		if (GetFinalPathNameByHandleW(hFile, path, MAX_PATH, FILE_NAME_OPENED))
#else
		typedef DWORD (WINAPI *GetFinalPathNameByHandleSig)(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
		GetFinalPathNameByHandleSig pfnGetFinalPathNameByHandle =
			DLLFunctionEx(GetFinalPathNameByHandleSig, L"kernel32.dll", "GetFinalPathNameByHandleW");
		if (pfnGetFinalPathNameByHandle && pfnGetFinalPathNameByHandle(hFile, path, MAX_PATH, FILE_NAME_OPENED))
#endif
		{
			if (StrHasPrefix(path, L"\\\\?\\")) {
				WCHAR *p = path + CSTRLEN(L"\\\\?\\");
				if (StrHasPrefix(p, L"UNC\\")) {
					p += 2;
					*p = L'\\'; // replace 'C' with backslash
				}
				lstrcpy(pszFile, p);
			}
		}
	}

	// Check if a warning message should be displayed for large files
#if defined(_WIN64)
	// less than 1/2 available physical memory:
	//     1. Buffers we allocated below or when saving file, depends on encoding.
	//     2. Scintilla's content buffer and style buffer, see CellBuffer class.
	//        The style buffer is disabled when using SCLEX_NULL (Text File, 2nd Text File, ANSI Art).
	//        i.e. when default scheme is Text File or 2nd Text File, memory required to load the file
	//        is about fileSize*2, buffers we allocated below can be reused by system to served
	//        as Scintilla's style buffer when calling SciCall_SetLexer() inside Style_SetLexer().
	//     3. Extra memory when moving gaps on editing, it may requires more than 2/3 physical memory.
	// large file TODO: https://github.com/zufuliu/notepad2/issues/125
	// [ ] [> 4 GiB] use SetFilePointerEx() and ReadFile()/WriteFile() to read/write file.
	// [-] [> 2 GiB] fix encoding conversion with MultiByteToWideChar() and WideCharToMultiByte().
	LONGLONG maxFileSize = INT64_C(0x100000000);
#else
	// 2 GiB: ptrdiff_t / Sci_Position used in Scintilla
	LONGLONG maxFileSize = INT64_C(0x80000000);
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
		status->bFileTooBig = TRUE;
		iSrcEncoding = -1;
		iWeakSrcEncoding = -1;
		WCHAR tchDocSize[32];
		WCHAR tchMaxSize[32];
		WCHAR tchDocBytes[32];
		WCHAR tchMaxBytes[32];
		StrFormatByteSize(fileSize.QuadPart, tchDocSize, COUNTOF(tchDocSize));
		StrFormatByteSize(maxFileSize, tchMaxSize, COUNTOF(tchMaxSize));
		_i64tow(fileSize.QuadPart, tchDocBytes, 10);
		_i64tow(maxFileSize, tchMaxBytes, 10);
		FormatNumberStr(tchDocBytes);
		FormatNumberStr(tchMaxBytes);
		MsgBoxWarn(MB_OK, IDS_WARNLOADBIGFILE, pszFile, tchDocSize, tchDocBytes, tchMaxSize, tchMaxBytes);
		return FALSE;
	}

	char *lpData = (char *)NP2HeapAlloc((SIZE_T)(fileSize.QuadPart) + 16);
	DWORD cbData = 0;
	// prevent unsigned integer overflow.
	const DWORD readLen = max_u((DWORD)(NP2HeapSize(lpData) - 2), (DWORD)fileSize.QuadPart);
	const BOOL bReadSuccess = ReadFile(hFile, lpData, readLen, &cbData, NULL);
	dwLastIOError = GetLastError();
	CloseHandle(hFile);

	if (!bReadSuccess) {
		NP2HeapFree(lpData);
		iSrcEncoding = -1;
		iWeakSrcEncoding = -1;
		return FALSE;
	}

	status->iEOLMode = GetScintillaEOLMode(iDefaultEOLMode);
	status->bInconsistent = FALSE;
	status->totalLineCount = 1;

	BOOL bBOM = FALSE;
	const int iEncoding = EditDetermineEncoding(pszFile, lpData, cbData, bSkipEncodingDetection, &bBOM);
	status->iEncoding = iEncoding;

	iSrcEncoding = -1;
	iWeakSrcEncoding = -1;
	const UINT uFlags = mEncoding[iEncoding].uFlags;

	if (cbData == 0) {
		SciCall_SetCodePage((uFlags & NCP_DEFAULT) ? iDefaultCodePage : SC_CP_UTF8);
		EditSetEmptyText();
		SciCall_SetEOLMode(status->iEOLMode);
		NP2HeapFree(lpData);
		return TRUE;
	}

	char *lpDataUTF8 = lpData;
	if (uFlags & NCP_UNICODE) {
		// cbData/2 => WCHAR, WCHAR*3 => UTF-8
		lpDataUTF8 = (char *)NP2HeapAlloc((cbData + 1)*sizeof(WCHAR));
		LPCWSTR pszTextW = bBOM ? ((LPWSTR)lpData + 1) : (LPWSTR)lpData;
		// NOTE: requires two extra trailing NULL bytes.
		const DWORD cchTextW = bBOM ? (cbData / sizeof(WCHAR)) : ((cbData / sizeof(WCHAR)) + 1);
		cbData = WideCharToMultiByte(CP_UTF8, 0, pszTextW, cchTextW, lpDataUTF8, (int)NP2HeapSize(lpDataUTF8), NULL, NULL);
		if (cbData == 0) {
			cbData = WideCharToMultiByte(CP_ACP, 0, pszTextW, -1, lpDataUTF8, (int)NP2HeapSize(lpDataUTF8), NULL, NULL);
			status->bUnicodeErr = TRUE;
		}
		if (cbData != 0) {
			// remove the NULL terminator.
			cbData -= 1;
		}

		NP2HeapFree(lpData);
		lpData = lpDataUTF8;
		FileVars_Init(lpData, cbData, &fvCurFile);
	} else if (uFlags & NCP_UTF8) {
		if (bBOM) {
			lpDataUTF8 = lpData + 3;
			cbData -= 3;
		}
	} else if (uFlags & (NCP_8BIT | NCP_7BIT)) {
		const UINT uCodePage = mEncoding[iEncoding].uCodePage;
		LPWSTR lpDataWide = (LPWSTR)NP2HeapAlloc(cbData * sizeof(WCHAR) + 16);
		const int cbDataWide = MultiByteToWideChar(uCodePage, 0, lpData, cbData, lpDataWide, (int)(NP2HeapSize(lpDataWide) / sizeof(WCHAR)));
		NP2HeapFree(lpData);

		lpData = (char *)NP2HeapAlloc(cbDataWide * kMaxMultiByteCount + 16);
		cbData = WideCharToMultiByte(CP_UTF8, 0, lpDataWide, cbDataWide, lpData, (int)NP2HeapSize(lpData), NULL, NULL);
		NP2HeapFree(lpDataWide);
		lpDataUTF8 = lpData;
	}

	if (cbData) {
		EditDetectEOLMode(lpDataUTF8, cbData, status);
		EditDetectIndentation(lpDataUTF8, cbData, &fvCurFile);
	}
	SciCall_SetCodePage((uFlags & NCP_DEFAULT) ? iDefaultCodePage : SC_CP_UTF8);
	EditSetNewText(lpDataUTF8, cbData, status->totalLineCount);

	NP2HeapFree(lpData);
	return TRUE;
}

//=============================================================================
//
// EditSaveFile()
//
BOOL EditSaveFile(HWND hwnd, LPCWSTR pszFile, BOOL bSaveCopy, EditFileIOStatus *status) {
	HANDLE hFile = CreateFile(pszFile,
					   GENERIC_WRITE,
					   FILE_SHARE_READ | FILE_SHARE_WRITE,
					   NULL, OPEN_ALWAYS,
					   FILE_ATTRIBUTE_NORMAL,
					   NULL);
	dwLastIOError = GetLastError();

	// failure could be due to missing attributes (2k/XP)
	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD dwAttributes = GetFileAttributes(pszFile);
		if (dwAttributes != INVALID_FILE_ATTRIBUTES) {
			dwAttributes = dwAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
			hFile = CreateFile(pszFile,
							   GENERIC_WRITE,
							   FILE_SHARE_READ | FILE_SHARE_WRITE,
							   NULL,
							   OPEN_ALWAYS,
							   FILE_ATTRIBUTE_NORMAL | dwAttributes,
							   NULL);
			dwLastIOError = GetLastError();
		}
	}

	if (hFile == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	// ensure consistent line endings
	if (bFixLineEndings) {
		EditEnsureConsistentLineEndings();
	}

	// strip trailing blanks
	if (bAutoStripBlanks) {
		EditStripTrailingBlanks(hwnd, TRUE);
	}

	BOOL bWriteSuccess;
	// get text
	DWORD cbData = (DWORD)SciCall_GetLength();
	char *lpData = NULL;

	if (cbData == 0) {
		bWriteSuccess = SetEndOfFile(hFile);
		dwLastIOError = GetLastError();
	} else {
		DWORD dwBytesWritten;
		int iEncoding = status->iEncoding;
		UINT uFlags = mEncoding[iEncoding].uFlags;
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
		SciCall_GetText(NP2HeapSize(lpData), lpData);
		// FIXME: move checks in front of disk file access
		/*if ((uFlags & NCP_UNICODE) == 0 && (uFlags & NCP_UTF8_SIGN) == 0) {
				BOOL bEncodingMismatch = TRUE;
				FILEVARS fv;
				FileVars_Init(lpData, cbData, &fv);
				if (fv.mask & FV_ENCODING) {
					int iAltEncoding;
					if (FileVars_IsValidEncoding(&fv)) {
						iAltEncoding = FileVars_GetEncoding(&fv);
						if (iAltEncoding == iEncoding)
							bEncodingMismatch = FALSE;
						else if ((mEncoding[iAltEncoding].uFlags & NCP_UTF8) && (uFlags & NCP_UTF8))
							bEncodingMismatch = FALSE;
					}
					if (bEncodingMismatch) {
						Encoding_GetLabel(iAltEncoding);
						Encoding_GetLabel(iEncoding);
						InfoBoxWarn(MB_OK, L"MsgEncodingMismatch", IDS_ENCODINGMISMATCH,
							mEncoding[iAltEncoding].wchLabel,
							mEncoding[iEncoding].wchLabel);
					}
				}
			}*/
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
			const BOOL zeroFlags = IsZeroFlagsCodePage(uCodePage);

			LPWSTR lpDataWide = (LPWSTR)NP2HeapAlloc(cbData * sizeof(WCHAR) + 16);
			const int cbDataWide = MultiByteToWideChar(CP_UTF8, 0, lpData, cbData, lpDataWide, (int)(NP2HeapSize(lpDataWide) / sizeof(WCHAR)));

			if (zeroFlags) {
				NP2HeapFree(lpData);
				lpData = (char *)NP2HeapAlloc(NP2HeapSize(lpDataWide) * 2);
			} else {
				ZeroMemory(lpData, NP2HeapSize(lpData));
			}

			if (zeroFlags) {
				cbData = WideCharToMultiByte(uCodePage, 0, lpDataWide, cbDataWide, lpData, (int)NP2HeapSize(lpData), NULL, NULL);
			} else {
				cbData = WideCharToMultiByte(uCodePage, WC_NO_BEST_FIT_CHARS, lpDataWide, cbDataWide, lpData, (int)NP2HeapSize(lpData), NULL, &bCancelDataLoss);
				if (!bCancelDataLoss) {
					cbData = WideCharToMultiByte(uCodePage, 0, lpDataWide, cbDataWide, lpData, (int)NP2HeapSize(lpData), NULL, NULL);
					bCancelDataLoss = FALSE;
				}
			}
			NP2HeapFree(lpDataWide);

			if (!bCancelDataLoss || InfoBoxWarn(MB_OKCANCEL, L"MsgConv3", IDS_ERR_UNICODE2) == IDOK) {
				SetEndOfFile(hFile);
				bWriteSuccess = WriteFile(hFile, lpData, cbData, &dwBytesWritten, NULL);
				dwLastIOError = GetLastError();
			} else {
				bWriteSuccess = FALSE;
				status->bCancelDataLoss = TRUE;
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
		if (!bSaveCopy) {
			SciCall_SetSavePoint();
		}
		return TRUE;
	}

	return FALSE;
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

	SciCall_BeginUndoAction();
	SciCall_SetTargetRange(iSelStart, iSelEnd);
	SciCall_ReplaceTarget(cchText, pszText);
	SciCall_SetSel(iAnchorPos, iCurPos);
	SciCall_EndUndoAction();
}

void EditReplaceMainSelection(Sci_Position cchText, LPCSTR pszText) {
	EditReplaceRange(SciCall_GetSelectionStart(), SciCall_GetSelectionEnd(), cchText, pszText);
}

//=============================================================================
//
// EditInvertCase()
//
void EditInvertCase(void) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength() - 1;
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

	BOOL bChanged = FALSE;
	for (int i = 0; i < cchTextW; i++) {
		if (IsCharUpper(pszTextW[i])) {
			pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
			bChanged = TRUE;
		} else if (IsCharLower(pszTextW[i])) {
			pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
			bChanged = TRUE;
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

	ZeroMemory(&enumOptions, sizeof(MAPPING_ENUM_OPTIONS));
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
		ZeroMemory(&bag, sizeof (MAPPING_PROPERTY_BAG));
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
				CopyMemory(pszConvW, pszTextW, dwDataSize);
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
static BOOL EditTitleCase(LPWSTR pszTextW, int cchTextW) {
	BOOL bChanged = FALSE;
#if 1
	// BOOKMARK_EDITION
	//Slightly enhanced function to make Title Case:
	//Added some '-characters and bPrevWasSpace makes it better (for example "'Don't'" will now work)
	BOOL bNewWord = TRUE;
	BOOL bPrevWasSpace = TRUE;
	for (int i = 0; i < cchTextW; i++) {
		const WCHAR ch = pszTextW[i];
		if (!IsCharAlphaNumeric(ch) && (!(ch == L'\'' || ch == L'`' || ch == 0xB4 || ch == 0x0384 || ch == 0x2019) || bPrevWasSpace)) {
			bNewWord = TRUE;
		} else {
			if (bNewWord) {
				if (IsCharLower(ch)) {
					pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = TRUE;
				}
			} else {
				if (IsCharUpper(ch)) {
					pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = TRUE;
				}
			}
			bNewWord = FALSE;
		}

		bPrevWasSpace = IsASpace(ch) || ch == L'[' || ch == L']' || ch == L'(' || ch == L')' || ch == L'{' || ch == L'}';
	}
#else
	BOOL bNewWord = TRUE;
	BOOL bWordEnd = TRUE;
	for (int i = 0; i < cchTextW; i++) {
		const WCHAR ch = pszTextW[i];
		const BOOL bAlphaNumeric = IsCharAlphaNumeric(ch);
		if (!bAlphaNumeric && (!(ch == L'\'' || ch == L'`' || ch == 0xB4 || ch == 0x0384 || ch == 0x2019) || bWordEnd)) {
			bNewWord = TRUE;
		} else {
			if (bNewWord) {
				if (IsCharLower(ch)) {
					pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = TRUE;
				}
			} else {
				if (IsCharUpper(ch)) {
					pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
					bChanged = TRUE;
				}
			}
			bNewWord = FALSE;
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
	const Sci_Position iSelCount = SciCall_GetSelTextLength() - 1;
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
	default:
		NP2_unreachable();
	}

	char *pszText = (char *)NP2HeapAlloc(iSelCount*kMaxMultiByteCount + 1);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	SciCall_GetSelText(pszText);
	const UINT cpEdit = SciCall_GetCodePage();
	int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));

	BOOL bChanged = FALSE;
	if (flags != 0 || pGuid != NULL) {
		int charsConverted = 0;
		LPWSTR pszMappedW = NULL;
		if (pGuid != NULL && IsWin7AndAbove()) {
			charsConverted = TransliterateText(pGuid, pszTextW, cchTextW, &pszMappedW);
		}
		if (pszMappedW == NULL && flags != 0) {
			charsConverted = LCMapString(LOCALE_USER_DEFAULT, flags, pszTextW, cchTextW, NULL, 0);
			if (charsConverted) {
				pszMappedW = (LPWSTR)NP2HeapAlloc((charsConverted + 1)*sizeof(WCHAR));
				charsConverted = LCMapString(LOCALE_USER_DEFAULT, flags, pszTextW, cchTextW, pszMappedW, charsConverted);
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
	const Sci_Position iSelCount = SciCall_GetSelTextLength() - 1;
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

	BOOL bNewSentence = TRUE;
	BOOL bChanged = FALSE;
	for (int i = 0; i < cchTextW; i++) {
		const WCHAR ch = pszTextW[i];
		if (ch == L'.' || ch == L';' || ch == L'!' || ch == L'?' || ch == L'\r' || ch == L'\n') {
			bNewSentence = TRUE;
		} else {
			if (IsCharAlphaNumeric(ch)) {
				if (bNewSentence) {
					if (IsCharLower(ch)) {
						pszTextW[i] = LOWORD(CharUpper((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
						bChanged = TRUE;
					}
					bNewSentence = FALSE;
				} else {
					if (IsCharUpper(ch)) {
						pszTextW[i] = LOWORD(CharLower((LPWSTR)(LONG_PTR)MAKELONG(ch, 0)));
						bChanged = TRUE;
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

#ifndef URL_ESCAPE_AS_UTF8		// (NTDDI_VERSION >= NTDDI_WIN7)
#define URL_ESCAPE_AS_UTF8		0x00040000
#endif
#ifndef URL_UNESCAPE_AS_UTF8	// (NTDDI_VERSION >= NTDDI_WIN8)
#define URL_UNESCAPE_AS_UTF8	URL_ESCAPE_AS_UTF8
#endif

//=============================================================================
//
// EditURLEncode()
//
LPWSTR EditURLEncodeSelection(int *pcchEscaped) {
	*pcchEscaped = 0;
	const Sci_Position iSelCount = SciCall_GetSelTextLength();
	if (iSelCount <= 1) {
		return NULL;
	}

	char *pszText = (char *)NP2HeapAlloc(iSelCount);
	SciCall_GetSelText(pszText);

	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc(iSelCount * sizeof(WCHAR));
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
	const Sci_Position iSelCount = SciCall_GetSelTextLength() - 1;
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
	const Sci_Position iSelCount = SciCall_GetSelTextLength() - 1;
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

#if NP2_USE_DESIGNATED_INITIALIZER
	EDITFINDREPLACE efr = {
		.hwnd = hwnd,
	};
#else
	EDITFINDREPLACE efr = { "", "", "", "", hwnd };
#endif

	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "\\");
	strcpy(efr.szReplace, "\\\\");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "\"");
	strcpy(efr.szReplace, "\\\"");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "\'");
	strcpy(efr.szReplace, "\\\'");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

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

#if NP2_USE_DESIGNATED_INITIALIZER
	EDITFINDREPLACE efr = {
		.hwnd = hwnd,
	};
#else
	EDITFINDREPLACE efr = { "", "", "", "", hwnd };
#endif

	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "\\\\");
	strcpy(efr.szReplace, "\\");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "\\\"");
	strcpy(efr.szReplace, "\"");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "\\\'");
	strcpy(efr.szReplace, "\'");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

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

#if NP2_USE_DESIGNATED_INITIALIZER
	EDITFINDREPLACE efr = {
		.hwnd = hwnd,
	};
#else
	EDITFINDREPLACE efr = { "", "", "", "", hwnd };
#endif

	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "&");
	strcpy(efr.szReplace, "&amp;");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "\"");
	strcpy(efr.szReplace, "&quot;");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "\'");
	strcpy(efr.szReplace, "&apos;");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "<");
	strcpy(efr.szReplace, "&lt;");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, ">");
	strcpy(efr.szReplace, "&gt;");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	if (pLexCurrent->iLexer != SCLEX_XML) {
		strcpy(efr.szFind, " ");
		strcpy(efr.szReplace, "&nbsp;");
		EditReplaceAllInSelection(hwnd, &efr, FALSE);

		strcpy(efr.szFind, "\t");
		strcpy(efr.szReplace, "&emsp;");
		EditReplaceAllInSelection(hwnd, &efr, FALSE);
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

#if NP2_USE_DESIGNATED_INITIALIZER
	EDITFINDREPLACE efr = {
		.hwnd = hwnd,
	};
#else
	EDITFINDREPLACE efr = { "", "", "", "", hwnd };
#endif

	SciCall_BeginUndoAction();

	strcpy(efr.szFind, "&quot;");
	strcpy(efr.szReplace, "\"");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "&apos;");
	strcpy(efr.szReplace, "\'");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "&lt;");
	strcpy(efr.szReplace, "<");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "&gt;");
	strcpy(efr.szReplace, ">");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "&nbsp;");
	strcpy(efr.szReplace, " ");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "&amp;");
	strcpy(efr.szReplace, "&");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);

	strcpy(efr.szFind, "&emsp;");
	strcpy(efr.szReplace, "\t");
	EditReplaceAllInSelection(hwnd, &efr, FALSE);
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
#define MAX_ESCAPE_HEX_DIGIT	4

void EditChar2Hex(void) {
	Sci_Position count = SciCall_GetSelTextLength() - 1;
	if (count == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	count *= 2 + MAX_ESCAPE_HEX_DIGIT;
	char *ch = (char *)NP2HeapAlloc(count + 10);
	WCHAR *wch = (WCHAR *)NP2HeapAlloc((count + 1) * sizeof(WCHAR));
	SciCall_GetSelText(ch);

	if (ch[0] == 0) {
		strcpy(ch, "\\x00");
	} else {
		const UINT cpEdit = SciCall_GetCodePage();
		char uesc = 'u';
		if (pLexCurrent->rid == NP2LEX_CSHARP) {
			uesc = 'x';
		}
		count = MultiByteToWideChar(cpEdit, 0, ch, -1, wch, (int)(count + 1)) - 1; // '\0'
		int j = 0;
		for (Sci_Position i = 0; i < count; i++) {
			if (wch[i] <= 0xFF) {
				sprintf(ch + j, "\\x%02X", wch[i] & 0xFF); // \xhh
				j += 4;
			} else {
				sprintf(ch + j, "\\%c%04X", uesc, wch[i]); // \uhhhh \xhhhh
				j += 6;
			}
		}
		if (count == 2 && IS_SURROGATE_PAIR(wch[0], wch[1])) {
			const UINT ucc = UTF16_TO_UTF32(wch[0], wch[1]);
			sprintf(ch + j, " U+%X", ucc);
		}
	}

	EditReplaceMainSelection(strlen(ch), ch);

	NP2HeapFree(ch);
	NP2HeapFree(wch);
}

//=============================================================================
//
// EditHex2Char()
//
void EditHex2Char(void) {
	Sci_Position count = SciCall_GetSelTextLength() - 1;
	if (count == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	count *= 2 + MAX_ESCAPE_HEX_DIGIT;
	char *ch = (char *)NP2HeapAlloc(count + 1);
	WCHAR *wch = (WCHAR *)NP2HeapAlloc((count + 1) * sizeof(WCHAR));
	const UINT cpEdit = SciCall_GetCodePage();
	int ci = 0;
	int cch = 0;

	SciCall_GetSelText(ch);

	const uint8_t *p = (const uint8_t *)ch;
	while (*p) {
		if (*p == '\\') {
			p++;
			if (*p == 'x' || *p == 'u') {
				p++;
				ci = 0;
				int ucc = 0;
				while (*p && (ucc++ < MAX_ESCAPE_HEX_DIGIT)) {
					const int hex = GetHexDigit(*p);
					if (hex < 0) {
						break;
					}
					ci = (ci << 4) | hex;
					p++;
				}
			} else {
				wch[cch++] = L'\\';
				ci = *p++;
			}
		} else {
			ci = *p++;
		}
		wch[cch++] = (WCHAR)ci;
		if (ci == 0) {
			break;
		}
	}

	wch[cch] = L'\0';
	cch = WideCharToMultiByte(cpEdit, 0, wch, -1, ch, (int)(count + 1), NULL, NULL) - 1; // '\0'
	EditReplaceMainSelection(cch, ch);

	NP2HeapFree(ch);
	NP2HeapFree(wch);
}

void EditShowHex(void) {
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	const Sci_Position count = SciCall_GetSelTextLength() - 1;
	if (count == 0) {
		return;
	}
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	char *ch = (char *)NP2HeapAlloc(count + 1);
	char *cch = (char *)NP2HeapAlloc(count * 3 + 3);
	SciCall_GetSelText(ch);
	const uint8_t *p = (const uint8_t *)ch;
	char *t = cch;
	*t++ = '[';
	while (*p) {
		int c = *p++;
		int v = c >> 4;
		*t++ = (char)((v >= 10) ? v - 10 + 'a' : v + '0');
		v = c & 0x0f;
		*t++ = (char)((v >= 10) ? v - 10 + 'a' : v + '0');
		*t++ = ' ';
	}
	*--t = ']';

	SciCall_InsertText(iSelEnd, cch);
	SciCall_SetSel(iSelEnd, iSelEnd + strlen(cch));
	NP2HeapFree(ch);
	NP2HeapFree(cch);
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
	const Sci_Position count = SciCall_GetSelTextLength() - 1;
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
	uint64_t ci = 0;

	SciCall_GetSelText(ch);

	while (*p) {
		if (*p == '0') {
			ci = 0;
			p++;
			if ((*p == 'x' || *p == 'X') && radix != 16) {
				p++;
				while (*p) {
					if (*p == '_') {
						p++;
					} else {
						const int hex = GetHexDigit(*p);
						if (hex < 0) {
							break;
						}
						ci = (ci << 4) | hex;
						p++;
					}
				}
				cch += ConvertNumRadix(tch + cch, ci, radix);
			} else if ((*p == 'o' || *p == 'O') && radix != 8) {
				p++;
				while (*p) {
					if (*p >= '0' && *p <= '7') {
						ci <<= 3;
						ci += (*p++ - '0');
					} else if (*p == '_') {
						p++;
					} else {
						break;
					}
				}
				cch += ConvertNumRadix(tch + cch, ci, radix);
			} else if ((*p == 'b' || *p == 'B') && radix != 2) {
				p++;
				while (*p) {
					if (*p == '0') {
						ci <<= 1;
						p++;
					} else if (*p == '1') {
						ci <<= 1;
						ci |= 1;
						p++;
					} else if (*p == '_') {
						p++;
					} else {
						break;
					}
				}
				cch += ConvertNumRadix(tch + cch, ci, radix);
			} else if ((*p >= '0' && *p <= '9') && radix != 10) {
				ci = *p++ - '0';
				while (*p) {
					if (*p >= '0' && *p <= '9') {
						ci *= 10;
						ci += (*p++ - '0');
					} else if (*p == '_') {
						p++;
					} else {
						break;
					}
				}
				cch += ConvertNumRadix(tch + cch, ci, radix);
			} else {
				tch[cch++] = '0';
			}
		} else if ((*p >= '1' && *p <= '9') && radix != 10) {
			ci = *p++ - '0';
			while (*p) {
				if (*p >= '0' && *p <= '9') {
					ci *= 10;
					ci += (*p++ - '0');
				} else if (*p == '_') {
					p++;
				} else {
					break;
				}
			}
			cch += ConvertNumRadix(tch + cch, ci, radix);
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
void EditModifyNumber(BOOL bIncrease) {
	const Sci_Position iSelCount = SciCall_GetSelTextLength() - 1;
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
		if (strchr(chNumber, '-')) {
			return;
		}

		const char *ptr = strpbrk(chNumber, "xX");
		const int radix = (ptr != NULL) ? 16 : 10;
		char *end;
		int iNumber = (int)strtol(chNumber, &end, radix);
		if (end == chNumber || iNumber < 0) {
			return;
		}

		if (bIncrease && iNumber < INT_MAX) {
			iNumber++;
		}
		if (!bIncrease && iNumber > 0) {
			iNumber--;
		}

		const int iWidth = (int)strlen(chNumber) - ((ptr != NULL) ? 2 : 0);
		if (ptr != NULL) {
			if (*ptr == 'X') {
				sprintf(chNumber, "%#0*X", iWidth, iNumber);
			} else {
				sprintf(chNumber, "%#0*x", iWidth, iNumber);
			}
		} else {
			sprintf(chNumber, "%0*i", iWidth, iNumber);
		}
		EditReplaceMainSelection(strlen(chNumber), chNumber);
	}
}

//=============================================================================
//
// EditTabsToSpaces()
//
void EditTabsToSpaces(int nTabWidth, BOOL bOnlyIndentingWS) {
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

	struct Sci_TextRange tr = { { iSelStart, iSelEnd }, pszText};
	SciCall_GetTextRange(&tr);

	const UINT cpEdit = SciCall_GetCodePage();
	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	NP2HeapFree(pszText);

	LPWSTR pszConvW = (LPWSTR)NP2HeapAlloc(cchTextW * sizeof(WCHAR) * nTabWidth + 2);
	int cchConvW = 0;

	BOOL bIsLineStart = TRUE;
	BOOL bModified = FALSE;
	// Contributed by Homam, Thank you very much!
	int i = 0;
	for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
		const WCHAR w = pszTextW[iTextW];
		if (w == L'\t' && (!bOnlyIndentingWS || bIsLineStart)) {
			for (int j = 0; j < nTabWidth - i % nTabWidth; j++) {
				pszConvW[cchConvW++] = L' ';
			}
			i = 0;
			bModified = TRUE;
		} else {
			i++;
			if (w == L'\n' || w == L'\r') {
				i = 0;
				bIsLineStart = TRUE;
			} else if (w != L' ') {
				bIsLineStart = FALSE;
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
void EditSpacesToTabs(int nTabWidth, BOOL bOnlyIndentingWS) {
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

	struct Sci_TextRange tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRange(&tr);

	const UINT cpEdit = SciCall_GetCodePage();

	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	NP2HeapFree(pszText);

	LPWSTR pszConvW = (LPWSTR)NP2HeapAlloc(cchTextW * sizeof(WCHAR) + 2);
	int cchConvW = 0;

	BOOL bIsLineStart = TRUE;
	BOOL bModified = FALSE;
	// Contributed by Homam, Thank you very much!
	int i = 0;
	int j = 0;
	WCHAR space[256];
	for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
		const WCHAR w = pszTextW[iTextW];
		if ((w == L' ' || w == L'\t') && (!bOnlyIndentingWS || bIsLineStart)) {
			space[j++] = w;
			if (j == nTabWidth - i % nTabWidth || w == L'\t') {
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
				bIsLineStart = TRUE;
			} else {
				bIsLineStart = FALSE;
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
	const Sci_Line iCurLine = SciCall_LineFromPosition(iCurPos);
	const Sci_Line iAnchorLine = SciCall_LineFromPosition(iAnchorPos);

	if (iCurLine == iAnchorLine) {
		const Sci_Position iLineCurPos = iCurPos - SciCall_PositionFromLine(iCurLine);
		const Sci_Position iLineAnchorPos = iAnchorPos - SciCall_PositionFromLine(iAnchorLine);
		//const Sci_Line iLineCur = SciCall_DocLineFromVisible(iCurLine);
		//const Sci_Line iLineAnchor = SciCall_DocLineFromVisible(iAnchorLine);
		//if (iLineCur == iLineAnchor) {
		//}

		if (iCurLine > 0) {
			SciCall_BeginUndoAction();
			SciCall_LineTranspose();
			SciCall_SetSel(SciCall_PositionFromLine(iAnchorLine - 1) + iLineAnchorPos, SciCall_PositionFromLine(iCurLine - 1) + iLineCurPos);
			SciCall_ChooseCaretX();
			SciCall_EndUndoAction();
		}
		return;
	}

	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	const Sci_Line iLineSrc = min_pos(iCurLine, iAnchorLine) - 1;
	//const Sci_Line iLineCur = SciCall_DocLineFromVisible(iCurLine);
	//const Sci_Line iLineAnchor = SciCall_DocLineFromVisible(iAnchorLine);
	//if (iLineCur == iLineAnchor) {
	//}

	if (iLineSrc >= 0) {
		const Sci_Position cLine = SciCall_GetLineLength(iLineSrc);
		char *pLine = (char *)NP2HeapAlloc(cLine + 1);
		SciCall_GetLine(iLineSrc, pLine);

		const Sci_Position iLineSrcStart = SciCall_PositionFromLine(iLineSrc);
		const Sci_Position iLineSrcEnd = SciCall_PositionFromLine(iLineSrc + 1);

		Sci_Line iLineDest = max_pos(iCurLine, iAnchorLine);
		if (max_pos(iCurPos, iAnchorPos) <= SciCall_PositionFromLine(iLineDest)) {
			if (iLineDest >= 1) {
				iLineDest--;
			}
		}

		SciCall_BeginUndoAction();

		SciCall_SetTargetRange(iLineSrcStart, iLineSrcEnd);
		SciCall_ReplaceTarget(0, "");

		const Sci_Position iLineDestStart = SciCall_PositionFromLine(iLineDest);
		SciCall_InsertText(iLineDestStart, pLine);
		NP2HeapFree(pLine);

		if (iLineDest == SciCall_GetLineCount() - 1) {
			char chaEOL[] = "\r\n";
			const int iEOLMode = SciCall_GetEOLMode();
			if (iEOLMode == SC_EOL_CR) {
				chaEOL[1] = 0;
			} else if (iEOLMode == SC_EOL_LF) {
				chaEOL[0] = '\n';
				chaEOL[1] = 0;
			}

			SciCall_InsertText(iLineDestStart, chaEOL);
			SciCall_SetTargetRange(SciCall_GetLineEndPosition(iLineDest), SciCall_GetLength());
			SciCall_ReplaceTarget(0, "");
		}

		if (iCurPos < iAnchorPos) {
			iCurPos = SciCall_PositionFromLine(iCurLine - 1);
			iAnchorPos = SciCall_PositionFromLine(iLineDest);
		} else {
			iAnchorPos = SciCall_PositionFromLine(iAnchorLine - 1);
			iCurPos = SciCall_PositionFromLine(iLineDest);
		}

		SciCall_SetSel(iAnchorPos, iCurPos);

		SciCall_EndUndoAction();
	}
}

//=============================================================================
//
// EditMoveDown()
//
void EditMoveDown(void) {
	Sci_Position iCurPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();
	const Sci_Line iCurLine = SciCall_LineFromPosition(iCurPos);
	const Sci_Line iAnchorLine = SciCall_LineFromPosition(iAnchorPos);

	if (iCurLine == iAnchorLine) {
		const Sci_Position iLineCurPos = iCurPos - SciCall_PositionFromLine(iCurLine);
		const Sci_Position iLineAnchorPos = iAnchorPos - SciCall_PositionFromLine(iAnchorLine);
		//const Sci_Line iLineCur = SciCall_DocLineFromVisible(iCurLine);
		//const Sci_Line iLineAnchor = SciCall_DocLineFromVisible(iAnchorLine);
		//if (iLineCur == iLineAnchor) {
		//}

		if (iCurLine < SciCall_GetLineCount() - 1) {
			SciCall_BeginUndoAction();
			SciCall_GotoLine(iCurLine + 1);
			SciCall_LineTranspose();
			SciCall_SetSel(SciCall_PositionFromLine(iAnchorLine + 1) + iLineAnchorPos, SciCall_PositionFromLine(iCurLine + 1) + iLineCurPos);
			SciCall_ChooseCaretX();
			SciCall_EndUndoAction();
		}
		return;
	}

	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	Sci_Line iLineSrc = max_pos(iCurLine, iAnchorLine) + 1;
	//const Sci_Line iLineCur = SciCall_DocLineFromVisible(iCurLine);
	//const Sci_Line iLineAnchor = SciCall_DocLineFromVisible(iAnchorLine);
	//if (iLineCur == iLineAnchor) {
	//}

	if (max_pos(iCurPos, iAnchorPos) <= SciCall_PositionFromLine(iLineSrc - 1)) {
		if (iLineSrc >= 1) {
			iLineSrc--;
		}
	}

	if (iLineSrc <= SciCall_GetLineCount() - 1) {
		const BOOL bLastLine = (iLineSrc == SciCall_GetLineCount() - 1);

		if (bLastLine &&
				(SciCall_GetLineEndPosition(iLineSrc) == SciCall_PositionFromLine(iLineSrc)) &&
				(SciCall_GetLineEndPosition(iLineSrc - 1) == SciCall_PositionFromLine(iLineSrc - 1))) {
			return;
		}

		if (bLastLine) {
			char chaEOL[] = "\r\n";
			const int iEOLMode = SciCall_GetEOLMode();
			if (iEOLMode == SC_EOL_CR) {
				chaEOL[1] = 0;
			} else if (iEOLMode == SC_EOL_LF) {
				chaEOL[0] = '\n';
				chaEOL[1] = 0;
			}
			SciCall_AppendText(strlen(chaEOL), chaEOL);
		}

		const Sci_Position cLine = SciCall_GetLineLength(iLineSrc);
		char *pLine = (char *)NP2HeapAlloc(cLine + 3);
		SciCall_GetLine(iLineSrc, pLine);

		const Sci_Position iLineSrcStart = SciCall_PositionFromLine(iLineSrc);
		const Sci_Position iLineSrcEnd = SciCall_PositionFromLine(iLineSrc + 1);
		const Sci_Line iLineDest = min_pos(iCurLine, iAnchorLine);

		SciCall_BeginUndoAction();

		SciCall_SetTargetRange(iLineSrcStart, iLineSrcEnd);
		SciCall_ReplaceTarget(0, "");

		const Sci_Position iLineDestStart = SciCall_PositionFromLine(iLineDest);
		SciCall_InsertText(iLineDestStart, pLine);

		if (bLastLine) {
			SciCall_SetTargetRange(SciCall_GetLineEndPosition(SciCall_GetLineCount() - 2), SciCall_GetLength());
			SciCall_ReplaceTarget(0, "");
		}

		NP2HeapFree(pLine);

		if (iCurPos < iAnchorPos) {
			iCurPos = SciCall_PositionFromLine(iCurLine + 1);
			iAnchorPos = SciCall_PositionFromLine(iLineSrc + 1);
		} else {
			iAnchorPos = SciCall_PositionFromLine(iAnchorLine + 1);
			iCurPos = SciCall_PositionFromLine(iLineSrc + 1);
		}

		SciCall_SetSel(iAnchorPos, iCurPos);

		SciCall_EndUndoAction();
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

//=============================================================================
//
// EditModifyLines()
//
void EditModifyLines(LPCWSTR pwszPrefix, LPCWSTR pwszAppend) {
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

	const int iPrefixLen = lstrlen(pwszPrefix);
	char *mszPrefix1 = NULL;
	int iPrefixLine = 0;
	if (iPrefixLen != 0) {
		const int size = iPrefixLen * kMaxMultiByteCount + 1;
		mszPrefix1 = (char *)NP2HeapAlloc(size);
		WideCharToMultiByte(cpEdit, 0, pwszPrefix, -1, mszPrefix1, size, NULL, NULL);
		ConvertWinEditLineEndingsEx(mszPrefix1, iEOLMode, &iPrefixLine);
	}

	const int iAppendLen = lstrlen(pwszAppend);
	char *mszAppend1 = NULL;
	int iAppendLine = 0;
	if (iAppendLen != 0) {
		const int size = iAppendLen * kMaxMultiByteCount + 1;
		mszAppend1 = (char *)NP2HeapAlloc(size);
		WideCharToMultiByte(cpEdit, 0, pwszAppend, -1, mszAppend1, size, NULL, NULL);
		ConvertWinEditLineEndingsEx(mszAppend1, iEOLMode, &iAppendLine);
	}

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

	BOOL bPrefixNumPadZero = FALSE;
	char *mszPrefix2 = NULL;
	Sci_Line iPrefixNum = 0;
	int iPrefixNumWidth = 1;
	BOOL bPrefixNum = FALSE;

	BOOL bAppendNumPadZero = FALSE;
	char *mszAppend2 = NULL;
	Sci_Line iAppendNum = 0;
	int iAppendNumWidth = 1;
	BOOL bAppendNum = FALSE;

	if (iPrefixLen != 0) {
		char *p = mszPrefix1;
		Sci_Line lineCount = 0;
		mszPrefix2 = (char *)NP2HeapAlloc(iPrefixLen * kMaxMultiByteCount + 1);
		while (!bPrefixNum && (p = strstr(p, "$(")) != NULL) {
			if (StrStartsWith(p, "$(I)")) {
				*p = 0;
				strcpy(mszPrefix2, p + CSTRLEN("$(I)"));
				bPrefixNum = TRUE;
				iPrefixNum = 0;
				lineCount = iLineEnd - iLineStart;
				bPrefixNumPadZero = FALSE;
			} else if (StrStartsWith(p, "$(0I)")) {
				*p = 0;
				strcpy(mszPrefix2, p + CSTRLEN("$(0I)"));
				bPrefixNum = TRUE;
				iPrefixNum = 0;
				lineCount = iLineEnd - iLineStart;
				bPrefixNumPadZero = TRUE;
			} else if (StrStartsWith(p, "$(N)")) {
				*p = 0;
				strcpy(mszPrefix2, p + CSTRLEN("$(N)"));
				bPrefixNum = TRUE;
				iPrefixNum = 1;
				lineCount = iLineEnd - iLineStart + 1;
				bPrefixNumPadZero = FALSE;
			} else if (StrStartsWith(p, "$(0N)")) {
				*p = 0;
				strcpy(mszPrefix2, p + CSTRLEN("$(0N)"));
				bPrefixNum = TRUE;
				iPrefixNum = 1;
				lineCount = iLineEnd - iLineStart + 1;
				bPrefixNumPadZero = TRUE;
			} else if (StrStartsWith(p, "$(L)")) {
				*p = 0;
				strcpy(mszPrefix2, p + CSTRLEN("$(L)"));
				bPrefixNum = TRUE;
				iPrefixNum = iLineStart + 1;
				lineCount = iLineEnd + 1;
				bPrefixNumPadZero = FALSE;
			} else if (StrStartsWith(p, "$(0L)")) {
				*p = 0;
				strcpy(mszPrefix2, p + CSTRLEN("$(0L)"));
				bPrefixNum = TRUE;
				iPrefixNum = iLineStart + 1;
				lineCount = iLineEnd + 1;
				bPrefixNumPadZero = TRUE;
			}
			p += CSTRLEN("$(");
		}
		if (bPrefixNum) {
			while (lineCount >= 10) {
				++iPrefixNumWidth;
				lineCount /= 10;
			}
		}
	}

	if (iAppendLen != 0) {
		char *p = mszAppend1;
		Sci_Line lineCount = 0;
		mszAppend2 = (char *)NP2HeapAlloc(iAppendLen * kMaxMultiByteCount + 1);
		while (!bAppendNum && (p = strstr(p, "$(")) != NULL) {
			if (StrStartsWith(p, "$(I)")) {
				*p = 0;
				strcpy(mszAppend2, p + CSTRLEN("$(I)"));
				bAppendNum = TRUE;
				iAppendNum = 0;
				lineCount = iLineEnd - iLineStart;
				bAppendNumPadZero = FALSE;
			} else if (StrStartsWith(p, "$(0I)")) {
				*p = 0;
				strcpy(mszAppend2, p + CSTRLEN("$(0I)"));
				bAppendNum = TRUE;
				iAppendNum = 0;
				lineCount = iLineEnd - iLineStart;
				bAppendNumPadZero = TRUE;
			} else if (StrStartsWith(p, "$(N)")) {
				*p = 0;
				strcpy(mszAppend2, p + CSTRLEN("$(N)"));
				bAppendNum = TRUE;
				iAppendNum = 1;
				lineCount = iLineEnd - iLineStart + 1;
				bAppendNumPadZero = FALSE;
			} else if (StrStartsWith(p, "$(0N)")) {
				*p = 0;
				strcpy(mszAppend2, p + CSTRLEN("$(0N)"));
				bAppendNum = TRUE;
				iAppendNum = 1;
				lineCount = iLineEnd - iLineStart + 1;
				bAppendNumPadZero = TRUE;
			} else if (StrStartsWith(p, "$(L)")) {
				*p = 0;
				strcpy(mszAppend2, p + CSTRLEN("$(L)"));
				bAppendNum = TRUE;
				iAppendNum = iLineStart + 1;
				lineCount = iLineEnd + 1;
				bAppendNumPadZero = FALSE;
			} else if (StrStartsWith(p, "$(0L)")) {
				*p = 0;
				strcpy(mszAppend2, p + CSTRLEN("$(0L)"));
				bAppendNum = TRUE;
				iAppendNum = iLineStart + 1;
				lineCount = iLineEnd + 1;
				bAppendNumPadZero = TRUE;
			}
			p += CSTRLEN("$(");
		}
		if (bAppendNum) {
			while (lineCount >= 10) {
				++iAppendNumWidth;
				lineCount /= 10;
			}
		}
	}

	char *mszInsert = (char *)NP2HeapAlloc(2 * max_i(iPrefixLen, iAppendLen) * kMaxMultiByteCount + 1);
	SciCall_BeginUndoAction();
	for (Sci_Line iLine = iLineStart, iLineDest = iLineStart; iLine <= iLineEnd; iLine++, iLineDest++) {
		if (iPrefixLen != 0) {
			strcpy(mszInsert, mszPrefix1);

			if (bPrefixNum) {
				char tchNum[64];
#if defined(_WIN64)
				if (bPrefixNumPadZero) {
					sprintf(tchNum, "%0*" PRId64, iPrefixNumWidth, iPrefixNum);
				} else {
					sprintf(tchNum, "%*" PRId64, iPrefixNumWidth, iPrefixNum);
				}
#else
				if (bPrefixNumPadZero) {
					sprintf(tchNum, "%0*d", iPrefixNumWidth, (int)iPrefixNum);
				} else {
					sprintf(tchNum, "%*d", iPrefixNumWidth, (int)iPrefixNum);
				}
#endif
				strcat(mszInsert, tchNum);
				strcat(mszInsert, mszPrefix2);
				iPrefixNum++;
			}

			const Sci_Position iPos = SciCall_PositionFromLine(iLineDest);
			SciCall_SetTargetRange(iPos, iPos);
			SciCall_ReplaceTarget(strlen(mszInsert), mszInsert);
			iLineDest += iPrefixLine;
		}

		if (iAppendLen != 0) {
			strcpy(mszInsert, mszAppend1);

			if (bAppendNum) {
				char tchNum[64];
#if defined(_WIN64)
				if (bAppendNumPadZero) {
					sprintf(tchNum, "%0*" PRId64, iAppendNumWidth, iAppendNum);
				} else {
					sprintf(tchNum, "%*" PRId64, iAppendNumWidth, iAppendNum);
				}
#else
				if (bAppendNumPadZero) {
					sprintf(tchNum, "%0*d", iAppendNumWidth, (int)iAppendNum);
				} else {
					sprintf(tchNum, "%*d", iAppendNumWidth, (int)iAppendNum);
				}
#endif
				strcat(mszInsert, tchNum);
				strcat(mszInsert, mszAppend2);
				iAppendNum++;
			}

			const Sci_Position iPos = SciCall_GetLineEndPosition(iLineDest);
			SciCall_SetTargetRange(iPos, iPos);
			SciCall_ReplaceTarget(strlen(mszInsert), mszInsert);
			iLineDest += iAppendLine;
		}
	}
	SciCall_EndUndoAction();

	//// Fix selection
	//if (iSelStart != iSelEnd && SciCall_GetTargetEnd() > SciCall_GetSelectionEnd()) {
	//	Sci_Position iCurPos = SciCall_GetCurrentPos();
	//	Sci_Position iAnchorPos = SciCall_GetAnchor();
	//	if (iCurPos > iAnchorPos)
	//		iCurPos = SciCall_GetTargetEnd();
	//	else
	//		iAnchorPos = SciCall_GetTargetEnd();
	//	SciCall_SetSel(iAnchorPos, iCurPos);
	//}

	// extend selection to start of first line
	// the above code is not required when last line has been excluded
	if (iSelStart != iSelEnd) {
		Sci_Position iCurPos = SciCall_GetCurrentPos();
		Sci_Position iAnchorPos = SciCall_GetAnchor();
		if (iCurPos < iAnchorPos) {
			iCurPos = SciCall_PositionFromLine(iLineStart);
			iAnchorPos = SciCall_PositionFromLine(iLineEnd + 1);
		} else {
			iAnchorPos = SciCall_PositionFromLine(iLineStart);
			iCurPos = SciCall_PositionFromLine(iLineEnd + 1);
		}
		SciCall_SetSel(iAnchorPos, iCurPos);
	}

	EndWaitCursor();
	if (mszPrefix1 != NULL) {
		NP2HeapFree(mszPrefix1);
	}
	if (mszAppend1 != NULL) {
		NP2HeapFree(mszAppend1);
	}
	if (mszPrefix2 != NULL) {
		NP2HeapFree(mszPrefix2);
	}
	if (mszAppend2 != NULL) {
		NP2HeapFree(mszAppend2);
	}
	NP2HeapFree(mszInsert);
}

//=============================================================================
//
// EditAlignText()
//
void EditAlignText(int nMode) {
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

	BOOL bModified = FALSE;
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

		if (iLineIndentPos != iLineEndPos) {
			const Sci_Position iIndentCol = SciCall_GetLineIndentation(iLine);
			Sci_Position iTail = iLineEndPos - 1;
			int ch = SciCall_GetCharAt(iTail);
			while (iTail >= iLineStart && (ch == ' ' || ch == '\t')) {
				iTail--;
				ch = SciCall_GetCharAt(iTail);
				iLineEndPos--;
			}

			const Sci_Position iEndCol = SciCall_GetColumn(iLineEndPos);
			iMinIndent = min_pos(iMinIndent, iIndentCol);
			iMaxLength = max_pos(iMaxLength, iEndCol);
		}
	}

	if (iMaxLength < BUFSIZE_ALIGN) {
		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iIndentPos = SciCall_GetLineIndentPosition(iLine);
			const Sci_Position iEndPos = SciCall_GetLineEndPosition(iLine);

			if (iIndentPos == iEndPos && iEndPos > 0) {
				if (!bModified) {
					SciCall_BeginUndoAction();
					bModified = TRUE;
				}

				SciCall_SetTargetRange(SciCall_PositionFromLine(iLine), iEndPos);
				SciCall_ReplaceTarget(0, "");
			} else {
				char tchLineBuf[BUFSIZE_ALIGN * kMaxMultiByteCount] = "";
				WCHAR wchLineBuf[BUFSIZE_ALIGN] = L"";
				WCHAR *pWords[BUFSIZE_ALIGN];
				WCHAR *p = wchLineBuf;

				int iWords = 0;
				Sci_Position iWordsLength = 0;
				const Sci_Position cchLine = SciCall_GetLine(iLine, tchLineBuf);

				if (!bModified) {
					SciCall_BeginUndoAction();
					bModified = TRUE;
				}

				MultiByteToWideChar(cpEdit, 0, tchLineBuf, (int)cchLine, wchLineBuf, COUNTOF(wchLineBuf));
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
						*p++ = 0;
					}
				}

				if (iWords > 0) {
					if (nMode == ALIGN_JUSTIFY || nMode == ALIGN_JUSTIFY_EX) {
						BOOL bNextLineIsBlank = FALSE;
						if (nMode == ALIGN_JUSTIFY_EX) {
							if (SciCall_GetLineCount() <= iLine + 1) {
								bNextLineIsBlank = TRUE;
							} else {
								const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iLine + 1);
								const Sci_Position iLineIndentPos = SciCall_GetLineIndentPosition(iLine + 1);
								if (iLineIndentPos == iLineEndPos) {
									bNextLineIsBlank = TRUE;
								}
							}
						}

						if ((nMode == ALIGN_JUSTIFY || nMode == ALIGN_JUSTIFY_EX) &&
								iWords > 1 && iWordsLength >= 2 &&
								((nMode != ALIGN_JUSTIFY_EX || !bNextLineIsBlank || iLineStart == iLineEnd) ||
								 (bNextLineIsBlank && iWordsLength*4 > (iMaxLength - iMinIndent)*3))) {
							const int iGaps = iWords - 1;
							const Sci_Position iSpacesPerGap = (iMaxLength - iMinIndent - iWordsLength) / iGaps;
							const Sci_Position iExtraSpaces = (iMaxLength - iMinIndent - iWordsLength) % iGaps;

							WCHAR wchNewLineBuf[BUFSIZE_ALIGN * 3];
							lstrcpy(wchNewLineBuf, pWords[0]);
							p = StrEnd(wchNewLineBuf);

							for (int i = 1; i < iWords; i++) {
								for (Sci_Position j = 0; j < iSpacesPerGap; j++) {
									*p++ = L' ';
									*p = 0;
								}
								if (i > iGaps - iExtraSpaces) {
									*p++ = L' ';
									*p = 0;
								}
								lstrcat(p, pWords[i]);
								p = StrEnd(p);
							}

							WideCharToMultiByte(cpEdit, 0, wchNewLineBuf, -1, tchLineBuf, COUNTOF(tchLineBuf), NULL, NULL);

							SciCall_SetTargetRange(SciCall_PositionFromLine(iLine), SciCall_GetLineEndPosition(iLine));
							SciCall_ReplaceTarget(strlen(tchLineBuf), tchLineBuf);

							SciCall_SetLineIndentation(iLine, iMinIndent);
						} else {
							WCHAR wchNewLineBuf[BUFSIZE_ALIGN];
							lstrcpy(wchNewLineBuf, pWords[0]);
							p = StrEnd(wchNewLineBuf);

							for (int i = 1; i < iWords; i++) {
								*p++ = L' ';
								*p = 0;
								lstrcat(wchNewLineBuf, pWords[i]);
								p = StrEnd(p);
							}

							WideCharToMultiByte(cpEdit, 0, wchNewLineBuf, -1, tchLineBuf, COUNTOF(tchLineBuf), NULL, NULL);

							SciCall_SetTargetRange(SciCall_PositionFromLine(iLine), SciCall_GetLineEndPosition(iLine));
							SciCall_ReplaceTarget(strlen(tchLineBuf), tchLineBuf);

							SciCall_SetLineIndentation(iLine, iMinIndent);
						}
					} else {
						const Sci_Position iExtraSpaces = iMaxLength - iMinIndent - iWordsLength - iWords + 1;
						Sci_Position iOddSpaces = iExtraSpaces % 2;

						WCHAR wchNewLineBuf[BUFSIZE_ALIGN * 3] = L"";
						p = wchNewLineBuf;

						if (nMode == ALIGN_RIGHT) {
							for (Sci_Position i = 0; i < iExtraSpaces; i++) {
								*p++ = L' ';
							}
							*p = 0;
						}

						if (nMode == ALIGN_CENTER) {
							for (Sci_Position i = 1; i < iExtraSpaces - iOddSpaces; i += 2) {
								*p++ = L' ';
							}
							*p = 0;
						}

						for (int i = 0; i < iWords; i++) {
							lstrcat(p, pWords[i]);
							if (i < iWords - 1) {
								lstrcat(p, L" ");
							}
							if (nMode == ALIGN_CENTER && iWords > 1 && iOddSpaces > 0 && i + 1 >= iWords / 2) {
								lstrcat(p, L" ");
								iOddSpaces--;
							}
							p = StrEnd(p);
						}

						WideCharToMultiByte(cpEdit, 0, wchNewLineBuf, -1, tchLineBuf, COUNTOF(tchLineBuf), NULL, NULL);

						Sci_Position iPos;
						if (nMode == ALIGN_RIGHT || nMode == ALIGN_CENTER) {
							SciCall_SetLineIndentation(iLine, iMinIndent);
							iPos = SciCall_GetLineIndentPosition(iLine);
						} else {
							iPos = SciCall_PositionFromLine(iLine);
						}

						SciCall_SetTargetRange(iPos, SciCall_GetLineEndPosition(iLine));
						SciCall_ReplaceTarget(strlen(tchLineBuf), tchLineBuf);

						if (nMode == ALIGN_LEFT) {
							SciCall_SetLineIndentation(iLine, iMinIndent);
						}
					}
				}
			}
		}
		if (bModified) {
			SciCall_EndUndoAction();
		}
	} else {
		MsgBoxInfo(MB_OK, IDS_BUFFERTOOSMALL);
	}

	if (iCurPos < iAnchorPos) {
		iCurPos = SciCall_PositionFromLine(iLineStart);
		iAnchorPos = SciCall_PositionFromLine(iLineEnd + 1);
	} else {
		iAnchorPos = SciCall_PositionFromLine(iLineStart);
		iCurPos = SciCall_PositionFromLine(iLineEnd + 1);
	}
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
		SciCall_ReplaceTarget(strlen(mszClose), mszClose);
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
void EditToggleLineComments(LPCWSTR pwszComment, BOOL bInsertAtStart) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return;
	}

	BeginWaitCursor();

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	Sci_Position iCurPos = SciCall_GetCurrentPos();

	char mszComment[256] = "";
	const UINT cpEdit = SciCall_GetCodePage();
	WideCharToMultiByte(cpEdit, 0, pwszComment, -1, mszComment, COUNTOF(mszComment), NULL, NULL);

	const int cchComment = (int)strlen(mszComment);
	const Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
	Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);

	if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
		if (iLineEnd - iLineStart >= 1) {
			iLineEnd--;
		}
	}

	Sci_Position iCommentCol = 0;
	if (!bInsertAtStart) {
		iCommentCol = 1024;
		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iLine);
			const Sci_Position iLineIndentPos = SciCall_GetLineIndentPosition(iLine);

			if (iLineIndentPos != iLineEndPos) {
				const Sci_Position iIndentColumn = SciCall_GetColumn(iLineIndentPos);
				iCommentCol = min_pos(iCommentCol, iIndentColumn);
			}
		}
	}

	SciCall_BeginUndoAction();
	int iAction = 0;

	for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
		const Sci_Position iIndentPos = SciCall_GetLineIndentPosition(iLine);
		BOOL bWhitespaceLine = FALSE;
		// a line with [space/tab] only
		if (iCommentCol && iIndentPos == SciCall_GetLineEndPosition(iLine)) {
			//continue;
			bWhitespaceLine = TRUE;
		}

		char tchBuf[32] = "";
		struct Sci_TextRange tr = { { iIndentPos, iIndentPos + min_i(31, cchComment) }, tchBuf };
		SciCall_GetTextRange(&tr);

		Sci_Position iCommentPos;
		if (StrStartsWithCaseEx(tchBuf, mszComment, cchComment)) {
			int ch;
			switch (iAction) {
			case 0:
				iAction = 2;
				FALLTHROUGH_ATTR;
				// fall through
			case 2:
				iCommentPos = iIndentPos;
				// a line with [space/tab] comment only
				ch = SciCall_GetCharAt(iIndentPos + cchComment);
				if (ch == '\n' || ch == '\r') {
					iCommentPos = SciCall_PositionFromLine(iLine);
				}
				SciCall_SetTargetRange(iCommentPos, iIndentPos + cchComment);
				SciCall_ReplaceTarget(0, "");
				break;
			case 1:
				iCommentPos = SciCall_FindColumn(iLine, iCommentCol);
				ch = SciCall_GetCharAt(iCommentPos);
				if (ch == '\t' || ch == ' ') {
					SciCall_InsertText(iCommentPos, mszComment);
				}
				break;
			}
		} else {
			switch (iAction) {
			case 0:
				iAction = 1;
				FALLTHROUGH_ATTR;
				// fall through
			case 1:
				iCommentPos = SciCall_FindColumn(iLine, iCommentCol);
				if (!bWhitespaceLine || (iLineStart == iLineEnd)) {
					SciCall_InsertText(iCommentPos, mszComment);
				} else {
					char tchComment[1024] = "";
					Sci_Position tab = 0;
					Sci_Position count = iCommentCol;
					const int tabWidth = fvCurFile.iTabWidth;
					if (!fvCurFile.bTabsAsSpaces && tabWidth > 0) {
						tab = iCommentCol / tabWidth;
						FillMemory(tchComment, tab, '\t');
						count -= tab * tabWidth;
					}
					FillMemory(tchComment + tab, count, ' ');
					strcat(tchComment, mszComment);
					SciCall_InsertText(iCommentPos, tchComment);
				}
				break;
			case 2:
				break;
			}
		}
	}

	SciCall_EndUndoAction();

	if (iSelStart != iSelEnd) {
		Sci_Position iAnchorPos;
		if (iCurPos == iSelStart) {
			iCurPos = SciCall_PositionFromLine(iLineStart);
			iAnchorPos = SciCall_PositionFromLine(iLineEnd + 1);
		} else {
			iAnchorPos = SciCall_PositionFromLine(iLineStart);
			iCurPos = SciCall_PositionFromLine(iLineEnd + 1);
		}
		SciCall_SetSel(iAnchorPos, iCurPos);
	}

	EndWaitCursor();
}

//=============================================================================
//
// EditPadWithSpaces()
//
void EditPadWithSpaces(BOOL bSkipEmpty, BOOL bNoUndoGroup) {
	Sci_Position iMaxColumn = 0;
	BOOL bReducedSelection = FALSE;

	Sci_Position iSelStart = 0;
	Sci_Position iSelEnd = 0;

	Sci_Line iLineStart = 0;
	Sci_Line iLineEnd = 0;

	Sci_Line iRcCurLine = 0;
	Sci_Line iRcAnchorLine = 0;
	Sci_Position iRcCurCol = 0;
	Sci_Position iRcAnchorCol = 0;

	const BOOL bIsRectangular = SciCall_IsRectangleSelection();
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
					bReducedSelection = TRUE;
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
			if (iPos != INVALID_POSITION) {
				iMaxColumn = max_pos(iMaxColumn, SciCall_GetColumn(iPos));
			}
		}
	}

	char *pmszPadStr = (char *)NP2HeapAlloc((iMaxColumn + 1) * sizeof(char));
	if (pmszPadStr) {
		FillMemory(pmszPadStr, NP2HeapSize(pmszPadStr), ' ');

		if (!bNoUndoGroup) {
			SciCall_BeginUndoAction();
		}

		for (Sci_Line iLine = iLineStart; iLine <= iLineEnd; iLine++) {
			const Sci_Position iLineSelEndPos = SciCall_GetLineSelEndPosition(iLine);
			if (bIsRectangular && INVALID_POSITION == iLineSelEndPos) {
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

		if (iCurPos < iAnchorPos) {
			iCurPos = SciCall_PositionFromLine(iLineStart);
			if (!bReducedSelection) {
				iAnchorPos = SciCall_GetLineEndPosition(iLineEnd);
			} else {
				iAnchorPos = SciCall_PositionFromLine(iLineEnd + 1);
			}
		} else {
			iAnchorPos = SciCall_PositionFromLine(iLineStart);
			if (!bReducedSelection) {
				iCurPos = SciCall_GetLineEndPosition(iLineEnd);
			} else {
				iCurPos = SciCall_PositionFromLine(iLineEnd + 1);
			}
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
			SciCall_SetTargetRange(iPos, SciCall_PositionAfter(iPos));
			SciCall_ReplaceTarget(0, "");
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
			SciCall_SetTargetRange(SciCall_PositionBefore(iEndPos), iEndPos);
			SciCall_ReplaceTarget(0, "");
		}
	}
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditStripTrailingBlanks()
//
void EditStripTrailingBlanks(HWND hwnd, BOOL bIgnoreSelection) {
	// Check if there is any selection... simply use a regular expression replace!
	if (!bIgnoreSelection && !SciCall_IsSelectionEmpty()) {
		if (!SciCall_IsRectangleSelection()) {
#if NP2_USE_DESIGNATED_INITIALIZER
			EDITFINDREPLACE efrTrim = {
				.szFind = "[ \t]+$",
				.hwnd = hwnd,
				.fuFlags = SCFIND_REGEXP,
			};
#else
			EDITFINDREPLACE efrTrim = { "[ \t]+$", "", "", "", hwnd, SCFIND_REGEXP };
#endif
			if (EditReplaceAllInSelection(hwnd, &efrTrim, FALSE)) {
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
		Sci_Position i = lineEnd - 1;
		int ch = SciCall_GetCharAt(i);
		while ((i >= lineStart) && (ch == ' ' || ch == '\t')) {
			i--;
			ch = SciCall_GetCharAt(i);
		}
		if (i < (lineEnd - 1)) {
			SciCall_SetTargetRange(i + 1, lineEnd);
			SciCall_ReplaceTarget(0, "");
		}
	}
	SciCall_EndUndoAction();
}

//=============================================================================
//
// EditStripLeadingBlanks()
//
void EditStripLeadingBlanks(HWND hwnd, BOOL bIgnoreSelection) {
	// Check if there is any selection... simply use a regular expression replace!
	if (!bIgnoreSelection && !SciCall_IsSelectionEmpty()) {
		if (!SciCall_IsRectangleSelection()) {
#if NP2_USE_DESIGNATED_INITIALIZER
			EDITFINDREPLACE efrTrim = {
				.szFind = "^[ \t]+",
				.hwnd = hwnd,
				.fuFlags = SCFIND_REGEXP,
			};
#else
			EDITFINDREPLACE efrTrim = { "^[ \t]+", "", "", "", hwnd, SCFIND_REGEXP };
#endif
			if (EditReplaceAllInSelection(hwnd, &efrTrim, FALSE)) {
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
		Sci_Position i = lineStart;
		int ch = SciCall_GetCharAt(i);
		while ((i <= lineEnd - 1) && (ch == ' ' || ch == '\t')) {
			i++;
			ch = SciCall_GetCharAt(i);
		}
		if (i > lineStart) {
			SciCall_SetTargetRange(lineStart, i);
			SciCall_ReplaceTarget(0, "");
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
	BOOL bIsLineStart;
	BOOL bIsLineEnd;

	if (iSelStart != iSelEnd) {
		const Sci_Line iLineStart = SciCall_LineFromPosition(iSelStart);
		const Sci_Line iLineEnd = SciCall_LineFromPosition(iSelEnd);
		const Sci_Position cch = SciCall_GetSelTextLength();
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
		bIsLineStart = TRUE;
		bIsLineEnd = TRUE;
	}

	BOOL bModified = FALSE;
	char *ci;
	char *co = pszOut;
	for (ci = pszIn; *ci; ci++) {
		if (*ci == ' ' || *ci == '\t') {
			if (*ci == '\t') {
				bModified = TRUE;
			}
			while (*(ci + 1) == ' ' || *(ci + 1) == '\t') {
				ci++;
				bModified = TRUE;
			}
			if (!bIsLineStart && (*(ci + 1) != '\n' && *(ci + 1) != '\r')) {
				*co++ = ' ';
			} else {
				bModified = TRUE;
			}
		} else {
			if (*ci == '\n' || *ci == '\r') {
				bIsLineStart = TRUE;
			} else {
				bIsLineStart = FALSE;
			}
			*co++ = *ci;
		}
	}
	if (bIsLineEnd && co > pszOut && *(co - 1) == ' ') {
		*--co = 0;
		bModified = TRUE;
	}

	if (bModified) {
		if (iSelStart != iSelEnd) {
			SciCall_TargetFromSelection();
		} else {
			SciCall_TargetWholeDocument();
		}
		SciCall_BeginUndoAction();
		SciCall_ReplaceTarget(-1, pszOut);
		if (iCurPos > iAnchorPos) {
			iCurPos = SciCall_GetTargetEnd();
			iAnchorPos = SciCall_GetTargetStart();
		} else if (iCurPos < iAnchorPos) {
			iCurPos = SciCall_GetTargetStart();
			iAnchorPos = SciCall_GetTargetEnd();
		}
		SciCall_SetSel(iAnchorPos, iCurPos);
		SciCall_EndUndoAction();
	}

	NP2HeapFree(pszIn);
	NP2HeapFree(pszOut);
}

//=============================================================================
//
// EditRemoveBlankLines()
//
void EditRemoveBlankLines(BOOL bMerge) {
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

			SciCall_SetTargetRange(iTargetStart, iTargetEnd);
			SciCall_ReplaceTarget(0, "");

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

	struct Sci_TextRange tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRange(&tr);

	const UINT cpEdit = SciCall_GetCodePage();
	const int cchTextW = MultiByteToWideChar(cpEdit, 0, pszText, (int)iSelCount, pszTextW, (int)(NP2HeapSize(pszTextW) / sizeof(WCHAR)));
	NP2HeapFree(pszText);

	LPWSTR pszConvW = (LPWSTR)NP2HeapAlloc(cchTextW * sizeof(WCHAR) * 3 + 2);

	WCHAR wszEOL[] = L"\r\n";
	int cchEOL = 2;
	const int iEOLMode = SciCall_GetEOLMode();
	if (iEOLMode == SC_EOL_CR) {
		cchEOL = 1;
	} else if (iEOLMode == SC_EOL_LF) {
		cchEOL = 1;
		wszEOL[0] = L'\n';
	}

#define ISDELIMITER(wc) StrChr(L",;.:-+%&\xA6|/*?!\"\'~\xB4#=", wc)
#define ISWHITE(wc)		IsASpaceOrTab(wc)
#define ISWORDEND(wc)	(/*ISDELIMITER(wc) ||*/ IsASpace(wc))

	int cchConvW = 0;
	int iLineLength = 0;
	BOOL bModified = FALSE;
	for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
		const WCHAR w = pszTextW[iTextW];

		//if (ISDELIMITER(w)) {
		//	int iNextWordLen = 0;
		//	WCHAR w2 = pszTextW[iTextW + 1];

		//	if (iLineLength + iNextWordLen + 1 > nColumn) {
		//		pszConvW[cchConvW++] = wszEOL[0];
		//		if (cchEOL > 1)
		//			pszConvW[cchConvW++] = wszEOL[1];
		//		iLineLength = 0;
		//		bModified = TRUE;
		//	}

		//	while (w2 != L'\0' && !ISWORDEND(w2)) {
		//		iNextWordLen++;
		//		w2 = pszTextW[iTextW + iNextWordLen + 1];
		//	}

		//	if (ISDELIMITER(w2) && iNextWordLen > 0) // delimiters go with the word
		//		iNextWordLen++;

		//	pszConvW[cchConvW++] = w;
		//	iLineLength++;

		//	if (iNextWordLen > 0) {
		//		if (iLineLength + iNextWordLen + 1 > nColumn) {
		//			pszConvW[cchConvW++] = wszEOL[0];
		//			if (cchEOL > 1)
		//				pszConvW[cchConvW++] = wszEOL[1];
		//			iLineLength = 0;
		//			bModified = TRUE;
		//		}
		//	}
		//}

		if (ISWHITE(w)) {
			while (IsASpaceOrTab(pszTextW[iTextW + 1])) {
				iTextW++;
				bModified = TRUE;
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
					pszConvW[cchConvW++] = wszEOL[0];
					if (cchEOL > 1) {
						pszConvW[cchConvW++] = wszEOL[1];
					}
					iLineLength = 0;
					bModified = TRUE;
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

	struct Sci_TextRange tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRange(&tr);

	char szEOL[] = "\r\n";
	int cchEOL = 2;
	const int iEOLMode = SciCall_GetEOLMode();
	if (iEOLMode == SC_EOL_CR) {
		cchEOL = 1;
	} else if (iEOLMode == SC_EOL_LF) {
		cchEOL = 1;
		szEOL[0] = '\n';
	}

	Sci_Position cchJoin = 0;
	BOOL bModified = FALSE;
	for (Sci_Position i = 0; i < iSelCount; i++) {
		if (IsEOLChar(pszText[i])) {
			if (pszText[i] == '\r' && pszText[i + 1] == '\n') {
				i++;
			}
			if (!IsEOLChar(pszText[i + 1]) && pszText[i + 1] != '\0') {
				pszJoin[cchJoin++] = ' ';
				bModified = TRUE;
			} else {
				while (IsEOLChar(pszText[i + 1])) {
					i++;
					bModified = TRUE;
				}
				if (pszText[i + 1] != 0) {
					pszJoin[cchJoin++] = szEOL[0];
					if (cchEOL > 1) {
						pszJoin[cchJoin++] = szEOL[1];
					}
					if (cchJoin > cchEOL) {
						pszJoin[cchJoin++] = szEOL[0];
						if (cchEOL > 1) {
							pszJoin[cchJoin++] = szEOL[1];
						}
					}
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
	WCHAR *pwszLine;
	WCHAR *pwszSortEntry;
} SORTLINE;

typedef int (__stdcall *FNSTRCMP)(LPCWSTR, LPCWSTR);
typedef int (__cdecl *QSortCmp)(const void *, const void *);

static int __cdecl CmpStd(const void *p1, const void *p2) {
	const SORTLINE *s1 = (const SORTLINE *)p1;
	const SORTLINE *s2 = (const SORTLINE *)p2;
	const int cmp = StrCmpW(s1->pwszSortEntry, s2->pwszSortEntry);
	return cmp ? cmp : StrCmpW(s1->pwszLine, s2->pwszLine);
}

static int __cdecl CmpIStd(const void *p1, const void *p2) {
	const SORTLINE *s1 = (const SORTLINE *)p1;
	const SORTLINE *s2 = (const SORTLINE *)p2;
	const int cmp = StrCmpIW(s1->pwszSortEntry, s2->pwszSortEntry);
	return cmp ? cmp : StrCmpIW(s1->pwszLine, s2->pwszLine);
}

static int __cdecl CmpStdRev(const void *p1, const void *p2) {
	return CmpStd(p2, p1);
}

static int __cdecl CmpIStdRev(const void *p1, const void *p2) {
	return CmpIStd(p2, p1);
}

static int __cdecl CmpLogical(const void *p1, const void *p2) {
	const SORTLINE *s1 = (const SORTLINE *)p1;
	const SORTLINE *s2 = (const SORTLINE *)p2;
	int cmp = StrCmpLogicalW(s1->pwszSortEntry, s2->pwszSortEntry);
	if (cmp == 0) {
		cmp = StrCmpLogicalW(s1->pwszLine, s2->pwszLine);
	}
	return cmp ? cmp : CmpStd(p1, p2);
}

static int __cdecl CmpILogical(const void *p1, const void *p2) {
	const SORTLINE *s1 = (const SORTLINE *)p1;
	const SORTLINE *s2 = (const SORTLINE *)p2;
	int cmp = StrCmpLogicalW(s1->pwszSortEntry, s2->pwszSortEntry);
	if (cmp == 0) {
		cmp = StrCmpLogicalW(s1->pwszLine, s2->pwszLine);
	}
	return cmp ? cmp : CmpIStd(p1, p2);
}

static int __cdecl CmpLogicalRev(const void *p1, const void *p2) {
	return CmpLogical(p2, p1);
}

static int __cdecl CmpILogicalRev(const void *p1, const void *p2) {
	return CmpILogical(p2, p1);
}

void EditSortLines(int iSortFlags) {
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

	const BOOL bIsRectangular = SciCall_IsRectangleSelection();
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

	char mszEOL[] = "\r\n";
	const UINT cpEdit = SciCall_GetCodePage();
	const int iEOLMode = SciCall_GetEOLMode();
	if (iEOLMode == SC_EOL_CR) {
		mszEOL[1] = 0;
	} else if (iEOLMode == SC_EOL_LF) {
		mszEOL[0] = '\n';
		mszEOL[1] = 0;
	}

	SciCall_BeginUndoAction();
	if (bIsRectangular) {
		EditPadWithSpaces(!(iSortFlags & SORT_SHUFFLE), TRUE);
	}

	SORTLINE *pLines = (SORTLINE *)NP2HeapAlloc(sizeof(SORTLINE) * iLineCount);
	Sci_Position cchTotal = 0;
	Sci_Position ichlMax = 3;
	for (Sci_Line i = 0, iLine = iLineStart; iLine <= iLineEnd; i++, iLine++) {
		const Sci_Position cchm = SciCall_GetLineLength(iLine);
		char *pmsz = (char *)NP2HeapAlloc(cchm + 1);
		SciCall_GetLine(iLine, pmsz);

		// remove EOL
		char *p = pmsz + cchm - 1;
		if (*p == '\n' || *p == '\r') {
			*p-- = '\0';
		}
		if (*p == '\r') {
			*p-- = '\0';
		}

		cchTotal += cchm;
		ichlMax = max_pos(ichlMax, cchm);

		const int cchw = MultiByteToWideChar(cpEdit, 0, pmsz, -1, NULL, 0) - 1;
		if (cchw > 0) {
			LPWSTR pwszLine = (LPWSTR)LocalAlloc(LPTR, sizeof(WCHAR) * (cchw + 1));
			MultiByteToWideChar(cpEdit, 0, pmsz, -1, pwszLine, (int)(LocalSize(pwszLine) / sizeof(WCHAR)));
			pLines[i].pwszLine = pwszLine;

			if (iSortFlags & SORT_COLUMN) {
				const int tabWidth = fvCurFile.iTabWidth;
				Sci_Position col = 0;
				Sci_Position tabs = tabWidth;
				while (*pwszLine) {
					if (*pwszLine == L'\t') {
						if (col + tabs <= iSortColumn) {
							col += tabs;
							tabs = tabWidth;
							pwszLine = CharNext(pwszLine);
						} else {
							break;
						}
					} else if (col < iSortColumn) {
						col++;
						if (--tabs == 0) {
							tabs = tabWidth;
						}
						pwszLine = CharNext(pwszLine);
					} else {
						break;
					}
				}
			}
			pLines[i].pwszSortEntry = pwszLine;
		} else {
			pLines[i].pwszLine = StrDup(L"");
			pLines[i].pwszSortEntry = pLines[i].pwszLine;
		}
		NP2HeapFree(pmsz);
	}

	if (iSortFlags & SORT_DESCENDING) {
		QSortCmp cmpFunc = (iSortFlags & SORT_LOGICAL)
			? ((iSortFlags & SORT_NOCASE) ? CmpILogicalRev : CmpLogicalRev)
			: ((iSortFlags & SORT_NOCASE) ? CmpIStdRev : CmpStdRev);
		qsort(pLines, iLineCount, sizeof(SORTLINE), cmpFunc);
	} else if (iSortFlags & SORT_SHUFFLE) {
		srand(GetTickCount());
		for (Sci_Line i = iLineCount - 1; i > 0; i--) {
			const Sci_Line j = rand() % i;
			const SORTLINE sLine = pLines[i];
			pLines[i] = pLines[j];
			pLines[j] = sLine;
		}
	} else {
		QSortCmp cmpFunc = (iSortFlags & SORT_LOGICAL)
			? ((iSortFlags & SORT_NOCASE) ? CmpILogical : CmpLogical)
			: ((iSortFlags & SORT_NOCASE) ? CmpIStd : CmpStd);
		qsort(pLines, iLineCount, sizeof(SORTLINE), cmpFunc);
	}

	char *pmszResult = (char *)NP2HeapAlloc(cchTotal + 2 * iLineCount + 1);
	char *pmszBuf = (char *)NP2HeapAlloc(ichlMax + 1);
	const int cbPmszBuf = (int)NP2HeapSize(pmszBuf);
	const int cbPmszResult = (int)NP2HeapSize(pmszResult);
	FNSTRCMP pfnStrCmp = (iSortFlags & SORT_NOCASE) ? StrCmpIW : StrCmpW;

	BOOL bLastDup = FALSE;
	for (Sci_Line i = 0; i < iLineCount; i++) {
		if (pLines[i].pwszLine && ((iSortFlags & SORT_SHUFFLE) || StrNotEmpty(pLines[i].pwszLine))) {
			BOOL bDropLine = FALSE;
			if (!(iSortFlags & SORT_SHUFFLE)) {
				if (iSortFlags & (SORT_MERGEDUP | SORT_UNIQDUP | SORT_UNIQUNIQ)) {
					if (i < iLineCount - 1) {
						if (pfnStrCmp(pLines[i].pwszLine, pLines[i + 1].pwszLine) == 0) {
							bLastDup = TRUE;
							bDropLine = iSortFlags & (SORT_MERGEDUP | SORT_UNIQDUP);
						} else {
							bDropLine = (!bLastDup && (iSortFlags & SORT_UNIQUNIQ)) || (bLastDup && (iSortFlags & SORT_UNIQDUP));
							bLastDup = FALSE;
						}
					} else {
						bDropLine = (!bLastDup && (iSortFlags & SORT_UNIQUNIQ)) || (bLastDup && (iSortFlags & SORT_UNIQDUP));
						bLastDup = FALSE;
					}
				}
			}

			if (!bDropLine) {
				WideCharToMultiByte(cpEdit, 0, pLines[i].pwszLine, -1, pmszBuf, cbPmszBuf, NULL, NULL);
				strncat(pmszResult, pmszBuf, cbPmszResult);
				strncat(pmszResult, mszEOL, cbPmszResult);
			}
		}
	}

	NP2HeapFree(pmszBuf);
	for (Sci_Line i = 0; i < iLineCount; i++) {
		if (pLines[i].pwszLine) {
			LocalFree(pLines[i].pwszLine);
		}
	}
	NP2HeapFree(pLines);

	const Sci_Position length = strlen(pmszResult);
	if (!bIsRectangular) {
		if (iAnchorPos > iCurPos) {
			iCurPos = iSelStart;
			iAnchorPos = iSelStart + length;
		} else {
			iAnchorPos = iSelStart;
			iCurPos = iSelStart + length;
		}
	}

	SciCall_SetTargetRange(SciCall_PositionFromLine(iLineStart), SciCall_PositionFromLine(iLineEnd + 1));
	SciCall_ReplaceTarget(length, pmszResult);
	SciCall_EndUndoAction();

	NP2HeapFree(pmszResult);

	if (!bIsRectangular) {
		SciCall_SetSel(iAnchorPos, iCurPos);
	} else {
		const Sci_Position iTargetStart = SciCall_GetTargetStart();
		Sci_Position iTargetEnd = SciCall_GetTargetEnd();
		SciCall_ClearSelections();
		if (iTargetStart != iTargetEnd) {
			iTargetEnd -= strlen(mszEOL);
			if (iRcAnchorLine > iRcCurLine) {
				iCurPos = SciCall_FindColumn(SciCall_LineFromPosition(iTargetStart), iRcCurCol);
				iAnchorPos = SciCall_FindColumn(SciCall_LineFromPosition(iTargetEnd), iRcAnchorCol);
			} else {
				iCurPos = SciCall_FindColumn(SciCall_LineFromPosition(iTargetEnd), iRcCurCol);
				iAnchorPos = SciCall_FindColumn(SciCall_LineFromPosition(iTargetStart), iRcAnchorCol);
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
	const Sci_Line iMaxLine = SciCall_GetLineCount();

	// Jumpt to end with line set to -1
	if (iNewLine < 0 || iNewLine >= iMaxLine) {
		SciCall_DocumentEnd();
		return;
	}

	const Sci_Position iLineEndPos = SciCall_GetLineEndPosition(iNewLine - 1);
	iNewCol = min_pos(iNewCol, iLineEndPos);
	const Sci_Position iNewPos = SciCall_FindColumn(iNewLine - 1, iNewCol - 1);
	// SciCall_GotoPos(pos) is equivalent to SciCall_SetSel(-1, pos)
	EditSelectEx(-1, iNewPos);
	SciCall_ChooseCaretX();
}

//=============================================================================
//
// EditSelectEx()
//
void EditSelectEx(Sci_Position iAnchorPos, Sci_Position iCurrentPos) {
	const Sci_Line iNewLine = SciCall_LineFromPosition(iCurrentPos);
	const Sci_Line iAnchorLine = SciCall_LineFromPosition(iAnchorPos);

	if (iAnchorLine == iNewLine) {
		// TODO: center current line on screen when it's not visible
		SciCall_EnsureVisible(iAnchorLine);
	} else {
		// Ensure that the first and last lines of a selection are always unfolded
		// This needs to be done *before* the SciCall_SetSel() message
		SciCall_EnsureVisible(iAnchorLine);
		SciCall_EnsureVisible(iNewLine);
	}

	SciCall_SetXCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 50);
	SciCall_SetYCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 5);
	SciCall_SetSel(iAnchorPos, iCurrentPos);
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
		lstrcpy(lpszExcerpt, L"");
		return;
	}

	WCHAR tch[256] = L"";
	DWORD cch = 0;
	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = min_pos(min_pos(SciCall_GetSelectionEnd(), iSelStart + COUNTOF(tch)), SciCall_GetLength());
	const Sci_Position iSelCount = iSelEnd - iSelStart;

	char *pszText = (char *)NP2HeapAlloc(iSelCount + 2);
	LPWSTR pszTextW = (LPWSTR)NP2HeapAlloc((iSelCount + 1) * sizeof(WCHAR));

	struct Sci_TextRange tr = { { iSelStart, iSelEnd }, pszText };
	SciCall_GetTextRange(&tr);
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
		Sci_Position iWordStart = SciCall_WordStartPosition(iPos, TRUE);
		Sci_Position iWordEnd = SciCall_WordEndPosition(iPos, TRUE);

		if (iWordStart == iWordEnd) {// we are in whitespace salad...
			iWordStart = SciCall_WordEndPosition(iPos, FALSE);
			iWordEnd = SciCall_WordEndPosition(iWordStart, TRUE);
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

void EditSelectLines(BOOL currentBlock, BOOL lineSelection) {
	if (lineSelection && !currentBlock) {
		SciCall_SetSelectionMode(SC_SEL_LINES);
		return;
	}

	// see Editor::LineSelectionRange()
	Sci_Position iCurrentPos = SciCall_GetCurrentPos();
	Sci_Position iAnchorPos = SciCall_GetAnchor();
	BOOL backward = (iCurrentPos < iAnchorPos);

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
		iCurrentPos = SciCall_PositionFromLine(iLineCurPos);
		iAnchorPos = SciCall_PositionFromLine(iLineAnchorPos + offset);
	} else {
		iAnchorPos = SciCall_PositionFromLine(iLineAnchorPos);
		iCurrentPos = SciCall_PositionFromLine(iLineCurPos + offset);
	}

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
		BOOL done = FALSE;
		LPWSTR lpsz = EditGetClipboardTextW();
		if (StrNotEmpty(lpsz)) {
			const int len = lstrlen(lpsz);
			LPWSTR lpszEsc = (LPWSTR)NP2HeapAlloc((2*len + 1)*sizeof(WCHAR));
			if (lpszEsc != NULL) {
				AddBackslashW(lpszEsc, lpsz);
				SendMessage(hwnd, EM_REPLACESEL, TRUE, (LPARAM)(lpszEsc));
				NP2HeapFree(lpszEsc);
				done = TRUE;
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

extern BOOL bFindReplaceTransparentMode;
extern int iFindReplaceOpacityLevel;
extern BOOL bFindReplaceUseMonospacedFont;
extern BOOL bFindReplaceFindAllBookmark;

static void FindReplaceSetFont(HWND hwnd, BOOL monospaced, HFONT *hFontFindReplaceEdit) {
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

static BOOL CopySelectionAsFindText(HWND hwnd, LPEDITFINDREPLACE lpefr, BOOL bFirstTime) {
	const Sci_Position cchSelection = SciCall_GetSelTextLength() - 1;
	char *lpszSelection = NULL;

	if (cchSelection != 0 && cchSelection <= NP2_FIND_REPLACE_LIMIT) {
		lpszSelection = (char *)NP2HeapAlloc(cchSelection + 1);
		SciCall_GetSelText(lpszSelection);
	}

	// only for manually selected text
	const BOOL hasFindText = StrNotEmptyA(lpszSelection);

	// First time you bring up find/replace dialog,
	// copy content from clipboard to find box when nothing is selected in the editor.
	if (!hasFindText && bFirstTime) {
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
		lpefr->bTransformBS = AddBackslashA(lpszEscSel, lpszSelection);

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
		for (int i = 0; i < MRU_GetCount(mruFind); i++) {
			MRU_Enum(mruFind, i, tch, COUNTOF(tch));
			ComboBox_AddString(hwndFind, tch);
		}

		LPEDITFINDREPLACE lpefr = (LPEDITFINDREPLACE)lParam;
		// don't copy selection after toggle find & replace on this window.
		BOOL hasFindText = FALSE;
		if (bSwitchedFindReplace != 3) {
			hasFindText = CopySelectionAsFindText(hwnd, lpefr, TRUE);
		}
		if (!GetWindowTextLength(hwndFind)) {
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8);
		}

		ComboBox_LimitText(hwndFind, NP2_FIND_REPLACE_LIMIT);
		ComboBox_SetExtendedUI(hwndFind, TRUE);

		HWND hwndRepl = GetDlgItem(hwnd, IDC_REPLACETEXT);
		if (hwndRepl) {
			AddBackslashComboBoxSetup(hwnd, IDC_REPLACETEXT);
			for (int i = 0; i < MRU_GetCount(mruReplace); i++) {
				MRU_Enum(mruReplace, i, tch, COUNTOF(tch));
				ComboBox_AddString(hwndRepl, tch);
			}

			ComboBox_LimitText(hwndRepl, NP2_FIND_REPLACE_LIMIT);
			ComboBox_SetExtendedUI(hwndRepl, TRUE);
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_REPLACETEXT, lpefr->szReplaceUTF8);
		}

		// focus on replace box when selected text is not empty.
		PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)((hasFindText && hwndRepl)? hwndRepl : hwndFind), 1);

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
			CopyMemory(lpefr, &efrSave, sizeof(EDITFINDREPLACE));
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

	case WM_COPYDATA: {
		HWND hwndFind = GetDlgItem(hwnd, IDC_FINDTEXT);
		HWND hwndRepl = GetDlgItem(hwnd, IDC_REPLACETEXT);
		LPEDITFINDREPLACE lpefr = (LPEDITFINDREPLACE)GetWindowLongPtr(hwnd, DWLP_USER);

		const BOOL hasFindText = CopySelectionAsFindText(hwnd, lpefr, FALSE);
		if (!GetWindowTextLength(hwndFind)) {
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8);
		}
		if (lpefr->bTransformBS) {
			CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_CHECKED);
		}
		// focus on replace box when selected text is not empty.
		PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)((hasFindText && hwndRepl)? hwndRepl : hwndFind), 1);
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

		const BOOL isReplace = GetDlgItem(hwnd, IDC_REPLACETEXT) != NULL;
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
			const BOOL bIsFindDlg = (hwndRepl == NULL);
			// Get current code page for Unicode conversion
			const UINT cpEdit = SciCall_GetCodePage();
			cpLastFind = cpEdit;

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
				lpefr->fuFlags |= SCFIND_REGEXP | SCFIND_POSIX;
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
					MRU_AddMultiline(mruFind, tch);
				}
			}
			if (StrNotEmptyA(lpefr->szReplace)) {
				if (GetDlgItemTextA2W(CP_UTF8, hwnd, IDC_REPLACETEXT, lpefr->szReplaceUTF8, COUNTOF(lpefr->szReplaceUTF8))) {
					ComboBox_GetText(hwndRepl, tch, COUNTOF(tch));
					MRU_AddMultiline(mruReplace, tch);
				}
			} else {
				strcpy(lpefr->szReplaceUTF8, "");
			}

			BOOL bCloseDlg;
			if (bIsFindDlg) {
				bCloseDlg = lpefr->bFindClose;
			} else {
				if (LOWORD(wParam) == IDOK) {
					bCloseDlg = FALSE;
				} else {
					bCloseDlg = lpefr->bReplaceClose;
				}
			}

			// Reload MRUs
			ComboBox_ResetContent(hwndFind);
			ComboBox_ResetContent(hwndRepl);

			for (int i = 0; i < MRU_GetCount(mruFind); i++) {
				MRU_Enum(mruFind, i, tch, COUNTOF(tch));
				ComboBox_AddString(hwndFind, tch);
			}

			for (int i = 0; i < MRU_GetCount(mruReplace); i++) {
				MRU_Enum(mruReplace, i, tch, COUNTOF(tch));
				ComboBox_AddString(hwndRepl, tch);
			}

			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_FINDTEXT, lpefr->szFindUTF8);
			SetDlgItemTextA2W(CP_UTF8, hwnd, IDC_REPLACETEXT, lpefr->szReplaceUTF8);

			SendMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetFocus()), 1);

			if (bCloseDlg) {
				DestroyWindow(hwnd);
				hDlgFindReplace = NULL;
			}

			switch (LOWORD(wParam)) {
			case IDOK: // find next
			case IDACC_SELTONEXT:
				if (!bIsFindDlg) {
					bReplaceInitialized = TRUE;
				}
				EditFindNext(lpefr, LOWORD(wParam) == IDACC_SELTONEXT || KeyboardIsKeyDown(VK_SHIFT));
				break;

			case IDC_FINDPREV: // find previous
			case IDACC_SELTOPREV:
				if (!bIsFindDlg) {
					bReplaceInitialized = TRUE;
				}
				EditFindPrev(lpefr, LOWORD(wParam) == IDACC_SELTOPREV || KeyboardIsKeyDown(VK_SHIFT));
				break;

			case IDC_REPLACE:
				bReplaceInitialized = TRUE;
				EditReplace(lpefr->hwnd, lpefr);
				break;

			case IDC_FINDALL:
				EditFindAll(lpefr, FALSE);
				break;

			case IDC_REPLACEALL:
				if (bIsFindDlg) {
					EditFindAll(lpefr, TRUE);
				} else {
					bReplaceInitialized = TRUE;
					EditReplaceAll(lpefr->hwnd, lpefr, TRUE);
				}
				break;

			case IDC_REPLACEINSEL:
				bReplaceInitialized = TRUE;
				EditReplaceAllInSelection(lpefr->hwnd, lpefr, TRUE);
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
			PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, IDC_FINDTEXT)), 1);
		}
		break;

		case IDC_TOGGLEFINDREPLACE: {
			bSwitchedFindReplace |= 2;
			LPEDITFINDREPLACE lpefr = (LPEDITFINDREPLACE)GetWindowLongPtr(hwnd, DWLP_USER);
			GetDlgPos(hwnd, &xFindReplaceDlgSave, &yFindReplaceDlgSave);
			CopyMemory(&efrSave, lpefr, sizeof(EDITFINDREPLACE));
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

			case IDC_CLEAR_FIND: {
				HWND hwndFind = GetDlgItem(hwnd, IDC_FINDTEXT);
				ComboBox_GetText(hwndFind, tch, COUNTOF(tch));
				ComboBox_ResetContent(hwndFind);
				MRU_Empty(mruFind);
				MRU_Save(mruFind);
				ComboBox_SetText(hwndFind, tch);
			}
			break;

			case IDC_CLEAR_REPLACE: {
				HWND hwndRepl = GetDlgItem(hwnd, IDC_REPLACETEXT);
				ComboBox_GetText(hwndRepl, tch, COUNTOF(tch));
				ComboBox_ResetContent(hwndRepl);
				MRU_Empty(mruReplace);
				MRU_Save(mruReplace);
				ComboBox_SetText(hwndRepl, tch);
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
HWND EditFindReplaceDlg(HWND hwnd, LPEDITFINDREPLACE lpefr, BOOL bReplace) {
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
		searchFlags |= ((sensitive - 1) & SCFIND_MATCHCASE);
	}
	return searchFlags;
}

int EditPrepareReplace(HWND hwnd, char *szFind2, char **pszReplace2, BOOL *bReplaceRE, LPCEDITFINDREPLACE lpefr) {
	const int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return searchFlags;
	}

	*bReplaceRE = (searchFlags & SCFIND_REGEXP);
	if (StrEqualA(lpefr->szReplace, "^c")) {
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
void EditFindNext(LPCEDITFINDREPLACE lpefr, BOOL fExtendSelection) {
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	const int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return;
	}

	const Sci_Position iSelPos = SciCall_GetCurrentPos();
	const Sci_Position iSelAnchor = SciCall_GetAnchor();

	struct Sci_TextToFind ttf = { { SciCall_GetSelectionEnd(), SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iPos = SciCall_FindText(searchFlags, &ttf);
	BOOL bSuppressNotFound = FALSE;

	if (iPos == -1 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap && !fExtendSelection) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap1", IDS_FIND_WRAPFW)) {
			ttf.chrg.cpMin = 0;
			iPos = SciCall_FindText(searchFlags, &ttf);
		} else {
			bSuppressNotFound = TRUE;
		}
	}

	if (iPos == -1) {
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
void EditFindPrev(LPCEDITFINDREPLACE lpefr, BOOL fExtendSelection) {
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	const int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return;
	}

	const Sci_Position iSelPos = SciCall_GetCurrentPos();
	const Sci_Position iSelAnchor = SciCall_GetAnchor();

	struct Sci_TextToFind ttf = { { SciCall_GetSelectionStart(), 0 }, szFind2, { 0, 0 } };
	Sci_Position iPos = SciCall_FindText(searchFlags, &ttf);
	const Sci_Position iLength = SciCall_GetLength();
	BOOL bSuppressNotFound = FALSE;

	if (iPos == -1 && ttf.chrg.cpMin < iLength && !lpefr->bNoFindWrap && !fExtendSelection) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap2", IDS_FIND_WRAPRE)) {
			ttf.chrg.cpMin = iLength;
			iPos = SciCall_FindText(searchFlags, &ttf);
		} else {
			bSuppressNotFound = TRUE;
		}
	}

	if (iPos == -1) {
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
BOOL EditReplace(HWND hwnd, LPCEDITFINDREPLACE lpefr) {
	BOOL bReplaceRE;
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	char *pszReplace2;
	const int searchFlags = EditPrepareReplace(hwnd, szFind2, &pszReplace2, &bReplaceRE, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return FALSE;
	}

	const Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();

	struct Sci_TextToFind ttf = { { iSelStart, SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iPos = SciCall_FindText(searchFlags, &ttf);
	BOOL bSuppressNotFound = FALSE;

	if (iPos == -1 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap1", IDS_FIND_WRAPFW)) {
			ttf.chrg.cpMin = 0;
			iPos = SciCall_FindText(searchFlags, &ttf);
		} else {
			bSuppressNotFound = TRUE;
		}
	}

	if (iPos == -1) {
		// not found
		LocalFree(pszReplace2);
		if (!bSuppressNotFound) {
			InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
		}
		return FALSE;
	}

	if (iSelStart != ttf.chrgText.cpMin || iSelEnd != ttf.chrgText.cpMax) {
		LocalFree(pszReplace2);
		EditSelectEx(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
		return FALSE;
	}

	SciCall_SetTargetRange(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
	SciCall_ReplaceTargetEx(bReplaceRE, -1, pszReplace2);

	ttf.chrg.cpMin = SciCall_GetTargetEnd();
	ttf.chrg.cpMax = SciCall_GetLength();

	iPos = SciCall_FindText(searchFlags, &ttf);
	bSuppressNotFound = FALSE;

	if (iPos == -1 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap) {
		if (IDOK == InfoBoxInfo(MB_OKCANCEL, L"MsgFindWrap1", IDS_FIND_WRAPFW)) {
			ttf.chrg.cpMin = 0;
			iPos = SciCall_FindText(searchFlags, &ttf);
		} else {
			bSuppressNotFound = TRUE;
		}
	}

	if (iPos != -1) {
		EditSelectEx(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
	} else {
		iPos = SciCall_GetTargetEnd();
		EditSelectEx(iPos, iPos); //?
		if (!bSuppressNotFound) {
			InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
		}
	}

	LocalFree(pszReplace2);
	return TRUE;
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
		if ((findFlag | editMarkAllStatus.findFlag) & NP2_MarkAllBookmark) {
			SciCall_MarkerDeleteAll(MarkerNumber_Bookmark);
		}
	}
	if (editMarkAllStatus.pszText) {
		NP2HeapFree(editMarkAllStatus.pszText);
	}

	editMarkAllStatus.pending = FALSE;
	editMarkAllStatus.ignoreSelectionUpdate = FALSE;
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

BOOL EditMarkAll_Start(BOOL bChanged, int findFlag, Sci_Position iSelCount, LPSTR pszText) {
	if (!bChanged && (findFlag == editMarkAllStatus.findFlag
		&& iSelCount == editMarkAllStatus.iSelCount
		// _stricmp() is not safe for DBCS string.
		&& memcmp(pszText, editMarkAllStatus.pszText, iSelCount) == 0)) {
		NP2HeapFree(pszText);
		return FALSE;
	}

	EditMarkAll_ClearEx(findFlag, iSelCount, pszText);
	if ((findFlag & SCFIND_REGEXP) && iSelCount == 1) {
		const char ch = *pszText;
		if (ch == '^' || ch == '$') {
			const Sci_Line lineCount = SciCall_GetLineCount();
			editMarkAllStatus.matchCount = lineCount - (ch == '^');
			UpdateStatusbar();
			return TRUE;
		}
	}

	//EditMarkAll_Runs = 0;
	if (findFlag & NP2_MarkAllBookmark) {
		Style_SetBookmark();
	}
	return EditMarkAll_Continue(&editMarkAllStatus, idleTaskTimer);
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

BOOL EditMarkAll_Continue(EditMarkAllStatus *status, HANDLE timer) {
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
	struct Sci_TextToFind ttf = { { cpMin, iMaxLength }, status->pszText, { 0, 0 } };

	Sci_Position matchCount = status->matchCount;
	UINT index = 0;
	Sci_Position ranges[EditMarkAll_RangeCacheCount*2];
	Sci_Line bookmarkLine = status->bookmarkLine;

	SciCall_SetIndicatorCurrent(IndicatorNumber_MarkOccurrence);
	WaitableTimer_Set(timer, WaitableTimer_IdleTaskTimeSlot);
	while (cpMin < iMaxLength && WaitableTimer_Continue(timer)) {
		ttf.chrg.cpMin = cpMin;
		const Sci_Position iPos = SciCall_FindText(findFlag, &ttf);
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
	const BOOL pending = iStartPos < iLength;
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
	status->ignoreSelectionUpdate = matchCount ? (findFlag & NP2_MarkAllSelectAll) : FALSE;
	status->lastMatchPos = cpMin;
	status->iStartPos = iStartPos;
	status->bookmarkLine = bookmarkLine;
	if (!pending || matchCount != status->matchCount) {
		status->matchCount = matchCount;
		UpdateStatusbar();
		return TRUE;
	}
	return FALSE;
}

BOOL EditMarkAll(BOOL bChanged, BOOL matchCase, BOOL wholeWord, BOOL bookmark) {
	// get current selection
	Sci_Position iSelStart = SciCall_GetSelectionStart();
	const Sci_Position iSelEnd = SciCall_GetSelectionEnd();
	Sci_Position iSelCount = iSelEnd - iSelStart;

	// if nothing selected or multiple lines are selected exit
	if (iSelCount == 0 || SciCall_LineFromPosition(iSelStart) != SciCall_LineFromPosition(iSelEnd)) {
		EditMarkAll_Clear();
		return FALSE;
	}

	// scintilla/src/Editor.h SelectionText.LengthWithTerminator()
	iSelCount = SciCall_GetSelTextLength() - 1;
	char *pszText = (char *)NP2HeapAlloc(iSelCount + 1);
	SciCall_GetSelText(pszText);

	// exit if selection is not a word and Match whole words only is enabled
	if (wholeWord) {
		const UINT cpEdit = SciCall_GetCodePage();
		const BOOL dbcs = !(cpEdit == CP_UTF8 || cpEdit == 0);
		// CharClassify::SetDefaultCharClasses()
		for (iSelStart = 0; iSelStart < iSelCount; ++iSelStart) {
			const unsigned char ch = pszText[iSelStart];
			if (dbcs && IsDBCSLeadByteEx(cpEdit, ch)) {
				++iSelStart;
			} else if (!(ch >= 0x80 || IsDocWordChar(ch))) {
				NP2HeapFree(pszText);
				EditMarkAll_Clear();
				return FALSE;
			}
		}
	}
	if (!matchCase) {
		const BOOL sensitive = IsStringCaseSensitiveA(pszText);
		//printf("%s sensitive=%d\n", __func__, sensitive);
		matchCase = sensitive ^ 1;
	}

	const int findFlag = (matchCase * SCFIND_MATCHCASE)
		| (wholeWord * SCFIND_WHOLEWORD)
		| (bookmark * NP2_MarkAllBookmark);
	return EditMarkAll_Start(bChanged, findFlag, iSelCount, pszText);
}

void EditFindAll(LPCEDITFINDREPLACE lpefr, BOOL selectAll) {
	char *szFind2 = (char *)NP2HeapAlloc(NP2_FIND_REPLACE_LIMIT);
	int searchFlags = EditPrepareFind(szFind2, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		NP2HeapFree(szFind2);
		return;
	}

	searchFlags |= (bFindReplaceFindAllBookmark * NP2_MarkAllBookmark)
		| (selectAll * NP2_MarkAllSelectAll);
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
		editMarkAllStatus.ignoreSelectionUpdate = TRUE;
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
		PosToStrW(iCount, tchNum);
		FormatNumberStr(tchNum);
		InfoBoxInfo(MB_OK, L"MsgReplaceCount", IDS_REPLCOUNT, tchNum);
	} else {
		InfoBoxWarn(MB_OK, L"MsgNotFound", IDS_NOTFOUND);
	}
}

//=============================================================================
//
// EditReplaceAll()
//
BOOL EditReplaceAll(HWND hwnd, LPCEDITFINDREPLACE lpefr, BOOL bShowInfo) {
	BOOL bReplaceRE;
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	char *pszReplace2;
	const int searchFlags = EditPrepareReplace(hwnd, szFind2, &pszReplace2, &bReplaceRE, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return FALSE;
	}

	// Show wait cursor...
	BeginWaitCursor();

	const BOOL bRegexStartOfLine = bReplaceRE && (szFind2[0] == '^');
	struct Sci_TextToFind ttf = { { 0, SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iCount = 0;
	while (SciCall_FindText(searchFlags, &ttf) != -1) {
		if (++iCount == 1) {
			SciCall_BeginUndoAction();
		}

		SciCall_SetTargetRange(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
		const Sci_Position iReplacedLen = SciCall_ReplaceTargetEx(bReplaceRE, -1, pszReplace2);

		ttf.chrg.cpMin = (ttf.chrgText.cpMin + iReplacedLen);
		ttf.chrg.cpMax = SciCall_GetLength();

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

	if (iCount) {
		SciCall_EndUndoAction();
	}

	// Remove wait cursor
	EndWaitCursor();

	if (bShowInfo) {
		ShwowReplaceCount(iCount);
	}

	LocalFree(pszReplace2);
	return TRUE;
}

//=============================================================================
//
// EditReplaceAllInSelection()
//
BOOL EditReplaceAllInSelection(HWND hwnd, LPCEDITFINDREPLACE lpefr, BOOL bShowInfo) {
	if (SciCall_IsRectangleSelection()) {
		NotifyRectangleSelection();
		return FALSE;
	}

	BOOL bReplaceRE;
	char szFind2[NP2_FIND_REPLACE_LIMIT];
	char *pszReplace2;
	const int searchFlags = EditPrepareReplace(hwnd, szFind2, &pszReplace2, &bReplaceRE, lpefr);
	if (searchFlags == NP2_InvalidSearchFlags) {
		return FALSE;
	}

	// Show wait cursor...
	BeginWaitCursor();

	const BOOL bRegexStartOfLine = bReplaceRE && (szFind2[0] == '^');
	struct Sci_TextToFind ttf = { { SciCall_GetSelectionStart(), SciCall_GetLength() }, szFind2, { 0, 0 } };
	Sci_Position iCount = 0;
	BOOL fCancel = FALSE;
	while (!fCancel && SciCall_FindText(searchFlags, &ttf) != -1) {
		if (ttf.chrgText.cpMin >= SciCall_GetSelectionStart() && ttf.chrgText.cpMax <= SciCall_GetSelectionEnd()) {
			if (++iCount == 1) {
				SciCall_BeginUndoAction();
			}

			SciCall_SetTargetRange(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
			const Sci_Position iReplacedLen = SciCall_ReplaceTargetEx(bReplaceRE, -1, pszReplace2);

			ttf.chrg.cpMin = (ttf.chrgText.cpMin + iReplacedLen);
			ttf.chrg.cpMax = SciCall_GetLength();

			if (ttf.chrg.cpMin == ttf.chrg.cpMax) {
				fCancel = TRUE;
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
			fCancel = TRUE;
		}
	}

	if (iCount) {
		const Sci_Position iPos = SciCall_GetTargetEnd();
		if (SciCall_GetSelectionEnd() <	iPos) {
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
	}

	// Remove wait cursor
	EndWaitCursor();

	if (bShowInfo) {
		ShwowReplaceCount(iCount);
	}

	LocalFree(pszReplace2);
	return TRUE;
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

		PosToStrW(iMaxLine, tchLn);
		FormatNumberStr(tchLn);
		GetDlgItemText(hwnd, IDC_LINE_RANGE, tchFmt, COUNTOF(tchFmt));
		wsprintf(tchLines, tchFmt, tchLn);
		SetDlgItemText(hwnd, IDC_LINE_RANGE, tchLines);

		PosToStrW(iLength, tchLn);
		FormatNumberStr(tchLn);
		GetDlgItemText(hwnd, IDC_COLUMN_RANGE, tchFmt, COUNTOF(tchFmt));
		wsprintf(tchLines, tchFmt, tchLn);
		SetDlgItemText(hwnd, IDC_COLUMN_RANGE, tchLines);

		CenterDlgInParent(hwnd);
	}
	return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			BOOL fTranslated;
			BOOL fTranslated2;
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
					SciCall_GotoPos(iNewCol - 1);
					SciCall_ChooseCaretX();
					EndDialog(hwnd, IDOK);
				} else {
					PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, IDC_COLNUM)), 1);
				}
			} else if (iNewLine > 0 && iNewLine <= iMaxLine) {
				EditJumpTo(iNewLine, iNewCol);
				EndDialog(hwnd, IDOK);
			} else {
				PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, ((iNewCol > 0) ? IDC_LINENUM : IDC_COLNUM))), 1);
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
BOOL EditLineNumDlg(HWND hwnd) {
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
	static HFONT hFontHover;
	static HCURSOR hCursorNormal;
	static HCURSOR hCursorHover;

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

		hCursorNormal = LoadCursor(NULL, IDC_ARROW);
		if ((hCursorHover = LoadCursor(NULL, IDC_HAND)) == NULL) {
			hCursorHover = LoadCursor(g_hInstance, IDC_ARROW);
		}

		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_PREFIX);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_PREFIX, wchPrefixLines.buffer);
		MultilineEditSetup(hwnd, IDC_MODIFY_LINE_APPEND);
		SetDlgItemText(hwnd, IDC_MODIFY_LINE_APPEND, wchAppendLines.buffer);
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
			SetCursor(id_hover != 0 ? hCursorHover : hCursorNormal);
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
		SetCursor(id_hover != 0 ? hCursorHover : hCursorNormal);
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
					PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetFocus()), 1);
				}
			}
			id_capture = 0;
		}
		SetCursor(id_hover != 0 ? hCursorHover : hCursorNormal);
	}
	break;

	case WM_CANCELMODE:
		if (id_capture != 0) {
			ReleaseCapture();
			id_hover = 0;
			id_capture = 0;
			SetCursor(hCursorNormal);
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			DStringW_GetDlgItemText(&wchPrefixLines, hwnd, IDC_MODIFY_LINE_PREFIX);
			DStringW_GetDlgItemText(&wchAppendLines, hwnd, IDC_MODIFY_LINE_APPEND);

			EditModifyLines(wchPrefixLines.buffer, wchAppendLines.buffer);
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
BOOL EditAlignDlg(HWND hwnd, int *piAlignMode) {
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
				BOOL bClear = TRUE;

				DStringW_GetDlgItemText(&wszOpen, hwnd, IDC_MODIFY_LINE_PREFIX);
				const int len = lstrlen(wszOpen.buffer);
				if (len >= 3) {
					LPCWSTR pwCur = StrChr(wszOpen.buffer, L'<');
					if (pwCur != NULL) {
						LPWSTR wchIns = (LPWSTR)NP2HeapAlloc((len + 5) * sizeof(WCHAR));
						lstrcpy(wchIns, L"</");
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
								bClear = FALSE;
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

void EditInsertDateTime(BOOL bShort) {
	WCHAR tchDateTime[256];
	WCHAR tchTemplate[256];

	SYSTEMTIME st;
	GetLocalTime(&st);

	if (IniGetString(INI_SECTION_NAME_FLAGS, bShort ? L"DateTimeShort" : L"DateTimeLong",
					 L"", tchTemplate, COUNTOF(tchTemplate))) {
		struct tm sst;
		sst.tm_isdst	= -1;
		sst.tm_sec		= (int)st.wSecond;
		sst.tm_min		= (int)st.wMinute;
		sst.tm_hour		= (int)st.wHour;
		sst.tm_mday		= (int)st.wDay;
		sst.tm_mon		= (int)st.wMonth - 1;
		sst.tm_year		= (int)st.wYear - 1900;
		sst.tm_wday		= (int)st.wDayOfWeek;
		mktime(&sst);
		wcsftime(tchDateTime, COUNTOF(tchDateTime), tchTemplate, &sst);
	} else {
		WCHAR tchDate[128];
		WCHAR tchTime[128];
		GetDateFormat(LOCALE_USER_DEFAULT, bShort ? DATE_SHORTDATE : DATE_LONGDATE,
					  &st, NULL, tchDate, COUNTOF(tchDate));
		GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, tchTime, COUNTOF(tchTime));

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
	sst.tm_isdst = -1;
	sst.tm_sec	 = (int)st.wSecond;
	sst.tm_min	 = (int)st.wMinute;
	sst.tm_hour	 = (int)st.wHour;
	sst.tm_mday	 = (int)st.wDay;
	sst.tm_mon	 = (int)st.wMonth - 1;
	sst.tm_year	 = (int)st.wYear - 1900;
	sst.tm_wday	 = (int)st.wDayOfWeek;
	mktime(&sst);

	WCHAR wchReplace[256];
	wcsftime(wchReplace, COUNTOF(wchReplace), wchTemplate, &sst);

	const UINT cpEdit = SciCall_GetCodePage();
#if NP2_USE_DESIGNATED_INITIALIZER
	EDITFINDREPLACE efrTS = {
		.hwnd = hwnd,
		.fuFlags = SCFIND_REGEXP,
	};
#else
	EDITFINDREPLACE efrTS = { "", "", "", "", hwnd, SCFIND_REGEXP };
#endif

	WideCharToMultiByte(cpEdit, 0, wchFind, -1, efrTS.szFind, COUNTOF(efrTS.szFind), NULL, NULL);
	WideCharToMultiByte(cpEdit, 0, wchReplace, -1, efrTS.szReplace, COUNTOF(efrTS.szReplace), NULL, NULL);

	if (!SciCall_IsSelectionEmpty()) {
		EditReplaceAllInSelection(hwnd, &efrTS, TRUE);
	} else {
		EditReplaceAll(hwnd, &efrTS, TRUE);
	}
}

typedef struct UnicodeControlCharacter {
	LPCSTR uccUTF8;
	LPCSTR representation;
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
	{ "\x1e", NULL },			// U+001E	RS		Record Separator (Block separator)
	{ "\x1f", NULL },			// U+001F	US		Unit Separator (Segment separator)
	{ "\xe2\x80\xa8", NULL },	// U+2028	LS		Line Separator
	{ "\xe2\x80\xa9", NULL },	// U+2029	PS		Paragraph Separator
	// Other
	{ "\xe2\x80\x8b", "ZWSP" },	// U+200B	ZWSP	Zero width space
	{ "\xe2\x81\xa0", "WJ" },	// U+2060	WJ		Word joiner
	{ "\xe2\x81\xa6", "LRI" },	// U+2066	LRI		Left-to-right isolate
	{ "\xe2\x81\xa7", "RLI" },	// U+2067	RLI		Right-to-left isolate
	{ "\xe2\x81\xa8", "FSI" },	// U+2068	FSI		First strong isolate
	{ "\xe2\x81\xa9", "PDI" },	// U+2069	PDI		Pop directional isolate
};

void EditInsertUnicodeControlCharacter(int menu) {
	menu = menu - IDM_INSERT_UNICODE_LRM;
	const UnicodeControlCharacter ucc = kUnicodeControlCharacterTable[menu];
	SciCall_ReplaceSel(ucc.uccUTF8);
}

void EditShowUnicodeControlCharacter(BOOL bShow) {
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

		if (iSortFlags & SORT_DESCENDING) {
			CheckRadioButton(hwnd, IDC_SORT_ASC, IDC_SORT_SHUFFLE, IDC_SORT_DESC);
		} else if (iSortFlags & SORT_SHUFFLE) {
			CheckRadioButton(hwnd, IDC_SORT_ASC, IDC_SORT_SHUFFLE, IDC_SORT_SHUFFLE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_UNIQUE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_IGNORE_CASE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_LOGICAL_NUMBER), FALSE);
		} else {
			CheckRadioButton(hwnd, IDC_SORT_ASC, IDC_SORT_SHUFFLE, IDC_SORT_ASC);
		}

		if (iSortFlags & SORT_MERGEDUP) {
			CheckDlgButton(hwnd, IDC_SORT_MERGE_DUP, BST_CHECKED);
		}

		if (iSortFlags & SORT_UNIQDUP) {
			CheckDlgButton(hwnd, IDC_SORT_REMOVE_DUP, BST_CHECKED);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), FALSE);
		}

		if (iSortFlags & SORT_UNIQUNIQ) {
			CheckDlgButton(hwnd, IDC_SORT_REMOVE_UNIQUE, BST_CHECKED);
		}

		if (iSortFlags & SORT_NOCASE) {
			CheckDlgButton(hwnd, IDC_SORT_IGNORE_CASE, BST_CHECKED);
		}

		if (iSortFlags & SORT_LOGICAL) {
			CheckDlgButton(hwnd, IDC_SORT_LOGICAL_NUMBER, BST_CHECKED);
		}

		if (!SciCall_IsRectangleSelection()) {
			*piSortFlags &= ~SORT_COLUMN;
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_COLUMN), FALSE);
		} else {
			*piSortFlags |= SORT_COLUMN;
			CheckDlgButton(hwnd, IDC_SORT_COLUMN, BST_CHECKED);
		}
		CenterDlgInParent(hwnd);
	}
	return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: {
			int *piSortFlags = (int *)GetWindowLongPtr(hwnd, DWLP_USER);
			int iSortFlags = 0;
			if (IsButtonChecked(hwnd, IDC_SORT_DESC)) {
				iSortFlags |= SORT_DESCENDING;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_SHUFFLE)) {
				iSortFlags |= SORT_SHUFFLE;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_MERGE_DUP)) {
				iSortFlags |= SORT_MERGEDUP;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_REMOVE_DUP)) {
				iSortFlags |= SORT_UNIQDUP;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_REMOVE_UNIQUE)) {
				iSortFlags |= SORT_UNIQUNIQ;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_IGNORE_CASE)) {
				iSortFlags |= SORT_NOCASE;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_LOGICAL_NUMBER)) {
				iSortFlags |= SORT_LOGICAL;
			}
			if (IsButtonChecked(hwnd, IDC_SORT_COLUMN)) {
				iSortFlags |= SORT_COLUMN;
			}
			*piSortFlags = iSortFlags;
			EndDialog(hwnd, IDOK);
		}
		break;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;

		case IDC_SORT_ASC:
		case IDC_SORT_DESC:
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), !IsButtonChecked(hwnd, IDC_SORT_REMOVE_UNIQUE));
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_DUP), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_UNIQUE), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_IGNORE_CASE), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_LOGICAL_NUMBER), TRUE);
			break;

		case IDC_SORT_SHUFFLE:
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_MERGE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_DUP), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_REMOVE_UNIQUE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_IGNORE_CASE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_SORT_LOGICAL_NUMBER), FALSE);
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
BOOL EditSortDlg(HWND hwnd, int *piSortFlags) {
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
	ExpandEnvironmentStringsEx(lpszCommand, (DWORD)(cbCommand / sizeof(WCHAR)));

	LPWSTR lpszArgs = (LPWSTR)NP2HeapAlloc(cbCommand);
	ExtractFirstArgument(lpszCommand, lpszCommand, lpszArgs);

	WCHAR wchDirectory[MAX_PATH] = L"";
	if (StrNotEmpty(szCurFile)) {
		lstrcpy(wchDirectory, szCurFile);
		PathRemoveFileSpec(wchDirectory);
	}

	SHELLEXECUTEINFO sei;
	ZeroMemory(&sei, sizeof(SHELLEXECUTEINFO));
	sei.cbSize = sizeof(SHELLEXECUTEINFO);
	sei.fMask = SEE_MASK_NOZONECHECKS;
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

void TryBrowseFile(HWND hwnd, LPCWSTR pszFile, BOOL bWarn) {
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
		GetModuleFileName(NULL, tchTemp, COUNTOF(tchTemp));
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
	ZeroMemory(&sei, sizeof(SHELLEXECUTEINFO));

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
			OpenContainingFolder(hwnd, pszFile, FALSE);
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

	struct Sci_TextToFind ft = { { iCurrentPos, 0 }, delimiters, { 0, 0 } };
	const int findFlag = SCFIND_REGEXP | SCFIND_POSIX;

	// forward
	if (iCurrentPos < iLineEnd) {
		ft.chrg.cpMax = iLineEnd;
		Sci_Position iPos = SciCall_FindText(findFlag, &ft);
		if (iPos >= 0) {
			iPos = ft.chrgText.cpMax;
			// keep column in filename(line,column): warning
			const int chPrev = SciCall_GetCharAt(iPos - 1);
			const int ch = SciCall_GetCharAt(iPos);
			if (chPrev == ',' && (ch >= '0' && ch <= '9')) {
				iLineEnd = SciCall_WordEndPosition(iPos, TRUE);
			} else {
				iLineEnd = SciCall_PositionBefore(iPos);
			}
		}
	}

	// backward
	if (iCurrentPos > iLineStart) {
		ft.chrg.cpMax = iLineStart;
		const Sci_Position iPos = SciCall_FindText(findFlag, &ft);
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
		Sci_Position iPos = SciCall_FindText(findFlag, &ft);
		if (iPos == -1) {
			break;
		}

		iStartPos = iPos + 1;
		iPos = SciCall_BraceMatchNext(iPos, ft.chrgText.cpMax);
		iEndPos = (iPos == -1) ? iLineEnd : iPos;
		if (iCurrentPos >= iStartPos && iCurrentPos <= iEndPos) {
			iLineStart = iStartPos;
			iLineEnd = iEndPos;
			break;
		}
		iStartPos = ft.chrgText.cpMax;
	}
	if (iLineStart >= iLineEnd) {
		return NULL;
	}

	char *mszSelection = (char *)NP2HeapAlloc(iLineEnd - iLineStart + 1);
	struct Sci_TextRange tr = { { iLineStart, iLineEnd }, mszSelection };
	SciCall_GetTextRange(&tr);

	return mszSelection;
}

extern BOOL bOpenFolderWithMetapath;

static DWORD EditOpenSelectionCheckFile(LPCWSTR link, LPWSTR path, int cchFilePath, LPWSTR wchDirectory) {
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

void EditOpenSelection(int type) {
	Sci_Position cchSelection = SciCall_GetSelTextLength();
	char *mszSelection = NULL;
	if (cchSelection > 1) {
		mszSelection = (char *)NP2HeapAlloc(cchSelection);
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
			if (*back != L'\0') {
				line = NULL;
			}
		}

		if (type == 4) { // containing folder
			if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
				type = 0;
			}
		} else if (dwAttributes != INVALID_FILE_ATTRIBUTES) {
			if (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				type = 3;
			} else {
				const BOOL can = line != NULL || Style_CanOpenFile(link);
				// open supported file in a new window
				type = can ? 2 : 1;
			}
		} else if (StrChr(link, L':')) { // link
			// TODO: check scheme
			type = 1;
		} else if (StrChr(link, L'@')) { // email
			lstrcpy(wszSelection, L"mailto:");
			lstrcpy(wszSelection + CSTRLEN(L"mailto:"), link);
			type = 1;
			link = wszSelection;
		}

		switch (type) {
		case 1:
			ShellExecute(hwndMain, L"open", link, NULL, NULL, SW_SHOWNORMAL);
			break;

		case 2: {
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
			ZeroMemory(&sei, sizeof(SHELLEXECUTEINFO));
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

		case 3:
			if (bOpenFolderWithMetapath) {
				TryBrowseFile(hwndMain, link, FALSE);
			} else {
				OpenContainingFolder(hwndMain, link, FALSE);
			}
			break;

		case 4:
			OpenContainingFolder(hwndMain, link, TRUE);
			break;
		}
	}

	NP2HeapFree(wszSelection);
}

//=============================================================================
//
// FileVars_Init()
//

extern BOOL bNoEncodingTags;
extern int fNoFileVariables;
extern BOOL fWordWrapG;
extern int iWordWrapMode;
extern int iLongLinesLimitG;

void FileVars_Init(LPCSTR lpData, DWORD cbData, LPFILEVARS lpfv) {
	ZeroMemory(lpfv, sizeof(FILEVARS));
	// see FileVars_Apply() for other Tab settings.
	tabSettings.schemeUseGlobalTabSettings = TRUE;
	lpfv->bTabIndents = tabSettings.bTabIndents;
	lpfv->fWordWrap = fWordWrapG;
	lpfv->iLongLinesLimit = iLongLinesLimitG;

	if ((fNoFileVariables && bNoEncodingTags) || !lpData || !cbData) {
		return;
	}

	char tch[512];
	strncpy(tch, lpData, min_u(cbData, sizeof(tch)));
	tch[sizeof(tch) - 1] = '\0';
	const BOOL utf8Sig = IsUTF8Signature(lpData);
	BOOL bDisableFileVariables = FALSE;

	// parse file variables at the beginning or end of the file.
	BOOL beginning = TRUE;
	int mask = 0;
	while (TRUE) {
		if (!fNoFileVariables) {
			// Emacs file variables
			int i;
			if (FileVars_ParseInt(tch, "enable-local-variables", &i) && (!i)) {
				bDisableFileVariables = TRUE;
			}

			if (!bDisableFileVariables) {
				if (FileVars_ParseInt(tch, "tab-width", &i)) {
					lpfv->iTabWidth = clamp_i(i, TAB_WIDTH_MIN, TAB_WIDTH_MAX);
					mask |= FV_TABWIDTH;
				}

				if (FileVars_ParseInt(tch, "*basic-indent", &i)) {
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
			}
		}

		if (!utf8Sig && !bNoEncodingTags && !bDisableFileVariables) {
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

		if (!fNoFileVariables && !bDisableFileVariables) {
			if (FileVars_ParseStr(tch, "mode", lpfv->tchMode, COUNTOF(lpfv->tchMode))) { // Emacs
				mask |= FV_MODE;
			}
		}

		if (beginning && mask == 0 && cbData > COUNTOF(tch)) {
			strncpy(tch, lpData + cbData - COUNTOF(tch) + 1, COUNTOF(tch) - 1);
			beginning = FALSE;
		} else {
			break;
		}
	}

	beginning = mask & FV_MaskHasTabIndentWidth;
	if (beginning == FV_TABWIDTH || beginning == FV_INDENTWIDTH) {
		if (beginning == FV_TABWIDTH) {
			lpfv->iIndentWidth = lpfv->iTabWidth;
		} else {
			lpfv->iTabWidth = lpfv->iIndentWidth;
		}
		mask |= FV_MaskHasTabIndentWidth;
	}
	if (mask & FV_ENCODING) {
		lpfv->iEncoding = Encoding_MatchA(lpfv->tchEncoding);
	}
	lpfv->mask = mask;
}

void EditSetWrapStartIndent(int tabWidth, int indentWidth) {
	int indent = 0;
	switch (iWordWrapIndent) {
	case EditWrapIndentOneCharacter:
		indent = 1;
		break;
	case EditWrapIndentTwoCharacter:
		indent = 2;
		break;
	case EditWrapIndentOneLevel:
		indent = indentWidth ? indentWidth : tabWidth;
		break;
	case EditWrapIndentTwoLevel:
		indent = indentWidth ? 2 * indentWidth : 2 * tabWidth;
		break;
	}
	SciCall_SetWrapStartIndent(indent);
}

void EditSetWrapIndentMode(int tabWidth, int indentWidth) {
	int indentMode;
	switch (iWordWrapIndent) {
	case EditWrapIndentSameAsSubline:
		indentMode = SC_WRAPINDENT_SAME;
		break;
	case EditWrapIndentOneLevelThanSubline:
		indentMode = SC_WRAPINDENT_INDENT;
		break;
	case EditWrapIndentTwoLevelThanSubline:
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
	const BOOL suffix = *pszName == '*';
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
BOOL FileVars_ParseInt(LPCSTR pszData, LPCSTR pszName, int *piValue) {
	LPCSTR pvStart = FileVars_Find(pszData, pszName);
	if (pvStart) {
		while (*pvStart == ':' || *pvStart == '=' || *pvStart == '\"' || *pvStart == '\'' || *pvStart == ' ' || *pvStart == '\t') {
			pvStart++;
		}

		char *pvEnd;
		*piValue = (int)strtol(pvStart, &pvEnd, 10);
		if (pvEnd != pvStart) {
			return TRUE;
		}

		switch (*pvStart) {
		case 't':
		case 'y':
			*piValue = 1;
			return TRUE;
		case 'f':
		case 'n':
			*piValue = 0;
			return FALSE;
		}
	}

	return FALSE;
}

//=============================================================================
//
// FileVars_ParseStr()
//
BOOL FileVars_ParseStr(LPCSTR pszData, LPCSTR pszName, char *pszValue, int cchValue) {
	LPCSTR pvStart = FileVars_Find(pszData, pszName);
	if (pvStart) {
		BOOL bQuoted = FALSE;

		while (*pvStart == ':' || *pvStart == '=' || *pvStart == '\"' || *pvStart == '\'' || *pvStart == ' ' || *pvStart == '\t') {
			if (*pvStart == '\'' || *pvStart == '"') {
				bQuoted = TRUE;
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
		return TRUE;
	}

	return FALSE;
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
					BOOL bSuccess = FALSE;
					RECT rcClient;

					if (GetThemeBackgroundContentRect(hTheme, NULL, EP_EDITTEXT, ETS_NORMAL, &csp->rgrc[0], &rcClient) == S_OK) {
						InflateRect(&rcClient, -1, -1);

						rcContent.left = rcClient.left - csp->rgrc[0].left;
						rcContent.top = rcClient.top - csp->rgrc[0].top;
						rcContent.right = csp->rgrc[0].right - rcClient.right;
						rcContent.bottom = csp->rgrc[0].bottom - rcClient.bottom;

						CopyRect(&csp->rgrc[0], &rcClient);
						bSuccess = TRUE;
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

// max level for Toggle Folds for Current Level for indentation based lexers.
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

static UINT Style_GetDefaultFoldState(int rid, int *maxLevel) {
	switch (rid) {
	case NP2LEX_TEXTFILE:
	case NP2LEX_2NDTEXTFILE:
	case NP2LEX_ANSI:
		*maxLevel = 2;
		return (1 << 1) | (1 << 2);
	case NP2LEX_CPP:
	case NP2LEX_CSHARP:
	case NP2LEX_XML:
	case NP2LEX_HTML:
	case NP2LEX_JSON:
		*maxLevel = 3;
		return (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);
	case NP2LEX_JAVA:
	case NP2LEX_RC:
	case NP2LEX_SCALA:
	case NP2LEX_RUBY:
		*maxLevel = 2;
		return (1 << 0) | (1 << 1) | (1 << 2);
	case NP2LEX_INI:
		*maxLevel = 0;
		return (1 << 0);
	case NP2LEX_DIFF:
		*maxLevel = 2;
		return (1 << 0) | (1 << 2);
	case NP2LEX_PYTHON:
		*maxLevel = 3;
		return (1 << 1) | (1 << 2) | (1 << 3);
	default:
		*maxLevel = 1;
		return (1 << 0) | (1 << 1);
	}
}

static void FoldToggleNode(Sci_Line line, FOLD_ACTION *pAction, BOOL *fToggled) {
	const BOOL fExpanded = SciCall_GetFoldExpanded(line);
	FOLD_ACTION action = *pAction;
	if (action == FOLD_ACTION_SNIFF) {
		action = fExpanded ? FOLD_ACTION_FOLD : FOLD_ACTION_EXPAND;
	}

	if ((BOOL)action != fExpanded) {
		SciCall_ToggleFold(line);
		if (*fToggled == FALSE || *pAction == FOLD_ACTION_SNIFF) {
			// empty INI section not changed after toggle (issue #48).
			const BOOL after = SciCall_GetFoldExpanded(line);
			if (fExpanded != after) {
				*fToggled = TRUE;
				if (*pAction == FOLD_ACTION_SNIFF) {
					*pAction = action;
				}
			}
		}
	}
}

void FoldToggleAll(FOLD_ACTION action) {
	BOOL fToggled = FALSE;
	SciCall_ColouriseAll();
	const Sci_Line lineCount = SciCall_GetLineCount();

	for (Sci_Line line = 0; line < lineCount; ++line) {
		const int level = SciCall_GetFoldLevel(line);
		if (level & SC_FOLDLEVELHEADERFLAG) {
			FoldToggleNode(line, &action, &fToggled);
		}
	}

	if (fToggled) {
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 5);
		SciCall_ScrollCaret();
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_EVEN, 0);
	}
}

void FoldToggleLevel(int lev, FOLD_ACTION action) {
	BOOL fToggled = FALSE;
	SciCall_ColouriseAll();
	const Sci_Line lineCount = SciCall_GetLineCount();
	Sci_Line line = 0;

	if (IsFoldIndentationBased(pLexCurrent->iLexer)) {
		struct FoldLevelStack levelStack = { 0, { 0 }};
		++lev;
		while (line < lineCount) {
			int level = SciCall_GetFoldLevel(line);
			if (level & SC_FOLDLEVELHEADERFLAG) {
				level &= SC_FOLDLEVELNUMBERMASK;
				FoldLevelStack_Push(&levelStack, level);
				if (lev == levelStack.levelCount) {
					FoldToggleNode(line, &action, &fToggled);
					line = SciCall_GetLastChild(line);
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
					FoldToggleNode(line, &action, &fToggled);
					line = SciCall_GetLastChild(line);
				}
			}
			++line;
		}
	}

	if (fToggled) {
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 5);
		SciCall_ScrollCaret();
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_EVEN, 0);
	}
}

void FoldToggleCurrentBlock(FOLD_ACTION action) {
	BOOL fToggled = FALSE;
	Sci_Line line = SciCall_LineFromPosition(SciCall_GetCurrentPos());
	const int level = SciCall_GetFoldLevel(line);

	if (!(level & SC_FOLDLEVELHEADERFLAG)) {
		line = SciCall_GetFoldParent(line);
		if (line < 0) {
			return;
		}
	}

	FoldToggleNode(line, &action, &fToggled);
	if (fToggled) {
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 5);
		SciCall_ScrollCaret();
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_EVEN, 0);
	}
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

	if (level != 0 && IsFoldIndentationBased(pLexCurrent->iLexer)) {
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
	BOOL fToggled = FALSE;
	int maxLevel = 0;
	SciCall_ColouriseAll();
	const UINT state = Style_GetDefaultFoldState(pLexCurrent->rid, &maxLevel);
	const Sci_Line lineCount = SciCall_GetLineCount();
	Sci_Line line = 0;

	if (IsFoldIndentationBased(pLexCurrent->iLexer)) {
		struct FoldLevelStack levelStack = { 0, { 0 }};
		while (line < lineCount) {
			int level = SciCall_GetFoldLevel(line);
			if (level & SC_FOLDLEVELHEADERFLAG) {
				level &= SC_FOLDLEVELNUMBERMASK;
				FoldLevelStack_Push(&levelStack, level);
				level = levelStack.levelCount;
				if ((state >> level) & 1) {
					FoldToggleNode(line, &action, &fToggled);
					if (level == maxLevel) {
						line = SciCall_GetLastChild(line);
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
				level -= SC_FOLDLEVELBASE;
				if ((state >> level) & 1) {
					FoldToggleNode(line, &action, &fToggled);
					if (level == maxLevel) {
						line = SciCall_GetLastChild(line);
					}
				}
			}
			++line;
		}
	}

	if (fToggled) {
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_SLOP | CARET_STRICT | CARET_EVEN, 5);
		SciCall_ScrollCaret();
		SciCall_SetXCaretPolicy(CARET_SLOP | CARET_EVEN, 50);
		SciCall_SetYCaretPolicy(CARET_EVEN, 0);
	}
}

static void FoldPerformAction(Sci_Line ln, int mode, FOLD_ACTION action) {
	BOOL fToggled = FALSE;
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
			const BOOL fHeader = (lv & SC_FOLDLEVELHEADERFLAG) != 0;
			lv &= SC_FOLDLEVELNUMBERMASK;

			if (lv < lvStop || (lv == lvStop && fHeader && ln != lnNode)) {
				return;
			}
			if (fHeader && (lv == lvNode || (lv > lvNode && (mode & FOLD_CHILDREN)))) {
				FoldToggleNode(ln, &action, &fToggled);
			}
		}
	} else {
		FoldToggleNode(ln, &action, &fToggled);
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
		BOOL sibling = menu == IDM_EDIT_GOTO_PREV_SIBLING_BLOCK;
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
						sibling = FALSE;
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

		BOOL sibling = menu == IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK;
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
						sibling = FALSE;
						break;
					}
					line = SciCall_GetLastChild(line);
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
