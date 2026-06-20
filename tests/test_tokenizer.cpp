#include "test_support.h"

// -----------------------------------------------------------------------
// Tokenizer: edge cases
// -----------------------------------------------------------------------

TEST_CASE("tokenizer: basic tokens") {
	auto r = tokenize("1 + 2 * 3");
	REQUIRE(r.ok());
	auto& toks = r.value();
	REQUIRE_EQ(toks.size(), std::size_t{ 6 });  // 1 + 2 * 3 EOF
	REQUIRE_EQ(toks[0].kind, TokenKind::Number);
	REQUIRE_APPROX(toks[0].number, 1.0, 1e-12);
	REQUIRE_EQ(toks[1].kind, TokenKind::Plus);
}

TEST_CASE("tokenizer: numbers with decimals") {
	auto r = tokenize("3.14 .5 0.0");
	REQUIRE(r.ok());
	auto& toks = r.value();
	REQUIRE_APPROX(toks[0].number, 3.14, 1e-12);
	REQUIRE_APPROX(toks[1].number, 0.5, 1e-12);
	REQUIRE_APPROX(toks[2].number, 0.0, 1e-12);
}

TEST_CASE("tokenizer: scientific notation") {
	auto r = tokenize("1e4 1e-4 1.5e3 2.5E-2 6.022e23");
	REQUIRE(r.ok());
	auto& toks = r.value();
	REQUIRE_EQ(toks[0].kind, TokenKind::Number);
	REQUIRE_APPROX(toks[0].number, 1e4, 1e-9);
	REQUIRE_APPROX(toks[1].number, 1e-4, 1e-18);
	REQUIRE_APPROX(toks[2].number, 1.5e3, 1e-9);
	REQUIRE_APPROX(toks[3].number, 2.5e-2, 1e-12);
	REQUIRE_APPROX(toks[4].number, 6.022e23, 1e15);
	// A leading-dot mantissa with an exponent is one number.
	auto r2 = tokenize(".5e3");
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[0].kind, TokenKind::Number);
	REQUIRE_APPROX(r2.value()[0].number, 500.0, 1e-9);
}

TEST_CASE("tokenizer: e disambiguation (constant vs exponent)") {
	// A bare `e` with no following digit must NOT be swallowed into a numeric
	// literal -- it stays a separate constant identifier. With no implicit
	// multiplication, `2e` is therefore Number followed by Identifier, which
	// is a parse error at the next layer (asserted below), NOT a product.
	auto r1 = tokenize("2e");
	REQUIRE(r1.ok());
	REQUIRE_EQ(r1.value()[0].kind, TokenKind::Number);
	REQUIRE_APPROX(r1.value()[0].number, 2.0, 1e-12);
	REQUIRE_EQ(r1.value()[1].kind, TokenKind::Identifier);

	auto r2 = tokenize("e");           // bare constant
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[0].kind, TokenKind::Identifier);

	// A well-formed exponent (sign + digit after `e`) IS one number.
	auto r3 = tokenize("2e+1");
	REQUIRE(r3.ok());
	REQUIRE_EQ(r3.value().size(), std::size_t{ 2 });  // Number EOF
	REQUIRE_EQ(r3.value()[0].kind, TokenKind::Number);
	REQUIRE_APPROX(r3.value()[0].number, 20.0, 1e-9);
}

TEST_CASE("parser: bare e after a number is not implicit multiplication") {
	// `2e` tokenizes as Number, Identifier with no operator between them.
	// Since implicit multiplication is not supported, this must be rejected
	// at parse time rather than silently treated as 2 * e.
	auto t = tokenize("2e");
	REQUIRE(t.ok());
	std::size_t size = 0;
	auto p = parseExpression(t.value(), 400, size);
	REQUIRE(!p.ok());
}

TEST_CASE("eval: scientific notation end to end") {
	REQUIRE_APPROX(evalToNumber("1e-4"), 1e-4, 1e-18);
	REQUIRE_APPROX(evalToNumber("1e-4 + 1"), 1.0001, 1e-12);
	REQUIRE_APPROX(evalToNumber("2.5e3 * 2"), 5000.0, 1e-9);
	REQUIRE_APPROX(evalToNumber("6.022e23 / 2"), 3.011e23, 1e15);
	// `e` still works as the constant alongside exponent literals.
	REQUIRE_APPROX(evalToNumber("2*e"), 2.0 * 2.718281828459045, 1e-9);
	REQUIRE_APPROX(evalToNumber("1e0"), 1.0, 1e-12);
}

TEST_CASE("tokenizer: identifiers vs slash command") {
	auto r1 = tokenize("/list");
	REQUIRE(r1.ok());
	REQUIRE_EQ(r1.value()[0].kind, TokenKind::Slash_Cmd);

	auto r2 = tokenize("4 / 2");
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[1].kind, TokenKind::Slash);
}

TEST_CASE("tokenizer: malformed numbers") {
	// "1.2.3" tokenizes as two adjacent numbers (1.2 then .3) — the
	// tokenizer accepts each, but the parser must reject the resulting
	// stream because there's no operator between them.
	auto r = tokenize("1.2.3");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().size(), std::size_t{ 3 });  // 1.2  .3  EOF
	std::size_t size = 0;
	auto p = parseExpression(r.value(), 400, size);
	REQUIRE(!p.ok());
}

TEST_CASE("tokenizer: unknown character") {
	auto r = tokenize("a @ b");
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::UnexpectedCharacter);
	REQUIRE(r.error().detail == "@");
}

