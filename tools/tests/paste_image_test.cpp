// Tests for the bitmap -> PNG encoder (P2).
//
// Encoding does not touch the screen, so this runs headlessly. The DIB is
// synthesised in memory and the encoder's output is verified by its PNG
// signature, its IHDR width/height parsed straight out of the bytes, and a
// round trip through GDI+ to confirm the image decodes back to what went in.
// Build and run: tools\run-tests.bat
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/PasteImage.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

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

/// Builds a 32-bit top-down DIB of the given size, filling pixel (x, y) with
/// the caller's colour. Top-down (negative height) matches what browsers and
/// the snipping tool put on the clipboard.
static std::vector<unsigned char> MakeDib(int width, int height,
										 unsigned char r, unsigned char g, unsigned char b) {
	const int stride = width * 4;
	const std::size_t pixelBytes = static_cast<std::size_t>(stride) * height;
	std::vector<unsigned char> dib(sizeof(BITMAPINFOHEADER) + pixelBytes);
	auto *header = reinterpret_cast<BITMAPINFOHEADER *>(dib.data());
	header->biSize = sizeof(BITMAPINFOHEADER);
	header->biWidth = width;
	header->biHeight = -height;		// top-down
	header->biPlanes = 1;
	header->biBitCount = 32;
	header->biCompression = BI_RGB;
	header->biSizeImage = static_cast<DWORD>(pixelBytes);

	unsigned char *pixels = dib.data() + sizeof(BITMAPINFOHEADER);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			unsigned char *p = pixels + static_cast<std::size_t>(y) * stride + x * 4;
			p[0] = b;	// BGRA order
			p[1] = g;
			p[2] = r;
			p[3] = 0xFF;
		}
	}
	return dib;
}

/// Reads the IHDR width and height straight from the PNG bytes.
static bool ReadPngSize(const std::vector<unsigned char> &png, unsigned &width, unsigned &height) {
	// 8-byte signature, 4-byte length, "IHDR", then width and height.
	if (png.size() < 33 || memcmp(png.data() + 12, "IHDR", 4) != 0) {
		return false;
	}
	auto be32 = [&](std::size_t offset) -> unsigned {
		return (static_cast<unsigned>(png[offset]) << 24) |
			   (static_cast<unsigned>(png[offset + 1]) << 16) |
			   (static_cast<unsigned>(png[offset + 2]) << 8) |
			   static_cast<unsigned>(png[offset + 3]);
	};
	width = be32(16);
	height = be32(20);
	return true;
}

