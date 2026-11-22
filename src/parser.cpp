#include "parser.hpp"

Parser::Parser(std::vector<Token> tokens)
    : m_tokens(std::move(tokens)),
      m_allocator(1024 * 1024 * 4)
{
}

Token Parser::consume() {
    if (m_index >= m_tokens.size()) {
        error("Unexpected end of input");
    }
    return m_tokens.at(m_index++);
}

void Parser::error(const std::string& msg) {
    SourceLoc loc = peek().has_value() ? peek().value().loc : SourceLoc{};
    m_diag.error(loc, msg);
    if (g_lsp_mode) throw LSPAbort();
    throw ParseAbort{};
}

[[noreturn]] void Parser::fail_at(const SourceLoc& loc, const std::string& msg) {
    m_diag.error(loc, msg);
    if (g_lsp_mode) throw LSPAbort();
    throw ParseAbort{};
}

/// Skip tokens until a statement/declaration boundary so parsing can
/// continue after an error. Always consumes at least one token.
void Parser::synchronize() {
    if (!peek().has_value()) return;
    consume(); // guarantee progress
    while (peek().has_value()) {
        TokenType t = peek().value().type;
        if (t == TokenType::semi || t == TokenType::close_brace) {
            consume();
            return;
        }
        if (t == TokenType::_function || t == TokenType::_struct ||
            t == TokenType::_global || t == TokenType::_static ||
            t == TokenType::_const || t == TokenType::let ||
            t == TokenType::_if || t == TokenType::_while ||
            t == TokenType::_for || t == TokenType::_return ||
            t == TokenType::_print || t == TokenType::_switch ||
            t == TokenType::_do || t == TokenType::_break ||
            t == TokenType::_continue) {
            return;
        }
        consume();
    }
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

NodeExpr* Parser::apply_casts(NodeExpr* expr) {
    if (!expr) return nullptr;
    while (peek().has_value() && peek().value().type == TokenType::_as) {
        SourceLoc loc = consume().loc;
        auto target = parse_full_type();
        if (!target.has_value()) error("Expected type after 'as'");
        auto cast = m_allocator.alloc<NodeExprCast>();
        cast->expr = expr;
        cast->target = *target;
        cast->loc = loc;
        auto out = m_allocator.alloc<NodeExpr>();
        out->var = cast;
        expr = out;
    }
    return expr;
}

std::optional<NodeExpr*> Parser::parse_mul_expr()
    {
        auto lhs = parse_primary_expr();
        if (!lhs) return {};
        lhs = apply_casts(*lhs);

        while (peek().has_value() && (peek().value().type == TokenType::star || peek().value().type == TokenType::slash || peek().value().type == TokenType::percent)) {
            auto op = consume().type;
            auto rhs = parse_primary_expr();
            if (!rhs) {
                error("Expected expression after operator");
            }
            rhs = apply_casts(*rhs);

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
        // Pointer dereference: *expr
        if (peek().has_value() && peek().value().type == TokenType::star) {
            auto deref = m_allocator.alloc<NodeExprDeref>();
            deref->star = consume();
            if (auto inner = parse_primary_expr()) {
                deref->expr = inner.value();
            } else {
                if (g_lsp_mode) { m_diag.error(deref->star.loc, "Expected expression after '*'"); throw LSPAbort(); }
                fail_at(deref->star.loc, "Expected expression after '*'");
            }
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = deref;
            return expr;
        }

        // Address-of: &expr
        if (peek().has_value() && peek().value().type == TokenType::amp) {
            auto addr = m_allocator.alloc<NodeExprAddrOf>();
            addr->ampersand = consume();
            if (auto inner = parse_primary_expr()) {
                addr->expr = inner.value();
            } else {
                if (g_lsp_mode) { m_diag.error(addr->ampersand.loc, "Expected expression after '&'"); throw LSPAbort(); }
                fail_at(addr->ampersand.loc, "Expected expression after '&'");
            }
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = addr;
            return expr;
        }

        if (peek().has_value() && peek().value().type == TokenType::open_brace) {
            consume(); // {
            auto lit = m_allocator.alloc<NodeExprStructLit>();
            while (peek().has_value() && peek().value().type != TokenType::close_brace) {
                if (peek().value().type != TokenType::dot) {
                    error("Expected '.' in struct literal");
                }
                consume();
                if (!peek().has_value() || peek().value().type != TokenType::ident) {
                    error("Expected field name in struct literal");
                }
                Token field_name = consume();
                if (!peek().has_value() || peek().value().type != TokenType::eq) {
                    error("Expected '=' after struct literal field name");
                }
                consume();
                auto value = parse_expr();
                if (!value) error("Invalid expression in struct literal");
                lit->fields.push_back({field_name, *value});
                if (peek().has_value() && peek().value().type == TokenType::comma) consume();
                else break;
            }
            if (!peek().has_value() || peek().value().type != TokenType::close_brace) {
                error("Expected '}' after struct literal");
            }
            consume();
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = lit;
            return expr;
        }

        if (peek().has_value() &&
            (peek().value().type == TokenType::_true ||
             peek().value().type == TokenType::_false ||
             peek().value().type == TokenType::_null)) {
            Token tok = consume();
            auto expr_int_lit = m_allocator.alloc<NodeExprIntLit>();
            expr_int_lit->int_lit = Token { .type = TokenType::int_lit,
                .value = std::string(tok.type == TokenType::_true ? "1" : "0"),
                .loc = tok.loc };
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = expr_int_lit;
            return expr;
        }

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
            NodeExpr* read_buf = nullptr;
            if (peek().has_value() && peek().value().type != TokenType::close_paren) {
                auto buf = parse_expr();
                if (buf.has_value()) {
                    read_buf = buf.value();
                } else {
                    error("Expected expression in read()");
                }
            }
            consume(); // )
            auto read_expr = m_allocator.alloc<NodeExprRead>();
            read_expr->loc = peek(-2).value().loc;
            read_expr->buffer = read_buf;
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = read_expr;
            return expr;
        }
        else if (peek().has_value() && peek().value().type == TokenType::ident
                 && peek().value().value.has_value() && *peek().value().value == "sizeof"
                 && peek(1).has_value() && peek(1).value().type == TokenType::open_paren) {
            SourceLoc loc = consume().loc; // sizeof
            consume(); // (
            auto sz = m_allocator.alloc<NodeExprSizeof>();
            sz->loc = loc;
            auto parsed_type = parse_full_type();
            if (parsed_type.has_value()) {
                sz->type = *parsed_type;
            } else if (peek().has_value() && peek().value().type == TokenType::ident
                       && peek().value().value.has_value()
                       && m_struct_types.contains(*peek().value().value)) {
                sz->struct_name = consume().value.value();
            } else if (auto operand = parse_expr()) {
                sz->expr = operand.value();
            } else {
                error("Expected type or expression in sizeof()");
            }
            if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
                error("Expected ')' after sizeof");
            }
            consume();
            auto expr = m_allocator.alloc<NodeExpr>();
            expr->var = sz;
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
                // Multi-dimensional indexing: a[i][j]
                while (peek().has_value() && peek().value().type == TokenType::open_square) {
                    consume(); // [
                    auto next_index = parse_expr();
                    if (!next_index) error("Expected index expression");
                    if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                        error("Expected ']'");
                    }
                    consume();
                    auto chained = m_allocator.alloc<NodeExprIndex>();
                    chained->base = expr;
                    chained->name = expr_index->name;
                    chained->index = *next_index;
                    expr = m_allocator.alloc<NodeExpr>();
                    expr->var = chained;
                }
                // Field access on an indexed element: a[0].x (and chains).
                while (peek().has_value() && peek().value().type == TokenType::dot) {
                    consume();
                    if (!peek().has_value() || peek().value().type != TokenType::ident) {
                        error("Expected field name after '.'");
                    }
                    auto access = m_allocator.alloc<NodeExprFieldAccess>();
                    access->base = expr;
                    access->field_name = consume();
                    expr = m_allocator.alloc<NodeExpr>();
                    expr->var = access;
                }
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
                // Pointer-to-struct field access: p->field
                if (peek().has_value() && peek().value().type == TokenType::arrow) {
                    consume();
                    if (!peek().has_value() || peek().value().type != TokenType::ident) {
                        error("Expected field name after '->'");
                    }
                    auto expr_field = m_allocator.alloc<NodeExprFieldAccess>();
                    expr_field->obj_name = ident;
                    expr_field->field_name = consume();
                    expr_field->arrow = true;
                    auto expr = m_allocator.alloc<NodeExpr>();
                    expr->var = expr_field;
                    return expr;
                }
                // Check for field access: ident.field (and chains a.b.c)
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
                    while (peek().has_value() && peek().value().type == TokenType::dot) {
                        consume(); // .
                        if (!peek().has_value() || peek().value().type != TokenType::ident) {
                            error("Expected field name after '.'");
                        }
                        auto chained = m_allocator.alloc<NodeExprFieldAccess>();
                        chained->base = expr;
                        chained->field_name = consume();
                        expr = m_allocator.alloc<NodeExpr>();
                        expr->var = chained;
                    }
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

        else if (peek().has_value() && peek().value().type == TokenType::bang) {
            consume();
            auto expr = parse_primary_expr();
            if (!expr) {
                error("Expected expression after '!'");
            }
            auto node = m_allocator.alloc<NodeExprLogNot>();
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
                try {
                    if (auto stmt = parse_stmt()) {
                        block->stmts.push_back(stmt.value());
                    } else {
                        error("Invalid statement");
                    }
                } catch (const ParseAbort&) {
                    // Recover inside the block so the rest of the function
                    // (and subsequent functions) are still parsed.
                    while (peek().has_value() &&
                           peek().value().type != TokenType::semi &&
                           peek().value().type != TokenType::close_brace) {
                        consume();
                    }
                    if (peek().has_value() && peek().value().type == TokenType::semi) consume();
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
        warn_assignment_in_condition();
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
        warn_assignment_in_condition();
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
        warn_assignment_in_condition();

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
        warn_assignment_in_condition();
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

// ── Assignment operator helpers ─────────────────────────────────────────

bool Parser::is_assign_op(TokenType t) {
    return t == TokenType::eq
        || t == TokenType::pluseq
        || t == TokenType::minuseq
        || t == TokenType::stareq
        || t == TokenType::slasheq
        || t == TokenType::percenteq
        || t == TokenType::ampeq
        || t == TokenType::pipeeq
        || t == TokenType::careteq
        || t == TokenType::shleq
        || t == TokenType::shreq;
}

AssignOp Parser::token_to_assign_op(TokenType t) {
    switch (t) {
        case TokenType::pluseq:    return AssignOp::add_assign;
        case TokenType::minuseq:   return AssignOp::sub_assign;
        case TokenType::stareq:    return AssignOp::mul_assign;
        case TokenType::slasheq:   return AssignOp::div_assign;
        case TokenType::percenteq: return AssignOp::mod_assign;
        case TokenType::ampeq:     return AssignOp::bitand_assign;
        case TokenType::pipeeq:    return AssignOp::bitor_assign;
        case TokenType::careteq:   return AssignOp::bitxor_assign;
        case TokenType::shleq:     return AssignOp::shl_assign;
        case TokenType::shreq:     return AssignOp::shr_assign;
        default:                   return AssignOp::assign;
    }
}

// Parse a format string literal and return the list of FormatSpecs.
// Scans for %d, %s, %% patterns. Everything else is Text fragments.
static std::vector<FormatSpec> parse_format_string(const std::string& fmt)
{
    std::vector<FormatSpec> specs;
    std::string cur_text;
    size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] == '%' && i + 1 < fmt.size()) {
            // Flush accumulated text
            if (!cur_text.empty()) {
                specs.push_back({FormatSpec::Text, cur_text});
                cur_text.clear();
            }
            char spec = fmt[i + 1];
            switch (spec) {
                case 'd':
                    specs.push_back({FormatSpec::Int, ""});
                    i += 2;
                    break;
                case 's':
                    specs.push_back({FormatSpec::String, ""});
                    i += 2;
                    break;
                case '%':
                    specs.push_back({FormatSpec::LiteralPercent, ""});
                    i += 2;
                    break;
                default:
                    // Unknown spec — treat as literal text
                    cur_text += '%';
                    cur_text += spec;
                    i += 2;
                    break;
            }
        } else {
            cur_text += fmt[i];
            i++;
        }
    }
    // Flush remaining text
    if (!cur_text.empty()) {
        specs.push_back({FormatSpec::Text, cur_text});
    }
    return specs;
}

