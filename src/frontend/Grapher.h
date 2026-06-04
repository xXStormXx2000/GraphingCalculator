#ifndef CALC_GRAPHER_H
#define CALC_GRAPHER_H

#include <string>

#include "CalculatorCore.h"
#include "Types.h"

namespace calc {

	struct GraphRequest {
		std::string  xName;
		std::string  yName;
		core::Rect         view;
		core::PlotFunctor  functor;
	};

	// Render an ASCII plot. Returns a string with embedded newlines, or a
	// Diagnostic if the view rect is invalid.
	core::Result<std::string> renderGraph(const GraphRequest& req);

}  // namespace calc

#endif