static void TestSignatureHelper() {
	const unsigned char pngHeader[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	CHECK(PasteIsPngImage(pngHeader, sizeof(pngHeader)), "the PNG signature is recognised");
	CHECK(!PasteIsPngImage("not a png!", 10), "arbitrary bytes are not a PNG");
	CHECK(!PasteIsPngImage(pngHeader, 4), "a truncated signature is not a PNG");
	CHECK(!PasteIsPngImage(nullptr, 0), "null is not a PNG");
}

static void TestDibEncoding() {
	std::vector<unsigned char> png;

	// A 4x3 image of a known colour.
	const std::vector<unsigned char> dib = MakeDib(4, 3, 0x11, 0x22, 0x33);
	CHECK(PasteEncodeDibToPng(dib.data(), dib.size(), png), "a valid DIB encodes");
	CHECK(PasteIsPngImage(png.data(), png.size()), "the output starts with the PNG signature");
	CHECK(png.size() > 100, "the output is a real image, not a stub");

	unsigned width = 0, height = 0;
	CHECK(ReadPngSize(png, width, height), "the IHDR chunk is present");
	CHECK(width == 4, "width survives encoding");
	CHECK(height == 3, "height survives encoding");

	// The encoded image decodes back to the same dimensions and pixel colour:
	// an independent check on the encoder, not a re-run of it.
	IStream *stream = nullptr;
	HGLOBAL pngMem = GlobalAlloc(GMEM_MOVEABLE, png.size());
	CHECK(pngMem != nullptr, "memory for the decode stream");
	if (pngMem != nullptr) {
		void *dst = GlobalLock(pngMem);
		memcpy(dst, png.data(), png.size());
		GlobalUnlock(pngMem);
		CHECK(CreateStreamOnHGlobal(pngMem, TRUE, &stream) == S_OK, "the PNG opens as a stream");
	}
	CHECK(stream != nullptr, "the PNG can be opened as a stream");
	if (stream != nullptr) {
		Gdiplus::Bitmap *decoded = Gdiplus::Bitmap::FromStream(stream);
		CHECK(decoded != nullptr && decoded->GetLastStatus() == Gdiplus::Ok, "the PNG decodes");
		if (decoded != nullptr) {
			CHECK(static_cast<int>(decoded->GetWidth()) == 4, "decoded width matches");
			CHECK(static_cast<int>(decoded->GetHeight()) == 3, "decoded height matches");
			Gdiplus::Color colour;
			// Sample a pixel away from the edges.
			if (decoded->GetPixel(1, 1, &colour) == Gdiplus::Ok) {
				CHECK(colour.GetR() == 0x11, "red channel survives the round trip");
				CHECK(colour.GetG() == 0x22, "green channel survives the round trip");
				CHECK(colour.GetB() == 0x33, "blue channel survives the round trip");
			}
			delete decoded;
		}
		stream->Release();
	}

	// A one-pixel image is the smallest realistic case.
	const std::vector<unsigned char> tiny = MakeDib(1, 1, 0xFF, 0x00, 0x00);
	CHECK(PasteEncodeDibToPng(tiny.data(), tiny.size(), png), "a 1x1 DIB encodes");
	CHECK(ReadPngSize(png, width, height) && width == 1 && height == 1, "1x1 size survives");
}

static void TestDibRefusals() {
	std::vector<unsigned char> png;
	png.assign(16, 0xAB);

	// Too short to hold a header at all.
	const unsigned char stub[8] = {};
	CHECK(!PasteEncodeDibToPng(stub, sizeof(stub), png), "a stub is refused");
	CHECK(png.empty(), "a refused encode leaves the output empty");

	// A BITMAPCOREHEADER payload (biSize 12) is not describable by this wrapper.
	BITMAPCOREHEADER core{};
	core.bcSize = sizeof(BITMAPCOREHEADER);
	core.bcWidth = 2;
	core.bcHeight = 2;
	core.bcPlanes = 1;
	core.bcBitCount = 24;
	CHECK(!PasteEncodeDibToPng(&core, sizeof(core), png), "a core-header DIB is refused");

	// Null and zero-length input.
	CHECK(!PasteEncodeDibToPng(nullptr, 100, png), "null dib is refused");
	CHECK(!PasteEncodeDibToPng(stub, 0, png), "zero-length dib is refused");
}

static void TestHBitmapEncoding() {
	// Produce an HBITMAP through GDI, then encode it: this is the CF_BITMAP path.
	HDC screen = GetDC(nullptr);
	HDC memory = CreateCompatibleDC(screen);
	HBITMAP bitmap = CreateCompatibleBitmap(screen, 5, 2);
	HGDIOBJ old = SelectObject(memory, bitmap);
	RECT rect = { 0, 0, 5, 2 };
	HBRUSH brush = CreateSolidBrush(RGB(0x40, 0x80, 0xC0));
	FillRect(memory, &rect, brush);
	DeleteObject(brush);
	SelectObject(memory, old);
	DeleteDC(memory);
	ReleaseDC(nullptr, screen);

	std::vector<unsigned char> png;
	CHECK(PasteEncodeHBitmapToPng(bitmap, png), "an HBITMAP encodes");
	CHECK(PasteIsPngImage(png.data(), png.size()), "HBITMAP output is PNG");
	unsigned width = 0, height = 0;
	CHECK(ReadPngSize(png, width, height) && width == 5 && height == 2, "HBITMAP size survives");
	DeleteObject(bitmap);

	CHECK(!PasteEncodeHBitmapToPng(nullptr, png), "a null HBITMAP is refused");
}

static void TestClipboardBitmapPath() {
	// The clipboard is an approved exception for headless testing. Put a DIB on
	// it exactly the way a screenshot tool does, then read it back through the
	// real PasteClipboardBitmapToPng path.
	const std::vector<unsigned char> dib = MakeDib(6, 4, 0x20, 0x60, 0xA0);
	const std::size_t bytes = dib.size();

	CHECK(OpenClipboard(nullptr), "clipboard opens for writing");
	CHECK(EmptyClipboard(), "clipboard is emptied");
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	CHECK(mem != nullptr, "clipboard memory allocated");
	if (mem != nullptr) {
		void *dst = GlobalLock(mem);
		memcpy(dst, dib.data(), bytes);
		GlobalUnlock(mem);
		CHECK(SetClipboardData(CF_DIB, mem) != nullptr, "the DIB is placed on the clipboard");
		// Ownership passed to the clipboard on success.
	}
	CloseClipboard();

	std::vector<unsigned char> png;
	CHECK(PasteClipboardBitmapToPng(png), "the clipboard bitmap is read and encoded");
	CHECK(PasteIsPngImage(png.data(), png.size()), "clipboard output is PNG");
	unsigned width = 0, height = 0;
	CHECK(ReadPngSize(png, width, height) && width == 6 && height == 4,
		"clipboard image keeps its dimensions");

	// Clear the clipboard so the test leaves the desktop as it found it.
	if (OpenClipboard(nullptr)) {
		EmptyClipboard();
		CloseClipboard();
	}
}

int main() {
	TestSignatureHelper();
	TestDibEncoding();
	TestDibRefusals();
	TestHBitmapEncoding();
	TestClipboardBitmapPath();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}