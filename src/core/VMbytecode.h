#ifndef VMBYTECODE_H
#define VMBYTECODE_H

#include <cstddef>
#include <vector>
#include <unordered_map>

#include "Ast.h"
#include "Builtins.h"

namespace calc::core {

	// Bytecode structure to represent each instruction
	struct Bytecode
	{
		VMop op;
		double value;
		size_t binding;
		Bytecode(VMop o, double val = 0, size_t bind = 0);
	};

	// Function to execute the bytecode
	double execute(const std::vector<Bytecode>& bytecode,
		const std::vector<double>& bindings,
		std::vector<double>& stack);

	// AST to Bytecode
	size_t ASTtoBytecode(const AstPtr AST, std::vector<Bytecode>& bytecode,
		const std::unordered_map<std::string, size_t>& bindings);

}

#endif // VMBYTECODE_H
