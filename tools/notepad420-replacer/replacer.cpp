/**
 * notepad420 记事本替换工具（GUI 外壳）
 *
 * 实际逻辑全部在 ReplacerOps.cpp（与 CLI --replace/--editor/--restore/--associate 共用）。
 * 不带参数运行 → GUI；带 CLI 开关 → 无窗口执行后按退出码退出。
 * manifest 声明 requireAdministrator：启动即由系统弹 UAC，进程内恒为管理员。
 */
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include "AclGuard.h"
#include "DefaultApp.h"
#include "ReplacerOps.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

/// UI 线程消息：进度与完成。
constexpr UINT WM_APP_PROGRESS = WM_APP + 1;	// wParam = pos, lParam = range
constexpr UINT WM_APP_DONE     = WM_APP + 2;	// lParam = WorkJob*（UI 线程接管所有权）

/// 前向声明：工作线程要用。
bool CreateDesktopShortcut(std::wstring &error);

/// 进度回调：工作线程上运行，只能 PostMessage 回 UI 线程。
/// 各阶段局部进度在此换算为全局累积位置；g_progBase/g_progTotal 由工作线程维护。
static HWND g_hwndDlg = nullptr;
HWND g_hwndProgress = nullptr;
static unsigned g_progBase = 0;		// 当前操作在全局进度中的起点
static unsigned g_progTotal = 0;		// 全阶段总步数（工作线程开局一次算定）

void ReplacerProgress(unsigned pos, unsigned range) {
	(void)range;	// 各阶段自带 range 已折算进 g_progBase
	if (g_hwndDlg != nullptr && g_progTotal > 0) {
		PostMessageW(g_hwndDlg, WM_APP_PROGRESS, g_progBase + pos, g_progTotal);
	}
}

void SetBusy(HWND hwnd, bool busy) {
	EnableWindow(GetDlgItem(hwnd, ID_REPLACE), !busy);
	EnableWindow(GetDlgItem(hwnd, ID_RESTORE), !busy);
	if (busy) {
		SendMessageW(g_hwndProgress, PBM_SETPOS, 0, 0);
		SetCursor(LoadCursorW(nullptr, IDC_WAIT));
	} else {
		SetCursor(LoadCursorW(nullptr, IDC_ARROW));
		SendMessageW(g_hwndProgress, PBM_SETPOS, 0, 0);		// 完成后进度条归零
	}
}

/// 工作线程载荷:一次「替换/恢复」的全部输入与产出。
struct WorkJob {
	bool bReplace;
	bool makeShortcut;
	bool associate;
	bool setEditor;
	int rc = 1;
	std::wstring error;
	std::wstring errTitle;
	std::wstring doneMsg;
};

DWORD WINAPI WorkThreadProc(LPVOID lparam) {
	WorkJob *job = static_cast<WorkJob *>(lparam);
	// 本线程要用 COM(CreateDesktopShortcut → IShellLink),未初始化会隐死或拒绝。
	const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	// 全阶段步数一次算定,进度条设一次 range;各阶段走自己的局部 ticks。
	const unsigned stepsReplace = 7;       // 3 targets + IFEO + UserChoice + ProgId + notify
	const unsigned stepsAssociate = 26;    // 25 exts + notify
	const unsigned stepsEditor = 1;
	const unsigned stepsShortcut = 1;
	if (job->bReplace) {
		g_progTotal = stepsReplace
					+ (job->associate ? stepsAssociate : 0)
					+ (job->setEditor ? stepsEditor : 0)
					+ (job->makeShortcut ? stepsShortcut : 0);
	} else {
		g_progTotal = stepsReplace;		// restore 与 replace 同构:7 steps
	}
	g_progBase = 0;
	ReplacerProgress(0, 0);		// 推一次(0, total),让 UI 把 range 定拢

	if (job->bReplace) {
		job->rc = ReplacerOps::RunReplaceOperation(job->error, ReplacerProgress);
		g_progBase = (job->rc == 0) ? stepsReplace : g_progTotal;
		if (job->rc == 0 && job->associate) {
			if (ReplacerOps::RunAssociateOperation(job->error, ReplacerProgress) != 0) {
				job->rc = 5; job->errTitle = L"notepad420 关联失败";
			}
			g_progBase += stepsAssociate;
		}
		if (job->rc == 0 && job->setEditor) {
			if (ReplacerOps::RunEditorOperation(job->error, ReplacerProgress) != 0) {
				job->rc = 6; job->errTitle = L"notepad420 环境变量失败";
			}
			g_progBase += stepsEditor;
		}
		if (job->rc == 0 && job->makeShortcut) {
			if (!CreateDesktopShortcut(job->error)) {
				job->rc = 1; job->errTitle = L"notepad420 快捷方式失败";
			}
			ReplacerProgress(stepsShortcut, 0);		// 内部无 tick,手动补上该步
			g_progBase += stepsShortcut;
		}
		if (job->rc == 0) {
			std::wstring msg = L"替换完成";
			if (job->associate) {
				msg += L"\n文本文件默认打开方式已改为notepad420";
			}
			if (job->setEditor) {
				msg += L"\nEDITOR & VISUAL已指向" + ReplacerOps::LocalNotepad420Path();
				msg += L"\n环境变量更改需重启终端生效";
			}
			job->doneMsg = msg;
		} else if (job->errTitle.empty()) {
			job->errTitle = L"notepad420 替换失败";
		}
	} else {
		g_progBase = 0;
		job->rc = ReplacerOps::RunRestoreOperation(job->error, ReplacerProgress);
		if (job->rc == 0) {
			job->doneMsg = L"恢复完成";
		} else {
			job->errTitle = L"notepad420 恢复失败";
		}
	}
	// 最后的 COM 卸载与结果投递。COM错误不拦截结果(gurantee: CoUninit 只在 hr=S_OK 时)。
	if (SUCCEEDED(hr)) {
		CoUninitialize();
	}
	PostMessageW(g_hwndDlg, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(job));
	return 0;
}

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

