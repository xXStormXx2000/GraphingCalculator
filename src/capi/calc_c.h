/* calc_c.h - C ABI for the calc::core calculation engine.
 *
 * This is the FFI seam. It exposes the engine (CalculatorCore.h) as plain C
 * functions so any language with a C foreign-function interface -- Python,
 * Rust, Go, C#, Node, Julia -- can drive it without touching C++ name
 * mangling, exceptions, or ABI-unstable types.
 *
 * It is a thin wrapper, not a reimplementation: every function here forwards
 * to the C++ engine and marshals the result into C-owned memory. The C++ core
 * is unchanged and unaware this layer exists.
 *
 * --- Conventions (read once, they apply everywhere) ----------------------
 *
 * Handles are opaque. `calc_core` and `calc_plot` are incomplete types; a C
 * caller only ever holds pointers to them and must not dereference them.
 *
 * Ownership is explicit and paired. Any pointer this library returns is owned
 * by this library's allocator and must be released with the matching
 * `*_free` function -- never with the caller's free(). The rule is: whoever
 * allocates, frees. Calling a `*_free` on NULL is always safe and is a no-op.
 *
 * Strings are NUL-terminated UTF-8. The engine's canonical output and the
 * diagnostic detail payloads may contain multibyte UTF-8 (the data files are
 * UTF-8); decode accordingly on the far side.
 *
 * Diagnostics are integer codes, never text. A failed result carries a
 * `diag_code` whose value matches the DiagCode enum in DiagCode.h exactly (the
 * values there are explicit and stable for this reason). Map it to a message
 * with your own table or with the engine's data/Errors.txt. `detail` carries
 * the runtime payload (a variable name, the offending character, ...) for the
 * codes that interpolate one, and is NULL otherwise.
 *
 * Exceptions never cross this boundary. Every entry point has a catch-all; an
 * unexpected C++ exception (e.g. std::bad_alloc) is reported as a NULL return
 * or an internal-error code, never propagated into the C caller's frame.
 *
 * Thread-safety. A `calc_core` is mutable (it owns the variable session), so
 * concurrent calls on one handle are NOT safe; use one handle per thread or
 * serialize access. A `calc_plot` is immutable once compiled and IS safe to
 * share and evaluate from many threads concurrently -- `calc_plot_eval`
 * allocates its own scratch per call and touches no shared mutable state.
 *
 * ABI stability. Structs below may grow, but only by appending fields at the
 * end; existing fields never move. Function signatures are frozen. This mirrors
 * the stable-integer discipline already used for DiagCode.
 */

#ifndef CALC_C_H
#define CALC_C_H

#include <stddef.h> /* size_t */

/* Export/visibility. The shared library exposes exactly these symbols. */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(CALC_C_BUILDING)
#    define CALC_C_API __declspec(dllexport)
#  else
#    define CALC_C_API __declspec(dllimport)
#  endif
#else
#  if defined(CALC_C_BUILDING)
#    define CALC_C_API __attribute__((visibility("default")))
#  else
#    define CALC_C_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/* Opaque handles                                                           */
/* ======================================================================== */

/* A calculation session: owns the user's variable definitions. */
typedef struct calc_core calc_core;

/* A compiled, immutable plot functor produced by calc_compile_plot. */
typedef struct calc_plot calc_plot;

/* ======================================================================== */
/* Result of evaluating a line                                              */
/* ======================================================================== */

/* The outcome of calc_evaluate_line. Exactly one of the two halves is
 * meaningful depending on `ok`. Always release with calc_eval_result_free. */
typedef struct {
	int          ok;            /* 1 = success, 0 = diagnostic               */

	/* --- success (ok == 1) --- */
	const char*  canonical;     /* printed result, always set on success     */
	const char*  assigned_name; /* the defined name, or NULL if not an
	                             *  assignment                                */
	int          has_value;     /* 1 if `value` holds a number (the
	                             *  expression reduced to a scalar)           */
	double       value;         /* meaningful only when has_value == 1        */

	/* --- failure (ok == 0) --- */
	int          diag_code;     /* a DiagCode integer (see DiagCode.h)        */
	const char*  detail;        /* diagnostic payload, or NULL if none        */
	size_t       span_begin;    /* half-open [begin, end) byte span into the  */
	size_t       span_end;      /* input that the diagnostic points at        */
} calc_eval_result;

/* ======================================================================== */
/* Result of parsing a slash command                                        */
/* ======================================================================== */

/* The outcome of calc_parse_command. On success, `args` holds `arg_count`
 * NUL-terminated, whitespace-trimmed argument strings. Release with
 * calc_command_result_free. */
typedef struct {
	int           ok;          /* 1 = success, 0 = diagnostic                 */

	/* --- success (ok == 1) --- */
	const char*   name;        /* command name (without the leading '/')      */
	const char**  args;        /* arg_count strings, or NULL if none          */
	size_t        arg_count;

	/* --- failure (ok == 0) --- */
	int           diag_code;
	const char*   detail;
	size_t        span_begin;
	size_t        span_end;
} calc_command_result;

