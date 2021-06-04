/******************************************************************************
*
*
* Notepad2
*
* Helpers.c
*   General helper functions
*   Parts taken from SciTE, (c) Neil Hodgson, https://www.scintilla.org
*   MinimizeToTray (c) 2000 Matthew Ellis
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
#include <shlobj.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <stdio.h>
#include "config.h"
#include "Helpers.h"
#include "VectorISA.h"
#include "GraphicUtils.h"
#include "resource.h"

LPCSTR GetCurrentLogTime(void) {
	static char buf[16];
	SYSTEMTIME lt;
	GetLocalTime(&lt);
	sprintf(buf, "%02d:%02d:%02d.%03d", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
	return buf;
}

void StopWatch_Show(const StopWatch *watch, LPCWSTR msg) {
	const double elapsed = StopWatch_Get(watch);
	WCHAR buf[256];
	swprintf(buf, COUNTOF(buf), L"%s: %.6f", msg, elapsed);
	MessageBox(NULL, buf, L"Notepad2", MB_OK);
}

void StopWatch_ShowLog(const StopWatch *watch, LPCSTR msg) {
	const double elapsed = StopWatch_Get(watch);
#if 0
	char buf[256];
	snprintf(buf, COUNTOF(buf), "%s %s: %.6f\n", "Notepad2", msg, elapsed);
	DebugPrint(buf);
#else
	printf("%s %s: %.6f\n", "Notepad2", msg, elapsed);
#endif
}

void DebugPrintf(const char *fmt, ...) {
	char buf[1024] = "";
	va_list va;
	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);
	DebugPrint(buf);
}

void IniClearSectionEx(LPCWSTR lpSection, LPCWSTR lpszIniFile, BOOL bDelete) {
	if (StrIsEmpty(lpszIniFile)) {
		return;
	}

	WritePrivateProfileSection(lpSection, (bDelete ? NULL : L""), lpszIniFile);
}

void IniClearAllSectionEx(LPCWSTR lpszPrefix, LPCWSTR lpszIniFile, BOOL bDelete) {
	if (StrIsEmpty(lpszIniFile)) {
		return;
	}

	WCHAR sections[1024] = L"";
	GetPrivateProfileSectionNames(sections, COUNTOF(sections), lpszIniFile);

	LPCWSTR p = sections;
	LPCWSTR value = bDelete ? NULL : L"";
	const int len = lstrlen(lpszPrefix);

	while (*p) {
		if (StrHasPrefixCaseEx(p, lpszPrefix, len)) {
			WritePrivateProfileSection(p, value, lpszIniFile);
		}
		p = StrEnd(p) + 1;
	}
}

//=============================================================================
//
// Manipulation of (cached) ini file sections
//
BOOL IniSectionParseArray(IniSection *section, LPWSTR lpCachedIniSection, BOOL quoted) {
	IniSectionClear(section);
	if (StrIsEmpty(lpCachedIniSection)) {
		return FALSE;
	}

	const int capacity = section->capacity;
	LPWSTR p = lpCachedIniSection;
	int count = 0;

	do {
		LPWSTR v = StrChr(p, L'=');
		if (v != NULL) {
			*v++ = L'\0';
			const int valueLen = lstrlen(v);
			IniKeyValueNode *node = &section->nodeList[count];
			node->key = p;
			p = v + valueLen + 1;
			if (quoted && valueLen > 1 && *v == L'\"' && v[valueLen - 1] == L'\"') {
				v[valueLen - 1] = L'\0';
				*v++ = L'\0';
			}
			node->value = v;
			++count;
		} else {
			p = StrEnd(p) + 1;
		}
	} while (*p && count < capacity);

	section->count = count;
	return count != 0;
}

BOOL IniSectionParse(IniSection *section, LPWSTR lpCachedIniSection) {
	IniSectionClear(section);
	if (StrIsEmpty(lpCachedIniSection)) {
		return FALSE;
	}

	const int capacity = section->capacity;
	LPWSTR p = lpCachedIniSection;
	int count = 0;

	do {
		LPWSTR v = StrChr(p, L'=');
		if (v != NULL) {
			*v++ = L'\0';
			const UINT keyLen = (UINT)(v - p - 1);
			IniKeyValueNode *node = &section->nodeList[count];
			node->hash = keyLen | ((*(const UINT *)p) << 8);
			node->key = p;
			node->value = v;
			++count;
			p = v;
		}
		p = StrEnd(p) + 1;
	} while (*p && count < capacity);

	if (count == 0) {
		return FALSE;
	}

	section->count = count;
	section->head = &section->nodeList[0];
	--count;
#if IniSectionImplUseSentinelNode
	section->nodeList[count].next = section->sentinel;
#else
	section->nodeList[count].next = NULL;
#endif
	while (count != 0) {
		section->nodeList[count - 1].next = &section->nodeList[count];
		--count;
	}
	return TRUE;
}

LPCWSTR IniSectionUnsafeGetValue(IniSection *section, LPCWSTR key, int keyLen) {
	if (keyLen == 0) {
		keyLen = lstrlen(key);
	}

	const UINT hash = keyLen | ((*(const UINT *)key) << 8);
	IniKeyValueNode *node = section->head;
	IniKeyValueNode *prev = NULL;
#if IniSectionImplUseSentinelNode
	section->sentinel->hash = hash;
	while (TRUE) {
		if (node->hash == hash) {
			if (node == section->sentinel) {
				return NULL;
			}
			if (StrEqual(node->key, key)) {
				// remove the node
				--section->count;
				if (prev == NULL) {
					section->head = node->next;
				} else {
					prev->next = node->next;
				}
				return node->value;
			}
		}
		prev = node;
		node = node->next;
	}
#else
	do {
		if (node->hash == hash && StrEqual(node->key, key)) {
			// remove the node
			--section->count;
			if (prev == NULL) {
				section->head = node->next;
			} else {
				prev->next = node->next;
			}
			return node->value;
		}
		prev = node;
		node = node->next;
	} while (node);
	return NULL;
#endif
}

void IniSectionGetStringImpl(IniSection *section, LPCWSTR key, int keyLen, LPCWSTR lpDefault, LPWSTR lpReturnedString, int cchReturnedString) {
	LPCWSTR value = IniSectionGetValueImpl(section, key, keyLen);
	// allow empty string value
	lstrcpyn(lpReturnedString, ((value == NULL) ? lpDefault : value), cchReturnedString);
}

int IniSectionGetIntImpl(IniSection *section, LPCWSTR key, int keyLen, int iDefault) {
	LPCWSTR value = IniSectionGetValueImpl(section, key, keyLen);
	if (value && CRTStrToInt(value, &keyLen)) {
		return keyLen;
	}
	return iDefault;
}

BOOL IniSectionGetBoolImpl(IniSection *section, LPCWSTR key, int keyLen, BOOL bDefault) {
	LPCWSTR value = IniSectionGetValueImpl(section, key, keyLen);
	if (value) {
		const UINT t = *value - L'0';
		if (t <= 1U) {
			return t;
		}
	}
	return bDefault;
}

void IniSectionSetString(IniSectionOnSave *section, LPCWSTR key, LPCWSTR value) {
	LPWSTR p = section->next;
	lstrcpy(p, key);
	lstrcat(p, L"=");
	lstrcat(p, value);
	p = StrEnd(p) + 1;
	*p = L'\0';
	section->next = p;
}

void IniSectionSetQuotedString(IniSectionOnSave *section, LPCWSTR key, LPCWSTR value) {
	LPWSTR p = section->next;
	lstrcpy(p, key);
	lstrcat(p, L"=\"");
	lstrcat(p, value);
	lstrcat(p, L"\"");
	p = StrEnd(p) + 1;
	*p = L'\0';
	section->next = p;
}

LPWSTR Registry_GetString(HKEY hKey, LPCWSTR valueName) {
	LPWSTR lpszText = NULL;
	DWORD type = REG_SZ;
	DWORD size = 0;

	LSTATUS status = RegQueryValueEx(hKey, valueName, NULL, &type, NULL, &size);
	if (status == ERROR_SUCCESS && type == REG_SZ && size != 0) {
		size = (size + 1)*sizeof(WCHAR);
		lpszText = (LPWSTR)NP2HeapAlloc(size);
		status = RegQueryValueEx(hKey, valueName, NULL, &type, (LPBYTE)lpszText, &size);
		if (status != ERROR_SUCCESS || type != REG_SZ || size == 0) {
			NP2HeapFree(lpszText);
			lpszText = NULL;
		}
	}
	return lpszText;
}

LSTATUS Registry_SetString(HKEY hKey, LPCWSTR valueName, LPCWSTR lpszText) {
	DWORD len = lstrlen(lpszText);
	len = len ? ((len + 1)*sizeof(WCHAR)) : 0;
	LSTATUS status = RegSetValueEx(hKey, valueName, 0, REG_SZ, (const BYTE *)lpszText, len);
	return status;
}

#if _WIN32_WINNT < _WIN32_WINNT_VISTA
LSTATUS Registry_DeleteTree(HKEY hKey, LPCWSTR lpSubKey) {
	typedef LSTATUS (WINAPI *RegDeleteTreeSig)(HKEY hKey, LPCWSTR lpSubKey);
	RegDeleteTreeSig pfnRegDeleteTree = DLLFunctionEx(RegDeleteTreeSig, L"advapi32.dll", "RegDeleteTreeW");

	LSTATUS status;
	if (pfnRegDeleteTree != NULL) {
		status = pfnRegDeleteTree(hKey, lpSubKey);
	} else {
		status = RegDeleteKey(hKey, lpSubKey);
		if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
			// TODO: Deleting a Key with Subkeys on Windows XP.
			// https://docs.microsoft.com/en-us/windows/win32/sysinfo/deleting-a-key-with-subkeys
		}
	}

	return status;
}
#endif

int DStringW_GetWindowText(DStringW *s, HWND hwnd) {
	int len = GetWindowTextLength(hwnd);
	if (len == 0) {
		if (s->buffer != NULL) {
			s->buffer[0] = L'\0';
		}
	} else {
		if (len + 1 > s->capacity || s->buffer == NULL) {
			len = (int)((len + 1) * sizeof(WCHAR));
			LPWSTR buffer = (s->buffer == NULL) ? (LPWSTR)NP2HeapAlloc(len) : (LPWSTR)NP2HeapReAlloc(s->buffer, len);
			if (buffer != NULL) {
				s->buffer = buffer;
				s->capacity = (int)(NP2HeapSize(buffer) / sizeof(WCHAR));
			}
		}
		len = GetWindowText(hwnd, s->buffer, s->capacity);
	}
	return len;
}

int ParseCommaList(LPCWSTR str, int result[], int count) {
	if (StrIsEmpty(str)) {
		return 0;
	}

	int index = 0;
	while (index < count) {
		LPWSTR end;
		result[index] = (int)wcstol(str, &end, 10);
		if (str == end) {
			break;
		}

		++index;
		if (*end == L',') {
			++end;
		}
		str = end;
	}
	return index;
}

int ParseCommaList64(LPCWSTR str, int64_t result[], int count) {
	if (StrIsEmpty(str)) {
		return 0;
	}

	int index = 0;
	while (index < count) {
		LPWSTR end;
		result[index] = _wcstoi64(str, &end, 10);
		if (str == end) {
			break;
		}

		++index;
		if (*end == L',') {
			++end;
		}
		str = end;
	}
	return index;
}

BOOL FindUserResourcePath(LPCWSTR path, LPWSTR outPath) {
	// similar to CheckIniFile()
	WCHAR tchFileExpanded[MAX_PATH];
	ExpandEnvironmentStrings(path, tchFileExpanded, COUNTOF(tchFileExpanded));

	if (PathIsRelative(tchFileExpanded)) {
		WCHAR tchBuild[MAX_PATH];
		// relative to program ini file
		if (StrNotEmpty(szIniFile)) {
			lstrcpy(tchBuild, szIniFile);
			lstrcpy(PathFindFileName(tchBuild), tchFileExpanded);
			if (PathIsFile(tchBuild)) {
				lstrcpy(outPath, tchBuild);
				return TRUE;
			}
		}

		// relative to program exe file
		GetModuleFileName(NULL, tchBuild, COUNTOF(tchBuild));
		lstrcpy(PathFindFileName(tchBuild), tchFileExpanded);
		if (PathIsFile(tchBuild)) {
			lstrcpy(outPath, tchBuild);
			return TRUE;
		}
	} else if (PathIsFile(tchFileExpanded)) {
		lstrcpy(outPath, tchFileExpanded);
		return TRUE;
	}
	return FALSE;
}

HBITMAP LoadBitmapFile(LPCWSTR path) {
	WCHAR szTmp[MAX_PATH];
	if (!FindUserResourcePath(path, szTmp)) {
		return NULL;
	}

	HBITMAP hbmp = (HBITMAP)LoadImage(NULL, szTmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
	return hbmp;
}

HBITMAP EnlargeImageForDPI(HBITMAP hbmp, UINT dpi) {
	BITMAP bmp;
	if (dpi > USER_DEFAULT_SCREEN_DPI && GetObject(hbmp, sizeof(BITMAP), &bmp)) {
		const int width = MulDiv(dpi, bmp.bmWidth, USER_DEFAULT_SCREEN_DPI);
		const int height = MulDiv(dpi, bmp.bmHeight, USER_DEFAULT_SCREEN_DPI);
		HBITMAP hCopy = (HBITMAP)CopyImage(hbmp, IMAGE_BITMAP, width, height, LR_COPYRETURNORG | LR_COPYDELETEORG);
		if (hCopy != NULL) {
			hbmp = hCopy;
		}
	}

	return hbmp;
}

HBITMAP ResizeImageForDPI(HBITMAP hbmp, UINT dpi, int height) {
	BITMAP bmp;
	if (dpi > USER_DEFAULT_SCREEN_DPI && GetObject(hbmp, sizeof(BITMAP), &bmp)) {
		height = MulDiv(dpi, height, USER_DEFAULT_SCREEN_DPI);
		// keep aspect ratio
		const int width = MulDiv(height, bmp.bmWidth, bmp.bmHeight);
		HBITMAP hCopy = (HBITMAP)CopyImage(hbmp, IMAGE_BITMAP, width, height, LR_COPYRETURNORG | LR_COPYDELETEORG);
		if (hCopy != NULL) {
			hbmp = hCopy;
		}
	}

	return hbmp;
}


void BackgroundWorker_Init(BackgroundWorker *worker, HWND hwnd) {
	worker->hwnd = hwnd;
	worker->eventCancel = CreateEvent(NULL, TRUE, FALSE, NULL);
	worker->workerThread = NULL;
}

void BackgroundWorker_Stop(BackgroundWorker *worker) {
	SetEvent(worker->eventCancel);
	HANDLE workerThread = worker->workerThread;
	if (workerThread) {
		worker->workerThread = NULL;
		while (WaitForSingleObject(workerThread, 0) != WAIT_OBJECT_0) {
			MSG msg;
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		CloseHandle(workerThread);
	}
}

void BackgroundWorker_Cancel(BackgroundWorker *worker) {
	BackgroundWorker_Stop(worker);
	ResetEvent(worker->eventCancel);
}

void BackgroundWorker_Destroy(BackgroundWorker *worker) {
	BackgroundWorker_Stop(worker);
	CloseHandle(worker->eventCancel);
}

//=============================================================================
//
// PrivateSetCurrentProcessExplicitAppUserModelID()
//
HRESULT PrivateSetCurrentProcessExplicitAppUserModelID(PCWSTR AppID) {
	if (StrIsEmpty(AppID)) {
		return S_OK;
	}
	if (StrCaseEqual(AppID, L"(default)")) {
		return S_OK;
	}

	// since Windows 7
#if _WIN32_WINNT >= _WIN32_WINNT_WIN7
	return SetCurrentProcessExplicitAppUserModelID(AppID);
#else
	typedef HRESULT (WINAPI *SetCurrentProcessExplicitAppUserModelIDSig)(PCWSTR AppID);
	SetCurrentProcessExplicitAppUserModelIDSig pfnSetCurrentProcessExplicitAppUserModelID =
		DLLFunctionEx(SetCurrentProcessExplicitAppUserModelIDSig, L"shell32.dll", "SetCurrentProcessExplicitAppUserModelID");
	if (pfnSetCurrentProcessExplicitAppUserModelID) {
		return pfnSetCurrentProcessExplicitAppUserModelID(AppID);
	}
	return S_OK;
#endif
}

//=============================================================================
//
// IsElevated()
//
BOOL IsElevated(void) {
	BOOL bIsElevated = FALSE;
	HANDLE hToken = NULL;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
		TOKEN_ELEVATION te;
		DWORD dwReturnLength = 0;

		if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &dwReturnLength)) {
			if (dwReturnLength == sizeof(te)) {
				bIsElevated = te.TokenIsElevated;
			}
		}
		CloseHandle(hToken);
	}
	return bIsElevated;
}

//=============================================================================
//
// BitmapMergeAlpha()
// Merge alpha channel into color channel
//
BOOL BitmapMergeAlpha(HBITMAP hbmp, COLORREF crDest) {
	BITMAP bmp;
	if (GetObject(hbmp, sizeof(BITMAP), &bmp)) {
		if (bmp.bmBitsPixel == 32) {
			//StopWatch watch;
			//StopWatch_Start(watch);
#if NP2_USE_AVX2
			#define BitmapMergeAlpha_Tag	"sse4 2x1"
			const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 2;
			uint64_t *prgba = (uint64_t *)bmp.bmBits;

			const __m128i i16x8Back = rgba_to_bgra_epi16x8_sse4_si32(crDest);
			for (ULONG x = 0; x < count; x++, prgba++) {
				__m128i i16x8Fore = unpack_color_epi16_sse4_ptr64(prgba);
				__m128i i16x8Alpha = _mm_shufflehi_epi16(_mm_shufflelo_epi16(i16x8Fore, 0xff), 0xff);
				i16x8Fore = mm_alpha_blend_epi16(i16x8Fore, i16x8Back, i16x8Alpha);
				const uint64_t color = pack_color_epi16_sse2_si64(i16x8Fore);
				*prgba = color | UINT64_C(0xff000000ff000000);
			}

#elif NP2_USE_SSE2
			#define BitmapMergeAlpha_Tag	"sse2 1x1"
			const ULONG count = bmp.bmHeight * bmp.bmWidth;
			uint32_t *prgba = (uint32_t *)bmp.bmBits;

			const __m128i i16x4Back = rgba_to_bgra_epi16_sse2_si32(crDest);
			for (ULONG x = 0; x < count; x++, prgba++) {
				__m128i i16x4Fore = unpack_color_epi16_sse2_ptr32(prgba);
				__m128i i16x4Alpha = _mm_shufflelo_epi16(i16x4Fore, 0xff);
				i16x4Fore = mm_alpha_blend_epi16(i16x4Fore, i16x4Back, i16x4Alpha);
				const uint32_t color = pack_color_epi16_sse2_si32(i16x4Fore);
				*prgba = color | 0xff000000U;
			}

#else
			#define BitmapMergeAlpha_Tag	"scalar"
			const ULONG count = bmp.bmHeight * bmp.bmWidth;
			RGBQUAD *prgba = (RGBQUAD *)bmp.bmBits;

			const BYTE red = GetRValue(crDest);
			const BYTE green = GetGValue(crDest);
			const BYTE blue = GetBValue(crDest);
			for (ULONG x = 0; x < count; x++) {
				const BYTE alpha = prgba[x].rgbReserved;
				prgba[x].rgbRed = ((prgba[x].rgbRed * alpha) + (red * (255 ^ alpha))) >> 8;
				prgba[x].rgbGreen = ((prgba[x].rgbGreen * alpha) + (green * (255 ^ alpha))) >> 8;
				prgba[x].rgbBlue = ((prgba[x].rgbBlue * alpha) + (blue * (255 ^ alpha))) >> 8;
				prgba[x].rgbReserved = 0xFF;
			}
#endif
			//StopWatch_Stop(watch);
			//StopWatch_ShowLog(&watch, "BitmapMergeAlpha " BitmapMergeAlpha_Tag);
			#undef BitmapMergeAlpha_Tag
			return TRUE;
		}
	}
	return FALSE;
}

//=============================================================================
//
// BitmapAlphaBlend()
// Perform alpha blending to color channel only
//
BOOL BitmapAlphaBlend(HBITMAP hbmp, COLORREF crDest, BYTE alpha) {
	BITMAP bmp;
	if (GetObject(hbmp, sizeof(BITMAP), &bmp)) {
		if (bmp.bmBitsPixel == 32) {
			//StopWatch watch;
			//StopWatch_Start(watch);
			//FILE *fp = fopen("bitmap.dat", "wb");
			//fwrite(bmp.bmBits, 1, bmp.bmHeight*bmp.bmWidth*4, fp);
			//fclose(fp);
#if NP2_USE_AVX2
#if 1
			#define BitmapAlphaBlend_Tag	"avx2 4x1"
			const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 4;
			__m128i *prgba = (__m128i *)bmp.bmBits;

			const __m256i i16x16Alpha = _mm256_broadcastw_epi16(mm_setlo_epi32(alpha));
			const __m256i i16x16Back = _mm256_broadcastq_epi64(_mm_mullo_epi16(rgba_to_bgra_epi16_sse4_si32(crDest), mm_xor_alpha_epi16(_mm256_castsi256_si128(i16x16Alpha))));
			const __m256i i16x16_0x8081 = _mm256_broadcastsi128_si256(_mm_set1_epi16(-0x8000 | 0x81));
			for (ULONG x = 0; x < count; x++, prgba++) {
				const __m256i origin = _mm256_cvtepu8_epi16(*prgba);
				__m256i i16x16Fore = _mm256_mullo_epi16(origin, i16x16Alpha);
				i16x16Fore = _mm256_add_epi16(i16x16Fore, i16x16Back);
				i16x16Fore = _mm256_srli_epi16(_mm256_mulhi_epu16(i16x16Fore, i16x16_0x8081), 7);
				i16x16Fore = _mm256_blend_epi16(origin, i16x16Fore, 0x77);
				i16x16Fore = _mm256_packus_epi16(i16x16Fore, i16x16Fore);
				i16x16Fore = _mm256_permute4x64_epi64(i16x16Fore, 8);
				_mm_storeu_si128(prgba, _mm256_castsi256_si128(i16x16Fore));
			}
#else
			#define BitmapAlphaBlend_Tag	"sse4 2x1"
			const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 2;
			uint64_t *prgba = (uint64_t *)bmp.bmBits;

			const __m128i i16x8Alpha = _mm_broadcastw_epi16(mm_setlo_epi32(alpha));
			const __m128i i16x8Back = _mm_mullo_epi16(rgba_to_bgra_epi16x8_sse4_si32(crDest), mm_xor_alpha_epi16(i16x8Alpha));
			for (ULONG x = 0; x < count; x++, prgba++) {
				const __m128i origin = unpack_color_epi16_sse4_ptr64(prgba);
				__m128i i16x8Fore = _mm_mullo_epi16(origin, i16x8Alpha);
				i16x8Fore = _mm_add_epi16(i16x8Fore, i16x8Back);
				i16x8Fore = mm_div_epu16_by_255(i16x8Fore);
				i16x8Fore = _mm_blend_epi16(origin, i16x8Fore, 0x77);
				i16x8Fore = pack_color_epi16_sse2_si128(i16x8Fore);
				_mm_storel_epi64((__m128i *)prgba, i16x8Fore);
			}
#endif // NP2_USE_AVX2
#elif NP2_USE_SSE2
			#define BitmapAlphaBlend_Tag	"sse2 1x4"
			const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 4;
			__m128i *prgba = (__m128i *)bmp.bmBits;

			const __m128i i16x8Alpha = _mm_shuffle_epi32(mm_setlo_alpha_epi16(alpha), 0x44);
			__m128i i16x8Back = _mm_shuffle_epi32(rgba_to_bgra_epi16_sse2_si32(crDest), 0x44);
			i16x8Back = _mm_mullo_epi16(i16x8Back, mm_xor_alpha_epi16(i16x8Alpha));
			for (ULONG x = 0; x < count; x++, prgba++) {
				__m128i origin = _mm_loadu_si128(prgba);
				__m128i color42 = _mm_shuffle_epi32(origin, 0x31);

				__m128i i16x8Fore = unpacklo_color_epi16_sse2_si32(origin);
				i16x8Fore = _mm_unpacklo_epi64(i16x8Fore, unpacklo_color_epi16_sse2_si32(color42));
				i16x8Fore = _mm_mullo_epi16(i16x8Fore, i16x8Alpha);
				i16x8Fore = _mm_add_epi16(i16x8Fore, i16x8Back);
				i16x8Fore = mm_div_epu16_by_255(i16x8Fore);
				__m128i i32x4Fore = pack_color_epi16_sse2_si128(i16x8Fore);

				i16x8Fore = unpackhi_color_epi16_sse2_si128(origin);
				i16x8Fore = _mm_unpacklo_epi64(i16x8Fore, unpackhi_color_epi16_sse2_si128(color42));
				i16x8Fore = _mm_mullo_epi16(i16x8Fore, i16x8Alpha);
				i16x8Fore = _mm_add_epi16(i16x8Fore, i16x8Back);
				i16x8Fore = mm_div_epu16_by_255(i16x8Fore);
				color42 = pack_color_epi16_sse2_si128(i16x8Fore);

				i32x4Fore = _mm_unpacklo_epi64(i32x4Fore, color42);
				i32x4Fore = _mm_and_si128(_mm_set1_epi32(0x00ffffff), i32x4Fore);
				origin = _mm_andnot_si128(_mm_set1_epi32(0x00ffffff), origin);
				i32x4Fore = _mm_or_si128(origin, i32x4Fore);
				_mm_storeu_si128(prgba, i32x4Fore);
			}

#else
			#define BitmapAlphaBlend_Tag	"scalar"
			const ULONG count = bmp.bmHeight * bmp.bmWidth;
			RGBQUAD *prgba = (RGBQUAD *)bmp.bmBits;

			const WORD red = GetRValue(crDest) * (255 ^ alpha);
			const WORD green = GetGValue(crDest) * (255 ^ alpha);
			const WORD blue = GetBValue(crDest) * (255 ^ alpha);
			for (ULONG x = 0; x < count; x++) {
				prgba[x].rgbRed = ((prgba[x].rgbRed * alpha) + red) >> 8;
				prgba[x].rgbGreen = ((prgba[x].rgbGreen * alpha) + green) >> 8;
				prgba[x].rgbBlue = ((prgba[x].rgbBlue * alpha) + blue) >> 8;
			}
#endif
			//StopWatch_Stop(watch);
			//StopWatch_ShowLog(&watch, "BitmapAlphaBlend " BitmapAlphaBlend_Tag);
			#undef BitmapAlphaBlend_Tag
			return TRUE;
		}
	}
	return FALSE;
}

//=============================================================================
//
// BitmapGrayScale()
// Gray scale color channel only
//
BOOL BitmapGrayScale(HBITMAP hbmp) {
	BITMAP bmp;
	if (GetObject(hbmp, sizeof(BITMAP), &bmp)) {
		if (bmp.bmBitsPixel == 32) {
			const ULONG count = bmp.bmHeight * bmp.bmWidth;
			RGBQUAD *prgba = (RGBQUAD *)bmp.bmBits;

			for (ULONG x = 0; x < count; x++) {
				// gray = 0.299*red + 0.587*green + 0.114*blue
				BYTE gray = (prgba[x].rgbRed * 38 + prgba[x].rgbGreen * 75 + prgba[x].rgbBlue * 15) >> 7;
				gray = ((gray * 0x80) + (0xD0 * (255 ^ 0x80))) >> 8;
				prgba[x].rgbRed = prgba[x].rgbGreen = prgba[x].rgbBlue = gray;
			}
			return TRUE;
		}
	}
	return FALSE;
}

//=============================================================================
//
// VerifyContrast()
// Check if two colors can be distinguished
//
BOOL VerifyContrast(COLORREF cr1, COLORREF cr2) {
#if NP2_USE_AVX2
	__m128i i32x4Fore = unpack_color_epi32_sse4_si32(cr1);
	__m128i i32x4Back = unpack_color_epi32_sse4_si32(cr2);

	__m128i diff = _mm_sad_epu8(i32x4Fore, i32x4Back);
	int value = mm_hadd_epi32_si32(diff);
	if (value >= 400) {
		return TRUE;
	}

	i32x4Fore = _mm_mullo_epi16(i32x4Fore, _mm_setr_epi32(3, 5, 1, 0));
	i32x4Back = _mm_mullo_epi16(i32x4Back, _mm_setr_epi32(3, 6, 1, 0));
	diff = _mm_sub_epi32(i32x4Fore, i32x4Back);
	value = mm_hadd_epi32_si32(diff);
	return abs(value) >= 400;

#else
	const BYTE r1 = GetRValue(cr1);
	const BYTE g1 = GetGValue(cr1);
	const BYTE b1 = GetBValue(cr1);
	const BYTE r2 = GetRValue(cr2);
	const BYTE g2 = GetGValue(cr2);
	const BYTE b2 = GetBValue(cr2);

	return	((abs((3 * r1 + 5 * g1 + 1 * b1) - (3 * r2 + 6 * g2 + 1 * b2))) >= 400) ||
			((abs(r1 - r2) + abs(b1 - b2) + abs(g1 - g2)) >= 400);
#endif
}

//=============================================================================
//
// IsFontAvailable()
// Test if a certain font is installed on the system
//
static int CALLBACK EnumFontFamExProc(CONST LOGFONT *plf, CONST TEXTMETRIC *ptm, DWORD fontType, LPARAM lParam) {
	UNREFERENCED_PARAMETER(plf);
	UNREFERENCED_PARAMETER(ptm);
	UNREFERENCED_PARAMETER(fontType);

	*((PBOOL)lParam) = TRUE;
	return FALSE;
}

BOOL IsFontAvailable(LPCWSTR lpszFontName) {
	BOOL fFound = FALSE;

	LOGFONT lf;
	ZeroMemory(&lf, sizeof(lf));
	lstrcpyn(lf.lfFaceName, lpszFontName, LF_FACESIZE);
	lf.lfCharSet = DEFAULT_CHARSET;

	HDC hDC = GetDC(NULL);
	EnumFontFamiliesEx(hDC, &lf, EnumFontFamExProc, (LPARAM)&fFound, 0);
	ReleaseDC(NULL, hDC);

	return fFound;
}

//=============================================================================
//
// SetClipData()
//
void SetClipData(HWND hwnd, LPCWSTR pszData) {
	if (OpenClipboard(hwnd)) {
		EmptyClipboard();
		HANDLE hData = GlobalAlloc(GHND, sizeof(WCHAR) * (lstrlen(pszData) + 1));
		WCHAR *pData = (WCHAR *)GlobalLock(hData);
		lstrcpyn(pData, pszData, (int)(GlobalSize(hData) / sizeof(WCHAR)));
		GlobalUnlock(hData);
		SetClipboardData(CF_UNICODETEXT, hData);
		CloseClipboard();
	}
}

//=============================================================================
//
// SetWindowTitle()
//
BOOL bFreezeAppTitle = FALSE;

BOOL SetWindowTitle(HWND hwnd, UINT uIDAppName, BOOL bIsElevated, UINT uIDUntitled,
					LPCWSTR lpszFile, int iFormat, BOOL bModified,
					UINT uIDReadOnly, BOOL bReadOnly,
					UINT uIDLocked, BOOL bLocked,
					LPCWSTR lpszExcerpt) {

	static WCHAR szCachedFile[MAX_PATH] = L"";
	static WCHAR szCachedDisplayName[MAX_PATH] = L"";
	static const WCHAR *pszSep = L" - ";
	static const WCHAR *pszMod = L"* ";

	if (bFreezeAppTitle) {
		return FALSE;
	}

	WCHAR szUntitled[128];
	WCHAR szAppName[128];
	GetString(uIDAppName, szAppName, COUNTOF(szAppName));
	GetString(uIDUntitled, szUntitled, COUNTOF(szUntitled));

	if (bIsElevated) {
		WCHAR szElevatedAppName[128];
		WCHAR fmt[64];
		FormatString(szElevatedAppName, fmt, IDS_APPTITLE_ELEVATED, szAppName);
		lstrcpyn(szAppName, szElevatedAppName, COUNTOF(szAppName));
	}

	WCHAR szTitle[512];
	lstrcpy(szTitle, (bModified ? pszMod : L""));

	if (StrNotEmpty(lpszExcerpt)) {
		WCHAR szExcerptQuote[256];
		WCHAR szExcerptFmt[32];
		FormatString(szExcerptQuote, szExcerptFmt, IDS_TITLEEXCERPT, lpszExcerpt);
		lstrcat(szTitle, szExcerptQuote);
	} else if (StrNotEmpty(lpszFile)) {
		if (iFormat < 2 && !PathIsRoot(lpszFile)) {
			if (!StrCaseEqual(szCachedFile, lpszFile)) {
				SHFILEINFO shfi;
				lstrcpy(szCachedFile, lpszFile);
				if (SHGetFileInfo2(lpszFile, 0, &shfi, sizeof(SHFILEINFO), SHGFI_DISPLAYNAME)) {
					lstrcpy(szCachedDisplayName, shfi.szDisplayName);
				} else {
					lstrcpy(szCachedDisplayName, PathFindFileName(lpszFile));
				}
			}
			lstrcat(szTitle, szCachedDisplayName);
			if (iFormat == 1) {
				WCHAR tchPath[MAX_PATH];
				lstrcpyn(tchPath, lpszFile, COUNTOF(tchPath));
				PathRemoveFileSpec(tchPath);
				lstrcat(szTitle, L" [");
				lstrcat(szTitle, tchPath);
				lstrcat(szTitle, L"]");
			}
		} else {
			lstrcat(szTitle, lpszFile);
		}
	} else {
		lstrcpy(szCachedFile, L"");
		lstrcpy(szCachedDisplayName, L"");
		lstrcat(szTitle, szUntitled);
	}

	if (bReadOnly) {
		WCHAR szReadOnly[32];
		GetString(uIDReadOnly, szReadOnly, COUNTOF(szReadOnly));
		lstrcat(szTitle, L" ");
		lstrcat(szTitle, szReadOnly);
	}
	if (bLocked) {
		WCHAR szLocked[32];
		GetString(uIDLocked, szLocked, COUNTOF(szLocked));
		lstrcat(szTitle, L" ");
		lstrcat(szTitle, szLocked);
	}

	lstrcat(szTitle, pszSep);
	lstrcat(szTitle, szAppName);

	return SetWindowText(hwnd, szTitle);
}

//=============================================================================
//
// SetWindowTransparentMode()
//
void SetWindowTransparentMode(HWND hwnd, BOOL bTransparentMode, int iOpacityLevel) {
	// https://docs.microsoft.com/en-us/windows/win32/winmsg/using-windows#using-layered-windows
	DWORD exStyle = GetWindowExStyle(hwnd);
	exStyle = bTransparentMode ? (exStyle | WS_EX_LAYERED) : (exStyle & ~WS_EX_LAYERED);
	SetWindowExStyle(hwnd, exStyle);
	if (bTransparentMode) {
		const BYTE bAlpha = (BYTE)(iOpacityLevel * 255 / 100);
		SetLayeredWindowAttributes(hwnd, 0, bAlpha, LWA_ALPHA);
	}
	// Ask the window and its children to repaint
	RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void SetWindowLayoutRTL(HWND hwnd, BOOL bRTL) {
	// https://docs.microsoft.com/en-us/windows/win32/winmsg/window-features#window-layout-and-mirroring
	DWORD exStyle = GetWindowExStyle(hwnd);
	exStyle = bRTL ? (exStyle | WS_EX_LAYOUTRTL) : (exStyle & ~WS_EX_LAYOUTRTL);
	SetWindowExStyle(hwnd, exStyle);
	// update layout in the client area
	InvalidateRect(hwnd, NULL, TRUE);
}

//=============================================================================
//
// CenterDlgInParentEx()
//
void CenterDlgInParentEx(HWND hDlg, HWND hParent) {
	RECT rcDlg;
	RECT rcParent;

	GetWindowRect(hDlg, &rcDlg);
	GetWindowRect(hParent, &rcParent);

	HMONITOR hMonitor = MonitorFromRect(&rcParent, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(hMonitor, &mi);

	const int xMin = mi.rcWork.left;
	const int yMin = mi.rcWork.top;

	const int xMax = (mi.rcWork.right) - (rcDlg.right - rcDlg.left);
	const int yMax = (mi.rcWork.bottom) - (rcDlg.bottom - rcDlg.top);

	int x;
	if ((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left) > 20) {
		x = rcParent.left + (((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left)) / 2);
	} else {
		x = rcParent.left + 70;
	}

	int y;
	if ((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top) > 20) {
		y = rcParent.top + (((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top)) / 2);
	} else {
		y = rcParent.top + 60;
	}

	SetWindowPos(hDlg, NULL, clamp_i(x, xMin, xMax), clamp_i(y, yMin, yMax), 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

void SetToRightBottomEx(HWND hDlg, HWND hParent) {
	RECT rcDlg;
	RECT rcParent;
	RECT rcClient;

	GetWindowRect(hDlg, &rcDlg);
	GetWindowRect(hParent, &rcParent);
	GetClientRect(hParent, &rcClient);

	const int width = (rcDlg.right - rcDlg.left) + ((rcParent.right - rcParent.left) - (rcClient.right - rcClient.left)) / 2;
	const int height = (rcDlg.bottom - rcDlg.top) + ((rcParent.bottom - rcParent.top) - (rcClient.bottom - rcClient.top)) / 2;
	const int x = rcParent.right - width;
	const int y = rcParent.bottom - height;
	SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

// Why doesn’t the "Automatically move pointer to the default button in a dialog box"
// work for nonstandard dialog boxes, and how do I add it to my own nonstandard dialog boxes?
// https://blogs.msdn.microsoft.com/oldnewthing/20130826-00/?p=3413/
void SnapToDefaultButton(HWND hwndBox) {
	BOOL fSnapToDefButton = FALSE;
	if (SystemParametersInfo(SPI_GETSNAPTODEFBUTTON, 0, &fSnapToDefButton, 0) && fSnapToDefButton) {
		// get child window at the top of the Z order.
		// for all our MessageBoxs it's the OK or YES button or NULL.
		HWND btn = GetWindow(hwndBox, GW_CHILD);
		if (btn != NULL) {
			WCHAR className[8] = L"";
			GetClassName(btn, className, COUNTOF(className));
			if (StrCaseEqual(className, L"Button")) {
				RECT rect;
				GetWindowRect(btn, &rect);
				const int x = rect.left + (rect.right - rect.left) / 2;
				const int y = rect.top + (rect.bottom - rect.top) / 2;
				SetCursorPos(x, y);
			}
		}
	}
}

//=============================================================================
//
// GetDlgPos()
//
void GetDlgPos(HWND hDlg, LPINT xDlg, LPINT yDlg) {
	RECT rcDlg;
	GetWindowRect(hDlg, &rcDlg);

	HWND hParent = GetParent(hDlg);
	RECT rcParent;
	GetWindowRect(hParent, &rcParent);

	// return positions relative to parent window
	*xDlg = rcDlg.left - rcParent.left;
	*yDlg = rcDlg.top - rcParent.top;
}

//=============================================================================
//
// SetDlgPos()
//
void SetDlgPos(HWND hDlg, int xDlg, int yDlg) {
	RECT rcDlg;
	GetWindowRect(hDlg, &rcDlg);

	HWND hParent = GetParent(hDlg);
	RECT rcParent;
	GetWindowRect(hParent, &rcParent);

	HMONITOR hMonitor = MonitorFromRect(&rcParent, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(hMonitor, &mi);

	const int xMin = mi.rcWork.left;
	const int yMin = mi.rcWork.top;

	const int xMax = (mi.rcWork.right) - (rcDlg.right - rcDlg.left);
	const int yMax = (mi.rcWork.bottom) - (rcDlg.bottom - rcDlg.top);

	// desired positions relative to parent window
	const int x = rcParent.left + xDlg;
	const int y = rcParent.top + yDlg;

	SetWindowPos(hDlg, NULL, clamp_i(x, xMin, xMax), clamp_i(y, yMin, yMax), 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

//=============================================================================
//
// Resize Dialog Helpers()
//
#define RESIZEDLG_PROP_KEY	L"ResizeDlg"
#define MAX_RESIZEDLG_ATTR_COUNT	2
// temporary fix for moving dialog to monitor with different DPI
// TODO: all dimensions no longer valid after window DPI changed.
#define NP2_ENABLE_RESIZEDLG_TEMP_FIX	0

typedef struct RESIZEDLG {
	int direction;
	UINT dpi;
	int cxClient;
	int cyClient;
	int mmiPtMinX;
	int mmiPtMinY;
	int mmiPtMaxX;	// only Y direction
	int mmiPtMaxY;	// only X direction
	int attrs[MAX_RESIZEDLG_ATTR_COUNT];
} RESIZEDLG, *PRESIZEDLG;

typedef const RESIZEDLG * LPCRESIZEDLG;

void ResizeDlg_InitEx(HWND hwnd, int cxFrame, int cyFrame, int nIdGrip, int iDirection) {
	const UINT dpi = GetWindowDPI(hwnd);
	RESIZEDLG *pm = (RESIZEDLG *)NP2HeapAlloc(sizeof(RESIZEDLG));
	pm->direction = iDirection;
	pm->dpi = dpi;

	RECT rc;
	GetClientRect(hwnd, &rc);
	pm->cxClient = rc.right - rc.left;
	pm->cyClient = rc.bottom - rc.top;

	const DWORD style = GetWindowStyle(hwnd) | WS_THICKFRAME;
	AdjustWindowRectForDpi(&rc, style, 0, dpi);
	pm->mmiPtMinX = rc.right - rc.left;
	pm->mmiPtMinY = rc.bottom - rc.top;
	// only one direction
	switch (iDirection) {
	case ResizeDlgDirection_OnlyX:
		pm->mmiPtMaxY = pm->mmiPtMinY;
		break;

	case ResizeDlgDirection_OnlyY:
		pm->mmiPtMaxX = pm->mmiPtMinX;
		break;
	}

	cxFrame = max_i(cxFrame, pm->mmiPtMinX);
	cyFrame = max_i(cyFrame, pm->mmiPtMinY);

	SetProp(hwnd, RESIZEDLG_PROP_KEY, (HANDLE)pm);

	SetWindowPos(hwnd, NULL, rc.left, rc.top, cxFrame, cyFrame, SWP_NOZORDER);

	SetWindowStyle(hwnd, style);
	SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

	WCHAR wch[64];
	GetMenuString(GetSystemMenu(GetParent(hwnd), FALSE), SC_SIZE, wch, COUNTOF(wch), MF_BYCOMMAND);
	InsertMenu(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_STRING | MF_ENABLED, SC_SIZE, wch);
	InsertMenu(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_SEPARATOR, 0, NULL);

	HWND hwndCtl = GetDlgItem(hwnd, nIdGrip);
	SetWindowStyle(hwndCtl, GetWindowStyle(hwndCtl) | SBS_SIZEGRIP | WS_CLIPSIBLINGS);
	const int cGrip = SystemMetricsForDpi(SM_CXHTHUMB, dpi);
	SetWindowPos(hwndCtl, NULL, pm->cxClient - cGrip, pm->cyClient - cGrip, cGrip, cGrip, SWP_NOZORDER);
}

void ResizeDlg_Destroy(HWND hwnd, int *cxFrame, int *cyFrame) {
	PRESIZEDLG pm = (PRESIZEDLG)GetProp(hwnd, RESIZEDLG_PROP_KEY);

	RECT rc;
	GetWindowRect(hwnd, &rc);
	if (cxFrame) {
		*cxFrame = rc.right - rc.left;
	}
	if (cyFrame) {
		*cyFrame = rc.bottom - rc.top;
	}

	RemoveProp(hwnd, RESIZEDLG_PROP_KEY);
	NP2HeapFree(pm);
}

void ResizeDlg_Size(HWND hwnd, LPARAM lParam, int *cx, int *cy) {
	PRESIZEDLG pm = (PRESIZEDLG)GetProp(hwnd, RESIZEDLG_PROP_KEY);
	const int cxClient = LOWORD(lParam);
	const int cyClient = HIWORD(lParam);
#if NP2_ENABLE_RESIZEDLG_TEMP_FIX
	const UINT dpi = GetWindowDPI(hwnd);
	const UINT old = pm->dpi;
	if (cx) {
		*cx = cxClient - MulDiv(pm->cxClient, dpi, old);
	}
	if (cy) {
		*cy = cyClient - MulDiv(pm->cyClient, dpi, old);
	}
	// store in original DPI.
	pm->cxClient = MulDiv(cxClient, old, dpi);
	pm->cyClient = MulDiv(cyClient, old, dpi);
#else
	if (cx) {
		*cx = cxClient - pm->cxClient;
	}
	if (cy) {
		*cy = cyClient - pm->cyClient;
	}
	pm->cxClient = cxClient;
	pm->cyClient = cyClient;
#endif
}

void ResizeDlg_GetMinMaxInfo(HWND hwnd, LPARAM lParam) {
	LPCRESIZEDLG pm = (LPCRESIZEDLG)GetProp(hwnd, RESIZEDLG_PROP_KEY);
	LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;
#if NP2_ENABLE_RESIZEDLG_TEMP_FIX
	const UINT dpi = GetWindowDPI(hwnd);
	const UINT old = pm->dpi;

	lpmmi->ptMinTrackSize.x = MulDiv(pm->mmiPtMinX, dpi, old);
	lpmmi->ptMinTrackSize.y = MulDiv(pm->mmiPtMinY, dpi, old);

	// only one direction
	switch (pm->direction) {
	case ResizeDlgDirection_OnlyX:
		lpmmi->ptMaxTrackSize.y = MulDiv(pm->mmiPtMaxY, dpi, old);
		break;

	case ResizeDlgDirection_OnlyY:
		lpmmi->ptMaxTrackSize.x = MulDiv(pm->mmiPtMaxX, dpi, old);
		break;
	}
#else
	lpmmi->ptMinTrackSize.x = pm->mmiPtMinX;
	lpmmi->ptMinTrackSize.y = pm->mmiPtMinY;

	// only one direction
	switch (pm->direction) {
	case ResizeDlgDirection_OnlyX:
		lpmmi->ptMaxTrackSize.y = pm->mmiPtMaxY;
		break;

	case ResizeDlgDirection_OnlyY:
		lpmmi->ptMaxTrackSize.x = pm->mmiPtMaxX;
		break;
	}
#endif
}

static inline int GetDlgCtlHeight(HWND hwndDlg, int nCtlId) {
	RECT rc;
	GetWindowRect(GetDlgItem(hwndDlg, nCtlId), &rc);
	const int height = rc.bottom - rc.top;
	return height;
}

void ResizeDlg_InitY2Ex(HWND hwnd, int cxFrame, int cyFrame, int nIdGrip, int iDirection, int nCtlId1, int nCtlId2) {
	const int hMin1 = GetDlgCtlHeight(hwnd, nCtlId1);
	const int hMin2 = GetDlgCtlHeight(hwnd, nCtlId2);
	ResizeDlg_InitEx(hwnd, cxFrame, cyFrame, nIdGrip, iDirection);
	PRESIZEDLG pm = (PRESIZEDLG)GetProp(hwnd, RESIZEDLG_PROP_KEY);
	pm->attrs[0] = hMin1;
	pm->attrs[1] = hMin2;
}

int ResizeDlg_CalcDeltaY2(HWND hwnd, int dy, int cy, int nCtlId1, int nCtlId2) {
	if (dy == 0) {
		return 0;
	}
	if (dy > 0) {
		return MulDiv(dy, cy, 100);
	}

	const LPCRESIZEDLG pm = (LPCRESIZEDLG)GetProp(hwnd, RESIZEDLG_PROP_KEY);
#if NP2_ENABLE_RESIZEDLG_TEMP_FIX
	const UINT dpi = GetWindowDPI(hwnd);
	const int hMin1 = MulDiv(pm->attrs[0], dpi, pm->dpi);
	const int hMin2 = MulDiv(pm->attrs[1], dpi, pm->dpi);
#else
	const int hMin1 = pm->attrs[0];
	const int hMin2 = pm->attrs[1];
#endif
	const int h1 = GetDlgCtlHeight(hwnd, nCtlId1);
	const int h2 = GetDlgCtlHeight(hwnd, nCtlId2);
	// cy + h1 >= hMin1			cy >= hMin1 - h1
	// dy - cy + h2 >= hMin2	cy <= dy + h2 - hMin2
	const int cyMin = hMin1 - h1;
	const int cyMax = dy + h2 - hMin2;
	cy = dy - MulDiv(dy, 100 - cy, 100);
	cy = clamp_i(cy, cyMin, cyMax);
	return cy;
}

HDWP DeferCtlPos(HDWP hdwp, HWND hwndDlg, int nCtlId, int dx, int dy, UINT uFlags) {
	HWND hwndCtl = GetDlgItem(hwndDlg, nCtlId);
	RECT rc;
	GetWindowRect(hwndCtl, &rc);
	MapWindowPoints(NULL, hwndDlg, (LPPOINT)&rc, 2);
	if (uFlags & SWP_NOSIZE) {
		return DeferWindowPos(hdwp, hwndCtl, NULL, rc.left + dx, rc.top + dy, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}
	return DeferWindowPos(hdwp, hwndCtl, NULL, 0, 0, rc.right - rc.left + dx, rc.bottom - rc.top + dy, SWP_NOZORDER | SWP_NOMOVE);
}

void ResizeDlgCtl(HWND hwndDlg, int nCtlId, int dx, int dy) {
	HWND hwndCtl = GetDlgItem(hwndDlg, nCtlId);
	RECT rc;
	GetWindowRect(hwndCtl, &rc);
	MapWindowPoints(NULL, hwndDlg, (LPPOINT)&rc, 2);
	SetWindowPos(hwndCtl, NULL, 0, 0, rc.right - rc.left + dx, rc.bottom - rc.top + dy, SWP_NOZORDER | SWP_NOMOVE);
	InvalidateRect(hwndCtl, NULL, TRUE);
}

// https://docs.microsoft.com/en-us/windows/desktop/Controls/subclassing-overview
// https://support.microsoft.com/en-us/help/102589/how-to-use-the-enter-key-from-edit-controls-in-a-dialog-box
// Ctrl+A: https://stackoverflow.com/questions/10127054/select-all-text-in-edit-contol-by-clicking-ctrla
static LRESULT CALLBACK MultilineEditProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
	UNREFERENCED_PARAMETER(dwRefData);

	switch (umsg) {
	case WM_GETDLGCODE:
		if (GetWindowStyle(hwnd) & ES_WANTRETURN) {
			return DLGC_WANTALLKEYS | DLGC_HASSETSEL;
		}
		break;

	case WM_CHAR:
		if (wParam == 1) { // Ctrl+A
			Edit_SetSel(hwnd, 0, -1);
			return TRUE;
		}
		break;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			SendMessage(GetParent(hwnd), WM_CLOSE, 0, 0);
			return TRUE;
		}
		if (wParam == VK_TAB && KeyboardIsKeyDown(VK_SHIFT)) {
			// normally focus on previous control that has the WS_TABSTOP style set.
			// focus on next control when the ES_WANTRETURN style is set (acts as normal Tab key).
			const BOOL previous = (GetWindowStyle(hwnd) & ES_WANTRETURN) == 0;
			HWND hwndParent = GetParent(hwnd);
			HWND hwndCtl = GetNextDlgTabItem(hwndParent, hwnd, previous);
			if (hwndCtl == hwnd) {
				hwndCtl = GetNextDlgTabItem(hwndParent, hwnd, !previous);
			}
			// TODO: find first control when hwnd is last tab item on this dialog.
			if (hwndCtl != hwnd) {
				PostMessage(hwndParent, WM_NEXTDLGCTL, (WPARAM)hwndCtl, TRUE);
			}
		}
		break;

	case WM_SETTEXT: {
		const LRESULT result = DefSubclassProc(hwnd, umsg, wParam, lParam);
		if (result) {
			NotifyEditTextChanged(GetParent(hwnd), GetDlgCtrlID(hwnd));
		}
		return result;
	}

	case WM_NCDESTROY:
		// Safer subclassing
		// https://devblogs.microsoft.com/oldnewthing/20031111-00/?p=41883
		RemoveWindowSubclass(hwnd, MultilineEditProc, uIdSubclass);
		break;
	}

	return DefSubclassProc(hwnd, umsg, wParam, lParam);
}

void MultilineEditSetup(HWND hwndDlg, int nCtlId) {
	SetWindowSubclass(GetDlgItem(hwndDlg, nCtlId), MultilineEditProc, 0, 0);
}

//=============================================================================
//
// MakeBitmapButton()
//
void MakeBitmapButton(HWND hwnd, int nCtlId, HINSTANCE hInstance, WORD wBmpId) {
	HWND hwndCtl = GetDlgItem(hwnd, nCtlId);
	HBITMAP hBmp = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(wBmpId), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
	hBmp = ResizeButtonImageForCurrentDPI(hBmp);
	BITMAP bmp;
	GetObject(hBmp, sizeof(BITMAP), &bmp);
	BUTTON_IMAGELIST bi;
	bi.himl = ImageList_Create(bmp.bmWidth, bmp.bmHeight, ILC_COLOR32 | ILC_MASK, 1, 0);
	ImageList_AddMasked(bi.himl, hBmp, CLR_DEFAULT);
	DeleteObject(hBmp);
	SetRect(&bi.margin, 0, 0, 0, 0);
	bi.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
	Button_SetImageList(hwndCtl, &bi);
}

//=============================================================================
//
// MakeColorPickButton()
//
void MakeColorPickButton(HWND hwnd, int nCtlId, HINSTANCE hInstance, COLORREF crColor) {
	HWND hwndCtl = GetDlgItem(hwnd, nCtlId);
	BUTTON_IMAGELIST bi;
	HIMAGELIST himlOld = NULL;
	COLORMAP colormap[2];

	if (Button_GetImageList(hwndCtl, &bi)) {
		himlOld = bi.himl;
	}

	if (IsWindowEnabled(hwndCtl) && crColor != (COLORREF)(-1)) {
		colormap[0].from = RGB(0x00, 0x00, 0x00);
		colormap[0].to	 = GetSysColor(COLOR_3DSHADOW);
	} else {
		colormap[0].from = RGB(0x00, 0x00, 0x00);
		colormap[0].to	 = RGB(0xFF, 0xFF, 0xFF);
	}

	if (IsWindowEnabled(hwndCtl) && crColor != (COLORREF)(-1)) {
		if (crColor == RGB(0xFF, 0xFF, 0xFF)) {
			crColor = RGB(0xFF, 0xFF, 0xFE);
		}

		colormap[1].from = RGB(0xFF, 0xFF, 0xFF);
		colormap[1].to	 = crColor;
	} else {
		colormap[1].from = RGB(0xFF, 0xFF, 0xFF);
		colormap[1].to	 = RGB(0xFF, 0xFF, 0xFF);
	}

	HBITMAP hBmp = CreateMappedBitmap(hInstance, IDB_PICK, 0, colormap, 2);

	bi.himl = ImageList_Create(16, 16, ILC_COLORDDB | ILC_MASK, 1, 0);
	ImageList_AddMasked(bi.himl, hBmp, RGB(0xFF, 0xFF, 0xFF));
	DeleteObject(hBmp);

	SetRect(&bi.margin, 0, 0, 2, 0);
	bi.uAlign = BUTTON_IMAGELIST_ALIGN_RIGHT;

	Button_SetImageList(hwndCtl, &bi);
	InvalidateRect(hwndCtl, NULL, TRUE);

	if (himlOld) {
		ImageList_Destroy(himlOld);
	}
}

//=============================================================================
//
// DeleteBitmapButton()
//
void DeleteBitmapButton(HWND hwnd, int nCtlId) {
	HWND hwndCtl = GetDlgItem(hwnd, nCtlId);
	BUTTON_IMAGELIST bi;
	if (Button_GetImageList(hwndCtl, &bi)) {
		ImageList_Destroy(bi.himl);
	}
}

//=============================================================================
//
// SendWMSize()
//
LRESULT SendWMSize(HWND hwnd) {
	RECT rc;
	GetClientRect(hwnd, &rc);
	return SendMessage(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
}

//=============================================================================
//
// StatusSetTextID()
//
BOOL StatusSetTextID(HWND hwnd, UINT nPart, UINT uID) {
	WCHAR szText[256];
	GetString(uID, szText, COUNTOF(szText));
	return (BOOL)SendMessage(hwnd, SB_SETTEXT, nPart, (LPARAM)szText);
}

//=============================================================================
//
// StatusCalcPaneWidth()
//
int StatusCalcPaneWidth(HWND hwnd, LPCWSTR lpsz) {
	HDC hdc = GetDC(hwnd);
	HFONT hfont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
	HFONT hfold = (HFONT)SelectObject(hdc, hfont);
	const int mmode = SetMapMode(hdc, MM_TEXT);

	SIZE size;
	GetTextExtentPoint32(hdc, lpsz, lstrlen(lpsz), &size);

	SetMapMode(hdc, mmode);
	SelectObject(hdc, hfold);
	ReleaseDC(hwnd, hdc);

	return size.cx + 9;
}

//=============================================================================
//
// Toolbar_Get/SetButtons()
//
int Toolbar_GetButtons(HWND hwnd, int cmdBase, LPWSTR lpszButtons, int cchButtons) {
	const int count = min_i(MAX_TOOLBAR_ITEM_COUNT_WITH_SEPARATOR, (int)SendMessage(hwnd, TB_BUTTONCOUNT, 0, 0));
	const int maxCch = cchButtons - 3; // two digits, one space and NULL
	int len = 0;

	for (int i = 0; i < count && len < maxCch; i++) {
		TBBUTTON tbb;
		SendMessage(hwnd, TB_GETBUTTON, i, (LPARAM)&tbb);
		const int iCmd = (tbb.idCommand == 0) ? 0 : tbb.idCommand - cmdBase + 1;
		len += wsprintf(lpszButtons + len, L"%i ", iCmd);
	}

	lpszButtons[len--] = L'\0';
	if (len >= 0) {
		lpszButtons[len] = L'\0';
	}
	return count;
}

int Toolbar_SetButtons(HWND hwnd, LPCWSTR lpszButtons, LPCTBBUTTON ptbb, int ctbb) {
	int count = (int)SendMessage(hwnd, TB_BUTTONCOUNT, 0, 0);
	if (StrIsEmpty(lpszButtons)) {
		return count;
	}

	while (count) {
		SendMessage(hwnd, TB_DELETEBUTTON, 0, 0);
		--count;
	}

	LPCWSTR p = lpszButtons;
	--ctbb;
	while (TRUE) {
		LPWSTR end;
		int iCmd = (int)wcstol(p, &end, 10);
		if (p != end) {
			iCmd = clamp_i(iCmd, 0, ctbb);
			SendMessage(hwnd, TB_ADDBUTTONS, 1, (LPARAM)&ptbb[iCmd]);
			p = end;
			++count;
			//if (count == MAX_TOOLBAR_ITEM_COUNT_WITH_SEPARATOR) {
			//	break;
			//}
		} else {
			break;
		}
	}

	return count;
}

//=============================================================================
//
// IsCmdEnabled()
//
BOOL IsCmdEnabled(HWND hwnd, UINT uId) {
	HMENU hmenu = GetMenu(hwnd);
	SendMessage(hwnd, WM_INITMENU, (WPARAM)hmenu, 0);
	const UINT ustate = GetMenuState(hmenu, uId, MF_BYCOMMAND);

	if (ustate == 0xFFFFFFFF) {
		return TRUE;
	}
	return !(ustate & (MF_GRAYED | MF_DISABLED));
}

INT GetCheckedRadioButton(HWND hwnd, int nIDFirstButton, int nIDLastButton) {
	for (int i = nIDFirstButton; i <= nIDLastButton; i++) {
		if (IsButtonChecked(hwnd, i)) {
			return i;
		}
	}
	return -1; // IDC_STATIC;
}

#if NP2_ENABLE_APP_LOCALIZATION_DLL
HMODULE LoadLocalizedResourceDLL(LANGID lang, LPCWSTR dllName) {
	if (lang == LANG_USER_DEFAULT) {
		lang = GetUserDefaultUILanguage();
	}

	LPCWSTR folder = NULL;
	const LANGID subLang = SUBLANGID(lang);
	switch (PRIMARYLANGID(lang)) {
	case LANG_ENGLISH:
		break;
	case LANG_CHINESE:
		folder = IsChineseTraditionalSubLang(subLang) ? L"zh-Hant" : L"zh-Hans";
		break;
	case LANG_GERMAN:
		folder = L"de";
		break;
	case LANG_ITALIAN:
		folder = L"it";
		break;
	case LANG_JAPANESE:
		folder = L"ja";
		break;
	case LANG_KOREAN:
		folder = L"ko";
		break;
	}

	if (folder == NULL) {
		return NULL;
	}

	WCHAR path[MAX_PATH];
	GetModuleFileName(NULL, path, COUNTOF(path));
	PathRemoveFileSpec(path);
	PathAppend(path, L"locale");
	PathAppend(path, folder);
	PathAppend(path, dllName);

	const DWORD flags = IsVistaAndAbove() ? (LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE) : LOAD_LIBRARY_AS_DATAFILE;
	HMODULE hDLL = LoadLibraryEx(path, NULL, flags);
	return hDLL;
}

#if NP2_ENABLE_TEST_LOCALIZATION_LAYOUT
void GetLocaleDefaultUIFont(LANGID lang, LPWSTR lpFaceName, WORD *wSize) {
	LPCWSTR font;
	const LANGID subLang = SUBLANGID(lang);
	switch (PRIMARYLANGID(lang)) {
	default:
	case LANG_ENGLISH:
		// https://docs.microsoft.com/en-us/typography/font-list/segoe-ui
		font = L"Segoe UI";
		break;
	case LANG_CHINESE:
		// https://docs.microsoft.com/en-us/typography/font-list/microsoft-yahei
		// https://docs.microsoft.com/en-us/typography/font-list/microsoft-jhenghei
		font = IsChineseTraditionalSubLang(subLang) ? L"Microsoft JhengHei UI" : L"Microsoft YaHei UI";
		break;
	case LANG_JAPANESE:
		// https://docs.microsoft.com/en-us/typography/font-list/meiryo
		font = L"Meiryo UI";
		break;
	case LANG_KOREAN:
		// https://docs.microsoft.com/en-us/typography/font-list/malgun-gothic
		font = L"Malgun Gothic";
		break;
	}
	*wSize = 9;
	lstrcpy(lpFaceName, font);
}
#endif
#endif

//=============================================================================
//
// PathRelativeToApp()
//
void PathRelativeToApp(LPCWSTR lpszSrc, LPWSTR lpszDest, int cchDest,
					   BOOL bSrcIsFile, BOOL bUnexpandEnv, BOOL bUnexpandMyDocs) {
	WCHAR wchAppPath[MAX_PATH];
	WCHAR wchWinDir[MAX_PATH];
	WCHAR wchUserFiles[MAX_PATH];
	WCHAR wchPath[MAX_PATH];
	const DWORD dwAttrTo = bSrcIsFile ? 0 : FILE_ATTRIBUTE_DIRECTORY;

	GetModuleFileName(NULL, wchAppPath, COUNTOF(wchAppPath));
	PathRemoveFileSpec(wchAppPath);
	GetWindowsDirectory(wchWinDir, COUNTOF(wchWinDir));
#if _WIN32_WINNT < _WIN32_WINNT_VISTA
	if (S_OK != SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, wchUserFiles)) {
		return;
	}
#else
	LPWSTR pszPath = NULL;
	if (S_OK != SHGetKnownFolderPath(&FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &pszPath)) {
		return;
	}
	lstrcpy(wchUserFiles, pszPath);
	CoTaskMemFree(pszPath);
#endif

	if (bUnexpandMyDocs &&
			!PathIsRelative(lpszSrc) &&
			!PathIsPrefix(wchUserFiles, wchAppPath) &&
			PathIsPrefix(wchUserFiles, lpszSrc) &&
			PathRelativePathTo(wchPath, wchUserFiles, FILE_ATTRIBUTE_DIRECTORY, lpszSrc, dwAttrTo)) {
		lstrcpy(wchUserFiles, L"%CSIDL:MYDOCUMENTS%");
		PathAppend(wchUserFiles, wchPath);
		lstrcpy(wchPath, wchUserFiles);
	} else if (PathIsRelative(lpszSrc) || PathCommonPrefix(wchAppPath, wchWinDir, NULL)) {
		lstrcpyn(wchPath, lpszSrc, COUNTOF(wchPath));
	} else {
		if (!PathRelativePathTo(wchPath, wchAppPath, FILE_ATTRIBUTE_DIRECTORY, lpszSrc, dwAttrTo)) {
			lstrcpyn(wchPath, lpszSrc, COUNTOF(wchPath));
		}
	}

	WCHAR wchResult[MAX_PATH];
	if (bUnexpandEnv) {
		if (!PathUnExpandEnvStrings(wchPath, wchResult, COUNTOF(wchResult))) {
			lstrcpyn(wchResult, wchPath, COUNTOF(wchResult));
		}
	} else {
		lstrcpyn(wchResult, wchPath, COUNTOF(wchResult));
	}

	lstrcpyn(lpszDest, wchResult, (cchDest == 0) ? MAX_PATH : cchDest);
}

//=============================================================================
//
// PathAbsoluteFromApp()
//
void PathAbsoluteFromApp(LPCWSTR lpszSrc, LPWSTR lpszDest, int cchDest, BOOL bExpandEnv) {
	WCHAR wchPath[MAX_PATH];

	if (StrHasPrefix(lpszSrc, L"%CSIDL:MYDOCUMENTS%")) {
#if _WIN32_WINNT < _WIN32_WINNT_VISTA
		if (S_OK != SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, wchPath)) {
			return;
		}
#else
		LPWSTR pszPath = NULL;
		if (S_OK != SHGetKnownFolderPath(&FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &pszPath)) {
			return;
		}
		lstrcpy(wchPath, pszPath);
		CoTaskMemFree(pszPath);
#endif
		PathAppend(wchPath, lpszSrc + CSTRLEN("%CSIDL:MYDOCUMENTS%"));
	} else {
		lstrcpyn(wchPath, lpszSrc, COUNTOF(wchPath));
	}

	if (bExpandEnv) {
		ExpandEnvironmentStringsEx(wchPath, COUNTOF(wchPath));
	}

	WCHAR wchResult[MAX_PATH];
	if (PathIsRelative(wchPath)) {
		GetModuleFileName(NULL, wchResult, COUNTOF(wchResult));
		PathRemoveFileSpec(wchResult);
		PathAppend(wchResult, wchPath);
	} else {
		lstrcpyn(wchResult, wchPath, COUNTOF(wchResult));
	}

	PathCanonicalizeEx(wchResult);
	if (PathGetDriveNumber(wchResult) != -1) {
		CharUpperBuff(wchResult, 1);
	}

	lstrcpyn(lpszDest, wchResult, (cchDest == 0) ? MAX_PATH : cchDest);
}

///////////////////////////////////////////////////////////////////////////////
//
//
// Name: PathIsLnkFile()
//
// Purpose: Determine wheter pszPath is a Windows Shell Link File by
// comparing the filename extension with L".lnk"
//
// Manipulates:
//
BOOL PathIsLnkFile(LPCWSTR pszPath) {
	if (!pszPath || !*pszPath) {
		return FALSE;
	}

	/*
	LPCWSTR pszExt = StrRChr(pszPath, NULL, L'.');
	if (!pszExt) {
		return FALSE;
	}
	if (StrCaseEqual(pszExt, L".lnk")) {
		return TRUE;
	}
	return FALSE;

	if (StrCaseEqual(PathFindExtension(pszPath), L".lnk")) {
		return TRUE;
	}
	return FALSE;
	*/

	if (!StrCaseEqual(PathFindExtension(pszPath), L".lnk")) {
		return FALSE;
	}

	WCHAR tchResPath[MAX_PATH];
	return PathGetLnkPath(pszPath, tchResPath, COUNTOF(tchResPath));
}

