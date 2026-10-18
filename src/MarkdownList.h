/******************************************************************************
*
* notepad420
*
* MarkdownList.h
*   Markdown list auto-completion (task 005), a byte-level port of the kedit
*   keystroke semantics (K:\Projects\website\kedit, src/services/optional/
*   keystrokes.js). Pure UTF-8 string logic: no Scintilla, no Windows types,
*   so the rules stay unit-testable without a GUI.
*
*   Scope: continuation markers ("> ", "- ", "* ", "+ ", "1. ", checkbox
*   variants, bare indentation), the "second Enter clears the marker" rule,
*   and same-level renumbering of ordered lists (kedit fixNumberedList).
*   Checkbox markers are continued verbatim, exactly as kedit does.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <cstddef>

/// One recognised line-leading marker, kedit indentRegexp semantics:
///   "^ {0,3}>[ ]*"                          block quote (up to 3 leading spaces)
///   "^[ \t]*[*+-][ \t](\[[ xX]\][ \t])?"    unordered bullet, optional checkbox
///   "^([ \t]*)\d+\.[ \t](\[[ xX]\][ \t])?"  ordered item, optional checkbox
///   "^\s+"                                  fallback: bare indentation
struct MdListMarker {
	/// Leading whitespace of the original line (verbatim bytes).
	char indent[192];
	/// The marker itself: "> ", "- ", "- [x] ", "1. ", or "" for bare indent.
	char marker[64];
	/// True for the ordered variant ("N. " prefix) - the only kind kedit runs
	/// fixNumberedList for.
	bool ordered;
};

/// Matches `line` (one line, no trailing '\n') against the marker rules.
/// Returns false only when the line has neither a marker nor leading
/// whitespace (plain text line, or empty line). `out` is always fully
/// initialised when the function returns.
bool MdListMatchMarker(const char *line, MdListMarker &out) noexcept;

/// True when `line` consists of nothing but a marker (indent + marker, no
/// content after it) - the shape that triggers kedit's clearNewline erase:
/// pressing Enter again removes the marker and leaves an empty line.
bool MdListLineIsBareMarker(const char *line) noexcept;

/// Builds the continuation text inserted after the newline for the marker
/// matched on the previous line: indent + marker (checkbox preserved
/// verbatim). `buf` is always NUL-terminated on success; returns false only
/// when `bufSize` is too small.
bool MdListContinuation(const MdListMarker &marker, char *buf, std::size_t bufSize) noexcept;

/// kedit fixNumberedList: renumbers the ordered-list section containing
/// `lines[lineIndex]` (the line the Enter was pressed on). The section head
/// is located with a backward getHits pass over the reversed prefix (kedit
/// "Prevents starting from 0"), then the section runs to its cut point.
/// Same-indent numbered items are rewritten as sequential numbers starting
/// from the section's first item. Blank lines stay inside while numbered
/// items follow; deeper-indented lines (nested lists, paragraph wraps) are
/// transparent; headings ("# "), thematic breaks, and a blank line followed
/// by non-indented plain text cut the section. Tab-indented
/// lines are matched with tabs expanded to 4 spaces; non-renumbered lines
/// keep their original bytes. The number prefix of renumbered lines is
/// rebuilt as indent + N + ". ".
///
/// `text` is UTF-8, lines separated by '\n' (the caller normalises Scintilla
/// EOLs). Returns a malloc'd replacement for the WHOLE `text` the caller must
/// free() (length in *outLength) when the section changed, or nullptr when
/// nothing changed / nothing matched / out of memory (*outLength is 0 then).
char *MdListRenumberSection(const char *text, std::size_t textLength,
							std::size_t lineIndex, std::size_t *outLength) noexcept;

/// Erase-branch variant: same algorithm but `lineIndex` may point at a line
/// that is no longer ordered (kedit erased the marker there), so the section
/// indent comes from the erased marker line instead of a gate on the line.
char *MdListRenumberSectionWithIndent(const char *text, std::size_t textLength,
									  std::size_t lineIndex, const char *indent,
									  std::size_t *outLength) noexcept;
