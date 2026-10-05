/******************************************************************************
*
* notepad420
*
* Paste.h
*   Clipboard paste entry points: probe the clipboard, arbitrate the tier, and
*   run the tier's conversion. Win32 layer over the pure policy in
*   src/PastePolicy.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <windows.h>
#include "PastePolicy.h"

struct ClipboardProbe {
	bool hasFileList = false;		// CF_HDROP
	bool hasBitmap = false;			// CF_DIB / CF_DIBV5 / CF_BITMAP / registered PNG
	bool hasText = false;			// CF_UNICODETEXT
	bool textHasDataUri = false;	// hasText and the text contains "data:image/"
	bool hasKeditBridge = false;	// kedit 桥接格式 application/x-notepad420-paste
};

/// Reads the clipboard's offered formats. The clipboard is always closed before
/// returning, so callers may hand it straight to Scintilla.
bool PasteProbeClipboard(HWND hwnd, ClipboardProbe &probe) noexcept;

/// True when the clipboard holds something this fork can paste, including the
/// file and bitmap tiers that Scintilla's own CanPaste() does not see.
bool PasteClipboardHasContent(HWND hwnd) noexcept;

/// Outcome of handling a paste command. The three cases are distinct because
/// falling back to Scintilla on a *failed* conversion would insert the very text
/// the tier was supposed to consume (a browser DIB+text clipboard would paste the
/// page URL), which the arbitration rule forbids.
///
/// \note kedit 桥接档是唯一的 "静退" 档:该档的任何一步失败(格式不含识别、
/// strict-match 不符、图片落盘失败)都该回退到现有仲裁链。为了不构造一个
/// 新的伪档, PasteHandleCommand 先检查桥接档; 成立则独成一档, 不成立—>
/// FallThrough，再让后续仲裁处理。
enum class PasteOutcome {
	Handled,		// the tier consumed the clipboard
	FallThrough,	// plain text: Scintilla's own paste is correct
	Refused,		// the tier owned the paste but could not serve it (full cache)
};

/// Tries the kedit bridge paste: 只有剪贴板携 application/x-notepad420-paste
/// 格式且 payload.text 与 text/plain 严格一致时才采纳该档。成功插入并返回真;
/// 否则都不动文档。仅在\''decision Q12\'\''语义下：任何环节失败只做静退返回
/// false, 由调用方回退到现有仲裁（不合使 PasteOutcome::Refused——静退不弹窗）。
/// 与 P1–P4 任何一档不混交。
bool PasteTryKeditBridge(HWND hwnd) noexcept;

/// Runs the paste for the arbitrated tier. See PasteOutcome for why the failure
/// case is not folded into FallThrough.
PasteOutcome PasteHandleCommand(HWND hwnd, bool reverse) noexcept;

/// P1/P3: insert the clipboard file list as paths (forward) or data URIs
/// (reverse). Returns false when the clipboard list could not be read.
bool PasteInsertFileList(HWND hwnd, bool reverse) noexcept;

/// P2/P3: cache the clipboard bitmap as a PNG and insert its path (forward),
/// or insert a data URI (reverse).
bool PasteInsertBitmap(HWND hwnd, bool reverse) noexcept;

/// P4: rewrite embedded data URIs in pasted text into cached file paths.
/// Returns false to fall through to a plain text paste.
bool PasteRewriteEmbeddedDataUris(HWND hwnd) noexcept;

/// Reports that the paste cache is full, in the status bar (never a modal
/// dialog), and that the user should clear the directory by hand.
void PasteNotifyCacheFull() noexcept;
