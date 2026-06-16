#ifndef CALC_DIAGCODE_H
#define CALC_DIAGCODE_H

namespace calc::core {

	// Stable identifiers for every diagnostic the backend can produce.
	// The backend never holds human-readable message text; it emits a code
	// plus an optional `detail` string (see Diagnostic in Types.h). The
	// frontend maps each code to a localized message template and fills in
	// the detail where the template has a placeholder.
	//
	// Values are explicit so that an external message file keyed by integer
	// stays stable even if entries are reordered here.
	enum class DiagCode {
		// Tokenizer ---------------------------------------------------------
		MalformedNumber = 1000,  // detail: unused
		UnexpectedCharacter = 1001,  // detail: the offending character

		// Parser ------------------------------------------------------------
		EmptyInput = 2000,
		UnexpectedTokenAfterExpression = 2001,
		UnexpectedToken = 2002,
		ExpectedCloseParen = 2003,  // expected ')'
		ExpectedExpression = 2004,
		ExpectedArgSeparator = 2005,  // expected ')' or ',' in argument list
		MultipleEquals = 2006,
		ExpectedExpressionAfterColon = 2007,
		ExpectedExpressionBeforeEquals = 2008,
		ExpectedExpressionAfterEquals = 2009,
		ExpressionTooLong = 2010,  // exceeded max AST node count

		// Simplifier / numeric ----------------------------------------------
		NotFinite = 3000,  // result is not a finite number
		DivisionByZero = 3001,

		// Command parsing (CalculatorCore::parseCommand) --------------------
		CommandMustStartWithSlash = 4000,
		ExpectedCommandName = 4001,
		ExpectedOpenParen = 4002,  // expected '(' after command name
		UnmatchedOpenParen = 4003,
		UnexpectedInputAfterCommand = 4004,

		// Definitions / graphing (CalculatorCore) ---------------------------
		SelfReference = 5000,  // detail: name; (frontend may also want the expansion)
		AxisNamesMustDiffer = 5001,
		NoSuchVariable = 5002,  // detail: variable name
		GraphTargetNotEquation = 5003,
		NonAxisVariable = 5004,  // detail: variable name
		NoAxisVariable = 5005,
		ConstantReassignment = 5006,  // detail: the reserved constant name

		// Graph rendering (frontend Grapher) --------------------------------
		EmptyXRange = 6000,
		EmptyYRange = 6001,
	};

}  // namespace calc::core

#endif
