// test_capi.cpp - exercises the C ABI (calc_c.h) as a C consumer would.
//
// This is a SEPARATE binary from calc_tests on purpose: it links the calc_c
// shared library and includes only its public C header, so it tests exactly
// what a foreign caller sees -- the opaque handles, the flat result structs,
// the diagnostic codes, and the allocate/caller-frees ownership contract --
// without reaching into any C++ engine type. If this links and passes, the
// boundary is sound; if a future change leaks an engine symbol or breaks the
// ABI, this is what catches it.
//
// It runs under the same ctest invocation as the rest of the suite, so it is
// covered by the sanitizer job automatically: ASan verifies every result is
// freed through its matching deallocator (no leaks, no double-frees), and
// UBSan watches the marshaling arithmetic. That sanitizer coverage of the
// wrapper is the whole reason this lives in CI rather than as a one-off check.

#include "calc_c.h"
#include "test_framework.h"

#include <cmath>
#include <cstring>
#include <set>
#include <string>

using namespace test_framework;

namespace {

	// RAII guards so a failing REQUIRE can't leak the handle/result it was
	// inspecting -- without these, an assertion failure would skip the manual
	// free and ASan would report a leak that masks the real failure. Each
	// guard calls the exact deallocator the C contract specifies.
	struct Core {
		calc_core* h;
		explicit Core(calc_core* p) : h(p) {}
		~Core() { calc_core_free(h); }
		operator calc_core* () const { return h; }
	};
	struct Eval {
		calc_eval_result* r;
		explicit Eval(calc_eval_result* p) : r(p) {}
		~Eval() { calc_eval_result_free(r); }
		calc_eval_result* operator->() const { return r; }
		explicit operator bool() const { return r != nullptr; }
	};
	struct Cmd {
		calc_command_result* r;
		explicit Cmd(calc_command_result* p) : r(p) {}
		~Cmd() { calc_command_result_free(r); }
		calc_command_result* operator->() const { return r; }
		explicit operator bool() const { return r != nullptr; }
	};
	struct Plot {
		calc_plot* h;
		explicit Plot(calc_plot* p) : h(p) {}
		~Plot() { calc_plot_free(h); }
		operator calc_plot* () const { return h; }
	};

	std::string str(const char* s) { return s ? std::string(s) : std::string(); }

}  // namespace

TEST_CASE("capi: version is reported") {
	REQUIRE(calc_c_version() != nullptr);
	REQUIRE(str(calc_c_version()) == "1.0.0");
}

TEST_CASE("capi: arithmetic reduces to a number") {
	Core core(calc_core_new());
	REQUIRE(core.h != nullptr);
	Eval r(calc_evaluate_line(core, "2 + 3 * 4", 400));
	REQUIRE(r);
	REQUIRE(r->ok == 1);
	REQUIRE(r->has_value == 1);
	REQUIRE_APPROX(r->value, 14.0, 1e-12);
	REQUIRE(str(r->canonical) == "14");
}

TEST_CASE("capi: right-associative power") {
	Core core(calc_core_new());
	Eval r(calc_evaluate_line(core, "2 ^ 3 ^ 2", 400));
	REQUIRE(r->ok == 1);
	REQUIRE_APPROX(r->value, 512.0, 1e-9);
}

TEST_CASE("capi: symbolic cancellation has no numeric value") {
	Core core(calc_core_new());
	Eval r(calc_evaluate_line(core, "a + b - a", 400));
	REQUIRE(r->ok == 1);
	REQUIRE(r->has_value == 0);
	REQUIRE(str(r->canonical) == "b");
}

TEST_CASE("capi: like-term and exponent combining") {
	Core core(calc_core_new());
	Eval r1(calc_evaluate_line(core, "3*a + 2*a", 400));
	REQUIRE(str(r1->canonical) == "5*a");
	Eval r2(calc_evaluate_line(core, "a*a*b", 400));
	REQUIRE(str(r2->canonical) == "a^2*b");
}

