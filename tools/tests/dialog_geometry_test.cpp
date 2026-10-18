// Dialog geometry checks - no GUI, no window: parses the IDD_PASTE_CACHE_MGR
// template straight out of the .rc files and validates the layout numbers.
//
// This guards a real defect class the eye caught only after shipping: the
// bottom buttons ran past the frame (buttons ended at y=167 in a 164-high
// dialog), so the frame line was clipped. The project convention, measured
// across all other dialogs, keeps the bottom-most button edge 6-9 dialog
// units above the frame.
//
// Build and run: tools\run-tests.bat
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

namespace {

struct Control {
	std::string kind;
	std::string id;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

struct Dialog {
	int width = 0;
	int height = 0;
	std::vector<Control> controls;
};

std::string Trim(const std::string &s) {
	std::size_t b = 0;
	std::size_t e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
		++b;
	}
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
		--e;
	}
	return s.substr(b, e - b);
}

std::vector<std::string> ReadLines(const char *path, bool &ok) {
	std::vector<std::string> lines;
	FILE *f = std::fopen(path, "rb");
	if (f == nullptr) {
		ok = false;
		return lines;
	}
	std::string text;
	char buf[8192];
	std::size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
		text.append(buf, n);
	}
	std::fclose(f);
	ok = true;

	std::size_t start = 0;
	for (std::size_t i = 0; i <= text.size(); ++i) {
		if (i == text.size() || text[i] == '\n') {
			lines.push_back(text.substr(start, i - start));
			start = i + 1;
		}
	}
	return lines;
}

// Strips a leading keyword and an optional quoted caption; leaves "ID,x,y,w,h".
bool SplitControlLine(const std::string &line, std::string &kind, std::string &rest) {
	const std::size_t sp = line.find_first_of(" \t");
	if (sp == std::string::npos) {
		return false;
	}
	kind = line.substr(0, sp);
	rest = Trim(line.substr(sp));
	if (!rest.empty() && rest[0] == '"') {
		std::size_t q = 1;
		while (q < rest.size()) {
			if (rest[q] == '"') {
				if (q + 1 < rest.size() && rest[q + 1] == '"') {	// "" escape
					q += 2;
					continue;
				}
				break;
			}
			++q;
		}
		if (q >= rest.size()) {
			return false;
		}
		rest = Trim(rest.substr(q + 1));
		if (!rest.empty() && rest[0] == ',') {
			rest = Trim(rest.substr(1));
		}
	}
	return true;
}

bool ParseDialog(const char *path, const char *dialogName, Dialog &out) {
	bool ok = false;
	const std::vector<std::string> lines = ReadLines(path, ok);
	if (!ok) {
		return false;
	}

	const std::string needle = std::string(dialogName) + " DIALOGEX";
	bool inside = false;
	for (const std::string &rawLine : lines) {
		const std::string line = Trim(rawLine);
		if (!inside) {
			if (line.compare(0, needle.size(), needle) != 0) {
				continue;
			}
			// "IDD_X DIALOGEX 0, 0, 300, 178"
			int a = 0, b = 0;
			if (std::sscanf(line.c_str() + needle.size(), " %d , %d , %d , %d",
							&a, &b, &out.width, &out.height) != 4) {
				return false;
			}
			inside = true;
			continue;
		}
		if (line == "END") {
			return !out.controls.empty();
		}
		if (line.empty() || line == "BEGIN" || line.compare(0, 5, "STYLE") == 0
			|| line.compare(0, 7, "CAPTION") == 0 || line.compare(0, 4, "FONT") == 0
			|| line.compare(0, 7, "EXSTYLE") == 0 || line.compare(0, 2, "//") == 0) {
			continue;
		}
		std::string kind;
		std::string rest;
		if (!SplitControlLine(line, kind, rest)) {
			continue;
		}
		const std::size_t comma = rest.find(',');
		if (comma == std::string::npos) {
			continue;
		}
		Control c;
		c.kind = kind;
		c.id = Trim(rest.substr(0, comma));
		if (std::sscanf(rest.c_str() + comma + 1, " %d , %d , %d , %d",
						&c.x, &c.y, &c.w, &c.h) != 4) {
			continue;
		}
		out.controls.push_back(c);
	}
	return false;
}

