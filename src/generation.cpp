#include "generation.hpp"
#include "backend/x86_64/backend.hpp"

Generator::Generator(NodeProg root, std::unique_ptr<Backend> backend)
    : m_prog(std::move(root))
    , m_backend(backend ? std::move(backend) : std::make_unique<X8664Backend>(m_output))
{
    m_scopes.push_back({0, {}});
}

void Generator::gen_func_def(const NodeFuncDef& func)
{
        size_t saved_stack_size = m_stack_size;

        m_backend->label(func.name.value.value());
        m_backend->func_prologue();
        m_stack_size = 1;

        enter_scope();
        m_in_function = true;
        m_func_epilogue_label = ".L" + func.name.value.value() + "_epilogue";

        for (size_t i = 0; i < func.params.size(); i++) {
            std::string param_name = func.params[i].value.value();
            auto& scope = m_scopes.back();
            scope.vars[param_name] = Var { .stack_loc = m_stack_size };
            scope.var_count++;
            m_backend->emit_insn("mov", "rax, [rbp + " + std::to_string(16 + i * 8) + "]");
            m_backend->push("rax");
            m_stack_size++;
        }

        for (const auto& s : func.body->stmts) {
            gen_stmt(s);
        }

        m_backend->label(m_func_epilogue_label);
        m_backend->func_epilogue();
        m_backend->ret();
        m_in_function = false;
        m_func_epilogue_label.clear();

        m_scopes.pop_back();
        m_stack_size = saved_stack_size;
}

void Generator::enter_scope()
{
        m_scopes.push_back({0, {}});
}

void Generator::exit_scope()
{
        auto& scope = m_scopes.back();
        m_stack_size -= scope.var_count;
        if (scope.var_count > 0) {
            m_backend->adjust_stack(scope.var_count * 8);
        }
        m_scopes.pop_back();
}

void Generator::declare_var(const std::string& name, IntType type, SourceLoc loc) {
    auto& scope = m_scopes.back();
        if (scope.vars.contains(name)) {
            lsp_exit(loc, "Identifier already used in this scope: " + name);
        }
        scope.vars[name] = Var { .stack_loc = m_stack_size, .type = type };
        scope.var_count++;
}

Var Generator::lookup_var(const std::string& name, SourceLoc loc) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (it->vars.contains(name)) {
                return it->vars.at(name);
            }
        }
        lsp_exit(loc, "Undeclared identifier: " + name);
}

