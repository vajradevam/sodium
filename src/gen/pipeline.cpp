#include "../generation.hpp"

// ==========================================================================
// IR function finalization
// ==========================================================================

void Generator::flush_function() {
    IRFunction* func = m_ir.current_function();
    if (!func) return;

    // Set stack slot count
    // Round frame size up to 16 bytes for ABI stack alignment
    // (x86-64 and RISC-V both require 16-byte alignment at call sites)
    func->stack_slots = (m_next_frame_offset + 15) & ~static_cast<uint32_t>(15);

    // Run the pipeline: liveness → allocator → rewriter → emitter
    std::string func_name = func->name;
    auto blocks = func->blocks; // copy blocks

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
    IREmitter emitter(*m_backend_ptr, *m_tri_ptr);
    emitter.emit_function(*func);

    // Reset IR state for next function
    m_ir.end_function();  // destroys the function
    m_vstack.clear();
    m_next_frame_offset = 0;
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
    m_next_frame_offset = 0;

    m_backend_ptr->extern_sym(func.name.value.value());
    // The entry block is already created by start_function();
    // the emitter will output the label and prologue

    enter_scope();
    m_in_function = true;
    m_current_return_type = func.return_type;
    m_current_func_name = func.name.value.value();
    m_func_epilogue_label = ".L" + func.name.value.value() + "_epilogue";

    // Process parameters: allocate frame slots and load from stack
    for (size_t i = 0; i < func.params.size(); i++) {
        std::string param_name = func.params[i].value.value();
        auto& scope = m_scopes.back();

        uint32_t offset = m_next_frame_offset;
        m_next_frame_offset += 8;
        // Declared types take precedence; otherwise use call-site inference.
        Type param_type = Type::make_int(IntType::i64);
        if (i < func.param_types.size() && func.param_types[i]) {
            param_type = *func.param_types[i];
        } else {
            auto inferred_it = m_inferred_param_types.find(func.name.value.value());
            if (inferred_it != m_inferred_param_types.end() && i < inferred_it->second.size()) {
                param_type = inferred_it->second[i];
            }
        }
        scope.vars[param_name] = Var { .stack_loc = offset, .type = param_type, .struct_type = "",
                                        .decl_loc = func.params[i].loc, .is_param = true };
        scope.var_count++;

        // Load parameter from ABI-defined location (argument register or stack)
        // using the target-agnostic LOAD_PARAM opcode.
        uint32_t param_val = m_ir.load_param(static_cast<int64_t>(i));
        uint32_t slot_addr = m_ir.frame_addr(static_cast<int64_t>(offset));
        m_ir.store(IRValue::vreg(slot_addr), 0, IRValue::vreg(param_val));
    }

    for (const auto& s : func.body->stmts) {
        try {
            gen_stmt(s);
        } catch (const GenAbort&) {
            // Keep checking the remaining statements in this function.
            m_vstack.clear();
            m_tstack.clear();
        } catch (const LSPAbort&) {
            // In LSP mode fail() throws LSPAbort instead; keep recovering so
            // later statements report their diagnostics too.
            m_vstack.clear();
            m_tstack.clear();
        }
    }

    // If main() has no explicit return, default to exit(0)
    if (func.name.value.value() == "main") {
        uint32_t zero = m_ir.load_i64(0);
        m_ir.ret(IRValue::vreg(zero));
    }

    // Add epilogue label and return
    m_ir.new_block(m_func_epilogue_label);
    m_ir.ret_void();
    m_in_function = false;
    m_current_return_type.reset();
    m_current_func_name.clear();
    m_func_epilogue_label.clear();

    // Warn for declarations in the function scope never read (E3b).
    if (!m_scopes.empty()) {
        for (const auto& [name, var] : m_scopes.back().vars) {
            if (!var.used) {
                if (var.is_param)
                    m_diag.warning(var.decl_loc, "unused parameter '" + name + "'");
                else
                    m_diag.warning(var.decl_loc, "unused variable '" + name + "'");
            }
        }
    }
    // Already warned above, so pop directly rather than through exit_scope()
    // (which would report the same locals a second time).
    m_scopes.pop_back();
    m_stack_size = saved_stack_size;

    // Finalize this function: run allocator and emit to backend
    flush_function();
}

