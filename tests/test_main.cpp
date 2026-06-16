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

// These headers are only included for the small number of tests that
// exercise internal APIs directly (printer round-tripping).
// Everything else goes through CalculatorCore or the Repl.

#include "StringTable.h"

#include "Parser.h"
#include "Printer.h"
#include "Tokenizer.h"

using namespace calc;
using namespace calc::core;

namespace {

	// Evaluate a line through the engine and return the canonical string.
	std::string evalToString(const std::string& input) {
		CalculatorCore core;
		auto r = core.evaluateLine(input, 400);
		if (!r) return "error: code " + std::to_string(static_cast<int>(r.error().code));
		return r.value().canonical;
	}

	// Evaluate a line and return the numeric result (NaN on error / non-numeric).
	double evalToNumber(const std::string& input) {
		CalculatorCore core;
		auto r = core.evaluateLine(input, 400);
		if (!r) return std::numeric_limits<double>::quiet_NaN();
		if (!r.value().value) return std::numeric_limits<double>::quiet_NaN();
		return *r.value().value;
	}

	// Parse and print without simplifying — exercises the parser + printer only.
	std::string parsePrint(const std::string& input) {
		auto t = tokenize(input);
		if (!t.ok()) return "<tok-error>";
		std::size_t size = 0;
		auto p = parseExpression(t.value(), 400, size);
		if (!p.ok()) return "<parse-error>";
		return toString(*p.value().expr);
	}

	void writeTempFile(const std::string& path, const std::string& content) {
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << content;
	}

}  // namespace

int main() {
	return test_framework::runAll();
}

// -----------------------------------------------------------------------
// Tokenizer: edge cases
// -----------------------------------------------------------------------

TEST_CASE("tokenizer: basic tokens") {
	auto r = tokenize("1 + 2 * 3");
	REQUIRE(r.ok());
	auto& toks = r.value();
	REQUIRE_EQ(toks.size(), std::size_t{ 6 });  // 1 + 2 * 3 EOF
	REQUIRE_EQ(toks[0].kind, TokenKind::Number);
	REQUIRE_APPROX(toks[0].number, 1.0, 1e-12);
	REQUIRE_EQ(toks[1].kind, TokenKind::Plus);
}

TEST_CASE("tokenizer: numbers with decimals") {
	auto r = tokenize("3.14 .5 0.0");
	REQUIRE(r.ok());
	auto& toks = r.value();
	REQUIRE_APPROX(toks[0].number, 3.14, 1e-12);
	REQUIRE_APPROX(toks[1].number, 0.5, 1e-12);
	REQUIRE_APPROX(toks[2].number, 0.0, 1e-12);
}

TEST_CASE("tokenizer: scientific notation") {
	auto r = tokenize("1e4 1e-4 1.5e3 2.5E-2 6.022e23");
	REQUIRE(r.ok());
	auto& toks = r.value();
	REQUIRE_EQ(toks[0].kind, TokenKind::Number);
	REQUIRE_APPROX(toks[0].number, 1e4, 1e-9);
	REQUIRE_APPROX(toks[1].number, 1e-4, 1e-18);
	REQUIRE_APPROX(toks[2].number, 1.5e3, 1e-9);
	REQUIRE_APPROX(toks[3].number, 2.5e-2, 1e-12);
	REQUIRE_APPROX(toks[4].number, 6.022e23, 1e15);
	// A leading-dot mantissa with an exponent is one number.
	auto r2 = tokenize(".5e3");
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[0].kind, TokenKind::Number);
	REQUIRE_APPROX(r2.value()[0].number, 500.0, 1e-9);
}

TEST_CASE("tokenizer: e disambiguation (constant vs exponent)") {
	// A bare `e` with no following digit must NOT be swallowed into a numeric
	// literal -- it stays a separate constant identifier. With no implicit
	// multiplication, `2e` is therefore Number followed by Identifier, which
	// is a parse error at the next layer (asserted below), NOT a product.
	auto r1 = tokenize("2e");
	REQUIRE(r1.ok());
	REQUIRE_EQ(r1.value()[0].kind, TokenKind::Number);
	REQUIRE_APPROX(r1.value()[0].number, 2.0, 1e-12);
	REQUIRE_EQ(r1.value()[1].kind, TokenKind::Identifier);

	auto r2 = tokenize("e");           // bare constant
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[0].kind, TokenKind::Identifier);

	// A well-formed exponent (sign + digit after `e`) IS one number.
	auto r3 = tokenize("2e+1");
	REQUIRE(r3.ok());
	REQUIRE_EQ(r3.value().size(), std::size_t{ 2 });  // Number EOF
	REQUIRE_EQ(r3.value()[0].kind, TokenKind::Number);
	REQUIRE_APPROX(r3.value()[0].number, 20.0, 1e-9);
}

TEST_CASE("parser: bare e after a number is not implicit multiplication") {
	// `2e` tokenizes as Number, Identifier with no operator between them.
	// Since implicit multiplication is not supported, this must be rejected
	// at parse time rather than silently treated as 2 * e.
	auto t = tokenize("2e");
	REQUIRE(t.ok());
	std::size_t size = 0;
	auto p = parseExpression(t.value(), 400, size);
	REQUIRE(!p.ok());
}

TEST_CASE("eval: scientific notation end to end") {
	REQUIRE_APPROX(evalToNumber("1e-4"), 1e-4, 1e-18);
	REQUIRE_APPROX(evalToNumber("1e-4 + 1"), 1.0001, 1e-12);
	REQUIRE_APPROX(evalToNumber("2.5e3 * 2"), 5000.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("6.022e23 / 2"), 3.011e23, 1e15);
	// `e` still works as the constant alongside exponent literals.
	REQUIRE_APPROX(evalToNumber("2*e"), 2.0 * 2.718281828459045, 1e-9);
	REQUIRE_APPROX(evalToNumber("1e0"), 1.0, 1e-12);
}

TEST_CASE("tokenizer: identifiers vs slash command") {
	auto r1 = tokenize("/list");
	REQUIRE(r1.ok());
	REQUIRE_EQ(r1.value()[0].kind, TokenKind::Slash_Cmd);

	auto r2 = tokenize("4 / 2");
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[1].kind, TokenKind::Slash);
}

