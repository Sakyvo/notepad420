// Tests for the paste cache directory (%TEMP%\notepad420-Paste).
//
// Runs headlessly against a throwaway directory, so no window, clipboard or
// user profile is touched. Build and run: tools\run-tests.bat
#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstring>

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

static void TestCapRefusesWithoutEviction() {
	// A dedicated directory keeps the count deterministic regardless of what
	// earlier tests wrote.
	WCHAR dir[MAX_PATH];
	swprintf(dir, MAX_PATH, L"%s-cap", g_sandbox);
	RemoveDirectory(dir);
	CHECK(PasteCacheEnsureDir(dir), "cap sandbox ready");

	// Fill exactly to the cap with one-byte files.
	for (std::size_t i = 0; i < PasteCapCount; ++i) {
		WCHAR path[MAX_PATH];
		const char one = 'x';
		PasteCapVerdict verdict = PasteCacheWrite(dir, &one, 1, ".png", path);
		CHECK(verdict == PasteCapVerdict::Ok, "filling up to the cap is allowed");
		if (i == 0) {
			lstrcpyn(g_firstFile, path, MAX_PATH);
		}
	}
	const PasteCacheUsage usage = PasteCacheMeasure(dir);
	CHECK(usage.count == PasteCapCount, "cache holds exactly the cap count");

	// Now the cap is reached: the next write must be refused, and nothing evicted.
	WCHAR refused[MAX_PATH];
	const char one = 'x';
	CHECK(PasteCacheWrite(dir, &one, 1, ".png", refused) == PasteCapVerdict::Full,
		"write past the count cap is refused");
	CHECK(PasteCacheMeasure(dir).count == usage.count, "refusal did not evict anything");
	CHECK(GetFileAttributes(g_firstFile) != INVALID_FILE_ATTRIBUTES, "the oldest file survived");

	// The byte cap refuses independently of the count.
	WCHAR big[MAX_PATH];
	CHECK(PasteCacheWrite(dir, &one, 1, ".png", big) == PasteCapVerdict::Full,
		"still refused at the cap");

	// Clean up the cap sandbox.
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
	// The P2 path needs to tell "the cap is reached" (the user must act) apart
	// from "the write failed for another reason" (do not blame the cap).
	WCHAR dir[MAX_PATH];
	swprintf(dir, MAX_PATH, L"%s\\capfree", g_sandbox);
	RemoveDirectory(dir);
	CHECK(PasteCacheEnsureDir(dir), "capfree sandbox ready");

	const char payload[] = "PNG";
	WCHAR path[MAX_PATH];
	bool full = true;
	CHECK(PasteCacheWriteEx(dir, payload, sizeof(payload) - 1, ".png", path, full) ==
			PasteCapVerdict::Ok, "write below the cap succeeds");
	CHECK(!full, "a successful write does not report a full cache");
	CHECK(GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES, "the file landed on disk");

	// An unwritable directory is a failure, but must NOT be blamed on the cap.
	full = true;
	CHECK(PasteCacheWriteEx(L"Z:\\\\notepad420\\\\definitely\\\\missing\\x", payload, 3, ".png", path, full) !=
			PasteCapVerdict::Ok, "an unwritable directory fails");
	CHECK(!full, "an unwritable directory is not reported as a full cache");

	// Fill to the cap, then confirm the refusal IS reported as full.
	for (std::size_t i = 1; i < PasteCapCount; ++i) {
		WCHAR filler[MAX_PATH];
		PasteCacheWrite(dir, &payload[0], 1, ".png", filler);
	}
	full = false;
	CHECK(PasteCacheWriteEx(dir, payload, 3, ".png", path, full) == PasteCapVerdict::Full,
		"the cap refuses further writes");
	CHECK(full, "the cap refusal is reported as a full cache");

	// Clean up so the shared sandbox stays deterministic.
	WCHAR pattern[MAX_PATH];
	swprintf(pattern, MAX_PATH, L"%s\\*", dir);
	WIN32_FIND_DATA data;
	const HANDLE find = FindFirstFile(pattern, &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			WCHAR victim[MAX_PATH];
			swprintf(victim, MAX_PATH, L"%s\\\\%s", dir, data.cFileName);
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

int main() {
	PrepareSandbox();
	TestDefaultDir();
	TestEnsureAndMeasure();
	TestWriteAndNaming();
	TestCapRefusesWithoutEviction();
	TestP4RewriteAgainstRealCache();
	TestWriteExDistinguishesFullFromUnwritable();
	CleanSandbox();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}