///////////////////////////////////////////////////////////////////////////////
//
//
// Name: PathGetLnkPath()
//
// Purpose: Try to get the path to which a lnk-file is linked
//
//
// Manipulates: pszResPath
//
BOOL PathGetLnkPath(LPCWSTR pszLnkFile, LPWSTR pszResPath, int cchResPath) {
	IShellLink *psl;
	BOOL bSucceeded = FALSE;

#if defined(__cplusplus)
	if (SUCCEEDED(CoCreateInstance(IID_IShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)(&psl)))) {
		IPersistFile *ppf;

		if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void **)(&ppf)))) {
			WCHAR wsz[MAX_PATH];
			lstrcpy(wsz, pszLnkFile);

			if (SUCCEEDED(ppf->Load(wsz, STGM_READ))) {
				WIN32_FIND_DATA fd;
				if (S_OK == psl->GetPath(pszResPath, cchResPath, &fd, 0)) {
					// This additional check seems reasonable
					bSucceeded = StrNotEmpty(pszResPath);
				}
			}
			ppf->Release();
		}
		psl->Release();
	}
#else
	if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID *)(&psl)))) {
		IPersistFile *ppf;

		if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void **)(&ppf)))) {
			WCHAR wsz[MAX_PATH];
			lstrcpy(wsz, pszLnkFile);

			if (SUCCEEDED(ppf->lpVtbl->Load(ppf, wsz, STGM_READ))) {
				WIN32_FIND_DATA fd;
				if (S_OK == psl->lpVtbl->GetPath(psl, pszResPath, cchResPath, &fd, 0)) {
					// This additional check seems reasonable
					bSucceeded = StrNotEmpty(pszResPath);
				}
			}
			ppf->lpVtbl->Release(ppf);
		}
		psl->lpVtbl->Release(psl);
	}
