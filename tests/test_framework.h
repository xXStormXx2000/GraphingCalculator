// Tiny test framework

#ifndef CALC_TEST_FRAMEWORK_H
#define CALC_TEST_FRAMEWORK_H

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test_framework {

	struct TestCase {
		std::string name;
		std::function<void()> fn;
	};

	inline std::vector<TestCase>& registry() {
		static std::vector<TestCase> r;
		return r;
	}

	inline int& failureCount() {
		static int c = 0;
		return c;
	}

	inline int& assertionCount() {
		static int c = 0;
		return c;
	}

	struct Registrar {
		Registrar(std::string name, std::function<void()> fn) {
			registry().push_back({ std::move(name), std::move(fn) });
		}
	};

	inline void reportFailure(const char* file, int line, const std::string& expr) {
		failureCount()++;
		std::cerr << "  FAIL: " << file << ":" << line;
		std::cerr << "\n        " << expr << "\n";
	}

	inline int runAll() {
		int passed = 0;
		int failed = 0;
		for (auto& tc : registry()) {
			const int beforeFailures = failureCount();
			std::cout << "[ RUN  ] " << tc.name << "\n";
			try {
				tc.fn();
			}
			catch (const std::exception& e) {
				failureCount()++;
				std::cerr << "  FAIL: uncaught exception: " << e.what() << "\n";
			}
			catch (...) {
				failureCount()++;
				std::cerr << "  FAIL: uncaught unknown exception\n";
			}
			if (failureCount() == beforeFailures) {
				std::cout << "[  OK  ] " << tc.name << "\n";
				++passed;
			}
			else {
				std::cout << "[ FAIL ] " << tc.name << "\n";
				++failed;
			}
		}
		std::cout << "\n" << assertionCount() << " assertions, "
			<< passed << " passed, " << failed << " failed.\n";
		return failed == 0 ? 0 : 1;
	}

}  // namespace test_framework

#define CALC_CONCAT2(a, b) a##b
#define CALC_CONCAT(a, b) CALC_CONCAT2(a, b)

#define TEST_CASE(name)                                                                                             \
static void CALC_CONCAT(_test_fn_, __LINE__)();                                                                     \
    static ::test_framework::Registrar CALC_CONCAT(_test_reg_, __LINE__){ name, &CALC_CONCAT(_test_fn_, __LINE__)}; \
    static void CALC_CONCAT(_test_fn_, __LINE__)()

#define REQUIRE(expr) do {                                          \
    ::test_framework::assertionCount()++;                           \
    if (!(expr)) {                                                  \
        ::test_framework::reportFailure(__FILE__, __LINE__, #expr); \
}                                                                   \
} while (0)

#define REQUIRE_EQ(a, b) do {                                       \
    ::test_framework::assertionCount()++;                           \
    auto _a = (a);                                                  \
    auto _b = (b);                                                  \
    if (!(_a == _b)) {                                              \
        std::ostringstream _os;                                     \
        _os << #a " == " #b " (got: ";                              \
        _os << _a << " != " << _b << ")";                           \
        ::test_framework::reportFailure(__FILE__, __LINE__, _os.str()); \
}                                                               \
} while (0)

#define REQUIRE_APPROX(a, b, tol) do {                              \
    ::test_framework::assertionCount()++;                           \
    double _a = (a);                                                \
    double _b = (b);                                                \
    /* Written as the negation of the pass condition so that a NaN  \
       operand FAILS. `std::abs(_a - _b) <= tol` is false when      \
       either side is NaN, and !false == true triggers the failure. \
       The naive form `std::abs(_a - _b) > tol` is also false for   \
       NaN, which would SILENTLY PASS -- the bug this guards. */     \
    if (!(std::abs(_a - _b) <= (tol))) {                            \
        std::ostringstream _os;                                     \
        _os << #a " ~ " #b " (got: " << _a << " vs " << _b << ")";  \
        ::test_framework::reportFailure(__FILE__, __LINE__, _os.str()); \
}                                                               \
} while (0)

#endif