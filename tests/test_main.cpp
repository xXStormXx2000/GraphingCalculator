// test_main.cpp - entry point for the engine test suite.
//
// The cases themselves live in the per-component files (test_tokenizer.cpp,
// test_evaluator.cpp, test_simplifier.cpp, test_printer.cpp, test_repl.cpp,
// test_plot.cpp); the framework auto-registers them via static initializers,
// so this file only has to run them. The C ABI boundary is a separate binary
// (test_capi.cpp / calc_c_tests). See test_support.h for the shared helpers.

#include "test_framework.h"

int main() {
	return test_framework::runAll();
}