#endif

	if (bSucceeded) {
		ExpandEnvironmentStringsEx(pszResPath, cchResPath);
		PathCanonicalizeEx(pszResPath);
	}

	return bSucceeded;
}

///////////////////////////////////////////////////////////////////////////////
//
//
// Name: PathIsLnkToDirectory()
//
// Purpose: Determine wheter pszPath is a Windows Shell Link File which
// refers to a directory
//
// Manipulates: pszResPath
//
BOOL PathIsLnkToDirectory(LPCWSTR pszPath, LPWSTR pszResPath, int cchResPath) {
	if (PathIsLnkFile(pszPath)) {
		WCHAR tchResPath[MAX_PATH];
		if (PathGetLnkPath(pszPath, tchResPath, COUNTOF(tchResPath))) {
			if (PathIsDirectory(tchResPath)) {
				lstrcpyn(pszResPath, tchResPath, cchResPath);
				return TRUE;
			}
		}
	}
	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
//
//
// Name: PathCreateDeskLnk()
//
// Purpose: Modified to create a desktop link to Notepad2
//
// Manipulates:
//
BOOL PathCreateDeskLnk(LPCWSTR pszDocument) {
	if (StrIsEmpty(pszDocument)) {
		return TRUE;
	}

	// init strings
	WCHAR tchExeFile[MAX_PATH];
	GetModuleFileName(NULL, tchExeFile, COUNTOF(tchExeFile));

	WCHAR tchDocTemp[MAX_PATH];
	lstrcpy(tchDocTemp, pszDocument);
	PathQuoteSpaces(tchDocTemp);

	WCHAR tchArguments[MAX_PATH + 16];
	lstrcpy(tchArguments, L"-n ");
	lstrcat(tchArguments, tchDocTemp);

#if _WIN32_WINNT < _WIN32_WINNT_VISTA
	WCHAR tchLinkDir[MAX_PATH];
	if (S_OK != SHGetFolderPath(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, tchLinkDir)) {
		return FALSE;
	}
#else
	LPWSTR tchLinkDir = NULL;
	if (S_OK != SHGetKnownFolderPath(&FOLDERID_Desktop, KF_FLAG_DEFAULT, NULL, &tchLinkDir)) {
		return FALSE;
	}
#endif

	WCHAR tchDescription[128];
	// TODO: read custom menu text from registry, see System Integratio.
	GetString(IDS_LINKDESCRIPTION, tchDescription, COUNTOF(tchDescription));
	//StripMnemonic(tchDescription);

	// Try to construct a valid filename...
	BOOL fMustCopy;
	WCHAR tchLnkFileName[MAX_PATH];
	if (!SHGetNewLinkInfo(pszDocument, tchLinkDir, tchLnkFileName, &fMustCopy, SHGNLI_PREFIXNAME)) {
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
		CoTaskMemFree(tchLinkDir);
#endif
		return FALSE;
	}

#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
	CoTaskMemFree(tchLinkDir);
#endif

	IShellLink *psl;
	BOOL bSucceeded = FALSE;
#if defined(__cplusplus)
	if (SUCCEEDED(CoCreateInstance(IID_IShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)(&psl)))) {
		IPersistFile *ppf;

		if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void **)(&ppf)))) {
			WCHAR wsz[MAX_PATH];
			lstrcpy(wsz, tchLnkFileName);

			psl->SetPath(tchExeFile);
			psl->SetArguments(tchArguments);
			psl->SetDescription(tchDescription);

			if (SUCCEEDED(ppf->Save(wsz, TRUE))) {
				bSucceeded = TRUE;
			}
			ppf->Release();
		}
		psl->Release();
	}
