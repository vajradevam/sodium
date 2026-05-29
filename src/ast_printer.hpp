#pragma once

#include "parser.hpp"

// Forward declarations for the AST printer functions.
void dump_ast(const NodeProg& prog);
void print_ast_stmt(const NodeStmt& stmt, int indent);
void print_ast_expr(const NodeExpr& expr, int indent);
void print_ast_expr(const NodeExpr* expr, int indent);
