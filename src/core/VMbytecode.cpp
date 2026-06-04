#include "VMbytecode.h"

#include <cmath>

namespace calc::core {

	namespace {
		struct FuncInfo {
			VMop op;
			size_t arity;
		};

		inline const std::unordered_map<std::string, FuncInfo>& funcToVMop() {
			static const std::unordered_map<std::string, FuncInfo> table = {
																			{"sin",  {VMop::Sin, 1}},
																			{"cos",  {VMop::Cos, 1}},
																			{"tan",  {VMop::Tan, 1}},
																			{"asin", {VMop::Asin, 1}},
																			{"acos", {VMop::Acos, 1}},
																			{"atan", {VMop::Atan, 1}},
																			{"sqrt", {VMop::Sqrt, 1}},
																			{"abs",  {VMop::Abs, 1}},
																			{"log",  {VMop::Log, 2}},
																			{"root", {VMop::Root, 2}},
			};
			return table;
		}
	}

	// Bytecode structure to represent each instruction
	Bytecode::Bytecode(VMop o, double val, size_t bind)
		: op(o), value(val), binding(bind) {
	}


	// Function to execute the bytecode
	double execute(const std::vector<Bytecode>& bytecode,
		const std::vector<double>& bindings,
		std::vector<double>& stack) {
		stack.clear();
		for (const Bytecode& instruction : bytecode) {
			switch (instruction.op) {
			case VMop::Push: {
				stack.push_back(instruction.value);
				break;
			}
			case VMop::Bind: {
				stack.push_back(bindings[instruction.binding]);
				break;
			}
			case VMop::Add: {
				double b = stack.back(); stack.pop_back();
				double a = stack.back(); stack.pop_back();
				stack.push_back(a + b);
				break;
			}
			case VMop::Sub: {
				double b = stack.back(); stack.pop_back();
				double a = stack.back(); stack.pop_back();
				stack.push_back(a - b);
				break;
			}
			case VMop::Mul: {
				double b = stack.back(); stack.pop_back();
				double a = stack.back(); stack.pop_back();
				stack.push_back(a * b);
				break;
			}
			case VMop::Div: {
				double b = stack.back(); stack.pop_back();
				double a = stack.back(); stack.pop_back();
				stack.push_back(a / b);
				break;
			}
			case VMop::Exp: {
				double b = stack.back(); stack.pop_back();
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::pow(a, b));
				break;
			}
			case VMop::Uminus: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(-a);
				break;
			}
			case VMop::Sin: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::sin(a));
				break;
			}
			case VMop::Cos: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::cos(a));
				break;
			}
			case VMop::Tan: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::tan(a));
				break;
			}
			case VMop::Asin: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::asin(a));
				break;
			}
			case VMop::Acos: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::acos(a));
				break;
			}
			case VMop::Atan: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::atan(a));
				break;
			}
			case VMop::Abs: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::abs(a));
				break;
			}
			case VMop::Log: {
				double b = stack.back(); stack.pop_back();
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::log(b) / std::log(a));
				break;
			}
			case VMop::Sqrt: {
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::sqrt(a));
				break;
			}
			case VMop::Root: {
				double b = stack.back(); stack.pop_back();
				double a = stack.back(); stack.pop_back();
				stack.push_back(std::pow(b, 1 / a));
				break;
			}
			default:
				break;
			}
		}
		return stack.empty() ? 0 : stack.back();
	}

	// AST to Bytecode
	size_t ASTtoBytecode(const AstPtr AST, std::vector<Bytecode>& bytecode,
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
								  VMop op;
								  switch (b.op) {
								  case BinaryOp::Add: op = VMop::Add; break;
								  case BinaryOp::Sub: op = VMop::Sub; break;
								  case BinaryOp::Mul: op = VMop::Mul; break;
								  case BinaryOp::Div: op = VMop::Div; break;
								  case BinaryOp::Pow: op = VMop::Exp; break;
								  default:
									  break;
								  }
								  Bytecode bc(op);
								  bytecode.push_back(bc);
								  return std::max(dLhs, 1 + dRhs);
							  },
							  [&](const CallNode& call) -> size_t {
								  FuncInfo funInfo = funcToVMop().at(call.name);
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
