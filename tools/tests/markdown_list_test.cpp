// Unit tests for the Markdown list auto-completion rules (src/MarkdownList.h).
//
// Pure byte-level logic, no Windows dependency. Build and run:
//   tools\run-tests.bat
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../src/MarkdownList.h"

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

static void TestMatchBullets() {
	MdListMarker m;
	CHECK(MdListMatchMarker("- A", m), "dash bullet matches");
	CHECK(std::strcmp(m.marker, "- ") == 0, "dash marker");
	CHECK(!m.ordered, "dash not ordered");

	CHECK(MdListMatchMarker("* item", m) && std::strcmp(m.marker, "* ") == 0, "star marker");
	CHECK(MdListMatchMarker("+ item", m) && std::strcmp(m.marker, "+ ") == 0, "plus marker");

	CHECK(MdListMatchMarker("  - nested", m), "indented bullet matches");
	CHECK(std::strcmp(m.indent, "  ") == 0, "indent preserved");
	CHECK(std::strcmp(m.marker, "- ") == 0, "marker after indent");

	// Task list: the checkbox bytes belong to the marker (kedit non-capturing suffix).
	CHECK(MdListMatchMarker("- [x] done", m), "checked box matches");
	CHECK(std::strcmp(m.marker, "- [x] ") == 0, "checkbox bytes are in the marker");
	CHECK(MdListMatchMarker("- [ ] todo", m) && std::strcmp(m.marker, "- [ ] ") == 0, "unchecked box marker");

	// "> " block quote, up to three leading spaces.
	CHECK(MdListMatchMarker("> quote", m) && std::strcmp(m.marker, "> ") == 0, "quote marker");
	CHECK(MdListMatchMarker("   > q", m), "3-space quote matches");
	// kedit truth: 4+ spaces before '>' fails alternative 1, but ^\s+ (alt 4)
	// still matches it as a bare indent - code blocks keep continuing.
	CHECK(MdListMatchMarker("    > q", m), "4-space quote continues as bare indent");
	CHECK(m.marker[0] == '\0' && std::strcmp(m.indent, "    ") == 0, "4-space quote has no marker bytes");

	// Ordered list.
	CHECK(MdListMatchMarker("1. first", m), "ordered matches");
	CHECK(std::strcmp(m.marker, "1. ") == 0, "ordered marker keeps the number");
	CHECK(m.ordered, "ordered flag set");
	CHECK(MdListMatchMarker("42. x", m) && std::strcmp(m.marker, "42. ") == 0, "multi-digit number");
	CHECK(MdListMatchMarker("  3. x", m) && std::strcmp(m.indent, "  ") == 0, "ordered with indent");
	CHECK(MdListMatchMarker("2. [ ] task", m) && std::strcmp(m.marker, "2. [ ] ") == 0, "ordered checkbox marker");
}

static void TestMatchNonMarkers() {
	MdListMarker m;
	CHECK(!MdListMatchMarker("plain line", m), "plain text is not a marker");
	CHECK(!MdListMatchMarker("", m), "empty line is not a marker");

	// Bare indentation continues lines (kedit "^\s+" fallback).
	CHECK(MdListMatchMarker("    code", m), "bare indent matches");
	CHECK(std::strcmp(m.indent, "    ") == 0, "bare indent captured");
	CHECK(m.marker[0] == '\0' && !m.ordered, "bare indent has empty marker");

	// "-no-space" is a plain word, not a bullet (marker needs [ \t] after).
	CHECK(!MdListMatchMarker("-nospace", m), "dash without space is not a bullet");

	// "1.item" without space is not an ordered item.
	CHECK(!MdListMatchMarker("1.item", m), "number without dot-space is not ordered");
}

