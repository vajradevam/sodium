#pragma once

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "tokenization.hpp"

struct NodeExprIntLit {
    Token int_lit;
};

struct NodeExprIdent {
    Token ident;
};

struct NodeExpr;

struct NodeExprCall {
    Token name;
    std::vector<NodeExpr*> args;
};

struct NodeExprStringLit {
    Token value;
};

struct NodeExprIndex {
    Token name;
    NodeExpr* index;
};

struct BinExprAdd {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprMulti {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprSub {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprDiv {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprMod {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprLT {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprGT {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprEQ {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprNEQ {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprLTE {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprGTE {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprAnd {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprOr {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprBitAnd {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprBitOr {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprXor {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprShl {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExprShr {
    NodeExpr* lhs;
    NodeExpr* rhs;
};

struct BinExpr {
    std::variant<BinExprAdd*, BinExprMulti*, BinExprSub*, BinExprDiv*, BinExprMod*,
                 BinExprLT*, BinExprGT*, BinExprLTE*, BinExprGTE*,
                 BinExprEQ*, BinExprNEQ*,
                 BinExprAnd*, BinExprOr*,
                 BinExprBitAnd*, BinExprBitOr*, BinExprXor*, BinExprShl*, BinExprShr*> var;
};

struct NodeExprBitNot {
    NodeExpr* expr;
};

struct NodeExprTernary {
    NodeExpr* cond;
    NodeExpr* then_expr;
    NodeExpr* else_expr;
};

struct NodeExpr {
    std::variant<NodeExprIntLit*, NodeExprIdent*, BinExpr*, NodeExprCall*, NodeExprStringLit*, NodeExprIndex*, NodeExprBitNot*, NodeExprTernary*> var;
};

struct NodeStmtExit {
    NodeExpr* expr;
};

struct NodeStmtReturn {
    NodeExpr* expr;
};

struct NodeStmtArrDecl {
    Token name;
    NodeExpr* size;
};

struct NodeStmtArrAssign {
    Token name;
    NodeExpr* index;
    NodeExpr* expr;
};

struct NodeStmtLet {
    Token ident;
    NodeExpr* expr;
};

struct NodeBlock;
struct NodeStmtFor;
struct NodeStmtBreak {};
struct NodeStmtContinue {};

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

struct NodeStmtPrint {
    NodeExpr* expr;
};

enum class AssignOp {
    assign,
    add_assign,
    sub_assign,
    mul_assign,
    div_assign,
    mod_assign,
    bitand_assign,
    bitor_assign,
    bitxor_assign,
    shl_assign,
    shr_assign,
};

struct NodeStmtAssign {
    Token ident;
    NodeExpr* expr;
    AssignOp op = AssignOp::assign;
};

struct NodeStmt {
    std::variant<NodeStmtExit*, NodeStmtLet*, NodeStmtIf*, NodeStmtWhile*, NodeStmtDoWhile*, NodeStmtAssign*, NodeStmtFor*, NodeStmtPrint*, NodeStmtBlock*, NodeStmtReturn*, NodeStmtArrDecl*, NodeStmtArrAssign*, NodeStmtBreak*, NodeStmtContinue*> var;
};

struct NodeStmtFor {
    std::optional<NodeStmt> init;
    NodeExpr* cond;
    NodeStmtAssign* update;
    NodeBlock* body;
};

struct NodeBlock {
    std::vector<NodeStmt> stmts;
};

struct NodeFuncDef {
    Token name;
    std::vector<Token> params;
    NodeBlock* body;
};

struct NodeProg {
    std::vector<NodeStmt> stmts;
    std::vector<NodeFuncDef*> funcs;
};

class Parser {
public:
    inline explicit Parser(std::vector<Token> tokens)
    : m_tokens(std::move(tokens)),
    m_allocator(1024 * 1024 * 4)
    {

    }

