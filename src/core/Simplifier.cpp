#include "Simplifier.h"

#include <cmath>
#include <map>
#include <vector>

#include "Builtins.h"
#include "Printer.h"

namespace calc::core {

	namespace {

		// --- Helpers --------------------------------------------------------------

		bool isNumber(const AstNode& n, double& out) {
			if (auto* p = std::get_if<NumberNode>(&n.value)) {
				out = p->value;
				return true;
			}
			return false;
		}

		// --- Flattening: collect all + / - terms recursively ---------------------
		enum class TermSign
		{
			Pos,
			Neg
		};
		struct SumTerm
		{
			TermSign sign;
			AstPtr expr;
		};

		enum class TermRole
		{
			Numer,
			Denom
		};
		struct ProductTerm
		{
			TermRole role;
			AstPtr expr;
		};

		void collectSum(const AstPtr& node, TermSign outerSign, std::vector<SumTerm>& out) {
			if (auto* b = std::get_if<BinaryNode>(&node->value)) {
				if (b->op == BinaryOp::Add) {
					collectSum(b->lhs, outerSign, out);
					collectSum(b->rhs, outerSign, out);
					return;
				}
				if (b->op == BinaryOp::Sub) {
					collectSum(b->lhs, outerSign, out);
					const TermSign flipped = (outerSign == TermSign::Pos) ? TermSign::Neg : TermSign::Pos;
					collectSum(b->rhs, flipped, out);
					return;
				}
			}
			if (auto* u = std::get_if<UnaryNode>(&node->value)) {
				if (u->op == UnaryOp::Negate) {
					const TermSign flipped = (outerSign == TermSign::Pos) ? TermSign::Neg : TermSign::Pos;
					collectSum(u->operand, flipped, out);
					return;
				}
			}
			out.push_back(SumTerm{ outerSign, node });
		}

		void collectProduct(const AstPtr& node, TermRole outerRole, std::vector<ProductTerm>& out) {
			if (auto* b = std::get_if<BinaryNode>(&node->value)) {
				if (b->op == BinaryOp::Mul) {
					collectProduct(b->lhs, outerRole, out);
					collectProduct(b->rhs, outerRole, out);
					return;
				}
				if (b->op == BinaryOp::Div) {
					collectProduct(b->lhs, outerRole, out);
					const TermRole flipped = (outerRole == TermRole::Numer) ? TermRole::Denom : TermRole::Numer;
					collectProduct(b->rhs, flipped, out);
					return;
				}
			}
			if (auto* u = std::get_if<UnaryNode>(&node->value)) {
				if (u->op == UnaryOp::Negate) {
					collectProduct(u->operand, outerRole, out);
					out.push_back(ProductTerm{ outerRole, makeNumber(-1.0, node->span) });
					return;
				}
			}
			out.push_back(ProductTerm{ outerRole, node });
		}

		// --- Simplification core --------------------------------------------------


		Result<AstPtr> simplifyImpl(const AstPtr& node);

