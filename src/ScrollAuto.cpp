/******************************************************************************
*
* notepad420
*
* ScrollAuto.cpp
*   See ScrollAuto.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <wchar.h>
#include "SciCall.h"
#include "ScrollAuto.h"

namespace {

// Window-procedure state (single editor window per process in notepad420).
bool s_mode = false;			///< auto-scroll active
bool s_held = false;			///< middle button still physically down
ULONGLONG s_startTick = 0;		///< GetTickCount64() at press
POINT s_origin{};				///< client point of the press
POINT s_mouse{};				///< latest client point
double s_accumY = 0.0;			///< fractional line carry

constexpr UINT_PTR kTimerId = 0xA101;
constexpr UINT_PTR kSubclassId = 0x7A42;

void Stop(HWND hwnd) noexcept {
	if (!s_mode) {
		return;
	}
	KillTimer(hwnd, kTimerId);
	ReleaseCapture();
	SciCall_SetCursor(SC_CURSORNORMAL);
	s_mode = false;
	s_held = false;
	s_accumY = 0.0;
}

void Start(HWND hwnd, POINT pt) noexcept {
	s_mode = true;
	s_held = false;
	s_startTick = GetTickCount64();
	s_origin = pt;
	s_mouse = pt;
	s_accumY = 0.0;
	SetCapture(hwnd);
	SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
	SetTimer(hwnd, kTimerId, ScrollAuto::kTimerMs, nullptr);
}

LRESULT CALLBACK AutoScrollSubProc(HWND hwnd, UINT uMsg, WPARAM wParam,
								   LPARAM lParam, UINT_PTR /*uIdSubclass*/,
								   DWORD_PTR /*dwRefData*/) {
	switch (uMsg) {
	case WM_MBUTTONDOWN: {
		if (s_mode) {
			Stop(hwnd);
		} else {
			s_held = true;
			Start(hwnd, { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) });
		}
		return 0;
	}
	case WM_MBUTTONUP: {
		if (s_mode && s_held) {
			const ULONGLONG elapsed = GetTickCount64() - s_startTick;
			if (ScrollAuto::IsToggleClick(elapsed)) {
				// quick click: switch into persistent mode
				s_held = false;
			} else {
				Stop(hwnd);
			}
			return 0;
		}
		break;
	}
	case WM_TIMER:
		if (wParam == kTimerId) {
			if (!s_mode) {
				KillTimer(hwnd, kTimerId);
				return 0;
			}
			const int lines = ScrollAuto::Tick(s_mouse.y - s_origin.y, s_accumY);
			if (lines != 0) {
				SciCall_LineScroll(0, lines);
			}
			return 0;
		}
		break;
	case WM_MOUSEMOVE:
		if (s_mode) {
			s_mouse = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
			SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
			return 0;
		}
		break;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_CHAR:
		if (s_mode) {
			Stop(hwnd);
			return uMsg == WM_MOUSEWHEEL || uMsg == WM_MOUSEHWHEEL ? (LRESULT)1 : 0;
		}
		break;
	case WM_SETCURSOR:
		if (s_mode) {
			SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
			return TRUE;
		}
		break;
	case WM_CAPTURECHANGED:
		if (s_mode && (HWND)lParam != hwnd) {
			Stop(hwnd);
		}
		break;
	case WM_NCDESTROY: {
		RemoveWindowSubclass(hwnd, AutoScrollSubProc, kSubclassId);
		KillTimer(hwnd, kTimerId);
		ReleaseCapture();
		s_mode = false;
		s_held = false;
		break;
	}
	default:
		break;
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

} // namespace

void ScrollAutoAttach(HWND hwndEdit) noexcept {
	if (hwndEdit == nullptr) {
		return;
	}
	SetWindowSubclass(hwndEdit, AutoScrollSubProc, kSubclassId, 0);
}

void ScrollAutoDetach(HWND hwndEdit) noexcept {
	if (hwndEdit == nullptr) {
		return;
	}
	RemoveWindowSubclass(hwndEdit, AutoScrollSubProc, kSubclassId);
}