#pragma once

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "tokenization.hpp"

enum class FloatType { f32, f64 };

enum class IntType {
    i8, i16, i32, i64,
    u8, u16, u32, u64
};

// ── Type representation ───────────────────────────────────────────────────
// A Type is either a base integer type or a pointer to another type.
// Pointer types carry the pointed-to type for scaling and load/store width.

struct Type {
    // Func is only ever the pointee of a Ptr: a function pointer `fn(A, B) -> R` is Ptr(Func).
    enum Kind : uint8_t { Int, Ptr, Struct, Func, Float };
    Kind kind;
    IntType int_type;    // valid when kind == Int
    Type* pointee;       // valid when kind == Ptr (arena-allocated via parser allocator)
    std::string struct_name; // valid when kind == Struct
    Type* fn_ret = nullptr;      // valid when kind == Func: return type (null = unannotated)
    Type* fn_params = nullptr;   // valid when kind == Func: array of fn_nparams parameter types
    uint32_t fn_nparams = 0;     // valid when kind == Func
    FloatType float_type = FloatType::f64; // valid when kind == Float

    static Type make_int(IntType t) { return {Int, t, nullptr, {}}; }
    static Type make_float(FloatType f) {
        Type t{Float, IntType::i64, nullptr, {}};
        t.float_type = f;
        return t;
    }
    static Type make_struct(std::string name) { return {Struct, IntType::i64, nullptr, std::move(name)}; }
    /// A function signature; `params` and `ret` must outlive the type (arena or type pool).
    static Type make_func(Type* params, uint32_t nparams, Type* ret) {
        Type t{Func, IntType::i64, nullptr, {}};
        t.fn_params = params;
        t.fn_nparams = nparams;
        t.fn_ret = ret;
        return t;
    }
    static Type make_ptr(Type* t, ArenaAllocator& alloc) {
        Type* p = alloc.alloc<Type>();
        p->kind = Ptr;
        p->int_type = IntType::i64; // unused
        p->pointee = t;
        return {Ptr, IntType::i64, p, {}};
    }
    // For convenience: create a pointer type without an arena (static storage).
    // Used in the Generator where we have a fixed set of pointer types.
    static Type make_ptr(Type* t) {
        return {Ptr, IntType::i64, t, {}};
    }

    bool operator==(const Type& o) const {
        if (kind != o.kind) return false;
        if (kind == Int) return int_type == o.int_type;
        if (kind == Float) return float_type == o.float_type;
        if (kind == Struct) return struct_name == o.struct_name;
        if (kind == Func) {
            if (fn_nparams != o.fn_nparams) return false;
            if ((fn_ret == nullptr) != (o.fn_ret == nullptr)) return false;
            if (fn_ret && *fn_ret != *o.fn_ret) return false;
            for (uint32_t i = 0; i < fn_nparams; i++)
                if (fn_params[i] != o.fn_params[i]) return false;
            return true;
        }
        // Ptr: compare pointee types recursively
        if (pointee == o.pointee) return true;
        if (!pointee || !o.pointee) return false;
        return *pointee == *o.pointee;
    }
    bool operator!=(const Type& o) const { return !(*this == o); }
};

// Return the size in bytes of a value of the given type.
inline size_t type_size(const Type& t) {
    if (t.kind == Type::Ptr || t.kind == Type::Func) return 8;  // pointers are 8 bytes
    if (t.kind == Type::Float) return t.float_type == FloatType::f32 ? 4 : 8;
    if (t.kind == Type::Struct) return 8; // fallback; layouts come from StructInfo
    switch (t.int_type) {
        case IntType::i8:  case IntType::u8:  return 1;
        case IntType::i16: case IntType::u16: return 2;
        case IntType::i32: case IntType::u32: return 4;
        case IntType::i64: case IntType::u64: return 8;
    }
    return 8; // unreachable
}

// ── Expression AST nodes ───────────────────────────────────────────────────

struct NodeExprIntLit { Token int_lit; };
struct NodeExprFloatLit { Token float_lit; };  // text keeps an optional 'f' suffix (f32)
struct NodeExprIdent { Token ident; };

struct NodeExpr;

struct NodeExprCall {
    Token name;               // callee name; "(indirect)" at the '(' when `callee` is set
    std::vector<NodeExpr*> args;
    NodeExpr* callee = nullptr; // non-null: call through the function pointer this evaluates to
};

struct NodeExprStringLit { Token value; };

