// Unit tests for the clipboard paste policy (src/PastePolicy.h).
//
// The policy is pure logic with no Windows dependency, so it is exercised
// through a standalone console harness rather than the GUI. Build and run:
//   tools\run-tests.bat
#include <cstdio>
#include <cstring>
#include <string>

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

static void TestArbitrationPriority() {
	// CF_HDROP wins over everything else on the clipboard.
	ClipboardKinds fileList{true, true, true, true};
	CHECK(PasteChooseTier(fileList) == PasteTier::FileList, "file list beats bitmap and text");

	// Bitmap beats text (browsers copy CF_DIB together with a text URL).
	ClipboardKinds bitmapAndText{false, true, true, true};
	CHECK(PasteChooseTier(bitmapAndText) == PasteTier::Bitmap, "bitmap beats text");

	// Text carrying an embedded data URI is transformed.
	ClipboardKinds dataUriText{false, false, true, true};
	CHECK(PasteChooseTier(dataUriText) == PasteTier::EmbeddedDataUri, "data URI text is its own tier");

	// Plain text is unchanged behaviour.
	ClipboardKinds plainText{false, false, true, false};
	CHECK(PasteChooseTier(plainText) == PasteTier::PlainText, "plain text");

	// Text that does not actually contain a data URI must not be treated as one.
	ClipboardKinds textNoUri{false, false, true, false};
	CHECK(PasteChooseTier(textNoUri) == PasteTier::PlainText, "text without data URI stays plain");

	// Nothing on the clipboard at all.
	ClipboardKinds nothing{false, false, false, false};
	CHECK(PasteChooseTier(nothing) == PasteTier::PlainText, "empty clipboard falls back to plain paste");

	// A bitmap-only clipboard with no text.
	ClipboardKinds bitmapOnly{false, true, false, false};
	CHECK(PasteChooseTier(bitmapOnly) == PasteTier::Bitmap, "bitmap only");
}

static void TestReversePasteOnlyReversesNonText() {
	// Reverse paste (Ctrl+Shift+V) inverts the file/bitmap tiers into data URIs,
	// but leaves text alone: a text clipboard never becomes EmbeddedDataUri.
	ClipboardKinds fileList{true, false, false, false};
	CHECK(PasteChooseTier(fileList, true) == PasteTier::FileList, "reverse keeps file list tier");

	ClipboardKinds bitmap{false, true, false, false};
	CHECK(PasteChooseTier(bitmap, true) == PasteTier::Bitmap, "reverse keeps bitmap tier");

	// This is the key rule: text with an embedded data URI is NOT converted on
	// the reverse path (kedit text should paste verbatim).
	ClipboardKinds dataUriText{false, false, true, true};
	CHECK(PasteChooseTier(dataUriText, true) == PasteTier::PlainText,
		"reverse paste never rewrites text");
}

static void TestCacheCap() {
	// Empty cache accepts the first image.
	CHECK(PasteCheckCap({0, 0}, 1, 1024) == PasteCapVerdict::Ok, "first image fits");

	// Exactly at the count cap is still allowed; one more is refused.
	CHECK(PasteCheckCap({PasteCapCount - 1, 0}, 1, 0) == PasteCapVerdict::Ok,
		"reaching the count cap exactly is allowed");
	CHECK(PasteCheckCap({PasteCapCount, 0}, 1, 0) == PasteCapVerdict::Full,
		"one past the count cap is refused");

	// Byte cap behaves the same way.
	CHECK(PasteCheckCap({0, PasteCapBytes - 10}, 1, 10) == PasteCapVerdict::Ok,
		"reaching the byte cap exactly is allowed");
	CHECK(PasteCheckCap({0, PasteCapBytes - 10}, 1, 11) == PasteCapVerdict::Full,
		"one byte past the byte cap is refused");

	// A batch that would exceed the cap is refused as a whole.
	CHECK(PasteCheckCap({PasteCapCount - 2, 0}, 3, 0) == PasteCapVerdict::Full,
		"batch exceeding the count cap is refused");

	// Already-full cache refuses anything, even a zero-byte item.
	CHECK(PasteCheckCap({PasteCapCount, PasteCapBytes}, 1, 0) == PasteCapVerdict::Full,
		"full cache refuses everything");
}

