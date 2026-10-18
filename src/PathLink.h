// PathLink — path hotspot candidate extraction and resolution (task 007).
// Pure byte-level logic, no Scintilla and no direct filesystem access:
// existence goes through a caller-supplied predicate so the decision layer
// is testable and the GUI caller owns GetFileAttributes.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>

/// One candidate token found on a line, as [begin, end) byte offsets.
struct PathLinkToken {
	ptrdiff_t begin;
	ptrdiff_t end;
};

/// Find the path-token overlapping `pos` in a NUL-terminated line.
/// Recognised shapes: drive paths `C:\...` / `C:/...`, UNC `\\srv\share\...`,
/// relative paths containing a path separator, quoted variants covering the
/// quotes. Unix-style leading `/` and URLs (`scheme://`) are not path tokens.
bool PathLinkExtractAt(const char *line, ptrdiff_t pos, PathLinkToken *token);

/// Resolve a token to an existing file-system path. Tries the token as-is,
/// then `%ENV%` expansion, then relative to `baseDir`. `exists(raw, baseDir,
/// out)` is the existence predicate; on a hit it must have written the final
/// path into `out`. Returns false when the token does not resolve.
bool PathLinkResolveInto(const char *token, const char *baseDir, char (&out)[MAX_PATH * 2],
	bool (*exists)(const char *raw, const char *baseDir, char *out));
