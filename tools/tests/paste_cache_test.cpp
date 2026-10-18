// Tests for the paste cache directory (%TEMP%\notepad420-Paste).
//
// Runs headlessly against a throwaway directory, so no window, clipboard or
// user profile is touched. Build and run: tools\run-tests.bat
#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/PasteCache.h"

#pragma comment(lib, "shlwapi.lib")

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

static WCHAR g_sandbox[MAX_PATH];
static WCHAR g_firstFile[MAX_PATH];

static void PrepareSandbox() {
	WCHAR temp[MAX_PATH];
	GetTempPath(MAX_PATH, temp);
	swprintf(g_sandbox, MAX_PATH, L"%snotepad420-paste-test-%u", temp, GetCurrentProcessId());
	RemoveDirectory(g_sandbox);
}

static void CleanSandbox() {
	WCHAR pattern[MAX_PATH];
	swprintf(pattern, MAX_PATH, L"%s\\*", g_sandbox);
	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			WCHAR victim[MAX_PATH];
			swprintf(victim, MAX_PATH, L"%s\\%s", g_sandbox, data.cFileName);
			DeleteFile(victim);
		} while (FindNextFile(find, &data));
		FindClose(find);
	}
	RemoveDirectory(g_sandbox);
}

static void TestDefaultDir() {
	WCHAR dir[MAX_PATH];
	CHECK(PasteCacheGetDir(dir), "default dir resolves");
	CHECK(wcsstr(dir, L"notepad420-Paste") != nullptr, "default dir carries the product name");
	WCHAR temp[MAX_PATH];
	GetTempPath(MAX_PATH, temp);
	CHECK(wcsncmp(dir, temp, wcslen(temp)) == 0, "default dir lives under %TEMP%");
}

static void TestCustomDir() {
	// ADR 0014: the cache root is configurable; empty string restores the
	// built-in default.
	WCHAR dir[MAX_PATH];
	CHECK(PasteCacheGetDir(dir), "resolve with no custom dir");
	CHECK(wcsstr(dir, L"notepad420-Paste") != nullptr, "no custom dir falls back to the default");

	// A custom dir is returned verbatim (env vars already expanded by the
	// caller in LoadSettings; the cache layer stores what it was given).
	const WCHAR custom[] = L"C:\\notepad420-cache-custom";
	CHECK(PasteCacheSetCustomDir(custom), "set a custom dir");
	CHECK(PasteCacheGetDir(dir), "resolve with custom dir");
	CHECK(wcscmp(dir, custom) == 0, "custom dir wins over the default");

	CHECK(PasteCacheSetCustomDir(L""), "clear the custom dir");
	CHECK(PasteCacheGetDir(dir), "resolve after clearing");
	CHECK(wcsstr(dir, L"notepad420-Paste") != nullptr, "cleared custom dir falls back to the default");
}

static void TestEnsureAndMeasure() {
	CHECK(PasteCacheEnsureDir(g_sandbox), "creating the cache dir succeeds");
	CHECK(PasteCacheEnsureDir(g_sandbox), "creating it twice is idempotent");
	CHECK(GetFileAttributes(g_sandbox) != INVALID_FILE_ATTRIBUTES, "dir now exists");

	PasteCacheUsage empty = PasteCacheMeasure(g_sandbox);
	CHECK(empty.count == 0 && empty.bytes == 0, "a fresh dir measures as empty");

	// A missing directory measures as empty rather than failing.
	PasteCacheUsage missing = PasteCacheMeasure(L"Z:\\definitely\\not\\here");
	CHECK(missing.count == 0 && missing.bytes == 0, "missing dir measures as empty");
}