static void TestPasteFileName() {
	char buf[64];

	// Zero-padded, millisecond precision, stable index suffix.
	PasteFormatFileName(buf, sizeof(buf), 2026, 9, 19, 7, 5, 3, 12, 7, ".png");
	CHECK(std::strcmp(buf, "20260919-070503-012_7.png") == 0, "zero padded file name");

	// A different index must not collide within the same millisecond.
	char buf2[64];
	PasteFormatFileName(buf2, sizeof(buf2), 2026, 9, 19, 7, 5, 3, 12, 7, ".png");
	CHECK(std::strcmp(buf, buf2) == 0, "same millisecond and index is deterministic");

	PasteFormatFileName(buf2, sizeof(buf2), 2026, 9, 19, 7, 5, 3, 12, 13, ".png");
	CHECK(std::strcmp(buf, buf2) != 0, "index disambiguates same millisecond");

	// Extension comes from the caller and is appended verbatim.
	PasteFormatFileName(buf, sizeof(buf), 2026, 12, 31, 23, 59, 59, 999, 63, ".png");
	CHECK(std::strcmp(buf, "20261231-235959-999_63.png") == 0, "millisecond rollover and index cap");

	// Truncation must stay NUL-terminated rather than overflow.
	char tiny[8];
	PasteFormatFileName(tiny, sizeof(tiny), 2026, 12, 31, 23, 59, 59, 999, 63, ".png");
	CHECK(std::strlen(tiny) < sizeof(tiny), "short buffer stays terminated");
}

static void TestDataUriPrecheck() {
	// The pre-check is what keeps ordinary pastes free of regex work.
	CHECK(PasteTextContainsDataUri("data:image/png;base64,AAAA", 26), "detects canonical data URI");
	CHECK(!PasteTextContainsDataUri("plain text", 10), "plain text is not a data URI");
	CHECK(!PasteTextContainsDataUri("DATA:IMAGE/PNG", 14), "pre-check is case sensitive like the canonical form");
	CHECK(!PasteTextContainsDataUri("data:image", 10), "truncated prefix is not a match");
	CHECK(!PasteTextContainsDataUri(nullptr, 0), "null text is not a match");
	CHECK(!PasteTextContainsDataUri("dat", 3), "shorter than the needle");
	// A match at the very end of the buffer must be found.
	const char tail[] = "xxdata:image/";
	CHECK(PasteTextContainsDataUri(tail, sizeof(tail) - 1), "match at end of buffer");

	// The wide variant is used when probing the clipboard directly.
	CHECK(PasteTextContainsDataUriW(L"data:image/png;base64,AAAA", 26), "wide: detects data URI");
	CHECK(!PasteTextContainsDataUriW(L"plain text", 10), "wide: plain text");
	CHECK(!PasteTextContainsDataUriW(nullptr, 0), "wide: null text");
	CHECK(PasteTextContainsDataUriW(L"xxdata:image/", 13), "wide: match at end of buffer");
}

