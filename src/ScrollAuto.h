/******************************************************************************
*
* notepad420
*
* ScrollAuto.h
*   Middle-button auto-scroll for the editor (NP3-style).
*
*   Behavior (mirrors Notepad3):
*   - Hold the middle button and drag: the page scrolls while the pointer is
*     outside the dead zone; releasing the button after more than
*     kAutoScrollClickMs stops.
*   - Quick click (press+release within the threshold) toggles a persistent
*     mode that scrolls continuously following the pointer; any other
*     button/wheel/key press exits.
*
*   The scroll-rate math is pure (no Windows dependency) so it is unit tested
*   headlessly by tools/tests/scroll_auto_test.cpp.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <windows.h>

namespace ScrollAuto {

/// Tuning constants, taken from Notepad3's autoscroll so the feel matches.
constexpr UINT kTimerMs = 30;			///< tick interval
constexpr int kDeadZonePx = 15;			///< |deltaY| below this scrolls nothing
constexpr double kDivisor = 60.0;		///< speed = delta/devisor per tick
constexpr ULONGLONG kClickMs = 200;		///< press+release within this = toggle mode

/// One tick of the scroll integrator. `deltaY` is (mouse.y - origin.y);
/// `accumY` carries sub-line remainders across ticks. Returns the number of
/// whole lines to scroll this tick (signed; positive = down) and updates
/// `accumY`. Inside the dead zone the accumulator resets to zero.
inline int Tick(int deltaY, double &accumY) noexcept {
	const int absDelta = deltaY < 0 ? -deltaY : deltaY;
	if (absDelta <= kDeadZonePx) {
		accumY = 0.0;
		return 0;
	}
	const double speed =
		(deltaY - (deltaY > 0 ? kDeadZonePx : -kDeadZonePx)) / kDivisor;
	accumY += speed;
	const int lines = static_cast<int>(accumY);
	accumY -= static_cast<double>(lines);
	return lines;
}

/// Whether a press->release of `elapsedMs` counts as a hold (scroll while
/// held, stop on release) or a quick click (toggle persistent mode).
constexpr bool IsToggleClick(ULONGLONG elapsedMs) noexcept {
	return elapsedMs <= kClickMs;
}

} // namespace ScrollAuto

/// Starts watching `hwndEdit` for middle-button auto-scroll. Idempotent.
void ScrollAutoAttach(HWND hwndEdit) noexcept;

/// Releases the subclass hook and any active capture/timer.
void ScrollAutoDetach(HWND hwndEdit) noexcept;