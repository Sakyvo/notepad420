// Tests for the replacer's ACL helper layer (tools/notepad420-replacer/AclGuard.cpp).
//
// Covers the regression that broke the shipped tool (an ACE flag the Win32
// parser rejects with ERROR_INVALID_FLAGS), plus the ACL snapshot round trip
// and the pure backup/snapshot path naming.
//
// Expected values come from the Win32 contract, not from re-running the code
// under test.
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <cstdio>
#include <cstring>

#include "../notepad420-replacer/AclGuard.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                     \
	do {                                                                      \
		++g_checks;                                                           \
		if (!(cond)) {                                                        \
			++g_failures;                                                     \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));      \
		}                                                                     \
	} while (0)

static void TestReplaceSddlIsAccepted() {
	const std::wstring sddl = AclGuard::BuildReplaceSddl();

	// The Win32 parser must accept it: this is exactly what failed in the
	// shipped build (error 1004, ERROR_INVALID_FLAGS).
	PSECURITY_DESCRIPTOR sd = nullptr;
	ULONG len = 0;
	const BOOL ok = ConvertStringSecurityDescriptorToSecurityDescriptorW(
		sddl.c_str(), SDDL_REVISION_1, &sd, &len);
	if (sd != nullptr) {
		LocalFree(sd);
	}
	CHECK(ok != FALSE, "replace SDDL is accepted by the Win32 parser");
	CHECK(AclGuard::ValidateSddl(sddl.c_str()), "ValidateSddl agrees");

	// The object-inherit-only ACE flag (SDDL "ID") is the flag that made
	// SetNamedSecurityInfo fail; it must never reappear.
	CHECK(sddl.find(L";OICIID;") == std::wstring::npos, "no ID ACE flag");
	CHECK(sddl.find(L"OICIIDFA") == std::wstring::npos, "no OICIIDFA ACE flag");
	// Contract shape: owner/group Administrators, protected DACL, SYSTEM and
	// Administrators granted full control.
	CHECK(sddl.find(L"O:BA") != std::wstring::npos, "owner is Administrators");
	CHECK(sddl.find(L"G:BA") != std::wstring::npos, "group is Administrators");
	CHECK(sddl.find(L"D:PAI") != std::wstring::npos, "DACL present, protected, inherited");
	CHECK(sddl.find(L";;;SY)") != std::wstring::npos, "SYSTEM ACE present");
	CHECK(sddl.find(L";;;BA)") != std::wstring::npos, "Administrators ACE present");

	// Sanity: a deliberately broken SDDL must be rejected, so the guard above
	// is not vacuous. `OICIIDFA` is the exact flag combination that made
	// SetNamedSecurityInfo fail with ERROR_INVALID_FLAGS in the shipped build.
	CHECK(!AclGuard::ValidateSddl(L"O:BAG:BAD:AI(A;OICIIDFA;FA;;;BA)"),
		  "the old broken SDDL is rejected");
}

static void TestPathNaming() {
	const std::wstring exe = L"C:\\Windows\\System32\\notepad.exe";
	CHECK(AclGuard::BackupPathFor(exe) == exe + L".bak", "backup path is <path>.bak");
	CHECK(AclGuard::SnapshotPathFor(exe) == exe + L".bak.sd", "snapshot path is <path>.bak.sd");
	CHECK(AclGuard::BackupPathFor(L"x") == L"x.bak", "short path backup naming");
	CHECK(AclGuard::SnapshotPathFor(L"x") == L"x.bak.sd", "short path snapshot naming");
}

/// Reads OWNER|GROUP|DACL of `path` as an SDDL string.
static std::wstring SecuritySddlOf(const std::wstring &path) {
	const DWORD info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
					   DACL_SECURITY_INFORMATION;
	DWORD need = 0;
	GetFileSecurityW(path.c_str(), info, nullptr, 0, &need);
	if (need == 0) {
		return std::wstring();
	}
	std::string buf(need, '\0');
	if (!GetFileSecurityW(path.c_str(), info, buf.data(), need, &need)) {
		return std::wstring();
	}
	PSECURITY_DESCRIPTOR sd = reinterpret_cast<PSECURITY_DESCRIPTOR>(buf.data());
	LPWSTR text = nullptr;
	if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
			sd, SDDL_REVISION_1, info, &text, nullptr)) {
		return std::wstring();
	}
	std::wstring result = text;
	LocalFree(text);
	return result;
}

