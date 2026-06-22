// VMbytecode.cpp — stack-machine compiler and interpreter.
//
// Precondition: the bytecode passed to execute() is assumed to come from
// ASTtoBytecode() on a simplified, well-formed AST. The VM trusts this — it
// does not validate opcodes or guard against stack underflow on the hot path,
// since a violation means an upstream bug, not recoverable input. Debug-build
// asserts catch such violations; release builds run unchecked for speed.

#include "VMbytecode.h"

#include <cmath>
#include <cassert>

namespace calc::core {

	// Integer power
	static inline double ipow(double a, int n) {
		if (n == 0) return 1.0;
		unsigned m = n < 0 ? -static_cast<unsigned>(n) : static_cast<unsigned>(n);
		double r = 1.0, base = a;
		while (m) {
			if (m & 1) r *= base;
			m >>= 1;
			if (m) base *= base;
		}
		return n < 0 ? 1.0 / r : r;
	}

	// Function to execute the bytecode
	double execute(const Chunk& bytecode,
		const std::vector<double>& bindings,
		std::vector<double>& stack) {
		// Size (not just reserve) so data() is valid for the whole depth.
		// No reallocation when capacity already suffices, which is the common
		// case since callers reserve requiredStackSize() once and reuse.
		if (stack.size() < bytecode.size())
			stack.resize(bytecode.size());

		double* const base = stack.data();
		double* sp = base;   // points one past the current top
		for (const Bytecode& instruction : bytecode) {
			switch (instruction.op) {
			case VMop::Push: {
				*sp++ = instruction.value;
				break;
			}
			case VMop::Bind: {
				*sp++ = bindings[instruction.binding];
				break;
			}
			case VMop::Add: {
				assert(sp - base >= 2 && "stack underflow");
				sp[-2] = sp[-2] + sp[-1]; --sp;
				break;
			}
			case VMop::Sub: {
				assert(sp - base >= 2 && "stack underflow");
				sp[-2] = sp[-2] - sp[-1]; --sp;
				break;
			}
			case VMop::Mul: {
				assert(sp - base >= 2 && "stack underflow");
				sp[-2] = sp[-2] * sp[-1]; --sp;
				break;
			}
			case VMop::Div: {
				assert(sp - base >= 2 && "stack underflow");
				sp[-2] = sp[-2] / sp[-1]; --sp;
				break;
			}
			case VMop::Pow: {
				assert(sp - base >= 2 && "stack underflow");
				double b = sp[-1], a = sp[-2];
				if (b == std::floor(b) && std::abs(b) < 512)
					sp[-2] = ipow(a, (int)b);
				else
					sp[-2] = std::pow(a, b);
				--sp;
				break;
			}
			case VMop::Uminus: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = -sp[-1];
				break;
			}
			case VMop::Sin: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::sin(sp[-1]);
				break;
			}
			case VMop::Cos: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::cos(sp[-1]);
				break;
			}
			case VMop::Tan: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::tan(sp[-1]);
				break;
			}
			case VMop::Asin: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::asin(sp[-1]);
				break;
			}
			case VMop::Acos: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::acos(sp[-1]);
				break;
			}
			case VMop::Atan: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::atan(sp[-1]);
				break;
			}
			case VMop::Sinh: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::sinh(sp[-1]);
				break;
			}
			case VMop::Cosh: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::cosh(sp[-1]);
				break;
			}
			case VMop::Tanh: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::tanh(sp[-1]);
				break;
			}
			case VMop::Asinh: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::asinh(sp[-1]);
				break;
			}
			case VMop::Acosh: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::acosh(sp[-1]);
				break;
			}
			case VMop::Atanh: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::atanh(sp[-1]);
				break;
			}
			case VMop::Abs: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::abs(sp[-1]);
				break;
			}
			case VMop::Log: {
				assert(sp - base >= 2 && "stack underflow");
				double b = sp[-1], a = sp[-2];
				sp[-2] = std::log(b) / std::log(a); --sp;
				break;
			}
			case VMop::Sqrt: {
				assert(sp - base >= 1 && "stack underflow");
				sp[-1] = std::sqrt(sp[-1]);
				break;
			}
			case VMop::Root: {
				assert(sp - base >= 2 && "stack underflow");
				double b = sp[-1], a = sp[-2];
				sp[-2] = std::pow(b, 1 / a); --sp;
				break;
			}
			default:
				assert(false && "You stupid donkey, you added a new VMop code and forgot to implement it in the VM");
				break;
			}
		}
		return sp == base ? 0 : sp[-1];
	}

	// AST to Bytecode
	size_t ASTtoBytecode(const AstPtr& AST, Chunk& bytecode,
		const std::unordered_map<std::string, size_t>& bindings) {
		return std::visit(overloaded{
							  [&](const NumberNode& n) -> size_t {
								  Bytecode bc(VMop::Push, n.value);
								  bytecode.push_back(bc);
								  return 1;
							  },
							  [&](const VariableNode& v) -> size_t {
								  Bytecode bc(VMop::Bind, 0, bindings.at(v.name));
								  bytecode.push_back(bc);
								  return 1;
							  },
							  [&](const UnaryNode& u) -> size_t {
								  size_t dLhs = ASTtoBytecode(u.operand, bytecode, bindings);
								  Bytecode bc(VMop::Uminus);
								  bytecode.push_back(bc);
								  return dLhs;
							  },
							  [&](const BinaryNode& b) -> size_t {
								  size_t dLhs = ASTtoBytecode(b.lhs, bytecode, bindings);
								  size_t dRhs = ASTtoBytecode(b.rhs, bytecode, bindings);
								  VMop op = VMop::Add;
								  switch (b.op) {
								  case BinaryOp::Add: op = VMop::Add; break;
								  case BinaryOp::Sub: op = VMop::Sub; break;
								  case BinaryOp::Mul: op = VMop::Mul; break;
								  case BinaryOp::Div: op = VMop::Div; break;
								  case BinaryOp::Pow: op = VMop::Pow; break;
								  default:
									  assert(false && "unreachable: malformed bytecode");
									  break;
								  }
								  Bytecode bc(op);
								  bytecode.push_back(bc);
								  return std::max(dLhs, 1 + dRhs);
							  },
							  [&](const CallNode& call) -> size_t {
								  FunctionDef funInfo = functions().at(call.name);
								  size_t maxDepth = 0;
								  if (funInfo.arity == 1) {
									  maxDepth = ASTtoBytecode(call.args.at(0), bytecode, bindings);
								  }
								  if (funInfo.arity == 2) {
									  size_t a = ASTtoBytecode(call.args.at(0), bytecode, bindings);
									  size_t b = ASTtoBytecode(call.args.at(1), bytecode, bindings);
									  maxDepth = std::max(a, 1 + b);
								  }
								  Bytecode bc(funInfo.op);
								  bytecode.push_back(bc);
								  return maxDepth;
							  },
							  [&](const EquationNode& e) -> size_t {
								  size_t dLhs = ASTtoBytecode(e.lhs, bytecode, bindings);
								  size_t dRhs = ASTtoBytecode(e.rhs, bytecode, bindings);
								  Bytecode bc(VMop::Sub);
								  bytecode.push_back(bc);
								  return std::max(dLhs, 1 + dRhs);
							  },
			}, AST->value);
	}

}