// Default-open takeover for the notepad420 replacer.
//
// Three layers:
//   - IFEO removal: delete HKLM IFEO\notepad.exe when its Debugger points at Notepad3.
//   - UserChoice rewrite (replace path): NP3/legacy-owned entries → kOurProgId.
//   - UserChoice associate (--associate, ADR 0010): the fixed 25-ext list is
//     unconditionally rebuilt to kOurProgId, independent of file replacement.
// The Win10 UserChoice hash is reproduced here, golden-tested against the
// 25 real NP3 entries found on the author's machine.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace DefaultApp {

/// The built hardcoded fallback; the live value is read from shell32.dll.
extern const wchar_t kExperienceFallback[];
/// Where our UserChoice entries point: notepad420's private ProgId (ADR 0006).
extern const wchar_t kOurProgId[];

/// ADR 0010 的默认打开接管清单:011 时代本机 25 条真实 NP3 接管项。
/// 实现与测试同源引用本常量。
extern const wchar_t *const kAssociateExts[];
extern const size_t kAssociateExtsCount;

/// 测试接缝:把 FileExts 读写的根子键重定向到沙箱(如 Software\np420test\...),
/// nullptr 复位为真实 FileExts。
void SetFileExtsSubKeyForTesting(const wchar_t *subKey);

/// 测试接缝:把 ProgId 写入/删除的 Classes 根子键重定向到沙箱, nullptr 复位。
void SetClassesSubKeyForTesting(const wchar_t *subKey);

/// true iff progId/debugger path references Notepad3 (case-insensitive).
bool IsNotepad3Target(const wchar_t *value) noexcept;

/// Base64 of the two XOR-folded SpookyHash-style accumulators over
/// (ext + sid + progId + hexDateTime + experience), lowercased.
/// Implementation is the C++ port of DanysysTeam/PS-SFTA Get-Hash; the 25
/// golden vectors captured from real NP3 entries pin the exact algorithm.
std::wstring ComputeUserChoiceHash(const std::wstring &ext, const std::wstring &sid,
								const std::wstring &progId, const std::wstring &hexDateTime,
								const std::wstring &experience);

/// Live-read the experience string from shell32.dll; fallback on failure.
std::wstring ReadUserExperienceString();

/// Current (possibly elevated) user's textual SID, e.g. S-1-5-21-...
std::wstring GetCurrentUserSid();

/// Write FileExts\<ext>\UserChoice = (progId, valid hash). Deletes any
/// pre-existing UserChoice key first (its deny-write ACL only allows delete).
/// Retries the Hash write until the minute-boundary LastWriteTime is stable.
bool WriteUserChoice(const wchar_t *ext, const wchar_t *progId, std::wstring &error);

struct ActionReport {
	int rewritten = 0;
	int deleted = 0;
	std::vector<std::wstring> skipped;	///< non-NP3 hijacks left alone, listed in UI
	std::wstring error;					///< first hard failure; non-empty => aborted
	bool Ok() const noexcept { return error.empty(); }
};

/// Delete IFEO\notepad.exe (both WOW64 views) when its Debugger targets Notepad3.
/// Keys with a foreign Debugger are reported in `skipped`, not touched.
/// ACCESS_DENIED/error aborts (admin is required and guaranteed by manifest).
ActionReport RemoveNp3IfeoHijacks();

/// Rewrite every FileExts UserChoice whose ProgId targets Notepad3或旧 farm
/// `Applications\notepad.exe` (k叉 011 的写入) 到 kOurProgId (\'Applications\notepad420.exe\' 当前新宏, ADR 0006)。
ActionReport RewriteNp3UserChoices();

/// 创建/覆盖私有 ProgId `HKCU\Software\Classes\Applications\notepad420.exe`,
/// shell\open\command 与 DefaultIcon 都指向给定的 exe。返回 Win32 错误码。
DWORD WriteOurProgId(const std::wstring &exePath, std::wstring &error) noexcept;

/// 删除该 ProgId(不存在为 no-op成功)。
DWORD DeleteOurProgId(std::wstring &error) noexcept;

/// Delete every FileExts UserChoice whose ProgId == kOurProgId (restore path;
/// these are all entries we wrote during replace).
ActionReport CleanupOurUserChoices();

} // namespace DefaultApp
