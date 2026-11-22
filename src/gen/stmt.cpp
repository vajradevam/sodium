#include "../generation.hpp"

// ==========================================================================
// gen_stmt — statement visitors emit IR
// ==========================================================================

void Generator::gen_stmt(const NodeStmt& stmt)
{
    struct StmtVisitor {
        Generator* gen;
        void operator()(const NodeStmtExit* stmt_exit) const
        {
            gen->gen_expr(*stmt_exit->expr);
            uint32_t code = gen->pop_vreg();
            gen->m_ir.call("_sodium_exit", {IRValue::vreg(code)});
            gen->m_emitted_exit = true;
        }

        void operator()(const NodeStmtLet* stmt_let) const
        {
            auto raw_type = stmt_let->type.value_or(IntType::i64);
            Type var_type = Type::make_int(raw_type);

            // Handle struct-typed variable
            if (!stmt_let->struct_type_name.empty()) {
                auto struct_info = gen->get_struct_info(stmt_let->struct_type_name);
                if (!struct_info.has_value()) {
                    gen->fail(stmt_let->ident.loc, "Unknown struct type: " + stmt_let->struct_type_name);
                }
                auto& scope = gen->m_scopes.back();
                uint32_t struct_bytes = static_cast<uint32_t>(struct_info.value().size);
                uint32_t slot = gen->m_next_frame_offset;
                gen->m_next_frame_offset += struct_bytes;
                scope.vars[stmt_let->ident.value.value()] = Var { .stack_loc = slot, .array_size = struct_info.value().size, .struct_type = stmt_let->struct_type_name, .decl_loc = stmt_let->ident.loc };
                scope.var_count += struct_info.value().size;
                // Zero-initialize the whole struct (including padding).
                zero_bytes(slot, struct_bytes);
                return;
            }

            if (auto arr_lit = std::get_if<NodeExprArrLit*>(&stmt_let->expr->var)) {
                auto& scope = gen->m_scopes.back();
                uint32_t arr_bytes = static_cast<uint32_t>((*arr_lit)->elements.size() * 8);
                uint32_t slot = gen->m_next_frame_offset;
                gen->m_next_frame_offset += arr_bytes;
                scope.vars[stmt_let->ident.value.value()] = Var { .stack_loc = slot, .array_size = (*arr_lit)->elements.size(), .type = var_type, .struct_type = "", .decl_loc = stmt_let->ident.loc };
                scope.var_count += (*arr_lit)->elements.size();
                // Evaluate array literal elements
                gen->gen_expr(*stmt_let->expr);
                // Elements are on vstack in forward order (e0, e1, ..., eN-1)
                // Pop in reverse (eN-1 first), store to slot + (N-1-i)*8
                for (size_t i = 0; i < (*arr_lit)->elements.size(); i++) {
                    uint32_t val = gen->pop_vreg();
                    size_t idx = (*arr_lit)->elements.size() - 1 - i;
                    uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(slot + idx * 8));
                    gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                }
            } else {
                gen->check_narrowing_assign(var_type, stmt_let->expr, stmt_let->ident.loc);
                gen->declare_var(stmt_let->ident.value.value(), var_type, stmt_let->ident.loc);
                gen->gen_expr(*stmt_let->expr);
                // Check if the expression produced a pointer type — if so, update var's type
                if (gen->peek_type().kind == Type::Ptr) {
                    Type expr_type = gen->pop_type();
                    uint32_t val = gen->pop_vreg();
                    // Update variable type to the pointer type
                    auto& scope = gen->m_scopes.back();
                    Var& existing = scope.vars.at(stmt_let->ident.value.value());
                    existing.type = expr_type;
                    uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(scope.vars.at(stmt_let->ident.value.value()).stack_loc));
                    gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                } else {
                    uint32_t val = gen->pop_vreg();
                    // Truncate if needed
                    if (var_type.kind == Type::Int && var_type.int_type != IntType::i64 && var_type.int_type != IntType::u64) {
                        switch (var_type.int_type) {
                            case IntType::i8:
                            case IntType::u8:  val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFF)); break;
                            case IntType::i16:
                            case IntType::u16: val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFFFF)); break;
                            case IntType::i32:
                            case IntType::u32: val = gen->m_ir.zext(IRValue::vreg(val), IRWidth::I32); break;
                            default: break;
                        }
                    }
                    auto& scope = gen->m_scopes.back();
                    const auto& var = scope.vars.at(stmt_let->ident.value.value());
                    uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                    // Use typed store matching the variable's type width
                    if (var.type.kind == Type::Int && var.type.int_type != IntType::i64 && var.type.int_type != IntType::u64) {
                        size_t sz = type_size(var.type);
                        if (sz <= 1)      gen->m_ir.store_8(IRValue::vreg(addr), 0, IRValue::vreg(val));
                        else if (sz <= 2) gen->m_ir.store_16(IRValue::vreg(addr), 0, IRValue::vreg(val));
                        else if (sz <= 4) gen->m_ir.store_32(IRValue::vreg(addr), 0, IRValue::vreg(val));
                        else              gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                    } else {
                        gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                    }
                }
            }
        }

        void operator()(const NodeStmtGlobal* stmt_global) const
        {
            gen->m_globals[stmt_global->name.value.value()] = true;
        }

        void operator()(const NodeStmtConst*) const
        {
            // Already handled by collect_globals; no runtime code needed.
        }

        void operator()(const NodeStmtExpr* stmt_expr) const
        {
            if (auto* call = std::get_if<NodeExprCall*>(&stmt_expr->expr->var)) {
                gen->warn_unused_result(*stmt_expr->expr, (*call)->name.loc);
            }
            gen->gen_expr(*stmt_expr->expr);
            gen->pop_vreg();  // discard result
        }

        void operator()(const NodeStmtAssign* stmt_assign) const
        {
            const auto& name = stmt_assign->ident.value.value();

            // Handle array assignment: arr = [a, b, c]
            for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                if (it->vars.contains(name)) {
                    const auto& var = it->vars.at(name);
                    if (var.array_size > 0 && stmt_assign->op == AssignOp::assign) {
                        if (auto arr_lit = std::get_if<NodeExprArrLit*>(&stmt_assign->expr->var)) {
                            if ((*arr_lit)->elements.size() != var.array_size) {
                                gen->fail(stmt_assign->ident.loc, "Array size mismatch");
                            }
                            for (size_t i = 0; i < var.array_size; i++) {
                                gen->gen_expr(*(*arr_lit)->elements[i]);
                            }
                            for (size_t i = 0; i < var.array_size; i++) {
                                uint32_t val = gen->pop_vreg();
                                size_t idx = var.array_size - 1 - i;
                                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc + idx * 8));
                                gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                            }
                            return;
                        }
                        gen->fail(stmt_assign->ident.loc, "Array assignment requires an array literal");
                    }
                    // Regular assignment (handled by store_var_from_vstack)
                    gen->check_narrowing_assign(var.type, stmt_assign->expr, stmt_assign->ident.loc);
                    gen->gen_expr(*stmt_assign->expr);
                    gen->store_var_from_vstack(name, stmt_assign->op);
                    return;
                }
            }
            // Global
            gen->gen_expr(*stmt_assign->expr);
            gen->store_var_from_vstack(name, stmt_assign->op);
        }

        void operator()(const NodeStmtIf* stmt_if) const
        {
            gen->gen_expr(*stmt_if->cond);
            uint32_t cond = gen->pop_vreg();

            auto label_else = gen->new_label();
            auto label_end = gen->new_label();
            std::string then_label = gen->new_label();
            std::string else_or_end = stmt_if->else_block ? label_else : label_end;

            gen->m_ir.br(IRValue::vreg(cond), then_label, else_or_end);

            gen->m_ir.new_block(then_label);
            gen->enter_scope();
            for (const auto& s : stmt_if->then_block->stmts) {
                gen->gen_stmt(s);
            }
            gen->exit_scope();

            if (stmt_if->else_block) {
                gen->m_ir.jmp(label_end);
                gen->m_ir.new_block(label_else);
                gen->enter_scope();
                for (const auto& s : stmt_if->else_block->stmts) {
                    gen->gen_stmt(s);
                }
                gen->exit_scope();
            }

            gen->m_ir.new_block(label_end);
        }

        void operator()(const NodeStmtWhile* stmt_while) const
        {
            auto label_begin = gen->new_label();
            auto label_end = gen->new_label();

            gen->m_loop_stack.push_back({ .begin_label = label_begin, .end_label = label_end, .continue_label = label_begin });
            gen->m_break_stack.push_back(label_end);

            gen->m_ir.new_block(label_begin);
            gen->gen_expr(*stmt_while->cond);
            uint32_t cond = gen->pop_vreg();

            auto label_body = gen->new_label();
            gen->m_ir.br(IRValue::vreg(cond), label_body, label_end);

            gen->m_ir.new_block(label_body);
            gen->enter_scope();
            for (const auto& s : stmt_while->body->stmts) {
                gen->gen_stmt(s);
            }
            gen->exit_scope();

            gen->m_ir.jmp(label_begin);
            gen->m_ir.new_block(label_end);

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

            gen->m_ir.new_block(label_begin);

            gen->enter_scope();
            for (const auto& s : stmt_do_while->body->stmts) {
                gen->gen_stmt(s);
            }
            gen->exit_scope();

            gen->m_ir.new_block(label_cont);
            gen->gen_expr(*stmt_do_while->cond);
            uint32_t cond = gen->pop_vreg();
            gen->m_ir.br(IRValue::vreg(cond), label_begin, label_end);

            gen->m_ir.new_block(label_end);

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

            uint32_t switch_val = gen->peek_vreg();
            for (size_t i = 0; i < stmt_switch->cases.size(); i++) {
                if (stmt_switch->cases[i].value == nullptr) continue;
                gen->gen_expr(*stmt_switch->cases[i].value);
                uint32_t case_val = gen->pop_vreg();
                uint32_t cmp = gen->m_ir.cmp_eq(IRValue::vreg(switch_val), IRValue::vreg(case_val));
                auto next_label = gen->new_label();
                gen->m_ir.br(IRValue::vreg(cmp), case_labels[i], next_label);
                gen->m_ir.new_block(next_label);
            }

            gen->pop_vreg();

            if (default_idx < stmt_switch->cases.size()) {
                gen->m_ir.jmp(case_labels[default_idx]);
            }
            gen->m_ir.jmp(label_end);

            for (size_t i = 0; i < stmt_switch->cases.size(); i++) {
                gen->m_ir.new_block(case_labels[i]);
                for (const auto& s : stmt_switch->cases[i].stmts) {
                    gen->gen_stmt(s);
                }
            }

            gen->m_ir.new_block(label_end);
            gen->m_break_stack.pop_back();
        }

        void operator()(const NodeStmtArrDecl* stmt_arr) const
        {
            auto const_size = gen->eval_const_expr(stmt_arr->size);
            if (!const_size.has_value()) {
                gen->fail(stmt_arr->loc, "Array size must be a compile-time constant expression");
            }
            size_t size = static_cast<size_t>(const_size.value());
            Type elem_type = stmt_arr->elem_type.value_or(Type::make_int(IntType::i64));
            size_t elem_size = type_size(elem_type);
            auto& scope = gen->m_scopes.back();
            uint32_t slot = gen->m_next_frame_offset;
            uint32_t arr_bytes = static_cast<uint32_t>(size * elem_size);
            gen->m_next_frame_offset += arr_bytes;
            scope.vars[stmt_arr->name.value.value()] = Var { .stack_loc = slot, .array_size = size,
                .type = elem_type, .struct_type = "", .decl_loc = stmt_arr->loc, .elem_size = elem_size };
            scope.var_count += size;
            // Zero-initialize array elements linearly
            zero_bytes(slot, arr_bytes);
        }

        void operator()(const NodeStmtArrAssign* stmt_arr_assign) const
        {
            const auto& name = stmt_arr_assign->name.value.value();
            gen->gen_expr(*stmt_arr_assign->index);
            gen->gen_expr(*stmt_arr_assign->expr);
            uint32_t val = gen->pop_vreg();
            uint32_t idx = gen->pop_vreg();

            for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                if (it->vars.contains(name)) {
                    const auto& var = it->vars.at(name);
                    int64_t elem_bytes = static_cast<int64_t>(var.elem_size);
                    uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                    uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(elem_bytes));
                    uint32_t elem_addr = gen->m_ir.sub(IRValue::vreg(base_addr), IRValue::vreg(offset));
                    if (var.type.kind == Type::Int && elem_bytes < 8) {
                        if (elem_bytes <= 1)      val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFF));
                        else if (elem_bytes <= 2) val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFFFF));
                        else                      val = gen->m_ir.zext(IRValue::vreg(val), IRWidth::I32);
                    }
                    typed_store(elem_addr, val, elem_bytes);
                    return;
                }
            }
            if (gen->m_global_var_info.contains(name)) {
                uint32_t base_addr = gen->m_ir.lea_label(name);
                uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                uint32_t elem_addr = gen->m_ir.add(IRValue::vreg(base_addr), IRValue::vreg(offset));
                gen->m_ir.store(IRValue::vreg(elem_addr), 0, IRValue::vreg(val));
                return;
            }
            std::string suggestion = gen->did_you_mean(name);
            if (!suggestion.empty()) {
                gen->fail_with_suggestion(stmt_arr_assign->name.loc, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)", suggestion);
            } else {
                gen->fail(stmt_arr_assign->name.loc, "Undeclared identifier: " + name);
            }
        }

        void operator()(const NodeStmtReturn* stmt_ret) const
        {
            if (gen->m_in_function) {
                if (stmt_ret->expr) {
                    gen->gen_expr(*stmt_ret->expr);
                    gen->check_return_type(*stmt_ret->expr, stmt_ret->loc);
                    uint32_t val = gen->pop_vreg();
                    gen->m_ir.ret(IRValue::vreg(val));
                } else {
                    gen->m_ir.ret_void();
                }
            } else {
                if (!stmt_ret->expr) {
                    gen->fail(stmt_ret->loc, "return with no value at top level");
                }
                gen->gen_expr(*stmt_ret->expr);
                uint32_t code = gen->pop_vreg();
                gen->m_ir.call("_sodium_exit", {IRValue::vreg(code)});
                gen->m_emitted_exit = true;
            }
        }

        void operator()(const NodeStmtPrint* stmt_print) const
        {
            if (stmt_print->specs.empty() && stmt_print->args.empty()) {
                // print() — just newline
                gen->m_ir.call("_sodium_print_nl", {});
                return;
            }

            size_t arg_idx = 0;
            for (auto& spec : stmt_print->specs) {
                switch (spec.type) {
                    case FormatSpec::Text: {
                        // Emit a string literal for this text fragment
                        auto label = gen->new_string_label();
                        gen->m_strings.push_back({label, spec.text});
                        uint32_t addr = gen->m_ir.lea_label(label);
                        gen->m_ir.call("_sodium_print_raw_str", {IRValue::vreg(addr)});
                        break;
                    }
                    case FormatSpec::Int: {
                        gen->gen_expr(*stmt_print->args[arg_idx++]);
                        uint32_t val = gen->pop_vreg();
                        gen->m_ir.call("_sodium_print_raw_int", {IRValue::vreg(val)});
                        break;
                    }
                    case FormatSpec::String: {
                        gen->gen_expr(*stmt_print->args[arg_idx++]);
                        uint32_t ptr = gen->pop_vreg();
                        gen->m_ir.call("_sodium_print_raw_str", {IRValue::vreg(ptr)});
                        break;
                    }
                    case FormatSpec::LiteralPercent: {
                        // Emit a single '%'
                        auto label = gen->new_string_label();
                        gen->m_strings.push_back({label, "%"});
                        uint32_t addr = gen->m_ir.lea_label(label);
                        gen->m_ir.call("_sodium_print_raw_str", {IRValue::vreg(addr)});
                        break;
                    }
                }
            }
            // Trailing newline
            gen->m_ir.call("_sodium_print_nl", {});
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

            gen->m_ir.new_block(label_begin);

            if (stmt_for->cond) {
                gen->gen_expr(*stmt_for->cond);
                uint32_t cond = gen->pop_vreg();
                auto label_body = gen->new_label();
                gen->m_ir.br(IRValue::vreg(cond), label_body, label_end);
                gen->m_ir.new_block(label_body);
            }

            gen->enter_scope();
            for (const auto& s : stmt_for->body->stmts) {
                gen->gen_stmt(s);
            }
            gen->exit_scope();

            gen->m_ir.new_block(label_cont);
            if (stmt_for->update) {
                NodeStmt update_stmt;
                update_stmt.var = stmt_for->update;
                gen->gen_stmt(update_stmt);
            }

            gen->m_ir.jmp(label_begin);
            gen->m_ir.new_block(label_end);

            gen->m_loop_stack.pop_back();
            gen->m_break_stack.pop_back();

            gen->exit_scope();
        }

        void operator()(const NodeStmtBreak* stmt_break) const
        {
            if (gen->m_break_stack.empty()) {
                gen->fail(stmt_break->loc, "break outside loop or switch");
            }
            gen->m_ir.jmp(gen->m_break_stack.back());
        }

        void operator()(const NodeStmtContinue* stmt_continue) const
        {
            for (auto it = gen->m_loop_stack.rbegin(); it != gen->m_loop_stack.rend(); ++it) {
                if (!it->continue_label.empty()) {
                    gen->m_ir.jmp(it->continue_label);
                    return;
                }
            }
            gen->fail(stmt_continue->loc, "continue outside loop");
        }

        void operator()(const NodeStmtFieldAssign* stmt_field) const
        {
            const auto& obj_name = stmt_field->obj_name.value.value();
            const auto& field_name = stmt_field->field_name.value.value();
            auto var = gen->lookup_var(obj_name, stmt_field->obj_name.loc);
            if (var.struct_type.empty()) {
                gen->fail(stmt_field->obj_name.loc, "Not a struct variable: " + obj_name);
            }
            auto struct_info = gen->get_struct_info(var.struct_type);
            if (!struct_info.has_value()) {
                gen->fail(stmt_field->field_name.loc, "Unknown struct type: " + var.struct_type);
            }
            auto offset_it = struct_info.value().field_offsets.find(field_name);
            if (offset_it == struct_info.value().field_offsets.end()) {
                gen->fail(stmt_field->field_name.loc, "Unknown field '" + field_name + "' in struct '" + var.struct_type + "'");
            }
            size_t field_offset = offset_it->second;
            size_t field_index = 0;
            for (; field_index < struct_info.value().field_names.size(); field_index++) {
                if (struct_info.value().field_names[field_index] == field_name) break;
            }
            Type field_type = field_index < struct_info.value().field_types.size()
                                  ? struct_info.value().field_types[field_index]
                                  : Type::make_int(IntType::i64);
            size_t field_size = type_size(field_type);
            bool field_signed = (field_type.kind == Type::Int &&
                                 (field_type.int_type == IntType::i8 ||
                                  field_type.int_type == IntType::i16 ||
                                  field_type.int_type == IntType::i32));
            uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
            uint32_t field_addr = gen->m_ir.lea(IRValue::vreg(base_addr), -static_cast<int64_t>(field_offset));

            if (stmt_field->op == AssignOp::assign) {
                gen->gen_expr(*stmt_field->expr);
                uint32_t val = gen->pop_vreg();
                if (field_type.kind == Type::Int && field_size < 8) {
                    if (field_size <= 1)      val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFF));
                    else if (field_size <= 2) val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFFFF));
                    else                      val = gen->m_ir.zext(IRValue::vreg(val), IRWidth::I32);
                }
                typed_store(field_addr, val, field_size);
            } else {
                uint32_t old_val = typed_load_val(field_addr, field_size, field_signed);
                gen->gen_expr(*stmt_field->expr);
                uint32_t rhs = gen->pop_vreg();
                uint32_t result = old_val;
                switch (stmt_field->op) {
                    case AssignOp::add_assign:     result = gen->m_ir.add(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::sub_assign:     result = gen->m_ir.sub(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::mul_assign:     result = gen->m_ir.mul(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::div_assign:     result = gen->m_ir.div(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::mod_assign:     result = gen->m_ir.mod(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::bitand_assign:  result = gen->m_ir.and_(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::bitor_assign:   result = gen->m_ir.or_(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::bitxor_assign:  result = gen->m_ir.xor_(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::shl_assign:     result = gen->m_ir.shl(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::shr_assign:     result = gen->m_ir.ashr(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    default: break;
                }
                if (field_type.kind == Type::Int && field_size < 8) {
                    if (field_size <= 1)      result = gen->m_ir.and_(IRValue::vreg(result), IRValue::imm_i64(0xFF));
                    else if (field_size <= 2) result = gen->m_ir.and_(IRValue::vreg(result), IRValue::imm_i64(0xFFFF));
                    else                      result = gen->m_ir.zext(IRValue::vreg(result), IRWidth::I32);
                }
                typed_store(field_addr, result, field_size);
            }
        }

        // Type-aware store/load helpers
        void typed_store(uint32_t ptr, uint32_t val, size_t elem_size) const {
            if (elem_size >= 8)      gen->m_ir.store(IRValue::vreg(ptr), 0, IRValue::vreg(val));
            else if (elem_size >= 4) gen->m_ir.store_32(IRValue::vreg(ptr), 0, IRValue::vreg(val));
            else if (elem_size >= 2) gen->m_ir.store_16(IRValue::vreg(ptr), 0, IRValue::vreg(val));
            else                     gen->m_ir.store_8(IRValue::vreg(ptr), 0, IRValue::vreg(val));
        }
        void zero_bytes(uint32_t base_slot, size_t bytes) const {
            uint32_t zero = gen->m_ir.load_i64(0);
            size_t pos = 0;
            while (bytes - pos >= 8) {
                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(base_slot + pos));
                gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(zero));
                pos += 8;
            }
            if (bytes - pos >= 4) {
                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(base_slot + pos));
                gen->m_ir.store_32(IRValue::vreg(addr), 0, IRValue::vreg(zero));
                pos += 4;
            }
            if (bytes - pos >= 2) {
                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(base_slot + pos));
                gen->m_ir.store_16(IRValue::vreg(addr), 0, IRValue::vreg(zero));
                pos += 2;
            }
            if (bytes - pos >= 1) {
                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(base_slot + pos));
                gen->m_ir.store_8(IRValue::vreg(addr), 0, IRValue::vreg(zero));
            }
        }

        uint32_t typed_load_val(uint32_t ptr, size_t elem_size, bool is_signed) const {
            if (elem_size >= 8)      return gen->m_ir.load(IRValue::vreg(ptr), 0);
            else if (elem_size == 4) return is_signed ? gen->m_ir.load_s32(IRValue::vreg(ptr), 0) : gen->m_ir.load_u32(IRValue::vreg(ptr), 0);
            else if (elem_size == 2) return is_signed ? gen->m_ir.load_s16(IRValue::vreg(ptr), 0) : gen->m_ir.load_u16(IRValue::vreg(ptr), 0);
            else                     return is_signed ? gen->m_ir.load_s8(IRValue::vreg(ptr), 0) : gen->m_ir.load_u8(IRValue::vreg(ptr), 0);
        }

        void operator()(const NodeStmtDerefAssign* stmt_deref) const
        {
            if (stmt_deref->op == AssignOp::assign) {
                gen->gen_expr(*stmt_deref->expr);
                gen->gen_expr(*stmt_deref->ptr_expr);
                Type ptr_type = gen->peek_type();
                uint32_t ptr = gen->pop_vreg();
                uint32_t val = gen->pop_vreg();
                if (ptr_type.kind == Type::Ptr && ptr_type.pointee) {
                    typed_store(ptr, val, type_size(*ptr_type.pointee));
                } else {
                    gen->m_ir.store(IRValue::vreg(ptr), 0, IRValue::vreg(val));
                }
            } else {
                gen->gen_expr(*stmt_deref->ptr_expr);
                Type ptr_type = gen->peek_type();
                uint32_t ptr = gen->pop_vreg();

                size_t elem_size = 8;
                bool is_signed = false;
                if (ptr_type.kind == Type::Ptr && ptr_type.pointee) {
                    elem_size = type_size(*ptr_type.pointee);
                    is_signed = (ptr_type.pointee->kind == Type::Int &&
                                 (ptr_type.pointee->int_type == IntType::i8 ||
                                  ptr_type.pointee->int_type == IntType::i16 ||
                                  ptr_type.pointee->int_type == IntType::i32));
                }

                uint32_t old_val = typed_load_val(ptr, elem_size, is_signed);
                gen->gen_expr(*stmt_deref->expr);
                uint32_t rhs = gen->pop_vreg();
                uint32_t result = old_val;
                switch (stmt_deref->op) {
                    case AssignOp::add_assign:     result = gen->m_ir.add(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::sub_assign:     result = gen->m_ir.sub(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::mul_assign:     result = gen->m_ir.mul(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::div_assign:     result = gen->m_ir.div(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::mod_assign:     result = gen->m_ir.mod(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::bitand_assign:  result = gen->m_ir.and_(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::bitor_assign:   result = gen->m_ir.or_(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::bitxor_assign:  result = gen->m_ir.xor_(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::shl_assign:     result = gen->m_ir.shl(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    case AssignOp::shr_assign:     result = gen->m_ir.ashr(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    default: break;
                }
                typed_store(ptr, result, elem_size);
            }
        }
    };

    StmtVisitor visitor { .gen = this };
    std::visit(visitor, stmt.var);
}
