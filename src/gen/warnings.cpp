#include "../generation.hpp"

#include <cstdint>

// ==========================================================================
// Flow-insensitive warning pass: unreachable code and missing returns (E3c).
// This runs over the AST before code generation so it cannot perturb IR.
// ==========================================================================

namespace {

SourceLoc expr_loc(const NodeExpr& expr) {
    if (auto* e = std::get_if<NodeExprIntLit*>(&expr.var))      return (*e)->int_lit.loc;
    if (auto* e = std::get_if<NodeExprFloatLit*>(&expr.var))    return (*e)->float_lit.loc;
    if (auto* e = std::get_if<NodeExprIdent*>(&expr.var))       return (*e)->ident.loc;
    if (auto* e = std::get_if<NodeExprCall*>(&expr.var))        return (*e)->name.loc;
    if (auto* e = std::get_if<NodeExprStringLit*>(&expr.var))   return (*e)->value.loc;
    if (auto* e = std::get_if<NodeExprIndex*>(&expr.var))       return (*e)->name.loc;
    if (auto* e = std::get_if<NodeExprFieldAccess*>(&expr.var)) return (*e)->obj_name.loc;
    if (auto* e = std::get_if<NodeExprAddrOf*>(&expr.var))      return (*e)->ampersand.loc;
    if (auto* e = std::get_if<NodeExprDeref*>(&expr.var))       return (*e)->star.loc;
    if (auto* e = std::get_if<NodeExprRead*>(&expr.var))        return (*e)->loc;
    if (auto* e = std::get_if<NodeExprCast*>(&expr.var))        return (*e)->loc;
    if (auto* e = std::get_if<NodeExprTernary*>(&expr.var))     return expr_loc(*(*e)->cond);
    if (auto* e = std::get_if<NodeExprBitNot*>(&expr.var))      return expr_loc(*(*e)->expr);
    if (auto* e = std::get_if<NodeExprLogNot*>(&expr.var))      return expr_loc(*(*e)->expr);
    if (auto* e = std::get_if<BinExpr*>(&expr.var)) {
        // The leftmost token; a synthetic operand (unary minus's `0`) has no location.
        return std::visit([](auto* node) {
            SourceLoc loc = expr_loc(*node->lhs);
            return loc.line != 0 ? loc : expr_loc(*node->rhs);
        }, (*e)->var);
    }
    return {};
}

SourceLoc stmt_loc(const NodeStmt& stmt) {
    if (auto* s = std::get_if<NodeStmtExpr*>(&stmt.var))        return expr_loc(*(*s)->expr);
    if (auto* s = std::get_if<NodeStmtExit*>(&stmt.var))        return expr_loc(*(*s)->expr);
    if (auto* s = std::get_if<NodeStmtPrint*>(&stmt.var)) {
        return (*s)->args.empty() ? SourceLoc{} : expr_loc(*(*s)->args.front());
    }
    if (auto* s = std::get_if<NodeStmtIf*>(&stmt.var))          return expr_loc(*(*s)->cond);
    if (auto* s = std::get_if<NodeStmtWhile*>(&stmt.var))       return expr_loc(*(*s)->cond);
    if (auto* s = std::get_if<NodeStmtDoWhile*>(&stmt.var))     return expr_loc(*(*s)->cond);
    if (auto* s = std::get_if<NodeStmtFor*>(&stmt.var)) {
        return (*s)->cond ? expr_loc(*(*s)->cond) : SourceLoc{};
    }
    if (auto* s = std::get_if<NodeStmtSwitch*>(&stmt.var))      return expr_loc(*(*s)->expr);
    if (auto* s = std::get_if<NodeStmtLet*>(&stmt.var))         return (*s)->ident.loc;
    if (auto* s = std::get_if<NodeStmtReturn*>(&stmt.var))      return (*s)->loc;
    if (auto* s = std::get_if<NodeStmtBreak*>(&stmt.var))       return (*s)->loc;
    if (auto* s = std::get_if<NodeStmtContinue*>(&stmt.var))    return (*s)->loc;
    if (auto* s = std::get_if<NodeStmtArrDecl*>(&stmt.var))     return (*s)->loc;
    if (auto* s = std::get_if<NodeStmtAssign*>(&stmt.var))      return (*s)->ident.loc;
    if (auto* s = std::get_if<NodeStmtGlobal*>(&stmt.var))      return (*s)->name.loc;
    if (auto* s = std::get_if<NodeStmtConst*>(&stmt.var))       return (*s)->name.loc;
    if (auto* s = std::get_if<NodeStmtArrAssign*>(&stmt.var))   return (*s)->name.loc;
    if (auto* s = std::get_if<NodeStmtFieldAssign*>(&stmt.var)) return (*s)->obj_name.loc;
    if (auto* s = std::get_if<NodeStmtDerefAssign*>(&stmt.var)) return (*s)->star.loc;
    if (auto* s = std::get_if<NodeStmtArrowAssign*>(&stmt.var)) return (*s)->ptr_name.loc;
    if (auto* s = std::get_if<NodeStmtAsm*>(&stmt.var))          return (*s)->keyword.loc;
    return {};
}

// Flow after a statement: normal fallthrough, definite return/exit, or a
// jump (break/continue) that makes later statements in the same block dead.
enum class Flow { FallsThrough, Returns, Jumps };

struct FlowAnalyzer {
    DiagnosticEngine& diag;

