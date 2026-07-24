#pragma once

#include "kond_common.hpp"

namespace kond {

struct SourcePos {
    std::string file;
    std::size_t offset = 0;
    int line = 1;
    int column = 1;
};

struct KondError final : std::runtime_error {
    std::string code;
    SourcePos pos;

    KondError(std::string errorCode, SourcePos where, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(errorCode)), pos(std::move(where)) {}
};

[[noreturn]] static void fail(const std::string &code, const SourcePos &pos, const std::string &message) {
    throw KondError(code, pos, message);
}

enum class TokenKind {
    End,
    Identifier,
    Number,
    String,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Dot,
    Colon,
    Semicolon,
    Plus,
    PlusEqual,
    Minus,
    MinusEqual,
    Star,
    StarEqual,
    Slash,
    SlashEqual,
    Percent,
    Amp,
    Pipe,
    Caret,
    Bang,
    Equal,
    EqualEqual,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Arrow,
    LeftArrow
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    SourcePos pos;
};

struct Expr;
struct Condition;
struct Statement;
struct FunctionDef;
struct ConditionDef;

using ExprPtr = std::shared_ptr<Expr>;
using ConditionPtr = std::shared_ptr<Condition>;
using StatementPtr = std::shared_ptr<Statement>;

enum class ExprKind {
    Literal,
    Variable,
    Array,
    Object,
    Unary,
    Binary,
    Call,
    Member,
    Index,
    Lambda,
    ConditionValue,
    Move,
    Borrow
};

enum class LiteralKind { Integer, Float, String };

struct Expr {
    ExprKind kind = ExprKind::Literal;
    LiteralKind literalKind = LiteralKind::Integer;
    SourcePos pos;
    std::string text;
    std::string op;
    std::vector<ExprPtr> items;
    std::vector<std::pair<std::string, ExprPtr>> fields;
    ExprPtr left;
    ExprPtr right;
    ExprPtr object;
    ExprPtr index;
    ExprPtr callee;
    std::string lambdaParam;
    ConditionPtr lambdaBody;
    ConditionPtr condition;
    bool mutableBorrow = false;

    static ExprPtr make(ExprKind kind, SourcePos pos) {
        auto result = std::make_shared<Expr>();
        result->kind = kind;
        result->pos = std::move(pos);
        return result;
    }
};

enum class ConditionKind { Always, Never, Not, And, Or, Relation, Is, Has, Call, ValueRef };

struct ShapeFieldSchema {
    std::string name;
    std::string predicate;
    ConditionPtr condition;
    SourcePos pos;
};

struct Condition {
    ConditionKind kind = ConditionKind::Always;
    SourcePos pos;
    std::string op;
    std::string predicate;
    ExprPtr left;
    ExprPtr right;
    ExprPtr value;
    std::vector<ConditionPtr> items;
    std::vector<ExprPtr> args;
    std::vector<std::string> fields;
    std::vector<ShapeFieldSchema> shapeFields;

    static ConditionPtr make(ConditionKind kind, SourcePos pos) {
        auto result = std::make_shared<Condition>();
        result->kind = kind;
        result->pos = std::move(pos);
        return result;
    }
};

struct Param {
    std::string name;
    ConditionPtr invariant;
    SourcePos pos;
};

struct FlowClause {
    std::string target;
    std::vector<ExprPtr> sources;
    SourcePos pos;
};

enum class StatementKind {
    Block,
    Let,
    Expression,
    Assign,
    Check,
    Prove,
    Require,
    Assume,
    If,
    While,
    Return,
    Update,
    UnsafeBlock,
    Match,
    For
};

struct MatchArm {
    ConditionPtr condition;
    StatementPtr action;
};

struct Statement {
    StatementKind kind = StatementKind::Expression;
    SourcePos pos;
    std::string name;
    std::string assignmentOp;
    ExprPtr expr;
    ExprPtr target;
    ConditionPtr condition;
    ConditionPtr invariant;
    StatementPtr body;
    StatementPtr elseBody;
    std::vector<StatementPtr> statements;
    std::vector<MatchArm> arms;
    std::vector<ConditionPtr> loopInvariants;

    static StatementPtr make(StatementKind kind, SourcePos pos) {
        auto result = std::make_shared<Statement>();
        result->kind = kind;
        result->pos = std::move(pos);
        return result;
    }
};

struct ConditionDef {
    std::string name;
    std::vector<Param> params;
    ConditionPtr body;
    SourcePos pos;
};

struct FunctionDef {
    std::string name;
    std::vector<Param> params;
    std::vector<ConditionPtr> requiresList;
    std::vector<ConditionPtr> ensures;
    std::vector<FlowClause> flows;
    StatementPtr body;
    SourcePos pos;
    bool unsafe = false;
    bool route = false;
    std::string routeMethod;
    std::string routePath;
};

// Optimization rules are deliberately represented as ordinary Kond
// expression/condition trees.  This keeps the rule language small and lets
// the same proof machinery validate a library rule that validates a normal
// `prove` statement.
enum class OptimizationProofKind { ExactEq, RealEq, ApproxEq, HeuristicImprovement };

struct OptimizationRule {
    std::string name;
    std::vector<std::string> parameters;
    ConditionPtr precondition;
    ExprPtr pattern;
    ExprPtr replacement;
    OptimizationProofKind proofKind = OptimizationProofKind::ExactEq;
    SourcePos pos;
};

struct Program {
    std::unordered_map<std::string, ConditionDef> conditions;
    std::unordered_map<std::string, FunctionDef> functions;
    std::vector<OptimizationRule> optimizationRules;
    std::vector<StatementPtr> topLevel;
};


Program parseProgram(std::string source, std::string file);
ExprPtr parseExpression(std::string source, std::string file);
// Merge an explicitly loaded source library.  Unlike an optimization library,
// a source library may export conditions and functions, but it never executes
// top-level statements while being loaded.
void mergeLibrary(Program &program, const Program &library);
void mergeOptimizationLibrary(Program &program, const Program &library);

} // namespace kond
