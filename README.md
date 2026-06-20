# Graphing Calculator

[![CI](https://github.com/xXStormXx2000/GraphingCalculator/actions/workflows/ci.yml/badge.svg)](https://github.com/xXStormXx2000/GraphingCalculator/actions/workflows/ci.yml)

A command-line graphing calculator with a REPL, ASCII plots, variable
storage, and algebraic simplification across symbolic terms. Written in
C++20 with no external dependencies.

## Build

```sh
cmake -B build -S .
cmake --build build
./build/calculator      # run the REPL
ctest --test-dir build  # run the test suite
```

C++20 is the only requirement. There are no third-party libraries; the
test suite uses a small embedded framework with a Catch2-compatible API.

## Usage

```
>> 2 + 3 * 4
14
>> 2 ^ 3 ^ 2          # right-associative: 2^(3^2) = 2^9
512
>> f: x^2 + 3         # store an expression
f: x^2 + 3
>> f + 1
x^2 + 4
>> a + b - a          # cancellation across symbolic terms
b
>> a * b / a
b
>> 3*a + 2*a          # like-term coefficient combining
5*a
>> a*a*b              # exponent combining
a^2*b
>> /help(pi)          # built-in constant help
pi = 3.141592654
  Pi - ratio of a circle's circumference to its diameter.
>> curve: y = x^2
curve: y = x^2
>> /graph(x, y, -3, 3, -1, 9, curve)
```

### Expressions

Operators are `+ - * / ^` with the usual precedence; `^` is
right-associative. Unary minus is allowed in any position (`2 + -3`,
`2 * -x`, `f(-1)`). Assignment uses `:` — `a: 4*5` stores `20` under `a`,
and stored names are inlined wherever they are used later.

### Expression size limit

Input is bounded by a maximum AST size (400 nodes by default) so that
pathological input — deeply nested parentheses, very long operator chains —
cannot exhaust the stack. Exceeding it is a normal diagnostic
(`ExpressionTooLong`), not a crash. The bound applies after variables are
inlined, so a chain of definitions that expands to an oversized tree is
caught at the point of expansion, not only when typed.

A stored definition is sized by its *simplified* form: `a: x + x + ... + x`
collapses to `200*x` and counts as a handful of nodes, not the length of
what you typed, so it inlines cheaply wherever it is used afterward. The
limit is a `m_maxSize` field on the frontend, so a different consumer of the
engine can raise or lower it; `evaluateLine` takes the bound as a parameter.

### Functions and constants

Functions: `sin cos tan asin acos atan sqrt abs log root`, where
`log(base, x)` is the logarithm of `x` in the given base and
`root(n, x)` is the n-th root of `x`.

Constants: `pi tau e phi G c h hbar k_B N_A R q_e`, with physical
constants using CODATA 2018 / SI 2019 values. They are spelled bare (no
sigil), so single letters like `c`, `e`, `h`, `G`, and `R` resolve to
their constant values and are not available as variable names.

These constants are not built into the engine — they are supplied by the
console frontend (`calc::defaultConstants()`) and injected when the engine
is constructed. The engine itself ships with no constants; a different
consumer chooses its own set, its own values, and its own spelling, or
none at all. See "Using the engine as a library" below.

### Commands

```
/graph(x_name, y_name, x_min, x_max, y_min, y_max, equation_var)
/list                 list all defined variables
/clear                remove all defined variables
/help(topic)          help for an operator, function, or command
/exit                 quit (/quit also works)
```

`/graph` plots a stored equation. Bound arguments may be any expression
that evaluates to a number, so `/graph(x, y, -pi, pi, -1, 1, wave)` works.

## Architecture

The program is split into a self-contained calculation engine and a thin
frontend, separated by a single public header.

```
                          frontend (calc)
                    Repl  +  Grapher  +  data files
                                |
                                |  CalculatorCore.h  (the only seam)
                                v
                          engine (calc::core)
      Tokenizer -> Parser -> Simplifier -> bytecode -> PlotFunctor
                                |
                       Evaluation / Printing
```

### Engine — `calc::core`

Everything reachable through `CalculatorCore.h`. It owns the durable state
(the user's variable definitions) and exposes a small surface:
`evaluateLine`, `definedNames` / `definitionOf` / `clear`, `compilePlot`,
and the free function `parseCommand`. It contains no I/O and no
user-facing text — errors are reported as a `DiagCode` plus an optional
detail payload. The engine is hidden behind a pimpl, so the frontend never
sees the AST or any other internal type.

| File | Responsibility |
| --- | --- |
| `Types.h` | tokens, source spans, `Diagnostic`, `Result<T>` |
| `DiagCode.h` | stable integer identifiers for every diagnostic |
| `Tokenizer.{h,cpp}` | character-class scanner |
| `Parser.{h,cpp}` | Pratt parser; precedence, unary minus, right-associative `^` |
| `Ast.h` | `std::variant` of node kinds plus factory helpers |
| `Builtins.{h,cpp}` | function table, `ConstantTable` type, free-variable collection |
| `Simplifier.{h,cpp}` | constant folding, identities, cancellation, denominator clearing |
| `Printer.{h,cpp}` | precedence-aware rendering back to a string |
| `VMbytecode.{h,cpp}` | compiles an AST to stack-machine bytecode and runs it |
| `CalculatorCore.{h,cpp}` | the public engine surface and command parser |

### Frontend — `calc`

The console interface. It coordinates: read a line, ask the engine to
evaluate it or dispatch a command, format the result, print it. All
user-facing text lives in data files (see below), so the frontend holds no
hardcoded English beyond compiled-in fallbacks.

| File | Responsibility |
| --- | --- |
| `Repl.{h,cpp}` | per-line dispatch, command handling, result formatting |
| `Grapher.{h,cpp}` | ASCII plot renderer |
| `StringTable.{h,cpp}` | flat key/value file loader for the data files |
| `main.cpp` | entry point |

### C ABI — `calc_c`

An optional `extern "C"` wrapper that exposes the engine to non-C++ callers as
a shared library. It is another consumer of `Calc::core`, like the frontend,
not part of the engine. See "From other languages (C ABI)" below for the
design and a usage example.

| File | Responsibility |
| --- | --- |
| `capi/calc_c.h` | pure-C public header (opaque handles, result structs) |
| `capi/calc_c.cpp` | the marshaling shim; the only file that sees both worlds |

## Using the engine as a library

The calculation engine is a self-contained, reusable library; the console
program is simply its first consumer. The engine has no I/O, no user-facing
text, and no dependency on the frontend, so another project can link it and
drive it through `CalculatorCore.h` alone.

The build exports the engine as the target `Calc::core`. There are two ways
to consume it, and the target name is the same either way, so consuming code
is identical regardless of which you choose.

**Vendored (`add_subdirectory`).** Drop this repository into your own tree
(for example as a Git submodule) and pull it in:

```cmake
add_subdirectory(GraphingCalculator)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Calc::core)
```

When consumed this way the calculator binary, the frontend, and the tests are
not built — only the engine. (They are gated behind a top-level-project check,
so they build only when this repository is the project being built directly.)

**Installed (`find_package`).** Install once, then locate the package from any
unrelated project:

```sh
cmake -B build -S .
cmake --build build
cmake --install build --prefix /your/install/prefix
```

```cmake
find_package(Calc 1.0 REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Calc::core)
```

If the package is installed to a non-standard prefix, point CMake at it with
`-DCMAKE_PREFIX_PATH=/your/install/prefix` when configuring the consumer.

### Public surface

Only three headers are installed: `CalculatorCore.h` (the engine API),
`Types.h` (which it includes, for `Result` and `Diagnostic`), and `DiagCode.h`
(which `Types.h` includes, for the diagnostic codes). Everything else in
`src/core` — the AST, parser, simplifier, bytecode VM — is internal and not
installed: the engine is hidden behind a pimpl, so consumers never see those
types.

A minimal consumer:

```cpp
#include "CalculatorCore.h"
#include <iostream>

int main() {
    calc::core::CalculatorCore engine;  // no constants
    auto result = engine.evaluateLine("3*a + 2*a");
    if (result) {
        std::cout << result.value().canonical << '\n';  // prints: 5*a
    }
}
```

`evaluateLine` returns a `Result`; on success its `canonical` field holds the
printed form, and `value` additionally holds a number when the expression
reduced to one (no free variables remained). On failure the `Result` carries a
`Diagnostic` — a `DiagCode` plus an optional detail payload — leaving any
user-facing wording to the caller.

#### Constants

The engine recognizes no constants by default: every identifier is a free
variable, so a default-constructed engine treats `pi` or `c` as ordinary
symbols. Which constants exist, what they equal, and how they are spelled is a
caller decision, passed to the constructor as a `ConstantTable` (a
`std::unordered_map<std::string, double>`). Each name in it folds to its value
during simplification and is reserved — it cannot be redefined.

```cpp
calc::core::CalculatorCore engine({
    {"pi", 3.14159265358979},
    {"e",  2.71828182845905},
});
engine.evaluateLine("pi + 1");   // -> 4.14159...; pi is no longer a variable
```

This keeps the engine neutral on a question that is really presentation: the
console frontend likes bare names (`c` is the speed of light), but a consumer
doing finance or working in another unit system can supply its own set — or
pass an empty table to keep every letter available as a variable. The frontend's
`calc::defaultConstants()` is the math/physics set the console program injects,
and a convenient starting point to copy or adapt. (Note `CalculatorCore({})` is
ambiguous between the move and table constructors; spell an empty set as
`CalculatorCore(calc::core::CalculatorCore::ConstantTable{})`.)

#### Diagnostics
The engine emits diagnostic codes, never text, so the wording is the one 
piece of presentation a consumer must supply. `DiagCode.h` (installed, so
you already have it) enumerates every code the engine can emit; `data/Errors.txt`
is the reference mapping from each code to a sample English message, with `{}`
marking where the detail payload is substituted. It is not needed at runtime by
the engine — it exists for the frontend and as a starting point you can adapt or
replace with your own messages, in your own language, however you render errors.

### From other languages (C ABI)

The C++ surface above is the right one for a C++ consumer, but most languages
can't call C++ directly: name mangling, exceptions, and an unstable ABI all
get in the way. What nearly every language *can* call is C. So the engine also
ships a thin C wrapper — `Calc::c`, built from `src/capi/` as a shared library
(`libcalc_c.so` / `calc_c.dll`) — that exposes the same operations as plain C
functions any FFI can bind: Python (ctypes/cffi), Rust, Go, C#, Node, Julia.

It is a wrapper, not a second engine. Every function forwards to the C++ core;
the only file that sees both worlds is `src/capi/calc_c.cpp`. The C++ engine is
unchanged and unaware the layer exists, so this is purely additive — a C++
consumer keeps using `Calc::core` and never pays for it.

The header `calc_c.h` includes only `<stddef.h>` and is the whole contract.
The shape mirrors the C++ surface through four ideas:

- **Opaque handles.** `calc_core*` and `calc_plot*` are pointers to incomplete
  types: a C caller holds them but can't look inside, so the pimpl-style
  encapsulation survives the boundary. The shared library is built with hidden
  visibility and exports *only* the `calc_*` functions; no engine symbols leak.
- **Flat result structs.** `evaluateLine`'s `Result<EvalResult>` becomes a
  `calc_eval_result` carrying either the success fields (canonical string,
  optional assigned name, optional numeric value) or the failure fields (the
  `DiagCode` integer, optional detail, source span).
- **Codes, not text.** Diagnostics cross as integers matching `DiagCode.h`
  exactly (its values are explicit and stable for this reason); map them with
  your own table or `data/Errors.txt`, just as a C++ consumer would.
- **Explicit ownership.** Whoever allocates, frees. Any pointer the library
  returns is released with its matching `*_free` (`calc_eval_result_free`,
  `calc_string_array_free`, …), never the caller's `free()`. Exceptions never
  cross the boundary, and freeing `NULL` is always a safe no-op.

Build it with the rest of the project (the `CALCULATOR_BUILD_C_API` option is
on by default), or pull it into a CMake consumer as `Calc::c` the same way as
`Calc::core`. A minimal Python consumer, using nothing but the standard
library:

```python
import ctypes as C

class EvalResult(C.Structure):
    _fields_ = [("ok", C.c_int), ("canonical", C.c_char_p),
                ("assigned_name", C.c_char_p), ("has_value", C.c_int),
                ("value", C.c_double), ("diag_code", C.c_int),
                ("detail", C.c_char_p), ("span_begin", C.c_size_t),
                ("span_end", C.c_size_t)]

lib = C.CDLL("./build/libcalc_c.so")
lib.calc_core_new.restype = C.c_void_p
lib.calc_evaluate_line.restype = C.POINTER(EvalResult)
lib.calc_evaluate_line.argtypes = [C.c_void_p, C.c_char_p, C.c_size_t]

core = lib.calc_core_new()
r = lib.calc_evaluate_line(core, b"3*a + 2*a", 400)
print(r.contents.canonical.decode())   # -> 5*a
lib.calc_eval_result_free(r)
lib.calc_core_free(core)
```

The C ABI is covered in CI: `tests/test_capi.cpp` exercises the boundary as a
C consumer would (opaque handles, result structs, the free contract) and runs
under the same AddressSanitizer/UndefinedBehavior job as the rest of the suite,
so the marshaling layer is checked for leaks and undefined behavior on every
change.

## How simplification works

The parser produces an ordinary binary AST. The simplifier produces a
**canonical form** in which addition/subtraction chains and
multiplication/division chains are each flattened and their terms tagged,
so that algebraically equal expressions with different tree shapes become
directly comparable.

This is what makes cancellation tractable. The trees `Add(a, Sub(b, a))`
and `Sub(Add(a, b), a)` mean the same thing but have different shapes;
structural equality on the tree cannot see that. By flattening
`a + b - a` into a set of signed terms `[+a, +b, -a]`, counting net
occurrences of each term is straightforward, and the `a` terms cancel.
The same idea applies to products: `a*b/a` becomes numerator/denominator
terms `[a, b, 1/a]`, the net exponent of `a` is zero, and it cancels.
Commutativity is handled by grouping like factors into a consistent
order, so `a*b` and `b*a` canonicalize identically.

The simplifier handles this internally and hands back an ordinary AST, so
the rest of the program — evaluator, printer, grapher — only ever deals
with the simple node kinds.

## How graphing works

`compilePlot` does all the expensive work once: it substitutes stored
variables, simplifies, optionally clears denominators, and compiles the
equation to bytecode, returning a `PlotFunctor` — an immutable,
thread-safe object that evaluates `lhs - rhs` of the equation at a given
point. The renderer samples the functor across a grid and draws a cell
wherever the sign changes between any pair of its corner samples, treating
NaN as "no crossing".

By default the functor evaluates the raw `lhs - rhs`, which is the true
level set but goes to ±infinity or NaN at poles (`y = 1/x` is non-finite
at `x = 0`). That is the right form for dense direct evaluation that can
handle non-finite samples itself — for example shading an implicit surface
per-fragment on the GPU, where the undistorted level set is wanted and
infinities simply don't shade.

The ASCII renderer instead samples sparsely and decides each cell by sign
change, so it sets `clearDenominators` on the `PlotRequest`. With the flag
set, both sides are multiplied through by every denominator before
compiling, so vertical asymptotes don't show up as spurious lines:
`y = 1/x` is rewritten as `y*x = 1`, which is finite everywhere.
Cross-multiplication handles sums of fractions, so
`y = 1/(x-1) + 1/(x+1)` becomes `y*(x-1)*(x+1) = 2x`. This changes the
function's magnitude near poles, so the cleared form is specifically for
sign-of-`(lhs - rhs)` strategies and is not a drop-in substitute for the
raw form. (One documented consequence: equations with removable
singularities such as `y = sin(x)/x` will draw a line at `x = 0`, because
the cleared form `y*x = sin(x)` is satisfied by every `y` there.)

`PlotFunctor` is dimension-agnostic: an equation can be compiled against
any number of axes, and the functor is evaluated with one coordinate per
axis. The ASCII renderer uses two; a different frontend could compile the
same equation against three axes for a surface without any engine change.

## Localization

All user-facing text — error messages, help, prompts, banner, command
responses, and even command names — is loaded from plain-text data files
at startup, so the interface can be translated without recompiling.

| File | Contents |
| --- | --- |
| `errors.txt` | diagnostic messages, keyed by `DiagCode` value; `{}` is a placeholder |
| `help.txt` | help text, keyed by topic name |
| `ui.txt` | prompts, banner, and command responses, keyed by a stable id |
| `commands.txt` | maps the command name the user types to a canonical id |

The format is one `key = value` entry per line; `#` starts a comment,
`\n` in a value becomes a newline. To add a language, translate the
values (and, in `commands.txt`, the command names on the left) and point
the program at the new files.

Lookups degrade gracefully: if a display-string file is missing or a key
is absent, the program falls back to built-in English, so it stays usable
with no data files present. Command names are the exception — they are
functional rather than decorative, so `commands.txt` is required for
commands (and their aliases) to resolve.

## Tests

`test_main.cpp` covers the tokenizer, parser precedence/associativity and
error paths, evaluation semantics and error reporting, simplifier
identities and cancellation, denominator clearing for the grapher (checked
through compiled plot functors), printer round-tripping, and REPL
integration including graphing and localized command dispatch.

`test_capi.cpp` is a separate binary that links the `calc_c` shared library
and includes only its public C header, so it covers the FFI boundary the way a
foreign caller meets it: opaque handles, the flat result structs, diagnostic
codes and detail payloads, plot compilation and sampling, command parsing, and
the allocate/caller-frees ownership contract (including NULL handling). Both
binaries run under the same CTest invocation, so the sanitizer job exercises
the wrapper for leaks and undefined behavior alongside the engine.