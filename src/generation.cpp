#include "generation.hpp"
#include "backend/x86_64/backend.hpp"

Generator::Generator(NodeProg root, Backend& backend, const TargetRegisterInfo& tri,
                     std::unique_ptr<Backend> owned_backend)
    : m_prog(std::move(root))
    , m_backend_ptr(&backend)
    , m_owned_backend(std::move(owned_backend))
    , m_tri_ptr(&tri)
{
    m_scopes.push_back({0, {}});
}

// ==========================================================================
// IR virtual register stack helpers
// ==========================================================================

uint32_t Generator::push_vreg(uint32_t vreg) {
    m_vstack.push_back(vreg);
    return vreg;
}

uint32_t Generator::pop_vreg() {
    if (m_vstack.empty()) {
        // Allocate a fresh vreg for convenience
        uint32_t fresh = m_ir.current_function()->new_vreg();
        return fresh;
    }
    uint32_t vreg = m_vstack.back();
    m_vstack.pop_back();
    return vreg;
}

uint32_t Generator::peek_vreg() {
    if (m_vstack.empty()) return UINT32_MAX;
    return m_vstack.back();
}

// ==========================================================================
// Variable load/store helpers
// ==========================================================================

void Generator::load_var_to_vstack(const std::string& name) {
    // Try local scope first
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->vars.contains(name)) {
            const auto& var = it->vars.at(name);
            if (var.array_size > 0 || var.type == IntType::i64 || var.type == IntType::u64) {
                uint32_t addr = m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                uint32_t val = m_ir.load(IRValue::vreg(addr), 0);
                push_vreg(val);
                return;
            } else {
                uint32_t addr = m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                uint32_t val = m_ir.load(IRValue::vreg(addr), 0);
                // Extend the loaded value
                switch (var.type) {
                    case IntType::i8:  val = m_ir.sext(IRValue::vreg(val), IRWidth::I8); break;
                    case IntType::i16: val = m_ir.sext(IRValue::vreg(val), IRWidth::I16); break;
                    case IntType::i32: val = m_ir.sext(IRValue::vreg(val), IRWidth::I32); break;
                    case IntType::u8:  val = m_ir.zext(IRValue::vreg(val), IRWidth::I8); break;
                    case IntType::u16: val = m_ir.zext(IRValue::vreg(val), IRWidth::I16); break;
                    case IntType::u32: val = m_ir.zext(IRValue::vreg(val), IRWidth::I32); break;
                    default: break;
                }
                push_vreg(val);
                return;
            }
        }
    }
    // Try global
    if (m_globals.contains(name)) {
        uint32_t val = m_ir.lea_label(name);
        uint32_t loaded = m_ir.load(IRValue::vreg(val), 0);
        push_vreg(loaded);
        return;
    }
    // Try constant
    if (m_constants.contains(name)) {
        uint32_t val = m_ir.load_i64(m_constants.at(name));
        push_vreg(val);
        return;
    }
    lsp_exit({}, "Undeclared identifier: " + name);
}

