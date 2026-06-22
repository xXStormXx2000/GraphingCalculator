// calc_c.cpp - implementation of the C ABI declared in calc_c.h.
//
// This is the ONLY translation unit that includes both the C header and the
// C++ engine. It does three jobs and nothing else:
//
//   1. reinterpret opaque C handles <-> C++ objects,
//   2. marshal C++ values (std::string, Result<T>, optional<T>) into flat,
//      C-owned structs,
//   3. stop every exception at the boundary.
//
// Keep logic out of here. Anything that looks like calculator behavior belongs
// in the engine; this file should stay a mechanical shim.
 

#define CALC_C_BUILDING 1
#include "CalcC.h"

#include "CalculatorCore.h"   // the entire public C++ surface we wrap 

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

	using calc::core::CalculatorCore;
	using calc::core::PlotFunctor;
	using calc::core::Program;
	using calc::core::VMop;

	// ---- handle casts -------------------------------------------------------
	// The opaque C types and the C++ types are distinct, so we cast through
	// void* deliberately. A calc_core* always points at a heap CalculatorCore;
	// a calc_plot* at a heap PlotFunctor. Centralizing the casts keeps the
	// reinterpret_cast in one place and documents the invariant. 

	CalculatorCore* toCore(calc_core* h) { return reinterpret_cast<CalculatorCore*>(h); }
	const CalculatorCore* toCore(const calc_core* h) { return reinterpret_cast<const CalculatorCore*>(h); }
	calc_core* fromCore(CalculatorCore* c) { return reinterpret_cast<calc_core*>(c); }

	PlotFunctor* toPlot(calc_plot* h) { return reinterpret_cast<PlotFunctor*>(h); }
	const PlotFunctor* toPlot(const calc_plot* h) { return reinterpret_cast<const PlotFunctor*>(h); }
	calc_plot* fromPlot(PlotFunctor* p) { return reinterpret_cast<calc_plot*>(p); }

	// ---- string duplication into C-owned (malloc'd) memory ------------------
	// Every string we hand back is malloc'd here and released by the matching
	////_free below. Using malloc/free (not new/delete) keeps the allocator
	// consistent with the flat structs, which are also malloc'd, so a single
	// mental model covers all C-owned memory: malloc here, free in//_free.
	// Returns nullptr on allocation failure (callers treat that as failure). 
	char* dupString(const std::string& s) {
		char* out = static_cast<char*>(std::malloc(s.size() + 1));
		if (!out) return nullptr;
		std::memcpy(out, s.c_str(), s.size() + 1);
		return out;
	}

	// Fill the diagnostic half of either result struct from a C++ Diagnostic.
	// Templated because calc_eval_result and calc_command_result share the same
	// four diagnostic fields by name but are otherwise unrelated types. 
	template <typename ResultT>
	void fillDiagnostic(ResultT* out, const calc::core::Diagnostic& d) {
		out->ok = 0;
		out->diag_code = static_cast<int>(d.code);
		out->detail = d.detail.empty() ? nullptr : dupString(d.detail);
		out->span_begin = d.span.begin;
		out->span_end = d.span.end;
	}

}  // namespace

// ======================================================================== 
// Lifecycle                                                                
// ======================================================================== 

