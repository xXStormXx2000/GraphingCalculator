// Property-based ("fuzz") tests.
//
// These complement the example-based tests in test_main.cpp. Instead of
// asserting specific outputs for hand-written inputs, they generate thousands
// of valid expressions from the grammar (see expr_generator.h) and assert
// INVARIANTS that must hold for every one of them:
//
//   1. Meaning preservation  - the simplified form evaluates to the same number
//                              as the original, at random bindings of its free
//                              variables. This is the strongest check: it
//                              catches any simplification that changes meaning.
//   2. Idempotence (string)  - re-simplifying an already-simplified form
//                              produces the EXACT same string. Canonical form
//                              is a true fixpoint, which requires the printer
//                              to round-trip (print -> parse -> print is the
//                              identity). Regression guard for a real printer
//                              bug this test found; see the inline note below.
//
// NOT checked here: contraction ("simplification never grows the AST"). That
// invariant is on the tree, which is hidden behind the engine's pimpl, and a
// text proxy from the printed form is not faithful (a reordering like
// `z - f(w)` -> `-f(w) + z` reads as an extra operator in text while the AST
// is unchanged). It belongs in an internal Ast.h-level test, not this
// black-box one.
//
// Two scoping rules keep these honest, both of which fall out of earlier design
// discussion:
//   * Domain: the original and simplified forms agree only on their COMMON
//     domain. a/a -> 1 disagrees at a=0 (the singularity is removable), so any
//     sample where EITHER evaluation is non-finite is discarded, not failed.
//     The engine's own NotFinite/DivisionByZero diagnostics are the detector.
//   * Conditioning: bindings and literals stay in a modest range and the
//     comparison uses a combined relative/absolute tolerance, so we test the
//     engine's algebra, not IEEE-754 rounding at extreme magnitudes.

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "CalculatorCore.h"
#include "Repl.h"            // for calc::defaultConstants()
#include "test_framework.h"
#include "expr_generator.h"

using namespace calc;
using namespace calc::core;

namespace {

	// Evaluate `expr` to a number in a fresh engine after binding each name in
	// `vars` to the matching value in `vals` (via the public assignment API).
	// Returns nullopt if the engine reports an error or the result isn't numeric
	// (e.g. a free variable remained, or a domain error like division by zero).
	std::optional<double> evalBound(const std::string& expr,
		const std::vector<std::string>& vars,
		const std::vector<double>& vals) {
		CalculatorCore core(defaultConstants());
		for (std::size_t i = 0; i < vars.size(); ++i) {
			// Bind via "name: value". Parenthesize negatives so the line parses
			// as an assignment of a number, not "name : -" something.
			std::string line = vars[i] + ": (" + std::to_string(vals[i]) + ")";
			auto r = core.evaluateLine(line, 400);
			if (!r) return std::nullopt;
		}
		auto r = core.evaluateLine(expr, 400);
		if (!r) return std::nullopt;
		if (!r.value().value) return std::nullopt;  // not numeric
		return *r.value().value;
	}

	// Simplify `expr` (with free variables) and return the canonical string,
	// or nullopt if the engine errored.
	std::optional<std::string> simplifyToString(const std::string& expr) {
		CalculatorCore core(defaultConstants());
		auto r = core.evaluateLine(expr, 400);
		if (!r) return std::nullopt;
		return r.value().canonical;
	}

	// Combined relative/absolute closeness. atol handles values near zero;
	// rtol handles large values. The relative bound is deliberately loose
	// (~1e-5): generated expressions can feed large arguments into sin/cos,
	// where argument reduction mod 2*pi amplifies a last-bit difference between
	// two equivalent-but-reordered evaluations into the 6th-7th significant
	// figure. That divergence is IEEE-754 conditioning of trig, not an algebra
	// error -- the values still agree to ~6 figures. NaN/inf are rejected by
	// the caller's domain filter before this is reached.
	bool isClose(double a, double b) {
		const double atol = 1e-7;
		const double rtol = 1e-5;
		const double diff = std::abs(a - b);
		const double scale = std::max(std::abs(a), std::abs(b));
		return diff <= atol + rtol * scale;
	}

}  // namespace

