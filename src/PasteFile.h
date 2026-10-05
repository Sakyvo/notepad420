/******************************************************************************
*
* notepad420
*
* PasteFile.h
*   Reverse-paste rendering of a file as a data URI (P3).
*
*   Filesystem only — no clipboard, no editor, no application globals — so the
*   rendering is exercised directly by tools/tests/paste_file_uri_test.cpp and
*   reused by both the file-list tier and the bitmap tier.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <windows.h>
#include <cstddef>
#include <string>

/// Largest single file a reverse paste will inline as base64; beyond this the
/// resulting data URI would be unusable in a text document anyway.
constexpr unsigned long long kMaxDataUriSourceBytes = 64ull * 1024 * 1024;

/// Renders a file as `data:<mime>;base64,<payload>`. MIME comes from the file
/// extension (unknown -> application/octet-stream). Returns false, leaving
/// `uri` empty, for an unreadable, empty, or oversized file.
bool PasteFileToDataUri(LPCWSTR path, std::string &uri) noexcept;

/// Renders raw bytes as `data:<mime>;base64,<payload>`.
bool PasteBytesToDataUri(const void *data, unsigned long long length,
						 LPCWSTR pathForExtension, std::string &uri) noexcept;

/// Builds the text a file-list paste inserts: absolute paths (forward) or one
/// `data:<mime>;base64,...` per file (reverse), CRLF separated, unquoted.
/// Filesystem only, so this is the piece the tests cover end to end; the
/// clipboard read and the editor insert around it are thin and manual.
bool PasteBuildFileListText(const wchar_t *const *paths, std::size_t count,
							bool reverse, std::wstring &out) noexcept;