bool IsGroupBox(const Control &c) { return c.kind == "GROUPBOX"; }
bool IsPushButton(const Control &c) { return c.kind == "PUSHBUTTON" || c.kind == "DEFPUSHBUTTON"; }

bool Overlaps(const Control &a, const Control &b) {
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void CheckDialog(const char *label, const Dialog &d) {
	const std::string tag(label);
	// Every control must fit inside the frame - the defect that shipped.
	for (const Control &c : d.controls) {
		const std::string where = tag + " " + c.kind + " " + c.id;
		CHECK(c.x >= 0 && c.y >= 0, (where + " has a negative origin").c_str());
		CHECK(c.x + c.w <= d.width, (where + " runs past the right frame edge").c_str());
		CHECK(c.y + c.h <= d.height, (where + " runs past the bottom frame edge").c_str());
	}

	// The bottom-most button keeps the project's frame margin (6-9 units).
	int bottom = 0;
	bool found = false;
	for (const Control &c : d.controls) {
		if (IsPushButton(c) && (!found || c.y + c.h > bottom)) {
			bottom = c.y + c.h;
			found = true;
		}
	}
	CHECK(found, (tag + " has at least one push button").c_str());
	if (found) {
		const int margin = d.height - bottom;
		CHECK(margin >= 6 && margin <= 9,
			  (tag + " bottom button margin must be 6-9 units; got " + std::to_string(margin)).c_str());
	}

	// Nothing overlaps: the note text once sat under the buttons.
	for (std::size_t i = 0; i < d.controls.size(); ++i) {
		for (std::size_t j = i + 1; j < d.controls.size(); ++j) {
			const Control &a = d.controls[i];
			const Control &b = d.controls[j];
			if (IsGroupBox(a) || IsGroupBox(b)) {
				continue;	// group boxes contain their children by design
			}
			const std::string where = tag + " overlap: " + a.kind + " " + a.id + " <-> " + b.kind + " " + b.id;
			CHECK(!Overlaps(a, b), where.c_str());
		}
	}
}

}	// namespace

static void TestPasteCacheMgrGeometry() {
	Dialog en;
	Dialog zh;
	const bool enOk = ParseDialog("src/Notepad4.rc", "IDD_PASTE_CACHE_MGR", en);
	const bool zhOk = ParseDialog("locale/zh-Hans/Notepad4.rc", "IDD_PASTE_CACHE_MGR", zh);
	CHECK(enOk, "IDD_PASTE_CACHE_MGR parses from src/Notepad4.rc");
	CHECK(zhOk, "IDD_PASTE_CACHE_MGR parses from locale/zh-Hans/Notepad4.rc");
	if (!enOk || !zhOk) {
		return;
	}

	CheckDialog("en", en);
	CheckDialog("zh-Hans", zh);

	// Both templates must stay in lockstep: same size, same control set, same
	// coordinates. Hand-maintained satellite rc files drift otherwise.
	CHECK(en.width == zh.width && en.height == zh.height, "en and zh-Hans dialog size agree");
	CHECK(en.controls.size() == zh.controls.size(), "en and zh-Hans declare the same control count");
	const std::size_t count = en.controls.size() < zh.controls.size() ? en.controls.size() : zh.controls.size();
	for (std::size_t i = 0; i < count; ++i) {
		const Control &a = en.controls[i];
		const Control &b = zh.controls[i];
		const std::string where = "en/zh control #" + std::to_string(i) + " (" + a.id + ")";
		CHECK(a.id == b.id, (where + " has the same identifier").c_str());
		CHECK(a.x == b.x && a.y == b.y, (where + " is anchored identically").c_str());
		// Label widths legitimately follow the translated text; structural
		// controls (inputs, buttons, boxes) must not drift between languages.
		const bool label = a.kind == "LTEXT" || a.kind == "RTEXT";
		if (!label) {
			CHECK(a.w == b.w && a.h == b.h, (where + " has identical geometry").c_str());
		}
	}
}

int main() {
	TestPasteCacheMgrGeometry();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}