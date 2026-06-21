#include "CalculatorCore.h"
#include "Ast.h"
#include "Tokenizer.h"
#include "Parser.h"
#include "Simplifier.h"
#include "Builtins.h"
#include "Printer.h"
#include "VMbytecode.h"
#include "Bytecode.h"

#include <unordered_set>
#include <unordered_map>
#include <string>
#include <charconv>
#include <stdexcept>
#include <cmath>
namespace calc::core {

	struct PlotFunctor::Impl {
		const Program m_program;
	};

	double PlotFunctor::operator()(const std::vector<double>& coords,
		std::vector<double>& scratch) const {
		return execute(m_impl->m_program.code, coords, scratch);
	}
	double PlotFunctor::operator()(const std::vector<double>& coords) const {
		std::vector<double> scratch;
		scratch.reserve(requiredStackSize());
		return execute(m_impl->m_program.code, coords, scratch);
	}
	std::size_t PlotFunctor::dimensions() const {
		return m_impl->m_program.dimensions;
	}
	std::size_t PlotFunctor::requiredStackSize() const {
		return m_impl->m_program.stackSize;
	}

	struct CalculatorCore::Impl {
		struct ASTVar {
			AstPtr ast;
			std::size_t size = 0;
		};
		ConstantTable m_constants;
		std::unordered_map<std::string, ASTVar> m_vars;
		AstPtr substituteVariables(const AstPtr& node, std::size_t& size, std::size_t maxSize, bool& tooBig) const;
		AstPtr substituteVariablesImpl(const AstPtr& node,
			std::unordered_set<std::string>& expanding,
			bool inSubExpression, std::size_t& size, std::size_t maxSize, bool& tooBig) const;
	};

	AstPtr CalculatorCore::Impl::substituteVariables(const AstPtr& node, std::size_t& size, std::size_t maxSize, bool& tooBig) const {
		std::unordered_set<std::string> expanding;
		tooBig = false;
		return substituteVariablesImpl(node, expanding, /*inSubExpression=*/false, size, maxSize, tooBig);
	}

