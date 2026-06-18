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

	// Virtual Machine Operation
	enum class VMop {
		Push,
		Bind,
		Add, Sub, Mul, Div, Exp, Uminus,
		Sin, Cos, Tan,
		Asin, Acos, Atan,
		Sinh, Cosh, Tanh,
		Asinh, Acosh, Atanh,
		Abs,
		Log,
		Sqrt, Root
	};

	struct FunctionDef {
		std::size_t arity = 0;
		std::function<double(const std::vector<double>&)> fn;
		VMop op = VMop::Push;
	};

	// Function table. Centralized so the parser, evaluator, and simplifier
	// can all consult it.
	inline const std::unordered_map<std::string, FunctionDef>& functions() {
		static const std::unordered_map<std::string, FunctionDef> table = {
																		   {"sin",  {1, [](const auto& a) { return std::sin(a[0]); }, VMop::Sin}},
																		   {"cos",  {1, [](const auto& a) { return std::cos(a[0]); }, VMop::Cos}},
																		   {"tan",  {1, [](const auto& a) { return std::tan(a[0]); }, VMop::Tan}},
																		   {"asin", {1, [](const auto& a) { return std::asin(a[0]); }, VMop::Asin}},
																		   {"acos", {1, [](const auto& a) { return std::acos(a[0]); }, VMop::Acos}},
																		   {"atan", {1, [](const auto& a) { return std::atan(a[0]); }, VMop::Atan}},
																		   {"sinh", {1, [](const auto& a) { return std::sinh(a[0]); }, VMop::Sinh}},
																		   {"cosh", {1, [](const auto& a) { return std::cosh(a[0]); }, VMop::Cosh}},
																		   {"tanh", {1, [](const auto& a) { return std::tanh(a[0]); }, VMop::Tanh}},
																		   {"asinh", {1, [](const auto& a) { return std::asinh(a[0]); }, VMop::Asinh}},
																		   {"acosh", {1, [](const auto& a) { return std::acosh(a[0]); }, VMop::Acosh}},
																		   {"atanh", {1, [](const auto& a) { return std::atanh(a[0]); }, VMop::Atanh}},
																		   {"sqrt", {1, [](const auto& a) { return std::sqrt(a[0]); }, VMop::Sqrt}},
																		   {"abs",  {1, [](const auto& a) { return std::abs(a[0]); }, VMop::Abs}},
																		   // log(base, x) = log_base(x).
																		   {"log",  {2, [](const auto& a) { return std::log(a[1]) / std::log(a[0]); }, VMop::Log}},
																		   // root(n, x) = x^(1/n).
																		   {"root", {2, [](const auto& a) { return std::pow(a[1], 1.0 / a[0]); }, VMop::Root}},
		};
		return table;
	}

	// A set of named constants the engine treats as reserved: each name folds
	// to its value during simplification and cannot be redefined by the user.
	// The engine ships with NONE built in -- which constants exist, and how
	// they are spelled, is a caller decision supplied at construction. (The
	// console frontend injects a physics/math set; see calc::defaultConstants
	// in the frontend.) This keeps the engine neutral on both questions.
	using ConstantTable = std::unordered_map<std::string, double>;

	// Collect all variable names referenced in an AST that are not constants
	// in `constants`. Used to find free variables in expressions and equations.
	void collectVariables(const AstNode& node,
		const ConstantTable& constants,
		std::unordered_set<std::string>& out);

	// Count the total number of nodes in an AST. Used to size a stored
	// definition by its *simplified* tree so the substitution budget charges
	// the real number of nodes a later expansion will splice in, not the
	// (usually larger) pre-simplification parse count.
	std::size_t countNodes(const AstNode& node);

}  // namespace calc::core

#endif