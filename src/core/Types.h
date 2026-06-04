#ifndef CALC_TYPES_H
#define CALC_TYPES_H

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

#include "DiagCode.h"

namespace calc::core {

// A half-open span [begin, end) into the original input string.
// Used to point error messages at the right place.
struct SourceSpan {
    std::size_t begin = 0;
    std::size_t end = 0;

    constexpr bool empty() const noexcept { return begin == end; }
};

enum class TokenKind {
    Number,      // 12, 3.14, .5
    Identifier,  // x, foo, sin (function-vs-variable resolved later)
    Plus, Minus, Star, Slash, Caret,
    LParen, RParen,
    Comma,
    Colon,       // assignment
    Equals,      // equation
    Slash_Cmd,   // leading '/' for REPL commands; only meaningful as the first token
    EndOfInput,
};

struct Token {
    TokenKind kind = TokenKind::EndOfInput;
    std::string text;     // textual form (for identifiers and numbers)
    double number = 0.0;  // valid only when kind == Number
    SourceSpan span;
};

// View rectangle used by the ASCII graph renderer.
struct Rect {
    double xLeft = 0.0;
    double xRight = 0.0;
    double yBottom = 0.0;
    double yTop = 0.0;
};

// A single diagnostic. Errors are *positional*: they include the span
// of the offending token so we can underline it nicely.
//
// The backend carries no human-readable text. It emits a `code` plus an
// optional `detail` string for codes that interpolate runtime data (a
// variable name, the offending character, etc.). The frontend turns the
// code into a localized message and substitutes the detail.
struct Diagnostic {
    DiagCode    code;
    SourceSpan  span;
    std::string detail = {};  // optional payload, empty when the code needs none
};

// Lightweight Result<T> type.
template <typename T>
class Result {
public:
    Result(T value) : m_ok(true), m_value(std::move(value)) {}
    Result(Diagnostic err) : m_ok(false), m_error(std::move(err)) {}

    bool ok() const noexcept { return m_ok; }
    explicit operator bool() const noexcept { return m_ok; }

    T& value() & { return m_value; }
    const T& value() const& { return m_value; }
    T&& value() && { return std::move(m_value); }

    const Diagnostic& error() const& { return m_error; }
    Diagnostic&& error() && { return std::move(m_error); }

private:
    bool m_ok;
    T m_value{};
    Diagnostic m_error{};
};

// Specialization for void-like results (operations that just succeed/fail).
struct Unit {};

inline std::ostream& operator<<(std::ostream& os, TokenKind k) {
    switch (k) {
    case TokenKind::Number:     return os << "Number";
    case TokenKind::Identifier: return os << "Identifier";
    case TokenKind::Plus:       return os << "Plus";
    case TokenKind::Minus:      return os << "Minus";
    case TokenKind::Star:       return os << "Star";
    case TokenKind::Slash:      return os << "Slash";
    case TokenKind::Caret:      return os << "Caret";
    case TokenKind::LParen:     return os << "LParen";
    case TokenKind::RParen:     return os << "RParen";
    case TokenKind::Comma:      return os << "Comma";
    case TokenKind::Colon:      return os << "Colon";
    case TokenKind::Equals:     return os << "Equals";
    case TokenKind::Slash_Cmd:  return os << "Slash_Cmd";
    case TokenKind::EndOfInput: return os << "EndOfInput";
    }
    return os << "Unknown";
}

}  // namespace calc::core

#endif
