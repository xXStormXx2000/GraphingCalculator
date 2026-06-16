#include "CalculatorCore.h"
#include "Ast.h"
#include "Tokenizer.h"
#include "Parser.h"
#include "Simplifier.h"
#include "Builtins.h"
#include "Printer.h"
#include "VMbytecode.h"

#include <unordered_set>
#include <unordered_map>
#include <string>
namespace calc::core {

	struct PlotFunctor::Impl {
		const std::vector<Bytecode> m_code;
		const std::size_t m_stackSize;
		const std::size_t m_dimensions;
	};

	double PlotFunctor::operator()(const std::vector<double>& coords,
		std::vector<double>& scratch) const {
		return execute(m_impl->m_code, coords, scratch);
	}
	double PlotFunctor::operator()(const std::vector<double>& coords) const {
		std::vector<double> scratch;
		scratch.reserve(requiredStackSize());
		return execute(m_impl->m_code, coords, scratch);
	}
	std::size_t PlotFunctor::dimensions() const {
		return m_impl->m_dimensions;
	}
	std::size_t PlotFunctor::requiredStackSize() const {
		return m_impl->m_stackSize;
	}

	struct CalculatorCore::Impl {
		struct ASTVar {
			AstPtr ast;
			std::size_t size = 0;
		};
		std::unordered_map<std::string, ASTVar> m_vars;
		AstPtr substituteVariables(const AstPtr& node, std::size_t& size, std::size_t maxSize, bool& toBig) const;
		AstPtr substituteVariablesImpl(const AstPtr& node,
			std::unordered_set<std::string>& expanding,
			bool inSubExpression, std::size_t& size, std::size_t maxSize, bool& toBig) const;
	};

	AstPtr CalculatorCore::Impl::substituteVariables(const AstPtr& node, std::size_t& size, std::size_t maxSize, bool& toBig) const {
		std::unordered_set<std::string> expanding;
		toBig = false;
		return substituteVariablesImpl(node, expanding, /*inSubExpression=*/false, size, maxSize, toBig);
	}

	AstPtr CalculatorCore::Impl::substituteVariablesImpl(const AstPtr& node,
		std::unordered_set<std::string>& expanding,
		bool inSubExpression, std::size_t& size, std::size_t maxSize, bool& toBig) const {
		if (toBig) return node;  // short-circuit if we've already blown the size budget
		return std::visit(overloaded{
							  [&](const NumberNode&) -> AstPtr { return node; },
							  [&](const VariableNode& v) -> AstPtr {
								  const auto it = m_vars.find(v.name);
								  if (it == m_vars.end()) return node;  // unbound -> leave as variable
								  // Refuse to inline an equation (y = x^2) into a sub-expression
								  // context. Inlining there would produce nonsense like the
								  // chained equation "y = x^2 = x" or an attempt to add an
								  // equation to a number. At the top level (e.g. typing `p` at
								  // the prompt or `q: p` to copy an equation), inlining is fine.
								  if (inSubExpression &&
									  std::get_if<EquationNode>(&it->second.ast->value)) {
									  return node;
								  }
								  // Defensive guard against infinite recursion.
								  if (expanding.count(v.name)) return node;
								  expanding.insert(v.name);
								  // The replacement tree takes the place of this VariableNode,
								  // so it inherits the current sub-expression context.
								  size += it->second.size;
								  if (size > maxSize) {
									  toBig = true;
									  return node;
								  }
								  AstPtr result = substituteVariablesImpl(it->second.ast, expanding, inSubExpression, size, maxSize, toBig);
								  expanding.erase(v.name);
								  return result;
							  },
			// Every recursive call below passes `true` for inSubExpression
			// because we're recursing into a child position, which is by
			// definition inside an expression.
			[&](const UnaryNode& u) -> AstPtr {
				return makeUnary(u.op,
								 substituteVariablesImpl(u.operand, expanding, true, size, maxSize, toBig),
								 node->span);
			},
			[&](const BinaryNode& b) -> AstPtr {
				return makeBinary(b.op,
								  substituteVariablesImpl(b.lhs, expanding, true, size, maxSize, toBig),
								  substituteVariablesImpl(b.rhs, expanding, true, size, maxSize, toBig),
								  node->span);
			},
			[&](const CallNode& c) -> AstPtr {
				std::vector<AstPtr> args;
				args.reserve(c.args.size());
				for (const AstPtr& a : c.args) {
					args.push_back(substituteVariablesImpl(a, expanding, true, size, maxSize, toBig));
				}
				return makeCall(c.name, std::move(args), node->span);
			},
			[&](const EquationNode& e) -> AstPtr {
				return makeEquation(substituteVariablesImpl(e.lhs, expanding, true, size, maxSize, toBig),
									substituteVariablesImpl(e.rhs, expanding, true, size, maxSize, toBig),
									node->span);
			},
			}, node->value);
	}