extern "C" calc_core* calc_core_new(void) {
	try {
		return fromCore(new CalculatorCore());
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" calc_core* calc_core_new_with_constants(const char* const* names,
	const double* values,
	size_t count) {
	try {
		if (count > 0 && (names == nullptr || values == nullptr)) return nullptr;
		CalculatorCore::ConstantTable table;
		table.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			if (names[i] == nullptr) return nullptr;
			table.emplace(std::string(names[i]), values[i]);
		}
		return fromCore(new CalculatorCore(std::move(table)));
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" void calc_core_free(calc_core* core) {
	// delete on nullptr is well-defined; no try needed (destructor is noexcept). 
	delete toCore(core);
}

// ======================================================================== 
// Evaluation                                                               
// ======================================================================== 

extern "C" calc_eval_result* calc_evaluate_line(calc_core* core,
	const char* input,
	size_t max_size) {
	try {
		if (core == nullptr || input == nullptr) return nullptr;

		auto* out = static_cast<calc_eval_result*>(
			std::calloc(1, sizeof(calc_eval_result)));
		if (!out) return nullptr;

		auto r = toCore(core)->evaluateLine(std::string_view(input), max_size);
		if (r) {
			const auto& ev = r.value();
			out->ok = 1;
			out->canonical = dupString(ev.canonical);
			if(out->canonical == nullptr) { calc_eval_result_free(out); return nullptr; }
			out->assigned_name = ev.assignedName ? dupString(*ev.assignedName) : nullptr;
			out->has_value = ev.value.has_value() ? 1 : 0;
			out->value = ev.value.value_or(0.0);
		}
		else {
			fillDiagnostic(out, r.error());
		}
		return out;
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" void calc_eval_result_free(calc_eval_result* result) {
	if (!result) return;
	// Both halves are zero-initialized by calloc, so freeing the unused half's
	// NULL pointers is harmless. 
	std::free(const_cast<char*>(result->canonical));
	std::free(const_cast<char*>(result->assigned_name));
	std::free(const_cast<char*>(result->detail));
	std::free(result);
}

// ======================================================================== 
// Variable session                                                         
// ======================================================================== 

extern "C" int calc_defined_names(const calc_core* core,
	const char*** out_names,
	size_t* out_count) {
	if (out_names) *out_names = nullptr;
	if (out_count) *out_count = 0;
	try {
		if (core == nullptr || out_names == nullptr || out_count == nullptr) return 0;

		std::vector<std::string> names = toCore(core)->definedNames();
		if (names.empty()) return 1;  // success, zero names 

		auto* arr = static_cast<const char**>(
			std::calloc(names.size(), sizeof(char*)));
		if (!arr) return 0;

		for (size_t i = 0; i < names.size(); ++i) {
			char* dup = dupString(names[i]);
			if (!dup) {
				// Roll back what we've allocated so far -- no partial leaks. 
				for (size_t j = 0; j < i; ++j) std::free(const_cast<char*>(arr[j]));
				std::free(arr);
				return 0;
			}
			arr[i] = dup;
		}
		*out_names = arr;
		*out_count = names.size();
		return 1;
	}
	catch (...) {
		return 0;
	}
}

extern "C" const char* calc_definition_of(const calc_core* core,
	const char* name) {
	try {
		if (core == nullptr || name == nullptr) return nullptr;
		auto def = toCore(core)->definitionOf(std::string(name));
		if (!def) return nullptr;
		return dupString(*def);
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" void calc_clear(calc_core* core) {
	try {
		if (core) toCore(core)->clear();
	}
	catch (...) {
		// clear() shouldn't throw, but swallow anything that does. 
	}
}

// ======================================================================== 
// Plotting                                                                 
// ======================================================================== 

extern "C" calc_plot* calc_compile_plot(const calc_core* core,
	const char* equation_name,
	const char* const* axis_names,
	size_t axis_count,
	int clear_denominators,
	int* out_diag_code) {
	if (out_diag_code) *out_diag_code = 0;
	try {
		if (core == nullptr || equation_name == nullptr ||
			(axis_count > 0 && axis_names == nullptr)) {
			return nullptr;
		}

		CalculatorCore::PlotRequest req;
		req.equationName = std::string(equation_name);
		req.clearDenominators = (clear_denominators != 0);
		req.axisNames.reserve(axis_count);
		for (size_t i = 0; i < axis_count; ++i) {
			if (axis_names[i] == nullptr) return nullptr;
			req.axisNames.emplace_back(axis_names[i]);
		}

		auto r = toCore(core)->compilePlot(req);
		if (!r) {
			if (out_diag_code) *out_diag_code = static_cast<int>(r.error().code);
			return nullptr;
		}
		return fromPlot(new PlotFunctor(std::move(r).value()));
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" double calc_plot_eval(const calc_plot* plot,
	const double* coords,
	size_t coord_count) {
	// No try/catch needed for correctness in the hot loop: the functor reports
	// domain errors as NaN, never exceptions. We still guard the vector build
	// (an allocation) so a bad_alloc becomes NaN rather than UB. 
	try {
		if (plot == nullptr || coord_count != calc_plot_dimensions(plot) || 
			(coord_count > 0 && coords == nullptr)) {
			return std::nan("");
		}
		std::vector<double> c(coords, coords + coord_count);
		return (*toPlot(plot))(c);
	}
	catch (...) {
		return std::nan("");
	}
}

extern "C" size_t calc_plot_dimensions(const calc_plot* plot) {
	try {
		return plot ? toPlot(plot)->dimensions() : 0;
	}
	catch (...) {
		return 0;
	}
}

extern "C" void calc_plot_free(calc_plot* plot) {
	delete toPlot(plot);
}

// ======================================================================== 
// Command parsing                                                          
// ======================================================================== 

extern "C" calc_command_result* calc_parse_command(const char* input) {
	try {
		if (input == nullptr) return nullptr;

		auto* out = static_cast<calc_command_result*>(
			std::calloc(1, sizeof(calc_command_result)));
		if (!out) return nullptr;

		auto r = calc::core::parseCommand(std::string_view(input));
		if (r) {
			const auto& cmd = r.value();
			out->ok = 1;
			out->name = dupString(cmd.name);
			if(out->name == nullptr){ calc_command_result_free(out); return nullptr; }
			out->arg_count = cmd.args.size();
			if (!cmd.args.empty()) {
				auto* arr = static_cast<const char**>(
					std::calloc(cmd.args.size(), sizeof(char*)));
				if (!arr) { calc_command_result_free(out); return nullptr; }
				bool ok = true;
				for (size_t i = 0; i < cmd.args.size(); ++i) {
					arr[i] = dupString(cmd.args[i]);
					if (arr[i] == nullptr) { ok = false; break; }
				}
				out->args = arr;
				if(!ok) { calc_command_result_free(out); return nullptr; }
			}
		}
		else {
			fillDiagnostic(out, r.error());
		}
		return out;
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" void calc_command_result_free(calc_command_result* result) {
	if (!result) return;
	std::free(const_cast<char*>(result->name));
	if (result->args) {
		for (size_t i = 0; i < result->arg_count; ++i) {
			std::free(const_cast<char*>(result->args[i]));
		}
		std::free(const_cast<char**>(result->args));
	}
	std::free(const_cast<char*>(result->detail));
	std::free(result);
}

// ======================================================================== 
// Bytecode                                                                 
// ======================================================================== 

// The C opcode enum must match the C++ VMop integers exactly, or a consumer
// reading `op` would misinterpret instructions. Enforce it at compile time so
// the two can never silently drift. 
static_assert(static_cast<int>(VMop::Invalid) == CALC_OP_INVALID, "VMop drift: Invalid");
static_assert(static_cast<int>(VMop::Push) == CALC_OP_PUSH, "VMop drift: Push");
static_assert(static_cast<int>(VMop::Bind) == CALC_OP_BIND, "VMop drift: Bind");
static_assert(static_cast<int>(VMop::Add) == CALC_OP_ADD, "VMop drift: Add");
static_assert(static_cast<int>(VMop::Sub) == CALC_OP_SUB, "VMop drift: Sub");
static_assert(static_cast<int>(VMop::Mul) == CALC_OP_MUL, "VMop drift: Mul");
static_assert(static_cast<int>(VMop::Div) == CALC_OP_DIV, "VMop drift: Div");
static_assert(static_cast<int>(VMop::Pow) == CALC_OP_POW, "VMop drift: Pow");
static_assert(static_cast<int>(VMop::Uminus) == CALC_OP_UMINUS, "VMop drift: Uminus");
static_assert(static_cast<int>(VMop::Sin) == CALC_OP_SIN, "VMop drift: Sin");
static_assert(static_cast<int>(VMop::Atanh) == CALC_OP_ATANH, "VMop drift: Atanh");
static_assert(static_cast<int>(VMop::Sqrt) == CALC_OP_SQRT, "VMop drift: Sqrt");
static_assert(static_cast<int>(VMop::Root) == CALC_OP_ROOT, "VMop drift: Root");
static_assert(static_cast<int>(VMop::Abs) == CALC_OP_ABS, "VMop drift: Abs");
static_assert(static_cast<int>(VMop::Log) == CALC_OP_LOG, "VMop drift: Log");

extern "C" calc_program* calc_compile_program(const calc_core* core,
	const char* equation_name,
	const char* const* axis_names,
	size_t axis_count,
	int clear_denominators) {
	try {
		if (core == nullptr || equation_name == nullptr ||
			(axis_count > 0 && axis_names == nullptr)) {
			return nullptr;
		}

		auto* out = static_cast<calc_program*>(std::calloc(1, sizeof(calc_program)));
		if (!out) return nullptr;

		calc::core::CalculatorCore::PlotRequest req;
		req.equationName = std::string(equation_name);
		req.clearDenominators = (clear_denominators != 0);
		req.axisNames.reserve(axis_count);
		for (size_t i = 0; i < axis_count; ++i) {
			if (axis_names[i] == nullptr) { std::free(out); return nullptr; }
			req.axisNames.emplace_back(axis_names[i]);
		}

		auto r = toCore(core)->compileProgram(req);
		if (!r) {
			out->ok = 0;
			out->diag_code = static_cast<int>(r.error().code);
			return out;
		}

		const Program& prog = r.value();
		out->ok = 1;
		out->stack_size = prog.stackSize;
		out->dimensions = prog.dimensions;
		out->code_count = prog.code.size();
		if (!prog.code.empty()) {
			out->code = static_cast<calc_bytecode*>(
				std::calloc(prog.code.size(), sizeof(calc_bytecode)));
			if (!out->code) { std::free(out); return nullptr; }
			// Field-by-field copy, not memcpy: VMop is uint16_t in C++ but the
			// C struct's op is int, so the layouts differ.
			for (size_t i = 0; i < prog.code.size(); ++i) {
				out->code[i].op = static_cast<int>(prog.code[i].op);
				out->code[i].value = prog.code[i].value;
				out->code[i].binding = prog.code[i].binding;
			}
		}
		return out;
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" void calc_program_free(calc_program* program) {
	if (!program) return;
	std::free(program->code);
	std::free(program);
}

// ======================================================================== 
// GLSL                                                                     
// ======================================================================== 

extern "C" calc_glsl_result* calc_compile_glsl(const calc_core* core,
	const char* equation_name,
	const char* const* axis_names,
	size_t axis_count,
	const char* const* axis_identifiers,
	size_t axis_identifier_count,
	int clear_denominators) {
	try {
		if (core == nullptr || equation_name == nullptr ||
			(axis_count > 0 && axis_names == nullptr) ||
			(axis_identifier_count > 0 && axis_identifiers == nullptr)) {
			return nullptr;
		}

		auto* out = static_cast<calc_glsl_result*>(
			std::calloc(1, sizeof(calc_glsl_result)));
		if (!out) return nullptr;

		calc::core::CalculatorCore::PlotRequest req;
		req.equationName = std::string(equation_name);
		req.clearDenominators = (clear_denominators != 0);
		req.axisNames.reserve(axis_count);
		for (size_t i = 0; i < axis_count; ++i) {
			if (axis_names[i] == nullptr) { std::free(out); return nullptr; }
			req.axisNames.emplace_back(axis_names[i]);
		}

		std::vector<std::string> identifiers;
		identifiers.reserve(axis_identifier_count);
		for (size_t i = 0; i < axis_identifier_count; ++i) {
			if (axis_identifiers[i] == nullptr) { std::free(out); return nullptr; }
			identifiers.emplace_back(axis_identifiers[i]);
		}

		try {
			auto r = toCore(core)->compileGLSL(req, identifiers);
			if (!r) {
				// User-input diagnostic: positive DiagCode, no text.
				out->ok = 0;
				out->diag_code = static_cast<int>(r.error().code);
				return out;
			}
			out->ok = 1;
			out->text = dupString(r.value());
			if(out->text == nullptr) { calc_glsl_result_free(out); return nullptr; }
			return out;
		}
		catch (const std::invalid_argument& e) {
			// Programming error (precondition violation in the caller). Report
			// it as the -1 sentinel with the developer-facing message, rather
			// than letting the throw cross the C boundary as a crash.
			out->ok = 0;
			out->diag_code = -1;
			out->text = dupString(e.what());
			return out;
		}
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" void calc_glsl_result_free(calc_glsl_result* result) {
	if (!result) return;
	std::free(const_cast<char*>(result->text));
	std::free(result);
}

// ======================================================================== 
// Generic deallocators                                                     
// ======================================================================== 

extern "C" void calc_string_free(const char* s) {
	std::free(const_cast<char*>(s));
}

extern "C" void calc_string_array_free(const char** arr, size_t count) {
	if (!arr) return;
	for (size_t i = 0; i < count; ++i) std::free(const_cast<char*>(arr[i]));
	std::free(const_cast<char**>(arr));
}

// ======================================================================== 
// Version                                                                  
// ======================================================================== 

extern "C" const char* calc_c_version(void) {
	return "1.0.0";
}
