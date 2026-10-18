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

/// Default cache root: %TEMP%\notepad420-Paste. A custom root (ADR 0014,
/// ini `PasteCacheDir`) takes precedence until cleared; it is stored verbatim
/// (env-var expansion belongs to the settings layer) and is not validated here.
bool PasteCacheGetDir(WCHAR (&dir)[MAX_PATH]) noexcept;

/// The default root alone (ignores any custom root): what the management
/// dialog's "Default" button writes back into the editable path field.
bool PasteCacheGetDefaultDir(WCHAR (&dir)[MAX_PATH]) noexcept;

/// Sets (or, with an empty string, clears) the custom cache root. Passing a
/// string longer than MAX_PATH fails without changing the previous value.
bool PasteCacheSetCustomDir(LPCWSTR dir) noexcept;

/// Returns the custom cache root when one is set (false otherwise). Unlike
/// PasteCacheGetDir() it never falls back to the default root, so the caller
/// can decide whether the value differs from the default.
bool PasteCacheGetCustomDir(WCHAR (&dir)[MAX_PATH]) noexcept;

/// Creates the directory (and parents) when missing. Idempotent.
bool PasteCacheEnsureDir(LPCWSTR dir) noexcept;

/// Occupancy, counted by enumerating the directory so the figure is consistent
/// across processes and restarts (never a per-process counter).
PasteCacheUsage PasteCacheMeasure(LPCWSTR dir) noexcept;

//=============================================================================
//
// Cache management (task 002 / ADR 0014)
//

/// True when `fileName` carries the cache naming shape
/// `YYYYMMDD-HHmmss-fff_N.<ext>` with an image extension (png jpg jpeg gif bmp
/// webp, case-insensitive). Calendar-invalid dates still pass: they stay
/// cache files whose age falls back to mtime. Foreign files in a custom cache
/// directory never match and are never touched by the cleanup.
bool PasteCacheIsCacheFileName(LPCWSTR fileName) noexcept;

/// Outcome of one cleanup pass over a cache directory (non-recursive).
struct PasteCacheCleanResult {
	unsigned deletedCount;
	unsigned long long deletedBytes;
	unsigned failedCount;	// matched but undeletable (locked): skipped quietly
};

/// Deletes cache-named files older than `olderThanDays` calendar days
/// (embedded filename date first, mtime as fallback); 0 means every
/// cache-named file. `todayYYYYMMDD` anchors the age computation. A missing
/// or unreadable directory is a quiet zero result (startup safety); files
/// that fail to delete count as failed and do not abort the pass.
PasteCacheCleanResult PasteCacheCleanup(LPCWSTR dir, unsigned olderThanDays,
										unsigned todayYYYYMMDD) noexcept;

/// Same enumeration and age rules as PasteCacheCleanup but nothing is
/// deleted: the confirmation prompt shows these numbers before the user
/// commits to the real pass.
PasteCacheCleanResult PasteCachePreviewCleanup(LPCWSTR dir, unsigned olderThanDays,
										   unsigned todayYYYYMMDD) noexcept;

/// Auto-clean throttle (calendar days): true when a pass should run now.
/// `lastYYYYMMDD` 0 means never cleaned. A future or garbage last-run
/// timestamp silently suppresses the pass until time catches up.
bool PasteCacheThrottleElapsed(unsigned lastYYYYMMDD, unsigned todayYYYYMMDD,
							   unsigned intervalDays) noexcept;

/// Reads a user-entered age in days (the manual cleanup field and the
/// automatic age/interval fields). Anything that is not a positive integer -
/// an emptied box, "0", pure garbage - falls back to `fallback`. ES_NUMBER
/// already blocks letters at the UI, so this covers the cleared-box and pasted
/// text cases. It never returns 0: zero days means "delete everything", which
/// must stay reachable only through the explicit "all files" checkbox.
int PasteCacheParseAgeDays(LPCWSTR text, int fallback) noexcept;

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