// ==========================================================================
// collect_globals
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
                    fail((*global)->name.loc, "Global array size must be a compile-time constant expression");
                }
                arr_size = static_cast<size_t>(const_size.value());
            }
            m_globals[name] = true;
            if (arr_size > 0) {
                m_global_var_info[name] = Var { .stack_loc = 0, .array_size = arr_size, .struct_type = "" };
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
                fail((*const_stmt)->name.loc, "Const initializer is not a compile-time constant expression");
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

std::string Generator::gen_prog() {
    try {
        std::string out = gen_prog_impl();
        if (!m_diag.empty() && !g_lsp_mode) {
            for (const auto& d : m_diag.diagnostics()) {
                std::cerr << m_diag.render_one(d);
                if (d.loc.line != 0 && d.loc.col != 0) print_code_context(d.loc);
            }
            if (m_diag.has_errors()) exit(EXIT_FAILURE);
        }
        return out;
    } catch (const GenAbort&) {
        if (!g_lsp_mode) {
            for (const auto& d : m_diag.diagnostics()) {
                std::cerr << m_diag.render_one(d);
                if (d.loc.line != 0 && d.loc.col != 0) print_code_context(d.loc);
            }
            exit(EXIT_FAILURE);
        }
        return {};
    } catch (const LSPAbort&) {
        // Handled by the LSP server, which reads the engine's diagnostics.
        return {};
    }
}

std::string Generator::gen_prog_impl()
{
    // Populate function names
    for (const auto* func : m_prog.funcs) {
        m_func_names[func->name.value.value()] = true;
    }

    // Populate struct type information with C-rule byte layout.
    for (const auto* sd : m_prog.structs) {
        m_struct_types[sd->name.value.value()] =
            StructInfo::compute_layout(sd->fields, sd->field_types);
    }

    // Flow-based warnings (unreachable code, missing return).
    check_flow_warnings();

    // Pre-collect globals
    collect_globals(m_prog.stmts);
    for (const auto& func : m_prog.funcs) {
        collect_globals(func->body->stmts);
    }

    m_backend_ptr->extern_sym("_sodium_exit");
    m_backend_ptr->extern_sym("_sodium_print_int");
    m_backend_ptr->extern_sym("_sodium_print_raw_int");
    m_backend_ptr->extern_sym("_sodium_print_raw_str");
    m_backend_ptr->extern_sym("_sodium_print_nl");
    m_backend_ptr->extern_sym("_sodium_read_int");
    m_backend_ptr->extern_sym("_sodium_read_str");
    m_backend_ptr->extern_sym("_sodium_read_eof");
    m_backend_ptr->extern_sym("_sodium_malloc");
    m_backend_ptr->extern_sym("_sodium_free");
    m_backend_ptr->extern_sym("_sodium_print_str");
    m_backend_ptr->extern_sym("__sodium_argc");
    m_backend_ptr->extern_sym("__sodium_argv");

    // Find and validate the main function
    NodeFuncDef* main_func = nullptr;
    for (const auto& func : m_prog.funcs) {
        if (func->name.value.value() == "main") {
            main_func = func;
            break;
        }
    }
    if (!main_func) {
        fail({}, "No main function found. Define 'function main() { ... }' as the entry point.");
    }
    if (!main_func->params.empty()) {
        fail(main_func->name.loc, "main function must not take parameters");
    }

    // Infer parameter types from call sites
    collect_call_types();

    // Global initializers — emit as IR and flush FIRST
    // (before _start, so the order in the output is correct)
    if (!m_global_inits.empty()) {
        m_ir.start_function("_start_globals");
        m_next_frame_offset = 0;

        for (const auto& init : m_global_inits) {
            gen_expr(*init.expr);
            uint32_t val = pop_vreg();
            m_ir.store(IRValue::vreg(m_ir.lea_label(init.name)), 0, IRValue::vreg(val));
        }

        m_ir.ret_void();
        flush_function();
    }

    // Emit the _start entry point.
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

    // Call global initializers before main
    if (!m_global_inits.empty()) {
        m_backend_ptr->call("_start_globals");
    }

    // Call main, then exit with its return value
    m_backend_ptr->call("main");
    if (m_backend_ptr->target_name() == "x86_64") {
        // x86-64: main's return value is in rax; exit arg goes in rdi
        m_backend_ptr->emit_insn("mov", "rdi, rax");
    } else {
        // RISC-V: main's return value is in a0, which is also exit's arg
        // No move needed
    }
    m_backend_ptr->call("_sodium_exit");

    // Then emit user functions (including main). A semantic error in one
    // function skips only that function; later functions are still checked.
    for (const auto& func : m_prog.funcs) {
        try {
            gen_func_def(*func);
        } catch (const GenAbort&) {
            m_ir.end_function();
            m_vstack.clear();
            m_tstack.clear();
            m_in_function = false;
            m_scopes.clear();
            m_scopes.push_back({0, {}});
        } catch (const LSPAbort&) {
            m_ir.end_function();
            m_vstack.clear();
            m_tstack.clear();
            m_in_function = false;
            m_scopes.clear();
            m_scopes.push_back({0, {}});
        }
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
