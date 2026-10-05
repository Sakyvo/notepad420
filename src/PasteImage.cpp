/******************************************************************************
*
* notepad420
*
* PasteImage.cpp
*   See PasteImage.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include <windows.h>
#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstring>
#include <vector>
#include "PasteImage.h"

namespace {

/// The eight-byte PNG file signature.
const unsigned char kPngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusReady = false;

/// Starts GDI+ once. Returns whether it is usable.
bool EnsureGdiPlus() noexcept {
	if (g_gdiplusReady) {
		return g_gdiplusToken != 0;
	}
	g_gdiplusReady = true;
	Gdiplus::GdiplusStartupInput input;
	if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Gdiplus::Ok) {
		g_gdiplusToken = 0;
	}
	return g_gdiplusToken != 0;
}

/// Looks up the PNG encoder CLSID.
bool GetPngEncoderClsid(CLSID &clsid) noexcept {
	UINT count = 0;
	UINT bytes = 0;
	if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) {
		return false;
	}
	std::vector<unsigned char> buffer(bytes);
	Gdiplus::ImageCodecInfo *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
	if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) {
		return false;
	}
	for (UINT i = 0; i < count; ++i) {
		if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
			clsid = codecs[i].Clsid;
			return true;
		}
	}
	return false;
}

/// Saves a GDI+ image into a memory stream as PNG.
bool SavePngToMemory(Gdiplus::Image *image, std::vector<unsigned char> &png) noexcept {
	CLSID clsid;
	if (!GetPngEncoderClsid(clsid)) {
		return false;
	}
	IStream *stream = nullptr;
	if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) {
		return false;
	}
	if (stream == nullptr) {
		return false;
	}

	const Gdiplus::Status status = image->Save(stream, &clsid, nullptr);
	if (status != Gdiplus::Ok) {
		stream->Release();
		return false;
	}

	// Read the stream back out.
	STATSTG stat{};
	if (stream->Stat(&stat, STATFLAG_NONAME) != S_OK || stat.cbSize.QuadPart == 0) {
		stream->Release();
		return false;
	}
	const std::size_t size = static_cast<std::size_t>(stat.cbSize.QuadPart);
	png.resize(size);
	LARGE_INTEGER zero{};
	stream->Seek(zero, STREAM_SEEK_SET, nullptr);
	ULONG read = 0;
	const HRESULT hr = stream->Read(png.data(), static_cast<ULONG>(size), &read);
	stream->Release();
	if (FAILED(hr) || read != size) {
		png.clear();
		return false;
	}
	return true;
}

/// Wraps CF_DIB bytes in a BITMAPFILEHEADER so GDI+ can read them as a stream.
bool WrapDibAsFile(const void *dib, std::size_t length, std::vector<unsigned char> &file) noexcept {
	if (dib == nullptr || length < sizeof(BITMAPINFOHEADER)) {
		return false;
	}
	const BITMAPINFOHEADER *header = static_cast<const BITMAPINFOHEADER *>(dib);
	// A BITMAPCOREHEADER (12 bytes) payload cannot be described by this wrapper.
	if (header->biSize < sizeof(BITMAPINFOHEADER)) {
		return false;
	}

	BITMAPFILEHEADER fileHeader{};
	fileHeader.bfType = 0x4D42;	// "BM"
	fileHeader.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + header->biSize);
	if (header->biSize == sizeof(BITMAPV5HEADER)) {
		fileHeader.bfOffBits += 0;	// V5 header is already counted by biSize
	}
	fileHeader.bfSize = static_cast<DWORD>(fileHeader.bfOffBits + length - header->biSize);

	file.resize(sizeof(BITMAPFILEHEADER) + length);
	memcpy(file.data(), &fileHeader, sizeof(fileHeader));
	memcpy(file.data() + sizeof(fileHeader), dib, length);
	return true;
}

} // namespace

bool PasteIsPngImage(const void *data, std::size_t length) noexcept {
	return data != nullptr && length >= sizeof(kPngSignature) &&
		   memcmp(data, kPngSignature, sizeof(kPngSignature)) == 0;
}

bool PasteEncodeDibToPng(const void *dib, std::size_t length, std::vector<unsigned char> &png) noexcept {
	png.clear();
	if (!EnsureGdiPlus()) {
		return false;
	}

	std::vector<unsigned char> file;
	if (!WrapDibAsFile(dib, length, file)) {
		return false;
	}

	IStream *stream = nullptr;
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, file.size());
	if (mem == nullptr) {
		return false;
	}
	void *dst = GlobalLock(mem);
	if (dst == nullptr) {
		GlobalFree(mem);
		return false;
	}
	memcpy(dst, file.data(), file.size());
	GlobalUnlock(mem);
	if (CreateStreamOnHGlobal(mem, TRUE, &stream) != S_OK || stream == nullptr) {
		GlobalFree(mem);
		return false;
	}
	Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromStream(stream);
	if (bitmap == nullptr || bitmap->GetLastStatus() != Gdiplus::Ok) {
		delete bitmap;
		stream->Release();
		return false;
	}

	const bool ok = SavePngToMemory(bitmap, png);
	delete bitmap;
	stream->Release();
	return ok;
}

bool PasteEncodeHBitmapToPng(HBITMAP hBitmap, std::vector<unsigned char> &png) noexcept {
	png.clear();
	if (hBitmap == nullptr || !EnsureGdiPlus()) {
		return false;
	}
	Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromHBITMAP(hBitmap, nullptr);
	if (bitmap == nullptr || bitmap->GetLastStatus() != Gdiplus::Ok) {
		delete bitmap;
		return false;
	}
	const bool ok = SavePngToMemory(bitmap, png);
	delete bitmap;
	return ok;
}

bool PasteClipboardBitmapToPng(std::vector<unsigned char> &png) noexcept {
	png.clear();
	if (!OpenClipboard(nullptr)) {
		return false;
	}

	bool ok = false;
	// CF_DIB keeps the palette and pixel data in one block, which the encoder
	// consumes directly.
	if (IsClipboardFormatAvailable(CF_DIB)) {
		const HANDLE handle = GetClipboardData(CF_DIB);
		if (handle != nullptr) {
			const void *data = GlobalLock(handle);
			if (data != nullptr) {
				const SIZE_T size = GlobalSize(handle);
				ok = PasteEncodeDibToPng(data, size, png);
				GlobalUnlock(handle);
			}
		}
	}

	// CF_BITMAP is the fallback for producers that only publish a GDI handle.
	if (!ok && IsClipboardFormatAvailable(CF_BITMAP)) {
		const HBITMAP bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
		if (bitmap != nullptr) {
			ok = PasteEncodeHBitmapToPng(bitmap, png);
		}
	}

	CloseClipboard();
	return ok;
}