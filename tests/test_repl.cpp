#include "test_support.h"

// -----------------------------------------------------------------------
// REPL integration
// -----------------------------------------------------------------------

TEST_CASE("repl: assignment and lookup") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine("a: 4 * 5"), std::string("a: 20"));
	REQUIRE_EQ(r.processLine("a + 3"), std::string("23"));
}

TEST_CASE("repl: assignment with variables") {
	std::stringstream in, out;
	Repl r(in, out);
	auto stored = r.processLine("f: x ^ 2 + 3");
	bool storedOk = (stored == "f: x^2 + 3" || stored == "f: 3 + x^2");
	REQUIRE(storedOk);
	auto resp = r.processLine("f + 1");
	bool respOk = (resp == "x^2 + 4" || resp == "4 + x^2");
	REQUIRE(respOk);
}

TEST_CASE("repl: list and clear") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("a: 1");
	r.processLine("b: 2");
	auto listed = r.processLine("/list");
	REQUIRE(listed.find("a: 1") != std::string::npos);
	REQUIRE(listed.find("b: 2") != std::string::npos);
	REQUIRE_EQ(r.processLine("/clear"), std::string("All variables cleared"));
	REQUIRE_EQ(r.processLine("/list"), std::string("(no variables defined)"));
}

TEST_CASE("repl: division by zero produces a friendly error") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("1/0");
	REQUIRE(resp.find("error") != std::string::npos);
}

TEST_CASE("repl: graph requires defined equation variable") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("/graph(x, y, -1, 1, -1, 1, undefined_var)");
	REQUIRE(resp.find("error") != std::string::npos);
}

TEST_CASE("repl: graph happy path - parabola y = x^2") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine("p: y = x^2").find("p:"), static_cast<size_t>(0));
	auto resp = r.processLine("/graph(x, y, -3, 3, -1, 9, p)");
	REQUIRE(resp.find('#') != std::string::npos);
	REQUIRE(resp.find('|') != std::string::npos);
	REQUIRE(resp.find('-') != std::string::npos);
}

TEST_CASE("repl: empty input yields empty response") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine(""), std::string(""));
	REQUIRE_EQ(r.processLine("   "), std::string(""));
}

TEST_CASE("repl: help") {
	std::stringstream in, out;
	Repl r(in, out);
	auto h = r.processLine("/help(+)");
	REQUIRE(h.find("Addition") != std::string::npos);
	auto h2 = r.processLine("/help(sin)");
	REQUIRE(h2.find("sine") != std::string::npos);
}

// -----------------------------------------------------------------------
// Diagnostic carets in REPL output
// -----------------------------------------------------------------------

TEST_CASE("diagnostic: tokenizer error has caret") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("a @ b");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("a @ b") != std::string::npos);
	REQUIRE(resp.find("    ^") != std::string::npos);
}

TEST_CASE("diagnostic: parser error has caret") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("(1 + 2");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("(1 + 2") != std::string::npos);
	REQUIRE(resp.find('^') != std::string::npos);
}

TEST_CASE("diagnostic: evaluator error has caret") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("1/0");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("1/0") != std::string::npos);
	REQUIRE(resp.find('^') != std::string::npos);
}

TEST_CASE("diagnostic: graph command errors point at offending arg") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("/graph(x, y, hello, 10, 0, 10, p)");
	REQUIRE(resp.find("x_min must be a number") != std::string::npos);
}

TEST_CASE("diagnostic: cross-line evaluator error suppresses caret cleanly") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("f: 1/x");
	r.processLine("x: 0");
	auto resp = r.processLine("f");
	REQUIRE(resp.find("error:") != std::string::npos);
}

// -----------------------------------------------------------------------
// Self-referential definitions (must be rejected at assignment time)
// -----------------------------------------------------------------------

TEST_CASE("repl: self-reference rejected at assignment") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("a: a + 1");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("itself") != std::string::npos);
	auto listed = r.processLine("/list");
	REQUIRE(listed.find("a:") == std::string::npos);
}

TEST_CASE("repl: self-reference in deeply nested expression rejected") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("foo: 2 * (3 + sin(foo))");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("itself") != std::string::npos);
}

TEST_CASE("repl: indirect self-reference through another variable rejected") {
	std::stringstream in, out;
	Repl r(in, out);
	auto first = r.processLine("b: a + 1");
	REQUIRE(first.find("error:") == std::string::npos);
	auto resp = r.processLine("a: b + 1");
	REQUIRE(resp.find("error:") != std::string::npos);
	REQUIRE(resp.find("'a'") != std::string::npos);
}

TEST_CASE("repl: legitimate chained assignments still work") {
	std::stringstream in, out;
	Repl r(in, out);
	REQUIRE_EQ(r.processLine("a: 5"), std::string("a: 5"));
	REQUIRE_EQ(r.processLine("b: a + 1"), std::string("b: 6"));
	REQUIRE_EQ(r.processLine("d: a * b"), std::string("d: 30"));
	REQUIRE_EQ(r.processLine("a: d + 1"), std::string("a: 31"));
}

