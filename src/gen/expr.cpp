#include "../generation.hpp"
void Generator::gen_expr(const NodeExpr& expr)
{
    struct ExprVisitor {
        Generator* gen;

        void operator()(const NodeExprIntLit* expr_int_lit)
        {
            int64_t val;
            try {
                val = std::stoll(expr_int_lit->int_lit.value.value());
            } catch (const std::out_of_range&) {
                gen->fail(expr_int_lit->int_lit.loc,
                    "Integer literal '" + expr_int_lit->int_lit.value.value() +
                    "' out of range for 64-bit signed integer");
            }
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
            // String literal is pointer to u8 (byte)
            Type* pointee = gen->alloc_type(Type::make_int(IntType::u8));
            gen->m_tstack.back() = Type::make_ptr(pointee);
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
                    gen->mark_var_used(name);
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
                uint32_t base_addr = gen->m_ir.lea_label(name);
                uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                uint32_t elem_addr = gen->m_ir.add(IRValue::vreg(base_addr), IRValue::vreg(offset));
                uint32_t val = gen->m_ir.load(IRValue::vreg(elem_addr), 0);
                gen->push_vreg(val);
                return;
            }
            std::string suggestion = gen->did_you_mean(name);
            if (!suggestion.empty()) {
                gen->fail_with_suggestion(expr_index->name.loc, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)", suggestion);
            } else {
                gen->fail(expr_index->name.loc, "Undeclared identifier: " + name);
            }
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

        void operator()(const NodeExprRead* read_expr) const
        {
            if (read_expr->buffer == nullptr) {
                // read() — integer read (existing behavior)
                uint32_t v = gen->m_ir.call("_sodium_read_int", {});
                gen->push_vreg(v);
                gen->m_tstack.back() = Type::make_int(IntType::i64);
            } else {
                // read(buf) — read a line of input into an array buffer
                auto* inner = read_expr->buffer;
                if (auto* ident = std::get_if<NodeExprIdent*>(&inner->var)) {
                    const auto& name = (*ident)->ident.value.value();
                    // Try local scope first
                    for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                        if (it->vars.contains(name)) {
                            gen->mark_var_used(name);
                            const auto& var = it->vars.at(name);
                            if (var.array_size == 0) {
                                gen->fail((*ident)->ident.loc,
                                    "read() requires an array variable, but '" + name + "' is a scalar");
                            }
                            // Local arrays grow downward from stack_loc: index 0
                            // is the highest slot. read_str writes forward from
                            // the pointer, so start at the lowest slot.
                            int64_t bottom_byte = static_cast<int64_t>(var.stack_loc)
                                + (static_cast<int64_t>(var.array_size) - 1) * 8;
                            uint32_t addr = gen->m_ir.frame_addr(bottom_byte);
                            int64_t byte_size = static_cast<int64_t>(var.array_size) * 8;
                            uint32_t result = gen->m_ir.call("_sodium_read_str",
                                {IRValue::vreg(addr), IRValue::imm_i64(byte_size)});
                            gen->push_vreg(result);
                            gen->m_tstack.back() = Type::make_int(IntType::i64);
                            return;
                        }
                    }
                    // Try global scope
                    if (gen->m_global_var_info.contains(name)) {
                        const auto& var = gen->m_global_var_info.at(name);
                        if (var.array_size == 0) {
                            gen->fail((*ident)->ident.loc,
                                "read() requires an array variable, but '" + name + "' is a global scalar");
                        }
                        uint32_t addr = gen->m_ir.lea_label(name);
                        int64_t byte_size = static_cast<int64_t>(var.array_size) * 8;
                        uint32_t result = gen->m_ir.call("_sodium_read_str",
                            {IRValue::vreg(addr), IRValue::imm_i64(byte_size)});
                        gen->push_vreg(result);
                        gen->m_tstack.back() = Type::make_int(IntType::i64);
                        return;
                    }
                    std::string suggestion = gen->did_you_mean(name);
                    if (!suggestion.empty()) {
                        gen->fail_with_suggestion((*ident)->ident.loc, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)", suggestion);
                    } else {
                        gen->fail((*ident)->ident.loc, "Undeclared identifier: " + name);
                    }
                } else {
                    gen->fail(read_expr->loc,
                        "read() argument must be an array variable");
                }
            }
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
                gen->fail(field_access->obj_name.loc, "Not a struct variable: " + obj_name);
            }
            auto struct_info = gen->get_struct_info(var.struct_type);
            if (!struct_info.has_value()) {
                gen->fail(field_access->field_name.loc, "Unknown struct type: " + var.struct_type);
            }
            auto offset_it = struct_info.value().field_offsets.find(field_name);
            if (offset_it == struct_info.value().field_offsets.end()) {
                gen->fail(field_access->field_name.loc, "Unknown field '" + field_name + "' in struct '" + var.struct_type + "'");
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
                        gen->mark_var_used(name);
                        const auto& var = it->vars.at(name);
                        uint32_t addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                        gen->push_vreg(addr);
                        // Stack slots are now packed by type size, so &x gives Ptr(typeof(x)).
                        Type* pointee = gen->alloc_type(var.type);
                        gen->m_tstack.back() = Type::make_ptr(pointee);
                        return;
                    }
                }
                if (gen->m_globals.contains(name)) {
                    uint32_t addr = gen->m_ir.lea_label(name);
                    gen->push_vreg(addr);
                    // Global variables default to i64
                    Type* pointee = gen->alloc_type(Type::make_int(IntType::i64));
                    gen->m_tstack.back() = Type::make_ptr(pointee);
                    return;
                }
                std::string suggestion = gen->did_you_mean(name);
                if (!suggestion.empty()) {
                    gen->fail_with_suggestion(addr_of->ampersand.loc, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)", suggestion);
                } else {
                    gen->fail(addr_of->ampersand.loc, "Undeclared identifier: " + name);
                }
            } else if (auto* arr_idx = std::get_if<NodeExprIndex*>(&inner->var)) {
                // &arr[i] — address of array element
                const auto& name = (*arr_idx)->name.value.value();
                gen->gen_expr(*(*arr_idx)->index);
                uint32_t idx = gen->pop_vreg();
                // Try local scope first
                for (auto it = gen->m_scopes.rbegin(); it != gen->m_scopes.rend(); ++it) {
                    if (it->vars.contains(name)) {
                        gen->mark_var_used(name);
                        const auto& var = it->vars.at(name);
                        uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                        uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                        uint32_t elem_addr = gen->m_ir.sub(IRValue::vreg(base_addr), IRValue::vreg(offset));
                        gen->push_vreg(elem_addr);
                        // Array elements are i64 (8-byte slots)
                        Type* pointee = gen->alloc_type(Type::make_int(IntType::i64));
                        gen->m_tstack.back() = Type::make_ptr(pointee);
                        return;
                    }
                }
                // Try global
                if (gen->m_global_var_info.contains(name)) {
                    uint32_t base_addr = gen->m_ir.lea_label(name);
                    uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                    uint32_t elem_addr = gen->m_ir.add(IRValue::vreg(base_addr), IRValue::vreg(offset));
                    gen->push_vreg(elem_addr);
                    Type* pointee = gen->alloc_type(Type::make_int(IntType::i64));
                    gen->m_tstack.back() = Type::make_ptr(pointee);
                    return;
                }
                std::string suggestion = gen->did_you_mean(name);
                if (!suggestion.empty()) {
                    gen->fail_with_suggestion((*arr_idx)->name.loc, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)", suggestion);
                } else {
                    gen->fail((*arr_idx)->name.loc, "Undeclared identifier: " + name);
                }
            } else if (auto* field = std::get_if<NodeExprFieldAccess*>(&inner->var)) {
                // &obj.field
                const auto& obj_name = (*field)->obj_name.value.value();
                const auto& field_name = (*field)->field_name.value.value();
                auto var = gen->lookup_var(obj_name, (*field)->obj_name.loc);
                if (var.struct_type.empty()) {
                    gen->fail((*field)->obj_name.loc, "Not a struct variable: " + obj_name);
                }
                auto struct_info = gen->get_struct_info(var.struct_type);
                if (!struct_info.has_value()) {
                    gen->fail((*field)->field_name.loc, "Unknown struct type: " + var.struct_type);
                }
                auto offset_it = struct_info.value().field_offsets.find(field_name);
                if (offset_it == struct_info.value().field_offsets.end()) {
                    gen->fail((*field)->field_name.loc, "Unknown field '" + field_name + "' in struct '" + var.struct_type + "'");
                }
                size_t field_offset = offset_it->second;
                uint32_t base_addr = gen->m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                uint32_t field_addr = gen->m_ir.lea(IRValue::vreg(base_addr), -static_cast<int64_t>(field_offset * 8));
                gen->push_vreg(field_addr);
                // Struct fields are i64 (8-byte slots)
                Type* pointee = gen->alloc_type(Type::make_int(IntType::i64));
                gen->m_tstack.back() = Type::make_ptr(pointee);
            } else {
                gen->fail(addr_of->ampersand.loc, "Cannot take address of this expression");
            }
        }

        void operator()(const NodeExprDeref* deref) const
        {
            // *ptr — evaluate ptr, then load from that address
            gen->gen_expr(*deref->expr);
            Type ptr_type = gen->peek_type();
            uint32_t ptr = gen->pop_vreg();

            if (ptr_type.kind == Type::Ptr && ptr_type.pointee) {
                size_t elem_size = type_size(*ptr_type.pointee);
                bool is_signed = (ptr_type.pointee->kind == Type::Int &&
                                  (ptr_type.pointee->int_type == IntType::i8 ||
                                   ptr_type.pointee->int_type == IntType::i16 ||
                                   ptr_type.pointee->int_type == IntType::i32 ||
                                   ptr_type.pointee->int_type == IntType::i64));
                uint32_t v;
                if (elem_size >= 8) {
                    v = gen->m_ir.load(IRValue::vreg(ptr), 0);
                } else if (elem_size == 4) {
                    v = is_signed ? gen->m_ir.load_s32(IRValue::vreg(ptr), 0)
                                  : gen->m_ir.load_u32(IRValue::vreg(ptr), 0);
                } else if (elem_size == 2) {
                    v = is_signed ? gen->m_ir.load_s16(IRValue::vreg(ptr), 0)
                                  : gen->m_ir.load_u16(IRValue::vreg(ptr), 0);
                } else {
                    v = is_signed ? gen->m_ir.load_s8(IRValue::vreg(ptr), 0)
                                  : gen->m_ir.load_u8(IRValue::vreg(ptr), 0);
                }
                gen->push_vreg(v);
                // Result type is the pointee type
                Type* pointee_copy = gen->alloc_type(*ptr_type.pointee);
                gen->m_tstack.back() = *pointee_copy;
            } else {
                // Unknown pointer type — load 8 bytes (default)
                uint32_t val = gen->m_ir.load(IRValue::vreg(ptr), 0);
                gen->push_vreg(val);
            }
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
                // malloc returns Ptr(u8) — untyped bytes
                Type* pointee = gen->alloc_type(Type::make_int(IntType::u8));
                gen->m_tstack.back() = Type::make_ptr(pointee);
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
            if (fname == "read_eof") {
                // read_eof() — 1 if the most recent read() hit EOF, else 0.
                if (!expr_call->args.empty()) {
                    gen->fail(expr_call->name.loc, "read_eof() takes no arguments");
                }
                uint32_t v = gen->m_ir.call("_sodium_read_eof", {});
                gen->push_vreg(v);
                gen->m_tstack.back() = Type::make_int(IntType::i64);
                return;
            }
            if (fname == "argc") {
                // argc() — return number of command-line arguments
                if (!expr_call->args.empty()) {
                    gen->fail(expr_call->name.loc, "argc() takes no arguments");
                }
                uint32_t addr = gen->m_ir.lea_label("__sodium_argc");
                uint32_t val = gen->m_ir.load(IRValue::vreg(addr), 0);
                gen->push_vreg(val);
                return;
            }
            if (fname == "argv") {
                // argv(i) — return pointer to the i-th argument string
                if (expr_call->args.size() != 1) {
                    gen->fail(expr_call->name.loc, "argv() takes exactly one argument (the index)");
                }
                gen->gen_expr(*expr_call->args[0]);
                uint32_t idx = gen->pop_vreg();
                uint32_t addr = gen->m_ir.lea_label("__sodium_argv");
                uint32_t argv_ptr = gen->m_ir.load(IRValue::vreg(addr), 0);
                uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                uint32_t entry_addr = gen->m_ir.add(IRValue::vreg(argv_ptr), IRValue::vreg(offset));
                uint32_t str_ptr = gen->m_ir.load(IRValue::vreg(entry_addr), 0);
                gen->push_vreg(str_ptr);
                // argv(i) returns Ptr(u8) (pointer to the argument string)
                Type* pointee = gen->alloc_type(Type::make_int(IntType::u8));
                gen->m_tstack.back() = Type::make_ptr(pointee);
                return;
            }
            if (!gen->m_func_names.contains(fname)) {
                std::string suggestion = gen->did_you_mean(fname);
                if (!suggestion.empty()) {
                    gen->fail_with_suggestion(expr_call->name.loc, "Undefined function: " + fname + " (did you mean '" + suggestion + "'?)", suggestion);
                } else {
                    gen->fail(expr_call->name.loc, "Undefined function: " + fname);
                }
            }

            // Collect arguments (in evaluation order)
            std::vector<IRValue> args;
            for (size_t i = 0; i < expr_call->args.size(); i++) {
                gen->gen_expr(*expr_call->args[i]);
            }
            gen->check_call_args(*expr_call, expr_call->name.loc);
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
                    Type type_b = gen->pop_type();
                    Type type_a = gen->pop_type();
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();

                    if (type_a.kind == Type::Ptr && type_a.pointee &&
                        type_b.kind == Type::Int) {
                        // ptr + int: scale the int by sizeof(*ptr)
                        size_t elem_size = type_size(*type_a.pointee);
                        if (elem_size > 1) {
                            uint32_t scaled = gen->m_ir.mul(IRValue::vreg(b),
                                IRValue::imm_i64(static_cast<int64_t>(elem_size)));
                            uint32_t v = gen->m_ir.add(IRValue::vreg(a), IRValue::vreg(scaled));
                            gen->push_vreg(v);
                            gen->m_tstack.back() = type_a;
                        } else {
                            uint32_t v = gen->m_ir.add(IRValue::vreg(a), IRValue::vreg(b));
                            gen->push_vreg(v);
                            gen->m_tstack.back() = type_a;
                        }
                    } else if (type_b.kind == Type::Ptr && type_b.pointee &&
                               type_a.kind == Type::Int) {
                        // int + ptr: scale the int by sizeof(*ptr)
                        size_t elem_size = type_size(*type_b.pointee);
                        if (elem_size > 1) {
                            uint32_t scaled = gen->m_ir.mul(IRValue::vreg(a),
                                IRValue::imm_i64(static_cast<int64_t>(elem_size)));
                            uint32_t v = gen->m_ir.add(IRValue::vreg(scaled), IRValue::vreg(b));
                            gen->push_vreg(v);
                            gen->m_tstack.back() = type_b;
                        } else {
                            uint32_t v = gen->m_ir.add(IRValue::vreg(a), IRValue::vreg(b));
                            gen->push_vreg(v);
                            gen->m_tstack.back() = type_b;
                        }
                    } else {
                        // int + int
                        uint32_t v = gen->m_ir.add(IRValue::vreg(a), IRValue::vreg(b));
                        gen->push_vreg(v);
                    }
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
                    Type type_b = gen->pop_type();
                    Type type_a = gen->pop_type();
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();

                    if (type_a.kind == Type::Ptr && type_a.pointee &&
                        type_b.kind == Type::Int) {
                        // ptr - int: scale the int by sizeof(*ptr)
                        size_t elem_size = type_size(*type_a.pointee);
                        if (elem_size > 1) {
                            uint32_t scaled = gen->m_ir.mul(IRValue::vreg(b),
                                IRValue::imm_i64(static_cast<int64_t>(elem_size)));
                            uint32_t v = gen->m_ir.sub(IRValue::vreg(a), IRValue::vreg(scaled));
                            gen->push_vreg(v);
                            gen->m_tstack.back() = type_a;
                        } else {
                            uint32_t v = gen->m_ir.sub(IRValue::vreg(a), IRValue::vreg(b));
                            gen->push_vreg(v);
                            gen->m_tstack.back() = type_a;
                        }
                    } else if (type_a.kind == Type::Ptr && type_b.kind == Type::Ptr &&
                               type_a.pointee && type_b.pointee &&
                               *type_a.pointee == *type_b.pointee) {
                        // ptr - ptr: difference in bytes, divide by element size
                        size_t elem_size = type_size(*type_a.pointee);
                        uint32_t diff = gen->m_ir.sub(IRValue::vreg(a), IRValue::vreg(b));
                        if (elem_size > 1) {
                            // Arithmetic shift right by log2(elem_size).
                            // Must use ASHR (not SHR) because the byte difference
                            // can be negative (stack grows down).
                            int shift = 0;
                            size_t s = elem_size;
                            while (s > 1) { s >>= 1; shift++; }
                            uint32_t v = gen->m_ir.ashr(IRValue::vreg(diff),
                                IRValue::imm_i64(static_cast<int64_t>(shift)));
                            gen->push_vreg(v);
                        } else {
                            gen->push_vreg(diff);
                        }
                    } else {
                        // int - int
                        uint32_t v = gen->m_ir.sub(IRValue::vreg(a), IRValue::vreg(b));
                        gen->push_vreg(v);
                    }
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
                    // Cyan integers are signed i64 by default: >> is arithmetic.
                    // Unsigned values are stored as non-negative 64-bit values, so
                    // ASHR gives the same result as SHR for them.
                    uint32_t v = gen->m_ir.ashr(IRValue::vreg(a), IRValue::vreg(b));
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