#else
	if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID *)(&psl)))) {
		IPersistFile *ppf;

		if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void **)(&ppf)))) {
			WCHAR wsz[MAX_PATH];
			lstrcpy(wsz, tchLnkFileName);

			psl->lpVtbl->SetPath(psl, tchExeFile);
			psl->lpVtbl->SetArguments(psl, tchArguments);
			psl->lpVtbl->SetDescription(psl, tchDescription);

			if (SUCCEEDED(ppf->lpVtbl->Save(ppf, wsz, TRUE))) {
				bSucceeded = TRUE;
			}

			ppf->lpVtbl->Release(ppf);
		}
		psl->lpVtbl->Release(psl);
	}
#endif

	return bSucceeded;
}

///////////////////////////////////////////////////////////////////////////////
//
//
// Name: PathCreateFavLnk()
//
// Purpose: Modified to create a Notepad2 favorites link
//
// Manipulates:
//
BOOL PathCreateFavLnk(LPCWSTR pszName, LPCWSTR pszTarget, LPCWSTR pszDir) {
	if (StrIsEmpty(pszName)) {
		return TRUE;
	}

	WCHAR tchLnkFileName[MAX_PATH];
	lstrcpy(tchLnkFileName, pszDir);
	PathAppend(tchLnkFileName, pszName);
	lstrcat(tchLnkFileName, L".lnk");

	if (PathIsFile(tchLnkFileName)) {
		return FALSE;
	}

	IShellLink *psl;
	BOOL bSucceeded = FALSE;
#if defined(__cplusplus)
	if (SUCCEEDED(CoCreateInstance(IID_IShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)(&psl)))) {
		IPersistFile *ppf;

		if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void **)(&ppf)))) {
			WCHAR wsz[MAX_PATH];
			lstrcpy(wsz, tchLnkFileName);

			psl->SetPath(pszTarget);

			if (SUCCEEDED(ppf->Save(wsz, TRUE))) {
				bSucceeded = TRUE;
			}

			ppf->Release();
		}
		psl->Release();
	}
