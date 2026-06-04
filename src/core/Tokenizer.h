#ifndef CALC_TOKENIZER_H
#define CALC_TOKENIZER_H

#include "Types.h"
#include <vector>

namespace calc::core {

	// Lexes a raw input string into a stream of Tokens.
	// On success returns the token list (always terminated by EndOfInput).
	// On failure returns a Diagnostic pointing at the offending position.
	Result<std::vector<Token>> tokenize(std::string_view input);

}  // namespace calc::core

#endif