static void TestBareMarker() {
	CHECK(MdListLineIsBareMarker("- "), "dash + space only");
	CHECK(MdListLineIsBareMarker("  - "), "indented bare marker");
	CHECK(MdListLineIsBareMarker("- [ ] "), "bare checkbox");
	CHECK(MdListLineIsBareMarker("1. "), "bare ordered marker");
	CHECK(MdListLineIsBareMarker("   "), "whitespace-only line");
	CHECK(MdListLineIsBareMarker(""), "empty line");

	CHECK(!MdListLineIsBareMarker("- A"), "content after marker is not bare");
	CHECK(!MdListLineIsBareMarker("1. first"), "ordered content is not bare");
	CHECK(!MdListLineIsBareMarker("- [x] done"), "checked box with content is not bare");
}

static void TestRenumberSection() {
	std::size_t len = 0;

	// Basic: inserting after "1. A" renumbers the following siblings.
	const char *doc1 = "1. A\n1. B\n1. C\nafter";
	char *out = MdListRenumberSection(doc1, std::strlen(doc1), 0, &len);
	CHECK(out != nullptr, "renumber produces output");
	CHECK(std::string(out, len) == "1. A\n2. B\n3. C\nafter", "sequential renumber");
	std::free(out);

	// Starting number comes from the first item of the section.
	const char *doc2 = "3. A\n3. B\n";
	out = MdListRenumberSection(doc2, std::strlen(doc2), 0, &len);
	CHECK(out != nullptr && std::string(out, len) == "3. A\n4. B\n", "renumber starts at the first number");
	std::free(out);

	// Indented sub-list: from the TOP-level line the sub lines are
	// transparent (kedit keeps their raw bytes; Enter on a sub line renumbers
	// the sub-section instead - see doc3b).
	const char *doc3 = "1. A\n   1. sub\n   1. sub\n1. B\n";
	out = MdListRenumberSection(doc3, std::strlen(doc3), 0, &len);
	CHECK(out != nullptr && std::string(out, len) == "1. A\n   1. sub\n   1. sub\n2. B\n",
		"sub lines are transparent from top level");
	std::free(out);

	// Enter on a sub line: spaceIndent is the sub indent, so only the sub
	// section renumbers.
	out = MdListRenumberSection(doc3, std::strlen(doc3), 2, &len);
	CHECK(out != nullptr && std::string(out, len) == "1. A\n   1. sub\n   2. sub\n1. B\n",
		"sub-level renumbers the sub-section");
	std::free(out);

	// kedit truth: /^#+ / guards inside the stop condition, so a heading
	// ALWAYS cuts the section (flush of what came before, nothing after).
	const char *doc4 = "1. A\n## head\n1. B\n";
	out = MdListRenumberSection(doc4, std::strlen(doc4), 0, &len);
	CHECK(out == nullptr && len == 0, "heading cuts the section");

	// Blank lines are transparent when numbered items follow.
	const char *doc4b = "1. A\n\n1. B\n";
	out = MdListRenumberSection(doc4b, std::strlen(doc4b), 0, &len);
	CHECK(out != nullptr && std::string(out, len) == "1. A\n\n2. B\n", "blank line stays inside the section");
	std::free(out);

	// lineIndex in the middle: kedit scans BACKWARD first (getHits over the
	// reversed prefix) so the section head is found; already-sequential text
	// comes back unchanged -> nullptr.
	const char *doc5 = "1. A\n2. B\nx\n";
	out = MdListRenumberSection(doc5, std::strlen(doc5), 1, &len);
	CHECK(out == nullptr && len == 0, "already sequential returns nullptr");

	// Backward scan: cursor on the second item of a wrongly-numbered list
	// renumbers the whole section, not just from the cursor down.
	const char *doc5b = "1. A\n1. B\n1. C\n";
	out = MdListRenumberSection(doc5b, std::strlen(doc5b), 1, &len);
	CHECK(out != nullptr && std::string(out, len) == "1. A\n2. B\n3. C\n", "backward scan finds the section head");
	std::free(out);

	// Paragraph continuation lines (deeper indent, no marker) stay untouched.
	const char *doc6 = "1. A\n   wrap\n1. B\n";
	out = MdListRenumberSection(doc6, std::strlen(doc6), 0, &len);
	CHECK(out != nullptr && std::string(out, len) == "1. A\n   wrap\n2. B\n", "wrapped paragraph does not break the section");
	std::free(out);

	// Thematic break of dashes cuts the section (it is not a bullet).
	const char *doc7 = "1. A\n---\n1. B\n";
	out = MdListRenumberSection(doc7, std::strlen(doc7), 0, &len);
	CHECK(out == nullptr && len == 0, "thematic break cuts the section");

	// Nothing to do on a non-ordered line.
	const char *doc8 = "- a\n- b\n";
	out = MdListRenumberSection(doc8, std::strlen(doc8), 0, &len);
	CHECK(out == nullptr && len == 0, "unordered section returns nullptr");

	// kedit truth: a non-indented plain line without a blank separator is
	// swallowed into the same pending group, so B still renumbers.
	const char *doc9 = "1. A\nplain\n1. B\n";
	out = MdListRenumberSection(doc9, std::strlen(doc9), 0, &len);
	CHECK(out != nullptr && std::string(out, len) == "1. A\nplain\n2. B\n", "plain line without blank joins the section");
	std::free(out);

	// But a blank line followed by plain text cuts it (hasNoIndent flush).
	const char *doc10 = "1. A\n\nplain\n\n1. B\n";
	out = MdListRenumberSection(doc10, std::strlen(doc10), 0, &len);
	CHECK(out == nullptr && len == 0, "blank then plain cuts the section");
	// Erase variant: Enter on a bare "N. " marker deletes the marker line; the
	// section then renumbers with the erased indent (cursor line is now empty).
	const char *doc11 = "1. A\n\n1. C\n";
	out = MdListRenumberSectionWithIndent(doc11, std::strlen(doc11), 1, "", &len);
	CHECK(out != nullptr && std::string(out, len) == "1. A\n\n2. C\n", "erase variant renumbers around the empty line");
	std::free(out);
}