	CalculatorCore::CalculatorCore() {
		m_impl = std::make_unique<Impl>();
	}

	CalculatorCore::~CalculatorCore() = default;
	CalculatorCore::CalculatorCore(CalculatorCore&&) noexcept = default;
	CalculatorCore& CalculatorCore::operator=(CalculatorCore&&) noexcept = default;

	std::vector<std::string> CalculatorCore::definedNames() const {
		std::vector<std::string> out;
		for (const auto& [name, var] : m_impl->m_vars) out.push_back(name);
		return out;
	}

	std::optional<std::string> CalculatorCore::definitionOf(const std::string& name) const {
		const auto it = m_impl->m_vars.find(name);
		if (it == m_impl->m_vars.end()) return {};
		return toString(*it->second.ast);
	}

	void CalculatorCore::clear() {
		m_impl->m_vars.clear();
	}

	Result<CalculatorCore::EvalResult> CalculatorCore::evaluateLine(std::string_view input, std::size_t maxSize)
	{
		Result<std::vector<Token>> tokensResult = tokenize(input);
		if (!tokensResult) {
			return tokensResult.error();
		}
		std::vector<Token> tokens = std::move(tokensResult).value();
		std::size_t size = 0;
		Result<ParsedExpression> parsed = parseExpression(tokens, maxSize, size);
		if (!parsed) {
			return parsed.error();
		}
		ParsedExpression p = std::move(parsed).value();
		bool toBig = false;
		AstPtr substituted = m_impl->substituteVariables(p.expr, size, maxSize, toBig);
		if (toBig) {
			return Diagnostic{ DiagCode::ExpressionTooLong, {tokens.front().span.begin, tokens.back().span.end}, std::to_string(maxSize) };
		}
		Result<AstPtr> simplifiedR = simplify(substituted);
		if (!simplifiedR) {
			return simplifiedR.error();
		}

		AstPtr simplified = simplifiedR.value();

		EvalResult evalResult;
		if (const auto* n = std::get_if<NumberNode>(&simplified->value)) {
			evalResult.value = n->value;
		}
		if (p.assignTo != "") {
			// Reject attempts to shadow a built-in constant. Constant names
			// are reserved: resolution always substitutes the constant's
			// value, so a stored definition under the same name could never
			// be reached and would silently never apply.
			if (constants().find(p.assignTo) != constants().end()) {
				return Diagnostic{ DiagCode::ConstantReassignment, p.expr->span, p.assignTo };
			}
			// Reject self-referential definitions.
			std::unordered_set<std::string> freeVars;
			collectVariables(*simplified, freeVars);
			if (freeVars.find(p.assignTo) != freeVars.end()) {
				return Diagnostic{ DiagCode::SelfReference, p.expr->span, p.assignTo };
			}
			m_impl->m_vars[p.assignTo] = { simplified, countNodes(*simplified) };
			evalResult.assignedName = p.assignTo;
		}
		evalResult.canonical = toString(*simplified);
		return evalResult;
	}