static void TestDataUriScanner() {
	DataUriMatch m;

	// A canonical PNG URI is found with the right bounds and extension.
	const char *png = "![x](data:image/png;base64,iVBORw0KGgo=)";
	const std::size_t pngLen = std::strlen(png);
	CHECK(PasteFindDataUri(png, pngLen, 0, m), "finds a canonical PNG data URI");
	CHECK(std::strcmp(m.extension, ".png") == 0, "PNG maps to .png");
	CHECK(std::memcmp(png + m.start, "data:image/png;base64,", 21) == 0, "match starts at the URI");
	const std::size_t payloadLen = m.payloadEnd - m.payloadStart;
	CHECK(std::memcmp(png + m.payloadStart, "iVBORw0KGgo=", payloadLen) == 0, "payload bounds are exact");

	// Every accepted type is recognised, and the MIME variant jpeg maps to .jpg.
	const char *jpeg = "data:image/jpeg;base64,AAAA";
	CHECK(PasteFindDataUri(jpeg, std::strlen(jpeg), 0, m) && std::strcmp(m.extension, ".jpg") == 0,
		"jpeg maps to .jpg");
	const char *svg = "data:image/svg+xml;base64,PHN2Zz4=";
	CHECK(PasteFindDataUri(svg, std::strlen(svg), 0, m) && std::strcmp(m.extension, ".svg") == 0,
		"svg+xml maps to .svg");
	const char *webp = "data:image/webp;base64,AAAA";
	CHECK(PasteFindDataUri(webp, std::strlen(webp), 0, m) && std::strcmp(m.extension, ".webp") == 0,
		"webp maps to .webp");

	// An unaccepted image type is skipped, and the scan keeps looking.
	const char *tiff = "data:image/tiff;base64,AAAA";
	CHECK(!PasteFindDataUri(tiff, std::strlen(tiff), 0, m), "unaccepted type is not matched");

	// Non-base64 encodings are not handled.
	const char *plainEnc = "data:image/png,AAAA";
	CHECK(!PasteFindDataUri(plainEnc, std::strlen(plainEnc), 0, m), "non-base64 data URI is skipped");

	// A bare space ends the payload, so prose after a URI is not swallowed.
	const char *prose = "data:image/png;base64,AAAA and then some prose";
	CHECK(PasteFindDataUri(prose, std::strlen(prose), 0, m), "URI followed by prose is matched");
	char prosePayload[32];
	PasteCopyBase64Payload(prose, m, prosePayload, sizeof(prosePayload));
	CHECK(std::strcmp(prosePayload, "AAAA") == 0, "prose is not part of the payload");

	// The realistic quoted form: the closing quote ends the payload.
	const char *quoted = "<img src=\"data:image/png;base64,AABB\" />";
	CHECK(PasteFindDataUri(quoted, std::strlen(quoted), 0, m), "quoted attribute value is matched");
	CHECK(quoted[m.end] == '"', "payload ends just before the closing quote");

	// Whitespace inside the payload is tolerated and excluded from the payload.
	const char *wrapped = "data:image/png;base64,iVBO\nRw0K\nGgo=";
	CHECK(PasteFindDataUri(wrapped, std::strlen(wrapped), 0, m), "wrapped base64 is matched");
	char payload[64];
	const std::size_t n = PasteCopyBase64Payload(wrapped, m, payload, sizeof(payload));
	CHECK(std::strcmp(payload, "iVBORw0KGgo=") == 0, "whitespace is stripped from the payload");
	CHECK(n == 12, "payload length counts only payload characters");

	// A second URI later in the text is reachable by resuming from the first end.
	const char *two = "a data:image/png;base64,AAAA b data:image/gif;base64,BBBB";
	CHECK(PasteFindDataUri(two, std::strlen(two), 0, m), "first of two URIs is found");
	const std::size_t firstEnd = m.end;
	CHECK(PasteFindDataUri(two, std::strlen(two), firstEnd, m), "second URI is found after the first");
	CHECK(std::strcmp(m.extension, ".gif") == 0, "second URI keeps its own type");

	// Truncated headers and empty payloads are rejected rather than half-handled.
	CHECK(!PasteFindDataUri("data:image/png;base6", 20, 0, m), "truncated header is rejected");
	CHECK(!PasteFindDataUri("data:image/png;base64,", 21, 0, m), "empty payload is rejected");
	CHECK(!PasteFindDataUri("data:image/;base64,AAAA", 22, 0, m), "empty type is rejected");

	// The payload copy reports failure rather than truncating silently. `png`'s
	// payload is 12 characters, so 10 bytes cannot hold it plus the terminator.
	// Re-scan png because `m` was reused by the checks above.
	CHECK(PasteFindDataUri(png, pngLen, 0, m), "png re-found for the buffer checks");
	char tooSmall[10];
	CHECK(PasteCopyBase64Payload(png, m, tooSmall, sizeof(tooSmall)) == 0, "short buffer is refused");
	CHECK(tooSmall[0] == '\0', "refused copy is still terminated");
	// A buffer that fits exactly still succeeds.
	char exact[13];
	CHECK(PasteCopyBase64Payload(png, m, exact, sizeof(exact)) == 12, "exact-fit buffer is accepted");
}

