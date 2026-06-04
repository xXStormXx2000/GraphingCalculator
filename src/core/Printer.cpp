#include "Printer.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace calc::core {

	namespace {

		// Precedence of an expression for printing purposes.
		// Higher number = binds more tightly.
		//  100   atoms (numbers, variables, function calls, parenthesized things)
		//   10   sum / unary minus printed as "-x"
		//   20   product
		//   30   power
		//   -1   equation (only at top level)
		int precedenceOf(const AstNode& node) {
			return std::visit(overloaded{
										 [](const NumberNode&) { return 100; },
										 [](const VariableNode&) { return 100; },
										 [](const CallNode&) { return 100; },
										 [](const UnaryNode&) { return 10; },
										 [](const BinaryNode& b) {
											 switch (b.op) {
											 case BinaryOp::Add: case BinaryOp::Sub: return 10;
											 case BinaryOp::Mul: case BinaryOp::Div: return 20;
											 case BinaryOp::Pow: return 30;
											 }
											 return 100;
										 },
										 [](const EquationNode&) { return -1; },
				}, node.value);
		}

		std::string render(const AstNode& node);

		// Returns true if `node` is a NumberNode with a negative value. These
		// need special handling as the base of ^: -2^a would be parsed as
		// -(2^a) rather than (-2)^a, so they must be wrapped in parens.
		bool isNegativeNumber(const AstNode& node) {
			const NumberNode* n = std::get_if<NumberNode>(&node.value);
			return n && n->value < 0.0;
		}

		// Print a child, wrapping in parens if its precedence is lower than the
		// parent's (or equal-and-on-the-right-of-non-commutative-op).
		// `forceParens` is set when the child is the base of ^ and happens to
		// be a negative number literal.
		std::string renderChild(const AstNode& child, int parentPrec,
			bool needParenForEqualPrec, bool forceParens = false) {
			const int p = precedenceOf(child);
			const bool wrap = forceParens ||
				(p < parentPrec) ||
				(needParenForEqualPrec && p == parentPrec);
			if (wrap) {
				return "(" + render(child) + ")";
			}
			return render(child);
		}

		std::string render(const AstNode& node) {
			return std::visit(overloaded{
								  [](const NumberNode& n) -> std::string { return formatNumber(n.value); },
								  [](const VariableNode& v) -> std::string { return v.name; },
								  [](const UnaryNode& u) -> std::string {
					// -x : negation. Wrap operand if it'd otherwise look ambiguous.
					const int childPrec = precedenceOf(*u.operand);
					std::string inner = (childPrec <= 10)
											? "(" + render(*u.operand) + ")"
											: render(*u.operand);
					return "-" + inner;
				},
				[](const BinaryNode& b) -> std::string {
					const int prec = (b.op == BinaryOp::Pow) ? 30
									 : (b.op == BinaryOp::Mul || b.op == BinaryOp::Div) ? 20
																						: 10;
					// For non-commutative ops, the side that "loses" associativity
					// needs parens at equal precedence:
					//   - Left-assoc -, /: right side needs parens.
					//   - Right-assoc ^: left side needs parens.
					const bool rNeedsEq = (b.op == BinaryOp::Sub) || (b.op == BinaryOp::Div);
					const bool lNeedsEq = (b.op == BinaryOp::Pow);

					const std::string lhs = renderChild(*b.lhs, prec, lNeedsEq,
														lNeedsEq && isNegativeNumber(*b.lhs));
					const std::string rhs = renderChild(*b.rhs, prec, rNeedsEq);
					const char* opStr = "";
					switch (b.op) {
					case BinaryOp::Add: opStr = " + "; break;
					case BinaryOp::Sub: opStr = " - "; break;
					case BinaryOp::Mul: opStr = "*";   break;
					case BinaryOp::Div: opStr = "/";   break;
					case BinaryOp::Pow: opStr = "^";   break;
					}
					return lhs + opStr + rhs;
				},
				[](const CallNode& c) -> std::string {
					std::string out = c.name + "(";
					for (std::size_t i = 0; i < c.args.size(); ++i) {
						if (i) out += ", ";
						out += render(*c.args[i]);
					}
					out += ")";
					return out;
				},
				[](const EquationNode& e) -> std::string {
					const NumberNode* lhsNum = std::get_if<NumberNode>(&e.lhs->value);
					const NumberNode* rhsNum = std::get_if<NumberNode>(&e.rhs->value);
					if (lhsNum && rhsNum && (lhsNum->value != rhsNum->value))
						return render(*e.lhs) + " =/= " + render(*e.rhs);
					return render(*e.lhs) + " = " + render(*e.rhs);
				},
				}, node.value);
		}

	}  // namespace

	std::string formatNumber(double v) {
		if (std::isnan(v)) return "NaN";
		if (std::isinf(v)) return v < 0 ? "-inf" : "inf";

		// Integer fast path.
		if (v == std::floor(v) && std::abs(v) < 1e16) {
			std::ostringstream oss;
			oss.precision(0);
			oss << std::fixed << v;
			return oss.str();
		}

		// General case: use a reasonable precision then strip trailing zeros.
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.10g", v);
		return std::string(buf);
	}

	std::string toString(const AstNode& node) {
		return render(node);
	}

}  // namespace calc::core
