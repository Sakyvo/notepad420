#include "ReplacerOps.h"

#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <vector>
#include "AclGuard.h"
#include "DefaultApp.h"

#pragma comment(lib, "shlwapi.lib")

namespace {

const wchar_t *const kDefaultEditorBackupKey = L"Software\\notepad420\\Replacer";

const wchar_t *const kTargets[] = {
	L"C:\\Windows\\notepad.exe",
	L"C:\\Windows\\System32\\notepad.exe",
	L"C:\\Windows\\SysWOW64\\notepad.exe",
};

// 测试可重定向的作用面:目标文件集与环境变量备份键(默认 = 真实值)。
std::vector<std::wstring> g_targets(std::begin(kTargets), std::end(kTargets));
std::wstring g_editorBackupKey = kDefaultEditorBackupKey;

WCHAR g_selfDir[MAX_PATH] = L"";

void EnsureSelfDir() {
	if (g_selfDir[0] == L'\0') {
		GetModuleFileNameW(nullptr, g_selfDir, MAX_PATH);
		PathRemoveFileSpecW(g_selfDir);
	}
}

std::wstring LocaleSourceDir() {
	EnsureSelfDir();
	std::wstring path = g_selfDir;
	if (!path.empty() && path.back() != L'\\') {
		path += L'\\';
	}
	return path + L"locale";
}

bool EnablePrivilege(LPCWSTR name) noexcept {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
		return false;
	}
	TOKEN_PRIVILEGES tp{};
	tp.PrivilegeCount = 1;
	if (!LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)) {
		CloseHandle(token);
		return false;
	}
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	const BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
	const DWORD err = GetLastError();
	CloseHandle(token);
	return ok != FALSE && err == ERROR_SUCCESS;
}

/// 替换单个目标:先存 ACL 快照,再取属主授控,备份原件,最后覆盖。
bool ReplaceOne(const std::wstring &path, std::wstring &error) {
	if (!PathFileExistsW(path.c_str())) {
		return true;	// 目标不存在视为无需处理
	}
	const std::wstring bak = AclGuard::BackupPathFor(path);
	const std::wstring snap = AclGuard::SnapshotPathFor(path);

	DWORD err = AclGuard::SaveSecuritySnapshot(path, snap, error);
	if (err != ERROR_SUCCESS) {
		return false;
	}
	err = AclGuard::TakeOwnershipAndGrant(path, error);
	if (err != ERROR_SUCCESS) {
		return false;
	}
	if (!PathFileExistsW(bak.c_str())) {
		if (!MoveFileW(path.c_str(), bak.c_str())) {
			error = path + L":备份原件失败(error=" + std::to_wstring(GetLastError()) + L")";
			return false;
		}
	}
	const std::wstring src = ReplacerOps::LocalNotepad420Path();
	if (!CopyFileW(src.c_str(), path.c_str(), FALSE)) {
		error = path + L":写入 notepad420 失败(error=" + std::to_wstring(GetLastError()) + L")";
		return false;
	}
	return AclGuard::DeployLocaleBeside(path, LocaleSourceDir(), error);
}

/// 恢复单个目标:删掉当前替换件,搬回原件,回放原始 ACL,清理备份。
bool RestoreOne(const std::wstring &path, std::wstring &error) {
	const std::wstring bak = AclGuard::BackupPathFor(path);
	const std::wstring snap = AclGuard::SnapshotPathFor(path);
	if (!PathFileExistsW(bak.c_str())) {
		return true;	// 无备份视为无需处理
	}
	if (PathFileExistsW(path.c_str())) {
		DWORD err = AclGuard::TakeOwnershipAndGrant(path, error);
		if (err != ERROR_SUCCESS) {
			return false;
		}
		if (!DeleteFileW(path.c_str())) {
			error = path + L":删除替换文件失败(error=" + std::to_wstring(GetLastError()) + L")";
			return false;
		}
	}
	if (!MoveFileW(bak.c_str(), path.c_str())) {
		error = path + L":还原原件失败(error=" + std::to_wstring(GetLastError()) + L")";
		return false;
	}
	if (PathFileExistsW(snap.c_str())) {
		// 回位后的原件属主是快照里的 TrustedInstaller,直接 SetFileSecurity 会拒绝
		// (error=5);先取属主接控制权再回放原始 ACL。
		std::wstring ownError;
		if (AclGuard::TakeOwnershipAndGrant(path, ownError) != ERROR_SUCCESS) {
			error = path + L":回写前取权失败(" + ownError + L")";
			return false;
		}
		std::wstring snapError;
		DWORD err = AclGuard::RestoreSecuritySnapshot(path, snap, snapError);
		if (err != ERROR_SUCCESS) {
			error = path + L":" + snapError;
			return false;
		}
		DeleteFileW(snap.c_str());
	}
	std::wstring localeError;
	if (!AclGuard::RemoveLocaleBeside(path, LocaleSourceDir(), localeError)) {
		error = localeError;
		return false;
	}
	return true;
}

} // namespace

