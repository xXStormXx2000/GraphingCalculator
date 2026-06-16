#include "Parser.h"

#include <cassert>
#include <optional>

namespace calc::core {

	namespace {

		// Pratt parser. Each binary operator has:
		//  - a left binding power (lbp): how strongly it binds to its left operand
		//  - a right binding power (rbp): how strongly it binds to its right operand
		//
		// Left-associative operators have lbp == rbp.
		// Right-associative operators (e.g. ^) have rbp < lbp; we pass `rbp - 1`
		// to the recursive call to make the recursion absorb its own kind.

		struct BindingPower {
			int lbp;
			int rbp;
			BinaryOp op;
		};

		// Returns binding power for the current token, or std::nullopt if it
		// isn't a binary operator.
		std::optional<BindingPower> binaryBp(TokenKind k) {
			switch (k) {
			case TokenKind::Plus:  return BindingPower{ 10, 11, BinaryOp::Add };
			case TokenKind::Minus: return BindingPower{ 10, 11, BinaryOp::Sub };
			case TokenKind::Star:  return BindingPower{ 20, 21, BinaryOp::Mul };
			case TokenKind::Slash: return BindingPower{ 20, 21, BinaryOp::Div };
								 // ^ is right-associative.
			case TokenKind::Caret: return BindingPower{ 30, 29, BinaryOp::Pow };
			default: return std::nullopt;
			}
		}

		class PrattParser {
		public:
			explicit PrattParser(const std::vector<Token>& tokens, std::size_t maxSize, std::size_t& size) : m_tokens(tokens), m_maxSize(maxSize), m_size(size) {}

			Result<AstPtr> parseTopLevel() {
				Result<AstPtr> expr = parseExpr(0);
				if (!expr) return std::move(expr).error();
				if (peek().kind != TokenKind::EndOfInput) {
					return Diagnostic{ DiagCode::UnexpectedTokenAfterExpression, peek().span };
				}
				return std::move(expr).value();
			}

			// Parse from current position up to (but not including) a stop token,
			// for sub-expressions like equation halves.
			Result<AstPtr> parseUntil(TokenKind stop) {
				Result<AstPtr> expr = parseExpr(0);
				if (!expr) return std::move(expr).error();
				if (peek().kind != stop && peek().kind != TokenKind::EndOfInput) {
					return Diagnostic{ DiagCode::UnexpectedToken, peek().span };
				}
				return std::move(expr).value();
			}

			const Token& peek() const { return m_tokens[m_pos]; }
			void advance() { if (peek().kind != TokenKind::EndOfInput) ++m_pos; }

		private:
			Result<AstPtr> parseExpr(int minBp) {
				if (++m_size > m_maxSize) {
					return Diagnostic{ DiagCode::ExpressionTooLong , { m_tokens.front().span.begin, m_tokens.back().span.end }, std::to_string(m_maxSize) };
				}
				Result<AstPtr> lhsResult = parsePrefix();
				if (!lhsResult) return std::move(lhsResult).error();
				AstPtr lhs = std::move(lhsResult).value();

				while (true) {
					std::optional<BindingPower> bp = binaryBp(peek().kind);
					if (!bp) break;
					if (bp->lbp < minBp) break;

					advance();
					Result<AstPtr> rhsResult = parseExpr(bp->rbp);
					if (!rhsResult) return std::move(rhsResult).error();
					AstPtr rhs = std::move(rhsResult).value();

					SourceSpan combined{ lhs->span.begin, rhs->span.end };
					lhs = makeBinary(bp->op, std::move(lhs), std::move(rhs), combined);
					if (++m_size > m_maxSize) {
						return Diagnostic{ DiagCode::ExpressionTooLong , { m_tokens.front().span.begin, m_tokens.back().span.end }, std::to_string(m_maxSize) };
					}
				}
				return lhs;
			}

			Result<AstPtr> parsePrefix() {
				const Token& t = peek();
				switch (t.kind) {
				case TokenKind::Number: {
					advance();
					return makeNumber(t.number, t.span);
				}
				case TokenKind::Identifier: {
					advance();
					// A function call if immediately followed by '('.
					if (peek().kind == TokenKind::LParen) {
						return parseCall(t);
					}
					return makeVariable(t.text, t.span);
				}
				case TokenKind::Minus: {
					// Unary minus: any prefix position.
					advance();
					// Bind tighter than * and / but looser than ^, so -x^2 parses as -(x^2).
					Result<AstPtr> operand = parseExpr(25);
					if (!operand) return std::move(operand).error();
					AstPtr op = std::move(operand).value();
					SourceSpan s{ t.span.begin, op->span.end };
					return makeUnary(UnaryOp::Negate, std::move(op), s);
				}
				case TokenKind::LParen: {
					advance();
					Result<AstPtr> inner = parseExpr(0);
					if (!inner) return std::move(inner).error();
					if (peek().kind != TokenKind::RParen) {
						return Diagnostic{ DiagCode::ExpectedCloseParen, peek().span };
					}
					advance();
					return std::move(inner).value();
				}
				default:
					return Diagnostic{ DiagCode::ExpectedExpression, t.span };
				}
			}

