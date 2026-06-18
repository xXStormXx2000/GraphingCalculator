#ifndef CALC_ENGINE_H
#define CALC_ENGINE_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Types.h"   // SourceSpan, Diagnostic (key-based), Result<T>

namespace calc::core {

	// ---- A compiled, immutable, thread-safe evaluation artifact -------------
	//
	// Produced by CalculatorCore::compilePlot. Holds an immutable bytecode program
	// (shared, read-only). Safe to share across threads PROVIDED each
	// concurrent call supplies its own scratch stack — one stack per worker.
	//
	// The functor is dimension-agnostic: it evaluates lhs - rhs of the compiled
	// equation given a coordinate for each axis. `coords[i]` is the value of the
	// axis interned at slot i (the order of axisNames passed to compilePlot).
	class PlotFunctor {
	public:
		// Evaluate lhs - rhs of the compiled equation at the given coordinates.
		// `coords` must have one entry per axis, in the order the axes were
		// listed in the PlotRequest. `scratch` is the caller-owned value stack,
		// reused across calls. Returns NaN on any domain error (the grapher
		// treats NaN as "no crossing"), so there is no error path in the hot loop.
		double operator()(const std::vector<double>& coords,
			std::vector<double>& scratch) const;
		double operator()(const std::vector<double>& coords) const;

		// Number of axes this functor expects in each `coords` argument.
		std::size_t dimensions() const;

		// Size a scratch stack once, before the sampling loop:
		//   std::vector<double> s; s.reserve(f.requiredStackSize());
		std::size_t requiredStackSize() const;

	private:
		friend class CalculatorCore;
		struct Impl;
		std::shared_ptr<const Impl> m_impl;   // immutable program + depth bound
	};

	// ---- The calculation session -------------------------------------------
	//
	// Owns durable state: the user's variable definitions. The frontend talks
	// to the engine through this surface for define/list/clear/evaluate and
	// for building plot functors. No iostream, no text rendering, no language.
	class CalculatorCore {
	public:
		// The set of named constants this engine recognizes. Each name folds to
		// its value during simplification and is reserved (cannot be redefined).
		// The engine ships with none: the caller supplies the set, choosing both
		// which constants exist and how they are spelled. A bare-default-
		// constructed engine has no constants at all, so every identifier is a
		// free variable. (See calc::defaultConstants() in the frontend for the
		// console program's physics/math set.)
		using ConstantTable = std::unordered_map<std::string, double>;

		CalculatorCore();  // no constants
		explicit CalculatorCore(ConstantTable constants);
		~CalculatorCore();
		CalculatorCore(CalculatorCore&&) noexcept;
		CalculatorCore& operator=(CalculatorCore&&) noexcept;

		// Parse + simplify a line. If it was an assignment ("a: x^2"), the
		// definition is stored and `assignedName` is set. `canonical` always
		// holds the printed form of the result. `value` additionally holds the
		// numeric result when the expression reduced to a number (no free
		// variables remained). Errors come back as a Diagnostic.
		struct EvalResult {
			std::optional<std::string> assignedName;  // set if this was a definition
			std::optional<double>      value;         // set if it reduced to a number
			std::string                canonical;     // printed form (always set)
		};
		Result<EvalResult> evaluateLine(std::string_view input, std::size_t maxSize = 400);

		// Variable session management (the durable state the engine owns).
		std::vector<std::string> definedNames() const;
		std::optional<std::string> definitionOf(const std::string& name) const;
		void clear();

		// Build a thread-safe plotting functor for a stored equation. Does the
		// eager work ONCE: substitute variables, simplify, optionally clear
		// denominators, compile to bytecode, intern each axis name to a slot
		// (axisNames[i] -> slot i). After this returns, mutating the
		// CalculatorCore cannot affect the functor.
		//
		// `axisNames` may hold any number of axes (2 for a plane curve, 3 for a
		// surface, ...). They must be distinct, and every free variable in the
		// equation must be one of them. Fails (Diagnostic) at build time, not
		// sample time, on an undefined non-axis variable, a non-equation, etc.
		//
		// The functor evaluates lhs - rhs. `clearDenominators` picks the form:
		//   false (default): raw L - R. The true level set, but non-finite at
		//     poles. For dense direct evaluation that handles inf/NaN itself
		//     (e.g. shading an implicit surface on the GPU).
		//   true: multiplied through by every denominator, so it's finite
		//     everywhere and asymptotes aren't spurious zero-crossings. Needed
		//     for sparse sign-change sampling (the ASCII grapher). Changes
		//     magnitude near poles, so it's not a drop-in for the raw form. See
		//     clearDenominators in Simplifier.h for edge cases (e.g. sin(x)/x).
		struct PlotRequest {
			std::string              equationName;        // a stored variable holding an equation
			std::vector<std::string> axisNames;           // axisNames[i] -> slot i
			bool                     clearDenominators = false;  // see above; default raw L - R
		};
		Result<PlotFunctor> compilePlot(const PlotRequest& req) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;   // pimpl: frontend never sees Ast.h guts
	};

	// ---- Command parser ---------------------------------------------------
	//
	// Parses a slash command of the form "/name(arg1, arg2, ...)"
	// into a name and a list of trimmed argument strings.
	// Arguments may contain any expression text including whitespace.
	// Returns a Diagnostic if the input is not a valid command syntax.
	struct ParsedCommand {
		std::string              name;
		std::vector<std::string> args;  // trimmed of leading/trailing whitespace
	};

	Result<ParsedCommand> parseCommand(std::string_view input);

}  // namespace calc::core

#endif