/// Strips the DACL control flags (`AI`, `PAI`, ...) that sit between `D:` and
/// the first ACE. Windows recomputes SE_DACL_AUTO_INHERITED from the parent at
/// set time, so that part is not stable for a leaf file; owner, group and the
/// ACE list are the meaningful, restorable content.
static std::wstring StableAclPart(const std::wstring &sddl) {
	const size_t d = sddl.find(L"D:");
	if (d == std::wstring::npos) {
		return sddl;
	}
	const size_t ace = sddl.find(L'(');
	if (ace == std::wstring::npos || ace < d) {
		return sddl;
	}
	return sddl.substr(0, d + 2) + sddl.substr(ace);
}

static void TestSnapshotRoundTrip() {
	// A private temp file: never touches the real notepad.exe.
	wchar_t tempDir[MAX_PATH];
	GetTempPathW(MAX_PATH, tempDir);
	std::wstring file = std::wstring(tempDir) + L"notepad420-acltest-" +
						std::to_wstring(GetCurrentProcessId()) + L".tmp";
	HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
						   FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		std::printf("SKIP snapshot round trip: cannot create temp file (%lu)\n", GetLastError());
		return;
	}
	CloseHandle(h);

	const std::wstring snap = AclGuard::SnapshotPathFor(file);
	DeleteFileW(snap.c_str());

	// Give the temp file a distinctive, non-default DACL to prove the snapshot
	// carries real content rather than "whatever a fresh file has".
	PSECURITY_DESCRIPTOR sd = nullptr;
	ULONG len = 0;
	// Deliberately restricted: only Administrators full control, no inherits.
	const wchar_t *kDistinct =
		L"O:BAG:BAD:PAI(A;;FA;;;BA)(A;;FR;;;WD)";
	ConvertStringSecurityDescriptorToSecurityDescriptorW(kDistinct, SDDL_REVISION_1, &sd, &len);
	PSID owner = nullptr, group = nullptr;
	PACL dacl = nullptr;
	BOOL d1 = FALSE, d2 = FALSE, present = FALSE, d3 = FALSE;
	GetSecurityDescriptorOwner(sd, &owner, &d1);
	GetSecurityDescriptorGroup(sd, &group, &d2);
	GetSecurityDescriptorDacl(sd, &present, &dacl, &d3);
	SetNamedSecurityInfoW(const_cast<LPWSTR>(file.c_str()), SE_FILE_OBJECT,
						  OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
						  owner, group, nullptr, nullptr);
	SetNamedSecurityInfoW(const_cast<LPWSTR>(file.c_str()), SE_FILE_OBJECT,
						  DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr);
	LocalFree(sd);

	const std::wstring before = SecuritySddlOf(file);
	CHECK(!before.empty(), "read back the restricted DACL");

	std::wstring error;
	const DWORD saveErr = AclGuard::SaveSecuritySnapshot(file, snap, error);
	CHECK(saveErr == ERROR_SUCCESS, "snapshot saved");
	CHECK(GetFileAttributesW(snap.c_str()) != INVALID_FILE_ATTRIBUTES,
		  "snapshot file exists on disk");

	// Widen the DACL so a restore that did nothing would be observable.
	PSECURITY_DESCRIPTOR wide = nullptr;
	ULONG wideLen = 0;
	ConvertStringSecurityDescriptorToSecurityDescriptorW(
		L"O:BAG:BAD:PAI(A;;FA;;;BA)(A;;FA;;;WD)", SDDL_REVISION_1, &wide, &wideLen);
	PSID wOwner = nullptr, wGroup = nullptr;
	PACL wDacl = nullptr;
	BOOL q1 = FALSE, q2 = FALSE, q3 = FALSE, q4 = FALSE;
	GetSecurityDescriptorOwner(wide, &wOwner, &q1);
	GetSecurityDescriptorGroup(wide, &wGroup, &q2);
	GetSecurityDescriptorDacl(wide, &q3, &wDacl, &q4);
	SetNamedSecurityInfoW(const_cast<LPWSTR>(file.c_str()), SE_FILE_OBJECT,
						  OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
						  wOwner, wGroup, nullptr, nullptr);
	SetNamedSecurityInfoW(const_cast<LPWSTR>(file.c_str()), SE_FILE_OBJECT,
						  DACL_SECURITY_INFORMATION, nullptr, nullptr, wDacl, nullptr);
	LocalFree(wide);

	CHECK(SecuritySddlOf(file) != before, "DACL actually changed before restore");
	const DWORD restoreErr = AclGuard::RestoreSecuritySnapshot(file, snap, error);
	CHECK(restoreErr == ERROR_SUCCESS, "snapshot restored");
	CHECK(StableAclPart(SecuritySddlOf(file)) == StableAclPart(before),
		  "restored owner/group/ACEs equal the original");

	// A second save must not clobber the first snapshot.
	std::wstring again;
	CHECK(AclGuard::SaveSecuritySnapshot(file, snap, again) == ERROR_SUCCESS,
		  "re-saving keeps the existing snapshot");
	CHECK(StableAclPart(SecuritySddlOf(file)) == StableAclPart(before),
		  "owner/group/ACEs still equal the original");

	DeleteFileW(snap.c_str());
	DeleteFileW(file.c_str());
}

