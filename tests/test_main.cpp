#include <cmath>
#include <sstream>

#include "CalculatorCore.h"
#include "Repl.h"
#include "test_framework.h"

// These headers are only included for the small number of tests that
// exercise internal APIs directly (printer round-tripping).
// Everything else goes through CalculatorCore or the Repl.
#include "Ast.h"
#include "Parser.h"
#include "Printer.h"
#include "Tokenizer.h"

using namespace calc;
using namespace calc::core;

namespace {

// Evaluate a line through the engine and return the canonical string.
// Returns an error string on failure so REQUIRE_EQ failure messages are readable.
std::string evalToString(const std::string& input) {
    CalculatorCore core;
    auto r = core.evaluateLine(input);
    if (!r) return "error: code " + std::to_string(static_cast<int>(r.error().code));
    return r.value().canonical;
}

// Evaluate a line through the engine and return the numeric result.
// Returns NaN on any error or if the result is not a number.
double evalToNumber(const std::string& input) {
    CalculatorCore core;
    auto r = core.evaluateLine(input);
    if (!r) return std::numeric_limits<double>::quiet_NaN();
    if (!r.value().value) return std::numeric_limits<double>::quiet_NaN();
    return *r.value().value;
}

}  // namespace

int main() {
    return test_framework::runAll();
}

// -----------------------------------------------------------------------
// Tokenizer
// -----------------------------------------------------------------------

TEST_CASE("tokenizer: basic tokens") {
    auto r = tokenize("1 + 2 * 3");
    REQUIRE(r.ok());
    auto& toks = r.value();
    REQUIRE_EQ(toks.size(), std::size_t{6});  // 1 + 2 * 3 EOF
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
    REQUIRE_EQ(r.value().size(), std::size_t{3});  // 1.2  .3  EOF
    auto p = parseExpression(r.value());
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
    REQUIRE(!core.evaluateLine("1 +").ok());
    REQUIRE(!core.evaluateLine("1 ** 2").ok());
    REQUIRE(!core.evaluateLine("(1 + 2").ok());
}

// -----------------------------------------------------------------------
// Evaluator
// -----------------------------------------------------------------------

TEST_CASE("evaluator: division by zero") {
    CalculatorCore core;
    auto r = core.evaluateLine("1/0");
    REQUIRE(!r.ok());
    REQUIRE(r.error().code == DiagCode::DivisionByZero ||
            r.error().code == DiagCode::NotFinite);
}