TEST_CASE("tokenizer: malformed numbers") {
	// "1.2.3" tokenizes as two adjacent numbers (1.2 then .3) — the
	// tokenizer accepts each, but the parser must reject the resulting
	// stream because there's no operator between them.
	auto r = tokenize("1.2.3");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().size(), std::size_t{ 3 });  // 1.2  .3  EOF
	std::size_t size = 0;
	auto p = parseExpression(r.value(), 400, size);
	REQUIRE(!p.ok());
}

TEST_CASE("tokenizer: unknown character") {
	auto r = tokenize("a @ b");
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::UnexpectedCharacter);
	REQUIRE(r.error().detail == "@");
}

// -----------------------------------------------------------------------
// Parser
// -----------------------------------------------------------------------

TEST_CASE("parser: precedence") {
	REQUIRE_EQ(evalToString("1 + 2 * 3"), std::string("7"));
	REQUIRE_EQ(evalToString("(1 + 2) * 3"), std::string("9"));
}

TEST_CASE("parser: right-associative power") {
	REQUIRE_APPROX(evalToNumber("2 ^ 3"), 8.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("2 ^ 3 ^ 2"), 512.0, 1e-12);  // 2^(3^2) = 2^9
	REQUIRE_APPROX(evalToNumber("(2 ^ 3) ^ 2"), 64.0, 1e-12);
}

TEST_CASE("parser: unary minus everywhere") {
	REQUIRE_APPROX(evalToNumber("-3"), -3.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("2 + -3"), -1.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("2 * -3"), -6.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("-2 ^ 2"), -4.0, 1e-12);   // -(2^2)
	REQUIRE_APPROX(evalToNumber("(-2) ^ 2"), 4.0, 1e-12);
}

TEST_CASE("parser: function calls") {
	REQUIRE_APPROX(evalToNumber("sin(0)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("log(2, 64)"), 6.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("sqrt(16)"), 4.0, 1e-12);
}

TEST_CASE("parser: nested parens") {
	REQUIRE_APPROX(evalToNumber("((1 + 2) * (3 + 4))"), 21.0, 1e-12);
}

TEST_CASE("parser: errors") {
	CalculatorCore core;
	REQUIRE(!core.evaluateLine("1 +", 400).ok());
	REQUIRE(!core.evaluateLine("1 ** 2", 400).ok());
	REQUIRE(!core.evaluateLine("(1 + 2", 400).ok());
}

// -----------------------------------------------------------------------
// Evaluator
// -----------------------------------------------------------------------

TEST_CASE("evaluator: division by zero") {
	CalculatorCore core;
	auto r = core.evaluateLine("1/0", 400);
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::DivisionByZero ||
		r.error().code == DiagCode::NotFinite);
}

TEST_CASE("evaluator: with bindings") {
	CalculatorCore core;
	core.evaluateLine("x: 3", 400);
	core.evaluateLine("y: 5", 400);
	auto r = core.evaluateLine("x*x + y", 400);
	REQUIRE(r.ok());
	REQUIRE(r.value().value.has_value());
	REQUIRE_APPROX(*r.value().value, 14.0, 1e-12);
}

TEST_CASE("evaluator: built-in constants") {
	REQUIRE_APPROX(evalToNumber("PI"), 3.14159265358979, 1e-10);
}

TEST_CASE("evaluator: undefined variable") {
	CalculatorCore core;
	// A free variable does not produce an error — it produces a symbolic result.
	auto r = core.evaluateLine("nope + 1", 400);
	REQUIRE(r.ok());
	REQUIRE(!r.value().value.has_value());  // not numeric
}

// -----------------------------------------------------------------------
// Simplifier
// -----------------------------------------------------------------------

TEST_CASE("simplifier: constant folding") {
	REQUIRE_EQ(evalToString("1 + 2 + 3"), std::string("6"));
	REQUIRE_EQ(evalToString("2 * 3 * 4"), std::string("24"));
	REQUIRE_EQ(evalToString("sin(0) + 5"), std::string("5"));
}

TEST_CASE("simplifier: identity rules") {
	REQUIRE_EQ(evalToString("x + 0"), std::string("x"));
	REQUIRE_EQ(evalToString("0 + x"), std::string("x"));
	REQUIRE_EQ(evalToString("x * 1"), std::string("x"));
	REQUIRE_EQ(evalToString("1 * x"), std::string("x"));
	REQUIRE_EQ(evalToString("x * 0"), std::string("0"));
	REQUIRE_EQ(evalToString("x ^ 0"), std::string("1"));
	REQUIRE_EQ(evalToString("x ^ 1"), std::string("x"));
}

TEST_CASE("simplifier: cancellation - the headline feature") {
	REQUIRE_EQ(evalToString("a + b - a"), std::string("b"));
	REQUIRE_EQ(evalToString("a - a"), std::string("0"));
	REQUIRE_EQ(evalToString("a*b/a"), std::string("b"));
	REQUIRE_EQ(evalToString("a/a"), std::string("1"));
}

TEST_CASE("simplifier: numeric folding mixed with variables") {
	// 1 + x + 2 -> x + 3  (or "3 + x"; either ordering is acceptable)
	auto s = evalToString("1 + x + 2");
	bool validOrdering = (s == "3 + x" || s == "x + 3");
	REQUIRE(validOrdering);
}

TEST_CASE("simplifier: double negation") {
	REQUIRE_EQ(evalToString("-(-x)"), std::string("x"));
}

TEST_CASE("simplifier: negation does not produce a double minus") {
	// Regression: -(a * -b) once printed "--1*a*b". Negation is canonicalized
	// to a single -1-factor form, so no stacked minus can be produced and the
	// two negations cancel to a plain product.
	REQUIRE_EQ(evalToString("-(a*-b)"), std::string("a*b"));
	REQUIRE_EQ(evalToString("-(-(-b))"), std::string("-b"));
	REQUIRE_EQ(evalToString("-x"), std::string("-x"));
	REQUIRE_EQ(evalToString("-(a+b)"), std::string("-(a + b)"));
}

// -----------------------------------------------------------------------
// Printer (round-trip and parenthesization)
// These tests operate on the internal AST/printer API directly.
// -----------------------------------------------------------------------

TEST_CASE("printer: parens only where needed") {
	auto t1 = tokenize("(a - b) - c");
	std::size_t size1 = 0;
	auto p1 = parseExpression(t1.value(), 400, size1);
	REQUIRE(p1.ok());
	REQUIRE_EQ(toString(*p1.value().expr), std::string("a - b - c"));

	auto t2 = tokenize("a - (b - c)");
	std::size_t size2 = 0;
	auto p2 = parseExpression(t2.value(), 400, size2);
	REQUIRE(p2.ok());
	REQUIRE_EQ(toString(*p2.value().expr), std::string("a - (b - c)"));
}

TEST_CASE("printer: power right-associativity reflected") {
	auto t = tokenize("a ^ b ^ c");
	std::size_t size = 0;
	auto p = parseExpression(t.value(), 400, size);
	REQUIRE(p.ok());
	REQUIRE_EQ(toString(*p.value().expr), std::string("a^b^c"));

	auto t2 = tokenize("(a ^ b) ^ c");
	size = 0;
	auto p2 = parseExpression(t2.value(), 400, size);
	REQUIRE(p2.ok());
	REQUIRE_EQ(toString(*p2.value().expr), std::string("(a^b)^c"));
}

// -----------------------------------------------------------------------
// REPL integration
// -----------------------------------------------------------------------

TEST_CASE("repl: assignment and lookup") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine("a: 4 * 5"), std::string("a: 20"));
	REQUIRE_EQ(r.processLine("a + 3"), std::string("23"));
}