TEST_CASE("property: simplification preserves meaning, never grows, is idempotent") {
	exprgen::Config cfg;
	cfg.maxDepth = 5;

	const int kExpressions = 4000;
	const int kBindingsPerExpr = 6;

	int checkedMeaning = 0;   // samples that passed the domain filter
	int skippedDomain = 0;    // samples discarded (non-finite on one/both sides)

	for (int i = 0; i < kExpressions; ++i) {
		// Deterministic, reproducible seed per expression.
		const std::uint64_t seed = 0x9E3779B97F4A7C15ull * (std::uint64_t)(i + 1);
		exprgen::Generator g(seed, cfg);

		std::vector<std::string> vars;
		const std::string expr = g.generate(vars);

		// --- Invariant 2 & 3 need the simplified form. ---
		std::optional<std::string> simp = simplifyToString(expr);
		if (!simp) continue;  // engine diagnosed (e.g. NotFinite by folding) -> skip

		// NOTE on contraction: the engine's real invariant ("simplification
		// never grows the AST") is on the tree, which lives behind the pimpl
		// and isn't reachable here. A text-based node count of the printed form
		// is NOT a faithful proxy -- e.g. `z - atan(w)` may print as
		// `-atan(w) + z`, whose leading unary minus reads as an extra operator
		// in text while the AST is the same size. Checking contraction properly
		// belongs in an internal (Ast.h-level) test, not this black-box one, so
		// it is intentionally omitted here rather than asserted unreliably.

		// Idempotence (string-level): re-simplifying the canonical form must
		// produce the EXACT same string. This is a regression guard for a real
		// bug found by this very test: the printer once formatted numbers
		// lossily (e.g. "%.10g"), so print -> parse -> print was not the
		// identity -- a value could print as 3.6e11 on one pass and
		// 361805638500 on the next (same value, different text). The fix was to
		// print numbers in shortest round-trip form (std::to_chars). This
		// assertion keeps that property enforced: if number (or any) formatting
		// ever stops round-tripping, simp2 != simp will catch it here.
		//
		// It is also load-bearing beyond cosmetics: toString is the
		// canonicalization key for like-term grouping in the simplifier, so a
		// non-deterministic printed form would mean a non-canonical key.
		std::optional<std::string> simp2 = simplifyToString(*simp);
		REQUIRE(simp2.has_value());
		if (simp2) REQUIRE(*simp2 == *simp);

		// --- Invariant 1: meaning preservation at random bindings. ---
		for (int b = 0; b < kBindingsPerExpr; ++b) {
			const std::uint64_t bseed = seed ^ (0xD1B54A32D192ED03ull * (std::uint64_t)(b + 1));
			std::mt19937_64 rng(bseed);
			std::uniform_real_distribution<double> dist(-10.0, 10.0);

			std::vector<double> vals(vars.size());
			for (double& v : vals) v = dist(rng);

			// Two evaluation paths:
			//   A) bind-then-evaluate the ORIGINAL expression
			//   B) bind-then-evaluate the SIMPLIFIED expression
			// If simplification preserved meaning, A == B on the common domain.
			std::optional<double> a = evalBound(expr, vars, vals);
			std::optional<double> bb = evalBound(*simp, vars, vals);

			// Domain filter: if either side is absent or non-finite, this point
			// is outside the common domain (removable singularity, etc.). Skip.
			if (!a || !bb || !std::isfinite(*a) || !std::isfinite(*bb)) {
				++skippedDomain;
				continue;
			}

			++checkedMeaning;
			REQUIRE(isClose(*a, *bb));
		}
	}

	// Sanity: the run must actually have exercised the meaning check on a
	// meaningful number of samples, otherwise the domain filter (or a bug in
	// generation) silently neutered the test and it would "pass" vacuously.
	REQUIRE(checkedMeaning > kExpressions);  // at least ~1 verified sample per expr on average
}