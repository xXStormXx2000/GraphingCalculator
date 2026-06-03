#ifndef CALC_AST_H
#define CALC_AST_H

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "Types.h"

namespace calc::core {

// Forward declaration so we can use AstPtr inside the variant.
struct AstNode;
using AstPtr = std::shared_ptr<AstNode>;

// ----- AST node kinds ------------------------------------------------------

struct NumberNode {
    double value = 0.0;
};

struct VariableNode {
    std::string name;
};

enum class UnaryOp { Negate };

struct UnaryNode {
    UnaryOp op;
    AstPtr operand;
};

enum class BinaryOp { Add, Sub, Mul, Div, Pow };

struct BinaryNode {
    BinaryOp op;
    AstPtr lhs;
    AstPtr rhs;
};

struct CallNode {
    std::string name;
    std::vector<AstPtr> args;
};

// "lhs = rhs" for graphable equations.
struct EquationNode {
    AstPtr lhs;
    AstPtr rhs;
};

// Single owning variant. Adding a new node kind = add a struct + add it here
// + handle it in each visitor. The compiler will tell you what's missing.
struct AstNode {
    std::variant<
        NumberNode,
        VariableNode,
        UnaryNode,
        BinaryNode,
        CallNode,
        EquationNode
        > value;
    SourceSpan span;
};

// Constructors as factory functions, so callers don't have to spell out
// the variant alternative every time.
inline AstPtr makeNumber(double v, SourceSpan s = {}) {
    return std::make_shared<AstNode>(AstNode{NumberNode{v}, s});
}
inline AstPtr makeVariable(std::string name, SourceSpan s = {}) {
    return std::make_shared<AstNode>(AstNode{VariableNode{std::move(name)}, s});
}
inline AstPtr makeUnary(UnaryOp op, AstPtr operand, SourceSpan s = {}) {
    return std::make_shared<AstNode>(AstNode{UnaryNode{op, std::move(operand)}, s});
}
inline AstPtr makeBinary(BinaryOp op, AstPtr lhs, AstPtr rhs, SourceSpan s = {}) {
    return std::make_shared<AstNode>(AstNode{BinaryNode{op, std::move(lhs), std::move(rhs)}, s});
}
inline AstPtr makeCall(std::string name, std::vector<AstPtr> args, SourceSpan s = {}) {
    return std::make_shared<AstNode>(AstNode{CallNode{std::move(name), std::move(args)}, s});
}
inline AstPtr makeEquation(AstPtr lhs, AstPtr rhs, SourceSpan s = {}) {
    return std::make_shared<AstNode>(AstNode{EquationNode{std::move(lhs), std::move(rhs)}, s});
}

// Convenience helper for std::visit lambdas.
template <typename... Fs>
struct overloaded : Fs... { using Fs::operator()...; };
template <typename... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

}  // namespace calc::core

#endif
