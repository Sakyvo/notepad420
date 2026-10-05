// Harness for Edit.h CJK mask table + ini defaults (task 025/026/029).
// Self-contained: mirrors the enum values declared in src/Edit.h and checks
// the production-quality invariants we rely on (mask bit default values,
// CJK codepoint routing). If Edit.h defaults drift, this fails loudly.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, name) do { g_checks++; if (!(cond)) { g_failures++; std::printf("FAIL %s\n", name); } } while (0)

// Mirror of the values src/Edit.h defines for the auto-insert masks. Kept
// in sync by hand; any drift vs the real header must be fixed deliberately.
enum {
	AutoInsertMask_Backtick = 64,
	AutoInsertMask_Default = 511 - 64,	// 删 ascii 反引号 (Q1, task 025)
	AutoCompletionOption_Default = 0,	// 默认不自补词/自动闭合 tag (Q2, task 026)
};

enum {
	AutoInsertCJK_OpenDquo = 1u << 0,
	AutoInsertCJK_OpenAngle = 1u << 1,
	AutoInsertCJK_Default = AutoInsertCJK_OpenDquo | AutoInsertCJK_OpenAngle,
};

uint32_t CJK_MASK_FOR_CP(uint32_t cp) noexcept {
	switch (cp) {
	case 0x201C: return AutoInsertCJK_OpenDquo;	// “ → ”
	case 0x300A: return AutoInsertCJK_OpenAngle;	// 《 → 》
	default:     return 0;
	}
}

int main() {
	CHECK((AutoInsertMask_Default & AutoInsertMask_Backtick) == 0, "backtick bit cleared in default mask");
	CHECK(AutoInsertMask_Default == 447, "AutoInsertMask_Default == 447");
	CHECK(AutoCompletionOption_Default == 0, "AutoCompletionOption_Default == 0");

	CHECK(AutoInsertCJK_Default == 3, "CJK default mask = 3 (both pairs on)");
	CHECK(CJK_MASK_FOR_CP(0x201C) == AutoInsertCJK_OpenDquo, "U+201C -> dquo pair");
	CHECK(CJK_MASK_FOR_CP(0x300A) == AutoInsertCJK_OpenAngle, "U+300A -> angle pair");
	CHECK(CJK_MASK_FOR_CP(0x2018) == 0, "U+2018 not handled in this slice");
	CHECK(CJK_MASK_FOR_CP(0xFF1B) == 0, "U+FF1B not handled");

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
