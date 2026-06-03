#ifndef CALC_STRINGTABLE_H
#define CALC_STRINGTABLE_H

#include <optional>
#include <string>
#include <unordered_map>

namespace calc {

// A flat key -> value string table loaded from a text file.
//
// File format (one entry per line):
//
//     # lines starting with '#' are comments
//     key = value text to end of line
//
// - Leading/trailing whitespace around the key and around the value is
//   trimmed. Whitespace *inside* the value is preserved.
// - The literal two-character sequence "\n" in a value is unescaped to a
//   real newline, so multi-line help text fits on one line in the file.
//   "\\" produces a literal backslash.
// - A key may be any text not containing '=' (e.g. "+", "PI", "1001").
// - Blank lines and comment lines are ignored.
//
// This type is frontend-only presentation infrastructure. It never fails
// hard: a missing file simply yields an empty table, and lookups of absent
// keys return std::nullopt so callers can fall back.
class StringTable {
public:
    StringTable() = default;

    // Load from `path`. Returns true if the file was opened and read.
    // On failure the table is left empty and false is returned; the caller
    // decides whether that is fatal (it generally is not).
    bool loadFromFile(const std::string& path);

    // Look up a key. Returns std::nullopt if absent.
    std::optional<std::string> get(const std::string& key) const;

    bool empty() const { return m_entries.empty(); }

private:
    std::unordered_map<std::string, std::string> m_entries;
};

}  // namespace calc

#endif
