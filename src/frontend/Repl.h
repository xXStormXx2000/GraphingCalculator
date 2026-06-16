#ifndef CALC_REPL_H
#define CALC_REPL_H

#include <iosfwd>
#include <string>

#include "CalculatorCore.h"
#include "StringTable.h"

namespace calc {

	class Repl {
	public:
		Repl(std::istream& in, std::ostream& out);

		void printBanner();

		// Run until EOF, /exit, or /quit. Returns the desired process exit code.
		int run();

		// Process one input line. Public for testability.
		// Returns the response string (without trailing newline or ">> " prefix).
		std::string processLine(const std::string& line);

	private:
		std::string handleCommand(const std::string& line);
		std::string handleExpression(const std::string& line);

		// Turn a diagnostic into a human-readable, caret-annotated string using
		// the loaded error-message table (with a built-in fallback).
		std::string formatDiagnostic(const core::Diagnostic& d,
			const std::string& source) const;

		// Look up help text for a topic from the loaded help table.
		std::optional<std::string> helpFor(const std::string& topic) const;

		// Look up a localized UI string by its stable key. Falls back to the
		// built-in English text if the ui table has no entry.
		std::string ui(const std::string& key) const;

		// Resolve a (possibly localized) command name typed by the user to its
		// canonical command id (e.g. "quitter" -> "exit"). Returns the input
		// unchanged if the commands table has no mapping, so the built-in
		// English names keep working when no file is present.
		std::string canonicalCommand(const std::string& typed) const;

		std::istream& m_in;
		std::ostream& m_out;
		bool m_running = true;
		core::CalculatorCore m_core;

		// Presentation data, loaded from files at construction.
		StringTable m_errors;    // keyed by DiagCode integer value
		StringTable m_help;      // keyed by topic name
		StringTable m_ui;        // keyed by stable UI-string id
		StringTable m_commands;  // localized command name -> canonical id

		//Max size of an input expression, in AST nodes, to prevent stack overflow.
		std::size_t m_maxSize = 400;
	};

}  // namespace calc

#endif
