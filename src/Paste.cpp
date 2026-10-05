/******************************************************************************
*
* notepad420
*
* Paste.cpp
*   See Paste.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <cstring>
#include <string>
#include <vector>
#include "SciCall.h"
#include "Helpers.h"
#include "Paste.h"
#include "PasteCache.h"
#include "PasteFile.h"
#include "PasteImage.h"
#include "KeditBridge.h"
#include "Notepad4.h"
#include "resource.h"

extern HWND hwndStatus;

namespace {

/// Upper bound on the amount of clipboard text scanned for an embedded data
/// URI. Keeps an accidentally huge clipboard from being copied twice.
constexpr std::size_t kTextProbeCap = 16u * 1024 * 1024;

bool ClipboardHasBitmapFormat() noexcept {
	if (IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) ||
		IsClipboardFormatAvailable(CF_BITMAP)) {
		return true;
	}
	// Browsers on some builds publish PNG through a registered format.
	const UINT pngFormat = RegisterClipboardFormat(L"PNG");
	return pngFormat != 0 && IsClipboardFormatAvailable(pngFormat);
}

/// Reads CF_UNICODETEXT and reports whether it carries a data URI. The global
/// memory handle is only locked, never freed, because it belongs to the
/// clipboard owner.
bool ClipboardTextHasDataUri() noexcept {
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
		return false;
	}
	const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
	if (handle == nullptr) {
		return false;
	}
	const WCHAR *text = static_cast<const WCHAR *>(GlobalLock(handle));
	if (text == nullptr) {
		return false;
	}

	// Find the length without trusting a NUL to appear within the allocation.
	const SIZE_T maxChars = GlobalSize(handle) / sizeof(WCHAR);
	std::size_t length = 0;
	while (length < maxChars && length * sizeof(WCHAR) < kTextProbeCap && text[length] != L'\0') {
		++length;
	}
	const bool found = PasteTextContainsDataUriW(text, length);
	GlobalUnlock(handle);
	return found;
}

} // namespace

namespace {

/// Inserts UTF-8 text at the caret as one undo step.
void InsertUtf8Text(const std::string &utf8) noexcept;

/// Converts wide text to UTF-8 and inserts it.
void InsertPlainText(const wchar_t *text) noexcept {
	if (text == nullptr || *text == L'\0') {
		return;
	}
	const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (bytes <= 1) {
		return;
	}
	std::string utf8(static_cast<std::size_t>(bytes - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), bytes, nullptr, nullptr);
	InsertUtf8Text(utf8);
}


/// Caches encoded PNG bytes and returns the path, or reports a full cache.
/// `cacheFull` distinguishes "the cap is reached" (which the user must act on)
/// from "the clipboard had no usable image".
bool CachePngOrReport(const std::vector<unsigned char> &png, std::wstring &path, bool &cacheFull) noexcept {
	cacheFull = false;
	WCHAR dir[MAX_PATH];
	if (!PasteCacheGetDir(dir)) {
		return false;
	}
	WCHAR written[MAX_PATH];
	const PasteCapVerdict verdict = PasteCacheWriteEx(dir, png.data(), png.size(), ".png", written, cacheFull);
	if (verdict != PasteCapVerdict::Ok || cacheFull) {
		return false;
	}
	path.assign(written);
	return true;
}


/// Stores a decoded P4 image in the paste cache. Returns false when the cap is
/// reached, which the engine turns into "leave the rest as text".
bool CacheDecodedImageSink(void *context, const void *bytes, std::size_t length,
						   const char *extension, std::string &outPath) noexcept {
	auto *cacheFull = static_cast<bool *>(context);
	outPath.clear();
	if (bytes == nullptr || length == 0) {
		return false;
	}
	WCHAR dir[MAX_PATH];
	if (!PasteCacheGetDir(dir)) {
		return false;
	}
	WCHAR written[MAX_PATH];
	bool full = false;
	if (PasteCacheWriteEx(dir, bytes, length, extension, written, full) != PasteCapVerdict::Ok || full) {
		if (full && cacheFull != nullptr) {
			*cacheFull = true;
		}
		return false;
	}
	// The path is ASCII-safe to widen this way; the cache dir comes from %TEMP%.
	outPath.clear();
	for (const WCHAR *p = written; *p != L'\0'; ++p) {
		outPath += static_cast<char>(static_cast<unsigned char>(*p));
	}
	return true;
}

/// Reads the clipboard's CF_UNICODETEXT as UTF-8. The clipboard must not be
/// open. Returns false when there is no text.
bool ReadClipboardTextUtf8(std::string &utf8) noexcept {
	utf8.clear();
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr)) {
		return false;
	}
	const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
	const WCHAR *text = (handle != nullptr) ? static_cast<const WCHAR *>(GlobalLock(handle)) : nullptr;
	if (text == nullptr) {
		CloseClipboard();
		return false;
	}
	const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (bytes > 1) {
		utf8.resize(static_cast<std::size_t>(bytes - 1));
		WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), bytes, nullptr, nullptr);
	}
	GlobalUnlock(handle);
	CloseClipboard();
	return !utf8.empty();
}

/// Inserts UTF-8 text at the caret, replacing the selection, as one undo step.
/// This is what a plain text paste does, so P4 does not disturb the rest of the
/// document.
void InsertUtf8Text(const std::string &utf8) noexcept {
	if (utf8.empty()) {
		return;
	}
	SciCall_BeginUndoAction();
	SciCall_ReplaceSel(utf8.c_str());
	SciCall_EndUndoAction();
}

} // namespace


bool PasteProbeClipboard(HWND hwnd, ClipboardProbe &probe) noexcept {
	probe = ClipboardProbe{};
	if (!OpenClipboard(hwnd)) {
		return false;
	}
	probe.hasFileList = IsClipboardFormatAvailable(CF_HDROP);
	probe.hasBitmap = ClipboardHasBitmapFormat();
	probe.hasText = IsClipboardFormatAvailable(CF_UNICODETEXT);
	if (probe.hasText) {
		probe.textHasDataUri = ClipboardTextHasDataUri();
	}
	const UINT bridgeMap = RegisterClipboardFormatW(L"Web Custom Format Map");
	probe.hasKeditBridge = bridgeMap != 0 && IsClipboardFormatAvailable(bridgeMap);
	CloseClipboard();
	return true;
}

/// 读 Chromium/Firefox "Web Custom Format Map" 的原始字节(做一个 JSON 字符串)。
/// 剪贴板在返回前已关；不存在返回 false。
bool ReadWebCustomFormatMap(std::string &json) noexcept;

/// 根据 map JSON 里查到的格式名读出 payload 字节。失败返回 false。
bool ReadCustomSlotBytes(const std::string &slotName, std::string &out) noexcept;

/// 读某个已注册的 binary 格式字节到 `out`。剪贴板在返回前已关。
bool ReadRegisteredFormatBytes(const wchar_t *formatName, std::string &out) noexcept {
	out.clear();
	const UINT fmt = RegisterClipboardFormatW(formatName);
	if (fmt == 0 || !IsClipboardFormatAvailable(fmt)) {
		return false;
	}
	if (!OpenClipboard(nullptr)) {
		return false;
	}
	const HANDLE handle = GetClipboardData(fmt);
	const size_t size = handle ? (size_t)GlobalSize(handle) : 0;
	const void *data = handle ? GlobalLock(handle) : nullptr;
	if (data != nullptr && size > 0) {
		out.assign(static_cast<const char *>(data), size);
	}
	if (handle != nullptr) {
		GlobalUnlock(handle);
	}
	CloseClipboard();
	// 尾部 NUL 从 Chromium 的 HGLOBAL 习惯中剃掉
	while (!out.empty() && out.back() == '\0') out.pop_back();
	return !out.empty();
}

bool ReadWebCustomFormatMap(std::string &json) noexcept {
	return ReadRegisteredFormatBytes(L"Web Custom Format Map", json);
}

bool ReadCustomSlotBytes(const std::string &slotName, std::string &out) noexcept {
	if (slotName.empty() || slotName.find(L'"') != std::string::npos) return false;
	std::wstring wname(slotName.begin(), slotName.end());
	return ReadRegisteredFormatBytes(wname.c_str(), out);
}

/// 从 "Web Custom Format Map" 财出 `application/x-notepad420-paste` 对应的槽名
/// (如 "Web Custom Format0")，并读出 payload JSON。失败返回 false。
bool ReadBridgePayloadJson(std::string &json) noexcept {
	json.clear();
	std::string mapJson;
	if (!ReadWebCustomFormatMap(mapJson)) {
		return false;
	}
	const std::string slot = KeditBridge::FindSlotName(mapJson);
	if (slot.empty()) {
		return false;
	}
	return ReadCustomSlotBytes(slot, json);
}

/// 图片盘 sink 的上下文：记录了 cache 是否被占满(供看查诊断)。
struct BridgeSinkContext {
	bool cacheFull = false;
};

bool PasteTryKeditBridge(HWND hwnd) noexcept {
	ClipboardProbe probe;
	if (!PasteProbeClipboard(hwnd, probe) || !probe.hasKeditBridge || !probe.hasText) {
		return false;
	}

	std::string json;
	if (!ReadBridgePayloadJson(json)) {
		return false;
	}
	KeditBridge::BridgePayload payload;
	if (!KeditBridge::ParseBridgePayload(json.data(), json.size(), payload)) {
		return false;
	}

	std::string plain;
	if (!ReadClipboardTextUtf8(plain)) {
		return false;
	}
	// 平台端可能调整换行(LF→CRLF):用 normalize 比较,但重写用的是剪贴板原文。
	if (KeditBridge::NormalizeEolLf(plain) != KeditBridge::NormalizeEolLf(payload.text)) {
		return false;	// strict-match 失败可能是旧挡或跟踪仓丢失,采静退
	}

	BridgeSinkContext ctx;
	std::string out;
	if (!KeditBridge::RewriteBridgeText(payload, plain, &ctx,
				[](void *c, const void *bytes, std::size_t len, const char *ext,
					std::string &outPath) -> bool {
					auto *s = static_cast<BridgeSinkContext *>(c);
					bool full = false;
					const bool ok = CacheDecodedImageSink(&full, bytes, len, ext, outPath);
					if (full) s->cacheFull = true;
					return ok;
				}, out)) {
		return false;   // 静退：后续仲裁会走纯文本档位
	}
	InsertUtf8Text(out);
	return true;
}

bool PasteClipboardHasContent(HWND hwnd) noexcept {
	ClipboardProbe probe;
	if (!PasteProbeClipboard(hwnd, probe)) {
		return false;
	}
	return probe.hasFileList || probe.hasBitmap || probe.hasText;
}

void PasteNotifyCacheFull() noexcept {
	StatusSetTextID(hwndStatus, STATUS_HELP, IDS_PASTE_CACHE_FULL);
	StatusSetSimple(hwndStatus, TRUE);
	InvalidateRect(hwndStatus, nullptr, TRUE);
	UpdateWindow(hwndStatus);
	StatusSetSimple(hwndStatus, FALSE);
}

PasteOutcome PasteHandleCommand(HWND hwnd, bool reverse) noexcept {
	ClipboardProbe probe;
	if (!PasteProbeClipboard(hwnd, probe)) {
		return PasteOutcome::FallThrough;
	}

	// kedit 桥接档最优先：识别通过才接管，否则静退让旧仲裁继续。
	if (!reverse && PasteTryKeditBridge(hwnd)) {
		return PasteOutcome::Handled;
	}

	const PasteTier tier = PasteChooseTier({
			probe.hasFileList, probe.hasBitmap, probe.hasText, probe.textHasDataUri
		}, reverse);

	// Only the plain-text tier defers to Scintilla (PasteAllowsPlainTextFallback).
	// Every other tier owns the paste: when its own conversion fails the
	// clipboard content is dropped rather than pasted in the form the
	// arbitration rule rejected.
	if (PasteAllowsPlainTextFallback(tier)) {
		return PasteOutcome::FallThrough;
	}
	bool handled = false;
	switch (tier) {
	case PasteTier::FileList:
		handled = PasteInsertFileList(hwnd, reverse);
		break;

	case PasteTier::Bitmap:
		handled = PasteInsertBitmap(hwnd, reverse);
		break;

	case PasteTier::EmbeddedDataUri:
		handled = PasteRewriteEmbeddedDataUris(hwnd);
		break;

	case PasteTier::PlainText:
	default:
		return PasteOutcome::FallThrough;
	}
	return handled ? PasteOutcome::Handled : PasteOutcome::Refused;
}

bool PasteInsertFileList(HWND hwnd, bool reverse) noexcept {
	UNREFERENCED_PARAMETER(hwnd);
	if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(hwnd)) {
		return false;
	}

	const HANDLE handle = GetClipboardData(CF_HDROP);
	if (handle == nullptr) {
		CloseClipboard();
		return false;
	}

	// Query the file list while the clipboard is open; the HANDLE belongs to the
	// clipboard owner and must not be freed here.
	const UINT count = DragQueryFile(static_cast<HDROP>(handle), 0xFFFFFFFF, nullptr, 0);
	if (count == 0) {
		CloseClipboard();
		return false;
	}

	// Collect the paths first, so a failure part-way through leaves the document
	// untouched rather than half-pasted.
	if (!PasteCheckFileCount(count)) {
		CloseClipboard();
		return false;
	}
	std::vector<std::wstring> files;
	files.reserve(count);
	for (UINT i = 0; i < count; ++i) {
		WCHAR path[MAX_PATH * 2];
		const UINT length = DragQueryFile(static_cast<HDROP>(handle), i, path, COUNTOF(path));
		if (length != 0) {
			files.emplace_back(path, length);
		}
	}
	CloseClipboard();
	if (files.empty()) {
		return false;
	}

	std::vector<const wchar_t *> raw;
	raw.reserve(files.size());
	for (const auto &file : files) {
		raw.push_back(file.c_str());
	}

	// P1/P3 assembly lives in PasteFile.cpp, where it is unit tested against
	// real files; only the clipboard read and the editor insert remain here.
	std::wstring inserted;
	if (!PasteBuildFileListText(raw.data(), raw.size(), reverse, inserted)) {
		return false;
	}
	InsertPlainText(inserted.c_str());
	return true;
}

bool PasteInsertBitmap(HWND hwnd, bool reverse) noexcept {
	UNREFERENCED_PARAMETER(hwnd);

	// A bitmap burst in one clipboard read is a single image, so reset the
	// per-paste index before naming it.
	PasteCacheResetIndex();

	std::vector<unsigned char> png;
	if (!PasteClipboardBitmapToPng(png)) {
		return false;
	}

	if (reverse) {
		// P3: inline as a data URI; nothing is written to the cache.
		std::string uri;
		if (!PasteBytesToDataUri(png.data(), png.size(), L"image.png", uri)) {
			return false;
		}
		std::wstring wide;
		wide.reserve(uri.size());
		for (char c : uri) {
			wide += static_cast<wchar_t>(static_cast<unsigned char>(c));
		}
		InsertPlainText(wide.c_str());
		return true;
	}

	// P2: cache as PNG and insert the path. A full cache is reported in the
	// status bar and the paste is abandoned rather than evicting anything.
	std::wstring path;
	bool cacheFull = false;
	if (!CachePngOrReport(png, path, cacheFull)) {
		if (cacheFull) {
			PasteNotifyCacheFull();
		}
		return false;
	}
	InsertPlainText(path.c_str());
	return true;
}

bool PasteRewriteEmbeddedDataUris(HWND hwnd) noexcept {
	UNREFERENCED_PARAMETER(hwnd);

	std::string text;
	if (!ReadClipboardTextUtf8(text)) {
		return false;
	}

	bool cacheFull = false;
	std::string rewritten;
	std::size_t converted = 0;
	std::size_t refused = 0;
	if (!PasteRewriteDataUris(text.data(), text.size(), &cacheFull,
							  CacheDecodedImageSink, rewritten, &converted, &refused)) {
		// Nothing convertible: fall through to Scintilla's own text paste so the
		// user gets the text exactly as before.
		return false;
	}

	InsertUtf8Text(rewritten);
	if (cacheFull && refused != 0) {
		PasteNotifyCacheFull();
	}
	return true;
}