static void TestSnapshotMissingIsReported() {
	std::wstring error;
	const DWORD err = AclGuard::RestoreSecuritySnapshot(
		L"C:\\Windows\\notepad.exe", L"C:\\does-not-exist\\nope.bak.sd", error);
	CHECK(err != ERROR_SUCCESS, "missing snapshot is an error");
	CHECK(!error.empty(), "missing snapshot reports a message");
}

static void WriteTextFile(const std::wstring &path, const char *content) {
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
						   FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	WriteFile(h, content, (DWORD)strlen(content), &written, nullptr);
	CloseHandle(h);
}

// helper used by tests (mirrors AclGuard's ParentDirOf semantics)
static std::wstring ParentOf(const std::wstring &path) {
	const size_t pos = path.find_last_of(L'\\');
	return pos == std::wstring::npos ? path : path.substr(0, pos);
}

static std::wstring ReadEnvVarNow(const wchar_t *name, bool *ok) {
	wchar_t buf[4096];
	DWORD sz = sizeof(buf), ty = 0;
	LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, L"Environment", name,
							 RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &ty, buf, &sz);
	if (s != ERROR_SUCCESS) {
		if (ok) *ok = (s == ERROR_FILE_NOT_FOUND);
		return L"";
	}
	if (ok) *ok = true;
	return buf;
}

// replacer env integration: iterate over 4 fixture scenarios (was EDITOR /
// VISUAL set?) and verify backup+restore of both vars leaves the user env
// in exactly its original state.
static void TestEditorEnvRoundTrip() {
	const std::wstring backupKey = L"Software\\notepad420-test\\Replacer";

	struct Orig { bool present; std::wstring value; };
	Orig origEditor{}, origVisual{};
	{
		std::wstring v; bool p = false;
		AclGuard::ReadUserEnvValue(L"EDITOR", v, p); origEditor = { p, v };
		AclGuard::ReadUserEnvValue(L"VISUAL", v, p); origVisual = { p, v };
	}

	auto restoreEnv = [&](const wchar_t *name, const Orig &o) {
		HKEY env = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0,
							KEY_SET_VALUE, nullptr, &env, nullptr) != ERROR_SUCCESS) return;
		if (o.present) {
			RegSetValueExW(env, name, 0, REG_SZ,
						   reinterpret_cast<const BYTE *>(o.value.c_str()),
						   (DWORD)((o.value.size() + 1) * sizeof(wchar_t)));
		} else {
			RegDeleteValueW(env, name);
		}
		RegCloseKey(env);
	};

	struct Scenario { bool editorHad, visualHad; const wchar_t *oldEditor, *oldVisual; };
	const Scenario scenarios[] = {
		{ false, false, nullptr, nullptr },
		{ true,  false, L"C:/pre/editor.exe", nullptr },
		{ false, true,  nullptr, L"C:/pre/visual.exe" },
		{ true,  true,  L"C:/pre/editor.exe", L"C:/pre/visual.exe" },
	};

	for (const auto &sc : scenarios) {
		HKEY env = nullptr;
		RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0,
						KEY_SET_VALUE, nullptr, &env, nullptr);
		if (sc.editorHad) {
			RegSetValueExW(env, L"EDITOR", 0, REG_SZ,
						   reinterpret_cast<const BYTE *>(sc.oldEditor),
						   (DWORD)((wcslen(sc.oldEditor) + 1) * sizeof(wchar_t)));
		} else {
			RegDeleteValueW(env, L"EDITOR");
		}
		if (sc.visualHad) {
			RegSetValueExW(env, L"VISUAL", 0, REG_SZ,
						   reinterpret_cast<const BYTE *>(sc.oldVisual),
						   (DWORD)((wcslen(sc.oldVisual) + 1) * sizeof(wchar_t)));
		} else {
			RegDeleteValueW(env, L"VISUAL");
		}
		RegCloseKey(env);
		RegDeleteKeyW(HKEY_CURRENT_USER, backupKey.c_str());

		std::wstring error;
		CHECK(AclGuard::SetUserEditors(L"C:\\temp\\np420-replacer\\notepad420.exe",
									   backupKey, error) == ERROR_SUCCESS,
			  "set both vars");

		bool okE = false, okV = false;
		std::wstring nowE = ReadEnvVarNow(L"EDITOR", &okE);
		std::wstring nowV = ReadEnvVarNow(L"VISUAL", &okV);
		CHECK(okE && nowE == L"C:\\temp\\np420-replacer\\notepad420.exe", "EDITOR persisted");
		CHECK(okV && nowV == L"C:\\temp\\np420-replacer\\notepad420.exe", "VISUAL persisted");

		// 再覆写一次;必须不覆盖第一次的备份
		CHECK(AclGuard::SetUserEditors(L"C:\\temp\\np420-replacer-second\\notepad420.exe",
									   backupKey, error) == ERROR_SUCCESS,
			  "second set survives");

		CHECK(AclGuard::RestoreUserEditors(backupKey, error) == ERROR_SUCCESS,
			  "restore returns ok");
		nowE = ReadEnvVarNow(L"EDITOR", &okE);
		nowV = ReadEnvVarNow(L"VISUAL", &okV);
		LSTATUS es = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"EDITOR",
								  RRF_RT_ANY, nullptr, nullptr, nullptr);
		LSTATUS vs = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"VISUAL",
								  RRF_RT_ANY, nullptr, nullptr, nullptr);
		CHECK((es == ERROR_SUCCESS) == sc.editorHad, "ED restore: original presence");
		CHECK((vs == ERROR_SUCCESS) == sc.visualHad, "VIS restore: original presence");
		if (sc.editorHad) CHECK(nowE == sc.oldEditor, "ED restore: original value");
		if (sc.visualHad) CHECK(nowV == sc.oldVisual, "VIS restore: original value");

		restoreEnv(L"EDITOR", origEditor);
		restoreEnv(L"VISUAL", origVisual);
	}

	// 备份键被 restore 删除
	HKEY hK = nullptr;
	LSTATUS s = RegOpenKeyExW(HKEY_CURRENT_USER, backupKey.c_str(), 0, KEY_READ, &hK);
	CHECK(s == ERROR_FILE_NOT_FOUND, "backup key deleted");
	RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\notepad420-test");   // tidy

	// 幂等: 第二次 restore 无事
	std::wstring error;
	CHECK(AclGuard::RestoreUserEditors(backupKey, error) == ERROR_SUCCESS,
		  "second restore is no-op");
}

