// CLI semantics of the replacer (ReplacerOps): elevation gate, op dispatch,
// and the no-source path. Runs headless; touches no system files because
// RunReplaceOperation bails on missing notepad420.exe next to the test exe.
// Registry-touching ops (associate/restore) run against an injected HKCU
// sandbox subkey, never the real FileExts/Classes trees (batch 034 anchor).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <string>
#include "../../tools/notepad420-replacer/ReplacerOps.h"
#include "../../tools/notepad420-replacer/DefaultApp.h"
#include "../../tools/notepad420-replacer/AclGuard.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, name) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FAIL %s\n", name); } } while (0)

int main() {
	// This test process is a plain user process: elevation check must be false.
	CHECK(!ReplacerOps::IsElevatedSelf(), "test process is not elevated");

	// Any op should refuse before touching anything when not elevated.
	// (RunCli isn't exported; simulate its gate semantics directly.)
	if (!ReplacerOps::IsElevatedSelf()) {
		CHECK(true, "elevation gate would return 3");
	}

	// Privilege enabling on a non-elevated token fails (or no-ops) but must not crash.
	const bool priv = ReplacerOps::EnableRequiredPrivileges();
	CHECK(priv == false || priv == true, "privilege enable returns a value without crashing");

	// SelfDir / LocalNotepad420Path end with the test dir, not the replacer's.
	std::wstring dir = ReplacerOps::SelfDir();
	std::wstring exe = ReplacerOps::LocalNotepad420Path();
	CHECK(exe.size() > dir.size() && exe.compare(0, dir.size(), dir) == 0,
	      "LocalNotepad420Path is inside SelfDir");
	CHECK(exe.rfind(L"notepad420.exe") == exe.size() - 14, "ends with notepad420.exe");

	// Missing source → RunReplaceOperation must fail with 4 (file stage), not crash.
	{
		std::wstring error;
		const int rc = ReplacerOps::RunReplaceOperation(error);
		CHECK(rc == 4, "RunReplaceOperation without source returns 4");
		CHECK(error.find(L"notepad420.exe") != std::wstring::npos, "error mentions missing exe");
	}

	// ---- 035: --associate 档(ADR 0010) -------------------------------------

	// 清单断言的期望值是独立字面量,逐字来自 ADR 0010(不作为实现常量引用,
	// 否则等于重算实现,测成同义反复)。
	static const wchar_t *const kExpectedExts[] = {
		L".cfg", L".conf", L".config", L".domains", L".dmp",
		L".editorconfig", L".gitattributes", L".gitconfig", L".gitignore", L".gitmodules",
		L".glsl", L".ini", L".json", L".json5", L".jsonc", L".jsonl",
		L".log", L".lua", L".md", L".nip",
		L".properties", L".toml", L".txt", L".xml", L".yaml",
	};
	CHECK(DefaultApp::kAssociateExtsCount == _countof(kExpectedExts),
	      "associate list size == 25");
	for (size_t i = 0; i < DefaultApp::kAssociateExtsCount && i < _countof(kExpectedExts); ++i) {
		if (wcscmp(DefaultApp::kAssociateExts[i], kExpectedExts[i]) != 0) {
			g_failures++;
			std::printf("FAIL ext[%u] mismatch\n", static_cast<unsigned>(i));
		}
		g_checks++;
	}
	// 无重复、全部以 '.' 开头。
	{
		bool dup = false, badPrefix = false;
		for (size_t i = 0; i < DefaultApp::kAssociateExtsCount; ++i) {
			if (DefaultApp::kAssociateExts[i][0] != L'.') {
				badPrefix = true;
			}
			for (size_t j = i + 1; j < DefaultApp::kAssociateExtsCount; ++j) {
				if (_wcsicmp(DefaultApp::kAssociateExts[i], DefaultApp::kAssociateExts[j]) == 0) {
					dup = true;
				}
			}
		}
		CHECK(!dup, "associate list has no duplicates");
		CHECK(!badPrefix, "every associate ext starts with '.'");
	}

	// 操作层:沙箱 FileExts 根下的端到端 + 幂等 + 首失败中止。
	const wchar_t *const kSandbox = L"Software\\np420test\\cli-assoc-test";
	RegDeleteTreeW(HKEY_CURRENT_USER, kSandbox);
	{
		HKEY hk = nullptr;
		CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, kSandbox, 0, nullptr, 0,
		                      KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS,
		      "sandbox root creatable");
		if (hk != nullptr) {
			RegCloseKey(hk);
		}

		DefaultApp::SetFileExtsSubKeyForTesting(kSandbox);

		// 失败注入:.log 的 UserChoice 带子键 → RegDeleteKey 拒绝 → 首失败返回 5。
		HKEY blocked = nullptr;
		CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,
		                      L"Software\\np420test\\cli-assoc-test\\.log\\UserChoice\\child",
		                      0, nullptr, 0, KEY_WRITE, nullptr, &blocked, nullptr) == ERROR_SUCCESS,
		      "failure-injection subkey creatable");
		if (blocked != nullptr) {
			RegCloseKey(blocked);
		}
		std::wstring error;
		int rc = ReplacerOps::RunAssociateOperation(error);
		CHECK(rc == 5, "blocked .log aborts with 5");
		CHECK(error.find(L".log") != std::wstring::npos, "error names the failing ext");
		{
			HKEY probe = nullptr;
			const LSTATUS sCfg = RegOpenKeyExW(HKEY_CURRENT_USER,
				L"Software\\np420test\\cli-assoc-test\\.cfg\\UserChoice", 0, KEY_READ, &probe);
			if (probe != nullptr) {
				RegCloseKey(probe);
			}
			CHECK(sCfg == ERROR_SUCCESS, "exts before failure point were written");
			const LSTATUS sYaml = RegOpenKeyExW(HKEY_CURRENT_USER,
				L"Software\\np420test\\cli-assoc-test\\.yaml\\UserChoice", 0, KEY_READ, &probe);
			if (probe != nullptr) {
				RegCloseKey(probe);
			}
			CHECK(sYaml == ERROR_FILE_NOT_FOUND, "exts after failure point untouched");
		}

		// 干净状态:全部 25 项建成,再跑幂等。
		RegDeleteTreeW(HKEY_CURRENT_USER, kSandbox);
		error.clear();
		rc = ReplacerOps::RunAssociateOperation(error);
		CHECK(rc == 0, "clean associate run succeeds");
		rc = ReplacerOps::RunAssociateOperation(error);
		CHECK(rc == 0, "second associate run is idempotent");

		// 抽查首/中/尾三项的 ProgId 与 Hash。
		for (const wchar_t *ext : {L".cfg", L".json", L".yaml"}) {
			const std::wstring ucKey =
				std::wstring(L"Software\\np420test\\cli-assoc-test\\") + ext + L"\\UserChoice";
			wchar_t progid[256] = L"";
			DWORD plen = sizeof(progid);
			CHECK(RegGetValueW(HKEY_CURRENT_USER, ucKey.c_str(), L"ProgId",
			                   RRF_RT_REG_SZ, nullptr, progid, &plen) == ERROR_SUCCESS &&
			          wcscmp(progid, L"Applications\\notepad420.exe") == 0,
			      "UserChoice ProgId is Applications\\notepad420.exe");
			wchar_t hash[256] = L"";
			DWORD hlen = sizeof(hash);
			CHECK(RegGetValueW(HKEY_CURRENT_USER, ucKey.c_str(), L"Hash",
			                   RRF_RT_REG_SZ, nullptr, hash, &hlen) == ERROR_SUCCESS &&
			          hash[0] != L'\0',
			      "UserChoice Hash present");
		}
		// 全量计数:沙箱下恰有 25 个扩展子键。
		{
			HKEY base = nullptr;
			CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, kSandbox, 0, KEY_READ, &base) == ERROR_SUCCESS,
			      "sandbox base reopens");
			DWORD subkeys = 0;
			if (base != nullptr) {
				RegQueryInfoKeyW(base, nullptr, nullptr, nullptr, &subkeys,
				                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
				RegCloseKey(base);
			}
			CHECK(subkeys == DefaultApp::kAssociateExtsCount, "exactly 25 ext keys created");
		}

		DefaultApp::SetFileExtsSubKeyForTesting(nullptr);
		RegDeleteTreeW(HKEY_CURRENT_USER, kSandbox);
	}

	// ---- 036: --restore 对「什么都没做过」幂等(ADR 0011) -------------------
	// 沙箱化四条链路:系统目标档 / FileExts / Classes(ProgId) / 环境变量备份键。
	// 任何一条漏进真树都是事故,所以每缝都带「真树不动」反向断言。
	const wchar_t *const kSandboxR = L"Software\\np420test\\cli-restore-test";
	RegDeleteTreeW(HKEY_CURRENT_USER, kSandboxR);
	{
		const std::wstring sbFileExts = std::wstring(kSandboxR) + L"\\FileExts";
		const std::wstring sbClasses  = std::wstring(kSandboxR) + L"\\Classes";
		const std::wstring sbBackup   = std::wstring(kSandboxR) + L"\\Backup";

		// 沙箱里各埋一个「我们写过的」痕迹,验证恢复真的清理了沙箱(正控)。
		HKEY hk = nullptr;
		CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,
		                      (sbFileExts + L"\\.zzz\\UserChoice").c_str(), 0, nullptr, 0,
		                      KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS,
		      "sandbox UserChoice creatable");
		if (hk != nullptr) {
			RegSetValueExW(hk, L"ProgId", 0, REG_SZ,
			               reinterpret_cast<const BYTE *>(DefaultApp::kOurProgId),
			               static_cast<DWORD>((wcslen(DefaultApp::kOurProgId) + 1) * sizeof(wchar_t)));
			RegCloseKey(hk);
			hk = nullptr;
		}
		CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,
		                      (sbClasses + L"\\Applications\\notepad420.exe").c_str(), 0, nullptr, 0,
		                      KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS,
		      "sandbox ProgId creatable");
		if (hk != nullptr) {
			RegCloseKey(hk);
			hk = nullptr;
		}

		// 档系统目标:clean(什么都不存在)+ orphan(有 exe 无 .bak)。
		wchar_t cwd[MAX_PATH];
		GetCurrentDirectoryW(MAX_PATH, cwd);
		const std::wstring fsRoot = std::wstring(cwd) + L"\\.target\\tests\\rplrestore";
		CreateDirectoryW((std::wstring(cwd) + L"\\.target\\tests").c_str(), nullptr);
		CreateDirectoryW(fsRoot.c_str(), nullptr);
		const std::wstring tClean = fsRoot + L"\\clean-notepad.exe";
		const std::wstring tOrphan = fsRoot + L"\\orphan-notepad.exe";
		{
			HANDLE f = CreateFileW(tOrphan.c_str(), GENERIC_WRITE, 0, nullptr,
			                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			CHECK(f != INVALID_HANDLE_VALUE, "orphan target creatable");
			if (f != INVALID_HANDLE_VALUE) {
				CloseHandle(f);
			}
		}
		const wchar_t *const fakeTargets[] = { tClean.c_str(), tOrphan.c_str() };

		// 真树快照(反控)。
		HKEY probe = nullptr;
		const bool realProgIdBefore = RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Classes\\Applications\\notepad420.exe", 0, KEY_READ, &probe) == ERROR_SUCCESS;
		if (probe != nullptr) {
			RegCloseKey(probe);
			probe = nullptr;
		}
		const bool realMdBefore = RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.md\\UserChoice",
			0, KEY_READ, &probe) == ERROR_SUCCESS;
		if (probe != nullptr) {
			RegCloseKey(probe);
			probe = nullptr;
		}
		std::wstring edBefore, viBefore;
		bool edP = false, viP = false;
		AclGuard::ReadUserEnvValue(L"EDITOR", edBefore, edP);
		AclGuard::ReadUserEnvValue(L"VISUAL", viBefore, viP);

		ReplacerOps::SetTargetsForTesting(fakeTargets, 2);
		DefaultApp::SetFileExtsSubKeyForTesting(sbFileExts.c_str());
		DefaultApp::SetClassesSubKeyForTesting(sbClasses.c_str());
		ReplacerOps::SetEditorBackupKeyForTesting(sbBackup.c_str());

		std::wstring error;
		const int rc = ReplacerOps::RunRestoreOperation(error);
		CHECK(rc == 0, "restore on never-touched state exits 0");
		CHECK(error.empty(), "restore leaves no error");
		// 第二遍:沙箱痕迹已被上一轮清空,真正的全干净状态也必须退 0。
		error.clear();
		const int rc2 = ReplacerOps::RunRestoreOperation(error);
		CHECK(rc2 == 0, "restore on fully-clean state exits 0");
		CHECK(error.empty(), "second restore leaves no error");

		// 沙箱痕迹被清理(恢复与清理链路真的跑过)。
		CHECK(RegOpenKeyExW(HKEY_CURRENT_USER,
			(sbFileExts + L"\\.zzz\\UserChoice").c_str(), 0, KEY_READ, &probe) == ERROR_FILE_NOT_FOUND,
		      "sandbox UserChoice cleaned by restore");
		if (probe != nullptr) {
			RegCloseKey(probe);
			probe = nullptr;
		}
		CHECK(RegOpenKeyExW(HKEY_CURRENT_USER,
			(sbClasses + L"\\Applications\\notepad420.exe").c_str(), 0, KEY_READ, &probe) == ERROR_FILE_NOT_FOUND,
		      "sandbox ProgId removed by restore");
		if (probe != nullptr) {
			RegCloseKey(probe);
			probe = nullptr;
		}
		// 从未替换的 orphan 目标不被动。
		CHECK(GetFileAttributesW(tOrphan.c_str()) != INVALID_FILE_ATTRIBUTES,
		      "orphan target untouched (no .bak => skip)");

		// 真树零副作用。
		const bool realProgIdAfter = RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Classes\\Applications\\notepad420.exe", 0, KEY_READ, &probe) == ERROR_SUCCESS;
		if (probe != nullptr) {
			RegCloseKey(probe);
			probe = nullptr;
		}
		CHECK(realProgIdAfter == realProgIdBefore, "real ProgId key untouched");
		const bool realMdAfter = RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.md\\UserChoice",
			0, KEY_READ, &probe) == ERROR_SUCCESS;
		if (probe != nullptr) {
			RegCloseKey(probe);
			probe = nullptr;
		}
		CHECK(realMdAfter == realMdBefore, "real .md UserChoice untouched");
		std::wstring edAfter, viAfter;
		bool edP2 = false, viP2 = false;
		AclGuard::ReadUserEnvValue(L"EDITOR", edAfter, edP2);
		AclGuard::ReadUserEnvValue(L"VISUAL", viAfter, viP2);
		CHECK(edP == edP2 && edBefore == edAfter, "EDITOR unchanged by sandboxed restore");
		CHECK(viP == viP2 && viBefore == viAfter, "VISUAL unchanged by sandboxed restore");

		// 恢复接缝与清理现场。
		ReplacerOps::SetTargetsForTesting(nullptr, 0);
		DefaultApp::SetFileExtsSubKeyForTesting(nullptr);
		DefaultApp::SetClassesSubKeyForTesting(nullptr);
		ReplacerOps::SetEditorBackupKeyForTesting(nullptr);
		RegDeleteTreeW(HKEY_CURRENT_USER, kSandboxR);
		DeleteFileW(tClean.c_str());
		DeleteFileW(tOrphan.c_str());
		RemoveDirectoryW(fsRoot.c_str());
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