#else
	if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID *)(&psl)))) {
		IPersistFile *ppf;

		if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void **)(&ppf)))) {
			WCHAR wsz[MAX_PATH];
			lstrcpy(wsz, tchLnkFileName);

			psl->lpVtbl->SetPath(psl, pszTarget);

			if (SUCCEEDED(ppf->lpVtbl->Save(ppf, wsz, TRUE))) {
				bSucceeded = TRUE;
			}

			ppf->lpVtbl->Release(ppf);
		}
		psl->lpVtbl->Release(psl);
	}
#endif

	return bSucceeded;
}

void OpenContainingFolder(HWND hwnd, LPCWSTR pszFile, BOOL bSelect) {
	WCHAR wchDirectory[MAX_PATH];
	lstrcpyn(wchDirectory, pszFile, COUNTOF(wchDirectory));

	LPCWSTR path = NULL;
	DWORD dwAttributes = GetFileAttributes(pszFile);
	if (bSelect || dwAttributes == INVALID_FILE_ATTRIBUTES || !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		PathRemoveFileSpec(wchDirectory);
	}
	if (bSelect && dwAttributes != INVALID_FILE_ATTRIBUTES) {
		// if pszFile is root, open the volume instead of open My Computer and select the volume
		if ((dwAttributes & FILE_ATTRIBUTE_DIRECTORY) && PathIsRoot(pszFile)) {
			bSelect = FALSE;
		} else {
			path = pszFile;
		}
	}

	dwAttributes = GetFileAttributes(wchDirectory);
	if (dwAttributes == INVALID_FILE_ATTRIBUTES || !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		return;
	}

	LPITEMIDLIST pidl = ILCreateFromPath(wchDirectory);
	if (pidl) {
		HRESULT hr;
		LPITEMIDLIST pidlEntry = path ? ILCreateFromPath(path) : NULL;
		if (pidlEntry) {
			hr = SHOpenFolderAndSelectItems(pidl, 1, (LPCITEMIDLIST *)(&pidlEntry), 0);
			CoTaskMemFree((LPVOID)pidlEntry);
		} else if (!bSelect) {
#if 0
			// Use an invalid item to open the folder?
			hr = SHOpenFolderAndSelectItems(pidl, 1, (LPCITEMIDLIST *)(&pidl), 0);
#else
			SHELLEXECUTEINFO sei;
			ZeroMemory(&sei, sizeof(SHELLEXECUTEINFO));

			sei.cbSize = sizeof(SHELLEXECUTEINFO);
			sei.fMask = SEE_MASK_IDLIST;
			sei.hwnd = hwnd;
			//sei.lpVerb = L"explore";
			sei.lpVerb = L"open";
			sei.lpIDList = (void *)pidl;
			sei.nShow = SW_SHOW;

			const BOOL result = ShellExecuteEx(&sei);
			hr = result ? S_OK : S_FALSE;
#endif
		} else {
			// open parent folder and select the folder
			hr = SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
		}
		CoTaskMemFree((LPVOID)pidl);
		if (hr == S_OK) {
			return;
		}
	}

#if 0
	if (path == NULL) {
		path = wchDirectory;
	}

	// open a new explorer window every time
	LPWSTR szParameters = (LPWSTR)NP2HeapAlloc((lstrlen(path) + 64) * sizeof(WCHAR));
	lstrcpy(szParameters, bSelect ? L"/select," : L"");
	lstrcat(szParameters, L"\"");
	lstrcat(szParameters, path);
	lstrcat(szParameters, L"\"");
	ShellExecute(hwnd, L"open", L"explorer", szParameters, NULL, SW_SHOW);
	NP2HeapFree(szParameters);
#endif
}