    Flow analyze_stmt(const NodeStmt& stmt) {
        if (std::holds_alternative<NodeStmtReturn*>(stmt.var) ||
            std::holds_alternative<NodeStmtExit*>(stmt.var)) {
            return Flow::Returns;
        }
        if (std::holds_alternative<NodeStmtBreak*>(stmt.var) ||
            std::holds_alternative<NodeStmtContinue*>(stmt.var)) {
            return Flow::Jumps;
        }
        if (auto* s = std::get_if<NodeStmtBlock*>(&stmt.var)) {
            return analyze_block((*s)->block->stmts);
        }
        if (auto* s = std::get_if<NodeStmtIf*>(&stmt.var)) {
            Flow then_flow = analyze_block((*s)->then_block->stmts);
            if (!(*s)->else_block) return Flow::FallsThrough;
            Flow else_flow = analyze_block((*s)->else_block->stmts);
            if (then_flow == Flow::Returns && else_flow == Flow::Returns) return Flow::Returns;
            return Flow::FallsThrough;
        }
        if (auto* s = std::get_if<NodeStmtWhile*>(&stmt.var)) {
            analyze_block((*s)->body->stmts);
            return Flow::FallsThrough; // loops may execute zero times
        }
        if (auto* s = std::get_if<NodeStmtDoWhile*>(&stmt.var)) {
            analyze_block((*s)->body->stmts);
            return Flow::FallsThrough;
        }
        if (auto* s = std::get_if<NodeStmtFor*>(&stmt.var)) {
            analyze_block((*s)->body->stmts);
            return Flow::FallsThrough;
        }
        if (auto* s = std::get_if<NodeStmtSwitch*>(&stmt.var)) {
            bool has_default = false;
            bool all_return = true;
            for (const auto& c : (*s)->cases) {
                if (!c.value) has_default = true; // NULL case value = default
                if (analyze_block(c.stmts) != Flow::Returns) all_return = false;
            }
            if (has_default && all_return && !(*s)->cases.empty()) return Flow::Returns;
            return Flow::FallsThrough;
        }
        return Flow::FallsThrough;
    }

    Flow analyze_block(const std::vector<NodeStmt>& stmts) {
        Flow flow = Flow::FallsThrough;
        bool warned = false;
        for (const auto& s : stmts) {
            if (flow != Flow::FallsThrough) {
                if (!warned) {
                    diag.warning(stmt_loc(s), "unreachable code");
                    warned = true;
                }
                // Still walk the statement for nested diagnostics, but keep
                // the block's flow terminated.
                (void)analyze_stmt(s);
                continue;
            }
            flow = analyze_stmt(s);
        }
        return flow;
    }
};

// Collect functions that return a value without observable side effects.
// A discarded call to one of these is almost certainly a bug (E3d).
struct PureValueScanner {
    bool returns_value = false;
    bool impure = false;

