// Tests for the extracted base64 codec (src/Base64.cpp).
//
// Expected values come from an independent source: RFC 4648 test vectors and
// Python's base64 module, not from re-running the implementation.
#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/Base64.h"

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

static std::string Encode(const void *data, size_t length, bool urlSafe = false) {
	char out[512];
	const size_t n = Base64Encode(out, static_cast<const uint8_t *>(data), length, urlSafe);
	out[n] = '\0';
	return std::string(out, n);
}

static std::string Decode(const char *text) {
	uint8_t out[512];
	const size_t n = Base64Decode(out, reinterpret_cast<const uint8_t *>(text), std::strlen(text));
	return std::string(reinterpret_cast<const char *>(out), n);
}

static void TestRfc4648Vectors() {
	// The canonical vectors from RFC 4648 section 10.
	CHECK(Encode("", 0) == "", "empty input");
	CHECK(Encode("f", 1) == "Zg==", "RFC: f");
	CHECK(Encode("fo", 2) == "Zm8=", "RFC: fo");
	CHECK(Encode("foo", 3) == "Zm9v", "RFC: foo");
	CHECK(Encode("foob", 4) == "Zm9vYg==", "RFC: foob");
	CHECK(Encode("fooba", 5) == "Zm9vYmE=", "RFC: fooba");
	CHECK(Encode("foobar", 6) == "Zm9vYmFy", "RFC: foobar");
}

static void TestUrlSafeAlphabet() {
	// 0xFB 0xFF encodes to "+/8=" in the standard alphabet and "-_8=" url-safe.
	const unsigned char bytes[] = { 0xFB, 0xFF };
	CHECK(Encode(bytes, 2, false) == "+/8=", "standard alphabet");
	CHECK(Encode(bytes, 2, true) == "-_8=", "url-safe alphabet");
}

static void TestRoundTrip() {
	// Every byte value survives a round trip.
	unsigned char all[256];
	for (int i = 0; i < 256; ++i) {
		all[i] = static_cast<unsigned char>(i);
	}
	const std::string encoded = Encode(all, sizeof(all));
	CHECK(encoded.size() == 344, "256 bytes encode to 344 characters");
	const std::string decoded = Decode(encoded.c_str());
	CHECK(decoded.size() == sizeof(all), "decoded length matches");
	CHECK(std::memcmp(decoded.data(), all, sizeof(all)) == 0, "decoded bytes match");

	// Lengths either side of the 3-byte block boundary.
	for (size_t length = 1; length <= 64; ++length) {
		std::string input(length, '\0');
		for (size_t i = 0; i < length; ++i) {
			input[i] = static_cast<char>(i * 7 + length);
		}
		const std::string round = Decode(Encode(input.data(), input.size()).c_str());
		CHECK(round == input, "round trip at every length up to 64");
	}
}

static void TestDecodeStopsAtNonAlphabet() {
	// Decoding ignores whatever follows the payload rather than mis-decoding it.
	CHECK(Decode("Zm9vYmFy") == "foobar", "clean payload decodes");
	const std::string withTrailing = Decode("Zm9vYmFy!!!");
	CHECK(withTrailing == "foobar", "trailing garbage is ignored");
}

int main() {
	TestRfc4648Vectors();
	TestUrlSafeAlphabet();
	TestRoundTrip();
	TestDecodeStopsAtNonAlphabet();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}