std::optional<NodeStmt> Parser::parse_print_stmt()
{
    consume(); // print
    if (!peek().has_value() || peek().value().type != TokenType::open_paren) {
        error("Expected '(' after print");
    }
    consume(); // (

    std::vector<NodeExpr*> args;
    std::vector<FormatSpec> specs;

    // Check for empty print() — just a newline
    if (peek().has_value() && peek().value().type == TokenType::close_paren) {
        consume(); // )
        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            error("Expected ';' after print");
        }
        consume(); // ;
        auto stmt_print = m_allocator.alloc<NodeStmtPrint>();
        stmt_print->specs = {};
        stmt_print->args = {};
        return NodeStmt { .var = stmt_print };
    }

    auto first_expr = parse_expr();
    if (!first_expr) {
        error("Expected expression in print");
    }

    // Check if first argument is a string literal (potential format string)
    if (auto* str_lit = std::get_if<NodeExprStringLit*>(&first_expr.value()->var)) {
        // Check for unescaped trailing % in format string
        const auto& fmt_str = (*str_lit)->value.value.value();
        if (!fmt_str.empty() && fmt_str.back() == '%') {
            // Count consecutive % characters from the end
            size_t trail_pct = 0;
            for (auto it = fmt_str.rbegin(); it != fmt_str.rend() && *it == '%'; ++it)
                trail_pct++;
            // Odd count means an unescaped trailing %
            if (trail_pct % 2 == 1) {
                error("Trailing '%' in format string literal");
            }
        }
        // Parse the format string
        specs = parse_format_string(fmt_str);

        // Parse additional comma-separated arguments
        while (peek().has_value() && peek().value().type == TokenType::comma) {
            consume(); // ','
            auto arg = parse_expr();
            if (!arg) {
                error("Expected expression after ','");
            }
            args.push_back(arg.value());
        }
    } else {
        // Old-style: print(expr) — single expression, print as integer
        specs.push_back({FormatSpec::Int, ""});
        args.push_back(first_expr.value());
    }

    // Each %d / %s consumes one argument. Reject a shortage here instead of
    // letting the generator index past the end of args.
    size_t needed_args = 0;
    for (const auto& spec : specs) {
        if (spec.type == FormatSpec::Int || spec.type == FormatSpec::String) {
            ++needed_args;
        }
    }
    if (args.size() < needed_args) {
        error("Not enough arguments for print format string");
    }

    if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
        error("Expected ')'");
    }
    consume(); // )
    if (!peek().has_value() || peek().value().type != TokenType::semi) {
        error("Expected ';' after print");
    }
    consume(); // ;

    auto stmt_print = m_allocator.alloc<NodeStmtPrint>();
    stmt_print->specs = std::move(specs);
    stmt_print->args = std::move(args);
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
            warn_assignment_in_condition();
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
                && is_assign_op(peek(1).value().type)) {
                update = m_allocator.alloc<NodeStmtAssign>();
                update->ident = consume();
                auto op_token = consume();
                update->op = token_to_assign_op(op_token.type);
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

            // Optional `: type` annotation (int base with pointer stars).
            std::optional<Type> param_type;
            if (peek().has_value() && peek().value().type == TokenType::colon) {
                consume();
                param_type = parse_full_type();
                if (!param_type.has_value()) error("Expected parameter type after ':'");
            }
            func->param_types.push_back(param_type);

            if (peek().has_value() && peek().value().type == TokenType::comma) {
                consume();
            } else if (peek().has_value() && peek().value().type != TokenType::close_paren) {
                error("Expected ',' or ')' after parameter");
            }
        }

        if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
            error("Expected ')' after parameters");
        }
        consume(); // )

        // Optional `-> type` return annotation.
        if (peek().has_value() && peek().value().type == TokenType::arrow) {
            consume();
            func->return_type = parse_full_type();
            if (!func->return_type.has_value()) error("Expected type after '->'");
        }

        func->body = parse_block();
        return func;
}

