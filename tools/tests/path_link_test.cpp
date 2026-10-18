// Unit tests for PathLink (src/PathLink.h / src/PathLink.cpp):
// path candidate extraction from a line + resolution against a base dir.
// Resolve existence goes through an injected predicate; no filesystem here.
//
// Pure logic, no Windows API dependency in the tested surface. Run:
//   tools\run-tests.bat
#include <cstdio>
#include <cstring>

#include "../../src/PathLink.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                     \
	do {                                                                      \
		++g_checks;                                                           \
		if (!(cond)) {                                                        \
			++g_failures;                                                     \
			std::printf("FAIL %s:%d  %s\\n", __FILE__, __LINE__, (what));      \
		}                                                                     \
	} while (0)

static bool ExistsAlways(const char *raw, const char *baseDir, char *out) {
	// stand-in for the real predicate: "exists" = non-empty token; write the
	// joined path the way the real implementation is expected to.
	std::strcpy(out, baseDir[0] ? baseDir : "C:");
	std::strcat(out, "\\");
	std::strcat(out, raw);
	return true;
}

static bool ExistsNever(const char * /*raw*/, const char * /*base*/, char * /*out*/) {
	return false;
}

static void TestExtractBasicPath() {
	PathLinkToken t;
	CHECK(PathLinkExtractAt("see C:\\Users\\me\\diamond_sword.png now", 6, &t), "absolute token found");
	CHECK(t.begin == 4, "token begins at C:");
	CHECK(t.end == 33, "token ends after png");
}

static void TestExtractQuotedPath() {
	PathLinkToken t;
	CHECK(PathLinkExtractAt("log \"C:\\Program Files\\app\\a.png\" done", 6, &t), "quoted token found");
	CHECK(t.begin == 4, "begins at open quote");
	CHECK(t.end == 32, "ends past close quote");
}

static void TestTrailingPunctuationExcluded() {
	PathLinkToken t;
	CHECK(PathLinkExtractAt("open C:\\tmp\\a.png.", 5, &t), "token with trailing dot");
	CHECK(t.end - t.begin == 12, "trailing dot excluded");
}

static void TestRejectUnixStyle() {
	PathLinkToken t;
	CHECK(!PathLinkExtractAt("run /usr/bin/ls -la", 4, &t), "unix-style path not a token");
}

static void TestRejectUrl() {
	PathLinkToken t;
	CHECK(!PathLinkExtractAt("see https://example.com/x.png end", 6, &t), "url not a path token");
}

static void TestRelativePath() {
	PathLinkToken t;
	CHECK(PathLinkExtractAt("images\\logo.png and", 0, &t), "relative token found");
	CHECK(t.end - t.begin == 15, "relative length");
}

static void TestUncPath() {
	PathLinkToken t;
	bool uok = PathLinkExtractAt("mount \\\\srv\\share\\a.txt ok", 6, &t);
	std::printf("dbg unc ok=%d begin=%td end=%td\n", (int)uok, (ptrdiff_t)t.begin, (ptrdiff_t)t.end);
	CHECK(uok, "unc token found");
	CHECK(t.end - t.begin == 17, "unc length");
}

static void TestPositionOutsideToken() {
	// col lands on the space right after a path: the token ends before
	// the position, so it must NOT be reported (guard against loops).
	PathLinkToken t;
	CHECK(!PathLinkExtractAt("see C:\\a\\b.png end", 14, &t), "pos after token rejected");
	CHECK(!PathLinkExtractAt("see C:\\a\\b.png end", 15, &t), "pos inside trailing word rejected");
}

static void TestResolvePredicateWired() {
	char out[MAX_PATH * 2];
	CHECK(PathLinkResolveInto("C:\\a\\b.png", "C:\\doc", out, ExistsAlways), "predicate accepts");
	CHECK(!PathLinkResolveInto("D:\\missing\\x.png", "C:\\doc", out, ExistsNever), "predicate rejects");
}

int main() {
	TestExtractBasicPath();
	TestExtractQuotedPath();
	TestTrailingPunctuationExcluded();
	TestRejectUnixStyle();
	TestRejectUrl();
	TestRelativePath();
	TestUncPath();
	TestPositionOutsideToken();
	TestResolvePredicateWired();
	std::printf("%d checks, %d failures\\n", g_checks, g_failures);
	return g_failures != 0;
}
