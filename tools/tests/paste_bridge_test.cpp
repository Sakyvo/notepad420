#include <windows.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include "../src/KeditBridge.h"
#include "../src/Base64.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, name) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FAIL %s\n", name); } } while (0)

// ---- fixtures ---------------------------------------------------------------

static const char kPngMagic[] = "\x89PNG\r\n\x1a\n";	// magic signature
static const char kJpgMagic[] = "\xFF\xD8\xFF\xE0";

// ---- fake sink --------------------------------------------------------------

struct SinkState {
	std::vector<std::pair<std::string, std::string>> calls;	// {ext, bytes->.ext, path}
	int failAt = -1;	// 第 i 次调用失败(0 起)
	std::string prefix = "C:/temp/np420-cache-";
};

static bool FakeSink(void *context, const void *bytes, std::size_t length,
					 const char *extension, std::string &outPath) {
	SinkState *s = static_cast<SinkState *>(context);
	const int idx = (int)s->calls.size();
	if (s->failAt == idx) {
		return false;
	}
	outPath = s->prefix + std::to_string(idx) + extension;
	s->calls.emplace_back(extension ? extension : "", outPath);
	(void)bytes; (void)length;
	return true;
}

// ---- tests ------------------------------------------------------------------

static void TestParseGood() {
	const char *json = R"({"v":1,"text":"![a](/imgs/a.png) hello","images":[{"uri":"/imgs/a.png","mime":"image/png","dataBase64":"iVBOR"}]})";
	KeditBridge::BridgePayload p;
	const bool ok = KeditBridge::ParseBridgePayload(json, std::strlen(json), p);
	CHECK(ok, "parse ok");
	CHECK(p.text == "![a](/imgs/a.png) hello", "text byte-equal");
	CHECK(p.images.size() == 1, "1 image");
	CHECK(p.images[0].uri == "/imgs/a.png", "uri");
	CHECK(p.images[0].mime == "image/png", "mime");
	CHECK(p.images[0].dataBase64 == "iVBOR", "dataBase64 preserved");
}

static void TestParseRejects() {
	KeditBridge::BridgePayload p;
	CHECK(!KeditBridge::ParseBridgePayload("", 0, p), "empty rejected");
	CHECK(!KeditBridge::ParseBridgePayload("{}", 2, p), "missing fields rejected");
	CHECK(!KeditBridge::ParseBridgePayload("{\"v\":2,\"text\":\"\",\"images\":[]}", 32, p), "version 2 rejected");
	CHECK(!KeditBridge::ParseBridgePayload("{\"v\":1,\"text\":\"a\"}", 20, p), "missing images rejected");
	CHECK(!KeditBridge::ParseBridgePayload("not json", 8, p), "not json rejected");
}

static void TestUnicodeEscape() {
	const char *json = "{\"v\":1,\"text\":\"a\\u4e2d\\u6587b\",\"images\":[{\"uri\":\"/x.png\",\"dataBase64\":\"AAAA\"}]}";
	KeditBridge::BridgePayload p;
	CHECK(KeditBridge::ParseBridgePayload(json, std::strlen(json), p), "unicode parse ok");
	CHECK(p.text.size() == 2 + 6, "utf-8 expansion");	// a + 中文(3+3 bytes) + b? a=1, 中=3, 文=3, b=1 => 8
	CHECK(p.text == "a中文b", "utf-8 bytes equal");
}

