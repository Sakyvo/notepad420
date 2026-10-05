/******************************************************************************
*
* notepad420
*
* PastePolicy.h
*   Clipboard paste policy: tier arbitration, cache cap, naming and MIME lookup.
*
*   Deliberately free of Windows headers so the policy can be unit tested
*   outside the GUI (see tools/tests/paste_policy_test.cpp). The Win32 sides
*   (clipboard access, GDI+ encoding, file IO) live in src/Paste.cpp and feed
*   these functions their inputs.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include "Base64.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

//=============================================================================
//
// Tier arbitration
//

/// Which paste path a Ctrl+V (or Ctrl+Shift+V) should take. Only one tier runs
/// per paste; see CONTEXT.md "粘贴仲裁" (paste arbitration).
enum class PasteTier {
	FileList,			// P1: CF_HDROP -> insert absolute paths
	Bitmap,				// P2: CF_DIB/CF_DIBV5 -> cache as PNG, insert path
	EmbeddedDataUri,	// P4: text containing data:image/...;base64 -> cache, insert path
	PlainText,			// fall through to Scintilla's own paste
};

/// What the clipboard currently offers, probed before arbitration.
struct ClipboardKinds {
	bool hasFileList;		// CF_HDROP
	bool hasBitmap;			// CF_DIB, CF_DIBV5 or a registered image format
	bool hasText;			// CF_UNICODETEXT
	bool textHasDataUri;	// hasText and the text matches the data URI pattern
};

/// Priority: file list > bitmap > embedded data URI > plain text.
/// On the reverse path (Ctrl+Shift+V) text is never reclassified, so embedded
/// data URI text stays PlainText and is inserted verbatim.
inline PasteTier PasteChooseTier(const ClipboardKinds &kinds, bool reverse = false) noexcept {
	if (kinds.hasFileList) {
		return PasteTier::FileList;
	}
	if (kinds.hasBitmap) {
		return PasteTier::Bitmap;
	}
	if (!reverse && kinds.hasText && kinds.textHasDataUri) {
		return PasteTier::EmbeddedDataUri;
	}
	return PasteTier::PlainText;
}

//=============================================================================
//
// Paste cache cap
//

/// Paste cache guard rails: 64 images / 128 MiB. Reaching the cap refuses the
/// paste with a status bar hint; nothing is ever evicted automatically.
constexpr std::size_t PasteCapCount = 64;
constexpr std::uint64_t PasteCapBytes = 128ull * 1024 * 1024;

/// Current occupancy of %TEMP%\notepad420-Paste, counted by enumerating the
/// directory so the figure survives process restarts.
struct PasteCacheUsage {
	std::size_t count;
	std::uint64_t bytes;
};

enum class PasteCapVerdict {
	Ok,
	Full,
};

/// Whether `addCount` more images totalling `addBytes` still fit.
inline PasteCapVerdict PasteCheckCap(const PasteCacheUsage &usage,
									 std::size_t addCount, std::uint64_t addBytes) noexcept {
	if (usage.count + addCount > PasteCapCount) {
		return PasteCapVerdict::Full;
	}
	if (usage.bytes + addBytes > PasteCapBytes) {
		return PasteCapVerdict::Full;
	}
	return PasteCapVerdict::Ok;
}

//=============================================================================
//
// File naming and MIME lookup
//

/// `YYYYMMDD-HHmmss-fff_N.ext`; the process-local index disambiguates images
/// written within the same millisecond.
inline int PasteFormatFileName(char *buf, std::size_t cchBuf,
							   int year, int month, int day,
							   int hour, int minute, int second, int millisecond,
							   unsigned index, const char *extension) noexcept {
	return std::snprintf(buf, cchBuf, "%04d%02d%02d-%02d%02d%02d-%03d_%u%s",
						 year, month, day, hour, minute, second, millisecond, index,
						 (extension != nullptr) ? extension : "");
}

/// Cheap pre-check before running the data URI pattern over pasted text. Keeps
/// ordinary pastes free of regex cost: no "data:image/" substring, no scan.
/// Case sensitive, matching the canonical spelling browsers emit.
inline bool PasteTextContainsDataUri(const char *utf8, std::size_t length) noexcept {
	static const char kNeedle[] = "data:image/";
	constexpr std::size_t kNeedleLen = sizeof(kNeedle) - 1;
	if (utf8 == nullptr || length < kNeedleLen) {
		return false;
	}
	const std::size_t last = length - kNeedleLen;
	for (std::size_t i = 0; i <= last; ++i) {
		if (utf8[i] == 'd' && std::memcmp(utf8 + i, kNeedle, kNeedleLen) == 0) {
			return true;
		}
	}
	return false;
}

/// Same pre-check over the clipboard's native wide string, so probing does not
/// need a UTF-8 conversion of a possibly huge paste.
inline bool PasteTextContainsDataUriW(const wchar_t *text, std::size_t length) noexcept {
	static const wchar_t kNeedle[] = L"data:image/";
	constexpr std::size_t kNeedleLen = (sizeof(kNeedle) / sizeof(kNeedle[0])) - 1;
	if (text == nullptr || length < kNeedleLen) {
		return false;
	}
	const std::size_t last = length - kNeedleLen;
	for (std::size_t i = 0; i <= last; ++i) {
		if (text[i] == L'd' && wmemcmp(text + i, kNeedle, kNeedleLen) == 0) {
			return true;
		}
	}
	return false;
}

/// MIME type for a file extension (lower case, ASCII). Unknown and empty
/// extensions fall back to application/octet-stream.
inline const char *PasteExtensionToMime(const wchar_t *extension) noexcept {
	static const struct {
		const wchar_t *extension;
		const char *mime;
	} kTable[] = {
		{ L".png",  "image/png" },
		{ L".jpg",  "image/jpeg" },
		{ L".jpeg", "image/jpeg" },
		{ L".gif",  "image/gif" },
		{ L".webp", "image/webp" },
		{ L".bmp",  "image/bmp" },
		{ L".avif", "image/avif" },
		{ L".svg",  "image/svg+xml" },
		{ L".ico",  "image/vnd.microsoft.icon" },
	};

	if (extension == nullptr) {
		return "application/octet-stream";
	}
	for (const auto &entry : kTable) {
		const wchar_t *a = extension;
		const wchar_t *b = entry.extension;
		while (*a != L'\0' && *b != L'\0') {
			wchar_t ca = *a;
			wchar_t cb = *b;
			if (ca >= L'A' && ca <= L'Z') {
				ca = static_cast<wchar_t>(ca - L'A' + L'a');
			}
			if (ca != cb) {
				break;
			}
			++a;
			++b;
		}
		if (*a == L'\0' && *b == L'\0') {
			return entry.mime;
		}
	}
	return "application/octet-stream";
}
//=============================================================================
//
// Embedded data URI scanning (P4)
//

/// One `data:image/<type>;base64,<payload>` occurrence found in pasted text,
/// with byte offsets into the scanned buffer so the caller can splice a file
/// path in its place. Offsets are half-open: [start, end) covers the whole URI;
/// [payloadStart, payloadEnd) covers just the base64 run.
struct DataUriMatch {
	std::size_t start;
	std::size_t end;
	std::size_t payloadStart;
	std::size_t payloadEnd;
	const char *extension;	// ASCII extension to save the decoded bytes with, ".png"
};

/// Accepted payload types, from the decision list in the spec
/// (png|jpe?g|gif|webp|bmp|avif|svg+xml). Returns nullptr for anything else:
/// an unaccepted type is left alone in the text rather than converted.
inline const char *PasteDataUriExtensionForType(const char *type, std::size_t typeLen) noexcept {
	struct { const char *type; const char *extension; } kTypes[] = {
		{ "png",      ".png"  },
		{ "jpeg",     ".jpg"  },
		{ "jpg",      ".jpg"  },
		{ "gif",      ".gif"  },
		{ "webp",     ".webp" },
		{ "bmp",      ".bmp"  },
		{ "avif",     ".avif" },
		{ "svg+xml",  ".svg"  },
		{ "x-icon",   ".ico"  },
		{ "vnd.microsoft.icon", ".ico" },
	};
	for (const auto &entry : kTypes) {
		const std::size_t len = std::strlen(entry.type);
		if (len != typeLen) {
			continue;
		}
		bool same = true;
		for (std::size_t i = 0; i < len; ++i) {
			char a = type[i];
			if (a >= 'A' && a <= 'Z') {
				a = static_cast<char>(a - 'A' + 'a');
			}
			if (a != entry.type[i]) {
				same = false;
				break;
			}
		}
		if (same) {
			return entry.extension;
		}
	}
	return nullptr;
}

/// ASCII space/tab/newline/CR, which wrapped base64 payloads may contain.
inline bool PasteIsBase64Space(char c) noexcept {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// Whitespace that may appear *inside* a payload: the line wrapping that MIME
/// and HTML sources use for long base64 runs. A plain space is deliberately not
/// included — it terminates the payload, so prose following a bare URI on the
/// same line is never swallowed into it.
inline bool PasteIsPayloadWrapping(char c) noexcept {
	return c == '\t' || c == '\r' || c == '\n';
}

/// Strict base64 alphabet, plus '=' padding.
inline bool PasteIsBase64Char(char c) noexcept {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		   (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

/// Byte that may appear inside a payload: base64 or line wrapping.
inline bool PasteIsPayloadByte(char c) noexcept {
	return PasteIsBase64Char(c) || PasteIsPayloadWrapping(c);
}

/// Scans for the first acceptable data URI at or after `from`. Returns false
/// when there is none left. The match is accepted only when the header parses
/// and at least one payload character follows the comma; an unterminated run of
/// base64 characters ends at the first character that cannot belong to it.
inline bool PasteFindDataUri(const char *text, std::size_t length,
							 std::size_t from, DataUriMatch &match) noexcept {
	static const char kPrefix[] = "data:image/";
	constexpr std::size_t kPrefixLen = 11;
	if (text == nullptr) {
		return false;
	}

	std::size_t i = from;
	while (i + kPrefixLen <= length) {
		if (std::memcmp(text + i, kPrefix, kPrefixLen) != 0) {
			++i;
			continue;
		}

		// Media type up to the ';'.
		std::size_t typeStart = i + kPrefixLen;
		std::size_t j = typeStart;
		while (j < length && text[j] != ';' && text[j] != ',') {
			++j;
		}
		if (j >= length || text[j] != ';') {
			++i;
			continue;
		}

		// Only the base64 encoding is handled.
		static const char kBase64[] = "base64,";
		constexpr std::size_t kBase64Len = 7;
		if (j + kBase64Len > length || std::memcmp(text + j + 1, kBase64, kBase64Len) != 0) {
			++i;
			continue;
		}

		const char *extension = PasteDataUriExtensionForType(text + typeStart, j - typeStart);
		if (extension == nullptr) {
			++i;	// unaccepted type: leave the text alone and keep scanning
			continue;
		}

		const std::size_t payloadStart = j + 1 + kBase64Len;
		std::size_t payloadEnd = payloadStart;
		while (payloadEnd < length && PasteIsPayloadByte(text[payloadEnd])) {
			++payloadEnd;
		}
		// Trailing wrapping belongs to the delimiter, not to the payload.
		while (payloadEnd > payloadStart && PasteIsPayloadWrapping(text[payloadEnd - 1])) {
			--payloadEnd;
		}
		if (payloadEnd == payloadStart) {
			++i;
			continue;
		}

		match.start = i;
		match.end = payloadEnd;
		match.payloadStart = payloadStart;
		match.payloadEnd = payloadEnd;
		match.extension = extension;
		return true;
	}
	return false;
}

/// Copies the base64 payload of `match` (dropping ASCII whitespace) into `out`,
/// NUL-terminating it. Returns the payload length, or 0 when it does not fit.
/// Decoding is left to the existing Base64Decode() in Helpers.h.
inline std::size_t PasteCopyBase64Payload(const char *text, const DataUriMatch &match,
										  char *out, std::size_t cchOut) noexcept {
	if (out == nullptr || cchOut == 0) {
		return 0;
	}
	std::size_t written = 0;
	for (std::size_t i = match.payloadStart; i < match.payloadEnd; ++i) {
		const char c = text[i];
		if (PasteIsPayloadWrapping(c)) {
			continue;
		}
		if (written + 1 >= cchOut) {
			out[0] = '\0';
			return 0;
		}
		out[written++] = c;
	}
	out[written] = '\0';
	return written;
}

//=============================================================================
//
// Inserted text assembly (P1/P3)
//

/// Joins the paths to insert, one per line, with no quoting: in a plain text
/// editor a path is just text, so quotes would only be noise. LOSES data when
/// `cchOut` is too small instead of truncating silently.
inline bool PasteJoinInsertedPaths(wchar_t *out, std::size_t cchOut,
								   const wchar_t *const *paths, std::size_t count) noexcept {
	if (out == nullptr || cchOut == 0 || (count != 0 && paths == nullptr)) {
		return false;
	}
	std::size_t written = 0;
	for (std::size_t i = 0; i < count; ++i) {
		if (paths[i] == nullptr) {
			continue;
		}
		if (written != 0) {
			if (written + 1 >= cchOut) {
				out[0] = L'\0';
				return false;
			}
			out[written++] = L'\r';
			if (written + 1 >= cchOut) {
				out[0] = L'\0';
				return false;
			}
			out[written++] = L'\n';
		}
		for (const wchar_t *p = paths[i]; *p != L'\0'; ++p) {
			if (written + 1 >= cchOut) {
				out[0] = L'\0';
				return false;
			}
			out[written++] = *p;
		}
	}
	out[written] = L'\0';
	return true;
}

/// `data:<mime>;base64,` — the header that a reverse-paste payload carries.
inline std::size_t PasteBuildDataUriHeader(char *out, std::size_t cchOut,
										   const char *mime) noexcept {
	if (out == nullptr || cchOut == 0) {
		return 0;
	}
	out[0] = '\0';
	if (mime == nullptr || *mime == '\0') {
		mime = "application/octet-stream";
	}
	const int n = std::snprintf(out, cchOut, "data:%s;base64,", mime);
	if (n < 0 || static_cast<std::size_t>(n) >= cchOut) {
		out[0] = '\0';
		return 0;
	}
	return static_cast<std::size_t>(n);
}

//=============================================================================
//
// File-list paste limits
//

/// Safety valve for a clipboard file list. Beyond this the paste is refused
/// outright rather than producing an enormous insert. Well above any realistic
/// selection.
constexpr std::size_t PasteMaxFileCount = 1024;

inline bool PasteCheckFileCount(std::size_t count) noexcept {
	return count > 0 && count <= PasteMaxFileCount;
}

/// Joins already-rendered ASCII data URIs into one CRLF-separated line list.
/// Returns false when `count` is 0, any URI is null, or `out` is too small.
inline bool PasteJoinDataUris(wchar_t *out, std::size_t cchOut,
							  const char *const *uris, std::size_t count) noexcept {
	if (out == nullptr || cchOut == 0 || count == 0 || uris == nullptr) {
		return false;
	}
	std::size_t written = 0;
	for (std::size_t i = 0; i < count; ++i) {
		if (uris[i] == nullptr || *uris[i] == '\0') {
			continue;
		}
		if (written != 0) {
			if (written + 2 >= cchOut) {
				out[0] = L'\0';
				return false;
			}
			out[written++] = L'\r';
			out[written++] = L'\n';
		}
		for (const char *p = uris[i]; *p != '\0'; ++p) {
			if (written + 1 >= cchOut) {
				out[0] = L'\0';
				return false;
			}
			out[written++] = static_cast<wchar_t>(static_cast<unsigned char>(*p));
		}
	}
	if (written == 0) {
		out[0] = L'\0';
		return false;
	}
	out[written] = L'\0';
	return true;
}

//=============================================================================
//
// P4 rewrite engine
//

/// Receives each decoded image and returns where it was cached, or reports that
/// the caller could not store it. Returning false stops the rewrite.
using PasteUriSink = bool (*)(void *context, const void *bytes, std::size_t length,
							  const char *extension, std::string &outPath);

/// Rewrites every embedded image data URI in `text` into the path the sink
/// stored it at. Writes the transformed text into `out` and reports how many
/// URIs were converted; an image the sink refuses (a full cache) is left as the
/// original URI text and every later one is left alone too, so the document
/// keeps a clean prefix/remainder split rather than a scatter of conversions.
///
/// Returns false only when the output could not be produced (a malformed URI or
/// a short buffer); the caller then leaves the text untouched.
bool PasteRewriteDataUris(const char *text, std::size_t length, void *sinkContext,
						  PasteUriSink sink, std::string &out,
						  std::size_t *converted, std::size_t *refused) noexcept;

/// Decodes a base64 payload and hands the bytes to `sink`. Returns false when
/// the payload is malformed (odd length after padding removal) or would decode
/// to more than `cchDecoded` bytes.
inline bool PasteDecodePayloadAndSink(const char *text, const DataUriMatch &match,
									  void *sinkContext, PasteUriSink sink,
									  std::string &outPath) noexcept {
	outPath.clear();
	const std::size_t base64Chars = match.payloadEnd - match.payloadStart;
	if (base64Chars == 0 || (base64Chars & 3u) != 0) {
		return false;
	}
	const std::size_t capacity = (base64Chars / 4) * 3;
	std::vector<uint8_t> decoded(capacity);

	// Compact the payload, dropping the line wrapping base64 may carry.
	std::string compact;
	compact.reserve(base64Chars);
	for (std::size_t i = match.payloadStart; i < match.payloadEnd; ++i) {
		const char c = text[i];
		if (!PasteIsPayloadWrapping(c)) {
			compact += c;
		}
	}
	const std::size_t decodedSize = Base64Decode(decoded.data(),
			reinterpret_cast<const uint8_t *>(compact.data()), compact.size());
	if (decodedSize == 0) {
		return false;
	}
	return sink(sinkContext, decoded.data(), decodedSize, match.extension, outPath);
}

inline bool PasteRewriteDataUris(const char *text, std::size_t length, void *sinkContext,
								 PasteUriSink sink, std::string &out,
								 std::size_t *converted, std::size_t *refused) noexcept {
	out.clear();
	if (converted != nullptr) {
		*converted = 0;
	}
	if (refused != nullptr) {
		*refused = 0;
	}
	if (text == nullptr || length == 0 || sink == nullptr) {
		return false;
	}

	// Cheap pre-check: no candidate substring means the text is untouched.
	if (!PasteTextContainsDataUri(text, length)) {
		out.assign(text, length);
		return true;
	}

	out.reserve(length);
	std::size_t cursor = 0;
	std::size_t convertedCount = 0;
	std::size_t refusedCount = 0;
	bool stopping = false;

	while (cursor < length) {
		DataUriMatch match;
		if (stopping || !PasteFindDataUri(text, length, cursor, match)) {
			break;
		}
		out.append(text + cursor, match.start - cursor);

		std::string path;
		if (PasteDecodePayloadAndSink(text, match, sinkContext, sink, path)) {
			out.append(path);
			++convertedCount;
		} else {
			// The cache refused it: keep the original URI text and do not
			// convert anything after it either.
			out.append(text + match.start, match.end - match.start);
			++refusedCount;
			stopping = true;
			cursor = match.end;
			continue;
		}
		cursor = match.end;
	}
	out.append(text + cursor, length - cursor);

	if (converted != nullptr) {
		*converted = convertedCount;
	}
	if (refused != nullptr) {
		*refused = refusedCount;
	}
	return convertedCount != 0;
}

//=============================================================================
//
// Plain-text fallback rule
//

/// Whether a paste may defer to Scintilla's own text paste. Only the plain-text
/// tier may: a tier that owns the clipboard (file list, bitmap, embedded data
/// URI) must not fall back, because that would insert the very text the
/// arbitration rule discarded -- e.g. pasting the page URL instead of a refused
/// screenshot.
inline bool PasteAllowsPlainTextFallback(PasteTier tier) noexcept {
	return tier == PasteTier::PlainText;
}
