// Tests for the reverse-paste file -> data URI rendering (P3).
//
// File-system only: no clipboard, no editor, no window. Build and run:
// tools\run-tests.bat
#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/PasteFile.h"
#include "../../src/PastePolicy.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                     \
	do {                                                                      \
		++g_checks;                                                           \
		if (!(cond)) {                                                        \
			++g_failures;                                                     \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));      \
		}                                                                     \
	} while (0)

static WCHAR g_dir[MAX_PATH];

static void WriteFileBytes(LPCWSTR name, const void *data, DWORD length) {
	WCHAR path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\%s", g_dir, name);
	const HANDLE file = CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(file, data, length, &written, nullptr);
		CloseHandle(file);
	}
}

static void PathOf(LPCWSTR name, WCHAR (&path)[MAX_PATH]) {
	swprintf(path, MAX_PATH, L"%s\\%s", g_dir, name);
}

static void Cleanup() {
	WCHAR pattern[MAX_PATH];
	swprintf(pattern, MAX_PATH, L"%s\\*", g_dir);
	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			WCHAR victim[MAX_PATH];
			swprintf(victim, MAX_PATH, L"%s\\%s", g_dir, data.cFileName);
			DeleteFile(victim);
		} while (FindNextFile(find, &data));
		FindClose(find);
	}
	RemoveDirectory(g_dir);
}

static void TestKnownTypesGetTheirMime() {
	WCHAR path[MAX_PATH];
	std::string uri;

	// "abc" base64-encodes to "YWJj".
	WriteFileBytes(L"a.png", "abc", 3);
	PathOf(L"a.png", path);
	CHECK(PasteFileToDataUri(path, uri), "png file renders");
	CHECK(uri == "data:image/png;base64,YWJj", "png data URI is exact");
	CHECK(uri.compare(0, 22, "data:image/png;base64,") == 0, "png header is the canonical one");

	WriteFileBytes(L"b.jpg", "abc", 3);
	PathOf(L"b.jpg", path);
	CHECK(PasteFileToDataUri(path, uri), "jpg file renders");
	CHECK(uri == "data:image/jpeg;base64,YWJj", "jpg maps to the jpeg MIME");

	WriteFileBytes(L"c.svg", "abc", 3);
	PathOf(L"c.svg", path);
	CHECK(PasteFileToDataUri(path, uri), "svg file renders");
	CHECK(uri == "data:image/svg+xml;base64,YWJj", "svg maps to svg+xml");

	// An unknown extension still inlines, with the octet-stream MIME.
	WriteFileBytes(L"d.bin", "abc", 3);
	PathOf(L"d.bin", path);
	CHECK(PasteFileToDataUri(path, uri), "unknown type renders");
	CHECK(uri == "data:application/octet-stream;base64,YWJj", "unknown type uses octet-stream");

	// The match is case insensitive, as Windows paths are.
	WriteFileBytes(L"e.PNG", "abc", 3);
	PathOf(L"e.PNG", path);
	CHECK(PasteFileToDataUri(path, uri), "uppercase extension renders");
	CHECK(uri == "data:image/png;base64,YWJj", "uppercase extension maps to png");
}

static void TestPayloadIsCorrectBase64() {
	// One byte: base64 is padded to "YQ==".
	WriteFileBytes(L"one.bin", "a", 1);
	WCHAR path[MAX_PATH];
	PathOf(L"one.bin", path);
	std::string uri;
	CHECK(PasteFileToDataUri(path, uri), "single byte renders");
	CHECK(uri == "data:application/octet-stream;base64,YQ==", "padding is correct");

	// Two bytes: "YWI=".
	WriteFileBytes(L"two.bin", "ab", 2);
	PathOf(L"two.bin", path);
	CHECK(PasteFileToDataUri(path, uri) && uri == "data:application/octet-stream;base64,YWI=",
		"two bytes encode correctly");

	// Binary content with every byte value round-trips through the encoder.
	unsigned char all[256];
	for (int i = 0; i < 256; ++i) {
		all[i] = static_cast<unsigned char>(i);
	}
	WriteFileBytes(L"all.bin", all, 256);
	PathOf(L"all.bin", path);
	CHECK(PasteFileToDataUri(path, uri), "all-bytes file renders");
	CHECK(uri.size() == 37 + 344, "256 bytes encode to 344 base64 characters");

	// Decoding the payload reproduces the file exactly: independent check of the
	// encoder through the project's own decoder would be circular, so compare
	// against a hand-computed prefix instead.
	CHECK(uri.compare(37, 12, "AAECAwQFBgcI") == 0, "payload starts with the expected bytes");
}

