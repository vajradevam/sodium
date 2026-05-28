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

struct BinExpr {
    std::variant<BinExprAdd*, BinExprMulti*, BinExprSub*, BinExprDiv*,
                 BinExprLT*, BinExprGT*, BinExprEQ*, BinExprNEQ*> var;
};

struct NodeExpr {
    std::variant<NodeExprIntLit*, NodeExprIdent*, BinExpr*> var;
};

struct NodeStmtExit {
    NodeExpr* expr;
};

struct NodeStmtLet {
    Token ident;
    NodeExpr* expr;
};

struct NodeBlock;

struct NodeStmtIf {
    NodeExpr* cond;
    NodeBlock* then_block;
    NodeBlock* else_block;
};

struct NodeStmtWhile {
    NodeExpr* cond;
    NodeBlock* body;
};

struct NodeStmtAssign {
    Token ident;
    NodeExpr* expr;
};

struct NodeStmt {
    std::variant<NodeStmtExit*, NodeStmtLet*, NodeStmtIf*, NodeStmtWhile*, NodeStmtAssign*> var;
};

struct NodeBlock {
    std::vector<NodeStmt> stmts;
};

struct NodeProg {
    std::vector<NodeStmt> stmts;
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
        return parse_eq_expr();
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
        auto lhs = parse_add_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::lt || peek().value().type == TokenType::gt)) {
            auto op = consume().type;
            auto rhs = parse_add_expr();
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

        while (peek().has_value() && (peek().value().type == TokenType::star || peek().value().type == TokenType::slash)) {
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
            } else {
                auto node = m_allocator.alloc<BinExprDiv>();
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
            auto expr_ident = m_allocator.alloc<NodeExprIdent>();
            expr_ident->ident = consume();
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = expr_ident;
            return expr;
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
            else_block = parse_block();
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

    std::optional<NodeStmt> parse_stmt() {
        if (peek().has_value() && peek().value().type == TokenType::exit && peek(1).has_value() && peek(1).value().type == TokenType::open_paren) {
            consume();
            consume();

            auto stmt_exit = m_allocator.alloc<NodeStmtExit>();

            if (auto node_expr = parse_expr()) {
                stmt_exit->expr = node_expr.value();
            }

            else {
                std::cerr << "INVALID EXPRESSION!" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (peek().has_value() && peek().value().type == TokenType::close_paren) {
                consume();
            } else {
                std::cerr << "Expected ')'" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (peek().has_value() && peek().value().type == TokenType::semi) {
                consume();
            } else {
                std::cerr << "Expected ';'" << std::endl;
                exit(EXIT_FAILURE);
            }

            return NodeStmt { .var = stmt_exit };
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
        } else if (peek().has_value() && peek().value().type == TokenType::_while) {
            return parse_while_stmt();
        } else if (
            peek().has_value() && peek().value().type == TokenType::ident
            && peek(1).has_value() && peek(1).value().type == TokenType::eq) {
                auto stmt_assign = m_allocator.alloc<NodeStmtAssign>();
                stmt_assign->ident = consume();
                consume();
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
        } else {
            return {};
        }
    }

    std::optional<NodeProg> parse_prog() {
        NodeProg prog;
        while (peek().has_value()) {
            if (auto stmt = parse_stmt()) {
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
