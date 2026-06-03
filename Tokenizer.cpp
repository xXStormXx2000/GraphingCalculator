#include "Tokenizer.h"

#include <cctype>
#include <charconv>

namespace calc::core {

namespace {

// Returns true if `c` can appear inside an identifier (after the first char).
constexpr bool isIdentTail(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

// Returns true if `c` can start an identifier.
constexpr bool isIdentHead(unsigned char c) {
    return std::isalpha(c) || c == '_';
}

}  // namespace

Result<std::vector<Token>> tokenize(std::string_view input) {
    std::vector<Token> out;
    out.reserve(input.size() / 2 + 1);

    std::size_t i = 0;
    const std::size_t n = input.size();

    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(input[i]);

        // Skip whitespace.
        if (std::isspace(c)) {
            ++i;
            continue;
        }

        const std::size_t start = i;

        // Numbers: integer or fractional, with at most one '.'.
        if (std::isdigit(c) || (c == '.' && i + 1 < n &&
                                std::isdigit(static_cast<unsigned char>(input[i + 1])))) {
            bool seenDot = false;
            while (i < n) {
                const unsigned char d = static_cast<unsigned char>(input[i]);
                if (std::isdigit(d)) { ++i; continue; }
                if (d == '.' && !seenDot) { seenDot = true; ++i; continue; }
                break;
            }
            std::string_view text = input.substr(start, i - start);
            double value = 0.0;
            // from_chars on doubles is supported in C++17 onward; libstdc++
            // shipped a complete implementation a few releases back so this
            // is safe on modern toolchains.
            const char* first = text.data();
            const char* last = first + text.size();
            // Structured binding: std::from_chars returns
            // std::from_chars_result, which has fields `ptr` (where parsing
            // stopped) and `ec` (error code). `auto` is required for the
            // structured binding syntax to work.
            auto [ptr, ec] = std::from_chars(first, last, value);
            if (ec != std::errc{} || ptr != last) {
                return Diagnostic{DiagCode::MalformedNumber, {start, i}};
            }
            out.push_back(Token{TokenKind::Number, std::string(text), value, {start, i}});
            continue;
        }

        // Identifiers (letters, then alnum/underscore).
        if (isIdentHead(c)) {
            while (i < n && isIdentTail(static_cast<unsigned char>(input[i]))) ++i;
            std::string_view text = input.substr(start, i - start);
            out.push_back(Token{TokenKind::Identifier, std::string(text), 0.0, {start, i}});
            continue;
        }

        // Single-character punctuation and operators.
        TokenKind kind;
        switch (c) {
        case '+': kind = TokenKind::Plus;      break;
        case '-': kind = TokenKind::Minus;     break;
        case '*': kind = TokenKind::Star;      break;
        case '^': kind = TokenKind::Caret;     break;
        case '(': kind = TokenKind::LParen;    break;
        case ')': kind = TokenKind::RParen;    break;
        case ',': kind = TokenKind::Comma;     break;
        case ':': kind = TokenKind::Colon;     break;
        case '=': kind = TokenKind::Equals;    break;
        case '/':
            // A '/' as the first non-whitespace token is a command prefix
            // (e.g. /graph, /list). Anywhere else it's the division operator.
            kind = out.empty() ? TokenKind::Slash_Cmd : TokenKind::Slash;
            break;
        default:
            return Diagnostic{
                DiagCode::UnexpectedCharacter,
                {start, start + 1},
                std::string(1, static_cast<char>(c))
            };
        }
        ++i;
        out.push_back(Token{kind, std::string(1, static_cast<char>(c)), 0.0, {start, i}});
    }

    out.push_back(Token{TokenKind::EndOfInput, "", 0.0, {n, n}});
    return out;
}

}  // namespace calc::core
