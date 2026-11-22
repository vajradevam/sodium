#include "../generation.hpp"
#include <iostream>

// ==========================================================================
// Helpers
// ==========================================================================

// Convert a Type to a human-readable string (e.g., "i64", "u8", "Ptr(u8)")
static std::string type_str(const Type& t) {
    if (t.kind == Type::Int) {
        switch (t.int_type) {
            case IntType::i8:  return "i8";
            case IntType::i16: return "i16";
            case IntType::i32: return "i32";
            case IntType::i64: return "i64";
            case IntType::u8:  return "u8";
            case IntType::u16: return "u16";
            case IntType::u32: return "u32";
            case IntType::u64: return "u64";
        }
    }
    if (t.kind == Type::Ptr && t.pointee) {
        return "Ptr(" + type_str(*t.pointee) + ")";
    }
    if (t.kind == Type::Ptr) {
        return "Ptr(?)";
    }
    return "?";
}

// ==========================================================================
// Static type inference (no IR emitted)
// ==========================================================================

Type Generator::infer_expr_type(
    const NodeExpr& expr,
    const std::unordered_map<std::string, Type>& scope_types)
{
    struct TypeInferrer {
        Generator* gen;
        const std::unordered_map<std::string, Type>& scope;
        Type result;

        void operator()(const NodeExprIntLit*) {
            result = Type::make_int(IntType::i64);
        }

        void operator()(const NodeExprStringLit*) {
            Type* pointee = gen->alloc_type(Type::make_int(IntType::u8));
            result = Type::make_ptr(pointee);
        }

        void operator()(const NodeExprIdent* ident) {
            const auto& name = ident->ident.value.value();
            auto it = scope.find(name);
            if (it != scope.end()) {
                result = it->second;
            } else {
                result = Type::make_int(IntType::i64);
            }
        }

        void operator()(const NodeExprCall* call) {
            const auto& fname = call->name.value.value();

            // Handle builtins that return pointer types
            if (fname == "malloc") {
                // malloc(size) returns Ptr(u8) — untyped bytes
                Type* pointee = gen->alloc_type(Type::make_int(IntType::u8));
                result = Type::make_ptr(pointee);
                return;
            }
            if (fname == "argv" || fname == "envp") {
                // argv(i)/envp(i) return Ptr(u8)
                Type* pointee = gen->alloc_type(Type::make_int(IntType::u8));
                result = Type::make_ptr(pointee);
                return;
            }
            if (fname == "exit" || fname == "free" || fname == "argc" || fname == "sys_open" ||
                fname == "sys_close" || fname == "sys_read" || fname == "sys_write" ||
                fname == "sys_lseek" || fname == "sys_unlink") {
                result = Type::make_int(IntType::i64);
                return;
            }

            // A call through a function-pointer variable yields the signature's return type.
            {
                auto var = scope.find(fname);
                if (var != scope.end() && var->second.kind == Type::Ptr && var->second.pointee
                    && var->second.pointee->kind == Type::Func) {
                    const Type* ret = var->second.pointee->fn_ret;
                    result = ret ? *ret : Type::make_int(IntType::i64);
                    return;
                }
            }

            // Infer types of arguments and record for callee
            std::vector<Type> inferred_args;
            for (size_t i = 0; i < call->args.size(); i++) {
                Type arg_t = gen->infer_expr_type(*call->args[i], scope);
                inferred_args.push_back(arg_t);
            }
            // Declared signatures are authoritative: do not infer (and do
            // not emit conflicting-pointer warnings) for typed functions.
            if (!fname.empty() && gen->m_func_names.contains(fname) &&
                !gen->has_declared_param_types(fname)) {
                auto& existing = gen->m_inferred_param_types[fname];
                if (existing.empty()) {
                    existing = inferred_args;
                } else {
                    // Merge: upgrade i64 → Ptr when first Ptr call site is seen.
                    // Once a parameter is Ptr, it stays Ptr (first Ptr call site wins).
                    // This avoids conflicts between &i64_var (Ptr(i64)) and string literals (Ptr(u8)).
                    for (size_t i = 0; i < inferred_args.size() && i < existing.size(); i++) {
                        if (inferred_args[i].kind == Type::Ptr && existing[i].kind != Type::Ptr) {
                            existing[i] = inferred_args[i];
                        } else if (inferred_args[i].kind == Type::Ptr && existing[i].kind == Type::Ptr) {
                            // Both Ptr: warn if element types disagree (first iteration only)
                            if (gen->m_type_infer_iteration == 0 && existing[i].pointee && inferred_args[i].pointee &&
                                !(*existing[i].pointee == *inferred_args[i].pointee)) {
                                std::cerr << "warning: " << call->name.loc.to_string()
                                          << ": parameter " << (i+1) << " of '" << fname
                                          << "' has conflicting inferred pointer types "
                                          << type_str(existing[i]) << " vs " << type_str(inferred_args[i])
                                          << "; using " << type_str(existing[i]) << std::endl;
                            }
                        }
                        // Otherwise keep existing (Ptr stays Ptr, i64 stays i64)
                    }
                    // Extend if more args than previously recorded
                    while (existing.size() < inferred_args.size()) {
                        existing.push_back(inferred_args[existing.size()]);
                    }
                }
            }
            // Declared return types are authoritative; untyped calls stay i64.
            const NodeFuncDef* decl = gen->find_func(fname);
            result = (decl && decl->return_type) ? *decl->return_type
                                                 : Type::make_int(IntType::i64);
        }

        void operator()(const NodeExprDeref* deref) {
            Type inner = gen->infer_expr_type(*deref->expr, scope);
            if (inner.kind == Type::Ptr && inner.pointee) {
                result = *inner.pointee;
            } else {
                result = Type::make_int(IntType::i64);
            }
        }

        void operator()(const NodeExprAddrOf* addr) {
            // &func: a function pointer (unless a variable of that name is in scope).
            if (auto* ident = std::get_if<NodeExprIdent*>(&addr->expr->var)) {
                const std::string& name = (*ident)->ident.value.value();
                if (!scope.contains(name) && !gen->find_var(name)) {
                    if (const NodeFuncDef* func = gen->find_func(name)) {
                        result = gen->function_pointer_type(*func);
                        return;
                    }
                }
            }
            Type inner = gen->infer_expr_type(*addr->expr, scope);
            Type* pointee = gen->alloc_type(inner);
            result = Type::make_ptr(pointee);
        }

        void operator()(const NodeExprSizeof*) {
            result = Type::make_int(IntType::i64);
        }

        void operator()(const NodeExprCast* cast) {
            result = cast->target;
        }

        void operator()(const NodeExprStructLit*) {
            result = Type::make_int(IntType::i64);
        }

        void operator()(const BinExpr* bin) {
            struct BinTypeInferrer {
                Generator* gen;
                const std::unordered_map<std::string, Type>& scope;
                Type result;

                void operator()(const BinExprAdd* b) {
                    Type lhs = gen->infer_expr_type(*b->lhs, scope);
                    Type rhs = gen->infer_expr_type(*b->rhs, scope);
                    // ptr + int → ptr
                    if (lhs.kind == Type::Ptr && rhs.kind == Type::Int) {
                        result = lhs;
                    } else if (rhs.kind == Type::Ptr && lhs.kind == Type::Int) {
                        result = rhs;
                    } else {
                        result = Type::make_int(IntType::i64);
                    }
                }
                void operator()(const BinExprSub* b) {
                    Type lhs = gen->infer_expr_type(*b->lhs, scope);
                    Type rhs = gen->infer_expr_type(*b->rhs, scope);
                    // ptr - int → ptr, ptr - ptr → i64
                    if (lhs.kind == Type::Ptr && rhs.kind == Type::Int) {
                        result = lhs;
                    } else {
                        result = Type::make_int(IntType::i64);
                    }
                }
                void operator()(const BinExprMulti* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprDiv* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprMod* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprLT* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprGT* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprLTE* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprGTE* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprEQ* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprNEQ* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprAnd* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprOr* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprBitAnd* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprBitOr* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprXor* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprShl* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
                void operator()(const BinExprShr* b) {
                    gen->infer_expr_type(*b->lhs, scope);
                    gen->infer_expr_type(*b->rhs, scope);
                    result = Type::make_int(IntType::i64);
                }
            };
            BinTypeInferrer bv{gen, scope, Type::make_int(IntType::i64)};
            std::visit(bv, bin->var);
            result = bv.result;
        }

        void operator()(const NodeExprBitNot* not_expr) {
            gen->infer_expr_type(*not_expr->expr, scope);
            result = Type::make_int(IntType::i64);
        }

        void operator()(const NodeExprLogNot* log_not) {
            gen->infer_expr_type(*log_not->expr, scope);
            result = Type::make_int(IntType::i64);
        }

        void operator()(const NodeExprTernary* ternary) {
            // Infer condition (may contain calls)
            gen->infer_expr_type(*ternary->cond, scope);
            // Infer types of both branches; prefer pointer if one is pointer
            Type then_t = gen->infer_expr_type(*ternary->then_expr, scope);
            Type else_t = gen->infer_expr_type(*ternary->else_expr, scope);
            if (then_t.kind == Type::Ptr) {
                result = then_t;
            } else if (else_t.kind == Type::Ptr) {
                result = else_t;
            } else {
                result = Type::make_int(IntType::i64);
            }
        }

        void operator()(const NodeExprIndex* idx) {
            // a[i] → lookup 'a' in scope, get element type
            const auto& name = idx->name.value.value();
            auto it = scope.find(name);
            if (it != scope.end() && it->second.kind == Type::Ptr && it->second.pointee) {
                result = *it->second.pointee;
            } else {
                result = Type::make_int(IntType::i64);
            }
        }

        void operator()(const NodeExprRead*) {
            result = Type::make_int(IntType::i64);
        }

        void operator()(const NodeExprArrLit*) {
            result = Type::make_int(IntType::i64);
        }

        void operator()(const NodeExprFieldAccess*) {
            // Static inference runs before local scopes exist; the generator's
            // field_addr resolves the exact type once scopes are populated.
            result = Type::make_int(IntType::i64);
        }
    };

    TypeInferrer inferrer{this, scope_types, Type::make_int(IntType::i64)};
    std::visit(inferrer, expr.var);
    return inferrer.result;
}

// ==========================================================================
// Helpers for collect_call_types
// ==========================================================================

namespace {

// Walk a statement and update the variable scope, recording call types.
// Returns true if the statement is a return (no further stmts reachable).
void infer_stmt_types(
    const NodeStmt& stmt,
    Generator* gen,
    std::unordered_map<std::string, Type>& scope)
{
    struct StmtInferrer {
        Generator* gen;
        std::unordered_map<std::string, Type>& scope;

        void operator()(const NodeStmtLet* let) {
            const auto& name = let->ident.value.value();
            if (let->expr) {
                scope[name] = gen->infer_expr_type(*let->expr, scope);
            } else {
                scope[name] = Type::make_int(IntType::i64);
            }
        }

        void operator()(const NodeStmtConst* cnst) {
            const auto& name = cnst->name.value.value();
            if (cnst->expr) {
                scope[name] = gen->infer_expr_type(*cnst->expr, scope);
            } else {
                scope[name] = Type::make_int(IntType::i64);
            }
        }

        void operator()(const NodeStmtGlobal*) {}

        void operator()(const NodeStmtExpr* expr_stmt) {
            // Expression statement (e.g., function call) — infer for side effects
            gen->infer_expr_type(*expr_stmt->expr, scope);
        }

        void operator()(const NodeStmtAssign* assign) {
            // x = expr → update x's type if it's not yet known
            const auto& name = assign->ident.value.value();
            Type val_t = gen->infer_expr_type(*assign->expr, scope);
            // If we already have a type for this var, prefer the existing one
            if (scope.find(name) == scope.end()) {
                scope[name] = val_t;
            }
        }

        void operator()(const NodeStmtBlock* block) {
            // New scope for the block
            std::unordered_map<std::string, Type> inner_scope = scope;
            for (const auto& s : block->block->stmts) {
                infer_stmt_types(s, gen, inner_scope);
            }
            // Merge inner_scope back? No — blocks keep their own scope.
        }

        void operator()(const NodeStmtIf* if_stmt) {
            // Infer the condition expression (may contain calls)
            gen->infer_expr_type(*if_stmt->cond, scope);
            // Walk then and else blocks
            std::unordered_map<std::string, Type> then_scope = scope;
            for (const auto& s : if_stmt->then_block->stmts) {
                infer_stmt_types(s, gen, then_scope);
            }
            if (if_stmt->else_block) {
                std::unordered_map<std::string, Type> else_scope = scope;
                for (const auto& s : if_stmt->else_block->stmts) {
                    infer_stmt_types(s, gen, else_scope);
                }
            }
        }

        void operator()(const NodeStmtWhile* while_stmt) {
            // Infer the condition expression (may contain calls)
            gen->infer_expr_type(*while_stmt->cond, scope);
            // Walk the body directly (it's a NodeBlock* not NodeStmt*)
            std::unordered_map<std::string, Type> inner_scope = scope;
            for (const auto& s : while_stmt->body->stmts) {
                infer_stmt_types(s, gen, inner_scope);
            }
        }

        void operator()(const NodeStmtDoWhile* dowhile) {
            // Infer the condition expression (may contain calls)
            gen->infer_expr_type(*dowhile->cond, scope);
            std::unordered_map<std::string, Type> inner_scope = scope;
            for (const auto& s : dowhile->body->stmts) {
                infer_stmt_types(s, gen, inner_scope);
            }
        }

        void operator()(const NodeStmtFor* for_stmt) {
            // for init; condition; increment { body }
            // Init creates a new scope
            std::unordered_map<std::string, Type> inner_scope = scope;
            if (for_stmt->init) {
                infer_stmt_types(*for_stmt->init, gen, inner_scope);
            }
            // Infer condition expression (may contain calls)
            if (for_stmt->cond) {
                gen->infer_expr_type(*for_stmt->cond, inner_scope);
            }
            // Infer increment expression (may contain calls)
            if (for_stmt->update) {
                gen->infer_expr_type(*for_stmt->update->expr, inner_scope);
            }
            for (const auto& s : for_stmt->body->stmts) {
                infer_stmt_types(s, gen, inner_scope);
            }
        }

        void operator()(const NodeStmtSwitch* switch_stmt) {
            // Infer switch expression (may contain calls)
            gen->infer_expr_type(*switch_stmt->expr, scope);
            for (const auto& c : switch_stmt->cases) {
                std::unordered_map<std::string, Type> case_scope = scope;
                for (const auto& s : c.stmts) {
                    infer_stmt_types(s, gen, case_scope);
                }
            }
        }

        void operator()(const NodeStmtReturn* ret) {
            if (ret->expr) {
                gen->infer_expr_type(*ret->expr, scope);
            }
        }

        void operator()(const NodeStmtExit* exit) {
            if (exit->expr) {
                gen->infer_expr_type(*exit->expr, scope);
            }
        }

        void operator()(const NodeStmtPrint* print) {
            for (const auto& arg : print->args) {
                gen->infer_expr_type(*arg, scope);
            }
        }

        void operator()(const NodeStmtBreak*) {}
        void operator()(const NodeStmtContinue*) {}

        void operator()(const NodeStmtArrDecl* decl) {
            // array declaration: name[size]
            const auto& name = decl->name.value.value();
            // Arrays are currently stored as addresses (i64*)
            scope[name] = Type::make_int(IntType::i64);
        }

        void operator()(const NodeStmtArrAssign* assign) {
            (void)gen->infer_expr_type(*assign->expr, scope);
        }

        void operator()(const NodeStmtFieldAssign* field_assign) {
            Type val_t = gen->infer_expr_type(*field_assign->expr, scope);
            if (!field_assign->base && field_assign->obj_name.value.has_value()) {
                const auto& obj_name = field_assign->obj_name.value.value();
                if (scope.find(obj_name) == scope.end()) {
                    scope[obj_name] = val_t;
                }
            }
        }

        void operator()(const NodeStmtArrowAssign* arrow) {
            gen->infer_expr_type(*arrow->expr, scope);
        }

        void operator()(const NodeStmtAsm*) {}

        void operator()(const NodeStmtDerefAssign* deref_assign) {
            // *ptr = expr — infer rhs type
            gen->infer_expr_type(*deref_assign->expr, scope);
            // Also infer the pointer expression type
            gen->infer_expr_type(*deref_assign->ptr_expr, scope);
        }
    };

    StmtInferrer inferrer{gen, scope};
    std::visit(inferrer, stmt.var);
}

} // anonymous namespace

// ==========================================================================
// collect_call_types — walk all function bodies to infer parameter types
// ==========================================================================

void Generator::collect_call_types()
{
    // Iterate to fixed point: keep walking until no new types are inferred.
    // Handles arbitrary call depth (A calls B calls C ...).
    const int MAX_ITERATIONS = 10;
    for (int iter = 0; iter < MAX_ITERATIONS; iter++) {
        m_type_infer_iteration = iter;
        // Snapshot current types to detect changes
        auto snapshot = m_inferred_param_types;

        for (const auto& func : m_prog.funcs) {
            std::unordered_map<std::string, Type> scope;
            auto it = m_inferred_param_types.find(func->name.value.value());
            if (it != m_inferred_param_types.end()) {
                for (size_t i = 0; i < func->params.size() && i < it->second.size(); i++) {
                    scope[func->params[i].value.value()] = it->second[i];
                }
                for (size_t i = it->second.size(); i < func->params.size(); i++) {
                    scope[func->params[i].value.value()] = Type::make_int(IntType::i64);
                }
            } else {
                for (const auto& param : func->params) {
                    scope[param.value.value()] = Type::make_int(IntType::i64);
                }
            }

            for (const auto& stmt : func->body->stmts) {
                infer_stmt_types(stmt, this, scope);
            }
        }

        // Check if anything changed
        bool changed = (snapshot.size() != m_inferred_param_types.size());
        if (!changed) {
            for (const auto& [fname, types] : m_inferred_param_types) {
                auto sit = snapshot.find(fname);
                if (sit == snapshot.end() || sit->second != types) {
                    changed = true;
                    break;
                }
            }
        }

        if (!changed) {
            break;
        }
    }
}



// ==========================================================================
// eval_const_expr — compile-time constant expression evaluation
// ==========================================================================

[[nodiscard]] std::optional<int64_t> Generator::sizeof_value(const NodeExprSizeof& sz) const {
    if (sz.type.has_value()) return static_cast<int64_t>(type_size(*sz.type));
    if (!sz.struct_name.empty()) {
        auto info = get_struct_info(sz.struct_name);
        if (info.has_value()) return static_cast<int64_t>(info->size);
        return std::nullopt;
    }
    if (sz.expr) {
        // Only the width matters. Identifiers resolve to their declared type;
        // everything else is 64-bit (which is also correct for pointers).
        Type t = Type::make_int(IntType::i64);
        if (auto* ident = std::get_if<NodeExprIdent*>(&sz.expr->var)) {
            const std::string& name = (*ident)->ident.value.value();
            for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
                auto found = it->vars.find(name);
                if (found != it->vars.end()) {
                    const Var& var = found->second;
                    if (var.array_size > 0)
                        return static_cast<int64_t>(var.array_size * var.elem_size);
                    t = var.type;
                    break;
                }
            }
        }
        return static_cast<int64_t>(type_size(t));
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<int64_t> Generator::eval_const_expr(const NodeExpr* expr) const
{
    struct ConstEvalVisitor {
        const Generator* gen;
        std::optional<int64_t> result;

        void operator()(const NodeExprIntLit* lit) {
            try {
                result = static_cast<int64_t>(std::stoll(lit->int_lit.value.value()));
            } catch (const std::out_of_range&) {
                gen->fail(lit->int_lit.loc,
                    "Integer literal '" + lit->int_lit.value.value() +
                    "' out of range for 64-bit signed integer");
            }
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
                    if (lhs && rhs) {
                        int64_t a = lhs.value(), bv = rhs.value();
                        int64_t res;
                        if (__builtin_add_overflow(a, bv, &res)) {
                            gen->fail({}, "Integer overflow in constant expression");
                        }
                        result = res;
                    }
                }
                void operator()(const BinExprSub* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) {
                        int64_t a = lhs.value(), bv = rhs.value();
                        int64_t res;
                        if (__builtin_sub_overflow(a, bv, &res)) {
                            gen->fail({}, "Integer overflow in constant expression");
                        }
                        result = res;
                    }
                }
                void operator()(const BinExprMulti* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) {
                        int64_t a = lhs.value(), bv = rhs.value();
                        int64_t res;
                        if (__builtin_mul_overflow(a, bv, &res)) {
                            gen->fail({}, "Integer overflow in constant expression");
                        }
                        result = res;
                    }
                }
                void operator()(const BinExprDiv* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) {
                        if (rhs.value() == 0) {
                            gen->fail({}, "Division by zero in constant expression");
                        }
                        result = lhs.value() / rhs.value();
                    }
                }
                void operator()(const BinExprMod* b) {
                    auto lhs = gen->eval_const_expr(b->lhs);
                    auto rhs = gen->eval_const_expr(b->rhs);
                    if (lhs && rhs) {
                        if (rhs.value() == 0) {
                            gen->fail({}, "Division by zero in constant expression");
                        }
                        result = lhs.value() % rhs.value();
                    }
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

        void operator()(const NodeExprSizeof* sz) {
            result = gen->sizeof_value(*sz);
        }

        void operator()(const NodeExprStructLit*) {}

        void operator()(const NodeExprCast* cast) {
            auto inner = gen->eval_const_expr(cast->expr);
            if (!inner) return;
            int64_t v = *inner;
            if (cast->target.kind == Type::Int) {
                switch (cast->target.int_type) {
                    case IntType::i8:  v = static_cast<int8_t>(v);  break;
                    case IntType::u8:  v = v & 0xFF;                break;
                    case IntType::i16: v = static_cast<int16_t>(v); break;
                    case IntType::u16: v = v & 0xFFFF;              break;
                    case IntType::i32: v = static_cast<int32_t>(v); break;
                    case IntType::u32: v = static_cast<uint32_t>(v); break;
                    default: break;
                }
            }
            result = v;
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