/* ======================================================================== */
/* Lifecycle                                                                */
/* ======================================================================== */

/* Create an engine with no constants: every identifier is a free variable.
 * Returns NULL only on allocation failure. */
CALC_C_API calc_core* calc_core_new(void);

/* Create an engine with a constant table. `names[i]` folds to `values[i]`
 * during simplification and becomes a reserved name. `names`/`values` are
 * copied; the caller keeps ownership of its own arrays. Returns NULL on
 * allocation failure or if any pointer argument is NULL with count > 0. */
CALC_C_API calc_core* calc_core_new_with_constants(const char* const* names,
                                                   const double* values,
                                                   size_t count);

/* Destroy an engine. Safe on NULL. */
CALC_C_API void calc_core_free(calc_core* core);

/* ======================================================================== */
/* Evaluation                                                               */
/* ======================================================================== */

/* Parse + simplify one line (optionally an assignment "a: x^2"). `max_size`
 * bounds the post-substitution AST node count (pass 400 to match the console
 * default). Returns a heap-allocated result the caller must release with
 * calc_eval_result_free; returns NULL only on allocation failure. */
CALC_C_API calc_eval_result* calc_evaluate_line(calc_core* core,
                                                const char* input,
                                                size_t max_size);

CALC_C_API void calc_eval_result_free(calc_eval_result* result);

/* ======================================================================== */
/* Variable session                                                         */
/* ======================================================================== */

/* List defined names. Writes a heap-allocated array of `*out_count`
 * NUL-terminated strings to *out_names and returns 1; returns 0 on failure
 * (in which case *out_names is NULL and *out_count is 0). Release the array
 * with calc_string_array_free. Order is unspecified. */
CALC_C_API int calc_defined_names(const calc_core* core,
                                  const char*** out_names,
                                  size_t* out_count);

/* Return the printed definition of `name`, or NULL if it is not defined (or
 * on failure). Release a non-NULL return with calc_string_free. */
CALC_C_API const char* calc_definition_of(const calc_core* core,
                                          const char* name);

/* Remove all variable definitions. Safe on NULL. */
CALC_C_API void calc_clear(calc_core* core);

/* ======================================================================== */
/* Plotting                                                                 */
/* ======================================================================== */

/* Compile a stored equation into an immutable plot functor. `axis_names` lists
 * `axis_count` distinct axis names; axis i becomes coordinate slot i. Every
 * free variable in the equation must be an axis. `clear_denominators` picks the
 * raw L-R form (0) or the asymptote-free cross-multiplied form (1); see the
 * PlotRequest docs in CalculatorCore.h.
 *
 * On success returns a non-NULL handle (release with calc_plot_free) and, if
 * out_diag_code is non-NULL, sets *out_diag_code to 0. On failure returns NULL
 * and sets *out_diag_code (when non-NULL) to the DiagCode integer. */
CALC_C_API calc_plot* calc_compile_plot(const calc_core* core,
                                        const char* equation_name,
                                        const char* const* axis_names,
                                        size_t axis_count,
                                        int clear_denominators,
                                        int* out_diag_code);

/* Evaluate lhs - rhs of the compiled equation at `coords` (which must hold
 * exactly calc_plot_dimensions() entries, one per axis in slot order).
 * Returns NaN on any domain error -- the grapher treats NaN as "no crossing",
 * so there is no separate error path. Allocates its own scratch internally,
 * which is why it is safe to call concurrently on a shared functor. */
CALC_C_API double calc_plot_eval(const calc_plot* plot,
                                 const double* coords,
                                 size_t coord_count);

/* Number of axes this functor expects in each calc_plot_eval call. */
CALC_C_API size_t calc_plot_dimensions(const calc_plot* plot);

/* Destroy a plot functor. Safe on NULL. */
CALC_C_API void calc_plot_free(calc_plot* plot);

/* ======================================================================== */
/* Command parsing (pure string manipulation; no engine state needed)       */
/* ======================================================================== */

/* Parse "/name(arg1, arg2, ...)" into a name and trimmed argument strings.
 * Returns a heap-allocated result the caller must release with
 * calc_command_result_free; returns NULL only on allocation failure. */
CALC_C_API calc_command_result* calc_parse_command(const char* input);

CALC_C_API void calc_command_result_free(calc_command_result* result);

/* ======================================================================== */
/* Generic deallocators for arrays/strings handed back above                */
/* ======================================================================== */

/* Free a single string returned by this library (e.g. calc_definition_of). */
CALC_C_API void calc_string_free(const char* s);

/* Free a string array returned by this library (e.g. calc_defined_names).
 * Frees both the element strings and the array itself. */
CALC_C_API void calc_string_array_free(const char** arr, size_t count);

/* ======================================================================== */
/* Version                                                                  */
/* ======================================================================== */

/* Semantic version of this C API, e.g. "1.0.0". Statically allocated; do not
 * free. */
CALC_C_API const char* calc_c_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* CALC_C_H */