		// After flattening a `+`/`-` chain into a list of signed terms, fold
		// numeric terms together and combine like terms by counting their net
		// occurrences. e.g.
		//   a + a + b - a   collapses to    a + b   (count of a is +1, b is +1)
		//   a + a + b       collapses to    2*a + b (count of a is +2)
		//   a - a           collapses to    0       (count of a is 0, removed)
		Result<AstPtr> simplifySum(std::vector<SumTerm> terms, SourceSpan span) {
			// Step 1: fold all numeric literals into one running total.
			double constant = 0.0;
			std::vector<SumTerm> nonConst;
			for (SumTerm& t : terms) {
				double v;
				if (isNumber(*t.expr, v)) {
					constant += (t.sign == TermSign::Pos) ? v : -v;
					if (!std::isfinite(constant)) return Diagnostic{ DiagCode::NotFinite, t.expr->span };
				}
				else {
					nonConst.push_back(std::move(t));
				}
			}
			// Step 2: group like terms by structural equality (using toString as
			// the equivalence key) and sum their signed counts. Pos contributes
			// +1, Neg contributes -1.
			std::map<std::string, double> counts;          // key -> net count
			std::map<std::string, AstPtr> representative; // key -> one representative AST
			for (SumTerm& t : nonConst) {
				double step = 1;
				BinaryNode* bN = std::get_if<BinaryNode>(&t.expr->value);
				if (bN && bN->op == BinaryOp::Mul) {
					const NumberNode* numNode = std::get_if<NumberNode>(&bN->rhs->value);
					if (numNode) {
						step = numNode->value;
						t.expr = bN->lhs;
					}
				}
				std::string key = toString(*t.expr);
				counts[key] += (t.sign == TermSign::Pos) ? step : -step;
				if (representative.find(key) == representative.end()) {
					representative[key] = std::move(t.expr);
				}
			}

			// Step 3: rebuild from the counts.
			std::vector<SumTerm> rebuilt;
			rebuilt.reserve(counts.size());
			for (const auto& [key, count] : counts) {
				if (count == 0) continue;
				const TermSign sign = (count > 0) ? TermSign::Pos : TermSign::Neg;
				const double magnitude = std::abs(count);
				AstPtr expr = representative[key];
				if (magnitude == 1) {
					rebuilt.push_back(SumTerm{ sign, std::move(expr) });
				}
				else {
					// Wrap the term in (count * expr), then re-simplify so an
					// inner numeric coefficient folds: e.g. (2 * (2*a)) -> 4*a.
					AstPtr product = makeBinary(BinaryOp::Mul, std::move(expr), makeNumber(magnitude, span), span);
					Result<AstPtr> spR = simplifyImpl(product);
					if (!spR) return std::move(spR).error();
					rebuilt.push_back(SumTerm{ sign, spR.value() });
				}
			}

			// Step 4: re-introduce the constant and collapse to the simplest form.
			if (rebuilt.empty()) {
				return makeNumber(constant, span);
			}
			if (constant != 0.0) {
				SumTerm t;
				t.sign = (constant > 0.0) ? TermSign::Pos : TermSign::Neg;
				t.expr = makeNumber(std::abs(constant), span);
				rebuilt.push_back(std::move(t));
			}
			if (rebuilt.size() == 1 && rebuilt[0].sign == TermSign::Pos) {
				return rebuilt[0].expr;
			}
			if (rebuilt.size() == 1 && rebuilt[0].sign == TermSign::Neg) {
				return makeUnary(UnaryOp::Negate, rebuilt[0].expr, span);
			}
			AstPtr newNode;
			if (rebuilt[0].sign == TermSign::Pos) {
				newNode = rebuilt[0].expr;
			}
			else {
				newNode = makeUnary(UnaryOp::Negate, rebuilt[0].expr);
			}
			for (size_t i = 1; i < rebuilt.size(); ++i) {
				if (rebuilt[i].sign == TermSign::Pos) {
					newNode = makeBinary(BinaryOp::Add, newNode, rebuilt[i].expr);
				}
				else {
					newNode = makeBinary(BinaryOp::Sub, newNode, rebuilt[i].expr);
				}
			}
			return newNode;
		}