TEST_CASE("capi: assignment reports the assigned name") {
	Core core(calc_core_new());
	Eval r(calc_evaluate_line(core, "f: x^2 + 3", 400));
	REQUIRE(r->ok == 1);
	REQUIRE(r->assigned_name != nullptr);
	REQUIRE(str(r->assigned_name) == "f");
	// A stored definition inlines on later use.
	Eval r2(calc_evaluate_line(core, "f + 1", 400));
	REQUIRE(str(r2->canonical) == "x^2 + 4");
}

TEST_CASE("capi: diagnostic carries code and span") {
	Core core(calc_core_new());
	Eval r(calc_evaluate_line(core, "1/0", 400));
	REQUIRE(r->ok == 0);
	REQUIRE(r->diag_code == 3001);  // DiagCode::DivisionByZero
	REQUIRE(r->span_end > r->span_begin);
	REQUIRE(r->canonical == nullptr);
}

TEST_CASE("capi: diagnostic detail payload round-trips") {
	Core core(calc_core_new());
	Eval r(calc_evaluate_line(core, "foo(2)", 400));
	REQUIRE(r->ok == 0);
	REQUIRE(r->diag_code == 3002);  // DiagCode::UnknownFunction
	REQUIRE(r->detail != nullptr);
	REQUIRE(str(r->detail) == "foo");
}

TEST_CASE("capi: expression-too-long is a diagnostic, not a crash") {
	Core core(calc_core_new());
	std::string deep = "1";
	for (int i = 0; i < 50; ++i) deep += "+(1";
	for (int i = 0; i < 50; ++i) deep += ")";
	Eval r(calc_evaluate_line(core, deep.c_str(), 8));  // tiny bound
	REQUIRE(r->ok == 0);
	REQUIRE(r->diag_code == 2010);  // DiagCode::ExpressionTooLong
}

TEST_CASE("capi: defined_names lists stored variables") {
	Core core(calc_core_new());
	{ Eval a(calc_evaluate_line(core, "f: x^2", 400)); }
	{ Eval b(calc_evaluate_line(core, "g: y + 1", 400)); }

	const char** names = nullptr;
	size_t count = 0;
	REQUIRE(calc_defined_names(core, &names, &count) == 1);
	REQUIRE(count == 2);
	std::set<std::string> got;
	for (size_t i = 0; i < count; ++i) got.insert(str(names[i]));
	calc_string_array_free(names, count);
	REQUIRE(got.count("f") == 1);
	REQUIRE(got.count("g") == 1);
}

TEST_CASE("capi: defined_names on empty session succeeds with zero") {
	Core core(calc_core_new());
	const char** names = reinterpret_cast<const char**>(0x1);  // sentinel
	size_t count = 99;
	REQUIRE(calc_defined_names(core, &names, &count) == 1);
	REQUIRE(count == 0);
	REQUIRE(names == nullptr);  // must be reset even on the empty path
}

TEST_CASE("capi: definition_of returns the simplified form, freed correctly") {
	Core core(calc_core_new());
	{ Eval a(calc_evaluate_line(core, "f: x + x + x", 400)); }
	const char* def = calc_definition_of(core, "f");
	REQUIRE(def != nullptr);
	REQUIRE(str(def) == "3*x");
	calc_string_free(def);  // the contract's deallocator for this string

	REQUIRE(calc_definition_of(core, "nope") == nullptr);  // undefined -> null
}

TEST_CASE("capi: clear empties the session") {
	Core core(calc_core_new());
	{ Eval a(calc_evaluate_line(core, "f: 1", 400)); }
	calc_clear(core);
	const char** names = nullptr;
	size_t count = 0;
	REQUIRE(calc_defined_names(core, &names, &count) == 1);
	REQUIRE(count == 0);
	calc_string_array_free(names, count);
}

TEST_CASE("capi: constants fold and are reserved") {
	const char* names[] = { "pi", "e" };
	const double vals[] = { 3.141592653589793, 2.718281828459045 };
	Core core(calc_core_new_with_constants(names, vals, 2));
	REQUIRE(core.h != nullptr);

	Eval r(calc_evaluate_line(core, "pi + 1", 400));
	REQUIRE(r->has_value == 1);
	REQUIRE_APPROX(r->value, 4.141592653589793, 1e-9);

	Eval bad(calc_evaluate_line(core, "pi: 3", 400));
	REQUIRE(bad->ok == 0);
	REQUIRE(bad->diag_code == 5006);  // DiagCode::ConstantReassignment
}