	Result<PlotFunctor> CalculatorCore::compilePlot(const CalculatorCore::PlotRequest& req) const {
		// Build the axis-name -> slot-index map, checking for duplicate axes.
		std::unordered_map<std::string, std::size_t> axisSlots;
		for (std::size_t i = 0; i < req.axisNames.size(); ++i) {
			const auto [pos, inserted] = axisSlots.emplace(req.axisNames[i], i);
			(void)pos;
			if (!inserted) {
				return Diagnostic{ DiagCode::AxisNamesMustDiffer, {} };
			}
		}

		const auto it = m_impl->m_vars.find(req.equationName);
		if (it == m_impl->m_vars.end()) {
			return Diagnostic{ DiagCode::NoSuchVariable, {}, req.equationName };
		}
		if (!std::get_if<EquationNode>(&it->second.ast->value)) {
			return Diagnostic{ DiagCode::GraphTargetNotEquation, {} };
		}

		// Multiply both sides through by every denominator so vertical
		// asymptotes don't show up as spurious sign-change pixels. See
		// clearDenominators in Simplifier.h for the rationale and edge cases.
		Result<AstPtr> equationR = clearDenominators(it->second.ast);
		if (!equationR) return std::move(equationR).error();
		AstPtr equation = equationR.value();

		// Every free variable must be one of the axes, and at least one axis
		// must actually appear in the equation.
		std::unordered_set<std::string> vars;
		collectVariables(*it->second.ast, vars);
		for (const std::string& v : vars) {
			if (axisSlots.find(v) == axisSlots.end()) {
				return Diagnostic{ DiagCode::NonAxisVariable, {}, v };
			}
		}
		bool anyAxisUsed = false;
		for (const std::string& axis : req.axisNames) {
			if (vars.find(axis) != vars.end()) { anyAxisUsed = true; break; }
		}
		if (!anyAxisUsed) {
			return Diagnostic{ DiagCode::NoAxisVariable, {} };
		}

		std::vector<Bytecode> code;
		std::size_t depth = ASTtoBytecode(equation, code, axisSlots);
		PlotFunctor f;
		f.m_impl = std::make_shared<const PlotFunctor::Impl>(PlotFunctor::Impl{
			std::move(code),
			depth,
			req.axisNames.size()
			});
		return f;
	}

}  // namespace calc::core

namespace calc::core {

	// Pure string manipulation — no AST types involved.
	Result<ParsedCommand> parseCommand(std::string_view input) {
		// Trim leading whitespace.
		std::size_t i = 0;
		while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) ++i;

		// Must start with '/'.
		if (i >= input.size() || input[i] != '/') {
			return Diagnostic{ DiagCode::CommandMustStartWithSlash, {i, i + 1} };
		}
		++i;

		// Read the command name: letters, digits, underscores.
		const std::size_t nameStart = i;
		while (i < input.size() &&
			(std::isalnum(static_cast<unsigned char>(input[i])) || input[i] == '_'))
			++i;
		if (i == nameStart) {
			return Diagnostic{ DiagCode::ExpectedCommandName, {nameStart, nameStart + 1} };
		}
		ParsedCommand cmd;
		cmd.name = std::string(input.substr(nameStart, i - nameStart));

		// Trim whitespace after name.
		while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) ++i;

		// No argument list — bare command like "/list".
		if (i >= input.size()) return cmd;

		if (input[i] != '(') {
			return Diagnostic{ DiagCode::ExpectedOpenParen, {i, i + 1} };
		}
		++i;  // consume '('

		// Split on commas at paren depth 1, respecting nested parens.
		int depth = 1;
		std::size_t argStart = i;
		while (i < input.size()) {
			const char c = input[i];
			if (c == '(') {
				++depth;
			}
			else if (c == ')') {
				--depth;
				if (depth == 0) {
					// Flush the last argument (may be empty for zero-arg commands).
					std::string_view raw = input.substr(argStart, i - argStart);
					// Trim leading whitespace.
					std::size_t s = 0;
					while (s < raw.size() &&
						std::isspace(static_cast<unsigned char>(raw[s]))) ++s;
					// Trim trailing whitespace.
					std::size_t e = raw.size();
					while (e > s &&
						std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
					// Only add non-empty arg; a bare "/cmd()" has no args.
					if (e > s) cmd.args.push_back(std::string(raw.substr(s, e - s)));
					++i;
					break;
				}
			}
			else if (c == ',' && depth == 1) {
				// Flush current argument.
				std::string_view raw = input.substr(argStart, i - argStart);
				std::size_t s = 0;
				while (s < raw.size() &&
					std::isspace(static_cast<unsigned char>(raw[s]))) ++s;
				std::size_t e = raw.size();
				while (e > s &&
					std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
				cmd.args.push_back(std::string(raw.substr(s, e - s)));
				argStart = i + 1;
			}
			++i;
		}

		if (depth != 0) {
			return Diagnostic{ DiagCode::UnmatchedOpenParen, {} };
		}

		// Anything after the closing ')' is an error.
		while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) ++i;
		if (i < input.size()) {
			return Diagnostic{ DiagCode::UnexpectedInputAfterCommand, {i, input.size()} };
		}

		return cmd;
	}

}  // namespace calc::core