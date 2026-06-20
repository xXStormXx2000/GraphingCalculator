// test_support.h - shared scaffolding for the split-up engine test files.
//
// The engine suite was one 1300-line test_main.cpp; it was split by component
// (tokenizer, parser, simplifier, printer, evaluator, repl, plot, regression)
// once the single file grew too large to navigate. The common includes,
// `using` directives, and the small evaluate/print helpers every file shares
// live here so the split files stay focused on their cases. Each file keeps its
// own TEST_CASEs; the framework auto-registers them, and one main() (in
// test_main.cpp) runs them all.

#ifndef CALC_TEST_SUPPORT_H
#define CALC_TEST_SUPPORT_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "CalculatorCore.h"
#include "Repl.h"
#include "test_framework.h"

// These headers are only needed by the small number of tests that exercise
// internal APIs directly (printer round-tripping, raw tokenize/parse).
// Everything else goes through CalculatorCore or the Repl.
#include "StringTable.h"

#include "Parser.h"
#include "Printer.h"
#include "Tokenizer.h"

using namespace calc;
using namespace calc::core;

namespace test_support {

	// The engine no longer ships with built-in constants; the caller supplies
	// them. The console frontend injects calc::defaultConstants(); the tests
	// use that same set so constant-dependent expressions (pi, e, ...) behave
	// as the shipped program does. A few tests that specifically check the
	// no-constants case construct a bare CalculatorCore{} instead.
	inline CalculatorCore makeCore() {
		return CalculatorCore(defaultConstants());
	}

	// Evaluate a line through the engine and return the canonical string.
	inline std::string evalToString(const std::string& input) {
		CalculatorCore core = makeCore();
		auto r = core.evaluateLine(input, 400);
		if (!r) return "error: code " + std::to_string(static_cast<int>(r.error().code));
		return r.value().canonical;
	}

	// Evaluate a line and return the numeric result (NaN on error / non-numeric).
	inline double evalToNumber(const std::string& input) {
		CalculatorCore core = makeCore();
		auto r = core.evaluateLine(input, 400);
		if (!r) return std::numeric_limits<double>::quiet_NaN();
		if (!r.value().value) return std::numeric_limits<double>::quiet_NaN();
		return *r.value().value;
	}

	// Parse and print without simplifying -- exercises the parser + printer only.
	inline std::string parsePrint(const std::string& input) {
		auto t = tokenize(input);
		if (!t.ok()) return "<tok-error>";
		std::size_t size = 0;
		auto p = parseExpression(t.value(), 400, size);
		if (!p.ok()) return "<parse-error>";
		return toString(*p.value().expr);
	}

	inline void writeTempFile(const std::string& path, const std::string& content) {
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << content;
	}

}  // namespace test_support

// The split files were written against these names unqualified, the way they
// read in the original single file. Bring them into scope so the cases are
// unchanged from how they were authored.
using test_support::makeCore;
using test_support::evalToString;
using test_support::evalToNumber;
using test_support::parsePrint;
using test_support::writeTempFile;

#endif  // CALC_TEST_SUPPORT_H
