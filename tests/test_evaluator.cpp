#include "test_support.h"

// -----------------------------------------------------------------------
// Evaluator
// -----------------------------------------------------------------------

TEST_CASE("evaluator: division by zero") {
	CalculatorCore core;
	auto r = core.evaluateLine("1/0", 400);
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::DivisionByZero ||
		r.error().code == DiagCode::NotFinite);
}

TEST_CASE("evaluator: with bindings") {
	CalculatorCore core;
	core.evaluateLine("x: 3", 400);
	core.evaluateLine("y: 5", 400);
	auto r = core.evaluateLine("x*x + y", 400);
	REQUIRE(r.ok());
	REQUIRE(r.value().value.has_value());
	REQUIRE_APPROX(*r.value().value, 14.0, 1e-12);
}

TEST_CASE("evaluator: built-in constants") {
	REQUIRE_APPROX(evalToNumber("pi"), 3.14159265358979, 1e-10);
}

TEST_CASE("evaluator: undefined variable") {
	CalculatorCore core;
	// A free variable does not produce an error — it produces a symbolic result.
	auto r = core.evaluateLine("nope + 1", 400);
	REQUIRE(r.ok());
	REQUIRE(!r.value().value.has_value());  // not numeric
}

// -----------------------------------------------------------------------
// Math functions: numeric correctness for the whole table
// -----------------------------------------------------------------------

