/******************************************************************************
*
* notepad420
*
* MarkdownList.cpp
*   See MarkdownList.h. Byte-level port of kedit's indentRegexp /
*   enterKeyHandler / fixNumberedList (src/services/optional/keystrokes.js);
*   the JS regexes are hand-compiled into the small matchers below, keeping
*   the alternation order, the pendingHits/flush batching and the
*   indent + N + last-two-chars renumber format exactly as kedit does.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include "MarkdownList.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char NoBreak = static_cast<char>(0xA0);	// JS \s includes U+00A0

bool IsTabOrSpace(char c) noexcept { return c == ' ' || c == '\t'; }

// JS \s (the input is already split on '\n', so '\n' itself never appears).
bool IsJsSpace(char c) noexcept {
	return c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r' || c == NoBreak;
}

bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// "[ xX]" + [ \t] at p, or 0 when absent (kedit's optional checkbox suffix).
std::size_t CheckboxLen(const char *p) noexcept {
	if (p[0] == '[' && (p[1] == ' ' || p[1] == 'x' || p[1] == 'X') && p[2] == ']' && IsTabOrSpace(p[3])) {
		return 4;
	}
	return 0;
}

bool CopyField(char *dst, std::size_t size, const char *src, std::size_t len) noexcept {
	if (len + 1 > size) {
		return false;	// pathological marker length; caller treats it as no match
	}
	std::memcpy(dst, src, len);
	dst[len] = '\0';
	return true;
}

// ---- fixNumberedList helpers ------------------------------------------------

bool IsHeading(const char *line) noexcept {
	if (line[0] != '#') {
		return false;
	}
	const char *p = line;
	while (*p == '#') {
		++p;
	}
	return *p == ' ';
}

// /^ {0,3}(?:-(?:[ \t]*-){2,}|\*(?:[ \t]*\*){2,}|_(?:[ \t]*_){2,})[ \t]*$/
bool IsThematicBreak(const char *line) noexcept {
	int n = 0;
	while (n < 3 && line[n] == ' ') {
		++n;
	}
	const char c = line[n];
	if (c != '-' && c != '*' && c != '_') {
		return false;
	}
	const char *p = line + n + 1;
	for (int k = 0; k < 2; ) {
		while (IsTabOrSpace(*p)) {
			++p;
		}
		if (*p != c) {
			return false;
		}
		++p;
		++k;
	}
	while (IsTabOrSpace(*p)) {
		++p;
	}
	return *p == '\0';
}

// Leading [ \t]* with tabs expanded to four spaces (kedit line.replace on the
// leading whitespace only).
std::string NormalizeLeading(const std::string &line) {
	std::size_t i = 0;
	while (i < line.size() && IsTabOrSpace(line[i])) {
		++i;
	}
	std::string out;
	out.reserve(line.size() + 3);
	for (std::size_t j = 0; j < i; ++j) {
		if (line[j] == '\t') {
			out += "    ";
		} else {
			out += line[j];
		}
	}
	out += line.substr(i);
	return out;
}

// One line's view for getHits: kind + the two capture groups.
struct LineMatch {
	bool blank = false;		// ^[ \s]*$  (JS leaves the groups undefined)
	bool numbered = false;	// group 1: (\d+\.[ \t])
	std::string group1;
	std::string content;	// group 2: (( )?.*)
	bool hasContent = false;	// group 2 defined (false when blank)
	bool leadingSpace = false;	// group 3: ( )?
};

// ^[ \s]*$ | ^spaceIndent(\d+\.[ \t])?(( )?.*)$  on the tab-normalized line.
bool MatchIndentRegex(const std::string &norm, const std::string &spaceIndent, LineMatch &m) {
	bool allSpace = true;
	for (char c : norm) {
		if (!IsJsSpace(c)) {
			allSpace = false;
			break;
		}
	}
	if (allSpace) {
		m.blank = true;
		return true;
	}
	if (norm.compare(0, spaceIndent.size(), spaceIndent) != 0) {
		return false;
	}
	std::size_t pos = spaceIndent.size();
	if (IsDigit(norm[pos])) {
		std::size_t j = pos;
		while (j < norm.size() && IsDigit(norm[j])) {
			++j;
		}
		if (j < norm.size() && norm[j] == '.' && j + 1 < norm.size() && IsTabOrSpace(norm[j + 1])) {
			m.numbered = true;
			m.group1 = norm.substr(pos, j + 2 - pos);
			pos = j + 2;
		}
	}
	m.hasContent = true;
	m.content = norm.substr(pos);
	m.leadingSpace = !m.content.empty() && m.content[0] == ' ';
	return true;
}

struct Hit {
	std::string raw;
	LineMatch match;
};

// kedit getHits: returns the contiguous consumed lines from `from` (blank
// lines are consumed too; a discarded trailing pending batch is NOT counted).
std::vector<Hit> GetHits(const std::vector<std::string> &lines, std::size_t from,
						 const std::string &spaceIndent) {
	std::vector<Hit> hits;
	std::vector<Hit> pending;
	bool hasHit = false;
	bool hasNoIndent = false;

	auto flush = [&]() noexcept {
		if (!hasHit && hasNoIndent) {
			return false;
		}
		hits.insert(hits.end(), pending.begin(), pending.end());
		pending.clear();
		hasHit = false;
		hasNoIndent = false;
		return true;
	};

	for (std::size_t i = from; i < lines.size(); ++i) {
		LineMatch lm;
		if (!MatchIndentRegex(NormalizeLeading(lines[i]), spaceIndent, lm)
			|| IsHeading(lines[i].c_str()) || IsThematicBreak(lines[i].c_str())) {
			flush();
			break;
		}
		pending.push_back({ lines[i], lm });
		if (!lm.hasContent) {
			if (!flush()) {
				break;
			}
		} else if (lm.numbered) {
			hasHit = true;
		} else if (!lm.leadingSpace) {
			hasNoIndent = true;
		}
	}
	flush();
	return hits;
}

// kedit formatHits: numbered lines rebuild as indent + num + group1.slice(-2)
// + group2; everything else keeps its raw bytes. `num` starts falsy and every
// falsy re-read re-parses the current item (kedit's "!num" quirk preserved).
std::vector<std::string> FormatHits(const std::vector<Hit> &hits, const std::string &indent) {
	std::vector<std::string> out;
	out.reserve(hits.size());
	long long num = 0;
	for (const Hit &hit : hits) {
		if (hit.match.numbered) {
			if (!num) {
				num = std::strtoll(hit.match.group1.c_str(), nullptr, 10);
			}
			const std::string sep = hit.match.group1.size() >= 2
				? hit.match.group1.substr(hit.match.group1.size() - 2)
				: hit.match.group1;
			out.push_back(indent + std::to_string(num) + sep + hit.match.content);
			++num;
		} else {
			out.push_back(hit.raw);
		}
	}
	return out;
}

void SplitLines(const char *text, std::size_t length, std::vector<std::string> &lines) {
	std::size_t start = 0;
	for (std::size_t i = 0; i < length; ++i) {
		if (text[i] == '\n') {
			lines.emplace_back(text + start, i - start);
			start = i + 1;
		}
	}
	lines.emplace_back(text + start, length - start);	// trailing "" after a final '\n'
}

}	// namespace

bool MdListMatchMarker(const char *line, MdListMarker &out) noexcept {
	std::memset(&out, 0, sizeof(out));

	// alternative 1: ^ {0,3}>[ ]*  (literal spaces only, marker = ">" + tail)
	int n = 0;
	while (n < 3 && line[n] == ' ') {
		++n;
	}
	if (line[n] == '>') {
		std::size_t tail = 0;
		while (line[n + 1 + tail] == ' ') {
			++tail;
		}
		if (!CopyField(out.indent, sizeof(out.indent), line, n)
		 || !CopyField(out.marker, sizeof(out.marker), line + n, tail + 1)) {
			return false;
		}
		return true;
	}

	// shared leading [ \t]* for alternatives 2/3/4
	std::size_t i = 0;
	while (IsTabOrSpace(line[i])) {
		++i;
	}

	// alternative 2: ^[ \t]*[*+-][ \t](\[[ xX]\][ \t])?
	if ((line[i] == '-' || line[i] == '*' || line[i] == '+') && IsTabOrSpace(line[i + 1])) {
		std::size_t m = i + 2 + CheckboxLen(line + i + 2);
		if (!CopyField(out.indent, sizeof(out.indent), line, i)
		 || !CopyField(out.marker, sizeof(out.marker), line + i, m - i)) {
			return false;
		}
		return true;
	}

	// alternative 3: ^([ \t]*)\d+\.[ \t](\[[ xX]\][ \t])?
	if (IsDigit(line[i])) {
		std::size_t j = i;
		while (IsDigit(line[j])) {
			++j;
		}
		if (line[j] == '.' && IsTabOrSpace(line[j + 1])) {
			std::size_t m = j + 2 + CheckboxLen(line + j + 2);
			if (!CopyField(out.indent, sizeof(out.indent), line, i)
			 || !CopyField(out.marker, sizeof(out.marker), line + i, m - i)) {
				return false;
			}
			out.ordered = true;
			return true;
		}
	}

	// alternative 4 (fallback): ^\s+
	return i > 0 && CopyField(out.indent, sizeof(out.indent), line, i);
}

bool MdListLineIsBareMarker(const char *line) noexcept {
	MdListMarker m;
	if (!MdListMatchMarker(line, m)) {
		return line[0] == '\0';	// an empty line carries no content either
	}
	return line[std::strlen(m.indent) + std::strlen(m.marker)] == '\0';
}

bool MdListContinuation(const MdListMarker &marker, char *buf, std::size_t bufSize) noexcept {
	const std::size_t li = std::strlen(marker.indent);
	const std::size_t lm = std::strlen(marker.marker);
	if (li + lm + 1 > bufSize) {
		return false;
	}
	std::memcpy(buf, marker.indent, li);
	std::memcpy(buf + li, marker.marker, lm);
	buf[li + lm] = '\0';
	return true;
}

namespace {

char *RenumberCore(const char *text, std::size_t textLength, std::size_t lineIndex,
					const char *givenIndent, std::size_t *outLength) noexcept
{
	*outLength = 0;
	if (lineIndex == static_cast<std::size_t>(-1)) {
		return nullptr;
	}

	std::vector<std::string> lines;
	SplitLines(text, textLength, lines);
	if (lineIndex >= lines.size()) {
		return nullptr;
	}

	std::string indent;
	if (givenIndent != nullptr) {
		indent = givenIndent;	// erase branch: the line lost its marker already
	} else {
		// kedit: fixNumberedList returns early unless the Enter line matched the
		// ordered alternative (indent capture group 1 defined).
		MdListMarker marker;
		if (!MdListMatchMarker(lines[lineIndex].c_str(), marker) || !marker.ordered) {
			return nullptr;
		}
		indent = marker.indent;
	}
	std::string spaceIndent;
	for (char c : indent) {
		spaceIndent += (c == '\t') ? "    " : std::string(1, c);
	}

	// "Prevents starting from 0": locate the section head by getHits over the
	// reversed prefix, then walk the work list with kedit's while(idx<=cur+1).
	std::vector<std::string> reversed(lines.begin(), lines.begin() + lineIndex);
	std::reverse(reversed.begin(), reversed.end());
	const std::size_t head = lineIndex - GetHits(reversed, 0, spaceIndent).size();

	bool changed = false;
	std::size_t idx = head;
	while (idx <= lineIndex + 1 && idx < lines.size()) {
		std::vector<Hit> hits = GetHits(lines, idx, spaceIndent);
		if (hits.empty()) {
			++idx;
			continue;
		}
		std::vector<std::string> formatted = FormatHits(hits, indent);
		for (std::size_t k = 0; k < formatted.size(); ++k) {
			if (formatted[k] != lines[idx + k]) {
				lines[idx + k].swap(formatted[k]);
				changed = true;
			}
		}
		idx += hits.size();
	}
	if (!changed) {
		return nullptr;
	}

	std::string joined;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		if (i != 0) {
			joined += '\n';
		}
		joined += lines[i];
	}
	const std::size_t size = joined.size();
	char *out = static_cast<char *>(std::malloc(size));
	if (out == nullptr) {
		return nullptr;
	}
	std::memcpy(out, joined.data(), size);
	*outLength = size;
	return out;
}

}	// namespace

char *MdListRenumberSection(const char *text, std::size_t textLength,
							std::size_t lineIndex, std::size_t *outLength) noexcept {
	return RenumberCore(text, textLength, lineIndex, nullptr, outLength);
}

char *MdListRenumberSectionWithIndent(const char *text, std::size_t textLength,
									  std::size_t lineIndex, const char *indent,
									  std::size_t *outLength) noexcept {
	return RenumberCore(text, textLength, lineIndex, indent ? indent : "", outLength);
}