static void TestBuildFileListText() {
	// The exact seam PasteInsertFileList calls, exercised against real files.
	WriteFileBytes(L"x.png", "abc", 3);
	WriteFileBytes(L"y.gif", "abc", 3);
	WriteFileBytes(L"z.bin", "abc", 3);

	WCHAR x[MAX_PATH], y[MAX_PATH], z[MAX_PATH];
	swprintf(x, MAX_PATH, L"%s\\x.png", g_dir);
	swprintf(y, MAX_PATH, L"%s\\y.gif", g_dir);
	swprintf(z, MAX_PATH, L"%s\\z.bin", g_dir);
	const wchar_t *paths[] = { x, y, z };

	// P1 forward: absolute paths, one per line, unquoted, any file type.
	std::wstring forward;
	CHECK(PasteBuildFileListText(paths, 3, false, forward), "P1 build succeeds");
	CHECK(forward == std::wstring(x) + L"\r\n" + y + L"\r\n" + z,
		"P1 output is the three unquoted absolute paths CRLF separated");
	CHECK(forward.find(L"\"") == std::wstring::npos, "P1 adds no quotes");

	// P3 reverse: one data URI per file, each with its own MIME type.
	std::wstring reverse;
	CHECK(PasteBuildFileListText(paths, 3, true, reverse), "P3 build succeeds");
	CHECK(reverse.find(L"data:image/png;base64,YWJj") == 0, "P3 line 1 is the png URI");
	CHECK(reverse.find(L"data:image/gif;base64,YWJj") != std::wstring::npos,
		"P3 keeps the gif MIME");
	CHECK(reverse.find(L"data:application/octet-stream;base64,YWJj") != std::wstring::npos,
		"P3 falls back to octet-stream for an unknown type");
	int lineCount = 1;
	for (wchar_t c : reverse) {
		if (c == L'\n') {
			++lineCount;
		}
	}
	CHECK(lineCount == 3, "P3 emits exactly three lines");

	// A single file behaves the same way.
	std::wstring single;
	CHECK(PasteBuildFileListText(paths, 1, false, single), "single-file P1 succeeds");
	CHECK(single == x, "single-file P1 is just the path");

	// Unreadable files are skipped, never turned into empty lines.
	const wchar_t *missing[] = { x, L"\\nope\\missing.png" };
	std::wstring skipped;
	CHECK(PasteBuildFileListText(missing, 2, true, skipped), "P3 skips unreadable files");
	CHECK(skipped.find(L"missing") == std::wstring::npos, "the missing file contributes nothing");
	CHECK(skipped.find(L"\r\n") == std::wstring::npos, "no stray separator");

	// Zero count is refused.
	std::wstring none;
	CHECK(!PasteBuildFileListText(paths, 0, false, none), "zero count is refused");
}

static void TestRefusals() {
	std::string uri;
	WCHAR path[MAX_PATH];

	// A missing file is refused, and leaves the output empty.
	PathOf(L"missing.png", path);
	CHECK(!PasteFileToDataUri(path, uri), "missing file is refused");
	CHECK(uri.empty(), "refused render leaves the output empty");

	// A zero-byte file is refused rather than producing an empty payload.
	WriteFileBytes(L"empty.png", "", 0);
	PathOf(L"empty.png", path);
	CHECK(!PasteFileToDataUri(path, uri), "empty file is refused");

	// A null path is refused without crashing.
	CHECK(!PasteFileToDataUri(nullptr, uri), "null path is refused");
}

int main() {
	WCHAR temp[MAX_PATH];
	GetTempPath(MAX_PATH, temp);
	swprintf(g_dir, MAX_PATH, L"%snotepad420-file-uri-test-%u", temp, GetCurrentProcessId());
	RemoveDirectory(g_dir);
	CreateDirectory(g_dir, nullptr);

	TestKnownTypesGetTheirMime();
	TestPayloadIsCorrectBase64();
	TestBuildFileListText();
	TestRefusals();
	Cleanup();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}