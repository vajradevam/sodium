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
// redo this without using pointers; if possible; or rewrite a module in Rust
struct BinExpr {
    std::variant<BinExprAdd*, BinExprMulti*> var;
};

struct NodeExpr {
    std::variant<NodeExprIntLit*, NodeExprIdent*> var;
};

struct NodeStmtExit {
    NodeExpr* expr;
};

struct NodeStmtLet {
    Token ident;
    NodeExpr* expr;
};

struct NodeStmt {
    std::variant<NodeStmtExit*, NodeStmtLet*> var;
};

struct NodeProg {
    std::vector<NodeStmt> stmts;
};

// todo i do not have any idea what is going on here
// primarily because was too busy in assigments and exams
// so was unable to do any changes after 24 january
// a note that the arena allocator does not work properly
// also the lexer is fucked up due to bad linked lists implementation

class Parser {
public:
    inline explicit Parser(std::vector<Token> tokens)
    : m_tokens(std::move(tokens)),
    m_allocator(1024 * 1024 * 4) // 4 MB

    {

    }

    std::optional<NodeExpr*> parse_expr()
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

        else {
            return {};
        }
    }

    std::optional<NodeStmt> parse_stmt() {
        if (peek().value().type == TokenType::exit && peek(1).has_value() && peek(1).value().type == TokenType::open_paren) {
            consume();
            consume();

            NodeStmtExit stmt_exit;

            if (auto node_expr = parse_expr()) {
                stmt_exit =  { .expr = node_expr.value() };
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
                auto stmt_let = NodeStmtLet { .ident = consume() };
                consume();
                if (auto expr = parse_expr()) {
                    stmt_let.expr = expr.value();
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
