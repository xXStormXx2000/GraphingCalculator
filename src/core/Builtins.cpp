#include "Builtins.h"

namespace calc::core {

	void collectVariables(const AstNode& node, std::unordered_set<std::string>& out) {
		std::visit(overloaded{
					   [&](const NumberNode&) {},
					   [&](const VariableNode& v) {
						   if (constants().find(v.name) == constants().end()) {
							   out.insert(v.name);
						   }
					   },
					   [&](const UnaryNode& u) { collectVariables(*u.operand, out); },
					   [&](const BinaryNode& b) {
						   collectVariables(*b.lhs, out);
						   collectVariables(*b.rhs, out);
					   },
					   [&](const CallNode& c) {
						   for (const AstPtr& a : c.args) collectVariables(*a, out);
					   },
					   [&](const EquationNode& e) {
						   collectVariables(*e.lhs, out);
						   collectVariables(*e.rhs, out);
					   },
			}, node.value);
	}

	std::size_t countNodes(const AstNode& node) {
		return std::visit(overloaded{
							   [&](const NumberNode&) -> std::size_t { return 1; },
							   [&](const VariableNode&) -> std::size_t { return 1; },
							   [&](const UnaryNode& u) -> std::size_t {
								   return 1 + countNodes(*u.operand);
							   },
							   [&](const BinaryNode& b) -> std::size_t {
								   return 1 + countNodes(*b.lhs) + countNodes(*b.rhs);
							   },
							   [&](const CallNode& c) -> std::size_t {
								   std::size_t total = 1;
								   for (const AstPtr& a : c.args) total += countNodes(*a);
								   return total;
							   },
							   [&](const EquationNode& e) -> std::size_t {
								   return 1 + countNodes(*e.lhs) + countNodes(*e.rhs);
							   },
			}, node.value);
	}

}  // namespace calc::core