#include "../generation.hpp"

// ==========================================================================
// Flow-insensitive warning pass: unreachable code and missing returns (E3c).
// This runs over the AST before code generation so it cannot perturb IR.
// ==========================================================================

namespace {

SourceLoc expr_loc(const NodeExpr& expr) {
    if (auto* e = std::get_if<NodeExprIntLit*>(&expr.var))      return (*e)->int_lit.loc;
    if (auto* e = std::get_if<NodeExprIdent*>(&expr.var))       return (*e)->ident.loc;
    if (auto* e = std::get_if<NodeExprCall*>(&expr.var))        return (*e)->name.loc;
    if (auto* e = std::get_if<NodeExprStringLit*>(&expr.var))   return (*e)->value.loc;
    if (auto* e = std::get_if<NodeExprIndex*>(&expr.var))       return (*e)->name.loc;
    if (auto* e = std::get_if<NodeExprFieldAccess*>(&expr.var)) return (*e)->obj_name.loc;
    if (auto* e = std::get_if<NodeExprAddrOf*>(&expr.var))      return (*e)->ampersand.loc;
    if (auto* e = std::get_if<NodeExprDeref*>(&expr.var))       return (*e)->star.loc;
    if (auto* e = std::get_if<NodeExprRead*>(&expr.var))        return (*e)->loc;
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

} // namespace

void Generator::check_flow_warnings() {
    FlowAnalyzer analyzer{m_diag};
    for (const auto* func : m_prog.funcs) {
        Flow flow = analyzer.analyze_block(func->body->stmts);
        // main() is special-cased by the generator: falling off the end is
        // defined to exit(0), so it needs no return warning.
        if (flow != Flow::Returns && func->name.value.value() != "main") {
            m_diag.warning(func->name.loc,
                           "missing return in function '" + func->name.value.value() + "'");
        }
    }
}