static void TestInsertedTextAssembly() {
	// One path per line, unquoted, CRLF separated.
	const wchar_t *paths[] = { L"C:\\a b\\pic.png", L"D:\\c.png" };
	wchar_t buf[128];
	CHECK(PasteJoinInsertedPaths(buf, 128, paths, 2), "two paths join");
	CHECK(std::wcscmp(buf, L"C:\\a b\\pic.png\r\nD:\\c.png") == 0,
		"paths are unquoted and CRLF separated");

	// A single path carries no separator.
	const wchar_t *one[] = { L"C:\\only.png" };
	CHECK(PasteJoinInsertedPaths(buf, 128, one, 1), "one path joins");
	CHECK(std::wcscmp(buf, L"C:\\only.png") == 0, "single path has no separator");

	// A path with a space keeps the space and gains no quotes.
	const wchar_t *spaced[] = { L"C:\\my folder\\a b.png" };
	CHECK(PasteJoinInsertedPaths(buf, 128, spaced, 1), "spaced path joins");
	CHECK(std::wcscmp(buf, L"C:\\my folder\\a b.png") == 0,
		"spaces are preserved without quoting");

	// Too small a buffer is refused rather than silently truncated.
	wchar_t tiny[8];
	CHECK(!PasteJoinInsertedPaths(tiny, 8, paths, 2), "short buffer is refused");
	CHECK(tiny[0] == L'\0', "refused join is emptied");

	// A null FIRST entry must not produce a leading separator.
	const wchar_t *nullFirst[] = { nullptr, L"C:\\a.png" };
	CHECK(PasteJoinInsertedPaths(buf, 128, nullFirst, 2), "a null first entry is skipped");
	CHECK(std::wcscmp(buf, L"C:\\a.png") == 0, "no leading separator is emitted");

	// Null entries are skipped, so a partial DragQueryFile result still works.
	const wchar_t *withNull[] = { L"C:\\a.png", nullptr, L"D:\\b.png" };
	CHECK(PasteJoinInsertedPaths(buf, 128, withNull, 3), "null entry is skipped");
	CHECK(std::wcscmp(buf, L"C:\\a.png\r\nD:\\b.png") == 0,
		"null entries do not add separators");

	// The data URI header carries the MIME type.
	// "data:image/png;base64," is 22 characters.
	char header[64];
	CHECK(PasteBuildDataUriHeader(header, sizeof(header), "image/png") == 22, "header length");
	CHECK(std::strcmp(header, "data:image/png;base64,") == 0, "header text");
	CHECK(PasteBuildDataUriHeader(header, sizeof(header), nullptr) > 0, "null mime falls back");
	CHECK(std::strcmp(header, "data:application/octet-stream;base64,") == 0, "fallback mime text");
	char smallHdr[8];
	CHECK(PasteBuildDataUriHeader(smallHdr, sizeof(smallHdr), "image/png") == 0,
		"short header buffer is refused");
	CHECK(smallHdr[0] == '\0', "refused header is emptied");
}