void Generator::gen_expr(const NodeExpr& expr)
{
        struct ExprVisitor {

            Generator* gen;

            void operator()(const NodeExprIntLit* expr_int_lit)
            {
                gen->backend()->load_imm("rax", std::stoll(expr_int_lit->int_lit.value.value()));
                gen->push("rax");
            }

            void operator()(const NodeExprIdent* expr_ident)
            {
                const auto& name = expr_ident->ident.value.value();
                for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                    if (it->vars.contains(name)) {
                        const auto var = it->vars.at(name);
                        if (var.array_size > 0 || var.type == IntType::i64 || var.type == IntType::u64) {
                            std::stringstream offset;
                            offset << "QWORD [rsp + " << (gen->m_stack_size - var.stack_loc - 1) * 8 << "]\n";
                            gen->push(offset.str());
                        } else {
                            gen->backend()->emit_insn("mov", "rax, QWORD [rsp + " + std::to_string((gen->m_stack_size - var.stack_loc - 1) * 8) + "]");
                            gen->extend(var.type);
                            gen->push("rax");
                        }
                        return;
                    }
                }
                if (gen->m_globals.contains(name)) {
                    gen->backend()->load("rax", gen->backend()->addr_label(name));
                    gen->push("rax");
                    return;
                }
                if (gen->m_constants.contains(name)) {
                    gen->backend()->load_imm("rax", gen->m_constants.at(name));
                    gen->push("rax");
                    return;
                }
                lsp_exit(expr_ident->ident.loc, "Undeclared identifier: " + name);
            }

            void operator()(const NodeExprStringLit* expr_str)
            {
                auto str = expr_str->value.value.value();
                auto label = gen->new_string_label();
                gen->m_strings.push_back({label, str});
                gen->backend()->emit_insn("mov", "rax, " + label);
                gen->push("rax");
            }

            void operator()(const NodeExprIndex* expr_index)
            {
                const auto& name = expr_index->name.value.value();
                // Try local scope first
                for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                    if (it->vars.contains(name)) {
                        const auto var = it->vars.at(name);
                        gen->gen_expr(*expr_index->index);
                        gen->pop("rdi");
                        size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                        gen->backend()->lea("rax", gen->backend()->addr_sp(base_offset));
                        gen->backend()->mov("rcx", "rdi");
                        gen->backend()->shl("rcx", "3");
                        gen->backend()->sub("rax", "rcx");
                        gen->backend()->load("rax", "[rax]");
                        gen->push("rax");
                        return;
                    }
                }
                // Try global
                if (gen->m_global_var_info.contains(name)) {
                    const auto var = gen->m_global_var_info.at(name);
                    gen->gen_expr(*expr_index->index);
                    gen->pop("rdi");
                    gen->backend()->lea("rax", gen->backend()->addr_label(name));
                    gen->backend()->mov("rcx", "rdi");
                    gen->backend()->shl("rcx", "3");
                    gen->backend()->add("rax", "rcx");
                    gen->backend()->load("rax", "[rax]");
                    gen->push("rax");
                    return;
                }
                lsp_exit(expr_index->name.loc, "Undeclared identifier: " + name);
            }

            void operator()(const NodeExprBitNot* bit_not)
            {
                gen->gen_expr(*bit_not->expr);
                gen->pop("rax");
                gen->backend()->not_("rax");
                gen->push("rax");
            }

            void operator()(const NodeExprTernary* ternary)
            {
                auto label_false = gen->new_label();
                auto label_end = gen->new_label();
                gen->gen_expr(*ternary->cond);
                gen->pop("rax");
                gen->backend()->test("rax", "rax");
                gen->backend()->jz(label_false);
                auto saved_stack = gen->m_stack_size;
                gen->gen_expr(*ternary->then_expr);
                gen->m_stack_size = saved_stack + 1;
                gen->backend()->jmp(label_end);
                gen->backend()->label(label_false);
                gen->m_stack_size = saved_stack;
                gen->gen_expr(*ternary->else_expr);
                gen->backend()->label(label_end);
            }

            void operator()(const NodeExprRead*) const
            {
                gen->backend()->call("_sodium_read_int");
                gen->backend()->push("rax");
                gen->m_stack_size++;
            }

            void operator()(const NodeExprArrLit* arr_lit) const
            {
                for (size_t i = 0; i < arr_lit->elements.size(); i++) {
                    gen->gen_expr(*arr_lit->elements[i]);
                }
            }

            void operator()(const NodeExprFieldAccess* field_access) const
            {
                const auto& obj_name = field_access->obj_name.value.value();
                const auto& field_name = field_access->field_name.value.value();
                auto var = gen->lookup_var(obj_name, field_access->obj_name.loc);
                if (var.struct_type.empty()) {
                    lsp_exit(field_access->obj_name.loc, "Not a struct variable: " + obj_name);
                }
                auto struct_info = gen->get_struct_info(var.struct_type);
                if (!struct_info.has_value()) {
                    lsp_exit(field_access->field_name.loc, "Unknown struct type: " + var.struct_type);
                }
                auto offset_it = struct_info.value().field_offsets.find(field_name);
                if (offset_it == struct_info.value().field_offsets.end()) {
                    lsp_exit(field_access->field_name.loc, "Unknown field '" + field_name + "' in struct '" + var.struct_type + "'");
                }
                size_t field_offset = offset_it->second;
                // Compute address: base of struct + field_offset * 8
                size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                size_t field_addr_offset = base_offset - field_offset * 8;
                gen->backend()->load("rax", gen->backend()->addr_sp(field_addr_offset));
                gen->push("rax");
            }

            void operator()(const NodeExprAddrOf* addr_of) const
            {
                // &var — push the stack address of the variable
                // For now we only support simple identifiers
                auto* inner = addr_of->expr;
                if (auto* ident = std::get_if<NodeExprIdent*>(&inner->var)) {
                    const auto& name = (*ident)->ident.value.value();
                    for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                        if (it->vars.contains(name)) {
                            const auto var = it->vars.at(name);
                            size_t offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                            gen->backend()->lea("rax", gen->backend()->addr_sp(offset));
                            gen->push("rax");
                            return;
                        }
                    }
                    if (gen->m_globals.contains(name)) {
                        gen->backend()->lea("rax", gen->backend()->addr_label(name));
                        gen->push("rax");
                        return;
                    }
                    lsp_exit(addr_of->ampersand.loc, "Undeclared identifier: " + name);
                } else if (auto* field = std::get_if<NodeExprFieldAccess*>(&inner->var)) {
                    // &obj.field
                    const auto& obj_name = (*field)->obj_name.value.value();
                    const auto& field_name = (*field)->field_name.value.value();
                    auto var = gen->lookup_var(obj_name, (*field)->obj_name.loc);
                    if (var.struct_type.empty()) {
                        lsp_exit((*field)->obj_name.loc, "Not a struct variable: " + obj_name);
                    }
                    auto struct_info = gen->get_struct_info(var.struct_type);
                    if (!struct_info.has_value()) {
                        lsp_exit((*field)->field_name.loc, "Unknown struct type: " + var.struct_type);
                    }
                    auto offset_it = struct_info.value().field_offsets.find(field_name);
                    if (offset_it == struct_info.value().field_offsets.end()) {
                        lsp_exit((*field)->field_name.loc, "Unknown field '" + field_name + "' in struct '" + var.struct_type + "'");
                    }
                    size_t field_offset = offset_it->second;
                    size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                    size_t field_addr_offset = base_offset - field_offset * 8;
                    gen->backend()->lea("rax", gen->backend()->addr_sp(field_addr_offset));
                    gen->push("rax");
                } else {
                    lsp_exit(addr_of->ampersand.loc, "Cannot take address of this expression");
                }
            }

            void operator()(const NodeExprDeref* deref) const
            {
                // *ptr — evaluate ptr, then load from that address
                gen->gen_expr(*deref->expr);
                gen->pop("rax");
                gen->backend()->load("rax", "[rax]");
                gen->push("rax");
            }

            void operator()(const NodeExprCall* expr_call)
            {
                const auto& fname = expr_call->name.value.value();

                // Handle malloc/free specially
                if (fname == "malloc") {
                    // Call the internal malloc helper
                    for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                        gen->gen_expr(**it);
                    }
                    gen->pop("rdi");  // size in rdi
                    gen->backend()->call("_sodium_malloc");
                    gen->backend()->push("rax");
                    gen->m_stack_size++;
                    return;
                }
                if (fname == "free") {
                    // Call the internal free helper
                    for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                        gen->gen_expr(**it);
                    }
                    gen->pop("rdi");  // pointer in rdi
                    gen->backend()->call("_sodium_free");
                    return;
                }

                if (!gen->m_func_names.contains(fname)) {
                    lsp_exit(expr_call->name.loc, "Undefined function: " + fname);
                }
                for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                    gen->gen_expr(**it);
                }
                gen->backend()->call(fname);
                size_t arg_count = expr_call->args.size();
                if (arg_count > 0) {
                    gen->backend()->adjust_stack(arg_count * 8);
                    gen->m_stack_size -= arg_count;
                }
                gen->backend()->push("rax");
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
                        gen->backend()->add("rax", "rdi");
                        gen->push("rax");
                    }
                    void operator()(const BinExprMulti* mul) {
                        gen->gen_expr(*mul->lhs);
                        gen->gen_expr(*mul->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->mul("rax", "rdi");
                        gen->push("rax");
                    }
                    void operator()(const BinExprSub* sub) {
                        gen->gen_expr(*sub->lhs);
                        gen->gen_expr(*sub->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->sub("rax", "rdi");
                        gen->push("rax");
                    }
                    void operator()(const BinExprDiv* div) {
                        gen->gen_expr(*div->lhs);
                        gen->gen_expr(*div->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->sign_extend_rax();
                        gen->backend()->div("rdi");
                        gen->push("rax");
                    }
                    void operator()(const BinExprMod* mod) {
                        gen->gen_expr(*mod->lhs);
                        gen->gen_expr(*mod->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->sign_extend_rax();
                        gen->backend()->div("rdi");
                        gen->push("rdx");
                    }
                    void operator()(const BinExprLT* lt) {
                        gen->gen_expr(*lt->lhs);
                        gen->gen_expr(*lt->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->cmp("rax", "rdi");
                        gen->backend()->set_cc("al", "l");
                        gen->backend()->movzx("rax", "al", 8);
                        gen->push("rax");
                    }
                    void operator()(const BinExprGT* gt) {
                        gen->gen_expr(*gt->lhs);
                        gen->gen_expr(*gt->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->cmp("rax", "rdi");
                        gen->backend()->set_cc("al", "g");
                        gen->backend()->movzx("rax", "al", 8);
                        gen->push("rax");
                    }
                    void operator()(const BinExprEQ* eq) {
                        gen->gen_expr(*eq->lhs);
                        gen->gen_expr(*eq->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->cmp("rax", "rdi");
                        gen->backend()->set_cc("al", "e");
                        gen->backend()->movzx("rax", "al", 8);
                        gen->push("rax");
                    }
                    void operator()(const BinExprNEQ* neq) {
                        gen->gen_expr(*neq->lhs);
                        gen->gen_expr(*neq->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->cmp("rax", "rdi");
                        gen->backend()->set_cc("al", "ne");
                        gen->backend()->movzx("rax", "al", 8);
                        gen->push("rax");
                    }
                    void operator()(const BinExprLTE* lte) {
                        gen->gen_expr(*lte->lhs);
                        gen->gen_expr(*lte->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->cmp("rax", "rdi");
                        gen->backend()->set_cc("al", "le");
                        gen->backend()->movzx("rax", "al", 8);
                        gen->push("rax");
                    }
                    void operator()(const BinExprGTE* gte) {
                        gen->gen_expr(*gte->lhs);
                        gen->gen_expr(*gte->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->cmp("rax", "rdi");
                        gen->backend()->set_cc("al", "ge");
                        gen->backend()->movzx("rax", "al", 8);
                        gen->push("rax");
                    }
                void operator()(const BinExprAnd* and_op) {
                        gen->gen_expr(*and_op->lhs);
                        gen->pop("rax");
                        gen->backend()->test("rax", "rax");
                        auto false_label = gen->new_label();
                        auto end_label = gen->new_label();
                        gen->backend()->jz(false_label);
                        gen->gen_expr(*and_op->rhs);
                        gen->pop("rax");
                        gen->backend()->test("rax", "rax");
                        gen->backend()->jz(false_label);
                        gen->backend()->load_imm("rax", 1);
                        gen->backend()->jmp(end_label);
                        gen->backend()->label(false_label);
                        gen->backend()->load_imm("rax", 0);
                        gen->backend()->label(end_label);
                        gen->push("rax");
                    }
                    void operator()(const BinExprOr* or_op) {
                        gen->gen_expr(*or_op->lhs);
                        gen->pop("rax");
                        gen->backend()->test("rax", "rax");
                        auto true_label = gen->new_label();
                        auto end_label = gen->new_label();
                        gen->backend()->jnz(true_label);
                        gen->gen_expr(*or_op->rhs);
                        gen->pop("rax");
                        gen->backend()->test("rax", "rax");
                        gen->backend()->jnz(true_label);
                        gen->backend()->load_imm("rax", 0);
                        gen->backend()->jmp(end_label);
                        gen->backend()->label(true_label);
                        gen->backend()->load_imm("rax", 1);
                        gen->backend()->label(end_label);
                        gen->push("rax");
                    }
                    void operator()(const BinExprBitAnd* and_op) {
                        gen->gen_expr(*and_op->lhs);
                        gen->gen_expr(*and_op->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->and_("rax", "rdi");
                        gen->push("rax");
                    }
                    void operator()(const BinExprBitOr* or_op) {
                        gen->gen_expr(*or_op->lhs);
                        gen->gen_expr(*or_op->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->or_("rax", "rdi");
                        gen->push("rax");
                    }
                    void operator()(const BinExprXor* xor_op) {
                        gen->gen_expr(*xor_op->lhs);
                        gen->gen_expr(*xor_op->rhs);
                        gen->pop("rdi");
                        gen->pop("rax");
                        gen->backend()->xor_("rax", "rdi");
                        gen->push("rax");
                    }
                    void operator()(const BinExprShl* shl_op) {
                        gen->gen_expr(*shl_op->lhs);
                        gen->gen_expr(*shl_op->rhs);
                        gen->pop("rcx");
                        gen->pop("rax");
                        gen->backend()->shl("rax", "cl");
                        gen->push("rax");
                    }
                    void operator()(const BinExprShr* shr_op) {
                        gen->gen_expr(*shr_op->lhs);
                        gen->gen_expr(*shr_op->rhs);
                        gen->pop("rcx");
                        gen->pop("rax");
                        gen->backend()->shr("rax", "cl");
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

void Generator::gen_stmt(const NodeStmt& stmt)
{
        struct StmtVisitor {
            Generator* gen;
            void operator()(const NodeStmtExit* stmt_exit) const
            {
                gen->gen_expr(*stmt_exit->expr);
                gen->pop("rdi");
                gen->backend()->call("_sodium_exit");
                gen->m_emitted_exit = true;
            }

            void operator()(const NodeStmtLet* stmt_let) const
            {
                auto var_type = stmt_let->type.value_or(IntType::i64);
                
                // Handle struct-typed variable: var p: Point
                if (!stmt_let->struct_type_name.empty()) {
                    auto struct_info = gen->get_struct_info(stmt_let->struct_type_name);
                    if (!struct_info.has_value()) {
                        lsp_exit(stmt_let->ident.loc, "Unknown struct type: " + stmt_let->struct_type_name);
                    }
                    auto& scope = gen->m_scopes.back();
                    scope.vars[stmt_let->ident.value.value()] = Var { .stack_loc = gen->m_stack_size, .array_size = struct_info.value().size, .struct_type = stmt_let->struct_type_name };
                    scope.var_count += struct_info.value().size;
                    // Zero-initialize all fields
                    for (size_t i = 0; i < struct_info.value().size; i++) {
                        gen->backend()->emit_insn("push", "0");
                    }
                    gen->m_stack_size += struct_info.value().size;
                    return;
                }
                
                if (auto arr_lit = std::get_if<NodeExprArrLit*>(&stmt_let->expr->var)) {
                    auto& scope = gen->m_scopes.back();
                    scope.vars[stmt_let->ident.value.value()] = Var { .stack_loc = gen->m_stack_size, .array_size = (*arr_lit)->elements.size(), .type = var_type };
                    scope.var_count += (*arr_lit)->elements.size();
                    gen->gen_expr(*stmt_let->expr);
                } else {
                    gen->declare_var(stmt_let->ident.value.value(), var_type, stmt_let->ident.loc);
                    gen->gen_expr(*stmt_let->expr);
                    gen->pop("rax");
                    gen->truncate(var_type);
                    gen->push("rax");
                }
            }

            void operator()(const NodeStmtGlobal* stmt_global) const
            {
                // All allocation and init registration is handled by the
                // pre-collection pass (collect_globals). This visitor just
                // ensures the variable is in m_globals for identifier lookup.
                gen->m_globals[stmt_global->name.value.value()] = true;
            }

            void operator()(const NodeStmtConst* stmt_const) const
            {
                // const declarations are evaluated at compile time.
                // If we reach here during codegen, the value is already
                // in m_constants from collect_globals. This visitor is
                // a no-op (constants produce no runtime code).
            }

            void operator()(const NodeStmtExpr* stmt_expr) const
            {
                gen->gen_expr(*stmt_expr->expr);
                gen->pop("rax");
            }

            void operator()(const NodeStmtAssign* stmt_assign) const
            {
                const auto& name = stmt_assign->ident.value.value();
                for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                    if (it->vars.contains(name)) {
                        const auto var = it->vars.at(name);
                        size_t offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                        if (stmt_assign->op == AssignOp::assign && var.array_size > 0) {
                            if (auto arr_lit = std::get_if<NodeExprArrLit*>(&stmt_assign->expr->var)) {
                                if ((*arr_lit)->elements.size() != var.array_size) {
                                    lsp_exit(stmt_assign->ident.loc, "Array size mismatch");
                                }
                                size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                                for (size_t i = 0; i < var.array_size; i++) {
                                    gen->gen_expr(*(*arr_lit)->elements[i]);
                                    gen->pop("rax");
                                    gen->backend()->emit_insn("mov", "QWORD [rsp + " + std::to_string((base_offset - i * 8)) + "], rax");
                                }
                                return;
                            }
                        }
                        if (var.array_size > 0) {
                            lsp_exit(stmt_assign->ident.loc, "Array assignment requires an array literal (e.g., arr = [1, 2, 3])");
                        }
                        if (stmt_assign->op == AssignOp::assign) {
                            gen->gen_expr(*stmt_assign->expr);
                            gen->pop("rax");
                            if (var.array_size == 0 && var.type != IntType::i64 && var.type != IntType::u64) {
                                gen->truncate(var.type);
                            }
                            gen->backend()->emit_insn("mov", "QWORD [rsp + " + std::to_string(offset) + "], rax");
                        } else {
                            gen->backend()->emit_insn("push", "QWORD [rsp + " + std::to_string(offset) + "]");
                            gen->m_stack_size++;
                            gen->gen_expr(*stmt_assign->expr);
                            gen->pop("rdi");
                            gen->pop("rax");
                            if (var.array_size == 0 && var.type != IntType::i64 && var.type != IntType::u64) {
                                gen->extend(var.type);
                            }
                            switch (stmt_assign->op) {
                                case AssignOp::add_assign:
                                    gen->backend()->add("rax", "rdi"); break;
                                case AssignOp::sub_assign:
                                    gen->backend()->sub("rax", "rdi"); break;
                                case AssignOp::mul_assign:
                                    gen->backend()->mul("rax", "rdi"); break;
                                case AssignOp::div_assign:
                                    gen->backend()->sign_extend_rax();
                                    gen->backend()->div("rdi"); break;
                                case AssignOp::mod_assign:
                                    gen->backend()->sign_extend_rax();
                                    gen->backend()->div("rdi");
                                    gen->backend()->mov("rax", "rdx"); break;
                                case AssignOp::bitand_assign:
                                    gen->backend()->and_("rax", "rdi"); break;
                                case AssignOp::bitor_assign:
                                    gen->backend()->or_("rax", "rdi"); break;
                                case AssignOp::bitxor_assign:
                                    gen->backend()->xor_("rax", "rdi"); break;
                                case AssignOp::shl_assign:
                                    gen->backend()->mov("rcx", "rdi");
                                    gen->backend()->shl("rax", "cl"); break;
                                case AssignOp::shr_assign:
                                    gen->backend()->mov("rcx", "rdi");
                                    gen->backend()->shr("rax", "cl"); break;
                                default:
                                    break;
                            }
                            if (var.array_size == 0 && var.type != IntType::i64 && var.type != IntType::u64) {
                                gen->truncate(var.type);
                            }
                            gen->backend()->emit_insn("mov", "QWORD [rsp + " + std::to_string(offset) + "], rax");
                        }
                        return;
                    }
                }
                if (gen->m_globals.contains(name)) {
                    if (stmt_assign->op == AssignOp::assign) {
                        gen->gen_expr(*stmt_assign->expr);
                        gen->pop("rax");
                        gen->backend()->store(gen->backend()->addr_label(name), "rax");
                    } else {
                        gen->backend()->load("rax", gen->backend()->addr_label(name));
                        gen->push("rax");
                        gen->gen_expr(*stmt_assign->expr);
                        gen->pop("rdi");
                        gen->pop("rax");
                        switch (stmt_assign->op) {
                            case AssignOp::add_assign:
                                gen->backend()->add("rax", "rdi"); break;
                            case AssignOp::sub_assign:
                                gen->backend()->sub("rax", "rdi"); break;
                            case AssignOp::mul_assign:
                                gen->backend()->mul("rax", "rdi"); break;
                            case AssignOp::div_assign:
                                gen->backend()->sign_extend_rax();
                                gen->backend()->div("rdi"); break;
                            case AssignOp::mod_assign:
                                gen->backend()->sign_extend_rax();
                                gen->backend()->div("rdi");
                                gen->backend()->mov("rax", "rdx"); break;
                            case AssignOp::bitand_assign:
                                gen->backend()->and_("rax", "rdi"); break;
                            case AssignOp::bitor_assign:
                                gen->backend()->or_("rax", "rdi"); break;
                            case AssignOp::bitxor_assign:
                                gen->backend()->xor_("rax", "rdi"); break;
                            case AssignOp::shl_assign:
                                gen->backend()->mov("rcx", "rdi");
                                gen->backend()->shl("rax", "cl"); break;
                            case AssignOp::shr_assign:
                                gen->backend()->mov("rcx", "rdi");
                                gen->backend()->shr("rax", "cl"); break;
                            default:
                                break;
                        }
                        gen->backend()->store(gen->backend()->addr_label(name), "rax");
                    }
                    return;
                }
                if (gen->m_constants.contains(name)) {
                    lsp_exit(stmt_assign->ident.loc, "Cannot assign to constant '" + name + "'");
                }
                lsp_exit(stmt_assign->ident.loc, "Undeclared identifier: " + name);
            }

            void operator()(const NodeStmtIf* stmt_if) const
            {
                auto label_else = gen->new_label();
                auto label_end = gen->new_label();

                gen->gen_expr(*stmt_if->cond);
                gen->pop("rax");
                gen->backend()->test("rax", "rax");

                if (stmt_if->else_block) {
                    gen->backend()->jz(label_else);
                } else {
                    gen->backend()->jz(label_end);
                }

                gen->enter_scope();
                for (const auto& s : stmt_if->then_block->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                if (stmt_if->else_block) {
                    gen->backend()->jmp(label_end);
                    gen->backend()->label(label_else);
                    gen->enter_scope();
                    for (const auto& s : stmt_if->else_block->stmts) {
                        gen->gen_stmt(s);
                    }
                    gen->exit_scope();
                }

                gen->backend()->label(label_end);
            }

            void operator()(const NodeStmtWhile* stmt_while) const
            {
                auto label_begin = gen->new_label();
                auto label_end = gen->new_label();

                gen->m_loop_stack.push_back({ .begin_label = label_begin, .end_label = label_end, .continue_label = label_begin });
                gen->m_break_stack.push_back(label_end);

                gen->backend()->label(label_begin);
                gen->gen_expr(*stmt_while->cond);
                gen->pop("rax");
                gen->backend()->test("rax", "rax");
                gen->backend()->jz(label_end);

                gen->enter_scope();
                for (const auto& s : stmt_while->body->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                gen->backend()->jmp(label_begin);
                gen->backend()->label(label_end);

                gen->m_loop_stack.pop_back();
                gen->m_break_stack.pop_back();
            }

            void operator()(const NodeStmtDoWhile* stmt_do_while) const
            {
                auto label_begin = gen->new_label();
                auto label_cont = gen->new_label();
                auto label_end = gen->new_label();

                gen->m_loop_stack.push_back({ .begin_label = label_begin, .end_label = label_end, .continue_label = label_cont });
                gen->m_break_stack.push_back(label_end);

                gen->backend()->label(label_begin);

                gen->enter_scope();
                for (const auto& s : stmt_do_while->body->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                gen->backend()->label(label_cont);
                gen->gen_expr(*stmt_do_while->cond);
                gen->pop("rax");
                gen->backend()->test("rax", "rax");
                gen->backend()->jnz(label_begin);
                gen->backend()->label(label_end);

                gen->m_loop_stack.pop_back();
                gen->m_break_stack.pop_back();
            }

            void operator()(const NodeStmtSwitch* stmt_switch) const
            {
                auto label_end = gen->new_label();
                gen->m_break_stack.push_back(label_end);

                gen->gen_expr(*stmt_switch->expr);

                std::vector<std::string> case_labels;
                size_t default_idx = stmt_switch->cases.size();
                for (size_t i = 0; i < stmt_switch->cases.size(); i++) {
                    case_labels.push_back(gen->new_label());
                    if (stmt_switch->cases[i].value == nullptr) {
                        default_idx = i;
                    }
                }

                for (size_t i = 0; i < stmt_switch->cases.size(); i++) {
                    if (stmt_switch->cases[i].value == nullptr) continue;
                    gen->backend()->emit_insn("push", "QWORD [rsp]");
                    gen->m_stack_size++;
                    gen->gen_expr(*stmt_switch->cases[i].value);
                    gen->pop("rdi");
                    gen->pop("rax");
                    gen->backend()->cmp("rax", "rdi");
                    gen->backend()->je(case_labels[i]);
                }

                gen->backend()->pop("rax");
                gen->m_stack_size--;

                if (default_idx < stmt_switch->cases.size()) {
                    gen->backend()->jmp(case_labels[default_idx]);
                }
                gen->backend()->jmp(label_end);

                for (size_t i = 0; i < stmt_switch->cases.size(); i++) {
                    gen->backend()->label(case_labels[i]);
                    for (const auto& s : stmt_switch->cases[i].stmts) {
                        gen->gen_stmt(s);
                    }
                }

                gen->backend()->label(label_end);
                gen->m_break_stack.pop_back();
            }

            void operator()(const NodeStmtArrDecl* stmt_arr) const
            {
                // Evaluate size expression at compile time (must be constant).
                auto const_size = gen->eval_const_expr(stmt_arr->size);
                if (!const_size.has_value()) {
                    lsp_exit(stmt_arr->loc, "Array size must be a compile-time constant expression");
                }
                size_t size = static_cast<size_t>(const_size.value());
                auto& scope = gen->m_scopes.back();
                scope.vars[stmt_arr->name.value.value()] = Var { .stack_loc = gen->m_stack_size, .array_size = size };
                scope.var_count += size;
                gen->backend()->load_imm("rcx", size);
                std::string label_loop = gen->new_label();
                std::string label_end = gen->new_label();
                gen->backend()->label(label_loop);
                gen->backend()->test("rcx", "rcx");
                gen->backend()->jz(label_end);
                gen->backend()->emit_insn("push", "0");
                gen->m_stack_size += size;
                gen->backend()->emit_insn("dec", "rcx");
                gen->backend()->jmp(label_loop);
                gen->backend()->label(label_end);
            }

            void operator()(const NodeStmtArrAssign* stmt_arr_assign) const
            {
                const auto& name = stmt_arr_assign->name.value.value();
                gen->gen_expr(*stmt_arr_assign->index);
                gen->gen_expr(*stmt_arr_assign->expr);
                gen->pop("rax");
                gen->pop("rdi");
                // Try local scope first
                for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                    if (it->vars.contains(name)) {
                        const auto var = it->vars.at(name);
                        size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                        gen->backend()->lea("rcx", gen->backend()->addr_sp(base_offset));
                        gen->backend()->mov("rsi", "rdi");
                        gen->backend()->shl("rsi", "3");
                        gen->backend()->sub("rcx", "rsi");
                        gen->backend()->emit_insn("mov", "[rcx], rax");
                        return;
                    }
                }
                // Try global
                if (gen->m_global_var_info.contains(name)) {
                    gen->backend()->lea("rcx", gen->backend()->addr_label(name));
                    gen->backend()->mov("rsi", "rdi");
                    gen->backend()->shl("rsi", "3");
                    gen->backend()->add("rcx", "rsi");
                    gen->backend()->emit_insn("mov", "[rcx], rax");
                    return;
                }
                lsp_exit(stmt_arr_assign->name.loc, "Undeclared identifier: " + name);
            }

            void operator()(const NodeStmtReturn* stmt_ret) const
            {
                if (gen->m_in_function) {
                    if (stmt_ret->expr) {
                        gen->gen_expr(*stmt_ret->expr);
                        gen->pop("rax");
                    }
                    gen->backend()->jmp(gen->m_func_epilogue_label);
                } else {
                    if (!stmt_ret->expr) {
                        lsp_exit(stmt_ret->loc, "return with no value at top level");
                    }
                    gen->gen_expr(*stmt_ret->expr);
                    gen->pop("rdi");
                    gen->backend()->call("_sodium_exit");
                    gen->m_emitted_exit = true;
                }
            }

                        void operator()(const NodeStmtPrint* stmt_print) const
            {
                gen->gen_expr(*stmt_print->expr);
                gen->pop("rdi");
                gen->backend()->call("_sodium_print_int");
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
                gen->enter_scope();

                if (stmt_for->init.has_value()) {
                    gen->gen_stmt(stmt_for->init.value());
                }

                auto label_begin = gen->new_label();
                auto label_cont = gen->new_label();
                auto label_end = gen->new_label();

                gen->m_loop_stack.push_back({ .begin_label = label_begin, .end_label = label_end, .continue_label = label_cont });
                gen->m_break_stack.push_back(label_end);

                gen->backend()->label(label_begin);

                if (stmt_for->cond) {
                    gen->gen_expr(*stmt_for->cond);
                    gen->pop("rax");
                    gen->backend()->test("rax", "rax");
                    gen->backend()->jz(label_end);
                }

                gen->enter_scope();
                for (const auto& s : stmt_for->body->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();

                gen->backend()->label(label_cont);
                if (stmt_for->update) {
                    NodeStmt update_stmt;
                    update_stmt.var = stmt_for->update;
                    gen->gen_stmt(update_stmt);
                }

                gen->backend()->jmp(label_begin);
                gen->backend()->label(label_end);

                gen->m_loop_stack.pop_back();
                gen->m_break_stack.pop_back();

                gen->exit_scope();
            }

            void operator()(const NodeStmtBreak* stmt_break) const
            {
                if (gen->m_break_stack.empty()) {
                    lsp_exit(stmt_break->loc, "break outside loop or switch");
                }
                gen->backend()->jmp(gen->m_break_stack.back());
            }

            void operator()(const NodeStmtContinue* stmt_continue) const
            {
                for (auto it = gen->m_loop_stack.rbegin(); it != gen->m_loop_stack.rend(); ++it) {
                    if (!it->continue_label.empty()) {
                        gen->backend()->jmp(it->continue_label);
                        return;
                    }
                }
                lsp_exit(stmt_continue->loc, "continue outside loop");
            }

            void operator()(const NodeStmtFieldAssign* stmt_field) const
            {
                const auto& obj_name = stmt_field->obj_name.value.value();
                const auto& field_name = stmt_field->field_name.value.value();
                auto var = gen->lookup_var(obj_name, stmt_field->obj_name.loc);
                if (var.struct_type.empty()) {
                    lsp_exit(stmt_field->obj_name.loc, "Not a struct variable: " + obj_name);
                }
                auto struct_info = gen->get_struct_info(var.struct_type);
                if (!struct_info.has_value()) {
                    lsp_exit(stmt_field->field_name.loc, "Unknown struct type: " + var.struct_type);
                }
                auto offset_it = struct_info.value().field_offsets.find(field_name);
                if (offset_it == struct_info.value().field_offsets.end()) {
                    lsp_exit(stmt_field->field_name.loc, "Unknown field '" + field_name + "' in struct '" + var.struct_type + "'");
                }
                size_t field_offset = offset_it->second;
                size_t base_offset = (gen->m_stack_size - var.stack_loc - 1) * 8;
                size_t field_addr_offset = base_offset - field_offset * 8;
                
                if (stmt_field->op == AssignOp::assign) {
                    gen->gen_expr(*stmt_field->expr);
                    gen->pop("rax");
                    gen->backend()->emit_insn("mov", "QWORD [rsp + " + std::to_string(field_addr_offset) + "], rax");
                } else {
                    // Compound assignment: read field, apply op, write back
                    gen->backend()->emit_insn("push", "QWORD [rsp + " + std::to_string(field_addr_offset) + "]");
                    gen->m_stack_size++;
                    gen->gen_expr(*stmt_field->expr);
                    gen->pop("rdi");
                    gen->pop("rax");
                    switch (stmt_field->op) {
                        case AssignOp::add_assign:
                            gen->backend()->add("rax", "rdi"); break;
                        case AssignOp::sub_assign:
                            gen->backend()->sub("rax", "rdi"); break;
                        case AssignOp::mul_assign:
                            gen->backend()->mul("rax", "rdi"); break;
                        case AssignOp::div_assign:
                            gen->backend()->sign_extend_rax();
                            gen->backend()->div("rdi"); break;
                        case AssignOp::mod_assign:
                            gen->backend()->sign_extend_rax();
                            gen->backend()->div("rdi");
                            gen->backend()->mov("rax", "rdx"); break;
                        case AssignOp::bitand_assign:
                            gen->backend()->and_("rax", "rdi"); break;
                        case AssignOp::bitor_assign:
                            gen->backend()->or_("rax", "rdi"); break;
                        case AssignOp::bitxor_assign:
                            gen->backend()->xor_("rax", "rdi"); break;
                        case AssignOp::shl_assign:
                            gen->backend()->mov("rcx", "rdi");
                            gen->backend()->shl("rax", "cl"); break;
                        case AssignOp::shr_assign:
                            gen->backend()->mov("rcx", "rdi");
                            gen->backend()->shr("rax", "cl"); break;
                        default:
                            break;
                    }
                    gen->backend()->emit_insn("mov", "QWORD [rsp + " + std::to_string(field_addr_offset) + "], rax");
                }
            }

            void operator()(const NodeStmtDerefAssign* stmt_deref) const
            {
                const auto& name = stmt_deref->ptr_expr;
                if (stmt_deref->op == AssignOp::assign) {
                    // *ptr = expr:
                    //   1. evaluate expr (value on stack)
                    //   2. evaluate ptr (address on stack)
                    //   3. pop address into rbx, pop value into rax
                    //   4. mov [rbx], rax
                    gen->gen_expr(*stmt_deref->expr);
                    gen->gen_expr(*stmt_deref->ptr_expr);
                    gen->pop("rbx");
                    gen->pop("rax");
                    gen->backend()->emit_insn("mov", "[rbx], rax");
                } else {
                    // *ptr += expr:
                    //   1. evaluate ptr (address on stack)
                    //   2. pop address into rbx
                    //   3. push [rbx] (current value)
                    //   4. evaluate expr
                    //   5. pop rdi (value), pop rax (current)
                    //   6. apply op
                    //   7. mov [rbx], rax
                    gen->gen_expr(*stmt_deref->ptr_expr);
                    gen->pop("rbx");
                    gen->backend()->emit_insn("push", "QWORD [rbx]");
                    gen->m_stack_size++;
                    gen->gen_expr(*stmt_deref->expr);
                    gen->pop("rdi");
                    gen->pop("rax");
                    switch (stmt_deref->op) {
                        case AssignOp::add_assign:
                            gen->backend()->add("rax", "rdi"); break;
                        case AssignOp::sub_assign:
                            gen->backend()->sub("rax", "rdi"); break;
                        case AssignOp::mul_assign:
                            gen->backend()->mul("rax", "rdi"); break;
                        case AssignOp::div_assign:
                            gen->backend()->sign_extend_rax();
                            gen->backend()->div("rdi"); break;
                        case AssignOp::mod_assign:
                            gen->backend()->sign_extend_rax();
                            gen->backend()->div("rdi");
                            gen->backend()->mov("rax", "rdx"); break;
                        case AssignOp::bitand_assign:
                            gen->backend()->and_("rax", "rdi"); break;
                        case AssignOp::bitor_assign:
                            gen->backend()->or_("rax", "rdi"); break;
                        case AssignOp::bitxor_assign:
                            gen->backend()->xor_("rax", "rdi"); break;
                        case AssignOp::shl_assign:
                            gen->backend()->mov("rcx", "rdi");
                            gen->backend()->shl("rax", "cl"); break;
                        case AssignOp::shr_assign:
                            gen->backend()->mov("rcx", "rdi");
                            gen->backend()->shr("rax", "cl"); break;
                        default:
                            break;
                    }
                    gen->backend()->emit_insn("mov", "[rbx], rax");
                }
            }
        };

        StmtVisitor visitor { .gen = this };
        std::visit(visitor, stmt.var);
}

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
                // else result stays empty (not a constant)
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

            void operator()(const NodeExprTernary* ternary) {
                auto cond = gen->eval_const_expr(ternary->cond);
                if (!cond) return;
                auto branch = cond.value() ? ternary->then_expr : ternary->else_expr;
                result = gen->eval_const_expr(branch);
            }

            // All other expression types are not compile-time constant
            void operator()(const NodeExprCall*) {}
            void operator()(const NodeExprStringLit*) {}
            void operator()(const NodeExprIndex*) {}
            void operator()(const NodeExprRead*) {}
            void operator()(const NodeExprArrLit*) {}
            void operator()(const NodeExprFieldAccess*) {}
            void operator()(const NodeExprAddrOf*) {}
            void operator()(const NodeExprDeref*) {}
        };

        ConstEvalVisitor visitor{this, std::nullopt};
        std::visit(visitor, expr->var);
        return visitor.result;
}

void Generator::collect_globals(const std::vector<NodeStmt>& stmts)
{
        for (const auto& stmt : stmts) {
            if (auto* global = std::get_if<NodeStmtGlobal*>(&stmt.var)) {
                const auto& name = (*global)->name.value.value();
                // Determine array size if any
                size_t arr_size = 0;
                if ((*global)->array_size) {
                    auto const_size = eval_const_expr((*global)->array_size);
                    if (!const_size.has_value()) {
                        lsp_exit((*global)->name.loc, "Global array size must be a compile-time constant expression");
                    }
                    arr_size = static_cast<size_t>(const_size.value());
                }
                m_globals[name] = true;
                if (arr_size > 0) {
                    m_global_var_info[name] = Var { .stack_loc = 0, .array_size = arr_size };
                    // Arrays go in .bss (zero-initialized)
                    m_bss_entries.push_back(name + ": resq " + std::to_string(arr_size));
                } else if ((*global)->expr) {
                    if (auto* int_lit = std::get_if<NodeExprIntLit*>(&(*global)->expr->var)) {
                        // Constant initializer: store directly in .data
                        m_data_entries.push_back({name, (*int_lit)->int_lit.value.value()});
                    } else {
                        // Non-constant initializer: allocate zero in .data,
                        // register runtime init
                        m_data_entries.push_back({name, "0"});
                        m_global_inits.push_back({name, (*global)->expr});
                    }
                } else {
                    m_bss_entries.push_back(name);
                }
            } else if (auto* const_stmt = std::get_if<NodeStmtConst*>(&stmt.var)) {
                const auto& name = (*const_stmt)->name.value.value();
                auto val = eval_const_expr((*const_stmt)->expr);
                if (!val.has_value()) {
                    lsp_exit((*const_stmt)->name.loc, "Const initializer is not a compile-time constant expression");
                }
                m_constants[name] = val.value();
            } else if (auto* block_stmt = std::get_if<NodeStmtBlock*>(&stmt.var)) {
                collect_globals((*block_stmt)->block->stmts);
            } else if (auto* if_stmt = std::get_if<NodeStmtIf*>(&stmt.var)) {
                collect_globals((*if_stmt)->then_block->stmts);
                if ((*if_stmt)->else_block) {
                    collect_globals((*if_stmt)->else_block->stmts);
                }
            } else if (auto* while_stmt = std::get_if<NodeStmtWhile*>(&stmt.var)) {
                collect_globals((*while_stmt)->body->stmts);
            } else if (auto* do_while_stmt = std::get_if<NodeStmtDoWhile*>(&stmt.var)) {
                collect_globals((*do_while_stmt)->body->stmts);
            } else if (auto* for_stmt = std::get_if<NodeStmtFor*>(&stmt.var)) {
                collect_globals((*for_stmt)->body->stmts);
            } else if (auto* switch_stmt = std::get_if<NodeStmtSwitch*>(&stmt.var)) {
                for (const auto& c : (*switch_stmt)->cases) {
                    collect_globals(c.stmts);
                }
            }
        }
}

[[nodiscard]] std::string Generator::gen_prog()
{
        // Populate function names for undefined-function check
        for (const auto* func : m_prog.funcs) {
            m_func_names[func->name.value.value()] = true;
        }

        // Populate struct type information
        for (const auto* sd : m_prog.structs) {
            StructInfo info;
            for (size_t i = 0; i < sd->fields.size(); i++) {
                const auto& field_name = sd->fields[i].value.value();
                info.field_names.push_back(field_name);
                info.field_offsets[field_name] = i; // each field is 1 qword
            }
            info.size = sd->fields.size();
            m_struct_types[sd->name.value.value()] = info;
        }

        // Pre-collect global/static declarations from top-level stmts and all
        // function bodies before any code emission. This ensures m_global_inits,
        // m_data_entries, and m_bss_entries are fully populated before the
        // _start init loop runs.
        collect_globals(m_prog.stmts);
        for (const auto& func : m_prog.funcs) {
            collect_globals(func->body->stmts);
        }

                m_backend->extern_sym("_sodium_exit");
        m_backend->extern_sym("_sodium_print_int");
        m_backend->extern_sym("_sodium_read_int");
        m_backend->extern_sym("_sodium_malloc");
        m_backend->extern_sym("_sodium_free");

m_backend->global_sym("_start"); m_backend->label("_start");

        for (const auto& init : m_global_inits) {
            gen_expr(*init.expr);
            pop("rax");
            m_backend->store(m_backend->addr_label(init.name), "rax");
        }

        for (const NodeStmt& stmt : m_prog.stmts) {
            gen_stmt(stmt);
        }

        if (!m_emitted_exit) {
            m_backend->load_imm("rdi", 0);
            m_backend->call("_sodium_exit");
        }

        for (const auto& func : m_prog.funcs) {
            gen_func_def(*func);
        }

        if (!m_data_entries.empty()) {
            m_backend->section(".data");
            for (const auto& entry : m_data_entries) {
                m_backend->dq(entry.name, entry.value);
            }
        }
        if (!m_strings.empty()) {
            m_backend->section(".rodata");
            for (const auto& entry : m_strings) {
                m_backend->db_str(entry.label, entry.value);
            }
        }
        if (!m_bss_entries.empty()) {
            m_backend->section(".bss");
            for (const auto& entry : m_bss_entries) {
                // entry can be "name: resq N" (array) or just "name" (scalar)
                if (entry.find(' ') != std::string::npos) {
                    m_backend->emit_insn("", entry);
                } else {
                    m_backend->resq(entry, 1);
                }
            }
        }

        return m_output.str();
}

std::string Generator::new_label()
{
        return ".L" + std::to_string(m_label_count++);
}

std::string Generator::new_string_label()
{
        return "str" + std::to_string(m_string_count++);
}

bool Generator::is_struct_type(const std::string& name) const {
    return m_struct_types.find(name) != m_struct_types.end();
}

std::optional<StructInfo> Generator::get_struct_info(const std::string& name) const {
    auto it = m_struct_types.find(name);
    if (it != m_struct_types.end()) return it->second;
    return {};
}

void Generator::push(const std::string& reg)
{
        m_backend->push(reg);
        m_stack_size++;
}

void Generator::pop(const std::string& reg)
{
        m_backend->pop(reg);
        m_stack_size--;
}

void Generator::truncate(IntType type)
{
        switch (type) {
            case IntType::i8:
            case IntType::u8:  m_backend->and_("rax", "0xFF"); break;
            case IntType::i16:
            case IntType::u16: m_backend->emit_insn("mov", "ecx, 0xFFFF"); m_backend->and_("rax", "rcx"); break;
            case IntType::i32:
            case IntType::u32: m_backend->mov("eax", "eax"); break;
            case IntType::i64:
            case IntType::u64: break;
        }
}

void Generator::extend(IntType type)
{
        switch (type) {
            case IntType::i8:  m_backend->movsx("rax", "al", 8); break;
            case IntType::i16: m_backend->movsx("rax", "ax", 16); break;
            case IntType::i32: m_backend->movsx("rax", "eax", 32); break;
            case IntType::u8:  m_backend->movzx("eax", "al", 8); break;
            case IntType::u16: m_backend->movzx("eax", "ax", 16); break;
            case IntType::u32: m_backend->mov("eax", "eax"); break;
            case IntType::i64:
            case IntType::u64: break;
        }
}

