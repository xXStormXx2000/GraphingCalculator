// calc_c.h - C ABI for the calc::core calculation engine.
//
// This is the FFI seam. It exposes the engine (CalculatorCore.h) as plain C
// functions so any language with a C foreign-function interface -- Python,
// Rust, Go, C#, Node, Julia -- can drive it without touching C++ name
// mangling, exceptions, or ABI-unstable types.
//
// It is a thin wrapper, not a reimplementation: every function here forwards
// to the C++ engine and marshals the result into C-owned memory. The C++ core
// is unchanged and unaware this layer exists.
//
// Most of the surface drives the engine (evaluate, define/list/clear, compile a
// plot functor, parse a command). Two pieces are lower-level: calc_compile_program
// hands back the neutral bytecode the engine compiles to, and calc_compile_glsl
// walks that bytecode to one concrete target (a GLSL fragment-shader expression).
// A consumer in any language can drive either rather than going through the
// opaque plot functor. See the bytecode and GLSL sections.
//
// --- Conventions (read once, they apply everywhere) ----------------------
//
// Handles are opaque. `calc_core` and `calc_plot` are incomplete types; a C
// caller only ever holds pointers to them and must not dereference them.
//
// Ownership is explicit and paired. Any pointer this library returns is owned
// by this library's allocator and must be released with the matching
// `*_free` function -- never with the caller's free(). The rule is: whoever
// allocates, frees. Calling a `*_free` on NULL is always safe and is a no-op.
//
// Strings are NUL-terminated UTF-8. The engine's canonical output and the
// diagnostic detail payloads may contain multibyte UTF-8 (the data files are
// UTF-8); decode accordingly on the far side.
//
// Diagnostics are integer codes, never text. A failed result carries a
// `diag_code` whose value matches the DiagCode enum in DiagCode.h exactly (the
// values there are explicit and stable for this reason). Map it to a message
// with your own table or with the engine's data/Errors.txt. `detail` carries
// the runtime payload (a variable name, the offending character, ...) for the
// codes that interpolate one, and is NULL otherwise.
//
// Exceptions never cross this boundary. Every entry point has a catch-all; an
// unexpected C++ exception (e.g. std::bad_alloc) is reported as a NULL return
// or an internal-error code, never propagated into the C caller's frame.
//
// Thread-safety. A `calc_core` is mutable (it owns the variable session), so
// concurrent calls on one handle are NOT safe; use one handle per thread or
// serialize access. A `calc_plot` is immutable once compiled and IS safe to
// share and evaluate from many threads concurrently -- `calc_plot_eval`
// allocates its own scratch per call and touches no shared mutable state.
//
// ABI stability. Structs below may grow, but only by appending fields at the
// end; existing fields never move. Function signatures are frozen. This mirrors
// the stable-integer discipline already used for DiagCode.
 

#ifndef CALC_C_H
#define CALC_C_H