static void TestJoinDataUris() {
	// Mirrors PasteInsertFileList's P3 assembly: one URI per line, CRLF separated.
	const char *uris[] = { "data:image/png;base64,YWJj", "data:image/gif;base64,YWJj" };
	wchar_t out[256];
	CHECK(PasteJoinDataUris(out, 256, uris, 2), "two URIs join");
	CHECK(std::wcscmp(out, L"data:image/png;base64,YWJj\r\ndata:image/gif;base64,YWJj") == 0,
		"URIs are CRLF separated");

	const char *one[] = { "data:image/png;base64,YWJj" };
	CHECK(PasteJoinDataUris(out, 256, one, 1), "one URI joins");
	CHECK(std::wcscmp(out, L"data:image/png;base64,YWJj") == 0, "single URI has no separator");

	// Null and empty entries are skipped rather than producing blank lines.
	const char *withGap[] = { "data:image/png;base64,YWJj", nullptr, "", "data:image/gif;base64,YWJj" };
	CHECK(PasteJoinDataUris(out, 256, withGap, 4), "gaps are skipped");
	CHECK(std::wcscmp(out, L"data:image/png;base64,YWJj\r\ndata:image/gif;base64,YWJj") == 0,
		"skipped entries add no separators");

	// All-empty input is a failure, not an empty paste.
	const char *none[] = { nullptr, "" };
	CHECK(!PasteJoinDataUris(out, 256, none, 2), "all-empty input is refused");

	// Zero count and a short buffer are refused.
	CHECK(!PasteJoinDataUris(out, 256, uris, 0), "zero count is refused");
	wchar_t tiny[8];
	CHECK(!PasteJoinDataUris(tiny, 8, uris, 2), "short buffer is refused");
	CHECK(tiny[0] == L'\0', "refused join is emptied");
}

static void TestFileCountValve() {
	// An empty list is not worth pasting, and an absurd list is refused.
	CHECK(!PasteCheckFileCount(0), "empty list is refused");
	CHECK(PasteCheckFileCount(1), "single file is accepted");
	CHECK(PasteCheckFileCount(PasteMaxFileCount), "the limit itself is accepted");
	CHECK(!PasteCheckFileCount(PasteMaxFileCount + 1), "one past the limit is refused");
	CHECK(PasteCheckFileCount(3), "a small multi-selection is accepted");
}

namespace {

/// A sink that accepts every image and records what it received, so the engine
/// can be tested without a real cache directory.
struct AcceptContext {
	int calls = 0;
	std::string decoded;
};

bool AcceptSink(void *context, const void *bytes, std::size_t length,
				const char *extension, std::string &outPath) {
	auto *ctx = static_cast<AcceptContext *>(context);
	++ctx->calls;
	if (bytes != nullptr && length != 0) {
		ctx->decoded.append(static_cast<const char *>(bytes), length);
	}
	outPath = "C:\\TEMP\\notepad420-Paste\\img\\x" +
			  std::to_string(ctx->calls) + (extension != nullptr ? extension : "");
	return true;
}

/// A sink that succeeds `allowance` times and then fails, standing in for a
/// cache that has just reached its cap.
struct RefusalContext {
	int calls = 0;
	int allowance = 0;
	std::string decoded;
};

bool CapSink(void *context, const void *bytes, std::size_t length,
			 const char *extension, std::string &outPath) {
	auto *ctx = static_cast<RefusalContext *>(context);
	++ctx->calls;
	if (ctx->calls > ctx->allowance) {
		outPath.clear();
		return false;
	}
	if (bytes != nullptr && length != 0) {
		ctx->decoded.append(static_cast<const char *>(bytes), length);
	}
	outPath = "cached" + std::to_string(ctx->calls) + (extension != nullptr ? extension : "");
	return true;
}

} // namespace