std::optional<Type> Parser::parse_full_type() {
    auto base = parse_type();
    Type type;
    if (base) {
        type = Type::make_int(*base);
    } else if (peek().has_value() && peek().value().type == TokenType::ident
               && peek().value().value.has_value()
               && m_type_aliases.contains(*peek().value().value)) {
        Token alias_name = consume();
        type = m_type_aliases.at(alias_name.value.value());
    } else if (peek().has_value() && peek().value().type == TokenType::ident
               && peek().value().value.has_value()
               && m_struct_types.contains(*peek().value().value)) {
        Token struct_name = consume();
        type = Type::make_struct(struct_name.value.value());
    } else {
        return {};
    }
    while (peek().has_value() && peek().value().type == TokenType::star) {
        consume();
        Type* pointee = m_allocator.alloc<Type>();
        *pointee = type;
        type = Type::make_ptr(pointee);
    }
    return type;
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
                    m_diag.error(ret_loc, "Invalid expression in return");
                    throw LSPAbort();
                }
                fail_at(ret_loc, "Invalid expression in return");
            }
        } else {
            stmt_ret = m_allocator.alloc<NodeStmtReturn>();
            stmt_ret->loc = ret_loc;
            stmt_ret->expr = nullptr;
        }
        if (!peek().has_value() || peek().value().type != TokenType::semi) {
            if (g_lsp_mode) {
                m_diag.error(ret_loc, "Expected ';' after return");
                throw LSPAbort();
            }
            fail_at(ret_loc, "Expected ';' after return");
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
            // bool is a 1-byte 0/1 value; char is Cyan's spelling of u8.
            case TokenType::_bool:
            case TokenType::_char: consume(); return IntType::u8;
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
                        m_diag.error(arr_loc, "Expected array size");
                        throw LSPAbort();
                    }
                    fail_at(arr_loc, "Expected array size");
                }
                if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                    if (g_lsp_mode) {
                        m_diag.error(arr_loc, "Expected ']'");
                        throw LSPAbort();
                    }
                    fail_at(arr_loc, "Expected ']'");
                }
                consume(); // ]
                if (!peek().has_value() || peek().value().type != TokenType::semi) {
                    if (g_lsp_mode) {
                        m_diag.error(arr_loc, "Expected ';'");
                        throw LSPAbort();
                    }
                    fail_at(arr_loc, "Expected ';'");
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
                if (auto full = parse_full_type()) {
                    if (full->kind == Type::Ptr) {
                        stmt_let->full_type = full;
                    } else if (full->kind == Type::Struct) {
                        stmt_let->struct_type_name = full->struct_name;
                        stmt_let->expr = nullptr;
                    } else {
                        stmt_let->type = full->int_type;
                    }
                    // Typed array: `var a: i32[10];`
                    if (peek().has_value() && peek().value().type == TokenType::open_square) {
                        auto stmt_arr = m_allocator.alloc<NodeStmtArrDecl>();
                        stmt_arr->name = stmt_let->ident;
                        stmt_arr->loc = stmt_let->ident.loc;
                        stmt_arr->elem_type = full;
                        consume(); // [
                        if (auto sz = parse_expr()) {
                            stmt_arr->size = sz.value();
                        } else {
                            error("Expected array size");
                        }
                        if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                            error("Expected ']' after array size");
                        }
                        consume(); // ]
                        stmt_arr->dims.push_back(stmt_arr->size);
                        while (peek().has_value() && peek().value().type == TokenType::open_square) {
                            consume(); // [
                            auto d = parse_expr();
                            if (!d) error("Expected array dimension size");
                            stmt_arr->dims.push_back(*d);
                            if (!peek().has_value() || peek().value().type != TokenType::close_square) {
                                error("Expected ']' after array dimension");
                            }
                            consume(); // ]
                        }
                        if (!peek().has_value() || peek().value().type != TokenType::semi) {
                            error("Expected ';' after array declaration");
                        }
                        consume();
                        return NodeStmt { .var = stmt_arr };
                    }
                    if (full->kind == Type::Struct) {
                        // Struct declarations may omit the initializer.
                        if (peek().has_value() && peek().value().type == TokenType::eq) {
                            consume();
                            if (auto expr = parse_expr()) stmt_let->expr = expr.value();
                            else error("Invalid expression");
                        }
                    } else {
                        if (!peek().has_value() || peek().value().type != TokenType::eq) {
                            error("Expected '=' after type");
                        }
                        consume();
                        if (auto expr = parse_expr()) {
                            stmt_let->expr = expr.value();
                        } else {
                            error("Invalid expression");
                        }
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
                // Assignment to a field of an indexed struct: a[0].x = v
                auto lhs = parse_expr();
                if (!lhs) error("Invalid indexed field assignment target");
                // Plain `a[i] = v` is an array assignment, not a field one.
                if (auto* indexed = std::get_if<NodeExprIndex*>(&(*lhs)->var)) {
                    auto arr = m_allocator.alloc<NodeStmtArrAssign>();
                    arr->name = (*indexed)->name;
                    arr->index = (*indexed)->index;
                    arr->base = (*indexed)->base;
                    if (!peek().has_value() || peek().value().type != TokenType::eq) {
                        error("Expected '=' in array assignment");
                    }
                    consume();
                    if (auto expr = parse_expr()) arr->expr = expr.value();
                    else error("Invalid expression in array assignment");
                    if (peek().has_value() && peek().value().type == TokenType::semi) consume();
                    else error("Expected ';' after array assignment");
                    return NodeStmt { .var = arr };
                }
                auto* access = std::get_if<NodeExprFieldAccess*>(&(*lhs)->var);
                if (!access || !(*access)->base) error("Invalid indexed field assignment target");
                auto stmt = m_allocator.alloc<NodeStmtFieldAssign>();
                stmt->base = (*access)->base;
                stmt->field_name = (*access)->field_name;
                if (!peek().has_value() || !is_assign_op(peek().value().type)) {
                    error("Expected assignment operator after indexed field access");
                }
                stmt->op = token_to_assign_op(consume().type);
                if (auto expr = parse_expr()) stmt->expr = expr.value();
                else error("Invalid expression in field assignment");
                if (peek().has_value() && peek().value().type == TokenType::semi) consume();
                else error("Expected ';' after field assignment");
                return NodeStmt { .var = stmt };
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
                    m_diag.error(brk_loc, "Expected ';' after break");
                    throw LSPAbort();
                }
                fail_at(brk_loc, "Expected ';' after break");
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
                    m_diag.error(cont_loc, "Expected ';' after continue");
                    throw LSPAbort();
                }
                fail_at(cont_loc, "Expected ';' after continue");
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
                        m_diag.error(ident.loc, "Expected ';' after " + std::string(is_inc ? "i++" : "i--"));
                        throw LSPAbort();
                    }
                    fail_at(ident.loc, "Expected ';' after " + std::string(is_inc ? "i++" : "i--"));
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
            && peek(1).has_value() && peek(1).value().type == TokenType::arrow
            && peek(2).has_value() && peek(2).value().type == TokenType::ident
            && peek(3).has_value() && is_assign_op(peek(3).value().type)) {
                auto stmt = m_allocator.alloc<NodeStmtArrowAssign>();
                stmt->ptr_name = consume();
                consume(); // ->
                stmt->field_name = consume();
                stmt->op = token_to_assign_op(consume().type);
                if (auto expr = parse_expr()) stmt->expr = expr.value();
                else error("Invalid expression in pointer field assignment");
                if (peek().has_value() && peek().value().type == TokenType::semi) consume();
                else error("Expected ';' after pointer field assignment");
                return NodeStmt { .var = stmt };
        } else if (
            peek().has_value() && peek().value().type == TokenType::ident
            && peek(1).has_value() && peek(1).value().type == TokenType::dot
            && peek(2).has_value() && peek(2).value().type == TokenType::ident
            && (peek(3).has_value() &&
                (is_assign_op(peek(3).value().type) || peek(3).value().type == TokenType::dot))) {
                auto lhs = parse_expr();
                if (!lhs) error("Invalid field assignment target");
                auto* access = std::get_if<NodeExprFieldAccess*>(&(*lhs)->var);
                if (!access) error("Invalid field assignment target");
                auto stmt_field_assign = m_allocator.alloc<NodeStmtFieldAssign>();
                stmt_field_assign->obj_name = (*access)->obj_name;
                stmt_field_assign->field_name = (*access)->field_name;
                if ((*access)->base) stmt_field_assign->base = (*access)->base;
                if (!peek().has_value() || !is_assign_op(peek().value().type)) {
                    error("Expected assignment operator after field access");
                }
                stmt_field_assign->op = token_to_assign_op(consume().type);
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
            && is_assign_op(peek(1).value().type)) {
                auto stmt_assign = m_allocator.alloc<NodeStmtAssign>();
                stmt_assign->ident = consume();
                auto op_token = consume();
                stmt_assign->op = token_to_assign_op(op_token.type);
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
        } else if (peek().has_value() && peek().value().type == TokenType::star) {
            // Dereference assignment: *ptr = expr, *ptr += expr, etc.
            auto stmt_deref = m_allocator.alloc<NodeStmtDerefAssign>();
            stmt_deref->star = consume();

            // Parse the pointer expression
            if (auto ptr_expr = parse_primary_expr()) {
                stmt_deref->ptr_expr = ptr_expr.value();
            } else {
                if (g_lsp_mode) { m_diag.error(stmt_deref->star.loc, "Expected pointer expression after '*'"); throw LSPAbort(); }
                fail_at(stmt_deref->star.loc, "Expected pointer expression after '*'");
            }

            // Check for assignment operator
            if (!peek().has_value() || !is_assign_op(peek().value().type)) {
                if (g_lsp_mode) { m_diag.error(stmt_deref->star.loc, "Expected '=' or compound assignment after pointer dereference"); throw LSPAbort(); }
                fail_at(stmt_deref->star.loc, "Expected '=' or compound assignment after pointer dereference");
            }

            auto op_token = consume();
            stmt_deref->op = token_to_assign_op(op_token.type);

            if (auto expr = parse_expr()) {
                stmt_deref->expr = expr.value();
            } else {
                if (g_lsp_mode) { m_diag.error(stmt_deref->star.loc, "Invalid expression in pointer dereference assignment"); throw LSPAbort(); }
                fail_at(stmt_deref->star.loc, "Invalid expression in pointer dereference assignment");
            }

            if (!peek().has_value() || peek().value().type != TokenType::semi) {
                if (g_lsp_mode) { m_diag.error(stmt_deref->star.loc, "Expected ';' after pointer dereference assignment"); throw LSPAbort(); }
                fail_at(stmt_deref->star.loc, "Expected ';' after pointer dereference assignment");
            }
            consume();
            return NodeStmt { .var = stmt_deref };
        } else {
            return {};
        }
}