static void TestRewriteHappyPath() {
	KeditBridge::BridgePayload p;
	p.text = "![a](/imgs/a.png) tail ![b](/imgs/b.jpg)";
	p.images = {
		{ "/imgs/a.png", "image/png", "", },
		{ "/imgs/b.jpg", "image/jpeg", "" },
	};
	// base64 of "PNG-payload-magic-mock": craft a fake magic payload
	// we only need first bytes to match magic. Use base64 of \x89PNG\r\n\x1a\n pad.
	p.images[0].dataBase64 = "iVBORw0KGgo=";	// = 0x89 'P' 'N' 'G' 0x0d 0x0a 0x1a 0x0a
	p.images[1].dataBase64 = "/9j//w==";		// = 0xff 0xd8 0xff 0xff negligible
	SinkState s;
	std::string out;
	CHECK(KeditBridge::RewriteBridgeText(p, &s, FakeSink, out), "rewrite ok");
	CHECK(out == "C:/temp/np420-cache-0.png tail C:/temp/np420-cache-1.jpg",
		  "whole ![alt](uri) replaced by bare path");
	CHECK(s.calls.size() == 2, "sink called twice");
	CHECK(s.calls[0].first == ".png" && s.calls[1].first == ".jpg", "magic-derived ext");
}

static void TestRewriteDeclMimeFallback() {
	KeditBridge::BridgePayload p;
	p.text = "![](/imgs/unk.bin)";
	// invalid magic; mime says webp
	p.images = { { "/imgs/unk.bin", "image/webp", "Zm9v" } };	// "foo"
	SinkState s;
	std::string out;
	CHECK(KeditBridge::RewriteBridgeText(p, &s, FakeSink, out), "rewrite ok");
	CHECK(s.calls[0].first == ".webp", "mime fallback when magic unknown");
	CHECK(out.find("np420-cache-0.webp") != std::string::npos, "uri substituted");
}

static void TestRewriteEmptyUriAndImages() {
	KeditBridge::BridgePayload p;
	p.text = "plain";
	SinkState s;
	std::string out;
	CHECK(!KeditBridge::RewriteBridgeText(p, &s, FakeSink, out), "empty images rejected");
}

static void TestRewriteMissingUri() {
	KeditBridge::BridgePayload p;
	p.text = "no ![](nope) match";
	p.images = { { "/imgs/missing.png", "image/png", "iVBORw0KGgo=" } };
	SinkState s;
	std::string out;
	CHECK(!KeditBridge::RewriteBridgeText(p, &s, FakeSink, out), "missing uri silent-fails");
	CHECK(s.calls.empty(), "no sink call on failure (transactional)");
}

static void TestRewriteSinkFailStopsEverything() {
	KeditBridge::BridgePayload p;
	p.text = "![a](/imgs/a.png) ![b](/imgs/b.png)";
	p.images = {
		{ "/imgs/a.png", "image/png", "iVBORw0KGgo=" },
		{ "/imgs/b.png", "image/png", "iVBORw0KGgo=" },
	};
	SinkState s;
	s.failAt = 1;	// 第二张失败
	std::string out;
	CHECK(!KeditBridge::RewriteBridgeText(p, &s, FakeSink, out), "partial failure = whole tier refused");
	CHECK(s.calls.size() == 1, "first image written then stopped");
}

static void TestEolNormalizeAndRewriteWithCrlf() {
	// 重现生产场景: Chromium 把 text/plain 从 LF 规范化为 CRLF,
	// payload.text 仍是 LF。strict match 应在规范化后;重写基于剪贴板 CRLF 文本。
	KeditBridge::BridgePayload p;
	p.text = "![a](/imgs/a.png)\nrest";
	p.images = { { "/imgs/a.png", "image/png", "iVBORw0KGgo=" } };
	CHECK(KeditBridge::NormalizeEolLf("a\r\nb\rc\n") == "a\nb\nc\n", "eol normalize");

	SinkState s;
	std::string out;
	// baseText 是剪贴板读到的 (CRLF), 行 payload.text (LF) 在 normalize 后严格相等。
	const std::string clipText = "![a](/imgs/a.png)\r\nrest";
	CHECK(KeditBridge::NormalizeEolLf(clipText) == KeditBridge::NormalizeEolLf(p.text),
		  "strict match via normalize");
	CHECK(KeditBridge::RewriteBridgeText(p, clipText, &s, FakeSink, out),
		  "rewrite on crlf base");
	CHECK(out == "C:/temp/np420-cache-0.png\r\nrest", "rewrite preserves CRLF");
}