TEST_CASE("repl: built-in constants cannot be reassigned") {
	std::stringstream in, out;
	Repl r(in, out);
	// Names in the constant table are reserved; assigning to one is an error
	// rather than a silently-shadowed (and unreachable) definition.
	REQUIRE(r.processLine("c: 5").find("reserved constant") != std::string::npos);
	REQUIRE(r.processLine("pi: 3").find("reserved constant") != std::string::npos);
	REQUIRE(r.processLine("h: 1").find("reserved constant") != std::string::npos);
	// A non-constant single letter still works.
	REQUIRE_EQ(r.processLine("d: 5"), std::string("d: 5"));
}

TEST_CASE("repl: equation with axis variables does not look self-referential") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("p: y = x^2");
	REQUIRE(resp.find("error:") == std::string::npos);
	REQUIRE(resp.find("p:") == 0);
}

TEST_CASE("repl: equation variable not inlined into another equation") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("curve: y = x^2");
	auto resp = r.processLine("result: curve = x");
	REQUIRE(resp.find("error:") == std::string::npos);
	const std::size_t firstEq = resp.find('=');
	REQUIRE(firstEq != std::string::npos);
	REQUIRE(resp.find('=', firstEq + 1) == std::string::npos);
}

TEST_CASE("repl: bare reference to equation variable shows the equation") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("p: y = x^2");
	auto resp = r.processLine("p");
	REQUIRE(resp.find("y") != std::string::npos);
	REQUIRE(resp.find("x^2") != std::string::npos);
	REQUIRE(resp.find('=') != std::string::npos);
}

TEST_CASE("repl: equation can be copied to another name") {
	std::stringstream in, out;
	Repl r(in, out);
	r.processLine("p: y = x^2");
	auto resp = r.processLine("q: p");
	REQUIRE(resp.find("q:") == 0);
	REQUIRE(resp.find("y") != std::string::npos);
	REQUIRE(resp.find("x^2") != std::string::npos);
	auto qShow = r.processLine("q");
	REQUIRE(qShow.find("y") != std::string::npos);
	REQUIRE(qShow.find("x^2") != std::string::npos);
}

// -----------------------------------------------------------------------
// REPL help and constants
// -----------------------------------------------------------------------

TEST_CASE("repl: help on a built-in constant shows value and description") {
	std::stringstream in, out;
	Repl r(in, out);
	auto resp = r.processLine("/help(pi)");
	REQUIRE(resp.find("pi") != std::string::npos);
	REQUIRE(resp.find("3.14") != std::string::npos);
	REQUIRE(resp.find("circumference") != std::string::npos);
}

TEST_CASE("repl: new constants work in expressions") {
	REQUIRE(evalToString("tau").find("6.28") == 0);
	REQUIRE(evalToString("phi").find("1.61") == 0);
	REQUIRE(evalToString("q_e").find("1.6") == 0);
}

// -----------------------------------------------------------------------
// StringTable (frontend): previously had no direct tests
// -----------------------------------------------------------------------

TEST_CASE("StringTable: a missing file loads nothing and is not fatal") {
	StringTable t;
	REQUIRE(!t.loadFromFile("this_file_does_not_exist_98342.txt"));
	REQUIRE(t.empty());
	REQUIRE(!t.get("anything").has_value());
}

TEST_CASE("StringTable: keys, comments, blanks, trimming, and escapes") {
	const std::string path = "tmp_stringtable_test.txt";
	writeTempFile(path,
		"# a comment line\n"
		"\n"
		"greeting = hello\n"
		"  spaced   =   hello world  \n"   // outer ws trimmed, inner preserved
		"esc = a\\nb\n"                     // \n -> real newline
		"bs = a\\\\b\n"                     // \\ -> single backslash
		"other = a\\tb\n"                   // unknown escape left as-is
		"no equals sign here\n"             // skipped
		"= valueonly\n"                     // empty key, skipped
	);

	StringTable t;
	REQUIRE(t.loadFromFile(path));
	REQUIRE(!t.empty());

	REQUIRE_EQ(*t.get("greeting"), std::string("hello"));
	REQUIRE_EQ(*t.get("spaced"), std::string("hello world"));
	REQUIRE_EQ(*t.get("esc"), std::string("a\nb"));
	REQUIRE_EQ(*t.get("bs"), std::string("a\\b"));
	REQUIRE_EQ(*t.get("other"), std::string("a\\tb"));

	REQUIRE(!t.get("no equals sign here").has_value());
	REQUIRE(!t.get("").has_value());
	REQUIRE(!t.get("missing").has_value());

	std::remove(path.c_str());
}
