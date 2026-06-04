#include "StringTable.h"

#include <fstream>

namespace calc {

	namespace {

		// Trim ASCII whitespace from both ends.
		std::string trim(const std::string& s) {
			std::size_t b = 0;
			std::size_t e = s.size();
			auto isws = [](char c) {
				return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
				};
			while (b < e && isws(s[b])) ++b;
			while (e > b && isws(s[e - 1])) --e;
			return s.substr(b, e - b);
		}

		// Replace the two-character escapes "\n" -> newline and "\\" -> backslash.
		// Any other backslash sequence is left as-is (backslash plus the char).
		std::string unescape(const std::string& s) {
			std::string out;
			out.reserve(s.size());
			for (std::size_t i = 0; i < s.size(); ++i) {
				if (s[i] == '\\' && i + 1 < s.size()) {
					const char next = s[i + 1];
					if (next == 'n') { out += '\n'; ++i; continue; }
					if (next == '\\') { out += '\\'; ++i; continue; }
				}
				out += s[i];
			}
			return out;
		}

	}  // namespace

	bool StringTable::loadFromFile(const std::string& path) {
		std::ifstream in(path);
		if (!in) return false;

		std::string line;
		while (std::getline(in, line)) {
			const std::string trimmed = trim(line);
			if (trimmed.empty() || trimmed[0] == '#') continue;

			const std::size_t eq = line.find('=');
			if (eq == std::string::npos) continue;  // malformed line: skip

			const std::string key = trim(line.substr(0, eq));
			if (key.empty()) continue;

			const std::string value = unescape(trim(line.substr(eq + 1)));
			m_entries[key] = value;
		}
		return true;
	}

	std::optional<std::string> StringTable::get(const std::string& key) const {
		const auto it = m_entries.find(key);
		if (it == m_entries.end()) return std::nullopt;
		return it->second;
	}

}  // namespace calc
