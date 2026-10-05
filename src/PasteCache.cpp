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

bool PasteCacheGetDir(WCHAR (&dir)[MAX_PATH]) noexcept {
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

	const PasteCacheUsage usage = PasteCacheMeasure(dir);
	if (PasteCheckCap(usage, 1, length) != PasteCapVerdict::Ok) {
		return PasteCapVerdict::Full;
	}

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
		return PasteCapVerdict::Full;
	}

	const PasteCacheUsage usage = PasteCacheMeasure(dir);
	if (PasteCheckCap(usage, 1, length) != PasteCapVerdict::Ok) {
		full = true;
		return PasteCapVerdict::Full;
	}
	return PasteCacheWrite(dir, data, length, extension, outPath);
}