TEST_CASE("evaluator: with bindings") {
    CalculatorCore core;
    core.evaluateLine("x: 3");
    core.evaluateLine("y: 5");
    auto r = core.evaluateLine("x*x + y");
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
    auto r = core.evaluateLine("nope + 1");
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
    REQUIRE_EQ(evalToString("a - a"),     std::string("0"));
    REQUIRE_EQ(evalToString("a*b/a"),     std::string("b"));
    REQUIRE_EQ(evalToString("a/a"),       std::string("1"));
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

// -----------------------------------------------------------------------
// Printer (round-trip and parenthesization)
// These tests operate on the internal AST/printer API directly.
// -----------------------------------------------------------------------

TEST_CASE("printer: parens only where needed") {
    auto t1 = tokenize("(a - b) - c");
    auto p1 = parseExpression(t1.value());
    REQUIRE(p1.ok());
    REQUIRE_EQ(toString(*p1.value().expr), std::string("a - b - c"));

    auto t2 = tokenize("a - (b - c)");
    auto p2 = parseExpression(t2.value());
    REQUIRE(p2.ok());
    REQUIRE_EQ(toString(*p2.value().expr), std::string("a - (b - c)"));
}

TEST_CASE("printer: power right-associativity reflected") {
    auto t = tokenize("a ^ b ^ c");
    auto p = parseExpression(t.value());
    REQUIRE(p.ok());
    REQUIRE_EQ(toString(*p.value().expr), std::string("a^b^c"));

    auto t2 = tokenize("(a ^ b) ^ c");
    auto p2 = parseExpression(t2.value());
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
    REQUIRE_EQ(r.processLine("/list"),  std::string("(no variables defined)"));
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
    REQUIRE_EQ(r.processLine(""),    std::string(""));
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
    core.evaluateLine("eq: y = 1/x");
    auto f = core.compilePlot({"eq", {"x", "y"}});
    REQUIRE(f.ok());

    // Points on the curve: y = 1/x -> lhs-rhs should be 0.
    REQUIRE_APPROX(f.value()({2.0, 0.5}),  0.0, 1e-12);
    REQUIRE_APPROX(f.value()({-1.0, -1.0}),  0.0, 1e-12);
    // Near the asymptote: cleared form y*x - 1 stays finite (not NaN).
    const double v = f.value()({1e-6, 1.0});
    REQUIRE(!std::isnan(v));
    REQUIRE(std::abs(v) < 2.0);
}

TEST_CASE("clearDenominators: sum of two fractions") {
    // y = 1/(x-1) + 1/(x+1): at x=0, y=0; at x=2, y=4/3.
    CalculatorCore core;
    core.evaluateLine("eq: y = 1/(x-1) + 1/(x+1)");
    auto f = core.compilePlot({"eq", {"x", "y"}});
    REQUIRE(f.ok());

    REQUIRE_APPROX(f.value()({0.0, 0.0}),       0.0, 1e-12);
    REQUIRE_APPROX(f.value()({2.0, 4.0/3.0}),   0.0, 1e-12);
}

TEST_CASE("clearDenominators: equation without denominators is unchanged in meaning") {
    // x^2 + y^2 = 1: point (1, 0) lies on the unit circle.
    CalculatorCore core;
    core.evaluateLine("eq: x^2 + y^2 = 1");
    auto f = core.compilePlot({"eq", {"x", "y"}});
    REQUIRE(f.ok());

    REQUIRE_APPROX(f.value()({1.0, 0.0}), 0.0, 1e-12);
}

TEST_CASE("graph: y = 1/x produces no asymptote line") {
    std::stringstream in, out;
    Repl r(in, out);
    r.processLine("h: y = 1/x");
    auto resp = r.processLine("/graph(x, y, -3, 3, -3, 3, h)");
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
    REQUIRE_EQ(r.processLine("a: 5"),     std::string("a: 5"));
    REQUIRE_EQ(r.processLine("b: a + 1"), std::string("b: 6"));
    REQUIRE_EQ(r.processLine("c: a * b"), std::string("c: 30"));
    REQUIRE_EQ(r.processLine("a: c + 1"), std::string("a: 31"));
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
    REQUIRE(resp.find("y")   != std::string::npos);
    REQUIRE(resp.find("x^2") != std::string::npos);
    REQUIRE(resp.find('=')   != std::string::npos);
}

TEST_CASE("repl: equation can be copied to another name") {
    std::stringstream in, out;
    Repl r(in, out);
    r.processLine("p: y = x^2");
    auto resp = r.processLine("q: p");
    REQUIRE(resp.find("q:")  == 0);
    REQUIRE(resp.find("y")   != std::string::npos);
    REQUIRE(resp.find("x^2") != std::string::npos);
    auto qShow = r.processLine("q");
    REQUIRE(qShow.find("y")   != std::string::npos);
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
    REQUIRE(s3.find("-(") != std::string::npos);
    REQUIRE(s3.find("2")  != std::string::npos);

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
    REQUIRE(resp.find("PI")           != std::string::npos);
    REQUIRE(resp.find("3.14")         != std::string::npos);
    REQUIRE(resp.find("circumference") != std::string::npos);
}

TEST_CASE("repl: new constants work in expressions") {
    REQUIRE(evalToString("tau").find("6.28") == 0);
    REQUIRE(evalToString("phi").find("1.61") == 0);
    REQUIRE(evalToString("q_e").find("1.6")  == 0);
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
    REQUIRE_EQ(evalToString("a*a"),     std::string("a^2"));
    REQUIRE_EQ(evalToString("a*a*a"),   std::string("a^3"));
    REQUIRE_EQ(evalToString("a/a/a"),   std::string("1/a"));

    auto s = evalToString("a*a*b");
    REQUIRE(s.find("a^2") != std::string::npos);
    REQUIRE(s.find("b")   != std::string::npos);
}

TEST_CASE("simplifier: nested powers collapse") {
    REQUIRE_EQ(evalToString("(x^2)^2"),   std::string("x^4"));
    REQUIRE_EQ(evalToString("(x^3)^4"),   std::string("x^12"));
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
