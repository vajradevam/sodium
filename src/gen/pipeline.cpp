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

    // O1: check the IR before any transform can hide a problem. Explicit
    // --verify-ir asks for it; debug builds (SODIUM_IR_VERIFY) always run it.
#if defined(SODIUM_IR_VERIFY)
    const bool want_verify = true;
#else
    const bool want_verify = m_verify_ir;
#endif
    auto check_ir = [&]() {
        if (!want_verify) return true;
        IRVerifyResult result = verify_function(*func);
        if (!result.ok()) {
            std::cerr << "IR verification failed:\n" << result.to_string();
            return false;
        }
        return true;
    };
    if (!check_ir()) throw GenAbort{};

    if (m_opt_level >= 1) {
        fold_function(*func);
        promote_locals(*func);
        cse_function(*func);
        cleanup_function(*func);
    }
    if (!check_ir()) throw GenAbort{};  // catch a pass that breaks the IR

    // Dump the IR after optimization, so --emit-ir shows what is actually
    // allocated and emitted.
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

    // Argument words are placed by the same ArgPlacer the call sites use (D-012). A struct
    // result larger than 16 bytes is returned through a hidden pointer that arrives as the
    // first argument word.
    ArgPlacer placer(*m_tri_ptr);
    auto store_word_param = [&](const Var& var, size_t bytes, const ArgPlacement& place) {
        m_ir.store_param(static_cast<int64_t>(placer.ordinal(place)), 0,
                         -static_cast<int64_t>(var.stack_loc + frame_bytes(var)),
                         bytes == 1 ? IRWidth::I8 : bytes == 2 ? IRWidth::I16
                                    : bytes == 4 ? IRWidth::I32 : IRWidth::I64);
    };
    m_ret_ptr_slot.reset();
    if (func.return_type && func.return_type->kind == Type::Struct) {
        auto ret_info = get_struct_info(func.return_type->struct_name);
        if (!ret_info.has_value()) fail(func.name.loc, "Unknown struct type: " + func.return_type->struct_name);
        if (ret_info->size > 16) {
            uint32_t slot = alloc_object(8);
            m_ir.store_param(static_cast<int64_t>(placer.ordinal(placer.scalar())), 0,
                             -static_cast<int64_t>(slot + 8), IRWidth::I64);
            m_ret_ptr_slot = slot;
        }
    }

    // Process parameters: allocate frame slots and store the incoming words into them.
    for (size_t i = 0; i < func.params.size(); i++) {
        std::string param_name = func.params[i].value.value();
        auto& scope = m_scopes.back();

        // Declared types take precedence; otherwise use call-site inference.
        if (i >= func.param_types.size() || !func.param_types[i]) {
            m_diag.warning(func.params[i].loc, "untyped parameter '" + param_name
                                               + "' uses inferred type");
        }
        Type param_type = Type::make_int(IntType::i64);
        if (i < func.param_types.size() && func.param_types[i]) {
            param_type = *func.param_types[i];
        } else {
            auto inferred_it = m_inferred_param_types.find(func.name.value.value());
            if (inferred_it != m_inferred_param_types.end() && i < inferred_it->second.size()) {
                param_type = inferred_it->second[i];
            }
        }

        if (param_type.kind == Type::Struct) {
            auto info = get_struct_info(param_type.struct_name);
            if (!info.has_value()) fail(func.params[i].loc, "Unknown struct type: " + param_type.struct_name);
            const std::vector<ArgPlacement> places = place_struct_arg(placer, *info);
            if (info->size > 16 && places.size() == 1) {
                // Passed by reference: the slot holds a pointer to the caller's copy.
                uint32_t slot = alloc_object(8);
                scope.vars[param_name] = Var { .stack_loc = slot, .array_size = info->size,
                    .type = param_type, .struct_type = param_type.struct_name,
                    .decl_loc = func.params[i].loc, .is_param = true, .by_ref = true, .dim_sizes = {} };
                scope.var_count++;
                store_word_param(scope.vars.at(param_name), 8, places[0]);
                continue;
            }
            uint32_t slot = alloc_object(info->size);
            scope.vars[param_name] = Var { .stack_loc = slot, .array_size = info->size,
                .type = param_type, .struct_type = param_type.struct_name,
                .decl_loc = func.params[i].loc, .is_param = true, .dim_sizes = {} };
            scope.var_count += info->size;
            for (size_t w = 0; w < places.size(); w++) {
                store_param_word(scope.vars.at(param_name), *info, w, placer.ordinal(places[w]));
            }
            continue;
        }

        // Parameters use 8-byte, 8-aligned slots; the value is stored at the
        // variable's own width so loads through var_addr() see it.
        uint32_t offset = static_cast<uint32_t>(align_frame(m_next_frame_offset, 8));
        m_next_frame_offset = offset + 8;
        scope.vars[param_name] = Var { .stack_loc = offset, .type = param_type, .struct_type = "",
                                        .decl_loc = func.params[i].loc, .is_param = true, .dim_sizes = {} };
        scope.var_count++;

        // Store the incoming word (argument register or stack slot) straight into
        // the frame slot at the variable's own width (STORE_PARAM, no vreg involved).
        store_word_param(scope.vars.at(param_name),
                         param_type.kind == Type::Int ? type_size(param_type) : 8, placer.scalar());
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
    m_ret_ptr_slot.reset();
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

void Generator::collect_enums() {
    for (const auto* def : m_prog.enums) {
        int64_t next = 0;
        for (const auto& member : def->members) {
            int64_t value = next;
            if (member.value) {
                auto constant = eval_const_expr(member.value);
                if (!constant.has_value()) {
                    fail(member.name.loc, "Enum value must be a compile-time constant");
                }
                value = constant.value();
            }
            m_constants[member.name.value.value()] = value;
            next = value + 1;
        }
    }
}

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
                m_global_var_info[name] = Var { .stack_loc = 0, .array_size = arr_size, .struct_type = "", .dim_sizes = {} };
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

    // Populate struct type information with C-rule byte layout. Earlier
    // structs are laid out first so nested fields use their real sizes.
    std::unordered_map<std::string, size_t> struct_sizes;
    std::unordered_map<std::string, size_t> struct_aligns;
    for (const auto* sd : m_prog.structs) {
        StructInfo info = StructInfo::compute_layout(sd->fields, sd->field_types, struct_sizes, struct_aligns);
        struct_sizes[sd->name.value.value()] = info.size;
        struct_aligns[sd->name.value.value()] = info.align;
        m_struct_types[sd->name.value.value()] = info;
    }

    // Enumerators are compile-time constants available to the whole program.
    collect_enums();

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
    m_backend_ptr->extern_sym("_sodium_print_raw_hex");
    m_backend_ptr->extern_sym("_sodium_print_raw_uint");
    m_backend_ptr->extern_sym("_sodium_print_raw_char");
    m_backend_ptr->extern_sym("_sodium_print_raw_str");
    m_backend_ptr->extern_sym("_sodium_print_nl");
    m_backend_ptr->extern_sym("_sodium_read_int");
    m_backend_ptr->extern_sym("_sodium_read_str");
    m_backend_ptr->extern_sym("_sodium_read_eof");
    m_backend_ptr->extern_sym("_sodium_malloc");
    m_backend_ptr->extern_sym("_sodium_free");
    m_backend_ptr->extern_sym("_sodium_print_str");
    m_backend_ptr->extern_sym("_sodium_sys_open");
    m_backend_ptr->extern_sym("_sodium_sys_close");
    m_backend_ptr->extern_sym("_sodium_sys_read");
    m_backend_ptr->extern_sym("_sodium_sys_write");
    m_backend_ptr->extern_sym("_sodium_sys_lseek");
    m_backend_ptr->extern_sym("_sodium_sys_unlink");
    m_backend_ptr->extern_sym("__sodium_argc");
    m_backend_ptr->extern_sym("__sodium_argv");
    m_backend_ptr->extern_sym("__sodium_envp");

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
        // envp follows argv[argc] (the NULL terminator): rsp + 16 + argc*8.
        // rax was reused for argv, so reload argc first.
        m_backend_ptr->emit_insn("mov", "rax, [rsp]");
        m_backend_ptr->emit_insn("lea", "rax, [rsp + rax*8 + 16]");
        m_backend_ptr->store(m_backend_ptr->addr_label("__sodium_envp"), "rax");
    } else {
        // RISC-V: argc at [sp], argv at [sp+8] (stack convention in QEMU/Linux)
        m_backend_ptr->emit_insn("ld", "t0, 0(sp)");
        m_backend_ptr->emit_insn("la", "t1, __sodium_argc");
        m_backend_ptr->emit_insn("sd", "t0, 0(t1)");
        m_backend_ptr->emit_insn("addi", "t0, sp, 8");
        m_backend_ptr->emit_insn("la", "t1, __sodium_argv");
        m_backend_ptr->emit_insn("sd", "t0, 0(t1)");
        // Reload argc (t0 was reused for argv); envp is at sp + 16 + argc*8.
        m_backend_ptr->emit_insn("ld", "t0, 0(sp)");
        m_backend_ptr->emit_insn("slli", "t0, t0, 3");
        m_backend_ptr->emit_insn("addi", "t0, t0, 16");
        m_backend_ptr->emit_insn("add", "t0, sp, t0");
        m_backend_ptr->emit_insn("la", "t1, __sodium_envp");
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