static void TestWriteAndNaming() {
	PasteCacheResetIndex();
	// A dedicated directory keeps the count deterministic for later tests.
	WCHAR dir[MAX_PATH];
	swprintf(dir, MAX_PATH, L"%s-write", g_sandbox);
	RemoveDirectory(dir);
	CHECK(PasteCacheEnsureDir(dir), "write sandbox ready");

	const char payload[] = "PNGDATA";
	WCHAR first[MAX_PATH];
	CHECK(PasteCacheWrite(dir, payload, sizeof(payload) - 1, ".png", first) == PasteCapVerdict::Ok,
		"first write is accepted");
	lstrcpyn(g_firstFile, first, MAX_PATH);

	CHECK(GetFileAttributes(first) != INVALID_FILE_ATTRIBUTES, "written file exists");
	CHECK(wcsstr(first, L".png") != nullptr, "written file keeps the extension");
	CHECK(wcsstr(first, L"-write") != nullptr, "written file is in its sandbox");

	// Contents round-trip.
	HANDLE file = CreateFile(first, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	CHECK(file != INVALID_HANDLE_VALUE, "written file opens");
	if (file != INVALID_HANDLE_VALUE) {
		char buf[16] = {};
		DWORD read = 0;
		ReadFile(file, buf, sizeof(buf), &read, nullptr);
		CloseHandle(file);
		CHECK(read == sizeof(payload) - 1 && memcmp(buf, payload, read) == 0, "contents round-trip");
	}

	// A second write in the same millisecond must not collide.
	WCHAR second[MAX_PATH];
	CHECK(PasteCacheWrite(dir, payload, sizeof(payload) - 1, ".png", second) == PasteCapVerdict::Ok,
		"second write is accepted");
	CHECK(wcscmp(first, second) != 0, "same-millisecond writes get distinct names");

	PasteCacheUsage usage = PasteCacheMeasure(dir);
	CHECK(usage.count == 2, "measure counts both files");
	CHECK(usage.bytes == 2 * (sizeof(payload) - 1), "measure sums both sizes");
}

static void TestWriteWithoutCap() {
	// ADR 0014: no eviction and no refusal by occupancy. Write far past the old
	// 64-file cap; every write succeeds and nothing is ever deleted.
	WCHAR dir[MAX_PATH];
	swprintf(dir, MAX_PATH, L"%s-nocap", g_sandbox);
	RemoveDirectory(dir);
	CHECK(PasteCacheEnsureDir(dir), "nocap sandbox ready");

	PasteCacheResetIndex();
	constexpr int kOverOldCap = 70; // old cap was 64; go past it
	WCHAR paths[kOverOldCap][MAX_PATH];
	const char one = 'x';
	for (int i = 0; i < kOverOldCap; ++i) {
		CHECK(PasteCacheWrite(dir, &one, 1, ".png", paths[i]) == PasteCapVerdict::Ok,
			"write past the old cap succeeds");
	}
	CHECK(PasteCacheMeasure(dir).count == kOverOldCap, "nothing was evicted past the old cap");

	// Clean up the nocap sandbox.
	WCHAR pattern[MAX_PATH];
	swprintf(pattern, MAX_PATH, L"%s\\*", dir);
	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			WCHAR victim[MAX_PATH];
			swprintf(victim, MAX_PATH, L"%s\\%s", dir, data.cFileName);
			DeleteFile(victim);
		} while (FindNextFile(find, &data));
		FindClose(find);
	}
	RemoveDirectory(dir);
}

static void TestWriteExDistinguishesFullFromUnwritable() {
	// The P2 path needs to tell an IO failure apart from success. With the cap
	// gone (ADR 0014), `full` means "cannot serve the paste" and is only ever
	// reported alongside a non-Ok verdict.
	WCHAR dir[MAX_PATH];
	swprintf(dir, MAX_PATH, L"%s\\capfree", g_sandbox);
	RemoveDirectory(dir);
	CHECK(PasteCacheEnsureDir(dir), "capfree sandbox ready");

	const char payload[] = "PNG";
	WCHAR path[MAX_PATH];
	bool full = true;
	CHECK(PasteCacheWriteEx(dir, payload, sizeof(payload) - 1, ".png", path, full) ==
			PasteCapVerdict::Ok, "an ordinary write succeeds");
	CHECK(!full, "a successful write never reports full");
	CHECK(GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES, "the file landed on disk");

	// An unwritable directory fails, and it IS the "cannot serve" case.
	full = false;
	CHECK(PasteCacheWriteEx(L"Z:\\notepad420\\definitely\\missing\\x", payload, 3, ".png", path, full) !=
			PasteCapVerdict::Ok, "an unwritable directory fails");
	CHECK(full, "an unwritable directory is reported as full (cannot serve)");

	// Clean up so the shared sandbox stays deterministic.
	WCHAR pattern[MAX_PATH];
	swprintf(pattern, MAX_PATH, L"%s\\*", dir);
	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			WCHAR victim[MAX_PATH];
			swprintf(victim, MAX_PATH, L"%s\\%s", dir, data.cFileName);
			DeleteFile(victim);
		} while (FindNextFile(find, &data));
		FindClose(find);
	}
	RemoveDirectory(dir);
}
namespace {

/// A P4 sink backed by the real cache directory, so the whole "rewrite the text
/// and store the images" path is exercised without a clipboard or an editor.
struct RewriteSinkContext {
	WCHAR dir[MAX_PATH];
	int stored = 0;
	int allowance = 1 << 30;
};

bool RewriteSink(void *context, const void *bytes, std::size_t length,
				 const char *extension, std::string &outPath) {
	auto *ctx = static_cast<RewriteSinkContext *>(context);
	++ctx->stored;
	if (ctx->stored > ctx->allowance) {
		outPath.clear();
		return false;
	}
	WCHAR written[MAX_PATH];
	bool full = false;
	if (PasteCacheWriteEx(ctx->dir, bytes, length, extension, written, full) != PasteCapVerdict::Ok ||
		full) {
		outPath.clear();
		return false;
	}
	outPath.clear();
	for (const WCHAR *p = written; *p != L'\0'; ++p) {
		outPath += static_cast<char>(static_cast<unsigned char>(*p));
	}
	return true;
}

} // namespace