	AstPtr CalculatorCore::Impl::substituteVariablesImpl(const AstPtr& node,
		std::unordered_set<std::string>& expanding,
		bool inSubExpression, std::size_t& size, std::size_t maxSize, bool& tooBig) const {
		if (tooBig) return node;  // short-circuit if we've already blown the size budget
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
									  tooBig = true;
									  return node;
								  }
								  AstPtr result = substituteVariablesImpl(it->second.ast, expanding, inSubExpression, size, maxSize, tooBig);
								  expanding.erase(v.name);
								  return result;
							  },
			// Every recursive call below passes `true` for inSubExpression
			// because we're recursing into a child position, which is by
			// definition inside an expression.
			[&](const UnaryNode& u) -> AstPtr {
				return makeUnary(u.op,
								 substituteVariablesImpl(u.operand, expanding, true, size, maxSize, tooBig),
								 node->span);
			},
			[&](const BinaryNode& b) -> AstPtr {
				return makeBinary(b.op,
								  substituteVariablesImpl(b.lhs, expanding, true, size, maxSize, tooBig),
								  substituteVariablesImpl(b.rhs, expanding, true, size, maxSize, tooBig),
								  node->span);
			},
			[&](const CallNode& c) -> AstPtr {
				std::vector<AstPtr> args;
				args.reserve(c.args.size());
				for (const AstPtr& a : c.args) {
					args.push_back(substituteVariablesImpl(a, expanding, true, size, maxSize, tooBig));
				}
				return makeCall(c.name, std::move(args), node->span);
			},
			[&](const EquationNode& e) -> AstPtr {
				return makeEquation(substituteVariablesImpl(e.lhs, expanding, true, size, maxSize, tooBig),
									substituteVariablesImpl(e.rhs, expanding, true, size, maxSize, tooBig),
									node->span);
			},
			}, node->value);
	}

	CalculatorCore::CalculatorCore() {
		m_impl = std::make_unique<Impl>();
	}

	CalculatorCore::CalculatorCore(ConstantTable constants) {
		m_impl = std::make_unique<Impl>();
		m_impl->m_constants = std::move(constants);
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
		bool tooBig = false;
		AstPtr substituted = m_impl->substituteVariables(p.expr, size, maxSize, tooBig);
		if (tooBig) {
			return Diagnostic{ DiagCode::ExpressionTooLong, {tokens.front().span.begin, tokens.back().span.end}, std::to_string(maxSize) };
		}
		Result<AstPtr> simplifiedR = simplify(m_impl->m_constants, substituted);
		if (!simplifiedR) {
			return simplifiedR.error();
		}

		AstPtr simplified = simplifiedR.value();

		EvalResult evalResult;
		if (const auto* n = std::get_if<NumberNode>(&simplified->value)) {
			evalResult.value = n->value;
		}
		if (p.assignTo != "") {
			// Reject attempts to shadow a constant. Constant names are reserved:
			// resolution always substitutes the constant's value, so a stored
			// definition under the same name could never be reached and would
			// silently never apply.
			if (m_impl->m_constants.find(p.assignTo) != m_impl->m_constants.end()) {
				return Diagnostic{ DiagCode::ConstantReassignment, p.expr->span, p.assignTo };
			}
			// Reject self-referential definitions.
			std::unordered_set<std::string> freeVars;
			collectVariables(*simplified, m_impl->m_constants, freeVars);
			if (freeVars.find(p.assignTo) != freeVars.end()) {
				return Diagnostic{ DiagCode::SelfReference, p.expr->span, p.assignTo };
			}
			m_impl->m_vars[p.assignTo] = { simplified, countNodes(*simplified) };
			evalResult.assignedName = p.assignTo;
		}
		evalResult.canonical = toString(*simplified);
		return evalResult;
	}
	Result<Program> CalculatorCore::compileProgram(const CalculatorCore::PlotRequest& req) const {
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

		// Optionally multiply both sides through by every denominator so vertical
		// asymptotes don't show up as spurious sign-change pixels. Gated by
		// req.clearDenominators: callers doing sparse sign-change sampling need it;
		// callers doing dense direct evaluation want the raw L - R and leave it off
		// (the default). See clearDenominators in Simplifier.h for the rationale and
		// edge cases, and the PlotRequest doc in CalculatorCore.h for why the two
		// forms aren't interchangeable.
		//
		// `equation` is the tree we actually compile, so every check below MUST run
		// against `equation`, not the stored it->second.ast — otherwise validation
		// inspects a different tree than the one we emit bytecode for.
		AstPtr equation = it->second.ast;
		if (req.clearDenominators) {
			Result<AstPtr> equationR = clearDenominators(m_impl->m_constants, it->second.ast);
			if (!equationR) return std::move(equationR).error();
			equation = std::move(equationR).value();
		}

		// Every free variable must be one of the axes, and at least one axis must
		// actually appear in the equation. Checked against `equation` (the compiled
		// tree), so the validation and the emitted bytecode always agree.
		std::unordered_set<std::string> vars;
		collectVariables(*equation, m_impl->m_constants, vars);
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

		Chunk code;
		std::size_t depth = ASTtoBytecode(equation, code, axisSlots);
		Program program = {
			std::move(code),
			depth,
			req.axisNames.size()
		};
		return program;
	}
	Result<PlotFunctor> CalculatorCore::compilePlot(const CalculatorCore::PlotRequest& req) const {
		Result<Program> programR = compileProgram(req);
		if (!programR) return std::move(programR).error();
		Program program = std::move(programR).value();   // move, don't copy the Chunk out of the Result
		PlotFunctor f;
		f.m_impl = std::make_shared<const PlotFunctor::Impl>(PlotFunctor::Impl{
			std::move(program)
			});
		return f;
	}

	namespace {

		// Format a double as a GLSL float literal: shortest round-trip (like the
		// printer's formatNumber) but ALWAYS in float syntax. GLSL types a bare
		// "2" as an int, so "pow(x, 2)" is a type error -- every literal needs a
		// '.' or exponent. Guarantees one.
		std::string glslFloat(double v) {
			// GLSL has no NaN/inf literals; emit expressions that evaluate to
			// them so the output is always valid source. (A compiled, simplified
			// equation shouldn't contain these, but be total rather than emit
			// something that won't compile.)
			if (std::isnan(v)) return "(0.0/0.0)";
			if (std::isinf(v)) return v < 0 ? "(-1.0/0.0)" : "(1.0/0.0)";

			char buf[64];
			const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf), v);
			std::string s(buf, r.ptr);
			if (s.find('.') == std::string::npos &&
				s.find('e') == std::string::npos &&
				s.find('E') == std::string::npos) {
				s += ".0";
			}
			return s;
		}

		// Walk an RPN bytecode program, emitting a fully-parenthesized GLSL infix
		// expression. The string analogue of the VM: pop operand strings, wrap,
		// push. Full parenthesization means precedence never needs tracking.
		// `axes[i]` is the caller-supplied GLSL accessor for axis slot i.
		std::string glslFromBytecode(const Chunk& code,
			const std::vector<std::string>& axes) {
			std::vector<std::string> stack;
			stack.reserve(code.size());
			auto pop = [&] { std::string s = std::move(stack.back()); stack.pop_back(); return s; };

			for (const Bytecode& in : code) {
				switch (in.op) {
				case VMop::Push: stack.push_back(glslFloat(in.value)); break;
				case VMop::Bind: stack.push_back(axes[in.binding]); break;

				case VMop::Add: { std::string b = pop(), a = pop(); stack.push_back("(" + a + "+" + b + ")"); break; }
				case VMop::Sub: { std::string b = pop(), a = pop(); stack.push_back("(" + a + "-" + b + ")"); break; }
				case VMop::Mul: { std::string b = pop(), a = pop(); stack.push_back("(" + a + "*" + b + ")"); break; }
				case VMop::Div: { std::string b = pop(), a = pop(); stack.push_back("(" + a + "/" + b + ")"); break; }
				case VMop::Pow: { std::string b = pop(), a = pop(); stack.push_back("pow(" + a + "," + b + ")"); break; }

				case VMop::Uminus: { std::string a = pop(); stack.push_back("(-" + a + ")"); break; }

				case VMop::Sin: { std::string a = pop(); stack.push_back("sin(" + a + ")"); break; }
				case VMop::Cos: { std::string a = pop(); stack.push_back("cos(" + a + ")"); break; }
				case VMop::Tan: { std::string a = pop(); stack.push_back("tan(" + a + ")"); break; }
				case VMop::Asin: { std::string a = pop(); stack.push_back("asin(" + a + ")"); break; }
				case VMop::Acos: { std::string a = pop(); stack.push_back("acos(" + a + ")"); break; }
				case VMop::Atan: { std::string a = pop(); stack.push_back("atan(" + a + ")"); break; }
				case VMop::Sinh: { std::string a = pop(); stack.push_back("sinh(" + a + ")"); break; }
				case VMop::Cosh: { std::string a = pop(); stack.push_back("cosh(" + a + ")"); break; }
				case VMop::Tanh: { std::string a = pop(); stack.push_back("tanh(" + a + ")"); break; }
				case VMop::Asinh: { std::string a = pop(); stack.push_back("asinh(" + a + ")"); break; }
				case VMop::Acosh: { std::string a = pop(); stack.push_back("acosh(" + a + ")"); break; }
				case VMop::Atanh: { std::string a = pop(); stack.push_back("atanh(" + a + ")"); break; }
				case VMop::Abs: { std::string a = pop(); stack.push_back("abs(" + a + ")"); break; }
				case VMop::Sqrt: { std::string a = pop(); stack.push_back("sqrt(" + a + ")"); break; }

							   // GLSL has no two-arg log: log(base, x) = log(x) / log(base).
				case VMop::Log: {
					std::string b = pop(), a = pop();
					stack.push_back("(log(" + b + ")/log(" + a + "))"); break;
				}
							  // GLSL has no root: root(n, x) = pow(x, 1.0 / n).
				case VMop::Root: {
					std::string b = pop(), a = pop();
					stack.push_back("pow(" + b + ",(1.0/" + a + "))"); break;
				}

				default: break;  // Invalid/unknown: skip. Well-formed bytecode never hits this.
				}
			}
			return stack.empty() ? std::string("0.0") : std::move(stack.back());
		}

	}  // namespace

	Result<std::string> CalculatorCore::compileGLSL(
		const CalculatorCore::PlotRequest& req,
		const std::vector<std::string>& axisIdentifiers) const {
		// Precondition, not user input: the caller must supply one GLSL accessor
		// per axis. Violating it is a programming error in the consumer, so we
		// throw rather than return a DiagCode (which is reserved for end-user
		// input errors). The C ABI catches this and reports it as the -1
		// programming-error sentinel; a C++ caller gets the exception.
		if (axisIdentifiers.size() != req.axisNames.size()) {
			throw std::invalid_argument(
				"compileGLSL: axisIdentifiers count (" +
				std::to_string(axisIdentifiers.size()) +
				") must equal axisNames count (" +
				std::to_string(req.axisNames.size()) + ")");
		}
		// User-input errors (undefined equation, non-axis variable, ...) flow
		// back as ordinary Result diagnostics from compileProgram.
		Result<Program> programR = compileProgram(req);
		if (!programR) return std::move(programR).error();
		return glslFromBytecode(programR.value().code, axisIdentifiers);
	}

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