INT_PTR CALLBACK MainDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		CheckDlgButton(hwnd, IDC_ASSOCIATE, BST_CHECKED);
		CheckDlgButton(hwnd, IDC_EDITOR, BST_CHECKED);
		SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 100));
		g_hwndProgress = GetDlgItem(hwnd, IDC_PROGRESS);
		g_hwndDlg = hwnd;
		return TRUE;
	case WM_APP_PROGRESS: {
		const unsigned pos = static_cast<unsigned>(wParam);
		const unsigned range = static_cast<unsigned>(lParam);
		if (range != static_cast<unsigned>(SendMessageW(g_hwndProgress, PBM_GETRANGE, FALSE, 0))) {
			SendMessageW(g_hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, static_cast<WORD>(range)));
		}
		SendMessageW(g_hwndProgress, PBM_SETPOS, pos, 0);
		return TRUE;
	}
	case WM_APP_DONE: {
		WorkJob *job = reinterpret_cast<WorkJob *>(lParam);
		SetBusy(hwnd, false);
		if (job->rc == 0) {
			MessageBoxW(hwnd, job->doneMsg.c_str(), L"notepad420", MB_ICONINFORMATION);
		} else {
			MessageBoxW(hwnd, job->error.c_str(), job->errTitle.empty() ? L"notepad420" : job->errTitle.c_str(), MB_ICONSTOP);
		}
		delete job;
		return TRUE;
	}
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_REPLACE:
		case ID_RESTORE: {
			WorkJob *job = new WorkJob;
			job->bReplace = (LOWORD(wParam) == ID_REPLACE);
			job->makeShortcut = IsDlgButtonChecked(hwnd, IDC_SHORTCUT) == BST_CHECKED;
			job->associate = IsDlgButtonChecked(hwnd, IDC_ASSOCIATE) == BST_CHECKED;
			job->setEditor = IsDlgButtonChecked(hwnd, IDC_EDITOR) == BST_CHECKED;
			SetBusy(hwnd, true);
			HANDLE thread = CreateThread(nullptr, 0, WorkThreadProc, job, 0, nullptr);
			if (thread == nullptr) {
				delete job;
				SetBusy(hwnd, false);
				MessageBoxW(hwnd, L"未能创建工作线程", L"notepad420", MB_ICONSTOP);
				return TRUE;
			}
			CloseHandle(thread);
			return TRUE;
		}
		}
		break;
	case WM_CLOSE:
		if (g_hwndProgress && !IsWindowEnabled(GetDlgItem(hwnd, ID_REPLACE))) {
			return TRUE;		// 任务进行中,禁止关哌
		}
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
	// 进度条控件(msctls_progress32)在 comctl32 v5 内,需初始化一次。
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
	InitCommonControlsEx(&icc);
	DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDlgProc, 0);
	CoUninitialize();
	return 0;
}
