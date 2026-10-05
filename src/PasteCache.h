/******************************************************************************
*
* notepad420
*
* PasteCache.h
*   The paste cache directory (%TEMP%\notepad420-Paste): creation, naming,
*   occupancy measurement and the 64 images / 128 MiB guard rail.
*
*   Win32 only, but free of application globals, so the behaviour is exercised
*   headlessly by tools/tests/paste_cache_test.cpp instead of by hand.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <windows.h>
#include <cstddef>
#include "PastePolicy.h"

/// Default cache root: %TEMP%\notepad420-Paste.
bool PasteCacheGetDir(WCHAR (&dir)[MAX_PATH]) noexcept;

/// Creates the directory (and parents) when missing. Idempotent.
bool PasteCacheEnsureDir(LPCWSTR dir) noexcept;

/// Occupancy, counted by enumerating the directory so the figure is consistent
/// across processes and restarts (never a per-process counter).
PasteCacheUsage PasteCacheMeasure(LPCWSTR dir) noexcept;

/// Restarts the process-local disambiguation index. Called once per paste
/// command so a burst of images in one paste gets 0, 1, 2, ...
void PasteCacheResetIndex() noexcept;

/// Builds the next cache file path without writing anything:
/// `dir\YYYYMMDD-HHmmss-fff_N.ext`, advancing the index.
bool PasteCacheNextPath(LPCWSTR dir, const char *extension, WCHAR (&path)[MAX_PATH]) noexcept;

/// Writes `length` bytes to a fresh cache file and reports where it landed.
/// Refuses (returning Full without creating a file) when the cap is reached:
/// nothing is ever evicted to make room.
PasteCapVerdict PasteCacheWrite(LPCWSTR dir, const void *data, std::size_t length,
								const char *extension, WCHAR (&outPath)[MAX_PATH]) noexcept;
/// Same as PasteCacheWrite(), but for a caller that must distinguish a full
/// cache from an unwritable directory. `full` is set only when the cap is what
/// refused the write, which is the case the user has to act on.
PasteCapVerdict PasteCacheWriteEx(LPCWSTR dir, const void *data, std::size_t length,
								 const char *extension, WCHAR (&outPath)[MAX_PATH],
								 bool &full) noexcept;