TEST_CASE("capi: default engine treats letters as free variables") {
	Core core(calc_core_new());  // no constants
	Eval r(calc_evaluate_line(core, "pi + pi", 400));
	REQUIRE(r->ok == 1);
	REQUIRE(r->has_value == 0);          // pi is just a symbol here
	REQUIRE(str(r->canonical) == "2*pi");
}

TEST_CASE("capi: compile and sample a plot") {
	Core core(calc_core_new());
	{ Eval a(calc_evaluate_line(core, "curve: y = x^2", 400)); }

	const char* axes[] = { "x", "y" };
	int diag = -1;
	Plot p(calc_compile_plot(core, "curve", axes, 2, /*clear=*/1, &diag));
	REQUIRE(p.h != nullptr);
	REQUIRE(diag == 0);
	REQUIRE(calc_plot_dimensions(p) == 2);

	// y = x^2: lhs-rhs is ~0 on the curve, nonzero (and finite) off it.
	const double on[]  = { 2.0, 4.0 };
	const double off[] = { 2.0, 0.0 };
	const double von  = calc_plot_eval(p, on, 2);
	const double voff = calc_plot_eval(p, off, 2);
	REQUIRE_APPROX(von, 0.0, 1e-9);
	REQUIRE(!std::isnan(voff));
	REQUIRE(std::abs(voff) > 1e-9);
}

TEST_CASE("capi: plot compile failures report a code, not a crash") {
	Core core(calc_core_new());
	const char* axes[] = { "x", "y" };

	int diag = -1;
	Plot missing(calc_compile_plot(core, "nope", axes, 2, 1, &diag));
	REQUIRE(missing.h == nullptr);
	REQUIRE(diag == 5002);  // DiagCode::NoSuchVariable

	{ Eval a(calc_evaluate_line(core, "notEq: x^2", 400)); }
	diag = -1;
	Plot notEq(calc_compile_plot(core, "notEq", axes, 2, 1, &diag));
	REQUIRE(notEq.h == nullptr);
	REQUIRE(diag == 5003);  // DiagCode::GraphTargetNotEquation
}

TEST_CASE("capi: command parsing splits trimmed args") {
	Cmd c(calc_parse_command("/graph(x, y, -3, 3, -1, 9, curve)"));
	REQUIRE(c->ok == 1);
	REQUIRE(str(c->name) == "graph");
	REQUIRE(c->arg_count == 7);
	REQUIRE(str(c->args[0]) == "x");
	REQUIRE(str(c->args[6]) == "curve");
}

TEST_CASE("capi: bare command has no args") {
	Cmd c(calc_parse_command("/list"));
	REQUIRE(c->ok == 1);
	REQUIRE(str(c->name) == "list");
	REQUIRE(c->arg_count == 0);
	REQUIRE(c->args == nullptr);
}

TEST_CASE("capi: malformed command is a diagnostic") {
	Cmd c(calc_parse_command("not a command"));
	REQUIRE(c->ok == 0);
	REQUIRE(c->diag_code == 4000);  // DiagCode::CommandMustStartWithSlash
}

TEST_CASE("capi: NULL inputs are handled, not crashed on") {
	// Freeing NULL is a documented no-op on every deallocator.
	calc_core_free(nullptr);
	calc_plot_free(nullptr);
	calc_eval_result_free(nullptr);
	calc_command_result_free(nullptr);
	calc_string_free(nullptr);
	calc_string_array_free(nullptr, 0);

	// NULL handle / input arguments return failure sentinels, never UB.
	REQUIRE(calc_evaluate_line(nullptr, "1", 400) == nullptr);
	Core core(calc_core_new());
	REQUIRE(calc_evaluate_line(core, nullptr, 400) == nullptr);
	REQUIRE(std::isnan(calc_plot_eval(nullptr, nullptr, 0)));
	REQUIRE(calc_plot_dimensions(nullptr) == 0);
	REQUIRE(calc_definition_of(nullptr, "x") == nullptr);
}

int main() {
	return runAll();
}