// -----------------------------------------------------------------------
// Parser
// -----------------------------------------------------------------------

TEST_CASE("parser: precedence") {
	REQUIRE_EQ(evalToString("1 + 2 * 3"), std::string("7"));
	REQUIRE_EQ(evalToString("(1 + 2) * 3"), std::string("9"));
}

TEST_CASE("parser: right-associative power") {
	REQUIRE_APPROX(evalToNumber("2 ^ 3"), 8.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("2 ^ 3 ^ 2"), 512.0, 1e-12);  // 2^(3^2) = 2^9
	REQUIRE_APPROX(evalToNumber("(2 ^ 3) ^ 2"), 64.0, 1e-12);
}

TEST_CASE("parser: unary minus everywhere") {
	REQUIRE_APPROX(evalToNumber("-3"), -3.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("2 + -3"), -1.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("2 * -3"), -6.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("-2 ^ 2"), -4.0, 1e-12);   // -(2^2)
	REQUIRE_APPROX(evalToNumber("(-2) ^ 2"), 4.0, 1e-12);
}

TEST_CASE("parser: function calls") {
	REQUIRE_APPROX(evalToNumber("sin(0)"), 0.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("log(2, 64)"), 6.0, 1e-12);
	REQUIRE_APPROX(evalToNumber("sqrt(16)"), 4.0, 1e-12);
}

TEST_CASE("parser: nested parens") {
	REQUIRE_APPROX(evalToNumber("((1 + 2) * (3 + 4))"), 21.0, 1e-12);
}

TEST_CASE("parser: errors") {
	CalculatorCore core;
	REQUIRE(!core.evaluateLine("1 +", 400).ok());
	REQUIRE(!core.evaluateLine("1 ** 2", 400).ok());
	REQUIRE(!core.evaluateLine("(1 + 2", 400).ok());
}

TEST_CASE("tokenizer: lone dot is an unexpected character") {
	// A '.' only starts a number when a digit follows; otherwise it is illegal.
	auto r = tokenize(".");
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::UnexpectedCharacter);

	auto r2 = tokenize(".x");
	REQUIRE(!r2.ok());
	REQUIRE(r2.error().code == DiagCode::UnexpectedCharacter);
}

TEST_CASE("tokenizer: underscores belong to identifiers") {
	// Several built-in constants (k_B, N_A, q_e) rely on this.
	auto r = tokenize("k_B + N_A");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value()[0].kind, TokenKind::Identifier);
	REQUIRE_EQ(r.value()[0].text, std::string("k_B"));
	REQUIRE_EQ(r.value()[2].text, std::string("N_A"));
}

TEST_CASE("tokenizer: empty and whitespace-only input is just EndOfInput") {
	auto r = tokenize("");
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value().size(), std::size_t{ 1 });
	REQUIRE_EQ(r.value()[0].kind, TokenKind::EndOfInput);

	auto r2 = tokenize("   \t  ");
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value().size(), std::size_t{ 1 });
	REQUIRE_EQ(r2.value()[0].kind, TokenKind::EndOfInput);
}

TEST_CASE("tokenizer: slash is a command prefix only as the first token") {
	auto r = tokenize("/2");          // leading slash -> command prefix
	REQUIRE(r.ok());
	REQUIRE_EQ(r.value()[0].kind, TokenKind::Slash_Cmd);

	auto r2 = tokenize("8 / 2 / 2");  // every later slash is division
	REQUIRE(r2.ok());
	REQUIRE_EQ(r2.value()[1].kind, TokenKind::Slash);
	REQUIRE_EQ(r2.value()[3].kind, TokenKind::Slash);
}

// -----------------------------------------------------------------------
// Parser: error paths not covered elsewhere
// -----------------------------------------------------------------------

TEST_CASE("parser: more than one '=' is rejected") {
	CalculatorCore core;
	auto r = core.evaluateLine("x = y = z", 400);
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::MultipleEquals);
}

TEST_CASE("parser: empty side of an equation is rejected") {
	CalculatorCore core;
	auto before = core.evaluateLine("= 3", 400);
	REQUIRE(!before.ok());
	REQUIRE(before.error().code == DiagCode::ExpectedExpressionBeforeEquals);

	auto after = core.evaluateLine("x =", 400);
	REQUIRE(!after.ok());
	REQUIRE(after.error().code == DiagCode::ExpectedExpressionAfterEquals);
}

TEST_CASE("parser: dangling colon is rejected") {
	CalculatorCore core;
	auto r = core.evaluateLine("a:", 400);
	REQUIRE(!r.ok());
	REQUIRE(r.error().code == DiagCode::ExpectedExpressionAfterColon);
}

TEST_CASE("parser: empty parens and trailing comma in an argument list") {
	CalculatorCore core;
	REQUIRE(!core.evaluateLine("()", 400).ok());
	REQUIRE(!core.evaluateLine("sin(1,)", 400).ok());
	REQUIRE(!core.evaluateLine("sin(,1)", 400).ok());
}

TEST_CASE("parser: assignment combined with an equation") {
	auto t = tokenize("curve: y = x^2");
	REQUIRE(t.ok());
	std::size_t size = 0;
	auto p = parseExpression(t.value(), 400, size);
	REQUIRE(p.ok());
	REQUIRE_EQ(p.value().assignTo, std::string("curve"));
	// The stored expression is the equation itself.
	REQUIRE(toString(*p.value().expr).find('=') != std::string::npos);
}