struct NodeExprIndex {
    Token name;
    NodeExpr* index;
    NodeExpr* base = nullptr; // non-null for chained a[i][j]
};

struct NodeExprFieldAccess {
    Token obj_name;
    Token field_name;
    bool arrow = false;    // true for p->field (obj_name is a struct pointer)
    NodeExpr* base = nullptr; // non-null for chained access like a.b.c
};

// Address-of expression: &var
struct NodeExprAddrOf {
    Token ampersand; // the & token (for source location)
    NodeExpr* expr;  // the variable being addressed
};

// Dereference expression: *ptr
struct NodeExprDeref {
    Token star;      // the * token (for source location)
    NodeExpr* expr;  // the pointer expression
};

struct BinExprAdd  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprMulti { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprSub  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprDiv  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprMod  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprLT   { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprGT   { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprEQ   { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprNEQ  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprLTE  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprGTE  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprAnd  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprOr   { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprBitAnd { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprBitOr  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprXor  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprShl  { NodeExpr* lhs; NodeExpr* rhs; };
struct BinExprShr  { NodeExpr* lhs; NodeExpr* rhs; };

struct BinExpr {
    std::variant<BinExprAdd*, BinExprMulti*, BinExprSub*, BinExprDiv*, BinExprMod*,
                 BinExprLT*, BinExprGT*, BinExprLTE*, BinExprGTE*,
                 BinExprEQ*, BinExprNEQ*,
                 BinExprAnd*, BinExprOr*,
                 BinExprBitAnd*, BinExprBitOr*, BinExprXor*, BinExprShl*, BinExprShr*> var;
};

struct NodeExprBitNot { NodeExpr* expr; };
struct NodeExprLogNot { NodeExpr* expr; };

struct NodeExprTernary {
    NodeExpr* cond;
    NodeExpr* then_expr;
    NodeExpr* else_expr;
};

struct NodeExprRead {
    NodeExpr* buffer = nullptr;  // nullptr = read integer, otherwise read string into buffer
    SourceLoc loc {};
};

struct NodeExprArrLit {
    std::vector<NodeExpr*> elements;
};

struct NodeExprSizeof {
    std::optional<Type> type;    // sizeof(i32), sizeof(u8*)
    std::string struct_name;     // sizeof(S)
    NodeExpr* expr = nullptr;    // sizeof(expr)
    SourceLoc loc{};
};

struct StructLitField {
    Token name;
    NodeExpr* value;
};

struct NodeExprStructLit {
    std::vector<StructLitField> fields;
};

struct NodeExprCast {
    NodeExpr* expr;
    Type target;
    SourceLoc loc{};
};

struct NodeExpr {
    std::variant<NodeExprIntLit*, NodeExprFloatLit*, NodeExprIdent*, BinExpr*, NodeExprCall*, NodeExprStringLit*, NodeExprIndex*, NodeExprBitNot*, NodeExprLogNot*, NodeExprTernary*, NodeExprRead*, NodeExprArrLit*, NodeExprFieldAccess*, NodeExprAddrOf*, NodeExprDeref*, NodeExprSizeof*, NodeExprCast*, NodeExprStructLit*> var;
};

// ── Statement AST nodes ────────────────────────────────────────────────────

struct NodeStmtExit { NodeExpr* expr; };

struct NodeStmtReturn {
    NodeExpr* expr;
    SourceLoc loc {};
};

struct NodeStmtArrDecl {
    Token name;
    NodeExpr* size;                    // first dimension (kept for compatibility)
    SourceLoc loc {};
    std::optional<Type> elem_type;     // empty = i64 elements
    std::vector<NodeExpr*> dims;       // all dimensions, outermost first
};

struct NodeStmtArrAssign {
    Token name;
    NodeExpr* index;
    NodeExpr* expr;
    NodeExpr* base = nullptr; // inner index for a[i][j] = v
};

struct NodeStmtLet {
    Token ident;
    NodeExpr* expr;
    std::optional<IntType> type {};
    std::optional<Type> full_type;    // pointer types from `var p: u8* = ...`
    std::string struct_type_name {}; // non-empty if struct-typed (e.g., "var p: Point")
};

struct NodeBlock;
struct NodeStmtFor;
struct NodeStmtSwitch;
struct NodeStmtGlobal;
struct NodeStmtConst;
struct NodeStmtExpr;
struct NodeStmtBreak {
    SourceLoc loc {};
};
struct NodeStmtContinue {
    SourceLoc loc {};
};

struct NodeStmtDoWhile {
    NodeExpr* cond;
    NodeBlock* body;
};

struct NodeStmtIf {
    NodeExpr* cond;
    NodeBlock* then_block;
    NodeBlock* else_block;
};

struct NodeStmtWhile {
    NodeExpr* cond;
    NodeBlock* body;
};

struct NodeStmtBlock {
    NodeBlock* block;
};

struct FormatSpec {
    enum Type : uint8_t { Text, Int, String, LiteralPercent, Hex, Unsigned, Char,
                          Float,  ///< %f: a float printed in fixed notation (D-038)
                          Any     ///< print(expr): an integer or a float, by the argument's type
    };
    Type type;
    std::string text;  // for Text fragments
};

struct NodeStmtPrint {
    std::vector<FormatSpec> specs;
    std::vector<NodeExpr*> args;
};

enum class AssignOp {
    assign,
    add_assign, sub_assign, mul_assign, div_assign, mod_assign,
    bitand_assign, bitor_assign, bitxor_assign,
    shl_assign, shr_assign,
};

struct NodeStmtAssign {
    Token ident;
    NodeExpr* expr;
    AssignOp op = AssignOp::assign;
};

struct NodeStmtFieldAssign {
    Token obj_name;
    Token field_name;
    NodeExpr* base = nullptr; // non-null for chained LHS like a.b.c
    NodeExpr* expr;
    AssignOp op = AssignOp::assign;
};

struct NodeStmtArrowAssign {
    Token ptr_name;
    Token field_name;
    NodeExpr* expr;
    AssignOp op = AssignOp::assign;
};

// Assignment through pointer dereference: *ptr = expr, *ptr += expr
struct NodeStmtDerefAssign {
    Token star;       // the * token
    NodeExpr* ptr_expr;  // the pointer expression
    NodeExpr* expr;   // the value
    AssignOp op = AssignOp::assign;
};

// Inline assembly statement (D-036): target tag, one instruction line and a
// list of clobbered physical register names.
struct NodeStmtAsm {
    Token keyword;
    std::string target;
    std::string text;
    std::vector<std::string> clobbers;
};

struct NodeStmt {
    std::variant<NodeStmtExit*, NodeStmtLet*, NodeStmtIf*, NodeStmtWhile*,
                 NodeStmtDoWhile*, NodeStmtSwitch*, NodeStmtGlobal*, NodeStmtConst*,
                 NodeStmtExpr*, NodeStmtAssign*, NodeStmtFor*, NodeStmtPrint*,
                 NodeStmtBlock*, NodeStmtReturn*, NodeStmtArrDecl*, NodeStmtArrAssign*,
                 NodeStmtBreak*, NodeStmtContinue*, NodeStmtFieldAssign*, NodeStmtDerefAssign*, NodeStmtArrowAssign*, NodeStmtAsm*> var;
};

struct NodeStmtGlobal {
    Token name;
    NodeExpr* expr; // nullptr = zero-init (.bss)
    NodeExpr* array_size = nullptr; // nullptr = scalar, or array count
};

struct NodeStmtConst {
    Token name;
    NodeExpr* expr;
};

struct NodeStmtExpr { NodeExpr* expr; };

struct SwitchCase {
    NodeExpr* value;
    std::vector<NodeStmt> stmts;
};

struct NodeStmtSwitch {
    NodeExpr* expr;
    std::vector<SwitchCase> cases;
};

struct NodeStmtFor {
    std::optional<NodeStmt> init;
    NodeExpr* cond;
    NodeStmtAssign* update;
    NodeBlock* body;
};

struct NodeFuncDef {
    Token name;
    std::vector<Token> params;
    // Parallel to params; an empty optional means the parameter is untyped.
    std::vector<std::optional<Type>> param_types;
    std::optional<Type> return_type; // empty = untyped (inferred/void-like)
    NodeBlock* body;
};

// ── Struct definitions ─────────────────────────────────────────────────────

struct NodeStructDef {
    Token name;
    std::vector<Token> fields;
    // Parallel to fields; empty optional = untyped field (i64).
    std::vector<std::optional<Type>> field_types;
};

struct NodeBlock {
    std::vector<NodeStmt> stmts;
};

struct NodeTypeAlias {
    Token name;
    Type type;
};

struct EnumMember {
    Token name;
    NodeExpr* value = nullptr; // nullptr = previous + 1
};

struct NodeEnumDef {
    Token name;
    std::vector<EnumMember> members;
};

struct NodeProg {
    std::vector<NodeStmt> stmts;
    std::vector<NodeFuncDef*> funcs;
    std::vector<NodeStructDef*> structs;
    std::vector<NodeTypeAlias*> aliases;
    std::vector<NodeEnumDef*> enums;
};

// ── Struct type info (populated during parsing for type lookup) ────────────

struct StructTypeInfo {
    std::vector<std::string> field_names;
    size_t size; // total size in qwords (one per field)
};

// ── Parser ─────────────────────────────────────────────────────────────────

/// Thrown by error/fail_at to unwind to parse_prog's recovery loop.
struct ParseAbort {};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    /// Diagnostics collected while parsing. Owned by this parser, not global.
    const DiagnosticEngine& diagnostics() const { return m_diag; }