static void TestP4RewriteAgainstRealCache() {
	WCHAR dir[MAX_PATH];
	swprintf(dir, MAX_PATH, L"%s-p4cache", g_sandbox);
	RemoveDirectory(dir);
	CHECK(PasteCacheEnsureDir(dir), "P4 cache sandbox ready");

	RewriteSinkContext ctx;
	lstrcpyn(ctx.dir, dir, MAX_PATH);

	// "abc" -> YWJj, "def" -> ZGVm. Both images land on disk and both URIs in
	// the text are replaced by the paths written.
	const std::string text =
		"see data:image/png;base64,YWJj and data:image/gif;base64,ZGVm done";
	std::string out;
	std::size_t converted = 0, refused = 0;
	CHECK(PasteRewriteDataUris(text.data(), text.size(), &ctx, RewriteSink, out,
							   &converted, &refused), "P4 rewrites against the real cache");
	CHECK(converted == 2 && refused == 0, "both images stored and replaced");
	CHECK(out.find("data:image/") == std::string::npos, "no URI survives");
	CHECK(out.find("see ") == 0 && out.find(" done") != std::string::npos,
		"the surrounding text is preserved");

	// Two files really exist, with the extensions the URIs asked for.
	PasteCacheUsage usage = PasteCacheMeasure(dir);
	CHECK(usage.count == 2, "two images landed in the cache");
	CHECK(usage.bytes == 6, "each image contributed its three decoded bytes");

	bool sawPng = false, sawGif = false;
	WCHAR pattern[MAX_PATH];
	swprintf(pattern, MAX_PATH, L"%s\\*", dir);
	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			const WCHAR *ext = PathFindExtension(data.cFileName);
			if (wcscmp(ext, L".png") == 0) {
				sawPng = true;
			}
			if (wcscmp(ext, L".gif") == 0) {
				sawGif = true;
			}
		} while (FindNextFile(find, &data));
		FindClose(find);
	}
	CHECK(sawPng, "a .png file was written");
	CHECK(sawGif, "a .gif file was written");

	// The rewritten paths point at the cache directory.
	CHECK(out.find("p4cache") != std::string::npos,
		"the inserted paths point into the cache directory");

	// Now a cap refusal in the middle: only the first image converts and the
	// rest of the text keeps its URI.
	WCHAR dir2[MAX_PATH];
	swprintf(dir2, MAX_PATH, L"%s-p4cap", g_sandbox);
	RemoveDirectory(dir2);
	PasteCacheEnsureDir(dir2);
	RewriteSinkContext ctx2;
	lstrcpyn(ctx2.dir, dir2, MAX_PATH);
	ctx2.allowance = 1;

	const std::string twoUris =
		"data:image/png;base64,YWJj|data:image/gif;base64,ZGVm";
	out.clear();
	CHECK(PasteRewriteDataUris(twoUris.data(), twoUris.size(), &ctx2, RewriteSink, out,
							   &converted, &refused), "the first image still converts");
	CHECK(converted == 1, "one conversion despite the refusal");
	CHECK(refused == 1, "the refused URI is counted");
	CHECK(PasteCacheMeasure(dir2).count == 1, "exactly one image was cached");
	CHECK(out.find("data:image/gif;base64,ZGVm") != std::string::npos,
		"the refused URI is left as text");

	// Clean both sandboxes so the shared fixture stays deterministic.
	for (const WCHAR *target : { dir, dir2 }) {
		WCHAR pat[MAX_PATH];
		swprintf(pat, MAX_PATH, L"%s\\*", target);
		WIN32_FIND_DATA d;
		const HANDLE h = FindFirstFile(pat, &d);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				WCHAR victim[MAX_PATH];
				swprintf(victim, MAX_PATH, L"%s\\%s", target, d.cFileName);
				DeleteFile(victim);
			} while (FindNextFile(h, &d));
			FindClose(h);
		}
		RemoveDirectory(target);
	}
}