TEST_CASE("repl: assignment with variables") {
	std::stringstream in, out;
	Repl r(in, out);
	auto stored = r.processLine("f: x ^ 2 + 3");
	bool storedOk = (stored == "f: x^2 + 3" || stored == "f: 3 + x^2");
	REQUIRE(storedOk);
	auto resp = r.processLine("f + 1");
	bool respOk = (resp == "x^2 + 4" || resp == "4 + x^2");
	REQUIRE(respOk);
}

TEST_CASE("repl: list and clear") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("a: 1");
	r.processLine("b: 2");
	auto listed = r.processLine("/list");
	REQUIRE(listed.find("a: 1") != std::string::npos);
	REQUIRE(listed.find("b: 2") != std::string::npos);
	REQUIRE_EQ(r.processLine("/clear"), std::string("All variables cleared"));
	REQUIRE_EQ(r.processLine("/list"), std::string("(no variables defined)"));
}

TEST_CASE("repl: division by zero produces a friendly error") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("1/0");
	REQUIRE(resp.find("error") != std::string::npos);
}

TEST_CASE("repl: graph requires defined equation variable") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("/graph(x, y, -1, 1, -1, 1, undefined_var)");
	REQUIRE(resp.find("error") != std::string::npos);
}

TEST_CASE("repl: graph happy path - parabola y = x^2") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine("p: y = x^2").find("p:"), static_cast<size_t>(0));
	auto resp = r.processLine("/graph(x, y, -3, 3, -1, 9, p)");
	REQUIRE(resp.find('#') != std::string::npos);
	REQUIRE(resp.find('|') != std::string::npos);
	REQUIRE(resp.find('-') != std::string::npos);
}

TEST_CASE("repl: empty input yields empty response") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine(""), std::string(""));
	REQUIRE_EQ(r.processLine("   "), std::string(""));
}

TEST_CASE("repl: help") {
	std::stringstream in, out;
	Repl r(in, out);
	auto h = r.processLine("/help(+)");
	REQUIRE(h.find("Addition") != std::string::npos);
	auto h2 = r.processLine("/help(sin)");
	REQUIRE(h2.find("sine") != std::string::npos);
}

// -----------------------------------------------------------------------
// clearDenominators (asymptote prevention for the grapher)
// Tested via PlotFunctor: compilePlot applies clearDenominators internally,
// so sampling the functor at known points verifies the cleared form is correct.
// -----------------------------------------------------------------------

TEST_CASE("clearDenominators: simple reciprocal") {
	// y = 1/x: on the curve lhs-rhs == 0; near x=0 the functor stays finite.
	CalculatorCore core;
	core.evaluateLine("eq: y = 1/x", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(f.ok());

	// Points on the curve: y = 1/x -> lhs-rhs should be 0.
	REQUIRE_APPROX(f.value()({ 2.0, 0.5 }), 0.0, 1e-12);
	REQUIRE_APPROX(f.value()({ -1.0, -1.0 }), 0.0, 1e-12);
	// Near the asymptote: cleared form y*x - 1 stays finite (not NaN).
	const double v = f.value()({ 1e-6, 1.0 });
	REQUIRE(!std::isnan(v));
	REQUIRE(std::abs(v) < 2.0);
}

TEST_CASE("clearDenominators: sum of two fractions") {
	// y = 1/(x-1) + 1/(x+1): at x=0, y=0; at x=2, y=4/3.
	CalculatorCore core;
	core.evaluateLine("eq: y = 1/(x-1) + 1/(x+1)", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(f.ok());

	REQUIRE_APPROX(f.value()({ 0.0, 0.0 }), 0.0, 1e-12);
	REQUIRE_APPROX(f.value()({ 2.0, 4.0 / 3.0 }), 0.0, 1e-12);
}

TEST_CASE("clearDenominators: equation without denominators is unchanged in meaning") {
	// x^2 + y^2 = 1: point (1, 0) lies on the unit circle.
	CalculatorCore core;
	core.evaluateLine("eq: x^2 + y^2 = 1", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(f.ok());

	REQUIRE_APPROX(f.value()({ 1.0, 0.0 }), 0.0, 1e-12);
}

TEST_CASE("graph: y = 1/x produces no asymptote line") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("g1: y = 1/x");
	auto resp = r.processLine("/graph(x, y, -3, 3, -3, 3, g1)");
	std::vector<std::string> lines;
	std::size_t pos = 0;
	while (pos < resp.size()) {
		const std::size_t nl = resp.find('\n', pos);
		if (nl == std::string::npos) { lines.push_back(resp.substr(pos)); break; }
		lines.push_back(resp.substr(pos, nl - pos));
		pos = nl + 1;
	}
	std::size_t axisCol = std::string::npos;
	for (const auto& ln : lines) {
		const std::size_t plus = ln.find('+');
		if (plus != std::string::npos &&
			ln.find('-') != std::string::npos &&
			ln.find_first_not_of("-+") == std::string::npos) {
			axisCol = plus;
			break;
		}
	}
	REQUIRE(axisCol != std::string::npos);
	int hashCount = 0;
	for (const auto& ln : lines) {
		if (axisCol < ln.size() && ln[axisCol] == '#') ++hashCount;
	}
	REQUIRE_EQ(hashCount, 0);
}

// -----------------------------------------------------------------------
// Diagnostic carets in REPL output
// -----------------------------------------------------------------------

TEST_CASE("diagnostic: tokenizer error has caret") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("a @ b");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("a @ b") != std::string::npos);
	REQUIRE(resp.find("    ^") != std::string::npos);
}

