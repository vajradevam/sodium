#include "../generation.hpp"

// ==========================================================================
// eval_const_expr — compile-time constant expression evaluation
// ==========================================================================

[[nodiscard]] std::optional<int64_t> Generator::eval_const_expr(const NodeExpr* expr) const
{
    struct ConstEvalVisitor {
        const Generator* gen;
        std::optional<int64_t> result;

        void operator()(const NodeExprIntLit* lit) {
            result = static_cast<int64_t>(std::stoll(lit->int_lit.value.value()));
        }

        void operator()(const NodeExprIdent* ident) {
            const auto& name = ident->ident.value.value();
            auto it = gen->m_constants.find(name);
            if (it != gen->m_constants.end()) {
                result = it->second;
            }
        }

        void operator()(const BinExpr* bin) {
            struct BinVisitor {
                const Generator* gen;
                std::optional<int64_t> result;
                void operator()(const BinExprAdd* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() + rhs.value();
                }
                void operator()(const BinExprSub* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() - rhs.value();
                }
                void operator()(const BinExprMulti* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() * rhs.value();
                }
                void operator()(const BinExprDiv* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs && rhs.value() != 0) result = lhs.value() / rhs.value();
                }
                void operator()(const BinExprMod* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs && rhs.value() != 0) result = lhs.value() % rhs.value();
                }
                void operator()(const BinExprLT* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() < rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprGT* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() > rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprLTE* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() <= rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprGTE* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() >= rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprEQ* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() == rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprNEQ* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() != rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprAnd* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() && rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprOr* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = (lhs.value() || rhs.value()) ? 1 : 0;
                }
                void operator()(const BinExprBitAnd* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() & rhs.value();
                }
                void operator()(const BinExprBitOr* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() | rhs.value();
                }
                void operator()(const BinExprXor* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() ^ rhs.value();
                }
                void operator()(const BinExprShl* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() << rhs.value();
                }
                void operator()(const BinExprShr* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) result = lhs.value() >> rhs.value();
                }
            };
            BinVisitor bv{gen, std::nullopt};
            std::visit(bv, bin->var);
            result = bv.result;
        }

        void operator()(const NodeExprBitNot* not_expr) {
            auto inner = gen->eval_const_expr(not_expr->expr);
            if (inner) result = ~(inner.value());
        }

        void operator()(const NodeExprLogNot* log_not) {
            auto inner = gen->eval_const_expr(log_not->expr);
            if (inner) result = (inner.value() == 0) ? 1 : 0;
        }

        void operator()(const NodeExprTernary* ternary) {
            auto cond = gen->eval_const_expr(ternary->cond);
            if (!cond) return;
            auto branch = cond.value() ? ternary->then_expr : ternary->else_expr;
            result = gen->eval_const_expr(branch);
        }

        void operator()(const NodeExprCall*) {}
        void operator()(const NodeExprStringLit*) {}
        void operator()(const NodeExprIndex*) {}
        void operator()(const NodeExprRead*) {}  // LSP no-op; read() has no variable references
        void operator()(const NodeExprArrLit*) {}
        void operator()(const NodeExprFieldAccess*) {}
        void operator()(const NodeExprAddrOf*) {}
        void operator()(const NodeExprDeref*) {}
    };

    ConstEvalVisitor visitor{this, std::nullopt};
    std::visit(visitor, expr->var);
    return visitor.result;
}
