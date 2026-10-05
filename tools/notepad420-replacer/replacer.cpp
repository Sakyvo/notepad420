/**
 * notepad420 记事本替换工具（GUI 外壳）
 *
 * 实际逻辑全部在 ReplacerOps.cpp（与 CLI --replace/--editor/--restore/--associate 共用）。
 * 不带参数运行 → GUI；带 CLI 开关 → 无窗口执行后按退出码退出。
 * manifest 声明 requireAdministrator：启动即由系统弹 UAC，进程内恒为管理员。
 */
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include "AclGuard.h"
#include "DefaultApp.h"
#include "ReplacerOps.h"
#include "resource.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

/// 桌面快捷方式 notepad420.lnk → 同目录 notepad420.exe。
bool CreateDesktopShortcut(std::wstring &error) {
	PWSTR desktop = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktop))) {
		error = L"找不到桌面目录";
		return false;
	}
	std::wstring lnk = desktop;
	CoTaskMemFree(desktop);
	if (!lnk.empty() && lnk.back() != L'\\') {
		lnk += L'\\';
	}
	lnk += L"notepad420.lnk";
	return AclGuard::CreateShortcut(lnk, ReplacerOps::LocalNotepad420Path(), error) == ERROR_SUCCESS;
}

INT_PTR CALLBACK MainDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
	switch (msg) {
	case WM_INITDIALOG:
		CheckDlgButton(hwnd, IDC_EDITOR, BST_CHECKED);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_REPLACE: {
			std::wstring error;
			const int rc = ReplacerOps::RunReplaceOperation(error);
			if (rc == 0) {
				const bool makeShortcut =
					IsDlgButtonChecked(hwnd, IDC_SHORTCUT) == BST_CHECKED;
				const bool associate =
					IsDlgButtonChecked(hwnd, IDC_ASSOCIATE) == BST_CHECKED;
				const bool setEditor =
					IsDlgButtonChecked(hwnd, IDC_EDITOR) == BST_CHECKED;
				if (associate && ReplacerOps::RunAssociateOperation(error) != 0) {
					MessageBoxW(hwnd, error.c_str(), L"notepad420 关联失败", MB_ICONSTOP);
					return TRUE;
				}
				if (setEditor && ReplacerOps::RunEditorOperation(error) != 0) {
					MessageBoxW(hwnd, error.c_str(), L"notepad420 环境变量失败", MB_ICONSTOP);
					return TRUE;
				}
				if (makeShortcut && !CreateDesktopShortcut(error)) {
					MessageBoxW(hwnd, error.c_str(), L"notepad420 快捷方式失败", MB_ICONSTOP);
					return TRUE;
				}
				std::wstring doneMsg = L"替换完成";
				if (associate) {
					doneMsg += L"\n\n文本扩展默认打开方式已指向 notepad420";
				}
				if (setEditor) {
					doneMsg += L"\n\nEDITOR / VISUAL 已指向 " + ReplacerOps::LocalNotepad420Path()
						+ L"\n\n请重启你正在使用的终端(Pi/pebrel/PowerShell 等)后再 Ctrl+G 生效";
				}
				MessageBoxW(hwnd, doneMsg.c_str(), L"notepad420", MB_ICONINFORMATION);
			} else {
				MessageBoxW(hwnd, error.c_str(), L"notepad420 替换失败", MB_ICONSTOP);
			}
			return TRUE;
		}
		case ID_RESTORE: {
			std::wstring error;
			const int rc = ReplacerOps::RunRestoreOperation(error);
			if (rc == 0) {
				MessageBoxW(hwnd, L"恢复完成", L"notepad420", MB_ICONINFORMATION);
			} else {
				MessageBoxW(hwnd, error.c_str(), L"notepad420 恢复失败", MB_ICONSTOP);
			}
			return TRUE;
		}
		}
		break;
	case WM_CLOSE:
		EndDialog(hwnd, 0);
		return TRUE;
	}
	return FALSE;
}

