#ifndef VMBYTECODE_H
#define VMBYTECODE_H

#include <cstddef>
#include <vector>
#include <unordered_map>

#include "Ast.h"
#include "Builtins.h"
#include "Bytecode.h"

namespace calc::core {

	// Function to execute the bytecode
	double execute(const Chunk& bytecode,
		const std::vector<double>& bindings,
		std::vector<double>& stack);

	// AST to Bytecode
	size_t ASTtoBytecode(const AstPtr& AST, Chunk& bytecode,
		const std::unordered_map<std::string, size_t>& bindings);

}

#endif // VMBYTECODE_H