namespace ReplacerOps {

bool IsElevatedSelf() noexcept {
	BOOL admin = FALSE;
	PSID group = nullptr;
	SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
	if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
								 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
		CheckTokenMembership(nullptr, group, &admin);
		FreeSid(group);
	}
	BOOL elevated = FALSE;
	HANDLE token = nullptr;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		TOKEN_ELEVATION elev{};
		DWORD out = 0;
		GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &out);
		elevated = elev.TokenIsElevated;
		CloseHandle(token);
	}
	return admin && elevated;
}

bool EnableRequiredPrivileges() noexcept {
	return EnablePrivilege(SE_TAKE_OWNERSHIP_NAME) &&
		   EnablePrivilege(SE_RESTORE_NAME) &&
		   EnablePrivilege(SE_SECURITY_NAME);
}

std::wstring SelfDir() {
	EnsureSelfDir();
	return g_selfDir;
}

std::wstring LocalNotepad420Path() {
	EnsureSelfDir();
	std::wstring path = g_selfDir;
	if (!path.empty() && path.back() != L'\\') {
		path += L'\\';
	}
	return path + L"notepad420.exe";
}

int RunReplaceOperation(std::wstring &error) {
	error.clear();
	if (!PathFileExistsW(LocalNotepad420Path().c_str())) {
		error = L"同目录下找不到 notepad420.exe,无法替换。";
		return 4;
	}
	for (const std::wstring &t : g_targets) {
		if (!ReplaceOne(t, error)) {
			return 4;
		}
	}
	DefaultApp::ActionReport ifeo = DefaultApp::RemoveNp3IfeoHijacks();
	if (!ifeo.Ok()) {
		error = L"解除 IFEO 劫持失败:" + ifeo.error;
		return 5;
	}
	DefaultApp::ActionReport uc = DefaultApp::RewriteNp3UserChoices();
	if (!uc.Ok()) {
		error = L"重写文件关联失败:" + uc.error;
		return 5;
	}
	std::wstring progidError;
	if (DefaultApp::WriteOurProgId(LocalNotepad420Path(), progidError) != ERROR_SUCCESS) {
		error = L"ProgId 写入失败:" + progidError;
		return 5;
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	return 0;
}

int RunEditorOperation(std::wstring &error) {
	error.clear();
	std::wstring envError;
	const DWORD rc = AclGuard::SetUserEditors(LocalNotepad420Path(), g_editorBackupKey, envError);
	if (rc != ERROR_SUCCESS) {
		error = L"EDITOR/VISUAL 写入失败:" + envError;
		return 6;
	}
	return 0;
}

int RunAssociateOperation(std::wstring &error) {
	error.clear();
	for (size_t i = 0; i < DefaultApp::kAssociateExtsCount; ++i) {
		const wchar_t *ext = DefaultApp::kAssociateExts[i];
		std::wstring extError;
		if (!DefaultApp::WriteUserChoice(ext, DefaultApp::kOurProgId, extError)) {
			error = std::wstring(ext) + L": " + extError;
			return 5;
		}
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	return 0;
}

int RunRestoreOperation(std::wstring &error) {
	error.clear();
	for (const std::wstring &t : g_targets) {
		if (!RestoreOne(t, error)) {
			return 4;
		}
	}
	DefaultApp::ActionReport uc = DefaultApp::CleanupOurUserChoices();
	if (!uc.Ok()) {
		error = L"清理文件关联失败:" + uc.error;
		return 5;
	}
	std::wstring progidError;
	if (DefaultApp::DeleteOurProgId(progidError) != ERROR_SUCCESS) {
		error = L"ProgId 回收失败:" + progidError;
		return 5;
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	std::wstring envError;
	AclGuard::RestoreUserEditors(g_editorBackupKey, envError);
	if (!envError.empty()) {
		error = L"EDITOR 回退失败——" + envError;
		return 6;
	}
	return 0;
}

void SetTargetsForTesting(const wchar_t *const *targets, size_t count) {
	g_targets.clear();
	if (targets == nullptr) {
		g_targets.assign(std::begin(kTargets), std::end(kTargets));
		return;
	}
	for (size_t i = 0; i < count; ++i) {
		g_targets.emplace_back(targets[i]);
	}
}

void SetEditorBackupKeyForTesting(const wchar_t *key) {
	g_editorBackupKey = (key != nullptr) ? key : kDefaultEditorBackupKey;
}

} // namespace ReplacerOps
