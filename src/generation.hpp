#pragma once

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "parser.hpp"

class Generator {

public:
    inline explicit Generator(NodeProg root)
    : m_prog(std::move(root))
    {

    }

    void gen_expr(const NodeExpr& expr)
    {
        struct ExprVisitor {

            Generator* gen;

            void operator()(const NodeExprIntLit* expr_int_lit)
            {
                gen->m_output << "    mov rax, " << expr_int_lit->int_lit.value.value() << "\n";
                gen->push("rax");
            }

            void operator()(const NodeExprIdent* expr_ident)
            {
                if (!gen->m_vars.contains(expr_ident->ident.value.value())) {
                    std::cerr << "Invalid Undeclared Identifier: " << expr_ident->ident.value.value() << std::endl;
                    exit(EXIT_FAILURE);
                }

                const auto var = gen->m_vars.at(expr_ident->ident.value.value());
                std::stringstream offset;
                offset << "QWORD [rsp + " << (gen->m_stack_size - var.stack_loc - 1) * 8 << "]\n";
                gen->push(offset.str());
            }

            void operator()(const BinExpr* bin_expr)
            {
                struct BinVisitor {
                    Generator* gen;
                    void operator()(const BinExprAdd* add) {
                        gen->gen_expr(*add->lhs);
                        gen->gen_expr(*add->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    add rax, rdi\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprMulti* mul) {
                        gen->gen_expr(*mul->lhs);
                        gen->gen_expr(*mul->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    imul rax, rdi\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprSub* sub) {
                        gen->gen_expr(*sub->lhs);
                        gen->gen_expr(*sub->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    sub rax, rdi\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprDiv* div) {
                        gen->gen_expr(*div->lhs);
                        gen->gen_expr(*div->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cqo\n";
                        gen->m_output << "    idiv rdi\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprLT* lt) {
                        gen->gen_expr(*lt->lhs);
                        gen->gen_expr(*lt->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cmp rax, rdi\n";
                        gen->m_output << "    setl al\n";
                        gen->m_output << "    movzx rax, al\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprGT* gt) {
                        gen->gen_expr(*gt->lhs);
                        gen->gen_expr(*gt->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cmp rax, rdi\n";
                        gen->m_output << "    setg al\n";
                        gen->m_output << "    movzx rax, al\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprEQ* eq) {
                        gen->gen_expr(*eq->lhs);
                        gen->gen_expr(*eq->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cmp rax, rdi\n";
                        gen->m_output << "    sete al\n";
                        gen->m_output << "    movzx rax, al\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprNEQ* neq) {
                        gen->gen_expr(*neq->lhs);
                        gen->gen_expr(*neq->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cmp rax, rdi\n";
                        gen->m_output << "    setne al\n";
                        gen->m_output << "    movzx rax, al\n";
                        gen->push("rax");
                    }
                };
                BinVisitor visitor{ .gen = gen };
                std::visit(visitor, bin_expr->var);
            }
        };

        ExprVisitor visitor{ .gen = this };
        std::visit(visitor, expr.var);
    }

    void gen_stmt(const NodeStmt& stmt)
    {
        struct StmtVisitor {
            Generator* gen;
            void operator()(const NodeStmtExit* stmt_exit) const
            {
                gen->gen_expr(*stmt_exit->expr);
                gen->m_output << "    mov rax, 60\n";
                gen->pop("rdi");
                gen->m_output << "    syscall\n";
            }

            void operator()(const NodeStmtLet* stmt_let) const
            {
                if(gen->m_vars.contains(stmt_let->ident.value.value())) {
                    std::cerr << "Identifier already used: " << stmt_let->ident.value.value() << std::endl;
                    exit(EXIT_FAILURE);
                }

                gen->m_vars.insert({stmt_let->ident.value.value(), Var { .stack_loc = gen->m_stack_size }});
                gen->gen_expr(*stmt_let->expr);


            }

            void operator()(const NodeStmtAssign* stmt_assign) const
            {
                if (!gen->m_vars.contains(stmt_assign->ident.value.value())) {
                    std::cerr << "Undeclared identifier: " << stmt_assign->ident.value.value() << std::endl;
                    exit(EXIT_FAILURE);
                }

                const auto var = gen->m_vars.at(stmt_assign->ident.value.value());
                gen->gen_expr(*stmt_assign->expr);
                gen->pop("rax");
                gen->m_output << "    mov QWORD [rsp + " << (gen->m_stack_size - var.stack_loc - 1) * 8 << "], rax\n";
            }

            void operator()(const NodeStmtIf* stmt_if) const
            {
                auto label_else = gen->new_label();
                auto label_end = gen->new_label();

                gen->gen_expr(*stmt_if->cond);
                gen->pop("rax");
                gen->m_output << "    test rax, rax\n";

                if (stmt_if->else_block) {
                    gen->m_output << "    jz " << label_else << "\n";
                } else {
                    gen->m_output << "    jz " << label_end << "\n";
                }

                for (const auto& s : stmt_if->then_block->stmts) {
                    gen->gen_stmt(s);
                }

                if (stmt_if->else_block) {
                    gen->m_output << "    jmp " << label_end << "\n";
                    gen->m_output << label_else << ":\n";
                    for (const auto& s : stmt_if->else_block->stmts) {
                        gen->gen_stmt(s);
                    }
                }

                gen->m_output << label_end << ":\n";
            }

            void operator()(const NodeStmtWhile* stmt_while) const
            {
                auto label_begin = gen->new_label();
                auto label_end = gen->new_label();

                gen->m_output << label_begin << ":\n";
                gen->gen_expr(*stmt_while->cond);
                gen->pop("rax");
                gen->m_output << "    test rax, rax\n";
                gen->m_output << "    jz " << label_end << "\n";

                for (const auto& s : stmt_while->body->stmts) {
                    gen->gen_stmt(s);
                }

                gen->m_output << "    jmp " << label_begin << "\n";
                gen->m_output << label_end << ":\n";
            }
        };

        StmtVisitor visitor { .gen = this };
        std::visit(visitor, stmt.var);
    }

    [[nodiscard]] std::string gen_prog()
    {
        std::stringstream output;
        m_output << "global _start\n_start:\n";

        for (const NodeStmt& stmt : m_prog.stmts) {
            gen_stmt(stmt);
        }

        m_output << "    mov rax, 60\n";
        m_output << "    mov rdi, 0\n";
        m_output << "    syscall\n";
        return m_output.str();
    }

private:

    std::string new_label() {
        return ".L" + std::to_string(m_label_count++);
    }

    void push(const std::string& reg) {
        m_output << "    push " << reg << "\n";
        m_stack_size++;
    }

    void pop(const std::string reg) {
        m_output << "    pop " << reg << "\n";
        m_stack_size--;
    }

    struct Var {
        size_t stack_loc;
    };

    const NodeProg m_prog;
    std::stringstream m_output;
    size_t m_stack_size = 0;
    size_t m_label_count = 0;
    std::unordered_map<std::string, Var> m_vars {};
};