//=============================================================================
//
// ExtractFirstArgument()
//
BOOL ExtractFirstArgument(LPCWSTR lpArgs, LPWSTR lpArg1, LPWSTR lpArg2) {
	BOOL bQuoted = FALSE;

	lstrcpy(lpArg1, lpArgs);
	if (lpArg2) {
		*lpArg2 = L'\0';
	}

	TrimString(lpArg1);
	if (*lpArg1 == L'\0') {
		return FALSE;
	}

	LPWSTR psz = lpArg1;
	WCHAR ch = *psz;
	if (ch == L'\"') {
		*psz++ = L' ';
		bQuoted = TRUE;
	} else if (ch == L'-' || ch == L'/') {
		// fix -appid="string with space"
		++psz;
		while ((ch = *psz) != L'\0' && ch != L' ') {
			++psz;
			if (ch == L'=' && *psz == L'\"') {
				bQuoted = TRUE;
				++psz;
				break;
			}
		}
	}

	psz = StrChr(psz, (bQuoted ? L'\"' : L' '));
	if (psz) {
		*psz = L'\0';
		if (lpArg2) {
			lstrcpy(lpArg2, psz + 1);
			TrimString(lpArg2);
		}
	}

	TrimString(lpArg1);

	return TRUE;
}

//=============================================================================
//
// PrepareFilterStr()
//
void PrepareFilterStr(LPWSTR lpFilter) {
	LPWSTR psz = StrEnd(lpFilter);
	while (psz != lpFilter) {
		if (*(psz = CharPrev(lpFilter, psz)) == L'|') {
			*psz = L'\0';
		}
	}
}

//=============================================================================
//
// StrTab2Space() - in place conversion
//
void StrTab2Space(LPWSTR lpsz) {
	WCHAR *c = lpsz;
	while ((c = StrChr(c, L'\t')) != NULL) {
		*c++ = L' ';
	}
}

//=============================================================================
//
// PathFixBackslashes() - in place conversion
//
void PathFixBackslashes(LPWSTR lpsz) {
	WCHAR *c = lpsz;
	while ((c = StrChr(c, L'/')) != NULL) {
		if (*CharPrev(lpsz, c) == L':' && *CharNext(c) == L'/') {
			c += 2;
		} else {
			*c++ = L'\\';
		}
	}
}

//=============================================================================
//
// ExpandEnvironmentStringsEx()
//
// Adjusted for Windows 95
//
void ExpandEnvironmentStringsEx(LPWSTR lpSrc, DWORD dwSrc) {
	WCHAR szBuf[312];

	if (ExpandEnvironmentStrings(lpSrc, szBuf, COUNTOF(szBuf))) {
		lstrcpyn(lpSrc, szBuf, dwSrc);
	}
}

//=============================================================================
//
// PathCanonicalizeEx()
//
//
void PathCanonicalizeEx(LPWSTR lpSrc) {
	WCHAR szDst[MAX_PATH];

	if (PathCanonicalize(szDst, lpSrc)) {
		lstrcpy(lpSrc, szDst);
	}
}

//=============================================================================
//
// GetLongPathNameEx()
//
//
DWORD GetLongPathNameEx(LPWSTR lpszPath, DWORD cchBuffer) {
	const DWORD dwRet = GetLongPathName(lpszPath, lpszPath, cchBuffer);
	if (dwRet) {
		if (PathGetDriveNumber(lpszPath) != -1) {
			CharUpperBuff(lpszPath, 1);
		}
		return dwRet;
	}
	return 0;
}

//=============================================================================
//
//	SHGetFileInfo2()
//
//	Return a default name when the file has been removed, and always append
//	a filename extension
//
DWORD_PTR SHGetFileInfo2(LPCWSTR pszPath, DWORD dwFileAttributes, SHFILEINFO *psfi, UINT cbFileInfo, UINT uFlags) {
	if (PathIsFile(pszPath)) {
		const DWORD_PTR dw = SHGetFileInfo(pszPath, dwFileAttributes, psfi, cbFileInfo, uFlags);
		if (lstrlen(psfi->szDisplayName) < lstrlen(PathFindFileName(pszPath))) {
			StrCatBuff(psfi->szDisplayName, PathFindExtension(pszPath), COUNTOF(psfi->szDisplayName));
		}
		return dw;
	}
	{
		const DWORD_PTR dw = SHGetFileInfo(pszPath, FILE_ATTRIBUTE_NORMAL, psfi, cbFileInfo, uFlags | SHGFI_USEFILEATTRIBUTES);
		if (lstrlen(psfi->szDisplayName) < lstrlen(PathFindFileName(pszPath))) {
			StrCatBuff(psfi->szDisplayName, PathFindExtension(pszPath), COUNTOF(psfi->szDisplayName));
		}
		return dw;
	}
}

void StripMnemonic(LPWSTR pszMenu) {
	LPWSTR prev = pszMenu;
	do {
		LPWSTR p = StrChr(prev, L'&');
		if (p == NULL) {
			break;
		}
		if (p[1] == L'&') {
			// double '&&' represents one literal '&'
			prev = p + 2;
		} else {
			int len = lstrlen(p);
			int offset = 1;
			prev = p;
			if (p > pszMenu && len > 2 && p[-1] == L'(' && p[2] == L')') {
				// "String (&S)" => "String"
				offset = 3;
				prev = p - 1;
				if (prev > pszMenu && prev[-1] == L' ') {
					--prev;
				}
			}

			len -= offset;
			MoveMemory(prev, p + offset, sizeof(WCHAR) * len);
			prev[len] = L'\0';
			break;
		}
	} while (TRUE);
}

//=============================================================================
//
// FormatNumberStr()
//
void FormatNumberStr(LPWSTR lpNumberStr) {
	const int i = lstrlen(lpNumberStr);
	if (i <= 3) {
		return;
	}

	// https://docs.microsoft.com/en-us/windows/desktop/Intl/locale-sthousand
	// https://docs.microsoft.com/en-us/windows/desktop/Intl/locale-sgrouping
	WCHAR szSep[4];
	const WCHAR sep = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, szSep, COUNTOF(szSep))? szSep[0] : L',';

	WCHAR *c = lpNumberStr + i;
	WCHAR *end = c;
#if 0
	i = 0;
	while ((c = CharPrev(lpNumberStr, c)) != lpNumberStr) {
		if (++i == 3) {
			i = 0;
			MoveMemory(c + 1, c, sizeof(WCHAR) * (end - c + 1));
			*c = sep;
			++end;
		}
	}
#endif
	lpNumberStr += 3;
	do {
		c -= 3;
		MoveMemory(c + 1, c, sizeof(WCHAR) * (end - c + 1));
		*c = sep;
		++end;
	} while (c > lpNumberStr);
}

//=============================================================================
//
// SetDlgItemIntEx()
//
BOOL SetDlgItemIntEx(HWND hwnd, int nIdItem, UINT uValue) {
	WCHAR szBuf[32];

	_ultow(uValue, szBuf, 10);
	FormatNumberStr(szBuf);

	return SetDlgItemText(hwnd, nIdItem, szBuf);
}

//=============================================================================
//
// A2W: Convert Dialog Item Text form Unicode to UTF-8 and vice versa
//
UINT GetDlgItemTextA2W(UINT uCP, HWND hDlg, int nIDDlgItem, LPSTR lpString, int nMaxCount) {
	DStringW wsz = DSTRINGW_INIT;
	const int iRet = DStringW_GetDlgItemText(&wsz, hDlg, nIDDlgItem);
	ZeroMemory(lpString, nMaxCount);
	if (iRet) {
		WideCharToMultiByte(uCP, 0, wsz.buffer, -1, lpString, nMaxCount - 2, NULL, NULL);
	}
	DStringW_Free(&wsz);
	return iRet;
}

void SetDlgItemTextA2W(UINT uCP, HWND hDlg, int nIDDlgItem, LPCSTR lpString) {
	const int len = lpString ? (int)strlen(lpString) : 0;
	if (len) {
		LPWSTR wsz = (LPWSTR)NP2HeapAlloc((len + 1) * sizeof(WCHAR));
		MultiByteToWideChar(uCP, 0, lpString, -1, wsz, len);
		SetDlgItemText(hDlg, nIDDlgItem, wsz);
		NP2HeapFree(wsz);
	} else {
		SetDlgItemText(hDlg, nIDDlgItem, L"");
	}
}

void ComboBox_AddStringA2W(UINT uCP, HWND hwnd, LPCSTR lpString) {
	const int len = lpString ? (int)strlen(lpString) : 0;
	if (len) {
		LPWSTR wsz = (LPWSTR)NP2HeapAlloc((len + 1) * sizeof(WCHAR));
		MultiByteToWideChar(uCP, 0, lpString, -1, wsz, len);
		ComboBox_AddString(hwnd, wsz);
		NP2HeapFree(wsz);
	}
}

//=============================================================================
//
// CodePageFromCharSet()
//
UINT CodePageFromCharSet(UINT uCharSet) {
	CHARSETINFO ci;
	if (TranslateCharsetInfo((DWORD *)(UINT_PTR)uCharSet, &ci, TCI_SRCCHARSET)) {
		return ci.ciACP;
	}
	return GetACP();
}

//=============================================================================
//
// MRU functions
//
LPMRULIST MRU_Create(LPCWSTR pszRegKey, int iFlags, int iSize) {
	LPMRULIST pmru = (LPMRULIST)NP2HeapAlloc(sizeof(MRULIST));
	pmru->szRegKey = pszRegKey;
	pmru->iFlags = iFlags;
	pmru->iSize = min_i(iSize, MRU_MAXITEMS);
	return pmru;
}

BOOL MRU_Destroy(LPMRULIST pmru) {
	for (int i = 0; i < pmru->iSize; i++) {
		if (pmru->pszItems[i]) {
			LocalFree(pmru->pszItems[i]);
		}
	}

	ZeroMemory(pmru, sizeof(MRULIST));
	NP2HeapFree(pmru);
	return TRUE;
}

int MRU_Compare(LPCMRULIST pmru, LPCWSTR psz1, LPCWSTR psz2) {
	return (pmru->iFlags & MRUFlags_CaseInsensitive) ? StrCmpI(psz1, psz2) : StrCmp(psz1, psz2);
}

BOOL MRU_Add(LPMRULIST pmru, LPCWSTR pszNew) {
	int i;
	for (i = 0; i < pmru->iSize; i++) {
		if (MRU_Compare(pmru, pmru->pszItems[i], pszNew) == 0) {
			LocalFree(pmru->pszItems[i]);
			break;
		}
	}
	i = min_i(i, pmru->iSize - 1);
	for (; i > 0; i--) {
		pmru->pszItems[i] = pmru->pszItems[i - 1];
	}
	pmru->pszItems[0] = StrDup(pszNew);
	return TRUE;
}

BOOL MRU_AddMultiline(LPMRULIST pmru, LPCWSTR pszNew) {
	const int len = lstrlen(pszNew);
	LPWSTR lpszEsc = (LPWSTR)NP2HeapAlloc((2*len + 1)*sizeof(WCHAR));
	AddBackslashW(lpszEsc, pszNew);
	MRU_Add(pmru, lpszEsc);
	NP2HeapFree(lpszEsc);
	return TRUE;
}