void Generator::store_var_from_vstack(const std::string& name, AssignOp op) {
    // Compound assignment: we need to read old value, apply op, store back.
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->vars.contains(name)) {
            const auto& var = it->vars.at(name);
            if (var.array_size > 0) {
                lsp_exit({}, "Cannot assign to array variable directly (use arr = [a,b,c] or arr[i] = val)");
            }

            if (op == AssignOp::assign) {
                uint32_t val = pop_vreg();
                if (var.type != IntType::i64 && var.type != IntType::u64) {
                    // Truncate
                    switch (var.type) {
                        case IntType::i8:
                        case IntType::u8:  val = m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFF)); break;
                        case IntType::i16:
                        case IntType::u16: val = m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFFFF)); break;
                        case IntType::i32:
                        case IntType::u32: val = m_ir.zext(IRValue::vreg(val), IRWidth::I32); break;
                        default: break;
                    }
                }
                uint32_t addr = m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
            } else {
                // Compound assignment: read old, apply op, write back
                load_var_to_vstack(name);
                // vstack after load: [..., RHS, LHS] (RHS from gen_expr, LHS from load_var)
                uint32_t lhs = pop_vreg();  // LHS is on top
                uint32_t rhs = pop_vreg();  // RHS is below
                uint32_t result = lhs;
                switch (op) {
                    case AssignOp::add_assign:     result = m_ir.add(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::sub_assign:     result = m_ir.sub(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::mul_assign:     result = m_ir.mul(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::div_assign:     result = m_ir.div(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::mod_assign:     result = m_ir.mod(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::bitand_assign:  result = m_ir.and_(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::bitor_assign:   result = m_ir.or_(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::bitxor_assign:  result = m_ir.xor_(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::shl_assign:     result = m_ir.shl(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    case AssignOp::shr_assign:     result = m_ir.shr(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    default: break;
                }
                // Truncate if needed
                if (var.type != IntType::i64 && var.type != IntType::u64) {
                    switch (var.type) {
                        case IntType::i8:
                        case IntType::u8:  result = m_ir.and_(IRValue::vreg(result), IRValue::imm_i64(0xFF)); break;
                        case IntType::i16:
                        case IntType::u16: result = m_ir.and_(IRValue::vreg(result), IRValue::imm_i64(0xFFFF)); break;
                        case IntType::i32:
                        case IntType::u32: result = m_ir.zext(IRValue::vreg(result), IRWidth::I32); break;
                        default: break;
                    }
                }
                uint32_t addr = m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(result));
            }
            return;
        }
    }
    // Global variable
    if (m_globals.contains(name)) {
        if (op == AssignOp::assign) {
            uint32_t val = pop_vreg();
            m_ir.store(IRValue::vreg(m_ir.lea_label(name)), 0, IRValue::vreg(val));
        } else {
            load_var_to_vstack(name);
            // vstack after load: [..., RHS, LHS] (RHS from gen_expr, LHS from load_var)
            uint32_t lhs = pop_vreg();  // LHS (old value) is on top
            uint32_t rhs = pop_vreg();  // RHS (expr value) is below
            uint32_t result = lhs;
            switch (op) {
                case AssignOp::add_assign:     result = m_ir.add(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::sub_assign:     result = m_ir.sub(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::mul_assign:     result = m_ir.mul(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::div_assign:     result = m_ir.div(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::mod_assign:     result = m_ir.mod(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::bitand_assign:  result = m_ir.and_(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::bitor_assign:   result = m_ir.or_(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::bitxor_assign:  result = m_ir.xor_(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::shl_assign:     result = m_ir.shl(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                case AssignOp::shr_assign:     result = m_ir.shr(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                default: break;
            }
            m_ir.store(IRValue::vreg(m_ir.lea_label(name)), 0, IRValue::vreg(result));
        }
        return;
    }
    if (m_constants.contains(name)) {
        lsp_exit({}, "Cannot assign to constant '" + name + "'");
    }
    lsp_exit({}, "Undeclared identifier: " + name);
}

// ==========================================================================
// IR function finalization
// ==========================================================================

void Generator::flush_function() {
    IRFunction* func = m_ir.current_function();
    if (!func) return;

    // Set stack slot count
    func->stack_slots = m_next_frame_slot;

    // Run the pipeline: liveness → allocator → rewriter → emitter
    // We save the function name and blocks before destroying via end_function
    std::string func_name = func->name;
    auto blocks = func->blocks; // copy blocks
    uint32_t slot_count = func->stack_slots;

    // Dump IR before allocation if requested
    if (m_emit_ir) {
        std::cerr << "; --- IR for " << func_name << " ---\n";
        std::cerr << func->to_string();
        std::cerr << "; --- end IR ---\n";
    }

    auto allocation = [&]() {
        LinearScanAllocator alloc(*m_tri_ptr);
        return alloc.allocate(*func);
    }();

    IRRewriter rewriter(*m_tri_ptr, allocation, *func);
    rewriter.rewrite();

    // Emit to backend
    IREmitter emitter(*m_backend_ptr, *m_tri_ptr, allocation);
    emitter.emit_function(*func);

    // Reset IR state for next function
    m_ir.end_function();  // destroys the function
    m_vstack.clear();
    m_next_frame_slot = 0;
    m_frame_slots = 0;
}

// ==========================================================================
// gen_func_def
// ==========================================================================

void Generator::gen_func_def(const NodeFuncDef& func)
{
    size_t saved_stack_size = m_stack_size;

    // Start a new IR function
    m_ir.start_function(func.name.value.value());
    m_next_frame_slot = 0;

    m_backend_ptr->extern_sym(func.name.value.value());
    // The entry block is already created by start_function();
    // the emitter will output the label and prologue

    enter_scope();
    m_in_function = true;
    m_func_epilogue_label = ".L" + func.name.value.value() + "_epilogue";

    // Process parameters: allocate frame slots and load from stack
    for (size_t i = 0; i < func.params.size(); i++) {
        std::string param_name = func.params[i].value.value();
        auto& scope = m_scopes.back();

        uint32_t slot = m_next_frame_slot++;
        scope.vars[param_name] = Var { .stack_loc = slot };
        scope.var_count++;

        // Load parameter from ABI-defined location (argument register or stack)
        // using the target-agnostic LOAD_PARAM opcode.
        uint32_t param_val = m_ir.load_param(static_cast<int64_t>(i));
        uint32_t slot_addr = m_ir.frame_addr(static_cast<int64_t>(slot));
        m_ir.store(IRValue::vreg(slot_addr), 0, IRValue::vreg(param_val));
    }

    for (const auto& s : func.body->stmts) {
        gen_stmt(s);
    }

    // Add epilogue label and return
    m_ir.new_block(m_func_epilogue_label);
    m_ir.ret_void();
    m_in_function = false;
    m_func_epilogue_label.clear();

    m_scopes.pop_back();
    m_stack_size = saved_stack_size;

    // Finalize this function: run allocator and emit to backend
    flush_function();
}

// ==========================================================================
// enter_scope / exit_scope
// ==========================================================================

void Generator::enter_scope()
{
    m_scopes.push_back({0, {}});
}

void Generator::exit_scope()
{
    auto& scope = m_scopes.back();
    m_stack_size -= scope.var_count;
    if (scope.var_count > 0) {
        // No need to adjust runtime stack — frame slots are managed by the allocator
    }
    m_scopes.pop_back();
}

void Generator::declare_var(const std::string& name, IntType type, SourceLoc loc) {
    auto& scope = m_scopes.back();
    if (scope.vars.contains(name)) {
        lsp_exit(loc, "Identifier already used in this scope: " + name);
    }
    uint32_t slot = m_next_frame_slot++;
    scope.vars[name] = Var { .stack_loc = slot, .type = type };
    scope.var_count++;
    m_stack_size++;
}

Var Generator::lookup_var(const std::string& name, SourceLoc loc) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->vars.contains(name)) {
            return it->vars.at(name);
        }
    }
    lsp_exit(loc, "Undeclared identifier: " + name);
}

// ==========================================================================
// gen_expr — expression visitors emit IR
// ==========================================================================

void Generator::gen_expr(const NodeExpr& expr)
{
    struct ExprVisitor {
        Generator* gen;

        void operator()(const NodeExprIntLit* expr_int_lit)
        {
            int64_t val = std::stoll(expr_int_lit->int_lit.value.value());
            uint32_t v = gen->m_ir.load_i64(val);
            gen->push_vreg(v);
        }

        void operator()(const NodeExprIdent* expr_ident)
        {
            gen->load_var_to_vstack(expr_ident->ident.value.value());
        }

        void operator()(const NodeExprStringLit* expr_str)
        {
            auto str = expr_str->value.value.value();
            auto label = gen->new_string_label();
            gen->m_strings.push_back({label, str});
            uint32_t v = gen->m_ir.lea_label(label);
            gen->push_vreg(v);
        }

        void operator()(const NodeExprIndex* expr_index)
        {
            const auto& name = expr_index->name.value.value();
            // Evaluate index
            gen->gen_expr(*expr_index->index);
            uint32_t idx = gen->pop_vreg();

            // Try local scope first
            for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                if (it->vars.contains(name)) {
                    const auto& var = it->vars.at(name);
                    uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                    uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                    uint32_t elem_addr = gen->m_ir.sub(IRValue::vreg(base_addr), IRValue::vreg(offset));
                    uint32_t val = gen->m_ir.load(IRValue::vreg(elem_addr), 0);
                    gen->push_vreg(val);
                    return;
                }
            }
            // Try global
            if (gen->m_global_var_info.contains(name)) {
                const auto& var = gen->m_global_var_info.at(name);
                uint32_t base_addr = gen->m_ir.lea_label(name);
                uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                uint32_t elem_addr = gen->m_ir.add(IRValue::vreg(base_addr), IRValue::vreg(offset));
                uint32_t val = gen->m_ir.load(IRValue::vreg(elem_addr), 0);
                gen->push_vreg(val);
                return;
            }
            lsp_exit(expr_index->name.loc, "Undeclared identifier: " + name);
        }

        void operator()(const NodeExprBitNot* bit_not)
        {
            gen->gen_expr(*bit_not->expr);
            uint32_t src = gen->pop_vreg();
            uint32_t v = gen->m_ir.not_(IRValue::vreg(src));
            gen->push_vreg(v);
        }

        void operator()(const NodeExprLogNot* log_not)
        {
            gen->gen_expr(*log_not->expr);
            uint32_t src = gen->pop_vreg();
            uint32_t v = gen->m_ir.cmp_eq(IRValue::vreg(src), IRValue::imm_i64(0));
            gen->push_vreg(v);
        }

        void operator()(const NodeExprTernary* ternary)
        {
            gen->gen_expr(*ternary->cond);
            uint32_t cond = gen->pop_vreg();

            auto label_else = gen->new_label();
            auto label_end = gen->new_label();
            auto label_then = gen->new_label();

            // br(cond, true_label, false_label): if cond != 0 → true_label
            gen->m_ir.br(IRValue::vreg(cond), label_then, label_else);

            // Then branch
            gen->m_ir.new_block(label_then);
            gen->gen_expr(*ternary->then_expr);
            // Result is on vstack; jump to end
            gen->m_ir.jmp(label_end);

            // Else branch
            gen->m_ir.new_block(label_else);
            gen->gen_expr(*ternary->else_expr);

            // End
            gen->m_ir.new_block(label_end);
        }

        void operator()(const NodeExprRead*) const
        {
            uint32_t v = gen->m_ir.call("_sodium_read_int", {});
            gen->push_vreg(v);
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
            // struct fields are stored after the base variable: slot - field_offset
            // (field 0 is at the same address as the struct base, field 1 is at base-8, etc.)
            uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
            uint32_t field_addr = gen->m_ir.lea(IRValue::vreg(base_addr), -static_cast<int64_t>(field_offset * 8));
            // Actually the fields go from base downward. Let me use: base - field_offset * 8
            // Actually lea with negative offset from base_addr
            // Wait, LEA computes base_addr + offset. We want base_addr - field_offset*8
            uint32_t val = gen->m_ir.load(IRValue::vreg(field_addr), 0);
            gen->push_vreg(val);
        }

        void operator()(const NodeExprAddrOf* addr_of) const
        {
            // &var — push the address of the variable
            auto* inner = addr_of->expr;
            if (auto* ident = std::get_if<NodeExprIdent*>(&inner->var)) {
                const auto& name = (*ident)->ident.value.value();
                for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                    if (it->vars.contains(name)) {
                        const auto& var = it->vars.at(name);
                        uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                        gen->push_vreg(addr);
                        return;
                    }
                }
                if (gen->m_globals.contains(name)) {
                    uint32_t addr = gen->m_ir.lea_label(name);
                    gen->push_vreg(addr);
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
                uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                uint32_t field_addr = gen->m_ir.lea(IRValue::vreg(base_addr), -static_cast<int64_t>(field_offset * 8));
                gen->push_vreg(field_addr);
            } else {
                lsp_exit(addr_of->ampersand.loc, "Cannot take address of this expression");
            }
        }

        void operator()(const NodeExprDeref* deref) const
        {
            // *ptr — evaluate ptr, then load from that address
            gen->gen_expr(*deref->expr);
            uint32_t ptr = gen->pop_vreg();
            uint32_t val = gen->m_ir.load(IRValue::vreg(ptr), 0);
            gen->push_vreg(val);
        }

        void operator()(const NodeExprCall* expr_call)
        {
            const auto& fname = expr_call->name.value.value();

            if (fname == "malloc") {
                // Call _sodium_malloc with the size argument
                for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                    gen->gen_expr(**it);
                }
                uint32_t size = gen->pop_vreg();
                uint32_t result = gen->m_ir.call("_sodium_malloc", {IRValue::vreg(size)});
                gen->push_vreg(result);
                return;
            }
            if (fname == "free") {
                for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                    gen->gen_expr(**it);
                }
                uint32_t ptr = gen->pop_vreg();
                gen->m_ir.call("_sodium_free", {IRValue::vreg(ptr)});
                return;
            }
            if (fname == "argc") {
                // argc() — return number of command-line arguments
                if (!expr_call->args.empty()) {
                    lsp_exit(expr_call->name.loc, "argc() takes no arguments");
                }
                uint32_t addr = gen->m_ir.lea_label("__sodium_argc");
                uint32_t val = gen->m_ir.load(IRValue::vreg(addr), 0);
                gen->push_vreg(val);
                return;
            }
            if (fname == "argv") {
                // argv(i) — return pointer to the i-th argument string
                if (expr_call->args.size() != 1) {
                    lsp_exit(expr_call->name.loc, "argv() takes exactly one argument (the index)");
                }
                gen->gen_expr(*expr_call->args[0]);
                uint32_t idx = gen->pop_vreg();
                uint32_t addr = gen->m_ir.lea_label("__sodium_argv");
                uint32_t argv_ptr = gen->m_ir.load(IRValue::vreg(addr), 0);
                uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                uint32_t entry_addr = gen->m_ir.add(IRValue::vreg(argv_ptr), IRValue::vreg(offset));
                uint32_t str_ptr = gen->m_ir.load(IRValue::vreg(entry_addr), 0);
                gen->push_vreg(str_ptr);
                return;
            }
            if (fname == "print_str") {
                // print_str(s) — print a null-terminated string
                if (expr_call->args.size() != 1) {
                    lsp_exit(expr_call->name.loc, "print_str() takes exactly one argument");
                }
                gen->gen_expr(*expr_call->args[0]);
                uint32_t ptr = gen->pop_vreg();
                gen->m_ir.call("_sodium_print_str", {IRValue::vreg(ptr)});
                return;
            }

            if (!gen->m_func_names.contains(fname)) {
                lsp_exit(expr_call->name.loc, "Undefined function: " + fname);
            }

            // Collect arguments (in evaluation order)
            std::vector<IRValue> args;
            for (size_t i = 0; i < expr_call->args.size(); i++) {
                gen->gen_expr(*expr_call->args[i]);
            }
            // Pop in reverse to get evaluation order
            for (size_t i = 0; i < expr_call->args.size(); i++) {
                uint32_t arg = gen->pop_vreg();
                args.insert(args.begin(), IRValue::vreg(arg));
            }

            uint32_t result = gen->m_ir.call(fname, args);
            gen->push_vreg(result);
        }

        void operator()(const BinExpr* bin_expr)
        {
            struct BinVisitor {
                Generator* gen;
                void operator()(const BinExprAdd* add) {
                    gen->gen_expr(*add->lhs);
                    gen->gen_expr(*add->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.add(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprMulti* mul) {
                    gen->gen_expr(*mul->lhs);
                    gen->gen_expr(*mul->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.mul(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprSub* sub) {
                    gen->gen_expr(*sub->lhs);
                    gen->gen_expr(*sub->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.sub(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprDiv* div) {
                    gen->gen_expr(*div->lhs);
                    gen->gen_expr(*div->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.div(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprMod* mod) {
                    gen->gen_expr(*mod->lhs);
                    gen->gen_expr(*mod->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.mod(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprLT* lt) {
                    gen->gen_expr(*lt->lhs);
                    gen->gen_expr(*lt->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_lt(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprGT* gt) {
                    gen->gen_expr(*gt->lhs);
                    gen->gen_expr(*gt->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_gt(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprEQ* eq) {
                    gen->gen_expr(*eq->lhs);
                    gen->gen_expr(*eq->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_eq(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprNEQ* neq) {
                    gen->gen_expr(*neq->lhs);
                    gen->gen_expr(*neq->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_ne(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprLTE* lte) {
                    gen->gen_expr(*lte->lhs);
                    gen->gen_expr(*lte->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_le(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprGTE* gte) {
                    gen->gen_expr(*gte->lhs);
                    gen->gen_expr(*gte->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_ge(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprAnd* and_op) {
                    gen->gen_expr(*and_op->lhs);
                    gen->gen_expr(*and_op->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t lhs_nz = gen->m_ir.cmp_ne(IRValue::vreg(a), IRValue::imm_i64(0));
                    uint32_t rhs_nz = gen->m_ir.cmp_ne(IRValue::vreg(b), IRValue::imm_i64(0));
                    uint32_t r = gen->m_ir.and_(IRValue::vreg(lhs_nz), IRValue::vreg(rhs_nz));
                    gen->push_vreg(r);
                }
                void operator()(const BinExprOr* or_op) {
                    gen->gen_expr(*or_op->lhs);
                    gen->gen_expr(*or_op->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t lhs_nz = gen->m_ir.cmp_ne(IRValue::vreg(a), IRValue::imm_i64(0));
                    uint32_t rhs_nz = gen->m_ir.cmp_ne(IRValue::vreg(b), IRValue::imm_i64(0));
                    uint32_t r = gen->m_ir.or_(IRValue::vreg(lhs_nz), IRValue::vreg(rhs_nz));
                    gen->push_vreg(r);
                }
                void operator()(const BinExprBitAnd* and_op) {
                    gen->gen_expr(*and_op->lhs);
                    gen->gen_expr(*and_op->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.and_(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprBitOr* or_op) {
                    gen->gen_expr(*or_op->lhs);
                    gen->gen_expr(*or_op->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.or_(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprXor* xor_op) {
                    gen->gen_expr(*xor_op->lhs);
                    gen->gen_expr(*xor_op->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.xor_(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprShl* shl_op) {
                    gen->gen_expr(*shl_op->lhs);
                    gen->gen_expr(*shl_op->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.shl(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprShr* shr_op) {
                    gen->gen_expr(*shr_op->lhs);
                    gen->gen_expr(*shr_op->rhs);
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.shr(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
            };
            BinVisitor visitor{ .gen = gen };
            std::visit(visitor, bin_expr->var);
        }
    };

    ExprVisitor visitor{ .gen = this };
    std::visit(visitor, expr.var);
}

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
            auto var_type = stmt_let->type.value_or(IntType::i64);

            // Handle struct-typed variable
            if (!stmt_let->struct_type_name.empty()) {
                auto struct_info = gen->get_struct_info(stmt_let->struct_type_name);
                if (!struct_info.has_value()) {
                    lsp_exit(stmt_let->ident.loc, "Unknown struct type: " + stmt_let->struct_type_name);
                }
                auto& scope = gen->m_scopes.back();
                uint32_t slot = gen->m_next_frame_slot;
                gen->m_next_frame_slot += struct_info.value().size;
                scope.vars[stmt_let->ident.value.value()] = Var { .stack_loc = slot, .array_size = struct_info.value().size, .struct_type = stmt_let->struct_type_name };
                scope.var_count += struct_info.value().size;
                // Zero-initialize all fields
                for (size_t i = 0; i < struct_info.value().size; i++) {
                    uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(slot + i));
                    gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::imm_i64(0));
                }
                return;
            }

            if (auto arr_lit = std::get_if<NodeExprArrLit*>(&stmt_let->expr->var)) {
                auto& scope = gen->m_scopes.back();
                uint32_t slot = gen->m_next_frame_slot;
                gen->m_next_frame_slot += (*arr_lit)->elements.size();
                scope.vars[stmt_let->ident.value.value()] = Var { .stack_loc = slot, .array_size = (*arr_lit)->elements.size(), .type = var_type };
                scope.var_count += (*arr_lit)->elements.size();
                // Evaluate array literal elements
                gen->gen_expr(*stmt_let->expr);
                // Elements are on vstack in forward order (e0, e1, ..., eN-1)
                // Pop in reverse (eN-1 first), store to slot + (N-1-i)
                for (size_t i = 0; i < (*arr_lit)->elements.size(); i++) {
                    uint32_t val = gen->pop_vreg();
                    size_t idx = (*arr_lit)->elements.size() - 1 - i;
                    uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(slot + idx));
                    gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                }
            } else {
                gen->declare_var(stmt_let->ident.value.value(), var_type, stmt_let->ident.loc);
                gen->gen_expr(*stmt_let->expr);
                uint32_t val = gen->pop_vreg();
                // Truncate if needed
                if (var_type != IntType::i64 && var_type != IntType::u64) {
                    switch (var_type) {
                        case IntType::i8:
                        case IntType::u8:  val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFF)); break;
                        case IntType::i16:
                        case IntType::u16: val = gen->m_ir.and_(IRValue::vreg(val), IRValue::imm_i64(0xFFFF)); break;
                        case IntType::i32:
                        case IntType::u32: val = gen->m_ir.zext(IRValue::vreg(val), IRWidth::I32); break;
                        default: break;
                    }
                }
                // Store
                auto& scope = gen->m_scopes.back();
                const auto& var = scope.vars.at(stmt_let->ident.value.value());
                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
            }
        }

        void operator()(const NodeStmtGlobal* stmt_global) const
        {
            gen->m_globals[stmt_global->name.value.value()] = true;
        }

        void operator()(const NodeStmtConst* stmt_const) const
        {
            // Already handled by collect_globals; no runtime code needed.
        }

        void operator()(const NodeStmtExpr* stmt_expr) const
        {
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
                                lsp_exit(stmt_assign->ident.loc, "Array size mismatch");
                            }
                            for (size_t i = 0; i < var.array_size; i++) {
                                gen->gen_expr(*(*arr_lit)->elements[i]);
                            }
                            for (size_t i = 0; i < var.array_size; i++) {
                                uint32_t val = gen->pop_vreg();
                                size_t idx = var.array_size - 1 - i;
                                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc + idx));
                                gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                            }
                            return;
                        }
                        lsp_exit(stmt_assign->ident.loc, "Array assignment requires an array literal");
                    }
                    // Regular assignment (handled by store_var_from_vstack)
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

            // if cond != 0 → then_label, else → else_or_end
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

            // if cond == 0 → exit loop
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

            // Compare switch value against each case
            uint32_t switch_val = gen->peek_vreg();  // don't pop yet, need it for all comparisons
            for (size_t i = 0; i < stmt_switch->cases.size(); i++) {
                if (stmt_switch->cases[i].value == nullptr) continue;
                gen->gen_expr(*stmt_switch->cases[i].value);
                uint32_t case_val = gen->pop_vreg();
                uint32_t cmp = gen->m_ir.cmp_eq(IRValue::vreg(switch_val), IRValue::vreg(case_val));
                auto next_label = gen->new_label();
                gen->m_ir.br(IRValue::vreg(cmp), case_labels[i], next_label);
                gen->m_ir.new_block(next_label);
            }

            // Pop the switch value (no longer needed)
            gen->pop_vreg();

            if (default_idx < stmt_switch->cases.size()) {
                gen->m_ir.jmp(case_labels[default_idx]);
            }
            gen->m_ir.jmp(label_end);

            // Emit case bodies
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
                lsp_exit(stmt_arr->loc, "Array size must be a compile-time constant expression");
            }
            size_t size = static_cast<size_t>(const_size.value());
            auto& scope = gen->m_scopes.back();
            uint32_t slot = gen->m_next_frame_slot;
            gen->m_next_frame_slot += size;
            scope.vars[stmt_arr->name.value.value()] = Var { .stack_loc = slot, .array_size = size };
            scope.var_count += size;
            // Zero-initialize array elements linearly
            // Each element is 8 bytes, stored at frame_addr(slot + i)
            for (size_t i = 0; i < size; i++) {
                uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(slot + i));
                gen->m_ir.store(IRValue::vreg(addr), 0, IRValue::imm_i64(0));
            }
        }

        void operator()(const NodeStmtArrAssign* stmt_arr_assign) const
        {
            const auto& name = stmt_arr_assign->name.value.value();
            gen->gen_expr(*stmt_arr_assign->index);
            gen->gen_expr(*stmt_arr_assign->expr);
            uint32_t val = gen->pop_vreg();
            uint32_t idx = gen->pop_vreg();

            // Try local scope first
            for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                if (it->vars.contains(name)) {
                    const auto& var = it->vars.at(name);
                    uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                    uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                    uint32_t elem_addr = gen->m_ir.sub(IRValue::vreg(base_addr), IRValue::vreg(offset));
                    gen->m_ir.store(IRValue::vreg(elem_addr), 0, IRValue::vreg(val));
                    return;
                }
            }
            // Try global
            if (gen->m_global_var_info.contains(name)) {
                uint32_t base_addr = gen->m_ir.lea_label(name);
                uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                uint32_t elem_addr = gen->m_ir.add(IRValue::vreg(base_addr), IRValue::vreg(offset));
                gen->m_ir.store(IRValue::vreg(elem_addr), 0, IRValue::vreg(val));
                return;
            }
            lsp_exit(stmt_arr_assign->name.loc, "Undeclared identifier: " + name);
        }

        void operator()(const NodeStmtReturn* stmt_ret) const
        {
            if (gen->m_in_function) {
                if (stmt_ret->expr) {
                    gen->gen_expr(*stmt_ret->expr);
                    uint32_t val = gen->pop_vreg();
                    // Set return value (convention: rax = return value)
                    // We emit ret with the value; the backend will put it in rax
                    gen->m_ir.ret(IRValue::vreg(val));
                } else {
                    gen->m_ir.ret_void();
                }
                // The allocator/emitter will handle the jump to epilogue
                // Actually ret already ends the block, and we need to jump to epilogue
                // The old code jumped to the epilogue label. With IR, we emit ret directly.
            } else {
                if (!stmt_ret->expr) {
                    lsp_exit(stmt_ret->loc, "return with no value at top level");
                }
                gen->gen_expr(*stmt_ret->expr);
                uint32_t code = gen->pop_vreg();
                gen->m_ir.call("_sodium_exit", {IRValue::vreg(code)});
                gen->m_emitted_exit = true;
            }
        }

        void operator()(const NodeStmtPrint* stmt_print) const
        {
            // Unify print: string literal → print_str, everything else → print_int
            if (std::holds_alternative<NodeExprStringLit*>(stmt_print->expr->var)) {
                // String literal: push its address and call _sodium_print_str
                gen->gen_expr(*stmt_print->expr);
                uint32_t addr = gen->pop_vreg();
                gen->m_ir.call("_sodium_print_str", {IRValue::vreg(addr)});
            } else {
                gen->gen_expr(*stmt_print->expr);
                uint32_t val = gen->pop_vreg();
                gen->m_ir.call("_sodium_print_int", {IRValue::vreg(val)});
            }
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
                lsp_exit(stmt_break->loc, "break outside loop or switch");
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
            uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
            uint32_t field_addr = gen->m_ir.lea(IRValue::vreg(base_addr), -static_cast<int64_t>(field_offset * 8));

            if (stmt_field->op == AssignOp::assign) {
                gen->gen_expr(*stmt_field->expr);
                uint32_t val = gen->pop_vreg();
                gen->m_ir.store(IRValue::vreg(field_addr), 0, IRValue::vreg(val));
            } else {
                // Compound assignment: read field → apply op → write back
                uint32_t old_val = gen->m_ir.load(IRValue::vreg(field_addr), 0);
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
                    case AssignOp::shr_assign:     result = gen->m_ir.shr(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    default: break;
                }
                gen->m_ir.store(IRValue::vreg(field_addr), 0, IRValue::vreg(result));
            }
        }

        void operator()(const NodeStmtDerefAssign* stmt_deref) const
        {
            if (stmt_deref->op == AssignOp::assign) {
                // *ptr = expr
                gen->gen_expr(*stmt_deref->expr);
                gen->gen_expr(*stmt_deref->ptr_expr);
                uint32_t ptr = gen->pop_vreg();   // address
                uint32_t val = gen->pop_vreg();   // value
                gen->m_ir.store(IRValue::vreg(ptr), 0, IRValue::vreg(val));
            } else {
                // *ptr += expr
                gen->gen_expr(*stmt_deref->ptr_expr);
                uint32_t ptr = gen->pop_vreg();
                uint32_t old_val = gen->m_ir.load(IRValue::vreg(ptr), 0);
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
                    case AssignOp::shr_assign:     result = gen->m_ir.shr(IRValue::vreg(old_val), IRValue::vreg(rhs)); break;
                    default: break;
                }
                gen->m_ir.store(IRValue::vreg(ptr), 0, IRValue::vreg(result));
            }
        }
    };

    StmtVisitor visitor { .gen = this };
    std::visit(visitor, stmt.var);
}

// ==========================================================================
// eval_const_expr (unchanged from original)
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

// ==========================================================================
// collect_globals (unchanged)
// ==========================================================================

void Generator::collect_globals(const std::vector<NodeStmt>& stmts)
{
    for (const auto& stmt : stmts) {
        if (auto* global = std::get_if<NodeStmtGlobal*>(&stmt.var)) {
            const auto& name = (*global)->name.value.value();
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
                m_bss_entries.push_back({name, arr_size});
            } else if ((*global)->expr) {
                if (auto* int_lit = std::get_if<NodeExprIntLit*>(&(*global)->expr->var)) {
                    m_data_entries.push_back({name, (*int_lit)->int_lit.value.value()});
                } else {
                    m_data_entries.push_back({name, "0"});
                    m_global_inits.push_back({name, (*global)->expr});
                }
            } else {
                m_bss_entries.push_back({name, 1});
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

// ==========================================================================
// gen_prog — emit complete program
// ==========================================================================

[[nodiscard]] std::string Generator::gen_prog()
{
    // Populate function names
    for (const auto* func : m_prog.funcs) {
        m_func_names[func->name.value.value()] = true;
    }

    // Populate struct type information
    for (const auto* sd : m_prog.structs) {
        StructInfo info;
        for (size_t i = 0; i < sd->fields.size(); i++) {
            const auto& field_name = sd->fields[i].value.value();
            info.field_names.push_back(field_name);
            info.field_offsets[field_name] = i;
        }
        info.size = sd->fields.size();
        m_struct_types[sd->name.value.value()] = info;
    }

    // Pre-collect globals
    collect_globals(m_prog.stmts);
    for (const auto& func : m_prog.funcs) {
        collect_globals(func->body->stmts);
    }

    m_backend_ptr->extern_sym("_sodium_exit");
    m_backend_ptr->extern_sym("_sodium_print_int");
    m_backend_ptr->extern_sym("_sodium_read_int");
    m_backend_ptr->extern_sym("_sodium_malloc");
    m_backend_ptr->extern_sym("_sodium_free");
    m_backend_ptr->extern_sym("_sodium_print_str");
    m_backend_ptr->extern_sym("__sodium_argc");
    m_backend_ptr->extern_sym("__sodium_argv");

    // Emit the _start entry point.
    // Before jumping to _start_body, save argc and argv from the
    // kernel-provided stack/registers into runtime globals so that
    // the builtins argc() and argv() can access them.
    m_backend_ptr->global_sym("_start");
    m_backend_ptr->label("_start");
    if (m_backend_ptr->target_name() == "x86_64") {
        // x86-64: argc at [rsp], argv at [rsp+8]
        m_backend_ptr->emit_insn("mov", "rax, [rsp]");
        m_backend_ptr->store(m_backend_ptr->addr_label("__sodium_argc"), "rax");
        m_backend_ptr->emit_insn("lea", "rax, [rsp+8]");
        m_backend_ptr->store(m_backend_ptr->addr_label("__sodium_argv"), "rax");
    } else {
        // RISC-V: argc at [sp], argv at [sp+8] (stack convention in QEMU/Linux)
        m_backend_ptr->emit_insn("ld", "t0, 0(sp)");
        m_backend_ptr->emit_insn("la", "t1, __sodium_argc");
        m_backend_ptr->emit_insn("sd", "t0, 0(t1)");
        m_backend_ptr->emit_insn("addi", "t0, sp, 8");
        m_backend_ptr->emit_insn("la", "t1, __sodium_argv");
        m_backend_ptr->emit_insn("sd", "t0, 0(t1)");
    }
    m_backend_ptr->jmp("_start_body");

    // Global initializers — emit as IR and flush
    if (!m_global_inits.empty()) {
        m_ir.start_function("_start_globals");
        m_next_frame_slot = 0;

        for (const auto& init : m_global_inits) {
            gen_expr(*init.expr);
            uint32_t val = pop_vreg();
            m_ir.store(IRValue::vreg(m_ir.lea_label(init.name)), 0, IRValue::vreg(val));
        }

        m_ir.ret_void();
        flush_function();
    }

    // Top-level statements
    {
        m_ir.start_function("_start_body");
        m_next_frame_slot = 0;

        // If there are global initializers, call them before main body
        if (!m_global_inits.empty()) {
            m_ir.call("_start_globals", {});
        }

        for (const NodeStmt& stmt : m_prog.stmts) {
            gen_stmt(stmt);
        }

        if (!m_emitted_exit) {
            m_ir.call("_sodium_exit", {IRValue::imm_i64(0)});
        }

        m_ir.ret_void();
        flush_function();
    }

    // Then emit user functions
    for (const auto& func : m_prog.funcs) {
        gen_func_def(*func);
    }

    // Data sections
    if (!m_data_entries.empty()) {
        m_backend_ptr->section(".data");
        for (const auto& entry : m_data_entries) {
            m_backend_ptr->dq(entry.name, entry.value);
        }
    }
    if (!m_strings.empty()) {
        m_backend_ptr->section(".rodata");
        for (const auto& entry : m_strings) {
            m_backend_ptr->db_str(entry.label, entry.value);
        }
    }
    if (!m_bss_entries.empty()) {
        m_backend_ptr->section(".bss");
        for (const auto& entry : m_bss_entries) {
            m_backend_ptr->resq(entry.name, entry.qwords);
        }
    }

    return m_output.str();
}

// ==========================================================================
// Utility methods (mostly unchanged)
// ==========================================================================

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