    std::optional<NodeExpr*> parse_expr()
    {
        auto lhs = parse_or_expr();
        if (!lhs) return {};

        if (peek().has_value() && peek().value().type == TokenType::question) {
            consume(); // ?
            auto then_expr = parse_expr();
            if (!then_expr) {
                std::cerr << "Expected expression after '?'" << std::endl;
                exit(EXIT_FAILURE);
            }
            if (!peek().has_value() || peek().value().type != TokenType::colon) {
                std::cerr << "Expected ':' in ternary expression" << std::endl;
                exit(EXIT_FAILURE);
            }
            consume(); // :
            auto else_expr = parse_expr();
            if (!else_expr) {
                std::cerr << "Expected expression after ':'" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto node = m_allocator.alloc<NodeExprTernary>();
            node->cond = lhs.value();
            node->then_expr = then_expr.value();
            node->else_expr = else_expr.value();
            auto out = m_allocator.alloc<NodeExpr>();
            out->var = node;
            return out;
        }

        return lhs;
    }

    std::optional<NodeExpr*> parse_or_expr()
    {
        auto lhs = parse_and_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::or_t) {
            consume();
            auto rhs = parse_and_expr();
            if (!rhs) {
                std::cerr << "Expected expression after ||" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto node = m_allocator.alloc<BinExprOr>();
            node->lhs = lhs.value();
            node->rhs = rhs.value();
            auto bin = m_allocator.alloc<BinExpr>();
            bin->var = node;
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = bin;
            lhs = expr;
        }
        return lhs;
    }

    std::optional<NodeExpr*> parse_and_expr()
    {
        auto lhs = parse_bitor_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::and_t) {
            consume();
            auto rhs = parse_bitor_expr();
            if (!rhs) {
                std::cerr << "Expected expression after &&" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto node = m_allocator.alloc<BinExprAnd>();
            node->lhs = lhs.value();
            node->rhs = rhs.value();
            auto bin = m_allocator.alloc<BinExpr>();
            bin->var = node;
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = bin;
            lhs = expr;
        }
        return lhs;
    }

    std::optional<NodeExpr*> parse_bitor_expr()
    {
        auto lhs = parse_bitxor_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::pipe) {
            consume();
            auto rhs = parse_bitxor_expr();
            if (!rhs) {
                std::cerr << "Expected expression after |" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto node = m_allocator.alloc<BinExprBitOr>();
            node->lhs = lhs.value();
            node->rhs = rhs.value();
            auto bin = m_allocator.alloc<BinExpr>();
            bin->var = node;
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = bin;
            lhs = expr;
        }
        return lhs;
    }

    std::optional<NodeExpr*> parse_bitxor_expr()
    {
        auto lhs = parse_bitand_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::caret) {
            consume();
            auto rhs = parse_bitand_expr();
            if (!rhs) {
                std::cerr << "Expected expression after ^" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto node = m_allocator.alloc<BinExprXor>();
            node->lhs = lhs.value();
            node->rhs = rhs.value();
            auto bin = m_allocator.alloc<BinExpr>();
            bin->var = node;
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = bin;
            lhs = expr;
        }
        return lhs;
    }

    std::optional<NodeExpr*> parse_bitand_expr()
    {
        auto lhs = parse_eq_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::amp) {
            consume();
            auto rhs = parse_eq_expr();
            if (!rhs) {
                std::cerr << "Expected expression after &" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto node = m_allocator.alloc<BinExprBitAnd>();
            node->lhs = lhs.value();
            node->rhs = rhs.value();
            auto bin = m_allocator.alloc<BinExpr>();
            bin->var = node;
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = bin;
            lhs = expr;
        }
        return lhs;
    }

    std::optional<NodeExpr*> parse_eq_expr()
    {
        auto lhs = parse_cmp_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::eq_eq || peek().value().type == TokenType::neq)) {
            auto op = consume().type;
            auto rhs = parse_cmp_expr();
            if (!rhs) {
                std::cerr << "Expected expression after operator" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (op == TokenType::eq_eq) {
                auto node = m_allocator.alloc<BinExprEQ>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else {
                auto node = m_allocator.alloc<BinExprNEQ>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            }
        }

        return lhs;
    }

    std::optional<NodeExpr*> parse_cmp_expr()
    {
        auto lhs = parse_shift_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::lt || peek().value().type == TokenType::gt
               || peek().value().type == TokenType::lte || peek().value().type == TokenType::gte)) {
            auto op = consume().type;
            auto rhs = parse_shift_expr();
            if (!rhs) {
                std::cerr << "Expected expression after operator" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (op == TokenType::lt) {
                auto node = m_allocator.alloc<BinExprLT>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else if (op == TokenType::lte) {
                auto node = m_allocator.alloc<BinExprLTE>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else if (op == TokenType::gte) {
                auto node = m_allocator.alloc<BinExprGTE>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else {
                auto node = m_allocator.alloc<BinExprGT>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            }
        }

        return lhs;
    }

    std::optional<NodeExpr*> parse_shift_expr()
    {
        auto lhs = parse_add_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::shl || peek().value().type == TokenType::shr)) {
            auto op = consume().type;
            auto rhs = parse_add_expr();
            if (!rhs) {
                std::cerr << "Expected expression after shift operator" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (op == TokenType::shl) {
                auto node = m_allocator.alloc<BinExprShl>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else {
                auto node = m_allocator.alloc<BinExprShr>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            }
        }
        return lhs;
    }

    std::optional<NodeExpr*> parse_add_expr()
    {
        auto lhs = parse_mul_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::plus || peek().value().type == TokenType::minus)) {
            auto op = consume().type;
            auto rhs = parse_mul_expr();
            if (!rhs) {
                std::cerr << "Expected expression after operator" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (op == TokenType::plus) {
                auto node = m_allocator.alloc<BinExprAdd>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else {
                auto node = m_allocator.alloc<BinExprSub>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            }
        }

        return lhs;
    }

    std::optional<NodeExpr*> parse_mul_expr()
    {
        auto lhs = parse_primary_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::star || peek().value().type == TokenType::slash || peek().value().type == TokenType::percent)) {
            auto op = consume().type;
            auto rhs = parse_primary_expr();
            if (!rhs) {
                std::cerr << "Expected expression after operator" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (op == TokenType::star) {
                auto node = m_allocator.alloc<BinExprMulti>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else if (op == TokenType::slash) {
                auto node = m_allocator.alloc<BinExprDiv>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            } else {
                auto node = m_allocator.alloc<BinExprMod>();
                node->lhs = lhs.value();
                node->rhs = rhs.value();
                auto bin = m_allocator.alloc<BinExpr>();
                bin->var = node;
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = bin;
                lhs = expr;
            }
        }

        return lhs;
    }

    std::optional<NodeExpr*> parse_primary_expr()
    {
        if (peek().has_value() && peek().value().type == TokenType::int_lit) {
            auto expr_int_lit = m_allocator.alloc<NodeExprIntLit>();
            expr_int_lit->int_lit = consume();
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = expr_int_lit;
            return expr;
        }
        else if (peek().has_value() && peek().value().type == TokenType::ident) {
            if (peek(1).has_value() && peek(1).value().type == TokenType::open_square) {
                auto expr_index = m_allocator.alloc<NodeExprIndex>();
                expr_index->name = consume();
                consume(); // [
                if (auto idx = parse_expr()) {
                    expr_index->index = idx.value();
                } else {
                    std::cerr << "Expected index expression" << std::endl;
                    exit(EXIT_FAILURE);
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    std::cerr << "Expected ']'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // ]
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = expr_index;
                return expr;
            }
            else if (peek(1).has_value() && peek(1).value().type == TokenType::open_paren) {
                auto expr_call = m_allocator.alloc<NodeExprCall>();
                expr_call->name = consume();
                consume(); // (
                while (peek().has_value() && peek().value().type != TokenType::close_paren) {
                    if (auto arg = parse_expr()) {
                        expr_call->args.push_back(arg.value());
                    } else {
                        std::cerr << "Invalid argument in function call" << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    if (peek().has_value() && peek().value().type == TokenType::comma) {
                        consume();
                    }
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
                    std::cerr << "Expected ')' after function arguments" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // )
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = expr_call;
                return expr;
            } else {
                auto expr_ident = m_allocator.alloc<NodeExprIdent>();
                expr_ident->ident = consume();
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = expr_ident;
                return expr;
            }
        }
        else if (peek().has_value() && peek().value().type == TokenType::string_lit) {
            auto expr_str = m_allocator.alloc<NodeExprStringLit>();
            expr_str->value = consume();
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = expr_str;
            return expr;
        }

        else if (peek().has_value() && peek().value().type == TokenType::minus) {
            consume();
            auto expr = parse_primary_expr();
            if (!expr) {
                std::cerr << "Expected expression after '-'" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto int_lit = m_allocator.alloc<NodeExprIntLit>();
            int_lit->int_lit = Token { .type = TokenType::int_lit, .value = "0" };
            auto node = m_allocator.alloc<BinExprSub>();
            node->lhs = m_allocator.alloc<NodeExpr>();
            node->lhs->var = int_lit;
            node->rhs = expr.value();
            auto bin = m_allocator.alloc<BinExpr>();
            bin->var = node;
            auto out = m_allocator.alloc<NodeExpr>();
            out->var = bin;
            return out;
        }

        else if (peek().has_value() && peek().value().type == TokenType::tilde) {
            consume();
            auto expr = parse_primary_expr();
            if (!expr) {
                std::cerr << "Expected expression after '~'" << std::endl;
                exit(EXIT_FAILURE);
            }
            auto node = m_allocator.alloc<NodeExprBitNot>();
            node->expr = expr.value();
            auto out = m_allocator.alloc<NodeExpr>();
            out->var = node;
            return out;
        }

        else if (peek().has_value() && peek().value().type == TokenType::open_paren) {
            consume();
            auto expr = parse_expr();
            if (!expr) {
                std::cerr << "Expected expression" << std::endl;
                exit(EXIT_FAILURE);
            }
            if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
                std::cerr << "Expected ')'" << std::endl;
                exit(EXIT_FAILURE);
            }
            consume();
            return expr;
        }
        else {
            return {};
        }
    }

    NodeBlock* parse_block()
    {
        auto block = m_allocator.alloc<NodeBlock>();
        if (peek().has_value() && peek().value().type == TokenType::open_brace) {
            consume();
            while (peek().has_value() && peek().value().type != TokenType::close_brace) {
                if (auto stmt = parse_stmt()) {
                    block->stmts.push_back(stmt.value());
                } else {
                    std::cerr << "Invalidddd" << std::endl;
                    exit(EXIT_FAILURE);
                }
            }
            if (!peek().has_value() || peek().value().type != TokenType::close_brace) {
                std::cerr << "Expected '}'" << std::endl;
                exit(EXIT_FAILURE);
            }
            consume();
        } else {
            // single statement without braces
            if (auto stmt = parse_stmt()) {
                block->stmts.push_back(stmt.value());
            } else {
                std::cerr << "Invalidddd" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        return block;
    }

    std::optional<NodeStmt> parse_if_stmt()
    {
        consume(); // if
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            std::cerr << "Expected '(' after if" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // (
        auto cond = parse_expr();
        if (!cond) {
            std::cerr << "Expected expression in if condition" << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            std::cerr << "Expected ')' after if condition" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // )

        auto then_block = parse_block();

        NodeBlock* else_block = nullptr;
        if (peek().has_value() && peek().value().type == TokenType::_else) {
            consume(); // else
            if (peek().has_value() && peek().value().type == TokenType::_if) {
                else_block = m_allocator.alloc<NodeBlock>();
                else_block->stmts.push_back(parse_if_stmt().value());
            } else {
                else_block = parse_block();
            }
        }

        auto stmt_if = m_allocator.alloc<NodeStmtIf>();
        stmt_if->cond = cond.value();
        stmt_if->then_block = then_block;
        stmt_if->else_block = else_block;
        return NodeStmt { .var = stmt_if };
    }

    std::optional<NodeStmt> parse_while_stmt()
    {
        consume(); // while
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            std::cerr << "Expected '(' after while" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // (
        auto cond = parse_expr();
        if (!cond) {
            std::cerr << "Expected expression in while condition" << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            std::cerr << "Expected ')' after while condition" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // )

        auto body = parse_block();

        auto stmt_while = m_allocator.alloc<NodeStmtWhile>();
        stmt_while->cond = cond.value();
        stmt_while->body = body;
        return NodeStmt { .var = stmt_while };
    }

    std::optional<NodeStmt> parse_do_while_stmt()
    {
        consume(); // do
        auto body = parse_block();

        if (!peek().has_value() || peek().value().type != TokenType::_while) {
            std::cerr << "Expected 'while' after do body" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // while

        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            std::cerr << "Expected '(' after while" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // (

        auto cond = parse_expr();
        if (!cond) {
            std::cerr << "Expected expression in do-while condition" << std::endl;
            exit(EXIT_FAILURE);
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            std::cerr << "Expected ')' after do-while condition" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // )

        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            std::cerr << "Expected ';' after do-while" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // ;

        auto stmt_do_while = m_allocator.alloc<NodeStmtDoWhile>();
        stmt_do_while->cond = cond.value();
        stmt_do_while->body = body;
        return NodeStmt { .var = stmt_do_while };
    }

    std::optional<NodeStmt> parse_print_stmt()
    {
        consume(); // print
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            std::cerr << "Expected '(' after print" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // (
        auto expr = parse_expr();
        if (!expr) {
            std::cerr << "Expected expression in print" << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            std::cerr << "Expected ')' after print expression" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // )
        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            std::cerr << "Expected ';' after print" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // ;
        auto stmt_print = m_allocator.alloc<NodeStmtPrint>();
        stmt_print->expr = expr.value();
        return NodeStmt { .var = stmt_print };
    }

    std::optional<NodeStmt> parse_for_stmt()
    {
        consume(); // for
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            std::cerr << "Expected '(' after for" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // (

        std::optional<NodeStmt> init;
        if (peek().has_value() && peek().value().type == TokenType::let
            && peek(1).has_value() && peek(1).value().type == TokenType::ident
            && peek(2).has_value() && peek(2).value().type == TokenType::eq) {
            consume();
            auto stmt_let = m_allocator.alloc<NodeStmtLet>();
            stmt_let->ident = consume();
            consume();
            if (auto expr = parse_expr()) {
                stmt_let->expr = expr.value();
            } else {
                std::cerr << "Invalid expression in for init" << std::endl;
                exit(EXIT_FAILURE);
            }
            init = NodeStmt { .var = stmt_let };
        } else if (peek().has_value() && peek().value().type == TokenType::ident
                   && peek(1).has_value() && peek(1).value().type == TokenType::eq) {
            auto stmt_assign = m_allocator.alloc<NodeStmtAssign>();
            stmt_assign->ident = consume();
            consume();
            if (auto expr = parse_expr()) {
                stmt_assign->expr = expr.value();
            } else {
                std::cerr << "Invalid expression in for init" << std::endl;
                exit(EXIT_FAILURE);
            }
            init = NodeStmt { .var = stmt_assign };
        }

        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            std::cerr << "Expected ';' after for init" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // ;

        NodeExpr* cond = nullptr;
        if (!(peek().has_value() && peek().value().type == TokenType::semi)) {
            auto c = parse_expr();
            if (!c) {
                std::cerr << "Invalid expression in for condition" << std::endl;
                exit(EXIT_FAILURE);
            }
            cond = c.value();
        }

        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            std::cerr << "Expected ';' after for condition" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // ;

        NodeStmtAssign* update = nullptr;
        if (!(peek().has_value() && peek().value().type == TokenType::close_paren)) {
            if (peek().has_value() && peek().value().type == TokenType::ident
                && peek(1).has_value() && peek(1).value().type == TokenType::eq) {
                update = m_allocator.alloc<NodeStmtAssign>();
                update->ident = consume();
                consume(); // eq
                auto rhs = parse_expr();
                if (!rhs) {
                    std::cerr << "Invalid expression in for update" << std::endl;
                    exit(EXIT_FAILURE);
                }
                update->expr = rhs.value();
            } else {
                std::cerr << "Invalid for update (must be assignment)" << std::endl;
                exit(EXIT_FAILURE);
            }
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            std::cerr << "Expected ')' after for update" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // )

        auto body = parse_block();

        auto stmt_for = m_allocator.alloc<NodeStmtFor>();
        stmt_for->init = std::move(init);
        stmt_for->cond = cond;
        stmt_for->update = update;
        stmt_for->body = body;
        return NodeStmt { .var = stmt_for };
    }

    NodeFuncDef* parse_func_def()
    {
        consume(); // function
        if (!peek().has_value() || peek().value().type != TokenType::ident) {
            std::cerr << "Expected function name" << std::endl;
            exit(EXIT_FAILURE);
        }
        auto func = m_allocator.alloc<NodeFuncDef>();
        func->name = consume();

        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            std::cerr << "Expected '(' after function name" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // (

        while (peek().has_value() && peek().value().type != TokenType::close_paren) {
            if (peek().value().type != TokenType::ident) {
                std::cerr << "Expected parameter name" << std::endl;
                exit(EXIT_FAILURE);
            }
            func->params.push_back(consume());
            if (peek().has_value() && peek().value().type == TokenType::comma) {
                consume();
            }
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            std::cerr << "Expected ')' after parameters" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // )

        func->body = parse_block();
        return func;
    }

    std::optional<NodeStmt> parse_return_stmt()
    {
        consume(); // return
        NodeStmtReturn* stmt_ret = nullptr;
        if (peek().has_value() && peek().value().type != TokenType::semi) {
            stmt_ret = m_allocator.alloc<NodeStmtReturn>();
            if (auto expr = parse_expr()) {
                stmt_ret->expr = expr.value();
            } else {
                std::cerr << "Invalid expression in return" << std::endl;
                exit(EXIT_FAILURE);
            }
        } else {
            stmt_ret = m_allocator.alloc<NodeStmtReturn>();
            stmt_ret->expr = nullptr;
        }
        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            std::cerr << "Expected ';' after return" << std::endl;
            exit(EXIT_FAILURE);
        }
        consume(); // ;
        return NodeStmt { .var = stmt_ret };
    }

    std::optional<NodeStmt> parse_stmt() {
        if (peek().has_value() && peek().value().type == TokenType::let
            && peek(1).has_value() && peek(1).value().type == TokenType::ident
            && peek(2).has_value() && peek(2).value().type == TokenType::open_square) {
                consume();
                auto stmt_arr = m_allocator.alloc<NodeStmtArrDecl>();
                stmt_arr->name = consume();
                consume(); // [
                if (auto sz = parse_expr()) {
                    stmt_arr->size = sz.value();
                } else {
                    std::cerr << "Expected array size" << std::endl;
                    exit(EXIT_FAILURE);
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    std::cerr << "Expected ']'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // ]
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    std::cerr << "Expected ';'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // ;
                return NodeStmt { .var = stmt_arr };
        } else if (
            peek().has_value() && peek().value().type == TokenType::let
            && peek(1).has_value() && peek(1).value().type == TokenType::ident
            && peek(2).has_value() && peek(2).value().type == TokenType::eq) {
                consume();
                auto stmt_let = m_allocator.alloc<NodeStmtLet>();
                stmt_let->ident = consume();
                consume();
                if (auto expr = parse_expr()) {
                    stmt_let->expr = expr.value();
                } else {
                    std::cerr << "Just looking like a wow... maa chuda madarchod" << std::endl;
                    exit(EXIT_FAILURE);
                }

                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    std::cerr << "Expected `;`" << std::endl;
                    exit(EXIT_FAILURE);
                }

                return NodeStmt { .var = stmt_let };
        } else if (peek().has_value() && peek().value().type == TokenType::_if) {
            return parse_if_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_do) {
            return parse_do_while_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_while) {
            return parse_while_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_for) {
            return parse_for_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_return) {
            return parse_return_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_print) {
            return parse_print_stmt();
        } else if (
            peek().has_value() && peek().value().type == TokenType::ident
            && peek(1).has_value() && peek(1).value().type == TokenType::open_square) {
                auto stmt_arr_assign = m_allocator.alloc<NodeStmtArrAssign>();
                stmt_arr_assign->name = consume();
                consume(); // [
                if (auto idx = parse_expr()) {
                    stmt_arr_assign->index = idx.value();
                } else {
                    std::cerr << "Expected index in array assignment" << std::endl;
                    exit(EXIT_FAILURE);
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    std::cerr << "Expected ']'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // ]
                if (!peek().has_value() || peek().value().type != TokenType::eq) {
                    std::cerr << "Expected '=' in array assignment" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // =
                if (auto expr = parse_expr()) {
                    stmt_arr_assign->expr = expr.value();
                } else {
                    std::cerr << "Invalid expression in array assignment" << std::endl;
                    exit(EXIT_FAILURE);
                }
                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    std::cerr << "Expected ';' after array assignment" << std::endl;
                    exit(EXIT_FAILURE);
                }
                return NodeStmt { .var = stmt_arr_assign };
        } else if (peek().has_value() && peek().value().type == TokenType::_break) {
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                std::cerr << "Expected ';' after break" << std::endl;
                exit(EXIT_FAILURE);
            }
            consume();
            return NodeStmt { .var = m_allocator.alloc<NodeStmtBreak>() };
        } else if (peek().has_value() && peek().value().type == TokenType::_continue) {
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                std::cerr << "Expected ';' after continue" << std::endl;
                exit(EXIT_FAILURE);
            }
            consume();
            return NodeStmt { .var = m_allocator.alloc<NodeStmtContinue>() };
        } else if (
            peek().has_value() && peek().value().type == TokenType::plusplus) {
                consume();
                if (!peek().has_value() || peek().value().type != TokenType::ident) {
                    std::cerr << "Expected identifier after '++'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                auto ident = consume();
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    std::cerr << "Expected ';' after ++ident" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume();
                auto stmt = m_allocator.alloc<NodeStmtAssign>();
                stmt->ident = ident;
                stmt->op = AssignOp::assign;
                // ident = ident + 1
                auto one = m_allocator.alloc<NodeExpr>();
                one->var = m_allocator.alloc<NodeExprIntLit>();
                std::get<NodeExprIntLit*>(one->var)->int_lit = Token { .type = TokenType::int_lit, .value = "1" };
                auto add = m_allocator.alloc<BinExpr>();
                auto bin_add = m_allocator.alloc<BinExprAdd>();
                auto lhs = m_allocator.alloc<NodeExpr>();
                lhs->var = m_allocator.alloc<NodeExprIdent>();
                std::get<NodeExprIdent*>(lhs->var)->ident = ident;
                bin_add->lhs = lhs;
                bin_add->rhs = one;
                add->var = bin_add;
                auto add_expr = m_allocator.alloc<NodeExpr>();
                add_expr->var = add;
                stmt->expr = add_expr;
                return NodeStmt { .var = stmt };
        } else if (
            peek().has_value() && peek().value().type == TokenType::minusminus) {
                consume();
                if (!peek().has_value() || peek().value().type != TokenType::ident) {
                    std::cerr << "Expected identifier after '--'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                auto ident = consume();
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    std::cerr << "Expected ';' after --ident" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume();
                auto stmt = m_allocator.alloc<NodeStmtAssign>();
                stmt->ident = ident;
                stmt->op = AssignOp::assign;
                auto one = m_allocator.alloc<NodeExpr>();
                one->var = m_allocator.alloc<NodeExprIntLit>();
                std::get<NodeExprIntLit*>(one->var)->int_lit = Token { .type = TokenType::int_lit, .value = "1" };
                auto sub = m_allocator.alloc<BinExpr>();
                auto bin_sub = m_allocator.alloc<BinExprSub>();
                auto lhs = m_allocator.alloc<NodeExpr>();
                lhs->var = m_allocator.alloc<NodeExprIdent>();
                std::get<NodeExprIdent*>(lhs->var)->ident = ident;
                bin_sub->lhs = lhs;
                bin_sub->rhs = one;
                sub->var = bin_sub;
                auto sub_expr = m_allocator.alloc<NodeExpr>();
                sub_expr->var = sub;
                stmt->expr = sub_expr;
                return NodeStmt { .var = stmt };
        } else if (
            peek().has_value() && peek().value().type == TokenType::ident
            && peek(1).has_value()
            && (peek(1).value().type == TokenType::plusplus
             || peek(1).value().type == TokenType::minusminus)) {
                auto ident = consume();
                bool is_inc = consume().type == TokenType::plusplus;
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    std::cerr << "Expected ';' after " << (is_inc ? "i++" : "i--") << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume();
                auto stmt = m_allocator.alloc<NodeStmtAssign>();
                stmt->ident = ident;
                stmt->op = AssignOp::assign;
                auto one = m_allocator.alloc<NodeExpr>();
                one->var = m_allocator.alloc<NodeExprIntLit>();
                std::get<NodeExprIntLit*>(one->var)->int_lit = Token { .type = TokenType::int_lit, .value = "1" };
                auto lhs = m_allocator.alloc<NodeExpr>();
                lhs->var = m_allocator.alloc<NodeExprIdent>();
                std::get<NodeExprIdent*>(lhs->var)->ident = ident;
                if (is_inc) {
                    auto add = m_allocator.alloc<BinExpr>();
                    auto bin_add = m_allocator.alloc<BinExprAdd>();
                    bin_add->lhs = lhs;
                    bin_add->rhs = one;
                    add->var = bin_add;
                    auto add_expr = m_allocator.alloc<NodeExpr>();
                    add_expr->var = add;
                    stmt->expr = add_expr;
                } else {
                    auto sub = m_allocator.alloc<BinExpr>();
                    auto bin_sub = m_allocator.alloc<BinExprSub>();
                    bin_sub->lhs = lhs;
                    bin_sub->rhs = one;
                    sub->var = bin_sub;
                    auto sub_expr = m_allocator.alloc<NodeExpr>();
                    sub_expr->var = sub;
                    stmt->expr = sub_expr;
                }
                return NodeStmt { .var = stmt };
        } else if (
            peek().has_value() && peek().value().type == TokenType::ident
            && peek(1).has_value()
             && (peek(1).value().type == TokenType::eq
              || peek(1).value().type == TokenType::pluseq
              || peek(1).value().type == TokenType::minuseq
              || peek(1).value().type == TokenType::stareq
              || peek(1).value().type == TokenType::slasheq
              || peek(1).value().type == TokenType::percenteq
              || peek(1).value().type == TokenType::ampeq
              || peek(1).value().type == TokenType::pipeeq
              || peek(1).value().type == TokenType::careteq
              || peek(1).value().type == TokenType::shleq
              || peek(1).value().type == TokenType::shreq)) {
                auto stmt_assign = m_allocator.alloc<NodeStmtAssign>();
                stmt_assign->ident = consume();
                auto op_token = consume();
                switch (op_token.type) {
                    case TokenType::pluseq: stmt_assign->op = AssignOp::add_assign; break;
                    case TokenType::minuseq: stmt_assign->op = AssignOp::sub_assign; break;
                    case TokenType::stareq: stmt_assign->op = AssignOp::mul_assign; break;
                    case TokenType::slasheq: stmt_assign->op = AssignOp::div_assign; break;
                    case TokenType::percenteq: stmt_assign->op = AssignOp::mod_assign; break;
                    case TokenType::ampeq: stmt_assign->op = AssignOp::bitand_assign; break;
                    case TokenType::pipeeq: stmt_assign->op = AssignOp::bitor_assign; break;
                    case TokenType::careteq: stmt_assign->op = AssignOp::bitxor_assign; break;
                    case TokenType::shleq: stmt_assign->op = AssignOp::shl_assign; break;
                    case TokenType::shreq: stmt_assign->op = AssignOp::shr_assign; break;
                    default: stmt_assign->op = AssignOp::assign; break;
                }
                if (auto expr = parse_expr()) {
                    stmt_assign->expr = expr.value();
                } else {
                    std::cerr << "Invalid expression in assignment" << std::endl;
                    exit(EXIT_FAILURE);
                }

                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    std::cerr << "Expected `;` after assignment" << std::endl;
                    exit(EXIT_FAILURE);
                }

                return NodeStmt { .var = stmt_assign };
        } else if (peek().has_value() && peek().value().type == TokenType::open_brace) {
            auto stmt_block = m_allocator.alloc<NodeStmtBlock>();
            stmt_block->block = parse_block();
            return NodeStmt { .var = stmt_block };
        } else {
            return {};
        }
    }

    std::optional<NodeProg> parse_prog() {
        NodeProg prog;
        while (peek().has_value()) {
            if (peek().value().type == TokenType::_function) {
                prog.funcs.push_back(parse_func_def());
            } else if (auto stmt = parse_stmt()) {
                prog.stmts.push_back(stmt.value());
            } else {
                std::cerr << "Invalidddd" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        return prog;
    }

private:
    [[nodiscard]] inline std::optional<Token> peek(int offset = 0) const {
        if (m_index + offset >= m_tokens.size()) {
            return {};
        } else {
            return m_tokens.at(m_index + offset);
        }

    }

    inline Token consume() {
        return m_tokens.at(m_index++);
    }

    const std::vector<Token> m_tokens;
    size_t m_index = 0;
    ArenaAllocator m_allocator;
};
