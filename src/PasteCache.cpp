/******************************************************************************
*
* notepad420
*
* PasteCache.cpp
*   See PasteCache.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstring>
#include <cstdio>
#include "PasteCache.h"

namespace {

constexpr LPCWSTR kCacheFolderName = L"notepad420-Paste";

/// Custom cache root (ADR 0014). Empty = unset, the built-in default applies.
/// A bool, not a WCHAR buffer: custom dirs longer than MAX_PATH are rejected.
bool g_hasCustomDir = false;
WCHAR g_customDir[MAX_PATH] = L"";

/// Process-local disambiguation index, advanced for every cache file name.
unsigned g_cacheIndex = 0;

bool EnsureDirectory(LPCWSTR dir) noexcept {
	const DWORD attributes = GetFileAttributes(dir);
	if (attributes != INVALID_FILE_ATTRIBUTES) {
		return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}
	return SHCreateDirectoryEx(nullptr, dir, nullptr) == ERROR_SUCCESS ||
		   GetFileAttributes(dir) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

bool PasteCacheSetCustomDir(LPCWSTR dir) noexcept {
	if (dir == nullptr || *dir == L'\0') {
		g_hasCustomDir = false;
		g_customDir[0] = L'\0';
		return true;
	}
	if (lstrlen(dir) >= MAX_PATH) {
		return false;
	}
	lstrcpyn(g_customDir, dir, MAX_PATH);
	g_hasCustomDir = true;
	return true;
}

bool PasteCacheGetCustomDir(WCHAR (&dir)[MAX_PATH]) noexcept {
	if (!g_hasCustomDir) {
		return false;
	}
	lstrcpyn(dir, g_customDir, MAX_PATH);
	return true;
}

bool PasteCacheGetDefaultDir(WCHAR (&dir)[MAX_PATH]) noexcept {
	const DWORD length = GetTempPath(MAX_PATH, dir);
	if (length == 0 || length > MAX_PATH) {
		return false;
	}
	PathAppend(dir, L"notepad420-Paste");
	return true;
}

bool PasteCacheGetDir(WCHAR (&dir)[MAX_PATH]) noexcept {
	if (g_hasCustomDir) {
		lstrcpyn(dir, g_customDir, MAX_PATH);
		return true;
	}
	WCHAR temp[MAX_PATH];
	const DWORD length = GetTempPath(MAX_PATH, temp);
	if (length == 0 || length > MAX_PATH) {
		return false;
	}
	dir[0] = L'\0';
	lstrcpyn(dir, temp, MAX_PATH);
	PathAppend(dir, kCacheFolderName);
	return true;
}

bool PasteCacheEnsureDir(LPCWSTR dir) noexcept {
	return (dir != nullptr) && EnsureDirectory(dir);
}

PasteCacheUsage PasteCacheMeasure(LPCWSTR dir) noexcept {
	PasteCacheUsage usage{0, 0};
	if (dir == nullptr || *dir == L'\0') {
		return usage;
	}

	WCHAR pattern[MAX_PATH];
	lstrcpyn(pattern, dir, MAX_PATH);
	PathAppend(pattern, L"*");

	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find == INVALID_HANDLE_VALUE) {
		return usage;
	}
	do {
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			continue;
		}
		++usage.count;
		usage.bytes += (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	} while (FindNextFile(find, &data));
	FindClose(find);
	return usage;
}

void PasteCacheResetIndex() noexcept {
	g_cacheIndex = 0;
}

bool PasteCacheNextPath(LPCWSTR dir, const char *extension, WCHAR (&path)[MAX_PATH]) noexcept {
	if (dir == nullptr || *dir == L'\0') {
		return false;
	}

	SYSTEMTIME now;
	GetLocalTime(&now);

	char name[MAX_PATH];
	if (PasteFormatFileName(name, sizeof(name),
							now.wYear, now.wMonth, now.wDay,
							now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
							g_cacheIndex++, extension) < 0) {
		return false;
	}

	WCHAR wideName[MAX_PATH];
	if (MultiByteToWideChar(CP_ACP, 0, name, -1, wideName, MAX_PATH) == 0) {
		return false;
	}
	path[0] = L'\0';
	lstrcpyn(path, dir, MAX_PATH);
	PathAppend(path, wideName);
	return true;
}

PasteCapVerdict PasteCacheWrite(LPCWSTR dir, const void *data, std::size_t length,
								const char *extension, WCHAR (&outPath)[MAX_PATH]) noexcept {
	outPath[0] = L'\0';
	if (!PasteCacheEnsureDir(dir)) {
		return PasteCapVerdict::Full;
	}

	// ADR 0014: no occupancy pre-check. Only the IO layer can refuse now.

	if (!PasteCacheNextPath(dir, extension, outPath)) {
		return PasteCapVerdict::Full;
	}

	const HANDLE file = CreateFile(outPath, GENERIC_WRITE, 0, nullptr,
								   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		outPath[0] = L'\0';
		return PasteCapVerdict::Full;
	}

	DWORD written = 0;
	const bool ok = WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr) &&
					written == length;
	CloseHandle(file);
	if (!ok) {
		DeleteFile(outPath);
		outPath[0] = L'\0';
		return PasteCapVerdict::Full;
	}
	return PasteCapVerdict::Ok;
}
PasteCapVerdict PasteCacheWriteEx(LPCWSTR dir, const void *data, std::size_t length,
								 const char *extension, WCHAR (&outPath)[MAX_PATH],
								 bool &full) noexcept {
	outPath[0] = L'\0';
	full = false;
	if (!PasteCacheEnsureDir(dir)) {
		full = true;
		return PasteCapVerdict::Full;
	}
	const PasteCapVerdict verdict = PasteCacheWrite(dir, data, length, extension, outPath);
	if (verdict != PasteCapVerdict::Ok) {
		full = true;
	}
	return verdict;
}

//=============================================================================
//
// Cache management (task 002 / ADR 0014)
//
namespace {

// Days since civil epoch (Howard Hinnant's algorithm); calendar-day deltas.
long long DaysFromCivil(int y, unsigned m, unsigned d) noexcept {
	y -= (m <= 2);
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097LL + static_cast<int>(doe) - 719468;
}

bool IsDigits(LPCWSTR s, int count) noexcept {
	for (int i = 0; i < count; ++i) {
		if (s[i] < L'0' || s[i] > L'9') {
			return false;
		}
	}
	return true;
}

unsigned DigitsTo(LPCWSTR s, int count) noexcept {
	unsigned v = 0;
	for (int i = 0; i < count; ++i) {
		v = v * 10 + (s[i] - L'0');
	}
	return v;
}

// Calendar validation incl. leap years; false for 20250230 and friends.
bool DateToDays(unsigned yyyymmdd, long long &outDays) noexcept {
	const unsigned m = (yyyymmdd / 100) % 100;
	const unsigned d = yyyymmdd % 100;
	const int y = static_cast<int>(yyyymmdd / 10000);
	if (y <= 0 || m < 1 || m > 12 || d < 1) {
		return false;
	}
	static const unsigned char daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	unsigned dim = daysInMonth[m - 1];
	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
		dim = 29;
	}
	if (d > dim) {
		return false;
	}
	outDays = DaysFromCivil(y, m, d);
	return true;
}

// mtime fallback date for entries whose embedded date is calendar-invalid.
bool FileTimeToDay(const WIN32_FIND_DATA &data, long long &outDays) noexcept {
	FILETIME local{};
	SYSTEMTIME st{};
	if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &st)) {
		return false;
	}
	return DateToDays(st.wYear * 10000u + st.wMonth * 100u + st.wDay, outDays);
}

bool HasImageExtension(LPCWSTR ext) noexcept {
	static const WCHAR *const imageExts[] = { L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp" };
	for (LPCWSTR candidate : imageExts) {
		if (_wcsicmp(ext, candidate) == 0) {
			return true;
		}
	}
	return false;
}

}	// namespace

bool PasteCacheIsCacheFileName(LPCWSTR fileName) noexcept {
	if (fileName == nullptr) {
		return false;
	}
	// YYYYMMDD-HHmmss-fff_N.<ext>
	if (!IsDigits(fileName, 8) || fileName[8] != L'-' || !IsDigits(fileName + 9, 6)
	 || fileName[15] != L'-' || !IsDigits(fileName + 16, 3) || fileName[19] != L'_') {
		return false;
	}
	LPCWSTR p = fileName + 20;
	if (!IsDigits(p, 1)) {
		return false;
	}
	while (*p >= L'0' && *p <= L'9') {
		++p;
	}
	return *p == L'.' && *(p + 1) != L'\0' && HasImageExtension(p + 1);
}

PasteCacheCleanResult PasteCacheCleanupCore(LPCWSTR dir, unsigned olderThanDays,
											unsigned todayYYYYMMDD, bool apply) noexcept {
	PasteCacheCleanResult result{0, 0, 0};
	if (dir == nullptr || *dir == L'\0') {
		return result;
	}
	long long todayDays = 0;
	if (!DateToDays(todayYYYYMMDD, todayDays)) {
		return result;
	}

	WCHAR pattern[MAX_PATH];
	lstrcpyn(pattern, dir, MAX_PATH);
	PathAppend(pattern, L"*");

	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find == INVALID_HANDLE_VALUE) {
		return result;	// missing/unreadable dir: quiet no-op
	}
	do {
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			continue;
		}
		if (!PasteCacheIsCacheFileName(data.cFileName)) {
			continue;	// foreign file: never touched
		}
		if (olderThanDays > 0) {
			long long itemDays = 0;
			unsigned embedded = 0;
			if (IsDigits(data.cFileName, 8)) {
				embedded = DigitsTo(data.cFileName, 8);
			}
			if (!DateToDays(embedded, itemDays) && !FileTimeToDay(data, itemDays)) {
				continue;	// unjudgeable age: keep
			}
			if (todayDays - itemDays < static_cast<long long>(olderThanDays)) {
				continue;
			}
		}
		WCHAR path[MAX_PATH];
		lstrcpyn(path, dir, MAX_PATH);
		PathAppend(path, data.cFileName);
		const unsigned long long size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
		if (!apply) {
			++result.deletedCount;
			result.deletedBytes += size;
		} else if (DeleteFile(path)) {
			++result.deletedCount;
			result.deletedBytes += size;
		} else {
			++result.failedCount;	// locked etc.: skip quietly
		}
	} while (FindNextFile(find, &data));
	FindClose(find);
	return result;
}

PasteCacheCleanResult PasteCacheCleanup(LPCWSTR dir, unsigned olderThanDays,
										unsigned todayYYYYMMDD) noexcept {
	return PasteCacheCleanupCore(dir, olderThanDays, todayYYYYMMDD, true);
}

PasteCacheCleanResult PasteCachePreviewCleanup(LPCWSTR dir, unsigned olderThanDays,
										   unsigned todayYYYYMMDD) noexcept {
	return PasteCacheCleanupCore(dir, olderThanDays, todayYYYYMMDD, false);
}

bool PasteCacheThrottleElapsed(unsigned lastYYYYMMDD, unsigned todayYYYYMMDD,
							   unsigned intervalDays) noexcept {
	long long todayDays = 0;
	if (!DateToDays(todayYYYYMMDD, todayDays)) {
		return false;
	}
	if (lastYYYYMMDD == 0) {
		return true;	// never cleaned before
	}
	long long lastDays = 0;
	if (!DateToDays(lastYYYYMMDD, lastDays)) {
		return false;	// garbage last-run: suppress quietly
	}
	return todayDays - lastDays >= static_cast<long long>(intervalDays);
}

int PasteCacheParseAgeDays(LPCWSTR text, int fallback) noexcept {
	if (text == nullptr) {
		return fallback;
	}
	while (*text == L' ' || *text == L'\t') {
		++text;
	}
	int value = 0;
	int digits = 0;
	while (*text >= L'0' && *text <= L'9' && digits < 9) {
		value = value * 10 + (*text - L'0');
		++text;
		++digits;
	}
	return (digits == 0 || value < 1) ? fallback : value;
}
