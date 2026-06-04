#ifndef CALC_BUILTINS_H
#define CALC_BUILTINS_H

#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Ast.h"

namespace calc::core {

	struct FunctionDef {
		std::size_t arity = 0;
		std::function<double(const std::vector<double>&)> fn;
	};

	// Function table. Centralized so the parser, evaluator, and simplifier
	// can all consult it.
	inline const std::unordered_map<std::string, FunctionDef>& functions() {
		static const std::unordered_map<std::string, FunctionDef> table = {
																		   {"sin",  {1, [](const auto& a) { return std::sin(a[0]); }}},
																		   {"cos",  {1, [](const auto& a) { return std::cos(a[0]); }}},
																		   {"tan",  {1, [](const auto& a) { return std::tan(a[0]); }}},
																		   {"asin", {1, [](const auto& a) { return std::asin(a[0]); }}},
																		   {"acos", {1, [](const auto& a) { return std::acos(a[0]); }}},
																		   {"atan", {1, [](const auto& a) { return std::atan(a[0]); }}},
																		   {"sqrt", {1, [](const auto& a) { return std::sqrt(a[0]); }}},
																		   {"abs",  {1, [](const auto& a) { return std::abs(a[0]); }}},
																		   // log(base, x) = log_base(x).
																		   {"log",  {2, [](const auto& a) { return std::log(a[1]) / std::log(a[0]); }}},
																		   // root(n, x) = x^(1/n).
																		   {"root", {2, [](const auto& a) { return std::pow(a[1], 1.0 / a[0]); }}},
		};
		return table;
	}

	struct ConstantDef {
		double value = 0.0;
	};

	// Mathematical and physical constants. Values are CODATA 2018 / SI 2019
	// where applicable.
	inline const std::unordered_map<std::string, ConstantDef>& constants() {
		static const std::unordered_map<std::string, ConstantDef> table = {
																		   {"PI",   {3.141592653589793238462643383279502884}},
																		   {"tau",  {6.283185307179586476925286766559005768}},
																		   {"e",    {2.718281828459045235360287471352662498}},
																		   {"phi",  {1.618033988749894848204586834365638118}},
																		   {"G",    {6.67430e-11}},
																		   {"c",    {299792458.0}},
																		   {"h",    {6.62607015e-34}},
																		   {"hbar", {1.054571817e-34}},
																		   {"k_B",  {1.380649e-23}},
																		   {"N_A",  {6.02214076e23}},
																		   {"R",    {8.314462618}},
																		   {"q_e",  {1.602176634e-19}},
		};
		return table;
	}

	// Collect all variable names referenced in an AST that are not built-in
	// constants. Used to find free variables in expressions and equations.
	void collectVariables(const AstNode& node,
		std::unordered_set<std::string>& out);

}  // namespace calc::core

#endif
