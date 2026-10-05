/******************************************************************************
*
* notepad420
*
* PasteFile.cpp
*   See PasteFile.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include <windows.h>
#include <shlwapi.h>
#include <cstring>
#include <string>
#include <vector>
#include "Base64.h"
#include "PasteFile.h"
#include "PastePolicy.h"

bool PasteBytesToDataUri(const void *data, unsigned long long length,
						 LPCWSTR pathForExtension, std::string &uri) noexcept {
	uri.clear();
	if (data == nullptr || length == 0 || length > kMaxDataUriSourceBytes) {
		return false;
	}

	char header[128];
	LPCWSTR extension = PathFindExtensionW(pathForExtension);
	const char *mime = PasteExtensionToMime(extension);
	const std::size_t headerLen = PasteBuildDataUriHeader(header, sizeof(header), mime);
	if (headerLen == 0) {
		return false;
	}

	const std::size_t byteCount = static_cast<std::size_t>(length);
	uri.assign(header, headerLen);
	uri.resize(headerLen + ((byteCount + 2) / 3) * 4);
	const std::size_t encoded = Base64Encode(uri.data() + headerLen,
			static_cast<const uint8_t *>(data), byteCount, false);
	uri.resize(headerLen + encoded);
	return true;
}

bool PasteFileToDataUri(LPCWSTR path, std::string &uri) noexcept {
	uri.clear();
	if (path == nullptr) {
		return false;
	}

	const HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
								   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	LARGE_INTEGER size;
	if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
		static_cast<unsigned long long>(size.QuadPart) > kMaxDataUriSourceBytes) {
		CloseHandle(file);
		return false;
	}

	std::vector<char> data(static_cast<std::size_t>(size.QuadPart));
	DWORD read = 0;
	const bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) &&
					read == data.size();
	CloseHandle(file);
	if (!ok) {
		return false;
	}

	return PasteBytesToDataUri(data.data(), static_cast<unsigned long long>(data.size()), path, uri);
}
bool PasteBuildFileListText(const wchar_t *const *paths, std::size_t count,
							bool reverse, std::wstring &out) noexcept {
	out.clear();
	if (paths == nullptr || !PasteCheckFileCount(count)) {
		return false;
	}

	if (!reverse) {
		// P1: absolute paths, one per line, unquoted, no type filtering.
		std::size_t total = 1;
		for (std::size_t i = 0; i < count; ++i) {
			if (paths[i] != nullptr) {
				total += wcslen(paths[i]) + 2;
			}
		}
		std::vector<wchar_t> joined(total);
		if (!PasteJoinInsertedPaths(joined.data(), joined.size(), paths, count)) {
			return false;
		}
		out.assign(joined.data());
		return !out.empty();
	}

	// P3: one data URI per file, each with its own MIME type.
	std::vector<std::string> uris;
	uris.reserve(count);
	for (std::size_t i = 0; i < count; ++i) {
		std::string uri;
		if (paths[i] != nullptr && PasteFileToDataUri(paths[i], uri)) {
			uris.push_back(std::move(uri));
		}
	}
	if (uris.empty()) {
		return false;
	}
	std::vector<const char *> raw;
	raw.reserve(uris.size());
	std::size_t totalChars = 1;
	for (const auto &uri : uris) {
		raw.push_back(uri.c_str());
		totalChars += uri.size() + 2;
	}
	std::vector<wchar_t> wide(totalChars);
	if (!PasteJoinDataUris(wide.data(), wide.size(), raw.data(), raw.size())) {
		return false;
	}
	out.assign(wide.data());
	return true;
}
