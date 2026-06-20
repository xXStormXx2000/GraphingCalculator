#include "test_support.h"

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
	constexpr int N = 75;
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
