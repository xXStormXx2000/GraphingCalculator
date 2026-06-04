#ifndef CALC_PARSER_H
#define CALC_PARSER_H

#include "Ast.h"
#include "Types.h"

namespace calc::core {

	// Output of the top-level parser.
	//
	// `assignTo` is non-empty if the user wrote "name : expr"; the engine stores
	// the resulting AST under that name.
	//
	// `expr` is always present.
	struct ParsedExpression {
		std::string assignTo;  // empty -> not an assignment
		AstPtr expr;
	};

	// Parses a sequence of tokens (including the trailing EndOfInput) into an
	// AST. Handles assignment, equations, all operator precedences, and unary
	// minus.
	Result<ParsedExpression> parseExpression(const std::vector<Token>& tokens);

}  // namespace calc::core

#endif