static void TestRewriteEngineConvertsAllUris() {
	// Two URIs, both accepted: both become paths and the surrounding text is
	// preserved byte for byte.
	const std::string text =
		"before ![a](data:image/png;base64,YWJj) mid "
		"![b](data:image/gif;base64,ZGVm) after";
	AcceptContext ctx;
	std::string out;
	std::size_t converted = 0, refused = 0;

	CHECK(PasteRewriteDataUris(text.data(), text.size(), &ctx, AcceptSink, out,
							   &converted, &refused), "both URIs are rewritten");
	CHECK(converted == 2, "two URIs converted");
	CHECK(refused == 0, "nothing refused");
	CHECK(ctx.calls == 2, "the sink was called once per image");
	CHECK(out.find("before ![a](") == 0, "text before the first URI is preserved");
	CHECK(out.find(" mid ") != std::string::npos, "text between the URIs is preserved");
	CHECK(out.find("after") != std::string::npos, "text after the last URI is preserved");
	CHECK(out.find("data:image/") == std::string::npos, "no data URI survives");
	CHECK(out.find("x1.png") != std::string::npos, "the first path carries the png extension");
	CHECK(out.find("x2.gif") != std::string::npos, "the second path carries the gif extension");
	CHECK(ctx.decoded == "abc" "def", "the decoded bytes of both images reached the sink");
}

static void TestRewriteEngineStopsAtRefusal() {
	// The cache accepts the first image and then refuses: the prefix converts,
	// the refusal and everything after it stay as literal URI text.
	const std::string text =
		"data:image/png;base64,YWJj|data:image/gif;base64,ZGVm|data:image/bmp;base64,QQ==";
	RefusalContext ctx;
	ctx.allowance = 1;
	std::string out;
	std::size_t converted = 0, refused = 0;

	CHECK(PasteRewriteDataUris(text.data(), text.size(), &ctx, CapSink, out,
							   &converted, &refused), "the first URI still converts");
	CHECK(converted == 1, "exactly one URI converted");
	CHECK(refused == 1, "the refused URI is counted once");
	CHECK(ctx.calls == 2, "the sink is not called again after a refusal");
	CHECK(out.find("cached1.png") == 0, "the accepted URI becomes a path");
	CHECK(out.find("data:image/gif;base64,ZGVm") != std::string::npos,
		"the refused URI keeps its original text");
	CHECK(out.find("data:image/bmp;base64,QQ==") != std::string::npos,
		"the URI after the refusal is left alone too");
}

static void TestRewriteEngineLeavesPlainTextAlone() {
	// No data URI at all: the text passes through untouched.
	const std::string text = "just some ordinary text, no images here";
	AcceptContext ctx;
	std::string out;
	std::size_t converted = 99, refused = 99;

	CHECK(PasteRewriteDataUris(text.data(), text.size(), &ctx, AcceptSink, out,
							   &converted, &refused), "plain text is accepted unchanged");
	CHECK(out == text, "plain text is byte-identical");
	CHECK(converted == 0 && refused == 0, "nothing converted or refused");
	CHECK(ctx.calls == 0, "the sink was never called");

	// A data URI of an unaccepted type is not converted either.
	const std::string tiff = "data:image/tiff;base64,AAAA";
	out.clear();
	CHECK(!PasteRewriteDataUris(tiff.data(), tiff.size(), &ctx, AcceptSink, out,
								&converted, &refused), "an unaccepted type is not rewritten");
	CHECK(out == tiff, "the unaccepted URI is preserved verbatim");
	CHECK(ctx.calls == 0, "the sink is not called for an unaccepted type");
}