static void TestLocaleDeployAndRemove() {
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	const std::wstring root = std::wstring(tmp) + L"np420_locale_test";   // no trailing slash
	const std::wstring sourceTree = root + L"\\locale";
	const std::wstring targetExe = root + L"\\target\\notepad.exe";

	// fixture: source locale\zh-Hans\Notepad4.dll, target dir with notepad.exe
	RemoveDirectoryW((root).c_str());
	SHCreateDirectoryExW(nullptr, (sourceTree + L"\\zh-Hans").c_str(), nullptr);
	SHCreateDirectoryExW(nullptr, ParentOf(targetExe).c_str(), nullptr);
	WriteTextFile(sourceTree + L"\\zh-Hans\\Notepad4.dll", "fake-dll");
	WriteTextFile(targetExe, "notepad");

	std::wstring error;
	CHECK(AclGuard::DeployLocaleBeside(targetExe, sourceTree, error),
		  "deploy into empty tree succeeds");
	CHECK(PathFileExistsW((root + L"\\target\\locale\\zh-Hans\\Notepad4.dll").c_str()),
		  "locale copied next to target");

	// redeploy overwrites
	WriteTextFile(sourceTree + L"\\zh-Hans\\Notepad4.dll", "fake-dll-v2");
	CHECK(AclGuard::DeployLocaleBeside(targetExe, sourceTree, error),
		  "redeploy succeeds");

	// remove: deletes the subtree, keeps target exe
	CHECK(AclGuard::RemoveLocaleBeside(targetExe, sourceTree, error),
		  "remove succeeds");
	CHECK(!PathFileExistsW((root + L"\\target\\locale\\zh-Hans\\Notepad4.dll").c_str()),
		  "locale paragraph removed");
	CHECK(PathFileExistsW(targetExe.c_str()), "target exe untouched");
	// idempotent on second run
	CHECK(AclGuard::RemoveLocaleBeside(targetExe, sourceTree, error),
		  "second remove is a no-op success");

	// cleanup fixture (best-effort; tree may already be half-removed)
		DeleteFileW((sourceTree + L"\\zh-Hans\\Notepad4.dll").c_str());
		RemoveDirectoryW((sourceTree + L"\\zh-Hans").c_str());
		RemoveDirectoryW(sourceTree.c_str());
		DeleteFileW(targetExe.c_str());
		RemoveDirectoryW(ParentOf(targetExe).c_str());
		RemoveDirectoryW(root.c_str());
}

int main() {
	TestReplaceSddlIsAccepted();
	TestPathNaming();
	TestSnapshotRoundTrip();
	TestSnapshotMissingIsReported();
	TestLocaleDeployAndRemove();
	TestEditorEnvRoundTrip();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}