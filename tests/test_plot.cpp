#include "test_support.h"

// -----------------------------------------------------------------------
// clearDenominators (asymptote prevention for the grapher)
// Tested via PlotFunctor: compilePlot applies clearDenominators internally,
// so sampling the functor at known points verifies the cleared form is correct.
// -----------------------------------------------------------------------

TEST_CASE("clearDenominators: simple reciprocal") {
	// y = 1/x: on the curve lhs-rhs == 0; near x=0 the functor stays finite.
	CalculatorCore core;
	core.evaluateLine("eq: y = 1/x", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"}, true });
	REQUIRE(f.ok());

	// Points on the curve: y = 1/x -> lhs-rhs should be 0.
	REQUIRE_APPROX(f.value()({ 2.0, 0.5 }), 0.0, 1e-12);
	REQUIRE_APPROX(f.value()({ -1.0, -1.0 }), 0.0, 1e-12);
	// Near the asymptote: cleared form y*x - 1 stays finite (not NaN).
	const double v = f.value()({ 1e-6, 1.0 });
	REQUIRE(!std::isnan(v));
	REQUIRE(std::abs(v) < 2.0);
}

TEST_CASE("clearDenominators: sum of two fractions") {
	// y = 1/(x-1) + 1/(x+1): at x=0, y=0; at x=2, y=4/3.
	CalculatorCore core;
	core.evaluateLine("eq: y = 1/(x-1) + 1/(x+1)", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"}, true });
	REQUIRE(f.ok());

	REQUIRE_APPROX(f.value()({ 0.0, 0.0 }), 0.0, 1e-12);
	REQUIRE_APPROX(f.value()({ 2.0, 4.0 / 3.0 }), 0.0, 1e-12);
}

TEST_CASE("clearDenominators: equation without denominators is unchanged in meaning") {
	// x^2 + y^2 = 1: point (1, 0) lies on the unit circle.
	CalculatorCore core;
	core.evaluateLine("eq: x^2 + y^2 = 1", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"}, true });
	REQUIRE(f.ok());

	REQUIRE_APPROX(f.value()({ 1.0, 0.0 }), 0.0, 1e-12);
}

TEST_CASE("graph: y = 1/x produces no asymptote line") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("g1: y = 1/x");
	auto resp = r.processLine("/graph(x, y, -3, 3, -3, 3, g1)");
	std::vector<std::string> lines;
	std::size_t pos = 0;
	while (pos < resp.size()) {
		const std::size_t nl = resp.find('\n', pos);
		if (nl == std::string::npos) { lines.push_back(resp.substr(pos)); break; }
		lines.push_back(resp.substr(pos, nl - pos));
		pos = nl + 1;
	}
	std::size_t axisCol = std::string::npos;
	for (const auto& ln : lines) {
		const std::size_t plus = ln.find('+');
		if (plus != std::string::npos &&
			ln.find('-') != std::string::npos &&
			ln.find_first_not_of("-+") == std::string::npos) {
			axisCol = plus;
			break;
		}
	}
	REQUIRE(axisCol != std::string::npos);
	int hashCount = 0;
	for (const auto& ln : lines) {
		if (axisCol < ln.size() && ln[axisCol] == '#') ++hashCount;
	}
	REQUIRE_EQ(hashCount, 0);
}

// -----------------------------------------------------------------------
// parseCommand: the free command parser (previously only hit via the REPL)
// -----------------------------------------------------------------------

TEST_CASE("parseCommand: bare command has a name and no arguments") {
	auto r = parseCommand("/list");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().name, std::string("list"));
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 0 });
}

TEST_CASE("parseCommand: arguments are split and trimmed") {
	auto r = parseCommand("/graph( x , y , -1 , 1 , 0 , 1 , p )");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().name, std::string("graph"));
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 7 });
	REQUIRE_EQ(r.value().args[0], std::string("x"));
	REQUIRE_EQ(r.value().args[2], std::string("-1"));
	REQUIRE_EQ(r.value().args[6], std::string("p"));
}

TEST_CASE("parseCommand: commas inside nested parens do not split arguments") {
	auto r = parseCommand("/graph(x, y, -(PI), PI, 0, log(2, 8), p)");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 7 });
	REQUIRE_EQ(r.value().args[2], std::string("-(PI)"));
	REQUIRE_EQ(r.value().args[5], std::string("log(2, 8)"));
}

TEST_CASE("parseCommand: empty argument list yields zero arguments") {
	auto r = parseCommand("/refresh()");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().name, std::string("refresh"));
	REQUIRE_EQ(r.value().args.size(), std::size_t{ 0 });
}

TEST_CASE("parseCommand: malformed command syntax is rejected") {
	REQUIRE(parseCommand("list").error().code == DiagCode::CommandMustStartWithSlash);
	REQUIRE(parseCommand("/").error().code == DiagCode::ExpectedCommandName);
	REQUIRE(parseCommand("/list extra").error().code == DiagCode::ExpectedOpenParen);
	REQUIRE(parseCommand("/graph(x").error().code == DiagCode::UnmatchedOpenParen);
	REQUIRE(parseCommand("/refresh() junk").error().code ==
		DiagCode::UnexpectedInputAfterCommand);
}

// -----------------------------------------------------------------------
// compilePlot: error paths and the dimension-agnostic design
// -----------------------------------------------------------------------