static void TestRewriteEngineRejectsMalformedPayload() {
	// A base64 run whose length is not a multiple of four cannot decode.
	const std::string bad = "data:image/png;base64,YWJ";
	AcceptContext ctx;
	std::string out;
	std::size_t converted = 0, refused = 0;
	CHECK(!PasteRewriteDataUris(bad.data(), bad.size(), &ctx, AcceptSink, out, &converted, &refused),
		"a malformed payload yields no replacement");
	CHECK(ctx.calls == 0, "the sink is not called for a malformed payload");

	// Null and empty input are refused rather than crashing.
	CHECK(!PasteRewriteDataUris(nullptr, 0, &ctx, AcceptSink, out, &converted, &refused),
		"null input is refused");
	CHECK(!PasteRewriteDataUris("x", 1, &ctx, nullptr, out, &converted, &refused),
		"a null sink is refused");
}

static void TestPlainTextFallbackRule() {
	// Only plain text may defer to Scintilla. A tier that owns the clipboard must
	// never fall back: a refused screenshot would otherwise paste the page URL
	// that accompanied the DIB on the clipboard.
	CHECK(PasteAllowsPlainTextFallback(PasteTier::PlainText), "plain text may fall back");
	CHECK(!PasteAllowsPlainTextFallback(PasteTier::FileList), "file list never falls back");
	CHECK(!PasteAllowsPlainTextFallback(PasteTier::Bitmap), "bitmap never falls back");
	CHECK(!PasteAllowsPlainTextFallback(PasteTier::EmbeddedDataUri), "data URI tier never falls back");

	// Tie the rule to the arbitration result for the exact conflict case: a
	// clipboard carrying both a DIB and text resolves to the bitmap tier, which
	// therefore must not fall back to the text paste.
	const ClipboardKinds dibAndText{false, true, true, false};
	const PasteTier tier = PasteChooseTier(dibAndText, false);
	CHECK(tier == PasteTier::Bitmap, "DIB plus text resolves to the bitmap tier");
	CHECK(!PasteAllowsPlainTextFallback(tier), "the resolved bitmap tier does not fall back");
}

static void TestExtensionToMime() {
	CHECK(std::strcmp(PasteExtensionToMime(L".png"), "image/png") == 0, "png mime");
	CHECK(std::strcmp(PasteExtensionToMime(L".PNG"), "image/png") == 0, "mime is case insensitive");
	CHECK(std::strcmp(PasteExtensionToMime(L".jpg"), "image/jpeg") == 0, "jpg mime");
	CHECK(std::strcmp(PasteExtensionToMime(L".jpeg"), "image/jpeg") == 0, "jpeg mime");
	CHECK(std::strcmp(PasteExtensionToMime(L".gif"), "image/gif") == 0, "gif mime");
	CHECK(std::strcmp(PasteExtensionToMime(L".webp"), "image/webp") == 0, "webp mime");
	CHECK(std::strcmp(PasteExtensionToMime(L".bmp"), "image/bmp") == 0, "bmp mime");
	CHECK(std::strcmp(PasteExtensionToMime(L".avif"), "image/avif") == 0, "avif mime");
	CHECK(std::strcmp(PasteExtensionToMime(L".svg"), "image/svg+xml") == 0, "svg mime");
	CHECK(std::strcmp(PasteExtensionToMime(L""), "application/octet-stream") == 0,
		"empty extension falls back to octet-stream");
	CHECK(std::strcmp(PasteExtensionToMime(L".txt"), "application/octet-stream") == 0,
		"unknown extension falls back to octet-stream");
	CHECK(std::strcmp(PasteExtensionToMime(nullptr), "application/octet-stream") == 0,
		"null extension falls back to octet-stream");
}

int main() {
	TestArbitrationPriority();
	TestReversePasteOnlyReversesNonText();
	TestCacheCap();
	TestPasteFileName();
	TestDataUriPrecheck();
	TestDataUriScanner();
	TestInsertedTextAssembly();
	TestFileCountValve();
	TestJoinDataUris();
	TestRewriteEngineConvertsAllUris();
	TestRewriteEngineStopsAtRefusal();
	TestRewriteEngineLeavesPlainTextAlone();
	TestRewriteEngineRejectsMalformedPayload();
	TestPlainTextFallbackRule();
	TestExtensionToMime();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}