    /// Apply the command-line warning policy (`-w`, `-Werror`).
    void set_warn_config(WarnConfig cfg) { m_diag.set_warn_config(cfg); }
    void set_color(bool v) { m_diag.set_color(v); }

    /// Record an error at an explicit location and report it.
    [[noreturn]] void fail_at(const SourceLoc& loc, const std::string& msg);

    /// Skip to the next statement/declaration boundary after an error.
    void synchronize();

    std::optional<NodeProg> parse_prog();

    std::optional<NodeExpr*> parse_expr();
    std::optional<NodeExpr*> parse_or_expr();
    std::optional<NodeExpr*> parse_and_expr();
    std::optional<NodeExpr*> parse_bitor_expr();
    std::optional<NodeExpr*> parse_bitxor_expr();
    std::optional<NodeExpr*> parse_bitand_expr();
    std::optional<NodeExpr*> parse_eq_expr();
    std::optional<NodeExpr*> parse_cmp_expr();
    std::optional<NodeExpr*> parse_shift_expr();
    std::optional<NodeExpr*> parse_add_expr();
    std::optional<NodeExpr*> parse_mul_expr();
    std::optional<NodeExpr*> parse_primary_expr();
    std::optional<NodeExpr*> parse_unit_expr();
    /// Wrap an expression in any trailing `as T` casts (unary-level precedence).
    NodeExpr* apply_casts(NodeExpr* expr);