/// CLI 模式：静默执行一个动作，返回进程退出码。
/// NP420RPL_DEBUG 置位时把阶段结果写到 %TEMP%\np420rpl-debug.txt（UTF-16 追加）。
int RunCli(const wchar_t *opArg) {
	wchar_t dbgPath[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, dbgPath);
	lstrcatW(dbgPath, L"np420rpl-debug.txt");
	wchar_t dbgFlag[8] = L"";
	const bool dbg = GetEnvironmentVariableW(L"NP420RPL_DEBUG", dbgFlag, 8) > 0;
	auto dbgLog = [&](const wchar_t *text) {
		if (!dbg) {
			return;
		}
		HANDLE f = CreateFileW(dbgPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
		if (f == INVALID_HANDLE_VALUE) {
			return;
		}
		SetFilePointer(f, 0, nullptr, FILE_END);
		DWORD w = 0;
		WriteFile(f, text, (DWORD)(lstrlenW(text) * sizeof(wchar_t)), &w, nullptr);
		CloseHandle(f);
	};
	const wchar_t *op = opArg;
	dbgLog(op);
	if (dbg) {
		std::wstring selfLine = L"|self=" + ReplacerOps::LocalNotepad420Path() + L"\r\n";
		dbgLog(selfLine.c_str());
	}
	dbgLog(L"|enter\r\n");
	if (!ReplacerOps::IsElevatedSelf()) {
		dbgLog(L"|gate:not-elevated\r\n");
		return 3;
	}
	if (!ReplacerOps::EnableRequiredPrivileges()) {
		dbgLog(L"|gate:privileges\r\n");
		return 1;
	}
	if (!AclGuard::ValidateSddl(AclGuard::BuildReplaceSddl().c_str())) {
		dbgLog(L"|gate:sddl\r\n");
		return 2;
	}
	std::wstring error;
	// 只取第一个空白前的 token 作开关（兼容尾随空白/引号内传入）。
	wchar_t tok[16] = L"";
	size_t n = 0;
	for (; opArg[n] != L'\0' && opArg[n] != L' ' && opArg[n] != L'\t' && n < 15; ++n) {
		tok[n] = opArg[n];
	}
	tok[n] = L'\0';
	int rc = 1;
	if (wcscmp(tok, L"--replace") == 0) {
		rc = ReplacerOps::RunReplaceOperation(error);
	} else if (wcscmp(tok, L"--editor") == 0) {
		rc = ReplacerOps::RunEditorOperation(error);
	} else if (wcscmp(tok, L"--restore") == 0) {
		rc = ReplacerOps::RunRestoreOperation(error);
	} else if (wcscmp(tok, L"--associate") == 0) {
		rc = ReplacerOps::RunAssociateOperation(error);
	}
	if (!error.empty()) {
		dbgLog(error.c_str());
		dbgLog(L"|rc-err\r\n");
	}
	wchar_t rcBuf[32] = L"";
	wsprintfW(rcBuf, L"|rc=%d\r\n", rc);
	dbgLog(rcBuf);
	return rc;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmdLine, int) {
	// CLI: --replace / --editor / --restore / --associate 任一出现即走静默路径（容忍前导空白/引号）。
	const wchar_t *cl = cmdLine;
	while (cl != nullptr && (*cl == L' ' || *cl == L'"')) {
		++cl;
	}
	if (cl != nullptr && cl[0] != L'\0') {
		const wchar_t *op = nullptr;
		if (wcsncmp(cl, L"--replace", 9) == 0) {
			op = cl;
		} else if (wcsncmp(cl, L"--editor", 8) == 0) {
			op = cl;
		} else if (wcsncmp(cl, L"--restore", 9) == 0) {
			op = cl;
		} else if (wcsncmp(cl, L"--associate", 11) == 0) {
			op = cl;
		}
		if (op != nullptr) {
			return RunCli(op);
		}
		// 未知参数：一律当 CLI 用法错误（静默），安装器只会传合法开关。
		return 1;
	}

	// asInvoker: manifest 不压 UAC,我们自己检查。非管理员：警告 + 自举。
	// requireAdministrator：系统已保证提权（用户拒绝 UAC 时进程根本不会启动）。
	if (!ReplacerOps::EnableRequiredPrivileges()) {
		MessageBoxW(nullptr, L"本机未能取得权限。",
					L"notepad420", MB_ICONSTOP);
		return 1;
	}
	// 自检 ACL 模板,模板非法则直接报错而不是等到替换时静默失败。
	if (!AclGuard::ValidateSddl(AclGuard::BuildReplaceSddl().c_str())) {
		MessageBoxW(nullptr, L"内置权限模板无效,程序无法继续。",
					L"notepad420", MB_ICONSTOP);
		return 1;
	}
	if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
		MessageBoxW(nullptr, L"COM 初始化失败,无法创建快捷方式。",
					L"notepad420", MB_ICONSTOP);
		return 1;
	}
	DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlgProc, 0);
	CoUninitialize();
	return 0;
}