BOOL MRU_AddFile(LPMRULIST pmru, LPCWSTR pszFile, BOOL bRelativePath, BOOL bUnexpandMyDocs) {
	int i;
	for (i = 0; i < pmru->iSize; i++) {
		if (pmru->pszItems[i] == NULL) {
			break;
		}
		if (StrCaseEqual(pmru->pszItems[i], pszFile)) {
			LocalFree(pmru->pszItems[i]);
			break;
		}
		{
			WCHAR wchItem[MAX_PATH];
			PathAbsoluteFromApp(pmru->pszItems[i], wchItem, COUNTOF(wchItem), TRUE);
			if (StrCaseEqual(wchItem, pszFile)) {
				LocalFree(pmru->pszItems[i]);
				break;
			}
		}
	}
	i = min_i(i, pmru->iSize - 1);
	for (; i > 0; i--) {
		pmru->pszItems[i] = pmru->pszItems[i - 1];
	}

	if (bRelativePath) {
		WCHAR wchFile[MAX_PATH];
		PathRelativeToApp(pszFile, wchFile, COUNTOF(wchFile), TRUE, TRUE, bUnexpandMyDocs);
		pmru->pszItems[0] = StrDup(wchFile);
	} else {
		pmru->pszItems[0] = StrDup(pszFile);
	}

	/* notepad2-mod custom code start */
	// Needed to make Win7 jump lists work when NP2 is not explicitly associated
	if (IsWin7AndAbove()) {
		SHAddToRecentDocs(SHARD_PATHW, pszFile);
	}
	/* notepad2-mod custom code end */

	return TRUE;
}

BOOL MRU_Delete(LPMRULIST pmru, int iIndex) {
	if (iIndex < 0 || iIndex > pmru->iSize - 1) {
		return 0;
	}
	if (pmru->pszItems[iIndex]) {
		LocalFree(pmru->pszItems[iIndex]);
	}
	for (int i = iIndex; i < pmru->iSize - 1; i++) {
		pmru->pszItems[i] = pmru->pszItems[i + 1];
		pmru->pszItems[i + 1] = NULL;
	}
	return TRUE;
}

BOOL MRU_DeleteFileFromStore(LPCMRULIST pmru, LPCWSTR pszFile) {
	int i = 0;
	WCHAR wchItem[MAX_PATH];

	LPMRULIST pmruStore = MRU_Create(pmru->szRegKey, pmru->iFlags, pmru->iSize);
	MRU_Load(pmruStore);

	while (MRU_Enum(pmruStore, i, wchItem, COUNTOF(wchItem)) != -1) {
		PathAbsoluteFromApp(wchItem, wchItem, COUNTOF(wchItem), TRUE);
		if (StrCaseEqual(wchItem, pszFile)) {
			MRU_Delete(pmruStore, i);
		} else {
			i++;
		}
	}

	MRU_Save(pmruStore);
	MRU_Destroy(pmruStore);
	return 1;
}

BOOL MRU_Empty(LPMRULIST pmru) {
	for (int i = 0; i < pmru->iSize; i++) {
		if (pmru->pszItems[i]) {
			LocalFree(pmru->pszItems[i]);
			pmru->pszItems[i] = NULL;
		}
	}
	return TRUE;
}

int MRU_Enum(LPCMRULIST pmru, int iIndex, LPWSTR pszItem, int cchItem) {
	if (pszItem == NULL || cchItem == 0) {
		int i = 0;
		while (i < pmru->iSize && pmru->pszItems[i]) {
			i++;
		}
		return i;
	}

	if (iIndex < 0 || iIndex > pmru->iSize - 1 || !pmru->pszItems[iIndex]) {
		return -1;
	}
	lstrcpyn(pszItem, pmru->pszItems[iIndex], cchItem);
	return TRUE;
}

BOOL MRU_Load(LPMRULIST pmru) {
	if (StrIsEmpty(szIniFile)) {
		return TRUE;
	}

	IniSection section;
	WCHAR *pIniSectionBuf = (WCHAR *)NP2HeapAlloc(sizeof(WCHAR) * MAX_INI_SECTION_SIZE_MRU);
	const int cchIniSection = (int)(NP2HeapSize(pIniSectionBuf) / sizeof(WCHAR));
	IniSection *pIniSection = &section;

	MRU_Empty(pmru);
	IniSectionInit(pIniSection, MRU_MAXITEMS);

	LoadIniSection(pmru->szRegKey, pIniSectionBuf, cchIniSection);
	IniSectionParseArray(pIniSection, pIniSectionBuf, pmru->iFlags & MRUFlags_QuoteValue);
	const int count = pIniSection->count;
	const int size = pmru->iSize;

	for (int i = 0, n = 0; i < count && n < size; i++) {
		const IniKeyValueNode *node = &pIniSection->nodeList[i];
		LPCWSTR tchItem = node->value;
		if (StrNotEmpty(tchItem)) {
			pmru->pszItems[n++] = StrDup(tchItem);
		}
	}

	IniSectionFree(pIniSection);
	NP2HeapFree(pIniSectionBuf);
	return TRUE;
}

BOOL MRU_Save(LPCMRULIST pmru) {
	if (StrIsEmpty(szIniFile)) {
		return TRUE;
	}
	if (MRU_GetCount(pmru) == 0) {
		IniClearSection(pmru->szRegKey);
		return TRUE;
	}

	WCHAR tchName[16];
	IniSectionOnSave section;
	WCHAR *pIniSectionBuf = (WCHAR *)NP2HeapAlloc(sizeof(WCHAR) * MAX_INI_SECTION_SIZE_MRU);
	IniSectionOnSave *pIniSection = &section;
	pIniSection->next = pIniSectionBuf;
	const BOOL quoted = pmru->iFlags & MRUFlags_QuoteValue;

	for (int i = 0; i < pmru->iSize; i++) {
		if (StrNotEmpty(pmru->pszItems[i])) {
			wsprintf(tchName, L"%02i", i + 1);
			if (quoted) {
				IniSectionSetQuotedString(pIniSection, tchName, pmru->pszItems[i]);
			} else {
				IniSectionSetString(pIniSection, tchName, pmru->pszItems[i]);
			}
		}
	}

	SaveIniSection(pmru->szRegKey, pIniSectionBuf);
	NP2HeapFree(pIniSectionBuf);
	return TRUE;
}

BOOL MRU_MergeSave(LPCMRULIST pmru, BOOL bAddFiles, BOOL bRelativePath, BOOL bUnexpandMyDocs) {
	LPMRULIST pmruBase = MRU_Create(pmru->szRegKey, pmru->iFlags, pmru->iSize);
	MRU_Load(pmruBase);

	if (bAddFiles) {
		for (int i = pmru->iSize - 1; i >= 0; i--) {
			if (pmru->pszItems[i]) {
				WCHAR wchItem[MAX_PATH];
				PathAbsoluteFromApp(pmru->pszItems[i], wchItem, COUNTOF(wchItem), TRUE);
				MRU_AddFile(pmruBase, wchItem, bRelativePath, bUnexpandMyDocs);
			}
		}
	} else {
		for (int i = pmru->iSize - 1; i >= 0; i--) {
			if (pmru->pszItems[i]) {
				MRU_Add(pmruBase, pmru->pszItems[i]);
			}
		}
	}

	MRU_Save(pmruBase);
	MRU_Destroy(pmruBase);
	return TRUE;
}

/*

 Themed Dialogs
 Modify dialog templates to use current theme font
 Based on code of MFC helper class CDialogTemplate

*/
BOOL GetThemedDialogFont(LPWSTR lpFaceName, WORD *wSize) {
#if NP2_ENABLE_APP_LOCALIZATION_DLL && NP2_ENABLE_TEST_LOCALIZATION_LAYOUT
	extern LANGID uiLanguage;
	GetLocaleDefaultUIFont(uiLanguage, lpFaceName, wSize);
	return TRUE;
#else

	BOOL bSucceed = FALSE;
	const UINT iLogPixelsY = g_uSystemDPI;

	if (IsAppThemed()) {
		HTHEME hTheme = OpenThemeData(NULL, L"WINDOWSTYLE;WINDOW");
		if (hTheme) {
			LOGFONT lf;
			if (S_OK == GetThemeSysFont(hTheme, TMT_MSGBOXFONT, &lf)) {
				if (lf.lfHeight < 0) {
					lf.lfHeight = -lf.lfHeight;
				}
				*wSize = (WORD)MulDiv(lf.lfHeight, 72, iLogPixelsY);
				if (*wSize < 8) {
					*wSize = 8;
				}
				lstrcpyn(lpFaceName, lf.lfFaceName, LF_FACESIZE);
				bSucceed = TRUE;
			}
			CloseThemeData(hTheme);
		}
	}

	if (!bSucceed) {
		NONCLIENTMETRICS ncm;
		ZeroMemory(&ncm, sizeof(ncm));
		ncm.cbSize = sizeof(NONCLIENTMETRICS);
#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
		if (!IsVistaAndAbove()) {
			ncm.cbSize -= sizeof(ncm.iPaddedBorderWidth);
		}
#endif
		if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0)) {
			if (ncm.lfMessageFont.lfHeight < 0) {
				ncm.lfMessageFont.lfHeight = -ncm.lfMessageFont.lfHeight;
			}
			*wSize = (WORD)MulDiv(ncm.lfMessageFont.lfHeight, 72, iLogPixelsY);
			if (*wSize < 8) {
				*wSize = 8;
			}
			lstrcpyn(lpFaceName, ncm.lfMessageFont.lfFaceName, LF_FACESIZE);
			bSucceed = TRUE;
		}
	}

	if (bSucceed && !IsVistaAndAbove()) {
		// Windows 2000, XP, 2003
		lstrcpy(lpFaceName, L"Tahoma");
	}
	return bSucceed;
#endif
}

static inline BOOL DialogTemplate_IsDialogEx(const DLGTEMPLATE *pTemplate) {
	return ((DLGTEMPLATEEX *)pTemplate)->signature == 0xFFFF;
}

static inline BOOL DialogTemplate_HasFont(const DLGTEMPLATE *pTemplate) {
	return (DS_SETFONT & (DialogTemplate_IsDialogEx(pTemplate) ? ((DLGTEMPLATEEX *)pTemplate)->style : pTemplate->style));
}

static inline int DialogTemplate_FontAttrSize(BOOL bDialogEx) {
	return (int)sizeof(WORD) * (bDialogEx ? 3 : 1);
}

static inline BYTE *DialogTemplate_GetFontSizeField(const DLGTEMPLATE *pTemplate) {
	const BOOL bDialogEx = DialogTemplate_IsDialogEx(pTemplate);
	WORD *pw;

	if (bDialogEx) {
		pw = (WORD *)((DLGTEMPLATEEX *)pTemplate + 1);
	} else {
		pw = (WORD *)(pTemplate + 1);
	}

	if (*pw == (WORD)(-1)) {
		pw += 2;
	} else {
		while (*pw++){}
	}

	if (*pw == (WORD)(-1)) {
		pw += 2;
	} else {
		while (*pw++){}
	}

	while (*pw++){}

	return (BYTE *)pw;
}

DLGTEMPLATE *LoadThemedDialogTemplate(LPCWSTR lpDialogTemplateID, HINSTANCE hInstance) {
	HRSRC hRsrc = FindResource(hInstance, lpDialogTemplateID, RT_DIALOG);
	if (hRsrc == NULL) {
		return NULL;
	}

	HGLOBAL hRsrcMem = LoadResource(hInstance, hRsrc);
	const DLGTEMPLATE *pRsrcMem = (DLGTEMPLATE *)LockResource(hRsrcMem);
	const UINT dwTemplateSize = (UINT)SizeofResource(hInstance, hRsrc);

	DLGTEMPLATE *pTemplate = dwTemplateSize ? (DLGTEMPLATE *)NP2HeapAlloc(dwTemplateSize + LF_FACESIZE * 2) : NULL;
	if (pTemplate == NULL) {
		FreeResource(hRsrcMem);
		return NULL;
	}

	CopyMemory((BYTE *)pTemplate, pRsrcMem, (size_t)dwTemplateSize);
	FreeResource(hRsrcMem);

	WCHAR wchFaceName[LF_FACESIZE];
	WORD wFontSize;
	if (!GetThemedDialogFont(wchFaceName, &wFontSize)) {
		return pTemplate;
	}

	const BOOL bDialogEx = DialogTemplate_IsDialogEx(pTemplate);
	const BOOL bHasFont = DialogTemplate_HasFont(pTemplate);
	const int cbFontAttr = DialogTemplate_FontAttrSize(bDialogEx);

	if (bDialogEx) {
		((DLGTEMPLATEEX *)pTemplate)->style |= DS_SHELLFONT;
	} else {
		pTemplate->style |= DS_SHELLFONT;
	}

	const int cbNew = cbFontAttr + (int)((lstrlen(wchFaceName) + 1) * sizeof(WCHAR));
	const BYTE *pbNew = (BYTE *)wchFaceName;

	BYTE *pb = DialogTemplate_GetFontSizeField(pTemplate);
	const int cbOld = (int)(bHasFont ? cbFontAttr + 2 * (lstrlen((WCHAR *)(pb + cbFontAttr)) + 1) : 0);

	const BYTE *pOldControls = (BYTE *)(((DWORD_PTR)pb + cbOld + 3) & ~(DWORD_PTR)3);
	BYTE *pNewControls = (BYTE *)(((DWORD_PTR)pb + cbNew + 3) & ~(DWORD_PTR)3);

	const WORD nCtrl = bDialogEx ? (WORD)((DLGTEMPLATEEX *)pTemplate)->cDlgItems : (WORD)pTemplate->cdit;
	if (cbNew != cbOld && nCtrl > 0) {
		MoveMemory(pNewControls, pOldControls, (size_t)(dwTemplateSize - (pOldControls - (BYTE *)pTemplate)));
	}

	*(WORD *)pb = wFontSize;
	MoveMemory(pb + cbFontAttr, pbNew, (size_t)(cbNew - cbFontAttr));

	return pTemplate;
}

INT_PTR ThemedDialogBoxParam(HINSTANCE hInstance, LPCWSTR lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
	DLGTEMPLATE *pDlgTemplate = LoadThemedDialogTemplate(lpTemplate, hInstance);
	const INT_PTR ret = DialogBoxIndirectParam(hInstance, pDlgTemplate, hWndParent, lpDialogFunc, dwInitParam);
	if (pDlgTemplate) {
		NP2HeapFree(pDlgTemplate);
	}

	return ret;
}

HWND CreateThemedDialogParam(HINSTANCE hInstance, LPCWSTR lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
	DLGTEMPLATE *pDlgTemplate = LoadThemedDialogTemplate(lpTemplate, hInstance);
	HWND hwnd = CreateDialogIndirectParam(hInstance, pDlgTemplate, hWndParent, lpDialogFunc, dwInitParam);
	if (pDlgTemplate) {
		NP2HeapFree(pDlgTemplate);
	}

	return hwnd;
}