    std::optional<NodeExpr*> parse_unary();

    std::optional<NodeStmt> parse_stmt();
    std::optional<NodeStmt> parse_print_stmt();
    std::optional<NodeStmt> parse_asm_stmt();
    std::optional<NodeStmt> parse_return_stmt();
    std::optional<NodeStmt> parse_while_stmt();
    std::optional<NodeStmt> parse_do_while_stmt();
    std::optional<NodeStmt> parse_switch_stmt();
    std::optional<NodeStmt> parse_for_stmt();
    std::optional<NodeStmt> parse_if_stmt();
    NodeBlock*              parse_block();
    NodeFuncDef*            parse_func_def();
    NodeStructDef*          parse_struct_def();
    NodeTypeAlias*          parse_type_alias();
    NodeEnumDef*            parse_enum_def();
    std::optional<IntType>  parse_type();
    /// Parse an integer type with any number of trailing '*' pointer levels.
    std::optional<Type>     parse_full_type();
    Type                    parse_fn_type();
    std::optional<std::string> parse_struct_type_name();

private:
    [[nodiscard]] inline std::optional<Token> peek(int offset = 0) const {
        if (m_index + offset >= m_tokens.size()) return {};
        return m_tokens.at(m_index + offset);
    }

    Token consume();

    [[noreturn]] void error(const std::string& msg);

    /// Warn when an '=' appears where a comparison was meant in a condition.
    void warn_assignment_in_condition();

    /// Check if a token type is an assignment operator (=, +=, -=, *=, etc.)
    static bool is_assign_op(TokenType t);
    /// Convert an assignment-operator token to the AssignOp enum.
    static AssignOp token_to_assign_op(TokenType t);
    /// Scan a print format string into its text and conversion fragments.
    std::vector<FormatSpec> parse_format_string(const std::string& fmt);

    const std::vector<Token> m_tokens;
    size_t m_index = 0;
    ArenaAllocator m_allocator;
    DiagnosticEngine m_diag;

    // Struct type registry: struct name -> field info
    // Populated during parsing so type annotations can be resolved.
    std::unordered_map<std::string, StructTypeInfo> m_struct_types;
    // Type aliases are resolved during parsing, so annotations carry real Types.
    std::unordered_map<std::string, Type> m_type_aliases;
    std::unordered_set<std::string> m_enumerators;
};
