// PathLink implementation — see PathLink.h. Task 007: path hotspot candidates
// reuse the URL hotspot UX; this module owns candidate shape + resolution.
#include "PathLink.h"
#include <cstring>

namespace {

inline bool IsSep(char ch) noexcept {
	return ch == '\\' || ch == '/';
}

inline bool IsWordChar(unsigned char ch) noexcept {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
		|| ch == '_' || ch == '-' || ch == '.' || ch == '$' || ch >= 0x80;
}

bool IsQuote(char ch) noexcept {
	return ch == '"' || ch == '\'';
}

inline bool HasDriveColon(const char *s, ptrdiff_t len) noexcept {
	return len >= 2 && s[1] == ':' && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'));
}

/// Reject tokens that belong to other features: URL scheme (http:// etc.)
/// and Unix-style absolute paths (leading '/').
bool TokenIsPathShape(const char *s, ptrdiff_t len) noexcept {
	if (len == 0) {
		return false;
	}
	// URL: "scheme://" anywhere inside
	for (ptrdiff_t i = 0; i + 2 < len; ++i) {
		if (s[i] == ':' && s[i + 1] == '/' && s[i + 2] == '/') {
			return false;
		}
	}
	if (s[0] == '/') {
		return false;	// unix-style absolute path
	}
	if (HasDriveColon(s, len)) {
		return true;
	}
	if (len >= 2 && s[0] == '\\' && s[1] == '\\') {
		return true;	// UNC
	}
	// a drive-letter prefix like "C:" without separators is still a path head
	if (HasDriveColon(s, len)) {
		return true;
	}
	// relative: contains a separator but is not a bare dot name
	for (ptrdiff_t i = 0; i < len; ++i) {
		if (IsSep(s[i])) {
			return true;
		}
	}
	return false;
}

} // namespace

bool PathLinkExtractAt(const char *line, ptrdiff_t pos, PathLinkToken *token) {
	if (line == nullptr || token == nullptr || pos < 0) {
		return false;
	}
	const ptrdiff_t len = static_cast<ptrdiff_t>(strlen(line));
	if (pos >= len) {
		return false;
	}

	// widen left/right over word chars, separators and a drive colon at head
	ptrdiff_t begin = pos;
	while (begin > 0 && (IsWordChar(static_cast<unsigned char>(line[begin - 1])) || IsSep(line[begin - 1]) || line[begin - 1] == ':')) {
		--begin;
	}
	// allow drive colon: "C:" head (word char before ':' was already consumed)
	ptrdiff_t end = pos;
	while (line[end] != '\0' && (IsWordChar(static_cast<unsigned char>(line[end])) || IsSep(line[end]) || line[end] == ':')) {
		++end;
	}

	// quoted: when the token is glued to an opening quote, take the whole
	// quoted span (spaces inside the path belong to it).
	if (begin > 0 && IsQuote(line[begin - 1])) {
		ptrdiff_t close = begin;	// find the closing quote scanning right
		while (line[close] != '\0' && !IsQuote(line[close])) {
			++close;
		}
		if (line[close] == '\0') {
			return false;	// unterminated quote: not a token
		}
		--begin;
		end = close + 1;
	}

	// trim trailing sentence punctuation that is not part of a filename
	while (end > begin) {
		const char ch = line[end - 1];
		if (ch != '.' && ch != ',' && ch != ';' && ch != ')' && ch != ']' && ch != '>') {
			break;
		}
		// a dot that directly follows a separator belongs to "." / ".." — keep
		if (ch == '.' && end - 2 >= begin && IsSep(line[end - 2])) {
			break;
		}
		--end;
	}
	// trim leading opening punctuation (quote already widened, others trimmed)
	while (begin < end) {
		const char ch = line[begin];
		if (ch != '<' && ch != '(' && ch != '[') {
			break;
		}
		++begin;
	}

	if (end <= begin) {
		return false;
	}
	// The token must actually contain the queried position; otherwise the
	// caller's left-to-right scan cannot advance (task 007 hang).
	if (end <= pos || begin > pos) {
		return false;
	}
	if (!TokenIsPathShape(line + begin, end - begin)) {
		return false;
	}
	token->begin = begin;
	token->end = end;
	return true;
}

bool PathLinkResolveInto(const char *token, const char *baseDir, char (&out)[MAX_PATH * 2],
	bool (*exists)(const char *raw, const char *baseDir, char *out)) {
	if (token == nullptr || token[0] == '\0' || exists == nullptr) {
		return false;
	}
	return exists(token, baseDir, out);
}
