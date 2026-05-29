#pragma once

#include <iostream>
#include <string>
#include <variant>

#include "parser.hpp"

// Helper for std::visit with multiple lambdas.
template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

inline void print_ast_expr(const NodeExpr* expr, int indent);

inline void print_ast_expr(const NodeExpr& expr, int indent) {
    std::visit(Overloaded{
        [indent](const NodeExprIntLit* e) {
            std::cout << std::string(indent, ' ') << "IntLit(" << e->int_lit.value.value() << ")\n";
        },
        [indent](const NodeExprIdent* e) {
            std::cout << std::string(indent, ' ') << "Ident(" << e->ident.value.value() << ")\n";
        },
        [indent](const BinExpr* e) {
            std::cout << std::string(indent, ' ') << "BinExpr\n";
            struct Printer {
                int indent;
                void print_bin(NodeExpr* lhs, NodeExpr* rhs) {
                    print_ast_expr(lhs, indent + 2);
                    print_ast_expr(rhs, indent + 2);
                }
            };
            Printer p{indent + 2};
            std::visit(Overloaded{
                [&p](const BinExprAdd* b) { std::cout << std::string(p.indent - 2, ' ') << "op: +\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprSub* b) { std::cout << std::string(p.indent - 2, ' ') << "op: -\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprMulti* b) { std::cout << std::string(p.indent - 2, ' ') << "op: *\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprDiv* b) { std::cout << std::string(p.indent - 2, ' ') << "op: /\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprMod* b) { std::cout << std::string(p.indent - 2, ' ') << "op: %\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprLT* b) { std::cout << std::string(p.indent - 2, ' ') << "op: <\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprGT* b) { std::cout << std::string(p.indent - 2, ' ') << "op: >\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprEQ* b) { std::cout << std::string(p.indent - 2, ' ') << "op: ==\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprNEQ* b) { std::cout << std::string(p.indent - 2, ' ') << "op: !=\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprLTE* b) { std::cout << std::string(p.indent - 2, ' ') << "op: <=\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprGTE* b) { std::cout << std::string(p.indent - 2, ' ') << "op: >=\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprAnd* b) { std::cout << std::string(p.indent - 2, ' ') << "op: &&\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprOr* b) { std::cout << std::string(p.indent - 2, ' ') << "op: ||\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprBitAnd* b) { std::cout << std::string(p.indent - 2, ' ') << "op: &\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprBitOr* b) { std::cout << std::string(p.indent - 2, ' ') << "op: |\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprXor* b) { std::cout << std::string(p.indent - 2, ' ') << "op: ^\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprShl* b) { std::cout << std::string(p.indent - 2, ' ') << "op: <<\n"; p.print_bin(b->lhs, b->rhs); },
                [&p](const BinExprShr* b) { std::cout << std::string(p.indent - 2, ' ') << "op: >>\n"; p.print_bin(b->lhs, b->rhs); },
            }, e->var);
        },
        [indent](const NodeExprCall* e) {
            std::cout << std::string(indent, ' ') << "Call(" << e->name.value.value() << ")\n";
            for (auto* arg : e->args) print_ast_expr(arg, indent + 2);
        },
        [indent](const NodeExprStringLit* e) {
            std::cout << std::string(indent, ' ') << "StringLit(\"" << e->value.value.value() << "\")\n";
        },
        [indent](const NodeExprIndex* e) {
            std::cout << std::string(indent, ' ') << "Index(" << e->name.value.value() << ")\n";
            print_ast_expr(e->index, indent + 2);
        },
        [indent](const NodeExprBitNot* e) {
            std::cout << std::string(indent, ' ') << "BitNot\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeExprTernary* e) {
            std::cout << std::string(indent, ' ') << "Ternary\n";
            print_ast_expr(e->cond, indent + 2);
            print_ast_expr(e->then_expr, indent + 2);
            print_ast_expr(e->else_expr, indent + 2);
        },
        [indent](const NodeExprRead*) {
            std::cout << std::string(indent, ' ') << "Read\n";
        },
        [indent](const NodeExprArrLit* e) {
            std::cout << std::string(indent, ' ') << "ArrLit\n";
            for (auto* el : e->elements) print_ast_expr(el, indent + 2);
        },
    }, expr.var);
}

inline void print_ast_expr(const NodeExpr* expr, int indent) {
    if (expr) print_ast_expr(*expr, indent);
}