		// Same idea for products. Numeric literals fold; like factors combine
		// into powers by summing their net exponent (Numer = +1, Denom = -1):
		//   a * a * b / a   collapses to    a * b   (exp of a is +1)
		//   a * a           collapses to    a^2     (exp of a is +2)
		//   a / a           collapses to    1       (exp of a is 0, removed)
		//   a / a / a       collapses to    1/a     (exp of a is -1)
		Result<AstPtr> simplifyProduct(std::vector<ProductTerm> terms, SourceSpan span) {
			// Fold numeric literals.
			double numerProduct = 1.0;
			double denomProduct = 1.0;
			std::vector<ProductTerm> nonConst;
			for (ProductTerm& t : terms) {
				double v;
				if (isNumber(*t.expr, v)) {
					if (t.role == TermRole::Numer) {
						numerProduct *= v;
						if (!std::isfinite(numerProduct)) return Diagnostic{ DiagCode::NotFinite, t.expr->span };
					}
					else {
						denomProduct *= v;
						if (!std::isfinite(denomProduct)) return Diagnostic{ DiagCode::NotFinite, t.expr->span };
						if (denomProduct == 0) return Diagnostic{ DiagCode::DivisionByZero, t.expr->span };
					}
				}
				else {
					nonConst.push_back(std::move(t));
				}
			}
			const double constant = numerProduct / denomProduct;

			if (!std::isfinite(constant)) return Diagnostic{ DiagCode::NotFinite, span };

			// Group like factors by structural equality and sum their net
			// exponents (Numer contributes +1, Denom contributes -1).
			std::map<std::string, int> exponents;
			std::map<std::string, AstPtr> representative;
			for (ProductTerm& t : nonConst) {
				std::string key = toString(*t.expr);
				exponents[key] += (t.role == TermRole::Numer) ? 1 : -1;
				if (representative.find(key) == representative.end()) {
					representative[key] = std::move(t.expr);
				}
			}

			// Rebuild from the exponents. Note: we must NOT collapse a denominator
			// term that started as a literal 0 -- denomHasZero remembers that case
			// and suppresses the multiplication-by-zero collapse below.
			std::vector<ProductTerm> rebuilt;
			rebuilt.reserve(exponents.size());
			for (const auto& [key, exp] : exponents) {
				if (exp == 0) continue;
				const TermRole role = (exp > 0) ? TermRole::Numer : TermRole::Denom;
				const int magnitude = std::abs(exp);
				AstPtr expr = representative[key];
				if (magnitude == 1) {
					rebuilt.push_back(ProductTerm{ role, std::move(expr) });
				}
				else {
					// Wrap the factor in expr^magnitude, then re-simplify so the
					// (a^m)^n -> a^(m*n) rule fires (e.g. (x^2)^2 -> x^4).
					AstPtr powered = makeBinary(BinaryOp::Pow, std::move(expr),
						makeNumber(magnitude, span), span);
					Result<AstPtr> spR = simplifyImpl(powered);
					if (!spR) return std::move(spR).error();
					rebuilt.push_back(ProductTerm{ role, spR.value() });
				}
			}

			// Multiplication by zero
			if (constant == 0.0) {
				bool hasFreeDenom = false;
				for (const ProductTerm& t : rebuilt) {
					if (t.role == TermRole::Denom) { hasFreeDenom = true; break; }
				}
				if (!hasFreeDenom) {
					return makeNumber(0.0, span);
				}
			}

			if (rebuilt.empty()) {
				return makeNumber(constant, span);
			}
			if (constant != 1.0) {
				ProductTerm t{ TermRole::Numer, makeNumber(constant, span) };
				rebuilt.push_back(std::move(t));
			}
			if (rebuilt.size() == 1 && rebuilt[0].role == TermRole::Numer) {
				return rebuilt[0].expr;
			}
			AstPtr newNode;
			if (rebuilt[0].role == TermRole::Numer) {
				newNode = rebuilt[0].expr;
			}
			else {
				newNode = makeBinary(BinaryOp::Div, makeNumber(1), rebuilt[0].expr);
			}
			for (size_t i = 1; i < rebuilt.size(); ++i) {
				if (rebuilt[i].role == TermRole::Numer) {
					newNode = makeBinary(BinaryOp::Mul, newNode, rebuilt[i].expr);
				}
				else {
					newNode = makeBinary(BinaryOp::Div, newNode, rebuilt[i].expr);
				}
			}
			return newNode;
		}