TEST_CASE("compilePlot: duplicate axis names are rejected") {
	CalculatorCore core;
	core.evaluateLine("eq: y = x", 400);
	auto f = core.compilePlot({ "eq", {"x", "x"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::AxisNamesMustDiffer);
}

TEST_CASE("compilePlot: unknown equation variable is rejected") {
	CalculatorCore core;
	auto f = core.compilePlot({ "ghost", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::NoSuchVariable);
	REQUIRE(f.error().detail == "ghost");
}

TEST_CASE("compilePlot: target must be an equation, not a plain expression") {
	CalculatorCore core;
	core.evaluateLine("p: x^2", 400);  // expression, not an equation
	auto f = core.compilePlot({ "p", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::GraphTargetNotEquation);
}

TEST_CASE("compilePlot: a free variable that is not an axis is rejected") {
	CalculatorCore core;
	core.evaluateLine("eq: y = x + z", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::NonAxisVariable);
	REQUIRE(f.error().detail == "z");
}

TEST_CASE("compilePlot: an equation using no axis at all is rejected") {
	CalculatorCore core;
	core.evaluateLine("eq: 2 = 2", 400);  // no free variables -> no axis appears
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::NoAxisVariable);
}

TEST_CASE("compilePlot: functor evaluates lhs - rhs and reports its shape") {
	CalculatorCore core;
	core.evaluateLine("eq: y = x^2", 400);
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(f.ok());
	REQUIRE_EQ(f.value().dimensions(), std::size_t{ 2 });
	REQUIRE(f.value().requiredStackSize() >= std::size_t{ 1 });

	REQUIRE_APPROX(f.value()({ 2.0, 4.0 }), 0.0, 1e-12);   // on the curve
	REQUIRE_APPROX(f.value()({ 3.0, 9.0 }), 0.0, 1e-12);
	REQUIRE_APPROX(f.value()({ 2.0, 5.0 }), 1.0, 1e-12);   // y - x^2 = 5 - 4
}

TEST_CASE("calls: wrong argument count is rejected") {
	// Every builtin must be called with its exact arity. These previously
	// slipped through and could reach the bytecode compiler, crashing on a
	// stack under/overflow when graphed.
	CalculatorCore core;
	auto tooMany = core.evaluateLine("sin(1, 2, 3)", 400);
	REQUIRE(!tooMany.ok());
	REQUIRE(tooMany.error().code == DiagCode::WrongArity);

	auto zeroArgs = core.evaluateLine("sin()", 400);
	REQUIRE(!zeroArgs.ok());
	REQUIRE(zeroArgs.error().code == DiagCode::WrongArity);

	auto tooFew = core.evaluateLine("log(2)", 400);  // log takes (base, x)
	REQUIRE(!tooFew.ok());
	REQUIRE(tooFew.error().code == DiagCode::WrongArity);
}

TEST_CASE("calls: unknown function names are rejected") {
	CalculatorCore core;
	auto sym = core.evaluateLine("foo(x)", 400);  // symbolic, still rejected
	REQUIRE(!sym.ok());
	REQUIRE(sym.error().code == DiagCode::UnknownFunction);

	auto num = core.evaluateLine("nosuchfn(1)", 400);
	REQUIRE(!num.ok());
	REQUIRE(num.error().code == DiagCode::UnknownFunction);
}

TEST_CASE("calls: correctly-formed calls still work") {
	REQUIRE_EQ(evalToString("sin(x)"), "sin(x)");          // symbolic preserved
	REQUIRE_APPROX(evalToNumber("log(2, 8)"), 3.0, 1e-12);  // numeric fold
	REQUIRE_APPROX(evalToNumber("root(2, 9)"), 3.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("abs(-5)"), 5.0, 1e-12);
}

TEST_CASE("calls: a bad-arity call can never reach the grapher") {
	// Regression: graphing "y = sin() + x" used to abort with std::out_of_range
	// because the zero-arg sin emitted a Sin op against an empty value stack.
	// The definition must now be rejected, so it is never stored to graph.
	CalculatorCore core;
	auto def = core.evaluateLine("eq: y = sin() + x", 400);
	REQUIRE(!def.ok());
	REQUIRE(def.error().code == DiagCode::WrongArity);

	// Since the definition was rejected, no "eq" exists to compile.
	auto f = core.compilePlot({ "eq", {"x", "y"} });
	REQUIRE(!f.ok());
	REQUIRE(f.error().code == DiagCode::NoSuchVariable);
}

TEST_CASE("compilePlot: the same engine compiles against three axes") {
	// The README claims PlotFunctor is dimension-agnostic; the ASCII renderer
	// only ever uses two axes, so this is the one place that claim is checked.
	CalculatorCore core;
	core.evaluateLine("eq: z = x + y", 400);
	auto f = core.compilePlot({ "eq", {"x", "y", "z"} });
	REQUIRE(f.ok());
	REQUIRE_EQ(f.value().dimensions(), std::size_t{ 3 });

	// coords are [x, y, z]; the functor returns lhs - rhs = z - (x + y).
	REQUIRE_APPROX(f.value()({ 1.0, 2.0, 3.0 }), 0.0, 1e-12);  // on the surface
	REQUIRE_APPROX(f.value()({ 1.0, 2.0, 5.0 }), 2.0, 1e-12);  // 5 - (1 + 2)
}
