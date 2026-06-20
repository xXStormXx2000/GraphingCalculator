#include "test_support.h"

// -----------------------------------------------------------------------
// Printer (round-trip and parenthesization)
// These tests operate on the internal AST/printer API directly.
// -----------------------------------------------------------------------

TEST_CASE("printer: parens only where needed") {
	auto t1 = tokenize("(a - b) - c");
	std::size_t size1 = 0;
	auto p1 = parseExpression(t1.value(), 400, size1);
	REQUIRE(p1.ok());
	REQUIRE_EQ(toString(*p1.value().expr), std::string("a - b - c"));

	auto t2 = tokenize("a - (b - c)");
	std::size_t size2 = 0;
	auto p2 = parseExpression(t2.value(), 400, size2);
	REQUIRE(p2.ok());
	REQUIRE_EQ(toString(*p2.value().expr), std::string("a - (b - c)"));
}

TEST_CASE("printer: power right-associativity reflected") {
	auto t = tokenize("a ^ b ^ c");
	std::size_t size = 0;
	auto p = parseExpression(t.value(), 400, size);
	REQUIRE(p.ok());
	REQUIRE_EQ(toString(*p.value().expr), std::string("a^b^c"));

	auto t2 = tokenize("(a ^ b) ^ c");
	size = 0;
	auto p2 = parseExpression(t2.value(), 400, size);
	REQUIRE(p2.ok());
	REQUIRE_EQ(toString(*p2.value().expr), std::string("(a^b)^c"));
}

// -----------------------------------------------------------------------
// Printer
// -----------------------------------------------------------------------

TEST_CASE("printer: negative number base of power keeps parens") {
	REQUIRE_EQ(evalToString("(-2)^a"), std::string("(-2)^a"));
}

TEST_CASE("printer: negation of sum keeps parens") {
	auto s1 = evalToString("-(4*a+3)");
	bool s1ok = (s1 == "-(3 + 4*a)" || s1 == "-(4*a + 3)" ||
		s1 == "-(a*4 + 3)" || s1 == "-(3 + a*4)");
	REQUIRE(s1ok);
	REQUIRE(s1[0] == '-');
	REQUIRE(s1[1] == '(');

	auto s2 = evalToString("-(x+1)");
	bool s2ok = (s2 == "-(1 + x)" || s2 == "-(x + 1)");
	REQUIRE(s2ok);

	auto s3 = evalToString("2/-(4*a+3)");
	REQUIRE(s3.find("2") != std::string::npos);

	auto s4 = evalToString("-(x*y)");
	REQUIRE((s4 == "-x*y" || s4 == "-y*x"));
	REQUIRE_EQ(evalToString("-x"), std::string("-x"));
}

// -----------------------------------------------------------------------
// Printer: formatNumber and round-trip stability
// -----------------------------------------------------------------------

TEST_CASE("printer: number formatting strips trailing zeros and bare dots") {
	REQUIRE_EQ(evalToString("4"), std::string("4"));      // integer, no ".0"
	REQUIRE_EQ(evalToString("10/2"), std::string("5"));
	REQUIRE_EQ(evalToString("-7"), std::string("-7"));
	REQUIRE_EQ(evalToString("1/4"), std::string("0.25"));
	REQUIRE_EQ(evalToString("3/2"), std::string("1.5"));
}

TEST_CASE("printer: output is reparseable and idempotent") {
	// Printing then reparsing must reach a fixed point: the printer never
	// emits something the parser would read back as a different tree.
	const std::vector<std::string> exprs = {
		"a*b + c", "(a + b)*c", "a^b^c", "a - (b - c)",
		"-x*y", "(-2)^a", "a/(b/c)", "a/b/c", "f(a, b, c)",
	};
	for (const auto& e : exprs) {
		const std::string once = parsePrint(e);
		const std::string twice = parsePrint(once);
		REQUIRE_EQ(once, twice);
	}
}