//=============================================================================
//
// File Dialog Hook
// OFNHookProc for GetOpenFileName/GetSaveFileName
// https://docs.microsoft.com/en-us/windows/win32/dlgbox/open-and-save-as-dialog-boxes
//
UINT_PTR CALLBACK OpenSaveFileDlgHookProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_NOTIFY: {
		LPOFNOTIFY pOFNOTIFY = (LPOFNOTIFY)lParam;
		switch (pOFNOTIFY->hdr.code) {
		case CDN_FILEOK: {
			HWND hFileDlg;
			LPOPENFILENAME pOFN = pOFNOTIFY->lpOFN;
			LPTSTR szPath = pOFN->lpstrFile;
			int n = lstrlen(szPath);
			BOOL bRawPath = TRUE;
			// Multi-selected result ends with \0\0.
			if ((pOFN->Flags & OFN_ALLOWMULTISELECT) && szPath[n + 1] != '\x0') {
				return FALSE;
			}
			if (!PathIsDirectory(szPath)) {
				return FALSE;
			}
			hFileDlg = GetParent(hWnd);
			for (int i = 0; i < n; i++) {
				if (szPath[i] == TEXT('/')) {
					szPath[i] = TEXT('\\');
					bRawPath &= FALSE;
				}
			}
			if (szPath[n - 1] != TEXT('\\') && szPath[n - 1] != TEXT(' ') && n + 1 < MAX_PATH) {
				bRawPath &= PathAddBackslash(szPath) == NULL;
			}
			if (bRawPath) {
				return FALSE;
			}
			// edt1: dlgs.h
			SendMessage(hFileDlg, CDM_SETCONTROLTEXT, edt1, (LPARAM)szPath);
			PostMessage(hFileDlg, WM_COMMAND, IDOK, 0);
			SetWindowLong(hWnd, 0, 1);
			return TRUE;
		}
		}
		return FALSE;
	}
	default:
		return FALSE;
	}
}

/******************************************************************************
*
* UnSlash functions
* Mostly taken from SciTE, (c) Neil Hodgson, https://www.scintilla.org
*
*/

/**
 * Convert C style \a, \b, \f, \n, \r, \t, \v, \xhh and \uhhhh into their indicated characters.
 */
unsigned int UnSlash(char *s, UINT cpEdit) {
	const char * const start = s;
	char *o = s;

	while (*s) {
		if (*s != '\\') {
			*o++ = *s++;
			continue;
		}
		s++;
		switch (*s) {
		case 'a':
			*o = '\a';
			break;
		case 'b':
			*o = '\b';
			break;
		case 'e':
			*o = '\x1B';
			break;
		case 'f':
			*o = '\f';
			break;
		case 'n':
			*o = '\n';
			break;
		case 'r':
			*o = '\r';
			break;
		case 't':
			*o = '\t';
			break;
		case 'v':
			*o = '\v';
			break;
		case '\\':
			*o = '\\';
			break;
		case 'x':
		case 'u': {
			const int digitCount = (*s == 'x') ? 2 : 4;
			int value = 0;
			int count = 0;
			for (; count < digitCount; count++) {
				const int hex = GetHexDigit(s[1]);
				if (hex < 0) {
					break;
				}
				value = (value << 4) | hex;
				s++;
			}
			if (value) {
				WCHAR val[2] = { (WCHAR)value, 0 };
				char ch[8];
				WideCharToMultiByte(cpEdit, 0, val, -1, ch, sizeof(ch), NULL, NULL);
				const char *pch = ch;
				*o = *pch++;
				while (*pch) {
					*++o = *pch++;
				}
			} else if (count == 0) {
				*o++ = '\\';
				*o = *s;
			} else {
				o--; // to balance o++; at end of switch
			}
		} break;
		default:
			// unknown escape sequence
			*o++ = '\\';
			*o = *s;
			break;
		}
		o++;
		if (*s) {
			s++;
		}
	}

	*o = '\0';
	return (unsigned int)(o - start);
}

/**
 * Convert C style \0oo into their indicated characters.
 * This is used to get control characters into the regular expresion engine.
 */
unsigned int UnSlashLowOctal(char *s) {
	const char * const start = s;
	char *o = s;

	while (*s) {
		if ((s[0] == '\\') && (s[1] == '0') && IsOctalDigit(s[2]) && IsOctalDigit(s[3])) {
			*o = (char)(8 * (s[2] - '0') + (s[3] - '0'));
			s += 3;
		} else {
			*o = *s;
		}
		o++;
		if (*s) {
			s++;
		}
	}

	*o = '\0';
	return (unsigned int)(o - start);
}

void TransformBackslashes(char *pszInput, BOOL bRegEx, UINT cpEdit) {
	if (bRegEx) {
		UnSlashLowOctal(pszInput);
	} else {
		UnSlash(pszInput, cpEdit);
	}
}

BOOL AddBackslashA(char *pszOut, const char *pszInput) {
	BOOL hasEscapeChar = FALSE;
	BOOL hasSlash = FALSE;
	char *lpszEsc = pszOut;
	const char *lpsz = pszInput;
	while (*lpsz) {
		switch (*lpsz) {
		case '\n':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'n';
			hasEscapeChar = TRUE;
			break;
		case '\r':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'r';
			hasEscapeChar = TRUE;
			break;
		case '\t':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 't';
			hasEscapeChar = TRUE;
			break;
		case '\\':
			*lpszEsc++ = '\\';
			*lpszEsc++ = '\\';
			hasSlash = TRUE;
			break;
		case '\f':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'f';
			hasEscapeChar = TRUE;
			break;
		case '\v':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'v';
			hasEscapeChar = TRUE;
			break;
		case '\a':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'b';
			hasEscapeChar = TRUE;
			break;
		case '\b':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'a';
			hasEscapeChar = TRUE;
			break;
		case '\x1B':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'e';
			hasEscapeChar = TRUE;
			break;
		default:
			*lpszEsc++ = *lpsz;
			break;
		}
		lpsz++;
	}

	if (hasSlash && !hasEscapeChar) {
		strcpy(pszOut, pszInput);
	}
	return hasEscapeChar;
}

BOOL AddBackslashW(LPWSTR pszOut, LPCWSTR pszInput) {
	BOOL hasEscapeChar = FALSE;
	BOOL hasSlash = FALSE;
	LPWSTR lpszEsc = pszOut;
	LPCWSTR lpsz = pszInput;
	while (*lpsz) {
		switch (*lpsz) {
		case '\n':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'n';
			hasEscapeChar = TRUE;
			break;
		case '\r':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'r';
			hasEscapeChar = TRUE;
			break;
		case '\t':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 't';
			hasEscapeChar = TRUE;
			break;
		case '\\':
			*lpszEsc++ = '\\';
			*lpszEsc++ = '\\';
			hasSlash = TRUE;
			break;
		case '\f':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'f';
			hasEscapeChar = TRUE;
			break;
		case '\v':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'v';
			hasEscapeChar = TRUE;
			break;
		case '\a':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'b';
			hasEscapeChar = TRUE;
			break;
		case '\b':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'a';
			hasEscapeChar = TRUE;
			break;
		case '\x1B':
			*lpszEsc++ = '\\';
			*lpszEsc++ = 'e';
			hasEscapeChar = TRUE;
			break;
		default:
			*lpszEsc++ = *lpsz;
			break;
		}
		lpsz++;
	}

	if (hasSlash && !hasEscapeChar) {
		lstrcpy(pszOut, pszInput);
	}
	return hasEscapeChar;
}

/*

 MinimizeToTray - Copyright 2000 Matthew Ellis <m.t.ellis@bigfoot.com>

 Changes made by flo:
 - Commented out: #include "stdafx.h"
 - Moved variable declaration: APPBARDATA appBarData;

*/

// MinimizeToTray
//
// A couple of routines to show how to make it produce a custom caption
// animation to make it look like we are minimizing to and maximizing
// from the system tray
//
// These routines are public domain, but it would be nice if you dropped
// me a line if you use them!
//
// 1.0 29.06.2000 Initial version
// 1.1 01.07.2000 The window retains it's place in the Z-order of windows
//    when minimized/hidden. This means that when restored/shown, it doen't
//    always appear as the foreground window unless we call SetForegroundWindow
//
// Copyright 2000 Matthew Ellis <m.t.ellis@bigfoot.com>
/*#include "stdafx.h"*/

// Odd. VC++6 winuser.h has IDANI_CAPTION defined (as well as IDANI_OPEN and
// IDANI_CLOSE), but the Platform SDK only has IDANI_OPEN...

// I don't know what IDANI_OPEN or IDANI_CLOSE do. Trying them in this code
// produces nothing. Perhaps they were intended for window opening and closing
// like the MAC provides...
#ifndef IDANI_OPEN
#define IDANI_OPEN 1
#endif
#ifndef IDANI_CLOSE
#define IDANI_CLOSE 2
#endif
#ifndef IDANI_CAPTION
#define IDANI_CAPTION 3
#endif

#define DEFAULT_RECT_WIDTH 150
#define DEFAULT_RECT_HEIGHT 30

// Returns the rect of where we think the system tray is. This will work for
// all current versions of the shell. If explorer isn't running, we try our
// best to work with a 3rd party shell. If we still can't find anything, we
// return a rect in the lower right hand corner of the screen
static VOID GetTrayWndRect(LPRECT lpTrayRect) {
	// First, we'll use a quick hack method. We know that the taskbar is a window
	// of class Shell_TrayWnd, and the status tray is a child of this of class
	// TrayNotifyWnd. This provides us a window rect to minimize to. Note, however,
	// that this is not guaranteed to work on future versions of the shell. If we
	// use this method, make sure we have a backup!
	HWND hShellTrayWnd = FindWindowEx(NULL, NULL, L"Shell_TrayWnd", NULL);
	if (hShellTrayWnd) {
		HWND hTrayNotifyWnd = FindWindowEx(hShellTrayWnd, NULL, L"TrayNotifyWnd", NULL);
		if (hTrayNotifyWnd) {
			GetWindowRect(hTrayNotifyWnd, lpTrayRect);
			return;
		}
	}

	// OK, we failed to get the rect from the quick hack. Either explorer isn't
	// running or it's a new version of the shell with the window class names
	// changed (how dare Microsoft change these undocumented class names!) So, we
	// try to find out what side of the screen the taskbar is connected to. We
	// know that the system tray is either on the right or the bottom of the
	// taskbar, so we can make a good guess at where to minimize to
	APPBARDATA appBarData;
	appBarData.cbSize = sizeof(appBarData);
	if (SHAppBarMessage(ABM_GETTASKBARPOS, &appBarData)) {
		// We know the edge the taskbar is connected to, so guess the rect of the
		// system tray. Use various fudge factor to make it look good
		switch (appBarData.uEdge) {
		case ABE_LEFT:
		case ABE_RIGHT:
			// We want to minimize to the bottom of the taskbar
			lpTrayRect->top = appBarData.rc.bottom - 100;
			lpTrayRect->bottom = appBarData.rc.bottom - 16;
			lpTrayRect->left = appBarData.rc.left;
			lpTrayRect->right = appBarData.rc.right;
			break;

		case ABE_TOP:
		case ABE_BOTTOM:
			// We want to minimize to the right of the taskbar
			lpTrayRect->top = appBarData.rc.top;
			lpTrayRect->bottom = appBarData.rc.bottom;
			lpTrayRect->left = appBarData.rc.right - 100;
			lpTrayRect->right = appBarData.rc.right - 16;
			break;
		}

		return;
	}

	// Blimey, we really aren't in luck. It's possible that a third party shell
	// is running instead of explorer. This shell might provide support for the
	// system tray, by providing a Shell_TrayWnd window (which receives the
	// messages for the icons) So, look for a Shell_TrayWnd window and work out
	// the rect from that. Remember that explorer's taskbar is the Shell_TrayWnd,
	// and stretches either the width or the height of the screen. We can't rely
	// on the 3rd party shell's Shell_TrayWnd doing the same, in fact, we can't
	// rely on it being any size. The best we can do is just blindly use the
	// window rect, perhaps limiting the width and height to, say 150 square.
	// Note that if the 3rd party shell supports the same configuraion as
	// explorer (the icons hosted in NotifyTrayWnd, which is a child window of
	// Shell_TrayWnd), we would already have caught it above
	hShellTrayWnd = FindWindowEx(NULL, NULL, L"Shell_TrayWnd", NULL);
	if (hShellTrayWnd) {
		GetWindowRect(hShellTrayWnd, lpTrayRect);
		if (lpTrayRect->right - lpTrayRect->left > DEFAULT_RECT_WIDTH) {
			lpTrayRect->left = lpTrayRect->right - DEFAULT_RECT_WIDTH;
		}
		if (lpTrayRect->bottom - lpTrayRect->top > DEFAULT_RECT_HEIGHT) {
			lpTrayRect->top = lpTrayRect->bottom - DEFAULT_RECT_HEIGHT;
		}

		return;
	}

	// OK. Haven't found a thing. Provide a default rect based on the current work
	// area
	SystemParametersInfo(SPI_GETWORKAREA, 0, lpTrayRect, 0);
	lpTrayRect->left = lpTrayRect->right - DEFAULT_RECT_WIDTH;
	lpTrayRect->top = lpTrayRect->bottom - DEFAULT_RECT_HEIGHT;
}

// Check to see if the animation has been disabled
/*static */BOOL GetDoAnimateMinimize(void) {
	ANIMATIONINFO ai;

	ai.cbSize = sizeof(ai);
	SystemParametersInfo(SPI_GETANIMATION, sizeof(ai), &ai, 0);

	return ai.iMinAnimate != 0;
}

void MinimizeWndToTray(HWND hwnd) {
	if (GetDoAnimateMinimize()) {
		RECT rcFrom;
		RECT rcTo;

		// Get the rect of the window. It is safe to use the rect of the whole
		// window - DrawAnimatedRects will only draw the caption
		GetWindowRect(hwnd, &rcFrom);
		GetTrayWndRect(&rcTo);

		// Get the system to draw our animation for us
		DrawAnimatedRects(hwnd, IDANI_CAPTION, &rcFrom, &rcTo);
	}

	// Add the tray icon. If we add it before the call to DrawAnimatedRects,
	// the taskbar gets erased, but doesn't get redrawn until DAR finishes.
	// This looks untidy, so call the functions in this order

	// Hide the window
	ShowWindow(hwnd, SW_HIDE);
}

void RestoreWndFromTray(HWND hwnd) {
	if (GetDoAnimateMinimize()) {
		// Get the rect of the tray and the window. Note that the window rect
		// is still valid even though the window is hidden
		RECT rcFrom;
		RECT rcTo;
		GetTrayWndRect(&rcFrom);
		GetWindowRect(hwnd, &rcTo);

		// Get the system to draw our animation for us
		DrawAnimatedRects(hwnd, IDANI_CAPTION, &rcFrom, &rcTo);
	}

	// Show the window, and make sure we're the foreground window
	ShowWindow(hwnd, SW_SHOW);
	SetActiveWindow(hwnd);
	SetForegroundWindow(hwnd);

	// Remove the tray icon. As described above, remove the icon after the
	// call to DrawAnimatedRects, or the taskbar will not refresh itself
	// properly until DAR finished
}