inline void print_ast_stmt(const NodeStmt& stmt, int indent) {
    std::visit(Overloaded{
        [indent](const NodeStmtExit* e) {
            std::cout << std::string(indent, ' ') << "Exit\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtLet* e) {
            std::cout << std::string(indent, ' ') << "Let(" << e->ident.value.value() << ")\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtIf* e) {
            std::cout << std::string(indent, ' ') << "If\n";
            print_ast_expr(e->cond, indent + 2);
            std::cout << std::string(indent + 2, ' ') << "then:\n";
            for (const auto& s : e->then_block->stmts) print_ast_stmt(s, indent + 4);
            if (e->else_block) {
                std::cout << std::string(indent + 2, ' ') << "else:\n";
                for (const auto& s : e->else_block->stmts) print_ast_stmt(s, indent + 4);
            }
        },
        [indent](const NodeStmtWhile* e) {
            std::cout << std::string(indent, ' ') << "While\n";
            print_ast_expr(e->cond, indent + 2);
            std::cout << std::string(indent + 2, ' ') << "body:\n";
            for (const auto& s : e->body->stmts) print_ast_stmt(s, indent + 4);
        },
        [indent](const NodeStmtDoWhile* e) {
            std::cout << std::string(indent, ' ') << "DoWhile\n";
            std::cout << std::string(indent + 2, ' ') << "body:\n";
            for (const auto& s : e->body->stmts) print_ast_stmt(s, indent + 4);
            print_ast_expr(e->cond, indent + 2);
        },
        [indent](const NodeStmtSwitch* e) {
            std::cout << std::string(indent, ' ') << "Switch\n";
            print_ast_expr(e->expr, indent + 2);
            for (const auto& c : e->cases) {
                std::cout << std::string(indent + 2, ' ') << "Case\n";
                print_ast_expr(c.value, indent + 4);
                for (const auto& s : c.stmts) print_ast_stmt(s, indent + 4);
            }
        },
        [indent](const NodeStmtPrint* e) {
            std::cout << std::string(indent, ' ') << "Print\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtBlock* e) {
            std::cout << std::string(indent, ' ') << "Block\n";
            for (const auto& s : e->block->stmts) print_ast_stmt(s, indent + 2);
        },
        [indent](const NodeStmtAssign* e) {
            static const char* op_names[] = {
                "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
            };
            int op_idx = static_cast<int>(e->op);
            const char* op_str = (op_idx >= 0 && op_idx < 11) ? op_names[op_idx] : "?";
            std::cout << std::string(indent, ' ') << "Assign(" << e->ident.value.value() << " " << op_str << " ...)\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtFor* e) {
            std::cout << std::string(indent, ' ') << "For\n";
            if (e->init) {
                std::cout << std::string(indent + 2, ' ') << "init:\n";
                print_ast_stmt(*e->init, indent + 4);
            }
            if (e->cond) {
                std::cout << std::string(indent + 2, ' ') << "cond:\n";
                print_ast_expr(e->cond, indent + 4);
            }
            if (e->update) {
                std::cout << std::string(indent + 2, ' ') << "update:\n";
                NodeStmt update_wrapper;
                update_wrapper.var = e->update;
                print_ast_stmt(update_wrapper, indent + 4);
            }
            std::cout << std::string(indent + 2, ' ') << "body:\n";
            for (const auto& s : e->body->stmts) print_ast_stmt(s, indent + 4);
        },
        [indent](const NodeStmtReturn* e) {
            std::cout << std::string(indent, ' ') << "Return\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtArrDecl* e) {
            std::cout << std::string(indent, ' ') << "ArrDecl(" << e->name.value.value() << ")\n";
            print_ast_expr(e->size, indent + 2);
        },
        [indent](const NodeStmtArrAssign* e) {
            std::cout << std::string(indent, ' ') << "ArrAssign(" << e->name.value.value() << ")\n";
            print_ast_expr(e->index, indent + 2);
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtGlobal* e) {
            std::cout << std::string(indent, ' ') << "Global(" << e->name.value.value() << ")\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtConst* e) {
            std::cout << std::string(indent, ' ') << "Const(" << e->name.value.value() << ")\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtExpr* e) {
            std::cout << std::string(indent, ' ') << "ExprStmt\n";
            print_ast_expr(e->expr, indent + 2);
        },
        [indent](const NodeStmtBreak*) {
            std::cout << std::string(indent, ' ') << "Break\n";
        },
        [indent](const NodeStmtContinue*) {
            std::cout << std::string(indent, ' ') << "Continue\n";
        },
    }, stmt.var);
}

inline void dump_ast(const NodeProg& prog) {
    std::cout << "Program\n";
    for (const auto& s : prog.stmts) {
        print_ast_stmt(s, 2);
    }
    for (const auto* func : prog.funcs) {
        std::cout << "  Function(" << func->name.value.value() << ")\n";
        std::cout << "    params:";
        for (const auto& p : func->params) {
            std::cout << " " << p.value.value();
        }
        std::cout << "\n";
        std::cout << "    body:\n";
        for (const auto& s : func->body->stmts) {
            print_ast_stmt(s, 6);
        }
    }
}