// Touch a file with an explicit last-write FILETIME (offline-picked dates).
static void SetWriteTimeIn(LPCWSTR dir, LPCWSTR name, const SYSTEMTIME &st) {
	WCHAR path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\%s", dir, name);
	HANDLE h = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
						  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return;
	}
	FILETIME ft;
	SystemTimeToFileTime(&st, &ft);
	SetFileTime(h, nullptr, nullptr, &ft);
	CloseHandle(h);
}

static bool ExistsIn(LPCWSTR dir, LPCWSTR name) {
	WCHAR path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\%s", dir, name);
	return GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

static void DeleteIn(LPCWSTR dir, LPCWSTR name) {
	WCHAR path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\%s", dir, name);
	DeleteFile(path);
}

static void CreateBytes(LPCWSTR dir, LPCWSTR name, DWORD size) {
	WCHAR path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\%s", dir, name);
	HANDLE h = CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return;
	}
	std::vector<char> chunk(size ? size : 1, 'x');
	DWORD written = 0;
	WriteFile(h, chunk.data(), static_cast<DWORD>(chunk.size()), &written, nullptr);
	CloseHandle(h);
}

static void TestIsCacheFileName() {
	CHECK(PasteCacheIsCacheFileName(L"20250113-101112-250_0.png"), "canonical png name");
	CHECK(PasteCacheIsCacheFileName(L"20250113-101112-250_12.jpeg"), "multi-digit index, jpeg");
	CHECK(PasteCacheIsCacheFileName(L"20250113-101112-250_0.GIF"), "extension is case-insensitive");
	CHECK(!PasteCacheIsCacheFileName(L"readme.txt"), "plain name is not cache");
	CHECK(!PasteCacheIsCacheFileName(L"20250113-101112-250_0.txt"), "non-image extension is not cache");
	CHECK(!PasteCacheIsCacheFileName(L"2025011-101112-250_0.png"), "7-digit date is not cache");
	CHECK(!PasteCacheIsCacheFileName(L"20250113-101112-25_0.png"), "2-digit millis is not cache");
	CHECK(!PasteCacheIsCacheFileName(L"20250113_101112-250_0.png"), "wrong separator is not cache");
	// Format-valid but calendar-invalid dates stay cache files: the cleanup
	// falls back to mtime for them (ADR decision: name gate, time fallback).
	CHECK(PasteCacheIsCacheFileName(L"20250230-101112-250_0.png"), "calendar-invalid stays a cache file");
}