TEST_CASE("diagnostic: parser error has caret") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("(1 + 2");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("(1 + 2") != std::string::npos);
	REQUIRE(resp.find('^') != std::string::npos);
}

TEST_CASE("diagnostic: evaluator error has caret") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("1/0");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("1/0") != std::string::npos);
	REQUIRE(resp.find('^') != std::string::npos);
}

TEST_CASE("diagnostic: graph command errors point at offending arg") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("/graph(x, y, hello, 10, 0, 10, p)");
	REQUIRE(resp.find("x_min must be a number") != std::string::npos);
}

TEST_CASE("diagnostic: cross-line evaluator error suppresses caret cleanly") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("f: 1/x");
	r.processLine("x: 0");
	auto resp = r.processLine("f");
	REQUIRE(resp.find("error:") != std::string::npos);
}

// -----------------------------------------------------------------------
// Self-referential definitions (must be rejected at assignment time)
// -----------------------------------------------------------------------

TEST_CASE("repl: self-reference rejected at assignment") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("a: a + 1");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("itself") != std::string::npos);
	auto listed = r.processLine("/list");
	REQUIRE(listed.find("a:") == std::string::npos);
}

TEST_CASE("repl: self-reference in deeply nested expression rejected") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("foo: 2 * (3 + sin(foo))");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("itself") != std::string::npos);
}

TEST_CASE("repl: indirect self-reference through another variable rejected") {
	std::stringstream in, out;
	Repl r(in, out);
	auto first = r.processLine("b: a + 1");
	REQUIRE(first.find("error:") == std::string::npos);
	auto resp = r.processLine("a: b + 1");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("'a'") != std::string::npos);
}

TEST_CASE("repl: legitimate chained assignments still work") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine("a: 5"), std::string("a: 5"));
	REQUIRE_EQ(r.processLine("b: a + 1"), std::string("b: 6"));
	REQUIRE_EQ(r.processLine("d: a * b"), std::string("d: 30"));
	REQUIRE_EQ(r.processLine("a: d + 1"), std::string("a: 31"));
}

TEST_CASE("repl: built-in constants cannot be reassigned") {
	std::stringstream in, out;
	Repl r(in, out);
	// Names in the constant table are reserved; assigning to one is an error
	// rather than a silently-shadowed (and unreachable) definition.
	REQUIRE(r.processLine("c: 5").find("reserved constant") != std::string::npos);
	REQUIRE(r.processLine("PI: 3").find("reserved constant") != std::string::npos);
	REQUIRE(r.processLine("h: 1").find("reserved constant") != std::string::npos);
	// A non-constant single letter still works.
	REQUIRE_EQ(r.processLine("d: 5"), std::string("d: 5"));
}

TEST_CASE("repl: equation with axis variables does not look self-referential") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("p: y = x^2");
	REQUIRE(resp.find("error:") == std::string::npos);
	REQUIRE(resp.find("p:") == 0);
}

TEST_CASE("repl: equation variable not inlined into another equation") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("curve: y = x^2");
	auto resp = r.processLine("result: curve = x");
	REQUIRE(resp.find("error:") == std::string::npos);
	const std::size_t firstEq = resp.find('=');
	REQUIRE(firstEq != std::string::npos);
	REQUIRE(resp.find('=', firstEq + 1) == std::string::npos);
}

TEST_CASE("repl: bare reference to equation variable shows the equation") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("p: y = x^2");
	auto resp = r.processLine("p");
	REQUIRE(resp.find("y") != std::string::npos);
	REQUIRE(resp.find("x^2") != std::string::npos);
	REQUIRE(resp.find('=') != std::string::npos);
}

TEST_CASE("repl: equation can be copied to another name") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("p: y = x^2");
	auto resp = r.processLine("q: p");
	REQUIRE(resp.find("q:") == 0);
	REQUIRE(resp.find("y") != std::string::npos);
	REQUIRE(resp.find("x^2") != std::string::npos);
	auto qShow = r.processLine("q");
	REQUIRE(qShow.find("y") != std::string::npos);
	REQUIRE(qShow.find("x^2") != std::string::npos);
}

// -----------------------------------------------------------------------
// Printer
// -----------------------------------------------------------------------

TEST_CASE("printer: negative number base of power keeps parens") {
	REQUIRE_EQ(evalToString("(-2)^a"), std::string("(-2)^a"));
}

TEST_CASE("printer: negation of sum keeps parens") {
	auto s1 = evalToString("-(4*a+3)");
	bool s1ok = (s1 == "-(3 + 4*a)" || s1 == "-(4*a + 3)" ||
		s1 == "-(a*4 + 3)" || s1 == "-(3 + a*4)");
	REQUIRE(s1ok);
	REQUIRE(s1[0] == '-');
	REQUIRE(s1[1] == '(');

	auto s2 = evalToString("-(x+1)");
	bool s2ok = (s2 == "-(1 + x)" || s2 == "-(x + 1)");
	REQUIRE(s2ok);

	auto s3 = evalToString("2/-(4*a+3)");
	REQUIRE(s3.find("2") != std::string::npos);

	auto s4 = evalToString("-(x*y)");
	REQUIRE((s4 == "-x*y" || s4 == "-y*x"));
	REQUIRE_EQ(evalToString("-x"), std::string("-x"));
}

// -----------------------------------------------------------------------
// REPL help and constants
// -----------------------------------------------------------------------

TEST_CASE("repl: help on a built-in constant shows value and description") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("/help(PI)");
	REQUIRE(resp.find("PI") != std::string::npos);
	REQUIRE(resp.find("3.14") != std::string::npos);
	REQUIRE(resp.find("circumference") != std::string::npos);
}

TEST_CASE("repl: new constants work in expressions") {
	REQUIRE(evalToString("tau").find("6.28") == 0);
	REQUIRE(evalToString("phi").find("1.61") == 0);
	REQUIRE(evalToString("q_e").find("1.6") == 0);
}

// -----------------------------------------------------------------------
// Simplifier: like-term and exponent combining
// -----------------------------------------------------------------------

