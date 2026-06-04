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

}  // namespace calc::core
