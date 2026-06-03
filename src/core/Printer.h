#ifndef CALC_PRINTER_H
#define CALC_PRINTER_H

#include <string>

#include "Ast.h"

namespace calc::core {

// Render an AST back to a human-readable expression string.
// Inserts parentheses only where required by operator precedence.
std::string toString(const AstNode& node);

// Trim trailing zeros after a decimal point, remove a bare trailing dot.
std::string formatNumber(double v);

}  // namespace calc::core

#endif