			Result<AstPtr> parseCall(const Token& nameToken) {
				// Already peeked '(' but not consumed yet; consume it now.
				assert(peek().kind == TokenKind::LParen);
				advance();

				std::vector<AstPtr> args;
				if (peek().kind != TokenKind::RParen) {
					while (true) {
						Result<AstPtr> arg = parseExpr(0);
						if (!arg) return std::move(arg).error();
						args.push_back(std::move(arg).value());
						if (peek().kind == TokenKind::Comma) {
							advance();
							continue;
						}
						break;
					}
				}
				if (peek().kind != TokenKind::RParen) {
					return Diagnostic{ DiagCode::ExpectedArgSeparator, peek().span };
				}
				const SourceSpan endSpan = peek().span;
				advance();
				return makeCall(nameToken.text, std::move(args),
					SourceSpan{ nameToken.span.begin, endSpan.end });
			}

			const std::vector<Token>& m_tokens;
			const std::size_t m_maxSize;
			std::size_t& m_size;
			std::size_t m_pos = 0;
		};

	}  // namespace

	Result<ParsedExpression> parseExpression(const std::vector<Token>& tokens, std::size_t maxSize, std::size_t& size) {
		if (tokens.empty() || tokens[0].kind == TokenKind::EndOfInput) {
			return Diagnostic{ DiagCode::EmptyInput, {0, 0} };
		}

		// Detect "name : expr" assignment.
		std::string assignTo;
		std::size_t start = 0;
		if (tokens.size() >= 2 &&
			tokens[0].kind == TokenKind::Identifier &&
			tokens[1].kind == TokenKind::Colon) {
			assignTo = tokens[0].text;
			start = 2;
		}

		// Detect "lhs = rhs" equation. We only support a single '='.
		std::size_t eqPos = std::string::npos;
		for (std::size_t i = start; i < tokens.size(); ++i) {
			if (tokens[i].kind == TokenKind::Equals) {
				if (eqPos != std::string::npos) {
					return Diagnostic{ DiagCode::MultipleEquals, tokens[i].span };
				}
				eqPos = i;
			}
		}

		if (eqPos == std::string::npos) {
			// Plain expression. Slice tokens [start ..].
			std::vector<Token> slice(tokens.begin() + static_cast<std::ptrdiff_t>(start), tokens.end());
			if (slice.empty() || slice[0].kind == TokenKind::EndOfInput) {
				return Diagnostic{ DiagCode::ExpectedExpressionAfterColon, tokens.back().span };
			}
			size = 0;
			PrattParser p(slice, maxSize, size);
			Result<AstPtr> expr = p.parseTopLevel();
			if (!expr) return std::move(expr).error();
			return ParsedExpression{ std::move(assignTo), std::move(expr).value() };
		}

		// Equation: parse left half [start, eqPos), right half [eqPos+1, end).
		std::vector<Token> lhsTokens(tokens.begin() + static_cast<std::ptrdiff_t>(start),
			tokens.begin() + static_cast<std::ptrdiff_t>(eqPos));
		lhsTokens.push_back(Token{ TokenKind::EndOfInput, "", 0.0, tokens[eqPos].span });

		std::vector<Token> rhsTokens(tokens.begin() + static_cast<std::ptrdiff_t>(eqPos + 1),
			tokens.end());

		if (lhsTokens.size() <= 1) {
			return Diagnostic{ DiagCode::ExpectedExpressionBeforeEquals, tokens[eqPos].span };
		}
		if (rhsTokens.empty() || rhsTokens[0].kind == TokenKind::EndOfInput) {
			return Diagnostic{ DiagCode::ExpectedExpressionAfterEquals, tokens[eqPos].span };
		}

		size = 1;
		PrattParser lp(lhsTokens, maxSize, size);
		Result<AstPtr> lhsAst = lp.parseTopLevel();
		if (!lhsAst) return std::move(lhsAst).error();

		PrattParser rp(rhsTokens, maxSize, size);
		Result<AstPtr> rhsAst = rp.parseTopLevel();
		if (!rhsAst) return std::move(rhsAst).error();

		AstPtr eq = makeEquation(std::move(lhsAst).value(), std::move(rhsAst).value(),
			SourceSpan{ tokens[start].span.begin, tokens.back().span.begin });
		return ParsedExpression{ std::move(assignTo), std::move(eq) };
	}

}  // namespace calc::core
