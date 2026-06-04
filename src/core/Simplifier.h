#ifndef CALC_SIMPLIFIER_H
#define CALC_SIMPLIFIER_H

#include "Ast.h"

namespace calc::core {

	// Recursively simplify an AST.
	Result<AstPtr> simplify(const AstPtr& node);

	// Rewrites an equation by multiplying both sides by every denominator,
	Result<AstPtr> clearDenominators(const AstPtr& equationNode);

}  // namespace calc::core

#endif
