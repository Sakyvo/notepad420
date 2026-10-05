// Tests for the auto-scroll tick integrator (src/ScrollAuto.cpp).
//
// The scroll math is pure (no Windows state), so it is exercised headlessly.
// Expected values are derived from the algorithm contract, not from running
// the code: delta within the dead zone scrolls nothing; past the dead zone,
// speed = (delta - deadzone) / divisor lines per 30ms tick, accumulating
// fractional remainders.
#include <cstdio>
#include <cmath>

#include "../../src/ScrollAuto.h"

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

static void TestDeadZone() {
	double acc = 0.0;
	// Every offset within +/-15 scrolls nothing.
	for (int d = -15; d <= 15; ++d) {
		acc = 7.5; // contaminate: dead-zone must also zero the accumulator
		CHECK(ScrollAuto::Tick(d, acc) == 0, "dead zone scrolls zero lines");
		CHECK(acc == 0.0, "dead zone resets the accumulator");
	}
}

static void TestSpeedProfile() {
	double acc = 0.0;

	// Just past the dead zone: (30 - 15) / 60 = 0.25 lines/tick. Needs 4 ticks
	// to accumulate a whole line.
	CHECK(ScrollAuto::Tick(30, acc) == 0, "first tick not a whole line");
	CHECK(std::fabs(acc - 0.25) < 1e-9, "quarter-line accumulated");
	CHECK(ScrollAuto::Tick(30, acc) == 0, "second tick");
	CHECK(ScrollAuto::Tick(30, acc) == 0, "third tick");
	CHECK(ScrollAuto::Tick(30, acc) == 1, "fourth tick emits one line");
	CHECK(std::fabs(acc) < 1e-9, "whole line consumed");

	// 75 px => (75-15)/60 = 1.0 lines/tick steady state.
	acc = 0.0;
	CHECK(ScrollAuto::Tick(75, acc) == 1, "75px is one line per tick");
	CHECK(ScrollAuto::Tick(75, acc) == 1, "steady one line per tick");

	// Farther away is proportionally faster: 135px => (135-15)/60 = 2 lines.
	acc = 0.0;
	CHECK(ScrollAuto::Tick(135, acc) == 2, "135px is two lines per tick");
}

static void TestNegativeDirection() {
	double acc = 0.0;

	// Negative (scroll up) mirrors positive.
	CHECK(ScrollAuto::Tick(-30, acc) == 0, "first tick up, fraction");
	CHECK(ScrollAuto::Tick(-30, acc) == 0, "second");
	CHECK(ScrollAuto::Tick(-30, acc) == 0, "third");
	CHECK(ScrollAuto::Tick(-30, acc) == -1, "fourth tick emits -1 line");

	// -135px => -2 lines/tick.
	acc = 0.0;
	CHECK(ScrollAuto::Tick(-135, acc) == -2, "two lines per tick up");
}

static void TestDirectionChangeAndReturnToCenter() {
	double acc = 0.0;

	// Build upward momentum with a fractional rate (30px = 0.25 lines/tick),
	// then pull back into the dead zone: stop + reset.
	ScrollAuto::Tick(30, acc);
	ScrollAuto::Tick(30, acc);
	CHECK(acc != 0.0, "momentum accumulated");
	CHECK(ScrollAuto::Tick(5, acc) == 0, "back inside dead zone: no lines");
	CHECK(acc == 0.0, "accumulator cleared on dead-zone entry");

	// Direction reversal works straight away: -30 => quarter-line up.
	CHECK(ScrollAuto::Tick(-30, acc) == 0, "reversal begins cleanly");
	CHECK(std::fabs(acc - (-0.25)) < 1e-9, "negative quarter accumulated");
}

static void TestToggleClickClassifier() {
	// <= 200 ms release toggles persistent mode; longer is hold-drag.
	CHECK(ScrollAuto::IsToggleClick(0), "0 ms is a click");
	CHECK(ScrollAuto::IsToggleClick(199), "199 ms is a click");
	CHECK(ScrollAuto::IsToggleClick(200), "200 ms is still a click");
	CHECK(!ScrollAuto::IsToggleClick(201), "201 ms is a hold");
	CHECK(!ScrollAuto::IsToggleClick(5000), "5 s is a hold");
}

int main() {
	TestDeadZone();
	TestSpeedProfile();
	TestNegativeDirection();
	TestDirectionChangeAndReturnToCenter();
	TestToggleClickClassifier();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}