static void TestRewriteMissingAlt() {
	KeditBridge::BridgePayload p;
	p.text = "naked (/imgs/a.png) without image syntax";
	p.images = { { "/imgs/a.png", "image/png", "iVBORw0KGgo=" } };
	SinkState s;
	std::string out;
	CHECK(!KeditBridge::RewriteBridgeText(p, &s, FakeSink, out),
		  "uri not inside ![alt](...) →静退");
}

static void TestMapParseGood() {
	const char *json = R"({"text/plain":"Web Custom Format0","application/x-notepad420-paste":"Web Custom Format1","text/custom":"Web Custom Format2"})";
	std::string v;
	CHECK(KeditBridge::ParseStringMap(json, std::strlen(json),
									"application/x-notepad420-paste", v), "find bridge slot");
	CHECK(v == "Web Custom Format1", "slot name exact");
	CHECK(KeditBridge::ParseStringMap(json, std::strlen(json), "text/plain", v),
		  "first slot too");
	CHECK(v == "Web Custom Format0", "first slot name");
}

static void TestMapParseRejects() {
	std::string v;
	const char *good = R"({"a":"b"})";
	CHECK(!KeditBridge::ParseStringMap(good, std::strlen(good), "nope", v),
		  "missing key returns false");
	CHECK(!KeditBridge::ParseStringMap("{}", 2, "key", v), "empty map false");
	CHECK(!KeditBridge::ParseStringMap("not json", 8, "key", v), "garbage false");
	CHECK(!KeditBridge::ParseStringMap("{\"a\":1}", 8, "a", v), "non-string value false");
	CHECK(!KeditBridge::ParseStringMap("", 0, "a", v), "empty false");
}

static void TestMapFindSlotName() {
	const char *m = R"({"application/x-notepad420-paste":"Web Custom Format3"})";
	CHECK(KeditBridge::FindSlotName(m) == "Web Custom Format3",
		  "finds slot by essence key");
	const char *none = R"({"text/html":"Web Custom Format0"})";
	CHECK(KeditBridge::FindSlotName(none).empty(), "not-found -> empty");
}

static void TestMagicDetection() {
	CHECK(std::strcmp(KeditBridge::ExtensionForMagic(
		(const uint8_t *)kPngMagic, 8), ".png") == 0, "png magic");
	CHECK(std::strcmp(KeditBridge::ExtensionForMagic(
		(const uint8_t *)kJpgMagic, 4), ".jpg") == 0, "jpg magic");
	const char gifHead[] = "GIF89a";
	CHECK(std::strcmp(KeditBridge::ExtensionForMagic(
		(const uint8_t *)gifHead, 6), ".gif") == 0, "gif magic");
	const char webpHead[] = "RIFF\0\0\0\0WEBP";
	CHECK(std::strcmp(KeditBridge::ExtensionForMagic(
		(const uint8_t *)webpHead, 12), ".webp") == 0, "webp magic");
	const char avifHead[] = "xxxxftypavif";
	CHECK(std::strcmp(KeditBridge::ExtensionForMagic(
		(const uint8_t *)avifHead, 12), ".avif") == 0, "avif magic (ftyp)");
	const char junk[] = "random bytes";
	CHECK(KeditBridge::ExtensionForMagic((const uint8_t *)junk, 12) == nullptr, "unknown -> null");
}

int main() {
	TestParseGood();
	TestParseRejects();
	TestUnicodeEscape();
	TestRewriteHappyPath();
	TestRewriteDeclMimeFallback();
	TestRewriteEmptyUriAndImages();
	TestRewriteMissingUri();
	TestRewriteSinkFailStopsEverything();
	TestEolNormalizeAndRewriteWithCrlf();
	TestRewriteMissingAlt();
	TestMapParseGood();
	TestMapParseRejects();
	TestMapFindSlotName();
	TestMagicDetection();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