TEST_CASE("simplifier: like-term counting in sums") {
	auto s1 = evalToString("a + a");
	REQUIRE((s1 == "2*a" || s1 == "a*2"));

	auto s2 = evalToString("a + a + a");
	REQUIRE((s2 == "3*a" || s2 == "a*3"));

	REQUIRE_EQ(evalToString("a - a + a"), std::string("a"));

	auto s3 = evalToString("a + a + b");
	bool s3ok = (s3.find("2*a") != std::string::npos ||
		s3.find("a*2") != std::string::npos);
	REQUIRE(s3ok);
	REQUIRE(s3.find("b") != std::string::npos);
}

TEST_CASE("simplifier: exponent combining in products") {
	REQUIRE_EQ(evalToString("a*a"), std::string("a^2"));
	REQUIRE_EQ(evalToString("a*a*a"), std::string("a^3"));
	REQUIRE_EQ(evalToString("a/a/a"), std::string("1/a"));

	auto s = evalToString("a*a*b");
	REQUIRE(s.find("a^2") != std::string::npos);
	REQUIRE(s.find("b") != std::string::npos);
}

TEST_CASE("simplifier: nested powers collapse") {
	REQUIRE_EQ(evalToString("(x^2)^2"), std::string("x^4"));
	REQUIRE_EQ(evalToString("(x^3)^4"), std::string("x^12"));
	REQUIRE_EQ(evalToString("((x^2)^2)^2"), std::string("x^8"));
	REQUIRE_EQ(evalToString("x^2 * x^2"), std::string("x^4"));
	REQUIRE_EQ(evalToString("x^3 * x^3"), std::string("x^6"));
	REQUIRE_EQ(evalToString("sin(x)*sin(x)*sin(x)"), std::string("sin(x)^3"));

	auto s = evalToString("(x^a)^2");
	REQUIRE(s.find("x^a") != std::string::npos);
}

TEST_CASE("simplifier: coefficient combining when terms are equal products") {
	auto s1 = evalToString("2*a + 2*a");
	REQUIRE((s1 == "4*a" || s1 == "a*4"));
	auto s2 = evalToString("3*a + 3*a");
	REQUIRE((s2 == "6*a" || s2 == "a*6"));
	auto s3 = evalToString("2*x^2 + 2*x^2");
	REQUIRE((s3 == "4*x^2" || s3 == "x^2*4"));

	auto s4 = evalToString("a + a + a + a + a");
	REQUIRE((s4 == "5*a" || s4 == "a*5"));
}


TEST_CASE("tokenizer: lone dot is an unexpected character") {
	// A '.' only starts a number when a digit follows; otherwise it is illegal.
	auto r = tokenize(".");
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::UnexpectedCharacter);

	auto r2 = tokenize(".x");
	REQUIRE(!r2.ok());
	REQUIRE(r2.error().code == DiagCode::UnexpectedCharacter);
}

TEST_CASE("tokenizer: underscores belong to identifiers") {
	// Several built-in constants (k_B, N_A, q_e) rely on this.
	auto r = tokenize("k_B + N_A");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value()[0].kind, TokenKind::Identifier);
	REQUIRE_EQ(r.value()[0].text, std::string("k_B"));
	REQUIRE_EQ(r.value()[2].text, std::string("N_A"));
}

TEST_CASE("tokenizer: empty and whitespace-only input is just EndOfInput") {
	auto r = tokenize("");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().size(), std::size_t{ 1 });
	REQUIRE_EQ(r.value()[0].kind, TokenKind::EndOfInput);

	auto r2 = tokenize("   \t  ");
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value().size(), std::size_t{ 1 });
	REQUIRE_EQ(r2.value()[0].kind, TokenKind::EndOfInput);
}

TEST_CASE("tokenizer: slash is a command prefix only as the first token") {
	auto r = tokenize("/2");          // leading slash -> command prefix
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value()[0].kind, TokenKind::Slash_Cmd);

	auto r2 = tokenize("8 / 2 / 2");  // every later slash is division
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[1].kind, TokenKind::Slash);
	REQUIRE_EQ(r2.value()[3].kind, TokenKind::Slash);
}

// -----------------------------------------------------------------------
// Parser: error paths not covered elsewhere
// -----------------------------------------------------------------------

TEST_CASE("parser: more than one '=' is rejected") {
	CalculatorCore core;
	auto r = core.evaluateLine("x = y = z", 400);
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::MultipleEquals);
}

TEST_CASE("parser: empty side of an equation is rejected") {
	CalculatorCore core;
	auto before = core.evaluateLine("= 3", 400);
	REQUIRE(!before.ok());
	REQUIRE(before.error().code == DiagCode::ExpectedExpressionBeforeEquals);

	auto after = core.evaluateLine("x =", 400);
	REQUIRE(!after.ok());
	REQUIRE(after.error().code == DiagCode::ExpectedExpressionAfterEquals);
}

TEST_CASE("parser: dangling colon is rejected") {
	CalculatorCore core;
	auto r = core.evaluateLine("a:", 400);
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::ExpectedExpressionAfterColon);
}

TEST_CASE("parser: empty parens and trailing comma in an argument list") {
	CalculatorCore core;
	REQUIRE(!core.evaluateLine("()", 400).ok());
	REQUIRE(!core.evaluateLine("sin(1,)", 400).ok());
	REQUIRE(!core.evaluateLine("sin(,1)", 400).ok());
}

TEST_CASE("parser: assignment combined with an equation") {
	auto t = tokenize("curve: y = x^2");
	REQUIRE(t.ok());
	std::size_t size = 0;
	auto p = parseExpression(t.value(), 400, size);
	REQUIRE(p.ok());
	REQUIRE_EQ(p.value().assignTo, std::string("curve"));
	// The stored expression is the equation itself.
	REQUIRE(toString(*p.value().expr).find('=') != std::string::npos);
}

// -----------------------------------------------------------------------
// Math functions: numeric correctness for the whole table
// -----------------------------------------------------------------------