void Parser::warn_assignment_in_condition() {
    if (!peek().has_value() || peek().value().type != TokenType::eq) return;
    Token eq_token = peek().value();
    m_diag.warning(eq_token.loc, "'=' in condition; did you mean '=='?");
    m_diag.add_fixit({eq_token.loc, "=="});
}

std::optional<NodeProg> Parser::parse_prog() {
    NodeProg prog;
    const size_t max_errors = 20; // D-004 error limit
    while (peek().has_value() && m_diag.error_count() < max_errors) {
        try {
            if (peek().value().type == TokenType::_function) {
                prog.funcs.push_back(parse_func_def());
            } else if (peek().value().type == TokenType::_type) {
                prog.aliases.push_back(parse_type_alias());
            } else if (peek().value().type == TokenType::_enum) {
                prog.enums.push_back(parse_enum_def());
            } else if (peek().value().type == TokenType::_struct) {
                auto struct_def = parse_struct_def();
                StructTypeInfo info;
                for (const auto& f : struct_def->fields) {
                    info.field_names.push_back(f.value.value());
                }
                info.size = struct_def->fields.size();
                m_struct_types[struct_def->name.value.value()] = info;
                prog.structs.push_back(struct_def);
            } else if (auto stmt = parse_stmt()) {
                const auto& var = stmt.value().var;
                bool allowed = false;
                if (std::holds_alternative<NodeStmtGlobal*>(var)) {
                    allowed = true;
                } else if (std::holds_alternative<NodeStmtConst*>(var)) {
                    allowed = true;
                } else if (auto* expr_stmt = std::get_if<NodeStmtExpr*>(&var)) {
                    if ((*expr_stmt)->expr == nullptr) allowed = true;
                }
                if (!allowed) {
                    error("Top-level statements are not allowed. Use function main() as the entry point.");
                }
                prog.stmts.push_back(stmt.value());
            } else {
                error("Invalid statement at top level");
            }
        } catch (const ParseAbort&) {
            synchronize();
        }
    }

    if (!g_lsp_mode && !m_diag.empty()) {
        for (const auto& d : m_diag.diagnostics()) {
            std::cerr << m_diag.render_one(d);
            if (d.loc.line != 0 && d.loc.col != 0) print_code_context(d.loc);
        }
        if (m_diag.has_errors()) exit(EXIT_FAILURE);
    }
    return prog;
}

