#include "parser.hpp"

Parser::Parser(std::vector<Token> tokens)
    : m_tokens(std::move(tokens)),
      m_allocator(1024 * 1024 * 4)
{
}

Token Parser::consume() {
    return m_tokens.at(m_index++);
}

void Parser::error(const std::string& msg) {
    SourceLoc loc = peek().has_value() ? peek().value().loc : SourceLoc{};
    if (g_lsp_mode) {
        g_lsp_errors.push_back({loc, msg});
        throw LSPAbort();
    }
    std::cerr << format_err(loc, msg) << std::endl;
    print_code_context(loc);
    exit(EXIT_FAILURE);
}


std::optional<NodeExpr*> Parser::parse_expr()
    {
        auto lhs = parse_or_expr();
        if (!lhs) return {};

        if (peek().has_value() && peek().value().type == TokenType::question) {
            consume(); // ?
            auto then_expr = parse_expr();
            if (!then_expr) {
                error("Expected expression after '?'");
            }
            if (!peek().has_value() || peek().value().type != TokenType::colon) {
                error("Expected ':' in ternary expression");
            }
            consume(); // :
            auto else_expr = parse_expr();
            if (!else_expr) {
                error("Expected expression after ':'");
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

std::optional<NodeExpr*> Parser::parse_or_expr()
    {
        auto lhs = parse_and_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::or_t) {
            consume();
            auto rhs = parse_and_expr();
            if (!rhs) {
                error("Expected expression after ||");
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

std::optional<NodeExpr*> Parser::parse_and_expr()
    {
        auto lhs = parse_bitor_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::and_t) {
            consume();
            auto rhs = parse_bitor_expr();
            if (!rhs) {
                error("Expected expression after &&");
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

std::optional<NodeExpr*> Parser::parse_bitor_expr()
    {
        auto lhs = parse_bitxor_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::pipe) {
            consume();
            auto rhs = parse_bitxor_expr();
            if (!rhs) {
                error("Expected expression after |");
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

std::optional<NodeExpr*> Parser::parse_bitxor_expr()
    {
        auto lhs = parse_bitand_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::caret) {
            consume();
            auto rhs = parse_bitand_expr();
            if (!rhs) {
                error("Expected expression after ^");
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

std::optional<NodeExpr*> Parser::parse_bitand_expr()
    {
        auto lhs = parse_eq_expr();
        if (!lhs) return {};

        while (peek().has_value() && peek().value().type == TokenType::amp) {
            consume();
            auto rhs = parse_eq_expr();
            if (!rhs) {
                error("Expected expression after &");
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

std::optional<NodeExpr*> Parser::parse_eq_expr()
    {
        auto lhs = parse_cmp_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::eq_eq || peek().value().type == TokenType::neq)) {
            auto op = consume().type;
            auto rhs = parse_cmp_expr();
            if (!rhs) {
                error("Expected expression after operator");
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

std::optional<NodeExpr*> Parser::parse_cmp_expr()
    {
        auto lhs = parse_shift_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::lt || peek().value().type == TokenType::gt
               || peek().value().type == TokenType::lte || peek().value().type == TokenType::gte)) {
            auto op = consume().type;
            auto rhs = parse_shift_expr();
            if (!rhs) {
                error("Expected expression after operator");
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

std::optional<NodeExpr*> Parser::parse_shift_expr()
    {
        auto lhs = parse_add_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::shl || peek().value().type == TokenType::shr)) {
            auto op = consume().type;
            auto rhs = parse_add_expr();
            if (!rhs) {
                error("Expected expression after shift operator");
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

std::optional<NodeExpr*> Parser::parse_add_expr()
    {
        auto lhs = parse_mul_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::plus || peek().value().type == TokenType::minus)) {
            auto op = consume().type;
            auto rhs = parse_mul_expr();
            if (!rhs) {
                error("Expected expression after operator");
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

std::optional<NodeExpr*> Parser::parse_mul_expr()
    {
        auto lhs = parse_primary_expr();
        if (!lhs) return {};

        while (peek().has_value() && (peek().value().type == TokenType::star || peek().value().type == TokenType::slash || peek().value().type == TokenType::percent)) {
            auto op = consume().type;
            auto rhs = parse_primary_expr();
            if (!rhs) {
                error("Expected expression after operator");
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

std::optional<NodeExpr*> Parser::parse_primary_expr()
    {
        if (peek().has_value() && peek().value().type == TokenType::int_lit) {
            auto expr_int_lit = m_allocator.alloc<NodeExprIntLit>();
            expr_int_lit->int_lit = consume();
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = expr_int_lit;
            return expr;
        }
        else if (peek().has_value() && peek().value().type == TokenType::ident
                 && peek().value().value.has_value() && peek().value().value.value() == "read") {
            if (!peek(1).has_value() || peek(1).value().type != TokenType::open_paren) {
                auto expr_ident = m_allocator.alloc<NodeExprIdent>();
                expr_ident->ident = consume();
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = expr_ident;
                return expr;
            }
            consume(); // read
            consume(); // (
            if (peek().has_value() && peek().value().type != TokenType::close_paren) {
                error("read() takes no arguments");
            }
            consume(); // )
            auto read_expr = m_allocator.alloc<NodeExprRead>();
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = read_expr;
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
                    error("Expected index expression");
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    error("Expected ']'");
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
                        error("Invalid argument in function call");
                    }
                    if (peek().has_value() && peek().value().type == TokenType::comma) {
                        consume();
                    }
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
                    error("Expected ')' after function arguments");
                }
                consume(); // )
                auto expr = m_allocator.alloc<NodeExpr>();
                expr->var = expr_call;
                return expr;
            } else {
                auto ident = consume();
                // Check for field access: ident.field
                if (peek().has_value() && peek().value().type == TokenType::dot) {
                    consume(); // .
                    if (!peek().has_value() || peek().value().type != TokenType::ident) {
                        error("Expected field name after '.'");
                    }
                    auto expr_field = m_allocator.alloc<NodeExprFieldAccess>();
                    expr_field->obj_name = ident;
                    expr_field->field_name = consume();
                    auto expr = m_allocator.alloc<NodeExpr>();
                    expr->var = expr_field;
                    return expr;
                }
                auto expr_ident = m_allocator.alloc<NodeExprIdent>();
                expr_ident->ident = ident;
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
                error("Expected expression after '-'");
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
                error("Expected expression after '~'");
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
                error("Expected expression");
            }
            if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
                error("Expected ')'");
            }
            consume();
            return expr;
        }
        else if (peek().has_value() && peek().value().type == TokenType::open_square) {
            consume(); // [
            auto arr_lit = m_allocator.alloc<NodeExprArrLit>();
            while (peek().has_value() && peek().value().type != TokenType::close_square) {
                if (auto elem = parse_expr()) {
                    arr_lit->elements.push_back(elem.value());
                } else {
                    error("Expected expression in array literal");
                }
                if (peek().has_value() && peek().value().type == TokenType::comma) {
                    consume();
                }
            }
            if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                error("Expected ']'");
            }
            consume(); // ]
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = arr_lit;
            return expr;
        }
        else {
            return {};
        }
}

NodeBlock* Parser::parse_block()
    {
        auto block = m_allocator.alloc<NodeBlock>();
        if (peek().has_value() && peek().value().type == TokenType::open_brace) {
            consume();
            while (peek().has_value() && peek().value().type != TokenType::close_brace) {
                if (auto stmt = parse_stmt()) {
                    block->stmts.push_back(stmt.value());
                } else {
                    error("Invalid statement");
                }
            }
            if (!peek().has_value() || peek().value().type != TokenType::close_brace) {
                error("Expected '}'");
            }
            consume();
        } else {
            // single statement without braces
            if (auto stmt = parse_stmt()) {
                block->stmts.push_back(stmt.value());
            } else {
                error("Invalid statement");
            }
        }
        return block;
    }

    std::optional<NodeStmt> Parser::parse_if_stmt()
    {
        consume(); // if
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            error("Expected '(' after if");
        }
        consume(); // (
        auto cond = parse_expr();
        if (!cond) {
            error("Expected expression in if condition");
        }
        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after if condition");
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

std::optional<NodeStmt> Parser::parse_while_stmt()
    {
        consume(); // while
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            error("Expected '(' after while");
        }
        consume(); // (
        auto cond = parse_expr();
        if (!cond) {
            error("Expected expression in while condition");
        }
        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after while condition");
        }
        consume(); // )

        auto body = parse_block();

        auto stmt_while = m_allocator.alloc<NodeStmtWhile>();
        stmt_while->cond = cond.value();
        stmt_while->body = body;
        return NodeStmt { .var = stmt_while };
}

std::optional<NodeStmt> Parser::parse_do_while_stmt()
    {
        consume(); // do
        auto body = parse_block();

        if (!peek().has_value() || peek().value().type != TokenType::_while) {
            error("Expected 'while' after do body");
        }
        consume(); // while

        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            error("Expected '(' after while");
        }
        consume(); // (

        auto cond = parse_expr();
        if (!cond) {
            error("Expected expression in do-while condition");
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after do-while condition");
        }
        consume(); // )

        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            error("Expected ';' after do-while");
        }
        consume(); // ;

        auto stmt_do_while = m_allocator.alloc<NodeStmtDoWhile>();
        stmt_do_while->cond = cond.value();
        stmt_do_while->body = body;
        return NodeStmt { .var = stmt_do_while };
}

std::optional<NodeStmt> Parser::parse_switch_stmt()
    {
        consume(); // switch
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            error("Expected '(' after switch");
        }
        consume(); // (
        auto expr = parse_expr();
        if (!expr) {
            error("Expected expression in switch");
        }
        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after switch expression");
        }
        consume(); // )
        if (!peek().has_value() || peek().value().type != TokenType::open_brace) {
            error("Expected '{' after switch");
        }
        consume(); // {

        auto stmt_switch = m_allocator.alloc<NodeStmtSwitch>();
        stmt_switch->expr = expr.value();

        while (peek().has_value() && peek().value().type != TokenType::close_brace) {
            SwitchCase sc;
            if (peek().value().type == TokenType::_case) {
                consume(); // case
                auto val = parse_expr();
                if (!val) {
                    error("Expected value after 'case'");
                }
                sc.value = val.value();
                if (!peek().has_value() || peek().value().type != TokenType::colon) {
                    error("Expected ':' after case value");
                }
                consume(); // :
            } else if (peek().value().type == TokenType::_default) {
                consume(); // default
                sc.value = nullptr;
                if (!peek().has_value() || peek().value().type != TokenType::colon) {
                    error("Expected ':' after default");
                }
                consume(); // :
            } else {
                error("Expected 'case' or 'default' in switch");
            }

            while (peek().has_value()
                   && peek().value().type != TokenType::close_brace
                   && peek().value().type != TokenType::_case
                   && peek().value().type != TokenType::_default) {
                if (auto s = parse_stmt()) {
                    sc.stmts.push_back(s.value());
                } else {
                    error("Invalid statement in switch case");
                }
            }
            stmt_switch->cases.push_back(std::move(sc));
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_brace) {
            error("Expected '}' after switch body");
        }
        consume(); // }

        return NodeStmt { .var = stmt_switch };
}

std::optional<NodeStmt> Parser::parse_print_stmt()
    {
        consume(); // print
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            error("Expected '(' after print");
        }
        consume(); // (
        auto expr = parse_expr();
        if (!expr) {
            error("Expected expression in print");
        }
        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after print expression");
        }
        consume(); // )
        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            error("Expected ';' after print");
        }
        consume(); // ;
        auto stmt_print = m_allocator.alloc<NodeStmtPrint>();
        stmt_print->expr = expr.value();
        return NodeStmt { .var = stmt_print };
}

std::optional<NodeStmt> Parser::parse_for_stmt()
    {
        consume(); // for
        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            error("Expected '(' after for");
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
                error("Invalid expression in for init");
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
                error("Invalid expression in for init");
            }
            init = NodeStmt { .var = stmt_assign };
        }

        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            error("Expected ';' after for init");
        }
        consume(); // ;

        NodeExpr* cond = nullptr;
        if (!(peek().has_value() && peek().value().type == TokenType::semi)) {
            auto c = parse_expr();
            if (!c) {
                error("Invalid expression in for condition");
            }
            cond = c.value();
        }

        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            error("Expected ';' after for condition");
        }
        consume(); // ;

        NodeStmtAssign* update = nullptr;
        if (!(peek().has_value() && peek().value().type == TokenType::close_paren)) {
            // ident++ / ident--
            if (peek().has_value() && peek().value().type == TokenType::ident
                && peek(1).has_value()
                && (peek(1).value().type == TokenType::plusplus
                 || peek(1).value().type == TokenType::minusminus)) {
                auto ident = consume();
                bool is_inc = consume().type == TokenType::plusplus;
                update = m_allocator.alloc<NodeStmtAssign>();
                update->ident = ident;
                update->op = AssignOp::assign;
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
                    update->expr = add_expr;
                } else {
                    auto sub = m_allocator.alloc<BinExpr>();
                    auto bin_sub = m_allocator.alloc<BinExprSub>();
                    bin_sub->lhs = lhs;
                    bin_sub->rhs = one;
                    sub->var = bin_sub;
                    auto sub_expr = m_allocator.alloc<NodeExpr>();
                    sub_expr->var = sub;
                    update->expr = sub_expr;
                }
            }
            // ++ident / --ident
            else if (peek().has_value()
                && (peek().value().type == TokenType::plusplus
                 || peek().value().type == TokenType::minusminus)) {
                bool is_inc = consume().type == TokenType::plusplus;
                if (!peek().has_value() || peek().value().type != TokenType::ident) {
                    error("Expected identifier after '++' or '--'");
                }
                auto ident = consume();
                update = m_allocator.alloc<NodeStmtAssign>();
                update->ident = ident;
                update->op = AssignOp::assign;
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
                    update->expr = add_expr;
                } else {
                    auto sub = m_allocator.alloc<BinExpr>();
                    auto bin_sub = m_allocator.alloc<BinExprSub>();
                    bin_sub->lhs = lhs;
                    bin_sub->rhs = one;
                    sub->var = bin_sub;
                    auto sub_expr = m_allocator.alloc<NodeExpr>();
                    sub_expr->var = sub;
                    update->expr = sub_expr;
                }
            }
            else if (peek().has_value() && peek().value().type == TokenType::ident
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
                update = m_allocator.alloc<NodeStmtAssign>();
                update->ident = consume();
                auto op_token = consume();
                switch (op_token.type) {
                    case TokenType::pluseq: update->op = AssignOp::add_assign; break;
                    case TokenType::minuseq: update->op = AssignOp::sub_assign; break;
                    case TokenType::stareq: update->op = AssignOp::mul_assign; break;
                    case TokenType::slasheq: update->op = AssignOp::div_assign; break;
                    case TokenType::percenteq: update->op = AssignOp::mod_assign; break;
                    case TokenType::ampeq: update->op = AssignOp::bitand_assign; break;
                    case TokenType::pipeeq: update->op = AssignOp::bitor_assign; break;
                    case TokenType::careteq: update->op = AssignOp::bitxor_assign; break;
                    case TokenType::shleq: update->op = AssignOp::shl_assign; break;
                    case TokenType::shreq: update->op = AssignOp::shr_assign; break;
                    default: update->op = AssignOp::assign; break;
                }
                auto rhs = parse_expr();
                if (!rhs) {
                    error("Invalid expression in for update");
                }
                update->expr = rhs.value();
            } else {
                error("Invalid for update (must be assignment)");
            }
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after for update");
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

NodeFuncDef* Parser::parse_func_def()
    {
        consume(); // function
        if (!peek().has_value() || peek().value().type != TokenType::ident) {
            error("Expected function name");
        }
        auto func = m_allocator.alloc<NodeFuncDef>();
        func->name = consume();

        if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
            error("Expected '(' after function name");
        }
        consume(); // (

        while (peek().has_value() && peek().value().type != TokenType::close_paren) {
            if (peek().value().type != TokenType::ident) {
                error("Expected parameter name");
            }
            func->params.push_back(consume());
            if (peek().has_value() && peek().value().type == TokenType::comma) {
                consume();
            }
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after parameters");
        }
        consume(); // )

        func->body = parse_block();
        return func;
}

std::optional<NodeStmt> Parser::parse_return_stmt()
    {
        SourceLoc ret_loc = peek().value().loc;
        consume(); // return
        NodeStmtReturn* stmt_ret = nullptr;
        if (peek().has_value() && peek().value().type != TokenType::semi) {
            stmt_ret = m_allocator.alloc<NodeStmtReturn>();
            stmt_ret->loc = ret_loc;
            if (auto expr = parse_expr()) {
                stmt_ret->expr = expr.value();
            } else {
                if (g_lsp_mode) {
                    g_lsp_errors.push_back({ret_loc, "Invalid expression in return"});
                    throw LSPAbort();
                }
                lsp_exit(ret_loc, "Invalid expression in return");
            }
        } else {
            stmt_ret = m_allocator.alloc<NodeStmtReturn>();
            stmt_ret->loc = ret_loc;
            stmt_ret->expr = nullptr;
        }
        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            if (g_lsp_mode) {
                g_lsp_errors.push_back({ret_loc, "Expected ';' after return"});
                throw LSPAbort();
            }
            lsp_exit(ret_loc, "Expected ';' after return");
        }
        consume(); // ;
        return NodeStmt { .var = stmt_ret };
}

std::optional<IntType> Parser::parse_type() {
        if (!peek().has_value()) return {};
        switch (peek().value().type) {
            case TokenType::_i8: consume(); return IntType::i8;
            case TokenType::_i16: consume(); return IntType::i16;
            case TokenType::_i32: consume(); return IntType::i32;
            case TokenType::_i64: consume(); return IntType::i64;
            case TokenType::_u8: consume(); return IntType::u8;
            case TokenType::_u16: consume(); return IntType::u16;
            case TokenType::_u32: consume(); return IntType::u32;
            case TokenType::_u64: consume(); return IntType::u64;
            default: return {};
        }
}

std::optional<NodeStmt> Parser::parse_stmt() {
        if (peek().has_value() && peek().value().type == TokenType::let
            && peek(1).has_value() && peek(1).value().type == TokenType::ident
            && peek(2).has_value() && peek(2).value().type == TokenType::open_square) {
                SourceLoc arr_loc = peek(1).value().loc;
                consume();
                auto stmt_arr = m_allocator.alloc<NodeStmtArrDecl>();
                stmt_arr->name = consume();
                stmt_arr->loc = arr_loc;
                consume(); // [
                if (auto sz = parse_expr()) {
                    stmt_arr->size = sz.value();
                } else {
                    if (g_lsp_mode) {
                        g_lsp_errors.push_back({arr_loc, "Expected array size"});
                        throw LSPAbort();
                    }
                    lsp_exit(arr_loc, "Expected array size");
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    if (g_lsp_mode) {
                        g_lsp_errors.push_back({arr_loc, "Expected ']'"});
                        throw LSPAbort();
                    }
                    lsp_exit(arr_loc, "Expected ']'");
                }
                consume(); // ]
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    if (g_lsp_mode) {
                        g_lsp_errors.push_back({arr_loc, "Expected ';'"});
                        throw LSPAbort();
                    }
                    lsp_exit(arr_loc, "Expected ';'");
                }
                consume(); // ;
                return NodeStmt { .var = stmt_arr };
        } else if (
            peek().has_value() && peek().value().type == TokenType::let
            && peek(1).has_value() && peek(1).value().type == TokenType::ident
            && peek(2).has_value() && peek(2).value().type == TokenType::colon) {
                consume();
                auto stmt_let = m_allocator.alloc<NodeStmtLet>();
                stmt_let->ident = consume();
                consume(); // :
                if (auto type = parse_type()) {
                    stmt_let->type = type.value();
                    if (!peek().has_value() || peek().value().type != TokenType::eq) {
                        error("Expected '=' after type");
                    }
                    consume();
                    if (auto expr = parse_expr()) {
                        stmt_let->expr = expr.value();
                    } else {
                        error("Invalid expression");
                    }
                } else if (auto struct_name = parse_struct_type_name()) {
                    stmt_let->struct_type_name = struct_name.value();
                    stmt_let->expr = nullptr;
                    // Optional initializer: var p: Point = ...
                    if (peek().has_value() && peek().value().type == TokenType::eq) {
                        consume();
                        if (auto expr = parse_expr()) {
                            stmt_let->expr = expr.value();
                        } else {
                            error("Invalid expression");
                        }
                    }
                } else {
                    error("Expected type after ':'");
                }
                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    error("Expected ';'");
                }
                return NodeStmt { .var = stmt_let };
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
                    error("Invalid expression in variable declaration");
                }

                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    error("Expected `;`");
                }

                return NodeStmt { .var = stmt_let };
        } else if (peek().has_value() && peek().value().type == TokenType::_if) {
            return parse_if_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_do) {
            return parse_do_while_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_switch) {
            return parse_switch_stmt();
        } else if (peek().has_value() && peek().value().type == TokenType::_static) {
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::let) {
                error("Expected 'var' after 'static'");
            }
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::ident) {
                error("Expected identifier after 'static var'");
            }
            auto stmt_global = m_allocator.alloc<NodeStmtGlobal>();
            stmt_global->name = consume();
            stmt_global->expr = nullptr;
            stmt_global->array_size = nullptr;
            // Check for array syntax: static var ident[size]
            if (peek().has_value() && peek().value().type == TokenType::open_square) {
                consume(); // [
                if (auto sz = parse_expr()) {
                    stmt_global->array_size = sz.value();
                } else {
                    error("Expected array size expression");
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    error("Expected ']'");
                }
                consume(); // ]
            }
            if (peek().has_value() && peek().value().type == TokenType::colon) {
                consume();
                if (!parse_type()) {
                    error("Expected type after ':'");
                }
            }
            if (peek().has_value() && peek().value().type == TokenType::eq) {
                consume();
                if (auto expr = parse_expr()) {
                    stmt_global->expr = expr.value();
                } else {
                    error("Expected expression in static declaration");
                }
            }
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                error("Expected ';' after static declaration");
            }
            consume();
            return NodeStmt { .var = stmt_global };
        } else if (peek().has_value() && peek().value().type == TokenType::_global) {
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::let) {
                error("Expected 'var' after 'global'");
            }
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::ident) {
                error("Expected identifier after 'global var'");
            }
            auto stmt_global = m_allocator.alloc<NodeStmtGlobal>();
            stmt_global->name = consume();
            stmt_global->expr = nullptr;
            stmt_global->array_size = nullptr;
            // Check for array syntax: global var ident[size]
            if (peek().has_value() && peek().value().type == TokenType::open_square) {
                consume(); // [
                if (auto sz = parse_expr()) {
                    stmt_global->array_size = sz.value();
                } else {
                    error("Expected array size expression");
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    error("Expected ']'");
                }
                consume(); // ]
            }
            if (peek().has_value() && peek().value().type == TokenType::eq) {
                consume();
                if (auto expr = parse_expr()) {
                    stmt_global->expr = expr.value();
                } else {
                    error("Expected expression in global declaration");
                }
            }
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                error("Expected ';' after global declaration");
            }
            consume();
            return NodeStmt { .var = stmt_global };
        } else if (peek().has_value() && peek().value().type == TokenType::_const) {
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::ident) {
                error("Expected identifier after 'const'");
            }
            auto stmt_const = m_allocator.alloc<NodeStmtConst>();
            stmt_const->name = consume();
            if (!peek().has_value() || peek().value().type != TokenType::eq) {
                error("Expected '=' after const name");
            }
            consume();
            if (auto expr = parse_expr()) {
                stmt_const->expr = expr.value();
            } else {
                error("Expected expression in const declaration");
            }
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                error("Expected ';' after const declaration");
            }
            consume();
            return NodeStmt { .var = stmt_const };
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
                    error("Expected index in array assignment");
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    error("Expected ']'");
                }
                consume(); // ]
                if (!peek().has_value() || peek().value().type != TokenType::eq) {
                    error("Expected '=' in array assignment");
                }
                consume(); // =
                if (auto expr = parse_expr()) {
                    stmt_arr_assign->expr = expr.value();
                } else {
                    error("Invalid expression in array assignment");
                }
                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    error("Expected ';' after array assignment");
                }
                return NodeStmt { .var = stmt_arr_assign };
        } else if (peek().has_value() && peek().value().type == TokenType::_break) {
            SourceLoc brk_loc = peek().value().loc;
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                if (g_lsp_mode) {
                    g_lsp_errors.push_back({brk_loc, "Expected ';' after break"});
                    throw LSPAbort();
                }
                lsp_exit(brk_loc, "Expected ';' after break");
            }
            consume();
            auto stmt_break = m_allocator.alloc<NodeStmtBreak>();
            stmt_break->loc = brk_loc;
            return NodeStmt { .var = stmt_break };
        } else if (peek().has_value() && peek().value().type == TokenType::_continue) {
            SourceLoc cont_loc = peek().value().loc;
            consume();
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                if (g_lsp_mode) {
                    g_lsp_errors.push_back({cont_loc, "Expected ';' after continue"});
                    throw LSPAbort();
                }
                lsp_exit(cont_loc, "Expected ';' after continue");
            }
            consume();
            auto stmt_continue = m_allocator.alloc<NodeStmtContinue>();
            stmt_continue->loc = cont_loc;
            return NodeStmt { .var = stmt_continue };
        } else if (
            peek().has_value() && peek().value().type == TokenType::plusplus) {
                consume();
                if (!peek().has_value() || peek().value().type != TokenType::ident) {
                    error("Expected identifier after '++'");
                }
                auto ident = consume();
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    error("Expected ';' after ++ident");
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
                    error("Expected identifier after '--'");
                }
                auto ident = consume();
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    error("Expected ';' after --ident");
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
                    if (g_lsp_mode) {
                        g_lsp_errors.push_back({ident.loc, "Expected ';' after " + std::string(is_inc ? "i++" : "i--")});
                        throw LSPAbort();
                    }
                    lsp_exit(ident.loc, "Expected ';' after " + std::string(is_inc ? "i++" : "i--"));
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
            && peek(1).has_value() && peek(1).value().type == TokenType::dot
            && peek(2).has_value() && peek(2).value().type == TokenType::ident
            && peek(3).has_value()
             && (peek(3).value().type == TokenType::eq
              || peek(3).value().type == TokenType::pluseq
              || peek(3).value().type == TokenType::minuseq
              || peek(3).value().type == TokenType::stareq
              || peek(3).value().type == TokenType::slasheq
              || peek(3).value().type == TokenType::percenteq
              || peek(3).value().type == TokenType::ampeq
              || peek(3).value().type == TokenType::pipeeq
              || peek(3).value().type == TokenType::careteq
              || peek(3).value().type == TokenType::shleq
              || peek(3).value().type == TokenType::shreq)) {
                auto stmt_field_assign = m_allocator.alloc<NodeStmtFieldAssign>();
                stmt_field_assign->obj_name = consume();
                consume(); // .
                stmt_field_assign->field_name = consume();
                auto op_token = consume();
                switch (op_token.type) {
                    case TokenType::pluseq: stmt_field_assign->op = AssignOp::add_assign; break;
                    case TokenType::minuseq: stmt_field_assign->op = AssignOp::sub_assign; break;
                    case TokenType::stareq: stmt_field_assign->op = AssignOp::mul_assign; break;
                    case TokenType::slasheq: stmt_field_assign->op = AssignOp::div_assign; break;
                    case TokenType::percenteq: stmt_field_assign->op = AssignOp::mod_assign; break;
                    case TokenType::ampeq: stmt_field_assign->op = AssignOp::bitand_assign; break;
                    case TokenType::pipeeq: stmt_field_assign->op = AssignOp::bitor_assign; break;
                    case TokenType::careteq: stmt_field_assign->op = AssignOp::bitxor_assign; break;
                    case TokenType::shleq: stmt_field_assign->op = AssignOp::shl_assign; break;
                    case TokenType::shreq: stmt_field_assign->op = AssignOp::shr_assign; break;
                    default: stmt_field_assign->op = AssignOp::assign; break;
                }
                if (auto expr = parse_expr()) {
                    stmt_field_assign->expr = expr.value();
                } else {
                    error("Invalid expression in field assignment");
                }
                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    error("Expected ';' after field assignment");
                }
                return NodeStmt { .var = stmt_field_assign };
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
                    error("Invalid expression in assignment");
                }

                if (peek().has_value() && peek().value().type == TokenType::semi) {
                    consume();
                } else {
                    error("Expected `;` after assignment");
                }

                return NodeStmt { .var = stmt_assign };
        } else if (
            peek().has_value() && peek().value().type == TokenType::ident
            && peek(1).has_value() && peek(1).value().type == TokenType::open_paren) {
            auto stmt_expr = m_allocator.alloc<NodeStmtExpr>();
            auto expr = parse_primary_expr();
            if (!expr) {
                error("Invalid expression statement");
            }
            stmt_expr->expr = expr.value();
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                error("Expected ';' after expression");
            }
            consume();
            return NodeStmt { .var = stmt_expr };
        } else if (peek().has_value() && peek().value().type == TokenType::open_brace) {
            auto stmt_block = m_allocator.alloc<NodeStmtBlock>();
            stmt_block->block = parse_block();
            return NodeStmt { .var = stmt_block };
        } else {
            return {};
        }
}

