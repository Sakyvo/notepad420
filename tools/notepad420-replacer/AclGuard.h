/******************************************************************************
*
* notepad420
*
* AclGuard.h
*   Win32-ACL helpers for the notepad420 replacer, kept free of any GUI so the
*   pure logic can be unit tested headlessly (tools/tests/replacer_acl_test.cpp).
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <windows.h>
#include <string>

namespace AclGuard {

/// The SDDL written over the target notepad.exe while it holds notepad420.
///
/// Owner/group become Administrators so the elevated process can rename and
/// replace the file; SYSTEM and Administrators get full control. Note: no ACE
/// flag other than inheritance is used — SetNamedSecurityInfo rejects the
/// object-inherit-only (`ID`) flag with ERROR_INVALID_FLAGS (1004).
constexpr const wchar_t *kReplaceSddl =
	L"O:BAG:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

/// Returns true when sddl converts to a security descriptor, i.e. the
/// descriptor is accepted by the Win32 SDDL parser (SDDL_REVISION_1).
bool ValidateSddl(const wchar_t *sddl) noexcept;

/// The exact string ReplaceSddl hands to the Win32 parser, and a guard that it
/// carries no ACE flag that SetNamedSecurityInfo would later reject.
std::wstring BuildReplaceSddl() noexcept;

/// `<path>.bak` — the backed-up original notepad.exe.
std::wstring BackupPathFor(const std::wstring &path);

/// `<path>.bak.sd` — the self-relative security descriptor snapshot taken
/// from the original file before its ACL was modified.
std::wstring SnapshotPathFor(const std::wstring &path);

/// Makes Administrators the owner and grants SYSTEM/Administrators full
/// control, leaving a DACL the elevated process can act through.
/// Returns ERROR_SUCCESS, or the failing Win32 error code.
DWORD TakeOwnershipAndGrant(const std::wstring &path, std::wstring &error) noexcept;

/// Captures the current OWNER|GROUP|DACL of `path` as a self-relative
/// descriptor and stores the raw bytes in `sdPath`. Does not overwrite an
/// existing snapshot.
DWORD SaveSecuritySnapshot(const std::wstring &path, const std::wstring &sdPath,
						   std::wstring &error) noexcept;

/// Applies the raw descriptor stored in `sdPath` back onto `path`
/// (OWNER|GROUP|DACL). SACLs are never touched.
DWORD RestoreSecuritySnapshot(const std::wstring &path, const std::wstring &sdPath,
							  std::wstring &error) noexcept;

/// Creates a shell link at `lnkPath` targeting `targetPath`.
DWORD CreateShortcut(const std::wstring &lnkPath, const std::wstring &targetPath,
					 std::wstring &error) noexcept;


/// Copies the whole `sourceLocaleDir` tree (e.g. `<replacer dir>\locale`,
/// currently holding `zh-Hans\Notepad4.dll`) next to the target notepad.exe,
/// i.e. `<target dir>\locale\...`. Existing files are overwritten so a
/// redeploy refreshes them. Returns false on the first failed copy.
bool DeployLocaleBeside(const std::wstring &targetPath,
                        const std::wstring &sourceLocaleDir,
                        std::wstring &error) noexcept;

/// Removes the locale entries that DeployLocaleBeside placed next to the
/// target: every file/dir name present under `sourceLocaleDir` at call time is
/// deleted from `<target dir>\locale`; the `locale` dir itself is removed if
/// it becomes empty. Missing entries are tolerated (idempotent on restore).
bool RemoveLocaleBeside(const std::wstring &targetPath,
                        const std::wstring &sourceLocaleDir,
                        std::wstring &error) noexcept;

/// EDITOR/VISUAL env-var integration (Ctrl+G external editor in harness tools).
/// Most tools resolve Ctrl+G as VISUAL → EDITOR → fallback; both must be set.

/// Writes user-scope EDITOR and VISUAL to `editorPath`. The previous values
/// (or absence) are preserved under `backupRegKey` so RestoreUserEditors can
/// undo them. Broadcasts WM_SETTINGCHANGE afterwards. Returns ERROR_SUCCESS
/// or the first failing Win32 code.
DWORD SetUserEditors(const std::wstring &editorPath,
                     const std::wstring &backupRegKey,
                     std::wstring &error) noexcept;

/// Restores EDITOR and VISUAL from the backup record in `backupRegKey`.
/// Idempotent: when no backup exists this is a successful no-op.
DWORD RestoreUserEditors(const std::wstring &backupRegKey,
                         std::wstring &error) noexcept;

/// Reads one current user-scope var; empty string covers both "unset" and
/// "empty value". Out-present distinguishes the two. `name` is EDITOR/VISUAL.
DWORD ReadUserEnvValue(const wchar_t *name, std::wstring &value, bool &present) noexcept;

} // namespace AclGuard