static void TestContinuation() {
	MdListMarker m;
	char buf[256];

	CHECK(MdListMatchMarker("- A", m) && MdListContinuation(m, buf, sizeof(buf)), "dash cont ok");
	CHECK(std::strcmp(buf, "- ") == 0, "dash continuation is dash + space");

	CHECK(MdListMatchMarker("  - [x] done", m) && MdListContinuation(m, buf, sizeof(buf)), "checkbox cont ok");
	CHECK(std::strcmp(buf, "  - [x] ") == 0, "checkbox continued verbatim (kedit keeps the box)");

	CHECK(MdListMatchMarker("1. item", m) && MdListContinuation(m, buf, sizeof(buf)), "ordered cont ok");
	CHECK(std::strcmp(buf, "1. ") == 0, "ordered continuation keeps the number; renumbering fixes it");

	CHECK(MdListMatchMarker("    code", m) && MdListContinuation(m, buf, sizeof(buf)), "bare indent cont ok");
	CHECK(std::strcmp(buf, "    ") == 0, "bare indent continuation is the indent itself");

	CHECK(MdListMatchMarker("> q", m) && MdListContinuation(m, buf, sizeof(buf)), "quote cont ok");
	CHECK(std::strcmp(buf, "> ") == 0, "quote continuation");

	// A tiny buffer is refused, not truncated.
	MdListMatchMarker("- A", m);
	CHECK(!MdListContinuation(m, buf, 2), "undersized buffer refused");
	CHECK(MdListContinuation(m, buf, 3) && std::strcmp(buf, "- ") == 0, "exact-size buffer accepted");
}

int main() {
	TestMatchBullets();
	TestMatchNonMarkers();
	TestBareMarker();
	TestRenumberSection();
	TestContinuation();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures != 0;
}