    void scan_stmt(const NodeStmt& stmt) {
        if (auto* r = std::get_if<NodeStmtReturn*>(&stmt.var)) {
            if ((*r)->expr) returns_value = true;
        } else if (std::holds_alternative<NodeStmtPrint*>(stmt.var) ||
                   std::holds_alternative<NodeStmtExit*>(stmt.var) ||
                   std::holds_alternative<NodeStmtAssign*>(stmt.var) ||
                   std::holds_alternative<NodeStmtFieldAssign*>(stmt.var) ||
                   std::holds_alternative<NodeStmtDerefAssign*>(stmt.var) ||
                   std::holds_alternative<NodeStmtArrAssign*>(stmt.var) ||
                   std::holds_alternative<NodeStmtArrowAssign*>(stmt.var) ||
                   std::holds_alternative<NodeStmtAsm*>(stmt.var) ||
                    std::holds_alternative<NodeStmtExpr*>(stmt.var)) {
            impure = true;
        } else if (auto* b = std::get_if<NodeStmtBlock*>(&stmt.var)) {
            scan_block((*b)->block->stmts);
        } else if (auto* i = std::get_if<NodeStmtIf*>(&stmt.var)) {
            scan_block((*i)->then_block->stmts);
            if ((*i)->else_block) scan_block((*i)->else_block->stmts);
        } else if (auto* w = std::get_if<NodeStmtWhile*>(&stmt.var)) {
            scan_block((*w)->body->stmts);
        } else if (auto* d = std::get_if<NodeStmtDoWhile*>(&stmt.var)) {
            scan_block((*d)->body->stmts);
        } else if (auto* f = std::get_if<NodeStmtFor*>(&stmt.var)) {
            scan_block((*f)->body->stmts);
        } else if (auto* s = std::get_if<NodeStmtSwitch*>(&stmt.var)) {
            for (const auto& c : (*s)->cases) scan_block(c.stmts);
        }
    }

    void scan_block(const std::vector<NodeStmt>& stmts) {
        for (const auto& s : stmts) scan_stmt(s);
    }
};

const char* int_type_name(IntType t) {
    switch (t) {
        case IntType::i8:  return "i8";
        case IntType::u8:  return "u8";
        case IntType::i16: return "i16";
        case IntType::u16: return "u16";
        case IntType::i32: return "i32";
        case IntType::u32: return "u32";
        case IntType::i64: return "i64";
        case IntType::u64: return "u64";
    }
    return "int";
}

} // namespace

void Generator::check_narrowing_assign(Type target, const NodeExpr* value, SourceLoc loc) {
    if (!value || target.kind != Type::Int) return;
    auto constant = eval_const_expr(value);
    if (!constant.has_value()) return;

    int64_t v = constant.value();
    int64_t lo = 0, hi = 0;
    bool bounded = true;
    switch (target.int_type) {
        case IntType::i8:  lo = -128;   hi = 127;    break;
        case IntType::u8:  lo = 0;      hi = 255;    break;
        case IntType::i16: lo = -32768; hi = 32767;  break;
        case IntType::u16: lo = 0;      hi = 65535;  break;
        case IntType::i32: lo = INT32_MIN; hi = INT32_MAX; break;
        case IntType::u32: lo = 0;      hi = UINT32_MAX;   break;
        default: bounded = false; break;
    }
    if (bounded && (v < lo || v > hi)) {
        m_diag.warning(loc, std::string("narrowing assignment to ") + int_type_name(target.int_type)
                            + ": value " + std::to_string(v) + " is out of range");
    }
}

void Generator::warn_unused_result(const NodeExpr& expr, SourceLoc loc) {
    auto* call = std::get_if<NodeExprCall*>(&expr.var);
    if (!call) return;
    const std::string& name = (*call)->name.value.value();
    if (m_pure_value_funcs.contains(name)) {
        m_diag.warning(loc, "unused result of function '" + name + "'");
    }
}

void Generator::check_flow_warnings() {
    FlowAnalyzer analyzer{m_diag};
    for (const auto* func : m_prog.funcs) {
        PureValueScanner purity;
        purity.scan_block(func->body->stmts);
        if (purity.returns_value && !purity.impure) {
            m_pure_value_funcs.insert(func->name.value.value());
        }
        Flow flow = analyzer.analyze_block(func->body->stmts);
        // main() is special-cased by the generator: falling off the end is
        // defined to exit(0), so it needs no return warning.
        if (flow != Flow::Returns && func->name.value.value() != "main") {
            m_diag.warning(func->name.loc,
                           "missing return in function '" + func->name.value.value() + "'");
        }
    }
}

SourceLoc statement_location(const NodeStmt& stmt) {
    return stmt_loc(stmt);
}

SourceLoc expr_source_loc(const NodeExpr& expr) { return expr_loc(expr); }