NodeTypeAlias* Parser::parse_type_alias() {
    consume(); // type
    if (!peek().has_value() || peek().value().type != TokenType::ident) {
        error("Expected alias name after 'type'");
    }
    Token name = consume();
    if (!peek().has_value() || peek().value().type != TokenType::eq) {
        error("Expected '=' after alias name");
    }
    consume();
    auto type = parse_full_type();
    if (!type.has_value()) error("Expected type after '=' in type alias");
    if (!peek().has_value() || peek().value().type != TokenType::semi) {
        error("Expected ';' after type alias");
    }
    consume();
    if (m_type_aliases.contains(name.value.value())) {
        fail_at(name.loc, "duplicate type alias '" + name.value.value() + "'");
    }
    m_type_aliases[name.value.value()] = *type;
    auto alias = m_allocator.alloc<NodeTypeAlias>();
    alias->name = name;
    alias->type = *type;
    return alias;
}

NodeEnumDef* Parser::parse_enum_def() {
    consume(); // enum
    if (!peek().has_value() || peek().value().type != TokenType::ident) {
        error("Expected enum name after 'enum'");
    }
    Token name = consume();
    if (!peek().has_value() || peek().value().type != TokenType::open_brace) {
        error("Expected '{' after enum name");
    }
    consume(); // {
    auto def = m_allocator.alloc<NodeEnumDef>();
    def->name = name;
    while (peek().has_value() && peek().value().type != TokenType::close_brace) {
        if (peek().value().type != TokenType::ident) {
            error("Expected enumerator name");
        }
        Token member = consume();
        if (m_enumerators.contains(member.value.value())) {
            fail_at(member.loc, "duplicate enumerator '" + member.value.value() + "'");
        }
        m_enumerators.insert(member.value.value());
        EnumMember em;
        em.name = member;
        if (peek().has_value() && peek().value().type == TokenType::eq) {
            consume();
            auto value = parse_expr();
            if (!value) error("Expected value after '=' in enum");
            em.value = *value;
        }
        def->members.push_back(std::move(em));
        if (peek().has_value() && peek().value().type == TokenType::comma) consume();
        else if (peek().has_value() && peek().value().type != TokenType::close_brace)
            error("Expected ',' or '}' in enum");
    }
    if (!peek().has_value() || peek().value().type != TokenType::close_brace) {
        error("Expected '}' after enum members");
    }
    consume();
    if (peek().has_value() && peek().value().type == TokenType::semi) consume();
    return def;
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
            std::optional<Type> field_type;
            if (peek().has_value() && peek().value().type == TokenType::colon) {
                consume();
                field_type = parse_full_type();
                if (!field_type.has_value()) error("Expected field type after ':'");
            }
            def->field_types.push_back(field_type);
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