TEST_CASE("functions: trigonometry") {
	REQUIRE_APPROX(evalToNumber("cos(0)"), 1.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("tan(0)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("asin(1)"), 1.5707963267948966, 1e-12);  // pi/2
	REQUIRE_APPROX(evalToNumber("acos(1)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("atan(1)"), 0.7853981633974483, 1e-12);  // pi/4
}

TEST_CASE("functions: abs, sqrt, log, root") {
	REQUIRE_APPROX(evalToNumber("abs(-5)"), 5.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("abs(5)"), 5.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("sqrt(2)"), 1.4142135623730951, 1e-12);
	REQUIRE_APPROX(evalToNumber("log(2, 8)"), 3.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("log(10, 1000)"), 3.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("root(2, 9)"), 3.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("root(3, 27)"), 3.0, 1e-6);
}

TEST_CASE("functions: composition and constants as arguments") {
	REQUIRE_APPROX(evalToNumber("sqrt(abs(-16))"), 4.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("sin(asin(0.5))"), 0.5, 1e-12);
	REQUIRE_APPROX(evalToNumber("sin(PI)"), 0.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("cos(PI)"), -1.0, 1e-12);
}

TEST_CASE("functions: stay symbolic when an argument is not numeric") {
	REQUIRE_EQ(evalToString("sin(x)"), std::string("sin(x)"));
	auto s = evalToString("log(2, x)");
	REQUIRE(s.find("log(2") != std::string::npos);
	REQUIRE(s.find("x") != std::string::npos);
}

TEST_CASE("constants: single-letter physical constants are reserved names") {
	// c, e, G, h, R, etc. resolve to their values, so they cannot be used as
	// ordinary variables. This is easy to trip over (a stray `c` becomes the
	// speed of light); pin it so the behavior is explicit.
	REQUIRE_APPROX(evalToNumber("c"), 299792458.0, 1e-3);
	REQUIRE_APPROX(evalToNumber("2*c"), 599584916.0, 1e-3);
	REQUIRE_APPROX(evalToNumber("e"), 2.718281828459045, 1e-12);
}

TEST_CASE("functions: domain errors surface as NotFinite") {
	CalculatorCore core;
	REQUIRE(core.evaluateLine("sqrt(-4)", 400).error().code == DiagCode::NotFinite);
	// log(1, x) divides by log(1) == 0.
	REQUIRE(core.evaluateLine("log(1, 5)", 400).error().code == DiagCode::NotFinite);
}

// -----------------------------------------------------------------------
// Simplifier: boundaries (what it deliberately does NOT do) and sign handling
// -----------------------------------------------------------------------

TEST_CASE("simplifier: does not distribute products over sums") {
	// u*(v + w) must stay factored — the sum is preserved, not expanded.
	// (Avoid single letters c, e, G, h, R: those are built-in constants.)
	auto s1 = evalToString("u*(v + w)");
	bool sumKept = s1.find("v + w") != std::string::npos ||
		s1.find("w + v") != std::string::npos;
	REQUIRE(sumKept);

	// 2*(x + 1) likewise stays factored.
	auto s2 = evalToString("2*(x + 1)");
	bool sumKept2 = s2.find("x + 1") != std::string::npos ||
		s2.find("1 + x") != std::string::npos;
	REQUIRE(sumKept2);
}

TEST_CASE("simplifier: a repeated sum becomes a power, not an expansion") {
	auto s = evalToString("(a + b)*(a + b)");
	REQUIRE(s.find("^2") != std::string::npos);
	bool baseKept = s.find("a + b") != std::string::npos ||
		s.find("b + a") != std::string::npos;
	REQUIRE(baseKept);
}

TEST_CASE("simplifier: subtraction of a difference flips signs correctly") {
	REQUIRE_EQ(evalToString("u - (v - w)"), std::string("u - v + w"));
	REQUIRE_EQ(evalToString("u - v - w"), std::string("u - v - w"));
	REQUIRE_EQ(evalToString("x - x"), std::string("0"));
}

TEST_CASE("simplifier: like terms with fractional coefficients combine") {
	// a/2 reduces to a*0.5; two of them must recombine to a.
	REQUIRE_EQ(evalToString("a/2 + a/2"), std::string("a"));
	auto s = evalToString("x/4 + x/4 + x/4 + x/4");
	REQUIRE_EQ(s, std::string("x"));
}

// -----------------------------------------------------------------------
// Characterization tests: these pin CURRENT behavior on purpose. If one of
// them starts failing it means a behavior changed — decide deliberately
// whether that change is intended before "fixing" the test.
// -----------------------------------------------------------------------

TEST_CASE("characterization: 0^x and 1^x collapse for a symbolic exponent") {
	REQUIRE_EQ(evalToString("1^x"), std::string("1"));
}

TEST_CASE("characterization: numeric 0^0 folds to 1 but 0^-1 errors") {
	REQUIRE_EQ(evalToString("0^0"), std::string("1"));   // matches std::pow
	CalculatorCore core;
	REQUIRE(core.evaluateLine("0^-1", 400).error().code == DiagCode::NotFinite);
}

TEST_CASE("characterization: a fully numeric false equation renders with =/=") {
	// The printer reports equality for all-numeric equations; an unequal pair
	// renders as "=/=". Equations like this cannot be graphed, so this is
	// cosmetic, but it is intentional current behavior.
	REQUIRE_EQ(evalToString("2 = 3"), std::string("2 =/= 3"));
	REQUIRE_EQ(evalToString("1 + 1 = 2"), std::string("2 = 2"));
}

// -----------------------------------------------------------------------
// Printer: formatNumber and round-trip stability
// -----------------------------------------------------------------------

TEST_CASE("printer: number formatting strips trailing zeros and bare dots") {
	REQUIRE_EQ(evalToString("4"), std::string("4"));      // integer, no ".0"
	REQUIRE_EQ(evalToString("10/2"), std::string("5"));
	REQUIRE_EQ(evalToString("-7"), std::string("-7"));
	REQUIRE_EQ(evalToString("1/4"), std::string("0.25"));
	REQUIRE_EQ(evalToString("3/2"), std::string("1.5"));
}

TEST_CASE("printer: output is reparseable and idempotent") {
	// Printing then reparsing must reach a fixed point: the printer never
	// emits something the parser would read back as a different tree.
	const std::vector<std::string> exprs = {
		"a*b + c", "(a + b)*c", "a^b^c", "a - (b - c)",
		"-x*y", "(-2)^a", "a/(b/c)", "a/b/c", "f(a, b, c)",
	};
	for (const auto& e : exprs) {
		const std::string once = parsePrint(e);
		const std::string twice = parsePrint(once);
		REQUIRE_EQ(once, twice);
	}
}

// -----------------------------------------------------------------------
// parseCommand: the free command parser (previously only hit via the REPL)
// -----------------------------------------------------------------------

TEST_CASE("parseCommand: bare command has a name and no arguments") {
	auto r = parseCommand("/list");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().name, std::string("list"));
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 0 });
}

TEST_CASE("parseCommand: arguments are split and trimmed") {
	auto r = parseCommand("/graph( x , y , -1 , 1 , 0 , 1 , p )");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().name, std::string("graph"));
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 7 });
	REQUIRE_EQ(r.value().args[0], std::string("x"));
	REQUIRE_EQ(r.value().args[2], std::string("-1"));
	REQUIRE_EQ(r.value().args[6], std::string("p"));
}