		Result<AstPtr> simplifyImpl(const AstPtr& node) {
			return std::visit(overloaded{
								  [&](const NumberNode& n) -> Result<AstPtr> {
									  if (!std::isfinite(n.value)) return Diagnostic{DiagCode::NotFinite, node->span};
									  return node;
								  },
								  [&](const VariableNode& v) -> Result<AstPtr> {
					// `auto` for iterators returned by .find() throughout this file:
					// the full iterator type adds nothing the .find() call doesn't
					// already convey.
					if (auto it = constants().find(v.name); it != constants().end()) {
						return makeNumber(it->second.value, node->span);
					}
					return node;
				},
				[&](const UnaryNode& u) -> Result<AstPtr> {
					Result<AstPtr> innerR = simplifyImpl(u.operand);
					if (!innerR) return std::move(innerR).error();
					AstPtr inner = innerR.value();
					// Fold -literal.
					double v;
					if (isNumber(*inner, v)) {
						return makeNumber(-v, node->span);
					}
					// Double negation.
					if (auto* innerU = std::get_if<UnaryNode>(&inner->value)) {
						if (innerU->op == UnaryOp::Negate) return innerU->operand;
					}
					return makeUnary(u.op, std::move(inner), node->span);
				},
				[&](const BinaryNode& b) -> Result<AstPtr> {
					// For + - * / we flatten into n-ary form.
					if (b.op == BinaryOp::Add || b.op == BinaryOp::Sub) {
						std::vector<SumTerm> terms;
						collectSum(node, TermSign::Pos, terms);
						for (SumTerm& t : terms) {
							Result<AstPtr> exprR = simplifyImpl(t.expr);
							if (!exprR) return std::move(exprR).error();
							t.expr = exprR.value();
						}
						return simplifySum(std::move(terms), node->span);
					}
					if (b.op == BinaryOp::Mul || b.op == BinaryOp::Div) {
						std::vector<ProductTerm> terms;
						collectProduct(node, TermRole::Numer, terms);
						for (ProductTerm& t : terms) {
							Result<AstPtr> exprR = simplifyImpl(t.expr);
							if (!exprR) return std::move(exprR).error();
							t.expr = exprR.value();
						}
						return simplifyProduct(std::move(terms), node->span);
					}
					// ^ : not flattened (right-assoc, non-commutative).
					Result<AstPtr> lhsR = simplifyImpl(b.lhs);
					if (!lhsR) return std::move(lhsR).error();
					AstPtr lhs = lhsR.value();
					Result<AstPtr> rhsR = simplifyImpl(b.rhs);
					if (!rhsR) return std::move(rhsR).error();
					AstPtr rhs = rhsR.value();
					double lv, rv;
					const bool lConst = isNumber(*lhs, lv);
					const bool rConst = isNumber(*rhs, rv);

					if (lConst && rConst) {
						double num = std::pow(lv, rv);
						if (!std::isfinite(num)) return Diagnostic{ DiagCode::NotFinite, node->span };
						return makeNumber(num, node->span);
					}

					// Identity rules.
					if (rConst && rv == 0.0) return makeNumber(1.0, node->span);  // x^0 = 1
					if (rConst && rv == 1.0) return lhs;                           // x^1 = x
					if (lConst && lv == 0.0) return makeNumber(0.0, node->span);   // 0^x = 0
					if (lConst && lv == 1.0) return makeNumber(1.0, node->span);   // 1^x = 1

					// (a^m)^n -> a^(m*n) when both m and n are integer literals.
					// We restrict to integer exponents because the general identity
					// (a^m)^n = a^(m*n) doesn't hold for negative bases with
					// non-integer exponents (e.g. ((-1)^2)^0.5 vs (-1)^1).
					if (rConst && rv == std::floor(rv)) {
						if (auto* innerPow = std::get_if<BinaryNode>(&lhs->value);
							innerPow && innerPow->op == BinaryOp::Pow) {
							double mv;
							if (isNumber(*innerPow->rhs, mv) && mv == std::floor(mv)) {
								return makeBinary(BinaryOp::Pow,
												  innerPow->lhs,
												  makeNumber(mv * rv, node->span),
												  node->span);
							}
						}
					}
					return makeBinary(BinaryOp::Pow, std::move(lhs), std::move(rhs), node->span);
				},
				[&](const CallNode& c) -> Result<AstPtr> {
					std::vector<AstPtr> args;
					args.reserve(c.args.size());
					bool allNumeric = true;
					std::vector<double> nums;
					for (const AstPtr& a : c.args) {
						Result<AstPtr> sR = simplifyImpl(a);
						if (!sR) return std::move(sR).error();
						AstPtr s = sR.value();
						double v;
						if (allNumeric && isNumber(*s, v)) {
							nums.push_back(v);
						}
else {
 allNumeric = false;
}
args.push_back(std::move(s));
}
if (allNumeric) {
	if (auto it = functions().find(c.name); it != functions().end()) {
		if (it->second.arity == args.size()) {
			double num = it->second.fn(nums);
			if (!std::isfinite(num)) return Diagnostic{DiagCode::NotFinite, node->span};
			return makeNumber(num, node->span);
		}
	}
}
return makeCall(c.name, std::move(args), node->span);
},
[&](const EquationNode& e) -> Result<AstPtr> {
	Result<AstPtr> lhs = simplifyImpl(e.lhs);
	if (!lhs) return std::move(lhs).error();
	Result<AstPtr> rhs = simplifyImpl(e.rhs);
	if (!rhs) return std::move(rhs).error();
	return makeEquation(lhs.value(), rhs.value(), node->span);
},
				}, node->value);
		}

	}  // namespace

	Result<AstPtr> simplify(const AstPtr& node) {
		return simplifyImpl(node);
	}

	// ----- clearDenominators ---------------------------------------------------
	//
	// We rewrite an expression as a single rational form (numer / denom). The
	// caller then constructs an equation L_numer * R_denom = R_numer * L_denom
	// which is asymptote-free.

	namespace {

		struct Rational {
			AstPtr numer;
			AstPtr denom;  // never null; if the original expression had no division, denom = 1
		};

		// Build an AST node representing the product of two ASTs, with mild
		// optimization: x * 1 -> x.
		AstPtr makeMul(AstPtr a, AstPtr b, SourceSpan span) {
			double av, bv;
			const bool aIsOne = isNumber(*a, av) && av == 1.0;
			const bool bIsOne = isNumber(*b, bv) && bv == 1.0;
			if (aIsOne) return b;
			if (bIsOne) return a;
			return makeBinary(BinaryOp::Mul, std::move(a), std::move(b), span);
		}

