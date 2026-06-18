// Grammar-based expression generator for property ("fuzz") testing.
//
// Raw-byte fuzzing is a poor fit for this engine: almost every random string
// dies in the tokenizer, so the parser, simplifier, and VM are never reached.
// Restricting the alphabet only moves the wall to the parser. The technique
// that actually exercises the engine is to GENERATE FROM THE GRAMMAR rather
// than filter toward it: build a valid expression tree directly and render it
// to a string, so 100% of generated inputs parse and land in the machinery we
// want to stress.
//
// This header produces expression *strings* (not AST nodes) on purpose: it
// drives the engine through the same public seam the rest of the suite uses
// (CalculatorCore::evaluateLine), so it also exercises the tokenizer, parser,
// and printer, and never reaches into engine internals.
//
// The generator is deterministic given a seed, so a failing case is
// reproducible: print the seed, re-run, get the same expression.

#ifndef CALC_EXPR_GENERATOR_H
#define CALC_EXPR_GENERATOR_H

#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace exprgen {

	// Which leaf variable names the generator may emit. Kept to single letters
	// the engine treats as free variables. NOTE: deliberately excludes letters
	// the console frontend's defaultConstants() reserves as constants
	// (c, e, h, G, R, k, q, N, ...), so generated "variables" are always free.
	inline const std::vector<std::string>& variableNames() {
		static const std::vector<std::string> v = { "a", "b", "x", "y", "z", "w" };
		return v;
	}

	struct Config {
		int maxDepth = 5;       // recursion bound on the generated tree
		bool allowPow = true;   // include '^' (kept to small integer exponents)
		bool allowFunctions = true;
		// Numeric literals are drawn from a modest range so the float math
		// stays well-conditioned: extreme magnitudes are a property of IEEE
		// 754, not of this engine, and would test the wrong thing.
		double literalMin = -20.0;
		double literalMax = 20.0;
	};

	class Generator {
	public:
		explicit Generator(std::uint64_t seed, Config cfg = {})
			: m_rng(seed), m_cfg(cfg) {}

		// Generate one expression string and, as a side effect, record which
		// variable names it used (so the caller can bind exactly those).
		std::string generate(std::vector<std::string>& usedVarsOut) {
			m_used.clear();
			std::string s = gen(m_cfg.maxDepth);
			usedVarsOut.assign(m_used.begin(), m_used.end());
			return s;
		}

	private:
		std::string gen(int depth) {
			// At depth 0 we must emit a leaf (number or variable).
			if (depth <= 0) return leaf();

			// Otherwise pick among leaf / binary / unary / function.
			const int roll = uniformInt(0, 9);
			if (roll < 3) return leaf();                       // ~30% leaves
			if (roll < 8) return binary(depth);                // ~50% binary ops
			if (roll < 9 || !m_cfg.allowFunctions)             // ~10% unary minus
				return "(-" + gen(depth - 1) + ")";
			return function(depth);                            // ~10% function call
		}

		std::string leaf() {
			if (uniformInt(0, 1) == 0) return number();
			return variable();
		}

		std::string number() {
			std::uniform_real_distribution<double> d(m_cfg.literalMin, m_cfg.literalMax);
			double v = d(m_rng);
			// Render with enough precision to round-trip through the parser.
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.6g", v);
			std::string s(buf);
			// Wrap negatives so they compose without forming "--" or "*-"
			// ambiguities at call sites (the parser accepts unary minus, but
			// parenthesizing keeps the generated text unambiguous).
			if (!s.empty() && s[0] == '-') return "(" + s + ")";
			return s;
		}

		std::string variable() {
			const auto& names = variableNames();
			const std::string& n = names[uniformInt(0, (int)names.size() - 1)];
			m_used.insert(n);
			return n;
		}

		std::string binary(int depth) {
			// Decide power vs. a 4-way binary op as two clear choices.
			if (m_cfg.allowPow && uniformInt(0, 4) == 0) {
				// ~1 in 5 binary nodes is a power; small non-negative integer
				// exponent keeps results finite and in the integer-power rule.
				std::string base = "(" + gen(depth - 1) + ")";
				int e = uniformInt(0, 3);
				return "(" + base + "^" + std::to_string(e) + ")";
			}
			static const char* ops[] = { "+", "-", "*", "/" };
			const char* op = ops[uniformInt(0, 3)];
			std::string l = gen(depth - 1);
			std::string r = gen(depth - 1);
			return "(" + l + op + r + ")";
		}

		std::string function(int depth) {
			// Only total functions over the reals, so a random argument can't
			// land outside the domain: sin/cos/atan accept all reals, and abs
			// is total. (sqrt/log/asin/acos are domain-restricted and would
			// produce non-finite results we'd have to filter, so they're left
			// out of generation — the numeric-agreement filter still handles
			// any that slip in via folding.)
			static const char* fns[] = { "sin", "cos", "atan", "abs" };
			const std::string fn = fns[uniformInt(0, 3)];
			return fn + "(" + gen(depth - 1) + ")";
		}

		int uniformInt(int lo, int hi) {
			std::uniform_int_distribution<int> d(lo, hi);
			return d(m_rng);
		}

		std::mt19937_64 m_rng;
		Config m_cfg;
		std::set<std::string> m_used;
	};

}  // namespace exprgen

#endif