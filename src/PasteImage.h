/******************************************************************************
*
* notepad420
*
* PasteImage.h
*   DIB/HBITMAP -> PNG encoding for the bitmap paste tier (P2/P3).
*
*   GDI+ is started lazily and shut down at process exit. Encoding itself does
*   not touch the screen, so it is exercised headlessly by
*   tools/tests/paste_image_test.cpp.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>

/// Encodes the clipboard's CF_DIB payload (a BITMAPINFOHEADER followed by
/// pixels) as a PNG file image. Returns false when the DIB is malformed or the
/// PNG encoder is unavailable.
bool PasteEncodeDibToPng(const void *dib, std::size_t length, std::vector<unsigned char> &png) noexcept;

/// Encodes an HBITMAP as PNG. The caller keeps ownership of the bitmap.
bool PasteEncodeHBitmapToPng(HBITMAP hBitmap, std::vector<unsigned char> &png) noexcept;

/// Reads the clipboard bitmap (CF_DIB / CF_DIBV5 / CF_BITMAP) as PNG. The
/// clipboard must not be open when this is called. Returns false when the
/// clipboard carries no usable bitmap.
bool PasteClipboardBitmapToPng(std::vector<unsigned char> &png) noexcept;

/// True when the encoded bytes start with the PNG signature. Used to validate
/// encoder output without a decoder.
bool PasteIsPngImage(const void *data, std::size_t length) noexcept;