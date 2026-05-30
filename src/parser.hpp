#pragma once

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "tokenization.hpp"

enum class IntType {
    i8, i16, i32, i64,
    u8, u16, u32, u64
};

// ── Expression AST nodes ───────────────────────────────────────────────────

struct NodeExprIntLit { Token int_lit; };
struct NodeExprIdent { Token ident; };

struct NodeExpr;

struct NodeExprCall {
    Token name;
    std::vector<NodeExpr*> args;
};

struct NodeExprStringLit { Token value; };

struct NodeExprIndex {
    Token name;
    NodeExpr* index;
};

struct NodeExprFieldAccess {
    Token obj_name;
    Token field_name;
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

struct NodeExprRead {};

struct NodeExprArrLit {
    std::vector<NodeExpr*> elements;
};

struct NodeExpr {
    std::variant<NodeExprIntLit*, NodeExprIdent*, BinExpr*, NodeExprCall*, NodeExprStringLit*, NodeExprIndex*, NodeExprBitNot*, NodeExprLogNot*, NodeExprTernary*, NodeExprRead*, NodeExprArrLit*, NodeExprFieldAccess*, NodeExprAddrOf*, NodeExprDeref*> var;
};

// ── Statement AST nodes ────────────────────────────────────────────────────

struct NodeStmtExit { NodeExpr* expr; };

struct NodeStmtReturn {
    NodeExpr* expr;
    SourceLoc loc {};
};

struct NodeStmtArrDecl {
    Token name;
    NodeExpr* size;
    SourceLoc loc {};
};

struct NodeStmtArrAssign {
    Token name;
    NodeExpr* index;
    NodeExpr* expr;
};

struct NodeStmtLet {
    Token ident;
    NodeExpr* expr;
    std::optional<IntType> type {};
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

struct NodeStmtPrint { NodeExpr* expr; };

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

struct NodeStmt {
    std::variant<NodeStmtExit*, NodeStmtLet*, NodeStmtIf*, NodeStmtWhile*,
                 NodeStmtDoWhile*, NodeStmtSwitch*, NodeStmtGlobal*, NodeStmtConst*,
                 NodeStmtExpr*, NodeStmtAssign*, NodeStmtFor*, NodeStmtPrint*,
                 NodeStmtBlock*, NodeStmtReturn*, NodeStmtArrDecl*, NodeStmtArrAssign*,
                 NodeStmtBreak*, NodeStmtContinue*, NodeStmtFieldAssign*, NodeStmtDerefAssign*> var;
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
    NodeBlock* body;
};

// ── Struct definitions ─────────────────────────────────────────────────────

struct NodeStructDef {
    Token name;
    std::vector<Token> fields;
};

struct NodeBlock {
    std::vector<NodeStmt> stmts;
};

struct NodeProg {
    std::vector<NodeStmt> stmts;
    std::vector<NodeFuncDef*> funcs;
    std::vector<NodeStructDef*> structs;
};

// ── Struct type info (populated during parsing for type lookup) ────────────

struct StructTypeInfo {
    std::vector<std::string> field_names;
    size_t size; // total size in qwords (one per field)
};

// ── Parser ─────────────────────────────────────────────────────────────────

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

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

    std::optional<NodeExpr*> parse_unary();

    std::optional<NodeStmt> parse_stmt();
    std::optional<NodeStmt> parse_print_stmt();
    std::optional<NodeStmt> parse_return_stmt();
    std::optional<NodeStmt> parse_while_stmt();
    std::optional<NodeStmt> parse_do_while_stmt();
    std::optional<NodeStmt> parse_switch_stmt();
    std::optional<NodeStmt> parse_for_stmt();
    std::optional<NodeStmt> parse_if_stmt();
    NodeBlock*              parse_block();
    NodeFuncDef*            parse_func_def();
    NodeStructDef*          parse_struct_def();
    std::optional<IntType>  parse_type();
    std::optional<std::string> parse_struct_type_name();

private:
    [[nodiscard]] inline std::optional<Token> peek(int offset = 0) const {
        if (m_index + offset >= m_tokens.size()) return {};
        return m_tokens.at(m_index + offset);
    }

    Token consume();

    [[noreturn]] void error(const std::string& msg);

    const std::vector<Token> m_tokens;
    size_t m_index = 0;
    ArenaAllocator m_allocator;

    // Struct type registry: struct name -> field info
    // Populated during parsing so type annotations can be resolved.
    std::unordered_map<std::string, StructTypeInfo> m_struct_types;
};