static void TestCleanup() {
	WCHAR dir[MAX_PATH];
	swprintf(dir, MAX_PATH, L"%s-clean", g_sandbox);
	RemoveDirectory(dir);
	CHECK(CreateDirectory(dir, nullptr), "clean sandbox dir");

	CreateBytes(dir, L"20250110-101112-250_0.png", 100);	// 10 days old
	CreateBytes(dir, L"20250115-101112-250_1.png", 100);	// 5 days old
	CreateBytes(dir, L"20250113-101112-250_2.png", 100);	// exactly 7 days old
	CreateBytes(dir, L"readme.txt", 100);					// not our naming
	CreateBytes(dir, L"20250110-101112-250_3.pdf", 100);	// cache name, foreign extension
	CreateBytes(dir, L"20250230-101112-250_4.png", 100);	// invalid embedded date, fresh mtime
	WCHAR sub[MAX_PATH];
	swprintf(sub, MAX_PATH, L"%s\\sub", dir);
	CreateDirectory(sub, nullptr);
	CreateBytes(sub, L"20250101-101112-250_5.png", 100);	// never recurse

	// today = 20250120, threshold 7 days: 10-old and exactly-7 go, the rest stay.
	PasteCacheCleanResult r = PasteCacheCleanup(dir, 7, 20250120);
	CHECK(r.deletedCount == 2, "two entries older-or-equal threshold deleted");
	CHECK(r.deletedBytes == 200, "deleted bytes accounted");
	CHECK(ExistsIn(dir, L"20250115-101112-250_1.png"), "fresh entry kept");
	CHECK(ExistsIn(dir, L"readme.txt"), "foreign file kept");
	CHECK(ExistsIn(dir, L"20250110-101112-250_3.pdf"), "foreign extension kept");
	CHECK(ExistsIn(dir, L"20250230-101112-250_4.png"), "mtime-fresh invalid-date kept");
	CHECK(ExistsIn(sub, L"20250101-101112-250_5.png"), "subdirectory not touched");

	// mtime fallback: same invalid-date file, backdated beyond the threshold.
	SYSTEMTIME old{};
	old.wYear = 2024; old.wMonth = 12; old.wDay = 25;
	SetWriteTimeIn(dir, L"20250230-101112-250_4.png", old);
	r = PasteCacheCleanup(dir, 7, 20250120);
	CHECK(r.deletedCount == 1, "invalid-date entry goes once its mtime is old");

	// Threshold 0 = everything cache-named, regardless of dates.
	CreateBytes(dir, L"20250119-010203-004_6.webp", 50);
	r = PasteCacheCleanup(dir, 0, 20250120);
	CHECK(r.deletedCount == 2, "threshold 0 clears all cache-named files");
	CHECK(ExistsIn(dir, L"readme.txt"), "threshold 0 still spares foreign files");
	CHECK(ExistsIn(sub, L"20250101-101112-250_5.png"), "threshold 0 still spares subdirs");

	// Preview counts exactly what Cleanup would delete, touching nothing.
	CreateBytes(dir, L"20250105-101112-250_7.png", 60);
	CreateBytes(dir, L"20250119-101112-250_8.png", 60);
	PasteCacheCleanResult p = PasteCachePreviewCleanup(dir, 7, 20250120);
	CHECK(p.deletedCount == 1 && p.deletedBytes == 60, "preview counts the aged entry only");
	CHECK(ExistsIn(dir, L"20250105-101112-250_7.png"), "preview deletes nothing");
	r = PasteCacheCleanup(dir, 7, 20250120);
	CHECK(r.deletedCount == p.deletedCount && r.deletedBytes == p.deletedBytes, "cleanup matches its preview");
	DeleteIn(dir, L"20250119-101112-250_8.png");

	// Missing directory: quiet zero result, no crash (startup safety).
	r = PasteCacheCleanup(L"Z:\\notepad420-does-not-exist", 7, 20250120);
	CHECK(r.deletedCount == 0 && r.failedCount == 0, "missing dir is a quiet no-op");

	DeleteIn(dir, L"readme.txt");
	DeleteIn(sub, L"20250101-101112-250_5.png");
	RemoveDirectory(sub);
	RemoveDirectory(dir);
}

static void TestThrottle() {
	CHECK(PasteCacheThrottleElapsed(0, 20250120, 3), "never cleaned -> run now");
	CHECK(!PasteCacheThrottleElapsed(20250118, 20250120, 3), "two days elapsed is below three");
	CHECK(PasteCacheThrottleElapsed(20250117, 20250120, 3), "exactly interval triggers");
	CHECK(PasteCacheThrottleElapsed(20250131, 20250203, 3), "crossing month boundary counts days");
	CHECK(PasteCacheThrottleElapsed(20240228, 20240302, 2), "leap-year Feb 29 counted");
	CHECK(!PasteCacheThrottleElapsed(20250121, 20250120, 3), "future timestamp is silently respected");
}

static void TestParseAgeDays() {
	// Manual/auto age fields. ES_NUMBER blocks letters at the UI, so this is the
	// safety net for an empty box (user cleared it) and typed/pasted oddities:
	// anything unusable must fall back to the remembered default, never to 0.
	CHECK(PasteCacheParseAgeDays(L"7", 7) == 7, "plain number is read");
	CHECK(PasteCacheParseAgeDays(L"30", 7) == 30, "another number is read");
	CHECK(PasteCacheParseAgeDays(L"1", 7) == 1, "one is the minimum, not a fallback");
	CHECK(PasteCacheParseAgeDays(L"", 7) == 7, "empty field falls back (never 0, which would clear everything)");
	CHECK(PasteCacheParseAgeDays(L"0", 7) == 7, "zero falls back: the minimum age is one day");
	CHECK(PasteCacheParseAgeDays(L"abc", 7) == 7, "garbage falls back");
	CHECK(PasteCacheParseAgeDays(L"12abc", 7) == 12, "leading digits are read");
	CHECK(PasteCacheParseAgeDays(L" 7 ", 7) == 7, "surrounding spaces are tolerated");
}

int main() {
	PrepareSandbox();
	TestDefaultDir();
	TestCustomDir();
	TestEnsureAndMeasure();
	TestWriteAndNaming();
	TestWriteWithoutCap();
	TestP4RewriteAgainstRealCache();
	TestWriteExDistinguishesFullFromUnwritable();
	TestIsCacheFileName();
	TestCleanup();
	TestThrottle();
	TestParseAgeDays();
	CleanSandbox();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}