TEST_CASE("functions: trigonometry") {
	REQUIRE_APPROX(evalToNumber("cos(0)"), 1.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("tan(0)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("asin(1)"), 1.5707963267948966, 1e-12);  // pi/2
	REQUIRE_APPROX(evalToNumber("acos(1)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("atan(1)"), 0.7853981633974483, 1e-12);  // pi/4
}

TEST_CASE("functions: hyperbolics") {
	// Base values at 0.
	REQUIRE_APPROX(evalToNumber("sinh(0)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("cosh(0)"), 1.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("tanh(0)"), 0.0, 1e-12);
	// Values at 1 (against known constants).
	REQUIRE_APPROX(evalToNumber("sinh(1)"), 1.1752011936438014, 1e-12);
	REQUIRE_APPROX(evalToNumber("cosh(1)"), 1.5430806348152437, 1e-12);
	REQUIRE_APPROX(evalToNumber("tanh(1)"), 0.7615941559557649, 1e-12);
	// tanh saturates toward +/-1 for large |x| (and stays finite).
	REQUIRE_APPROX(evalToNumber("tanh(20)"), 1.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("tanh(-20)"), -1.0, 1e-9);
}

TEST_CASE("functions: hyperbolic identity cosh^2 - sinh^2 = 1") {
	// This catches a copy-paste error between the three implementations
	// (e.g. sinh's case calling std::cosh): the identity only holds if each
	// is wired to the right libm function. Checked at several points.
	REQUIRE_APPROX(evalToNumber("cosh(0)^2 - sinh(0)^2"), 1.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("cosh(1)^2 - sinh(1)^2"), 1.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("cosh(2)^2 - sinh(2)^2"), 1.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("cosh(-3)^2 - sinh(-3)^2"), 1.0, 1e-7);
}

TEST_CASE("functions: inverse hyperbolics and round-trips") {
	REQUIRE_APPROX(evalToNumber("asinh(0)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("acosh(1)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("atanh(0)"), 0.0, 1e-12);
	// Inverse undoes forward on each function's valid domain.
	REQUIRE_APPROX(evalToNumber("asinh(sinh(0.7))"), 0.7, 1e-12);
	REQUIRE_APPROX(evalToNumber("acosh(cosh(0.7))"), 0.7, 1e-12);
	REQUIRE_APPROX(evalToNumber("atanh(tanh(0.7))"), 0.7, 1e-12);
}

TEST_CASE("functions: inverse hyperbolic domain errors surface as NotFinite") {
	CalculatorCore core;
	// acosh is defined for x >= 1; atanh for |x| < 1. Outside, libm yields
	// NaN, which the simplifier reports as NotFinite (same path as sqrt(-1)).
	REQUIRE(core.evaluateLine("acosh(0.5)", 400).error().code == DiagCode::NotFinite);
	REQUIRE(core.evaluateLine("atanh(2)", 400).error().code == DiagCode::NotFinite);
	REQUIRE(core.evaluateLine("atanh(1)", 400).error().code == DiagCode::NotFinite);  // ->inf
}

TEST_CASE("functions: hyperbolics stay symbolic with a non-numeric argument") {
	REQUIRE_EQ(evalToString("sinh(x)"), std::string("sinh(x)"));
	REQUIRE_EQ(evalToString("cosh(x)"), std::string("cosh(x)"));
	REQUIRE_EQ(evalToString("tanh(x)"), std::string("tanh(x)"));
	REQUIRE_EQ(evalToString("asinh(x)"), std::string("asinh(x)"));
}

TEST_CASE("functions: abs, sqrt, log, root") {
	REQUIRE_APPROX(evalToNumber("abs(-5)"), 5.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("abs(5)"), 5.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("sqrt(2)"), 1.4142135623730951, 1e-12);
	REQUIRE_APPROX(evalToNumber("log(2, 8)"), 3.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("log(10, 1000)"), 3.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("root(2, 9)"), 3.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("root(3, 27)"), 3.0, 1e-6);
}

TEST_CASE("functions: composition and constants as arguments") {
	REQUIRE_APPROX(evalToNumber("sqrt(abs(-16))"), 4.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("sin(asin(0.5))"), 0.5, 1e-12);
	REQUIRE_APPROX(evalToNumber("sin(pi)"), 0.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("cos(pi)"), -1.0, 1e-12);
}

TEST_CASE("functions: stay symbolic when an argument is not numeric") {
	REQUIRE_EQ(evalToString("sin(x)"), std::string("sin(x)"));
	auto s = evalToString("log(2, x)");
	REQUIRE(s.find("log(2") != std::string::npos);
	REQUIRE(s.find("x") != std::string::npos);
}

TEST_CASE("constants: single-letter physical constants are reserved names") {
	// c, e, G, h, R, etc. resolve to their values, so they cannot be used as
	// ordinary variables. This is easy to trip over (a stray `c` becomes the
	// speed of light); pin it so the behavior is explicit.
	REQUIRE_APPROX(evalToNumber("c"), 299792458.0, 1e-3);
	REQUIRE_APPROX(evalToNumber("2*c"), 599584916.0, 1e-3);
	REQUIRE_APPROX(evalToNumber("e"), 2.718281828459045, 1e-12);
}

TEST_CASE("functions: domain errors surface as NotFinite") {
	CalculatorCore core;
	REQUIRE(core.evaluateLine("sqrt(-4)", 400).error().code == DiagCode::NotFinite);
	// log(1, x) divides by log(1) == 0.
	REQUIRE(core.evaluateLine("log(1, 5)", 400).error().code == DiagCode::NotFinite);
}

// -----------------------------------------------------------------------
// CalculatorCore: session state
// -----------------------------------------------------------------------

TEST_CASE("core: definitionOf is empty for an unknown name") {
	CalculatorCore core;
	REQUIRE(!core.definitionOf("nope").has_value());
	core.evaluateLine("a: 5", 400);
	REQUIRE(core.definitionOf("a").has_value());
	REQUIRE_EQ(*core.definitionOf("a"), std::string("5"));
}

TEST_CASE("core: redefining a name overwrites the previous definition") {
	CalculatorCore core;
	core.evaluateLine("a: 1", 400);
	core.evaluateLine("a: 2", 400);
	REQUIRE_EQ(*core.definitionOf("a"), std::string("2"));
	REQUIRE_EQ(core.definedNames().size(), std::size_t{ 1 });
}

TEST_CASE("core: numeric result carries a value, symbolic result does not") {
	CalculatorCore core;
	auto num = core.evaluateLine("2 + 3", 400);
	REQUIRE(num.ok());
	REQUIRE(num.value().value.has_value());
	REQUIRE_APPROX(*num.value().value, 5.0, 1e-12);
	REQUIRE_EQ(num.value().canonical, std::string("5"));

	auto sym = core.evaluateLine("x + 1", 400);
	REQUIRE(sym.ok());
	REQUIRE(!sym.value().value.has_value());
}