std::optional<NodeProg> Parser::parse_prog() {
        NodeProg prog;
        while (peek().has_value()) {
            if (peek().value().type == TokenType::_function) {
                prog.funcs.push_back(parse_func_def());
            } else if (peek().value().type == TokenType::_struct) {
                auto struct_def = parse_struct_def();
                // Register the struct type for type resolution
                StructTypeInfo info;
                for (const auto& f : struct_def->fields) {
                    info.field_names.push_back(f.value.value());
                }
                info.size = struct_def->fields.size();
                m_struct_types[struct_def->name.value.value()] = info;
                prog.structs.push_back(struct_def);
            } else if (auto stmt = parse_stmt()) {
                prog.stmts.push_back(stmt.value());
            } else {
                error("Invalid statement at top level");
            }
        }
        return prog;
}

NodeStructDef* Parser::parse_struct_def() {
    consume(); // struct
    if (!peek().has_value() || peek().value().type != TokenType::ident) {
        error("Expected struct name after 'struct'");
    }
    auto def = m_allocator.alloc<NodeStructDef>();
    def->name = consume();
    if (!peek().has_value() || peek().value().type != TokenType::open_brace) {
        error("Expected '{' after struct name");
    }
    consume(); // {
    while (peek().has_value() && peek().value().type != TokenType::close_brace) {
        if (peek().value().type == TokenType::let) {
            consume(); // var
            if (!peek().has_value() || peek().value().type != TokenType::ident) {
                error("Expected field name in struct");
            }
            def->fields.push_back(consume());
            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                error("Expected ';' after struct field");
            }
            consume(); // ;
        } else {
            error("Expected struct field declaration (var name;)");
        }
    }
    if (!peek().has_value() || peek().value().type != TokenType::close_brace) {
        error("Expected '}' after struct fields");
    }
    consume(); // }
    if (!peek().has_value() || peek().value().type != TokenType::semi) {
        error("Expected ';' after struct definition");
    }
    consume(); // ;
    return def;
}

std::optional<std::string> Parser::parse_struct_type_name() {
    if (peek().has_value() && peek().value().type == TokenType::ident
        && peek().value().value.has_value()) {
        auto name = peek().value().value.value();
        auto it = m_struct_types.find(name);
        if (it != m_struct_types.end()) {
            consume(); // consume the struct type name
            return name;
        }
    }
    return {};
}