#include <stddef.h> // size_t 

 // Export/visibility. The shared library exposes exactly these symbols. 
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

    // ======================================================================== 
    // Opaque handles                                                           
    // ======================================================================== 

    // A calculation session: owns the user's variable definitions. 
    typedef struct calc_core calc_core;

    // A compiled, immutable plot functor produced by calc_compile_plot. 
    typedef struct calc_plot calc_plot;

    // ======================================================================== 
    // Result of evaluating a line                                              
    // ======================================================================== 

    // The outcome of calc_evaluate_line. Exactly one of the two halves is
    // meaningful depending on `ok`. Always release with calc_eval_result_free. 
    typedef struct {
        int          ok;              // 1 = success, 0 = diagnostic               

        // --- success (ok == 1) --- 
        const char*  canonical;       // printed result, always set on success     
        const char*  assigned_name;   // the defined name, or NULL if not an
                                      //  assignment                                
        int          has_value;       // 1 if `value` holds a number (the
                                      //  expression reduced to a scalar)           
        double       value;           // meaningful only when has_value == 1        

        // --- failure (ok == 0) --- 
        int          diag_code;       // a DiagCode integer (see DiagCode.h)        
        const char*  detail;          // diagnostic payload, or NULL if none        
        size_t       span_begin;      // half-open [begin, end) byte span into the  
        size_t       span_end;        // input that the diagnostic points at        
    } calc_eval_result;

    // ======================================================================== 
    // Result of parsing a slash command                                        
    // ======================================================================== 

    // The outcome of calc_parse_command. On success, `args` holds `arg_count`
    // NUL-terminated, whitespace-trimmed argument strings. Release with
    // calc_command_result_free. 
    typedef struct {
        int           ok;             // 1 = success, 0 = diagnostic                 

        // --- success (ok == 1) --- 
        const char*   name;           // command name (without the leading '/')      
        const char**  args;           // arg_count strings, or NULL if none          
        size_t        arg_count;

        // --- failure (ok == 0) --- 
        int           diag_code;
        const char*   detail;
        size_t        span_begin;
        size_t        span_end;
    } calc_command_result;

    // ======================================================================== 
    // Lifecycle                                                                
    // ======================================================================== 

    // Create an engine with no constants: every identifier is a free variable.
    // Returns NULL only on allocation failure. 
    CALC_C_API calc_core* calc_core_new(void);

    // Create an engine with a constant table. `names[i]` folds to `values[i]`
    // during simplification and becomes a reserved name. `names`/`values` are
    // copied; the caller keeps ownership of its own arrays. Returns NULL on
    // allocation failure or if any pointer argument is NULL with count > 0. 
    CALC_C_API calc_core* calc_core_new_with_constants(const char* const* names,
        const double* values,
        size_t count);

    // Destroy an engine. Safe on NULL. 
    CALC_C_API void calc_core_free(calc_core* core);

    // ======================================================================== 
    // Evaluation                                                               
    // ======================================================================== 

    // Parse + simplify one line (optionally an assignment "a: x^2"). `max_size`
    // bounds the post-substitution AST node count (pass 400 to match the console
    // default). Returns a heap-allocated result the caller must release with
    // calc_eval_result_free; returns NULL only on allocation failure. 
    CALC_C_API calc_eval_result* calc_evaluate_line(calc_core* core,
        const char* input,
        size_t max_size);

    CALC_C_API void calc_eval_result_free(calc_eval_result* result);

    // ======================================================================== 
    // Variable session                                                         
    // ======================================================================== 

    // List defined names. Writes a heap-allocated array of `*out_count`
    // NUL-terminated strings to//out_names and returns 1; returns 0 on failure
    // (in which case//out_names is NULL and//out_count is 0). Release the array
    // with calc_string_array_free. Order is unspecified. 
    CALC_C_API int calc_defined_names(const calc_core* core,
        const char*** out_names,
        size_t* out_count);

    // Return the printed definition of `name`, or NULL if it is not defined (or
    // on failure). Release a non-NULL return with calc_string_free. 
    CALC_C_API const char* calc_definition_of(const calc_core* core,
        const char* name);

    // Remove all variable definitions. Safe on NULL. 
    CALC_C_API void calc_clear(calc_core* core);

    // ======================================================================== 
    // Plotting                                                                 
    // ======================================================================== 

    // Compile a stored equation into an immutable plot functor. `axis_names` lists
    // `axis_count` distinct axis names; axis i becomes coordinate slot i. Every
    // free variable in the equation must be an axis. `clear_denominators` picks the
    // raw L-R form (0) or the asymptote-free cross-multiplied form (1); see the
    // PlotRequest docs in CalculatorCore.h.
    //
    // On success returns a non-NULL handle (release with calc_plot_free) and, if
    // out_diag_code is non-NULL, sets//out_diag_code to 0. On failure returns NULL
    // and sets//out_diag_code (when non-NULL) to the DiagCode integer. 
    CALC_C_API calc_plot* calc_compile_plot(const calc_core* core,
        const char* equation_name,
        const char* const* axis_names,
        size_t axis_count,
        int clear_denominators,
        int* out_diag_code);

    // Evaluate lhs - rhs of the compiled equation at `coords` (which must hold
    // exactly calc_plot_dimensions() entries, one per axis in slot order).
    // Returns NaN on any domain error -- the grapher treats NaN as "no crossing",
    // so there is no separate error path. Allocates its own scratch internally,
    // which is why it is safe to call concurrently on a shared functor. 
    CALC_C_API double calc_plot_eval(const calc_plot* plot,
        const double* coords,
        size_t coord_count);

    // Number of axes this functor expects in each calc_plot_eval call. 
    CALC_C_API size_t calc_plot_dimensions(const calc_plot* plot);

    // Destroy a plot functor. Safe on NULL. 
    CALC_C_API void calc_plot_free(calc_plot* plot);

    // ======================================================================== 
    // Command parsing (pure string manipulation; no engine state needed)       
    // ======================================================================== 

    // Parse "/name(arg1, arg2, ...)" into a name and trimmed argument strings.
    // Returns a heap-allocated result the caller must release with
    // calc_command_result_free; returns NULL only on allocation failure. 
    CALC_C_API calc_command_result* calc_parse_command(const char* input);

    CALC_C_API void calc_command_result_free(calc_command_result* result);

    // ======================================================================== 
    // Bytecode (the neutral compiled form)                                     
    // ======================================================================== 

    // Opcode values, mirroring calc::core::VMop in Bytecode.h. These integers are
    // the stable public bytecode format: a consumer walks a calc_program's `code`
    // and switches on `op` to interpret each instruction (run it on a stack, or
    // transpile it to a CPU evaluator, a GLSL/WGSL shader, etc.). The values match
    // the C++ enum exactly -- a static_assert in the implementation enforces this --
    // so they are safe to depend on across the ABI. Append within a band; never
    // renumber or reuse a value. 
    typedef enum {
        CALC_OP_INVALID = 0,
        CALC_OP_PUSH = 1000,
        CALC_OP_BIND = 1001,
        CALC_OP_ADD = 2000,
        CALC_OP_SUB = 2001,
        CALC_OP_MUL = 2002,
        CALC_OP_DIV = 2003,
        CALC_OP_POW = 2004,
        CALC_OP_UMINUS = 3000,
        CALC_OP_SIN = 4000, CALC_OP_COS = 4001, CALC_OP_TAN = 4002,
        CALC_OP_ASIN = 4100, CALC_OP_ACOS = 4101, CALC_OP_ATAN = 4102,
        CALC_OP_SINH = 4200, CALC_OP_COSH = 4201, CALC_OP_TANH = 4202,
        CALC_OP_ASINH = 4300, CALC_OP_ACOSH = 4301, CALC_OP_ATANH = 4302,
        CALC_OP_SQRT = 4400, CALC_OP_ROOT = 4401,
        CALC_OP_ABS = 4500, CALC_OP_LOG = 4501
    } calc_op;

    // One instruction, mirroring calc::core::Bytecode field-for-field. `value` is
    // meaningful only when op == CALC_OP_PUSH; `binding` only when op ==
    // CALC_OP_BIND (it indexes the coordinate vector, in axis-name order). For
    // every other op both are unused. The program is in RPN order: evaluating
    // lhs - rhs of the equation means running the instructions on a value stack. 
    typedef struct {
        int    op;       // a calc_op value 
        double value;
        size_t binding;
    } calc_bytecode;

    // A compiled program: the neutral form the engine emits, from which any target
    // can be generated. This is the same compilation calc_compile_plot performs,
    // exposed as inspectable data instead of an opaque functor -- the primitive
    // that keeps the engine target-neutral, since a consumer can walk it to any
    // representation it needs.
    //
    // On success (`ok` == 1): `code` holds `code_count` instructions; `stack_size`
    // is the maximum value-stack depth (size your stack with it); `dimensions` is
    // the number of axes (the length of the coordinate vector a Bind selects from).
    // On failure (`ok` == 0): `code` is NULL and `diag_code` is the DiagCode reason.
    // Always release with calc_program_free. 
    typedef struct {
        int            ok;          // 1 = success, 0 = diagnostic                  
        calc_bytecode* code;        // code_count instructions, or NULL on failure  
        size_t         code_count;
        size_t         stack_size;  // max stack depth (valid when ok == 1)         
        size_t         dimensions;  // axis count       (valid when ok == 1)        
        int            diag_code;   // a DiagCode integer (valid when ok == 0)      
    } calc_program;

    // Compile a stored equation to neutral bytecode. Same arguments and build-time
    // semantics as calc_compile_plot (distinct axis names, every free variable an
    // axis, clear_denominators picks raw L-R vs. cross-multiplied), but hands back
    // the program for the caller to interpret or transpile rather than an opaque
    // functor. Returns a heap-allocated result the caller must release with
    // calc_program_free; returns NULL only on allocation failure. 
    CALC_C_API calc_program* calc_compile_program(const calc_core* core,
        const char* equation_name,
        const char* const* axis_names,
        size_t axis_count,
        int clear_denominators);

    CALC_C_API void calc_program_free(calc_program* program);

    // ======================================================================== 
    // GLSL (one concrete target, built on the bytecode)                        
    // ======================================================================== 

    // The outcome of calc_compile_glsl. The single `text` field is reused: on
    // success it is the emitted GLSL expression; on a programming error it is a
    // human-readable message. Which one is governed by `diag_code`:
    //
    //   ok == 1                : success. `text` is the GLSL expression.
    //   ok == 0, diag_code > 0 : a user-input diagnostic (a DiagCode integer, e.g.
    //                            an undefined equation). `text` is NULL. Same
    //                            errors compile_program reports.
    //   ok == 0, diag_code ==-1: a PROGRAMMING error in the caller (e.g. the wrong
    //                            number of axis identifiers). `text` holds a plain
    //                            English message meant for the developer, NOT the
    //                            end user -- it has no code and no localization,
    //                            because it describes a misuse of the API, not
    //                            something a user typed. Fix your call; don't show
    //                            this to a user.
    //
    // -1 is a reserved sentinel, guaranteed disjoint from every DiagCode (which are
    // all positive). Always release with calc_glsl_result_free. 
    typedef struct {
        int          ok;          // 1 = success, 0 = failure                       
        const char* text;        // GLSL on success; error message when diag_code
                                  //  == -1; NULL when diag_code > 0                 
        int          diag_code;   // >0 DiagCode (user input); -1 programming error 
    } calc_glsl_result;

    // Emit a GLSL expression for lhs - rhs of a stored equation, ready to splice
    // into a fragment shader (e.g. "((p.y)-pow(p.x,2.0))"). Same build-time
    // semantics as calc_compile_program. `axis_identifiers` supplies the GLSL
    // accessor for each axis, in axis-name order; `axis_identifier_count` MUST
    // equal axis_count, or you get the diag_code == -1 programming-error result.
    // Returns a heap-allocated result the caller must release with
    // calc_glsl_result_free; returns NULL only on allocation failure. 
    CALC_C_API calc_glsl_result* calc_compile_glsl(const calc_core* core,
        const char* equation_name,
        const char* const* axis_names,
        size_t axis_count,
        const char* const* axis_identifiers,
        size_t axis_identifier_count,
        int clear_denominators);

    CALC_C_API void calc_glsl_result_free(calc_glsl_result* result);

    // ======================================================================== 
    // Generic deallocators for arrays/strings handed back above                
    // ======================================================================== 

    // Free a single string returned by this library (e.g. calc_definition_of). 
    CALC_C_API void calc_string_free(const char* s);

    // Free a string array returned by this library (e.g. calc_defined_names).
    // Frees both the element strings and the array itself. 
    CALC_C_API void calc_string_array_free(const char** arr, size_t count);

    // ======================================================================== 
    // Version                                                                  
    // ======================================================================== 

    // Semantic version of this C API, e.g. "1.0.0". Statically allocated; do not
    // free. 
    CALC_C_API const char* calc_c_version(void);

#ifdef __cplusplus
}  // extern "C" 
#endif

#endif // CALC_C_H 
