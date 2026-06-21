#ifndef CALC_BYTECODE_H
#define CALC_BYTECODE_H

#include <cstddef>
#include <cstdint>
#include <vector>


namespace calc::core {
	// Stable opcode values: this enum is part of the public bytecode format and
	// crosses the C ABI, so consumers in other languages depend on these integers.
	// Append new ops at the end; never renumber or reuse a value.
	enum class VMop : std::uint16_t {
		Invalid = 0,
		Push = 1000,
		Bind = 1001,
		Add = 2000, Sub = 2001, Mul = 2002, Div = 2003, Pow = 2004, 
		Uminus = 3000,
		Sin = 4000, Cos = 4001, Tan = 4002,
		Asin = 4100, Acos = 4101, Atan = 4102,
		Sinh = 4200, Cosh = 4201, Tanh = 4202,
		Asinh = 4300, Acosh = 4301, Atanh = 4302,
		Sqrt = 4400, Root = 4401,
		Abs = 4500, Log = 4501
	};

	// Bytecode structure to represent each instruction
	struct Bytecode {
		VMop		op		= VMop::Invalid;
		double      value	= 0;			// valid only when op == Push
		std::size_t binding = 0;			// valid only when op == Bind; index into the plot's axis bindings
	};

	using Chunk = std::vector<Bytecode>;

	struct Program {
		Chunk       code;
		std::size_t stackSize = 0;
		std::size_t dimensions = 0;
	};
}

#endif