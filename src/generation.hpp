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
    struct Var {
        size_t stack_loc;
        size_t array_size = 0;
    };

    struct Scope {
        size_t var_count;
        std::unordered_map<std::string, Var> vars;
    };

    struct LoopContext {
        std::string begin_label;
        std::string end_label;
        std::string continue_label;
    };

    inline explicit Generator(NodeProg root)
    : m_prog(std::move(root))
    {
        m_scopes.push_back({0, {}});
    }

    void gen_func_def(const NodeFuncDef& func)
    {
        size_t saved_stack_size = m_stack_size;

        m_output << func.name.value.value() << ":\n";
        m_output << "    push rbp\n";
        m_output << "    mov rbp, rsp\n";
        m_stack_size = 1;

        enter_scope();
        m_in_function = true;

        for (size_t i = 0; i < func.params.size(); i++) {
            std::string param_name = func.params[i].value.value();
            auto& scope = m_scopes.back();
            scope.vars[param_name] = Var { .stack_loc = m_stack_size };
            scope.var_count++;
            m_output << "    mov rax, [rbp + " << 16 + i * 8 << "]\n";
            m_output << "    push rax\n";
            m_stack_size++;
        }

        for (const auto& s : func.body->stmts) {
            gen_stmt(s);
        }

        m_output << ".L" << func.name.value.value() << "_epilogue:\n";
        m_output << "    mov rsp, rbp\n";
        m_output << "    pop rbp\n";
        m_output << "    ret\n";
        m_in_function = false;

        m_scopes.pop_back();
        m_stack_size = saved_stack_size;
    }

    void enter_scope() {
        m_scopes.push_back({0, {}});
    }

    void exit_scope() {
        auto& scope = m_scopes.back();
        m_stack_size -= scope.var_count;
        if (scope.var_count > 0) {
            m_output << "    add rsp, " << scope.var_count * 8 << "\n";
        }
        m_scopes.pop_back();
    }

    void declare_var(const std::string& name) {
        auto& scope = m_scopes.back();
        if (scope.vars.contains(name)) {
            std::cerr << "Identifier already used in this scope: " << name << std::endl;
            exit(EXIT_FAILURE);
        }
        scope.vars[name] = Var { .stack_loc = m_stack_size };
        scope.var_count++;
    }

    Var lookup_var(const std::string& name) {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (it->vars.contains(name)) {
                return it->vars.at(name);
            }
        }
        std::cerr << "Undeclared identifier: " << name << std::endl;
        exit(EXIT_FAILURE);
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
                const auto var = gen->lookup_var(expr_ident->ident.value.value());
                std::stringstream offset;
                offset << "QWORD [rsp + " << (gen->m_stack_size - var.stack_loc - 1) * 8 << "]\n";
                gen->push(offset.str());
            }

            void operator()(const NodeExprStringLit* expr_str)
            {
                auto str = expr_str->value.value.value();
                auto label = gen->new_string_label();
                gen->m_strings.push_back({label, str});
                gen->m_output << "    mov rax, " << label << "\n";
                gen->push("rax");
            }

            void operator()(const NodeExprIndex* expr_index)
            {
                auto var = gen->lookup_var(expr_index->name.value.value());
                gen->gen_expr(*expr_index->index);
                gen->pop("rdi");
                size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                gen->m_output << "    lea rax, [rsp + " << base_offset << "]\n";
                gen->m_output << "    mov rcx, rdi\n";
                gen->m_output << "    shl rcx, 3\n";
                gen->m_output << "    sub rax, rcx\n";
                gen->m_output << "    mov rax, [rax]\n";
                gen->push("rax");
            }

            void operator()(const NodeExprBitNot* bit_not)
            {
                gen->gen_expr(*bit_not->expr);
                gen->pop("rax");
                gen->m_output << "    not rax\n";
                gen->push("rax");
            }

            void operator()(const NodeExprTernary* ternary)
            {
                auto label_false = gen->new_label();
                auto label_end = gen->new_label();
                gen->gen_expr(*ternary->cond);
                gen->pop("rax");
                gen->m_output << "    test rax, rax\n";
                gen->m_output << "    jz " << label_false << "\n";
                auto saved_stack = gen->m_stack_size;
                gen->gen_expr(*ternary->then_expr);
                gen->m_stack_size = saved_stack + 1;
                gen->m_output << "    jmp " << label_end << "\n";
                gen->m_output << label_false << ":\n";
                gen->m_stack_size = saved_stack;
                gen->gen_expr(*ternary->else_expr);
                gen->m_output << label_end << ":\n";
            }

            void operator()(const NodeExprCall* expr_call)
            {
                for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                    gen->gen_expr(**it);
                }
                gen->m_output << "    call " << expr_call->name.value.value() << "\n";
                size_t arg_count = expr_call->args.size();
                if (arg_count > 0) {
                    gen->m_output << "    add rsp, " << arg_count * 8 << "\n";
                    gen->m_stack_size -= arg_count;
                }
                gen->m_output << "    push rax\n";
                gen->m_stack_size++;
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
                    void operator()(const BinExprMod* mod) {
                        gen->gen_expr(*mod->lhs);
                        gen->gen_expr(*mod->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cqo\n";
                        gen->m_output << "    idiv rdi\n";
                        gen->push("rdx");
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
                    void operator()(const BinExprLTE* lte) {
                        gen->gen_expr(*lte->lhs);
                        gen->gen_expr(*lte->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cmp rax, rdi\n";
                        gen->m_output << "    setle al\n";
                        gen->m_output << "    movzx rax, al\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprGTE* gte) {
                        gen->gen_expr(*gte->lhs);
                        gen->gen_expr(*gte->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    cmp rax, rdi\n";
                        gen->m_output << "    setge al\n";
                        gen->m_output << "    movzx rax, al\n";
                        gen->push("rax");
                    }
                void operator()(const BinExprAnd* and_op) {
                        gen->gen_expr(*and_op->lhs);
                        gen->pop("rax");
                        gen->m_output << "    test rax, rax\n";
                        auto false_label = gen->new_label();
                        auto end_label = gen->new_label();
                        gen->m_output << "    jz " << false_label << "\n";
                        gen->gen_expr(*and_op->rhs);
                        gen->pop("rax");
                        gen->m_output << "    test rax, rax\n";
                        gen->m_output << "    jz " << false_label << "\n";
                        gen->m_output << "    mov rax, 1\n";
                        gen->m_output << "    jmp " << end_label << "\n";
                        gen->m_output << false_label << ":\n";
                        gen->m_output << "    mov rax, 0\n";
                        gen->m_output << end_label << ":\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprOr* or_op) {
                        gen->gen_expr(*or_op->lhs);
                        gen->pop("rax");
                        gen->m_output << "    test rax, rax\n";
                        auto true_label = gen->new_label();
                        auto end_label = gen->new_label();
                        gen->m_output << "    jnz " << true_label << "\n";
                        gen->gen_expr(*or_op->rhs);
                        gen->pop("rax");
                        gen->m_output << "    test rax, rax\n";
                        gen->m_output << "    jnz " << true_label << "\n";
                        gen->m_output << "    mov rax, 0\n";
                        gen->m_output << "    jmp " << end_label << "\n";
                        gen->m_output << true_label << ":\n";
                        gen->m_output << "    mov rax, 1\n";
                        gen->m_output << end_label << ":\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprBitAnd* and_op) {
                        gen->gen_expr(*and_op->lhs);
                        gen->gen_expr(*and_op->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    and rax, rdi\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprBitOr* or_op) {
                        gen->gen_expr(*or_op->lhs);
                        gen->gen_expr(*or_op->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    or rax, rdi\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprXor* xor_op) {
                        gen->gen_expr(*xor_op->lhs);
                        gen->gen_expr(*xor_op->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->m_output << "    xor rax, rdi\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprShl* shl_op) {
                        gen->gen_expr(*shl_op->lhs);
                        gen->gen_expr(*shl_op->rhs);
                        gen->pop("rcx");
                        gen->pop("rax");
                        gen->m_output << "    shl rax, cl\n";
                        gen->push("rax");
                    }
                    void operator()(const BinExprShr* shr_op) {
                        gen->gen_expr(*shr_op->lhs);
                        gen->gen_expr(*shr_op->rhs);
                        gen->pop("rcx");
                        gen->pop("rax");
                        gen->m_output << "    shr rax, cl\n";
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
                gen->declare_var(stmt_let->ident.value.value());
                gen->gen_expr(*stmt_let->expr);
            }

            void operator()(const NodeStmtAssign* stmt_assign) const
            {
                const auto var = gen->lookup_var(stmt_assign->ident.value.value());
                size_t offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                if (stmt_assign->op == AssignOp::assign) {
                    gen->gen_expr(*stmt_assign->expr);
                    gen->pop("rax");
                    gen->m_output << "    mov QWORD [rsp + " << offset << "], rax\n";
                } else {
                    gen->m_output << "    push QWORD [rsp + " << offset << "]\n";
                    gen->m_stack_size++;
                    gen->gen_expr(*stmt_assign->expr);
                    gen->pop("rdi");
                    gen->pop("rax");
                    switch (stmt_assign->op) {
                        case AssignOp::add_assign:
                            gen->m_output << "    add rax, rdi\n"; break;
                        case AssignOp::sub_assign:
                            gen->m_output << "    sub rax, rdi\n"; break;
                        case AssignOp::mul_assign:
                            gen->m_output << "    imul rax, rdi\n"; break;
                        case AssignOp::div_assign:
                            gen->m_output << "    cqo\n";
                            gen->m_output << "    idiv rdi\n"; break;
                        case AssignOp::mod_assign:
                            gen->m_output << "    cqo\n";
                            gen->m_output << "    idiv rdi\n";
                            gen->m_output << "    mov rax, rdx\n"; break;
                        case AssignOp::bitand_assign:
                            gen->m_output << "    and rax, rdi\n"; break;
                        case AssignOp::bitor_assign:
                            gen->m_output << "    or rax, rdi\n"; break;
                        case AssignOp::bitxor_assign:
                            gen->m_output << "    xor rax, rdi\n"; break;
                        case AssignOp::shl_assign:
                            gen->m_output << "    mov rcx, rdi\n";
                            gen->m_output << "    shl rax, cl\n"; break;
                        case AssignOp::shr_assign:
                            gen->m_output << "    mov rcx, rdi\n";
                            gen->m_output << "    shr rax, cl\n"; break;
                        default:
                            break;
                    }
                    gen->m_output << "    mov QWORD [rsp + " << offset << "], rax\n";
                }
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

                gen->enter_scope();
                for (const auto& s : stmt_if->then_block->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                if (stmt_if->else_block) {
                    gen->m_output << "    jmp " << label_end << "\n";
                    gen->m_output << label_else << ":\n";
                    gen->enter_scope();
                    for (const auto& s : stmt_if->else_block->stmts) {
                        gen->gen_stmt(s);
                    }
                    gen->exit_scope();
                }

                gen->m_output << label_end << ":\n";
            }

            void operator()(const NodeStmtWhile* stmt_while) const
            {
                auto label_begin = gen->new_label();
                auto label_end = gen->new_label();

                gen->m_loop_stack.push_back({ .begin_label = label_begin, .end_label = label_end, .continue_label = label_begin });

                gen->m_output << label_begin << ":\n";
                gen->gen_expr(*stmt_while->cond);
                gen->pop("rax");
                gen->m_output << "    test rax, rax\n";
                gen->m_output << "    jz " << label_end << "\n";

                gen->enter_scope();
                for (const auto& s : stmt_while->body->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                gen->m_output << "    jmp " << label_begin << "\n";
                gen->m_output << label_end << ":\n";

                gen->m_loop_stack.pop_back();
            }

            void operator()(const NodeStmtDoWhile* stmt_do_while) const
            {
                auto label_begin = gen->new_label();
                auto label_cont = gen->new_label();
                auto label_end = gen->new_label();

                gen->m_loop_stack.push_back({ .begin_label = label_begin, .end_label = label_end, .continue_label = label_cont });

                gen->m_output << label_begin << ":\n";

                gen->enter_scope();
                for (const auto& s : stmt_do_while->body->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                gen->m_output << label_cont << ":\n";
                gen->gen_expr(*stmt_do_while->cond);
                gen->pop("rax");
                gen->m_output << "    test rax, rax\n";
                gen->m_output << "    jnz " << label_begin << "\n";
                gen->m_output << label_end << ":\n";

                gen->m_loop_stack.pop_back();
            }

            void operator()(const NodeStmtArrDecl* stmt_arr) const
            {
                size_t size = 0;
                if (auto int_lit = std::get_if<NodeExprIntLit*>(&stmt_arr->size->var)) {
                    size = std::stoull((*int_lit)->int_lit.value.value());
                } else {
                    std::cerr << "Array size must be a constant integer" << std::endl;
                    exit(EXIT_FAILURE);
                }
                auto& scope = gen->m_scopes.back();
                scope.vars[stmt_arr->name.value.value()] = Var { .stack_loc = gen->m_stack_size, .array_size = size };
                scope.var_count += size;
                gen->gen_expr(*stmt_arr->size);
                gen->pop("rcx");
                std::string label_loop = gen->new_label();
                std::string label_end = gen->new_label();
                gen->m_output << label_loop << ":\n";
                gen->m_output << "    test rcx, rcx\n";
                gen->m_output << "    jz " << label_end << "\n";
                gen->m_output << "    push 0\n";
                gen->m_stack_size += size;
                gen->m_output << "    dec rcx\n";
                gen->m_output << "    jmp " << label_loop << "\n";
                gen->m_output << label_end << ":\n";
            }

            void operator()(const NodeStmtArrAssign* stmt_arr_assign) const
            {
                gen->gen_expr(*stmt_arr_assign->index);
                gen->gen_expr(*stmt_arr_assign->expr);
                gen->pop("rax");
                gen->pop("rdi");
                auto var = gen->lookup_var(stmt_arr_assign->name.value.value());
                size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                gen->m_output << "    lea rcx, [rsp + " << base_offset << "]\n";
                gen->m_output << "    mov rsi, rdi\n";
                gen->m_output << "    shl rsi, 3\n";
                gen->m_output << "    sub rcx, rsi\n";
                gen->m_output << "    mov [rcx], rax\n";
            }

            void operator()(const NodeStmtReturn* stmt_ret) const
            {
                if (gen->m_in_function) {
                    if (stmt_ret->expr) {
                        gen->gen_expr(*stmt_ret->expr);
                        gen->pop("rax");
                    }
                    gen->m_output << "    mov rsp, rbp\n";
                    gen->m_output << "    pop rbp\n";
                    gen->m_output << "    ret\n";
                } else {
                    if (!stmt_ret->expr) {
                        std::cerr << "return with no value at top level" << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    gen->gen_expr(*stmt_ret->expr);
                    gen->m_output << "    mov rax, 60\n";
                    gen->pop("rdi");
                    gen->m_output << "    syscall\n";
                }
            }

            void operator()(const NodeStmtPrint* stmt_print) const
            {
                gen->gen_expr(*stmt_print->expr);

                gen->m_output << "    pop rax\n";
                gen->m_stack_size--;

                gen->m_output << "    sub rsp, 32\n";
                gen->m_output << "    mov rdi, rsp\n";
                gen->m_output << "    add rdi, 31\n";
                gen->m_output << "    mov byte [rdi], 10\n";

                auto label_non_zero = gen->new_label();
                auto label_neg_ok = gen->new_label();
                auto label_done = gen->new_label();
                auto label_loop = gen->new_label();

                gen->m_output << "    test rax, rax\n";
                gen->m_output << "    jnz " << label_non_zero << "\n";
                gen->m_output << "    dec rdi\n";
                gen->m_output << "    mov byte [rdi], '0'\n";
                gen->m_output << "    jmp " << label_done << "\n";

                gen->m_output << label_non_zero << ":\n";
                gen->m_output << "    mov r8, 0\n";
                gen->m_output << "    cmp rax, 0\n";
                gen->m_output << "    jge " << label_neg_ok << "\n";
                gen->m_output << "    mov r8, 1\n";
                gen->m_output << "    neg rax\n";
                gen->m_output << label_neg_ok << ":\n";

                gen->m_output << label_loop << ":\n";
                gen->m_output << "    dec rdi\n";
                gen->m_output << "    mov rcx, 10\n";
                gen->m_output << "    xor rdx, rdx\n";
                gen->m_output << "    div rcx\n";
                gen->m_output << "    add dl, '0'\n";
                gen->m_output << "    mov [rdi], dl\n";
                gen->m_output << "    test rax, rax\n";
                gen->m_output << "    jnz " << label_loop << "\n";

                gen->m_output << "    cmp r8, 1\n";
                gen->m_output << "    jne " << label_done << "\n";
                gen->m_output << "    dec rdi\n";
                gen->m_output << "    mov byte [rdi], '-'\n";

                gen->m_output << label_done << ":\n";
                gen->m_output << "    mov rsi, rdi\n";
                gen->m_output << "    mov rdx, rsp\n";
                gen->m_output << "    add rdx, 32\n";
                gen->m_output << "    sub rdx, rsi\n";
                gen->m_output << "    mov rax, 1\n";
                gen->m_output << "    mov rdi, 1\n";
                gen->m_output << "    syscall\n";
                gen->m_output << "    add rsp, 32\n";
            }

            void operator()(const NodeStmtBlock* stmt_block) const
            {
                gen->enter_scope();
                for (const auto& s : stmt_block->block->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();
            }

            void operator()(const NodeStmtFor* stmt_for) const
            {
                if (stmt_for->init.has_value()) {
                    gen->gen_stmt(stmt_for->init.value());
                }

                auto label_begin = gen->new_label();
                auto label_cont = gen->new_label();
                auto label_end = gen->new_label();

                gen->m_loop_stack.push_back({ .begin_label = label_begin, .end_label = label_end, .continue_label = label_cont });

                gen->m_output << label_begin << ":\n";

                if (stmt_for->cond) {
                    gen->gen_expr(*stmt_for->cond);
                    gen->pop("rax");
                    gen->m_output << "    test rax, rax\n";
                    gen->m_output << "    jz " << label_end << "\n";
                }

                gen->enter_scope();
                for (const auto& s : stmt_for->body->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                gen->m_output << label_cont << ":\n";
                if (stmt_for->update) {
                    const auto var = gen->lookup_var(stmt_for->update->ident.value.value());
                    gen->gen_expr(*stmt_for->update->expr);
                    gen->pop("rax");
                    gen->m_output << "    mov QWORD [rsp + " << (gen->m_stack_size - var.stack_loc - 1) * 8 << "], rax\n";
                }

                gen->m_output << "    jmp " << label_begin << "\n";
                gen->m_output << label_end << ":\n";

                gen->m_loop_stack.pop_back();
            }

            void operator()(const NodeStmtBreak*) const
            {
                if (gen->m_loop_stack.empty()) {
                    std::cerr << "break outside loop" << std::endl;
                    exit(EXIT_FAILURE);
                }
                gen->m_output << "    jmp " << gen->m_loop_stack.back().end_label << "\n";
            }

            void operator()(const NodeStmtContinue*) const
            {
                if (gen->m_loop_stack.empty()) {
                    std::cerr << "continue outside loop" << std::endl;
                    exit(EXIT_FAILURE);
                }
                gen->m_output << "    jmp " << gen->m_loop_stack.back().continue_label << "\n";
            }
        };

        StmtVisitor visitor { .gen = this };
        std::visit(visitor, stmt.var);
    }

    [[nodiscard]] std::string gen_prog()
    {
        m_output << "global _start\n_start:\n";

        for (const NodeStmt& stmt : m_prog.stmts) {
            gen_stmt(stmt);
        }

        m_output << "    mov rax, 60\n";
        m_output << "    mov rdi, 0\n";
        m_output << "    syscall\n";

        for (const auto& func : m_prog.funcs) {
            gen_func_def(*func);
        }

        if (!m_strings.empty()) {
            m_output << "section .rodata\n";
            for (const auto& entry : m_strings) {
                m_output << entry.label << ": db \"" << entry.value << "\", 0\n";
            }
        }

        return m_output.str();
    }

private:

    std::string new_label() {
        return ".L" + std::to_string(m_label_count++);
    }

    std::string new_string_label() {
        return ".str" + std::to_string(m_string_count++);
    }

    void push(const std::string& reg) {
        m_output << "    push " << reg << "\n";
        m_stack_size++;
    }

    void pop(const std::string reg) {
        m_output << "    pop " << reg << "\n";
        m_stack_size--;
    }

    const NodeProg m_prog;
    std::stringstream m_output;
    size_t m_stack_size = 0;
    struct StringEntry {
        std::string label;
        std::string value;
    };

    size_t m_label_count = 0;
    size_t m_string_count = 0;
    bool m_in_function = false;
    std::vector<Scope> m_scopes;
    std::vector<StringEntry> m_strings;
    std::vector<LoopContext> m_loop_stack;
};