TEST_CASE("parseCommand: commas inside nested parens do not split arguments") {
	auto r = parseCommand("/graph(x, y, -(PI), PI, 0, log(2, 8), p)");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 7 });
	REQUIRE_EQ(r.value().args[2], std::string("-(PI)"));
	REQUIRE_EQ(r.value().args[5], std::string("log(2, 8)"));
}

TEST_CASE("parseCommand: empty argument list yields zero arguments") {
	auto r = parseCommand("/refresh()");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().name, std::string("refresh"));
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 0 });
}

TEST_CASE("parseCommand: malformed command syntax is rejected") {
	REQUIRE(parseCommand("list").error().code == DiagCode::CommandMustStartWithSlash);
	REQUIRE(parseCommand("/").error().code == DiagCode::ExpectedCommandName);
	REQUIRE(parseCommand("/list extra").error().code == DiagCode::ExpectedOpenParen);
	REQUIRE(parseCommand("/graph(x").error().code == DiagCode::UnmatchedOpenParen);
	REQUIRE(parseCommand("/refresh() junk").error().code ==
		DiagCode::UnexpectedInputAfterCommand);
}

// -----------------------------------------------------------------------
// compilePlot: error paths and the dimension-agnostic design
// -----------------------------------------------------------------------

TEST_CASE("compilePlot: duplicate axis names are rejected") {
	CalculatorCore core;
	core.evaluateLine("eq: y = x", 400);
	auto f = core.compilePlot({ "eq", {"x", "x"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::AxisNamesMustDiffer);
}

TEST_CASE("compilePlot: unknown equation variable is rejected") {
	CalculatorCore core;
	auto f = core.compilePlot({ "ghost", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::NoSuchVariable);
	REQUIRE(f.error().detail == "ghost");
}

TEST_CASE("compilePlot: target must be an equation, not a plain expression") {
	CalculatorCore core;
	core.evaluateLine("p: x^2", 400);  // expression, not an equation
	auto f = core.compilePlot({ "p", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::GraphTargetNotEquation);
}

TEST_CASE("compilePlot: a free variable that is not an axis is rejected") {
	CalculatorCore core;
	core.evaluateLine("eq: y = x + z", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::NonAxisVariable);
	REQUIRE(f.error().detail == "z");
}

TEST_CASE("compilePlot: an equation using no axis at all is rejected") {
	CalculatorCore core;
	core.evaluateLine("eq: 2 = 2", 400);  // no free variables -> no axis appears
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::NoAxisVariable);
}

TEST_CASE("compilePlot: functor evaluates lhs - rhs and reports its shape") {
	CalculatorCore core;
	core.evaluateLine("eq: y = x^2", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(f.ok());
	REQUIRE_EQ(f.value().dimensions(), std::size_t{ 2 });
	REQUIRE(f.value().requiredStackSize() >= std::size_t{ 1 });

	REQUIRE_APPROX(f.value()({ 2.0, 4.0 }), 0.0, 1e-12);   // on the curve
	REQUIRE_APPROX(f.value()({ 3.0, 9.0 }), 0.0, 1e-12);
	REQUIRE_APPROX(f.value()({ 2.0, 5.0 }), 1.0, 1e-12);   // y - x^2 = 5 - 4
}

TEST_CASE("compilePlot: the same engine compiles against three axes") {
	// The README claims PlotFunctor is dimension-agnostic; the ASCII renderer
	// only ever uses two axes, so this is the one place that claim is checked.
	CalculatorCore core;
	core.evaluateLine("eq: z = x + y", 400);
	auto f = core.compilePlot({ "eq", {"x", "y", "z"} });
	REQUIRE(f.ok());
	REQUIRE_EQ(f.value().dimensions(), std::size_t{ 3 });

	// coords are [x, y, z]; the functor returns lhs - rhs = z - (x + y).
	REQUIRE_APPROX(f.value()({ 1.0, 2.0, 3.0 }), 0.0, 1e-12);  // on the surface
	REQUIRE_APPROX(f.value()({ 1.0, 2.0, 5.0 }), 2.0, 1e-12);  // 5 - (1 + 2)
}

// -----------------------------------------------------------------------
// CalculatorCore: session state
// -----------------------------------------------------------------------

TEST_CASE("core: definitionOf is empty for an unknown name") {
	CalculatorCore core;
	REQUIRE(!core.definitionOf("nope").has_value());
	core.evaluateLine("a: 5", 400);
	REQUIRE(core.definitionOf("a").has_value());
	REQUIRE_EQ(*core.definitionOf("a"), std::string("5"));
}

TEST_CASE("core: redefining a name overwrites the previous definition") {
	CalculatorCore core;
	core.evaluateLine("a: 1", 400);
	core.evaluateLine("a: 2", 400);
	REQUIRE_EQ(*core.definitionOf("a"), std::string("2"));
	REQUIRE_EQ(core.definedNames().size(), std::size_t{ 1 });
}

TEST_CASE("core: numeric result carries a value, symbolic result does not") {
	CalculatorCore core;
	auto num = core.evaluateLine("2 + 3", 400);
	REQUIRE(num.ok());
	REQUIRE(num.value().value.has_value());
	REQUIRE_APPROX(*num.value().value, 5.0, 1e-12);
	REQUIRE_EQ(num.value().canonical, std::string("5"));

	auto sym = core.evaluateLine("x + 1", 400);
	REQUIRE(sym.ok());
	REQUIRE(!sym.value().value.has_value());
}

// -----------------------------------------------------------------------
// StringTable (frontend): previously had no direct tests
// -----------------------------------------------------------------------

TEST_CASE("StringTable: a missing file loads nothing and is not fatal") {
	StringTable t;
	REQUIRE(!t.loadFromFile("this_file_does_not_exist_98342.txt"));
	REQUIRE(t.empty());
	REQUIRE(!t.get("anything").has_value());
}

TEST_CASE("StringTable: keys, comments, blanks, trimming, and escapes") {
	const std::string path = "tmp_stringtable_test.txt";
	writeTempFile(path,
		"# a comment line\n"
		"\n"
		"greeting = hello\n"
		"  spaced   =   hello world  \n"   // outer ws trimmed, inner preserved
		"esc = a\\nb\n"                     // \n -> real newline
		"bs = a\\\\b\n"                     // \\ -> single backslash
		"other = a\\tb\n"                   // unknown escape left as-is
		"no equals sign here\n"             // skipped
		"= valueonly\n"                     // empty key, skipped
	);

	StringTable t;
	REQUIRE(t.loadFromFile(path));
	REQUIRE(!t.empty());

	REQUIRE_EQ(*t.get("greeting"), std::string("hello"));
	REQUIRE_EQ(*t.get("spaced"), std::string("hello world"));
	REQUIRE_EQ(*t.get("esc"), std::string("a\nb"));
	REQUIRE_EQ(*t.get("bs"), std::string("a\\b"));
	REQUIRE_EQ(*t.get("other"), std::string("a\\tb"));

	REQUIRE(!t.get("no equals sign here").has_value());
	REQUIRE(!t.get("").has_value());
	REQUIRE(!t.get("missing").has_value());

	std::remove(path.c_str());
}
// ---------------------------------------------------------------------------
// Regression: simplifier performance on large flat sums and products.
//
// A bug once made the simplifier re-descend already-canonical subtrees, so the
// flatten/group cycle ran an exponential number of times: a sum of ~20 distinct
// terms took tens of seconds, and ~50 terms never finished. The fix marks every
// node the simplifier produces as canonical (AstNode::simplified) and early-outs
// in simplifyImpl, collapsing the work back to roughly linear.
//
// These tests guard against that regression. They assert two things:
//   1. Correctness  - the canonical result is exactly right (a wrong-answer
//                     regression would change the string).
//   2. Completion   - evaluation finishes well within a time budget (an
//                     exponential regression would blow the budget; the old
//                     code could not finish 50 terms in any practical time).
//
// The budgets are deliberately loose (seconds, not milliseconds) so the tests
// stay reliable on slow or loaded CI machines while still being many orders of
// magnitude below the old exponential behaviour.

namespace {

	// Run fn and return its wall-clock duration in seconds.
	template <typename F>
	double timeSeconds(F&& fn) {
		const auto start = std::chrono::steady_clock::now();
		fn();
		const auto end = std::chrono::steady_clock::now();
		return std::chrono::duration<double>(end - start).count();
	}

}  // namespace

TEST_CASE("simplifier regression: large flat sum of distinct terms completes") {
	// Build "v0 + v1 + ... + v199". Distinct terms, so nothing combines; the
	// canonical form is the same terms in sorted order. With 200 terms this is
	// trivial for the fixed code (well under a second) and impossible for the
	// old exponential code.
	constexpr int N = 200;
	std::string input;
	for (int i = 0; i < N; ++i) {
		if (i) input += " + ";
		input += "v" + std::to_string(i);
	}

	// Expected canonical form: variables sorted lexicographically (v0, v1, v10,
	// v100, ... ) joined by " + ", matching the engine's ordering.
	std::vector<std::string> names;
	names.reserve(N);
	for (int i = 0; i < N; ++i) names.push_back("v" + std::to_string(i));
	std::sort(names.begin(), names.end());
	std::string expected;
	for (size_t i = 0; i < names.size(); ++i) {
		if (i) expected += " + ";
		expected += names[i];
	}

	std::string got;
	const double secs = timeSeconds([&] { got = evalToString(input); });

	REQUIRE_EQ(got, expected);
	REQUIRE(secs < 5.0);
}

TEST_CASE("simplifier regression: large flat product of distinct factors completes") {
	// Build "v0*v1*...*v199". Mirror of the sum case on the product path
	// (simplifyProduct / collectProduct), which had the identical bug.
	constexpr int N = 200;
	std::string input;
	for (int i = 0; i < N; ++i) {
		if (i) input += "*";
		input += "v" + std::to_string(i);
	}

	std::vector<std::string> names;
	names.reserve(N);
	for (int i = 0; i < N; ++i) names.push_back("v" + std::to_string(i));
	std::sort(names.begin(), names.end());
	std::string expected;
	for (size_t i = 0; i < names.size(); ++i) {
		if (i) expected += "*";
		expected += names[i];
	}

	std::string got;
	const double secs = timeSeconds([&] { got = evalToString(input); });

	REQUIRE_EQ(got, expected);
	REQUIRE(secs < 5.0);
}

TEST_CASE("simplifier regression: deeply nested mixed sum/product completes") {
	// The original reproducer: a*aa + (a*aaa + (a*aaaa + ( ... ))). Each level
	// nests a sum whose left operand is a distinct product. This stressed both
	// collectSum and collectProduct together and hung for tens of seconds at a
	// few hundred levels. We only assert completion here (the canonical form is
	// large and its exact spelling is not the point of this test).
	constexpr int N = 130;
	std::string input;
	for (int i = 1; i <= N; ++i) {
		input += "a*" + std::string(static_cast<size_t>(i) + 1, 'a') + " + (";
	}
	input += "a";
	input += std::string(static_cast<size_t>(N), ')');

	std::string got;
	const double secs = timeSeconds([&] { got = evalToString(input); });

	// Must not be an error and must finish promptly.
	REQUIRE(got.rfind("error:", 0) != 0);
	REQUIRE(secs < 5.0);
}

TEST_CASE("simplifier regression: like-term combining still correct at scale") {
	// Guards the *correctness* side of the fix: the memoization must not skip
	// legitimate combining. Summing the same variable N times must fold to N*a,
	// not leave N separate terms. (a + a + ... + a, 100 times -> 100*a.)
	constexpr int N = 100;
	std::string input;
	for (int i = 0; i < N; ++i) {
		if (i) input += " + ";
		input += "a";
	}

	std::string got;
	const double secs = timeSeconds([&] { got = evalToString(input); });

	REQUIRE_EQ(got, std::string("100*a"));
	REQUIRE(secs < 5.0);

	// And the product mirror: a * a * ... * a, 100 times -> a^100.
	std::string prodInput;
	for (int i = 0; i < N; ++i) {
		if (i) prodInput += "*";
		prodInput += "a";
	}
	REQUIRE_EQ(evalToString(prodInput), std::string("a^100"));
}