		AstPtr makeAdd(AstPtr a, AstPtr b, SourceSpan span) {
			double av, bv;
			const bool aIsZero = isNumber(*a, av) && av == 0.0;
			const bool bIsZero = isNumber(*b, bv) && bv == 0.0;
			if (aIsZero) return b;
			if (bIsZero) return a;
			return makeBinary(BinaryOp::Add, std::move(a), std::move(b), span);
		}

		AstPtr makeSub(AstPtr a, AstPtr b, SourceSpan span) {
			double bv;
			if (isNumber(*b, bv) && bv == 0.0) return a;
			return makeBinary(BinaryOp::Sub, std::move(a), std::move(b), span);
		}

		Rational toRational(const AstPtr& node) {
			const SourceSpan span = node->span;
			return std::visit(overloaded{
										 [&](const NumberNode&) -> Rational {
											 return Rational{node, makeNumber(1.0, span)};
										 },
										 [&](const VariableNode&) -> Rational {
											 return Rational{node, makeNumber(1.0, span)};
										 },
										 [&](const UnaryNode& u) -> Rational {
					// -(a/b) = (-a)/b
					Rational r = toRational(u.operand);
					r.numer = makeUnary(UnaryOp::Negate, std::move(r.numer), span);
					return r;
				},
				[&](const CallNode&) -> Rational {
					return Rational{node, makeNumber(1.0, span)};
				},
				[&](const EquationNode&) -> Rational {
					// Equations don't appear inside expressions. Defensive default.
					return Rational{node, makeNumber(1.0, span)};
				},
				[&](const BinaryNode& b) -> Rational {
					const Rational l = toRational(b.lhs);
					const Rational r = toRational(b.rhs);
					switch (b.op) {
					case BinaryOp::Add:
						// (l_n / l_d) + (r_n / r_d) = (l_n*r_d + r_n*l_d) / (l_d*r_d)
						return Rational{
							makeAdd(makeMul(l.numer, r.denom, span),
									makeMul(r.numer, l.denom, span),
									span),
							makeMul(l.denom, r.denom, span),
						};
					case BinaryOp::Sub:
						return Rational{
							makeSub(makeMul(l.numer, r.denom, span),
									makeMul(r.numer, l.denom, span),
									span),
							makeMul(l.denom, r.denom, span),
						};
					case BinaryOp::Mul:
						return Rational{
							makeMul(l.numer, r.numer, span),
							makeMul(l.denom, r.denom, span),
						};
					case BinaryOp::Div:
						// (l_n / l_d) / (r_n / r_d) = (l_n * r_d) / (l_d * r_n)
						return Rational{
							makeMul(l.numer, r.denom, span),
							makeMul(l.denom, r.numer, span),
						};
					case BinaryOp::Pow: {
						// x^n where n is a non-negative integer literal: distribute.
						// Otherwise treat as opaque. (We *could* do (a/b)^n =
						// a^n / b^n in general but for variable exponents over
						// negative bases this isn't even mathematically clean.)
						double exp;
						if (isNumber(*b.rhs, exp) && exp >= 0 &&
							exp == std::floor(exp) && exp < 32) {
							return Rational{
								makeBinary(BinaryOp::Pow, l.numer, b.rhs, span),
								makeBinary(BinaryOp::Pow, l.denom, b.rhs, span),
							};
						}
						return Rational{node, makeNumber(1.0, span)};
					}
					}
					return Rational{node, makeNumber(1.0, span)};
				},
				}, node->value);
		}

	}  // namespace

	Result<AstPtr> clearDenominators(const AstPtr& equationNode) {
		const auto* eq = std::get_if<EquationNode>(&equationNode->value);
		if (!eq) return equationNode;  // not an equation; nothing to do

		const Rational L = toRational(eq->lhs);
		const Rational R = toRational(eq->rhs);

		// Cross-multiply: L_n * R_d = R_n * L_d. Then run simplify on each side
		// so the resulting trees are tidy (e.g. so 1*x*1 collapses to x).
		Result<AstPtr> newLhs = simplify(makeMul(L.numer, R.denom, equationNode->span));
		if (!newLhs) return std::move(newLhs).error();
		Result<AstPtr> newRhs = simplify(makeMul(R.numer, L.denom, equationNode->span));
		if (!newRhs) return std::move(newRhs).error();
		return makeEquation(newLhs.value(), newRhs.value(), equationNode->span);
	}

}  // namespace calc::core
