#include "../generation.hpp"

namespace {
bool int_signed(IntType t) {
    return t == IntType::i8 || t == IntType::i16 ||
           t == IntType::i32 || t == IntType::i64;
}

IRWidth width_of_int(IntType t) {
    switch (t) {
        case IntType::i8:  case IntType::u8:  return IRWidth::I8;
        case IntType::i16: case IntType::u16: return IRWidth::I16;
        case IntType::i32: case IntType::u32: return IRWidth::I32;
        case IntType::i64: case IntType::u64: return IRWidth::I64;
    }
    return IRWidth::I64;
}

// Result type of `a + b` / `a - b` on integers: the wider operand, but nothing narrower than
// i32 (C's integer promotion). The register holds the full 64-bit result, so a u8 difference
// that went negative must not be claimed to be a u8 (it would be zero-extended on return).
Type int_arith_type(const Type& a, const Type& b) {
    const Type& wider = type_size(a) >= type_size(b) ? a : b;
    if (wider.kind == Type::Int && type_size(wider) < 4) return Type::make_int(IntType::i32);
    return wider;
}

uint32_t load_typed_field(IRBuilder& ir, uint32_t ptr, const Type& t) {
    if (t.kind == Type::Float) {
        return ir.fload(t.float_type == FloatType::f32 ? IRWidth::F32 : IRWidth::F64,
                        IRValue::vreg(ptr), 0);
    }
    size_t sz = type_size(t);
    bool sgn = int_signed(t.int_type);
    if (sz >= 8) return ir.load(IRValue::vreg(ptr), 0);
    if (sz == 4) return sgn ? ir.load_s32(IRValue::vreg(ptr), 0) : ir.load_u32(IRValue::vreg(ptr), 0);
    if (sz == 2) return sgn ? ir.load_s16(IRValue::vreg(ptr), 0) : ir.load_u16(IRValue::vreg(ptr), 0);
    return sgn ? ir.load_s8(IRValue::vreg(ptr), 0) : ir.load_u8(IRValue::vreg(ptr), 0);
}

// Raw file-descriptor builtins (L4a): Cyan name -> runtime symbol and arity.
struct SysBuiltin {
    const char* cyan_name;
    const char* runtime_name;
    size_t arity;
};

// `type_size` uses 8 as a fallback for structs; real layouts come from the
// struct registry. Pointer arithmetic on T* must use the actual struct size.
static size_t pointee_size(Generator* gen, const Type& t) {
    if (t.kind == Type::Struct) {
        auto info = gen->get_struct_info(t.struct_name);
        if (info.has_value()) return info->size;
    }
    return type_size(t);
}

const SysBuiltin* sys_builtin_for(const std::string& name) {
    static const SysBuiltin table[] = {
        {"sys_open",   "_sodium_sys_open",   3},
        {"sys_close",  "_sodium_sys_close",  1},
        {"sys_read",   "_sodium_sys_read",   3},
        {"sys_write",  "_sodium_sys_write",  3},
        {"sys_lseek",  "_sodium_sys_lseek",  3},
        {"sys_unlink", "_sodium_sys_unlink", 1},
    };
    for (const auto& b : table)
        if (name == b.cyan_name) return &b;
    return nullptr;
}
} // namespace

void Generator::gen_expr(const NodeExpr& expr)
{
    struct ExprVisitor {
        Generator* gen;

        void operator()(const NodeExprFloatLit* lit)
        {
            gen->gen_float_literal(*lit);
        }

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

            bool is_local = false;
            for (const auto& scope : gen->m_scopes) {
                if (scope.vars.contains(name)) { is_local = true; break; }
            }
            if (is_local) {
                Type elem_type;
                uint32_t elem_addr = gen->index_addr(*expr_index, elem_type);
                // A struct element is a struct value: represented by its address.
                uint32_t val = elem_type.kind == Type::Struct
                                   ? elem_addr
                                   : load_typed_field(gen->m_ir, elem_addr, elem_type);
                gen->push_vreg(val);
                gen->m_tstack.back() = elem_type;
                return;
            }
            // Evaluate index for globals
            gen->gen_expr(*expr_index->index);
            uint32_t idx = gen->pop_vreg();

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
            gen->require_not_float(expr_source_loc(*bit_not->expr), "the operand of '~'");
            uint32_t src = gen->pop_vreg();
            uint32_t v = gen->m_ir.not_(IRValue::vreg(src));
            gen->push_vreg(v);
        }

        void operator()(const NodeExprLogNot* log_not)
        {
            gen->gen_expr(*log_not->expr);
            gen->require_not_float(expr_source_loc(*log_not->expr), "the operand of '!'");
            uint32_t src = gen->pop_vreg();
            uint32_t v = gen->m_ir.cmp_eq(IRValue::vreg(src), IRValue::imm_i64(0));
            gen->push_vreg(v);
        }

        void operator()(const NodeExprTernary* ternary)
        {
            gen->gen_expr(*ternary->cond);
            gen->require_not_float(expr_source_loc(*ternary->cond), "a condition");
            uint32_t cond = gen->pop_vreg();

            auto label_else = gen->new_label();
            auto label_end = gen->new_label();
            auto label_then = gen->new_label();

            // Both arms must write their result to the same vreg, and that vreg
            // needs a dominating definition in the entry block: a def in only
            // one arm does not dominate the join. (The old code left two
            // arm-local vregs on the vstack and used the else one in the join;
            // it only worked because the allocator happened to give both the
            // same physical register.)
            uint32_t result = gen->m_ir.load_i64(0);

            // br(cond, true_label, false_label): if cond != 0 → true_label
            gen->m_ir.br(IRValue::vreg(cond), label_then, label_else);

            // Then branch
            gen->m_ir.new_block(label_then);
            gen->gen_expr(*ternary->then_expr);
            if (gen->peek_type().kind == Type::Float) {
                // A float result travels through a frame temporary: the arms cannot share a
                // vreg without knowing its class before the branch.
                const Type result_type = gen->peek_type();
                uint32_t slot = gen->alloc_object(8);
                gen->store_float_top(gen->object_addr(slot, 8), result_type.float_type);
                gen->m_ir.jmp(label_end);

                gen->m_ir.new_block(label_else);
                gen->gen_expr(*ternary->else_expr);
                gen->coerce_float_top(result_type, expr_source_loc(*ternary->else_expr),
                                      "the arms of a conditional expression");
                gen->store_float_top(gen->object_addr(slot, 8), result_type.float_type);

                gen->m_ir.new_block(label_end);
                gen->push_vreg(gen->m_ir.fload(
                    result_type.float_type == FloatType::f32 ? IRWidth::F32 : IRWidth::F64,
                    IRValue::vreg(gen->object_addr(slot, 8)), 0));
                gen->m_tstack.back() = result_type;
                return;
            }
            uint32_t then_v = gen->pop_vreg();
            gen->m_ir.emit(IRInstruction::copy(result, IRValue::vreg(then_v)));
            gen->m_ir.jmp(label_end);

            // Else branch
            gen->m_ir.new_block(label_else);
            gen->gen_expr(*ternary->else_expr);
            Type result_type = gen->peek_type();
            if (result_type.kind == Type::Float) {
                gen->m_ir.new_block(label_end);   // keep the partial IR well formed
                gen->fail(expr_source_loc(*ternary->else_expr),
                          "the arms of a conditional expression must both be floats or both integers "
                          "(write the first arm as a float, e.g. '0.0')");
            }
            uint32_t else_v = gen->pop_vreg();
            gen->m_ir.emit(IRInstruction::copy(result, IRValue::vreg(else_v)));

            // End
            gen->m_ir.new_block(label_end);
            gen->push_vreg(result);
            gen->m_tstack.back() = result_type;
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
                            // read_str writes forward from the lowest address.
                            uint32_t addr = gen->var_addr(var);
                            int64_t byte_size = static_cast<int64_t>(var.array_size * var.elem_size);
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
            Type field_type;
            uint32_t field_addr = gen->field_addr(*field_access, field_type);
            // A nested struct field is a struct value: represented by its address.
            uint32_t val = field_type.kind == Type::Struct
                               ? field_addr
                               : load_typed_field(gen->m_ir, field_addr, field_type);
            gen->push_vreg(val);
            gen->m_tstack.back() = field_type;
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
                        uint32_t addr = gen->var_addr(var);
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
                if (const NodeFuncDef* func = gen->find_func(name)) {
                    // &func — the address of a defined function, typed fn(params) -> ret.
                    uint32_t addr = gen->m_ir.lea_label(name);
                    gen->push_vreg(addr);
                    gen->m_tstack.back() = gen->function_pointer_type(*func);
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
                        uint32_t base_addr = gen->var_addr(var);
                        uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx),
                            IRValue::imm_i64(static_cast<int64_t>(var.elem_size)));
                        uint32_t elem_addr = gen->m_ir.add(IRValue::vreg(base_addr), IRValue::vreg(offset));
                        gen->push_vreg(elem_addr);
                        Type* pointee = gen->alloc_type(var.type);
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
                uint32_t base_addr = gen->var_addr(var);
                uint32_t field_addr = gen->m_ir.lea(IRValue::vreg(base_addr), static_cast<int64_t>(field_offset));
                gen->push_vreg(field_addr);
                size_t index = 0;
                for (; index < struct_info.value().field_names.size(); index++) {
                    if (struct_info.value().field_names[index] == field_name) break;
                }
                Type field_type = index < struct_info.value().field_types.size()
                                      ? struct_info.value().field_types[index]
                                      : Type::make_int(IntType::i64);
                Type* pointee = gen->alloc_type(field_type);
                gen->m_tstack.back() = Type::make_ptr(pointee);
            } else {
                gen->fail(addr_of->ampersand.loc, "Cannot take address of this expression");
            }
        }

        void operator()(const NodeExprStructLit*) const
        {
            gen->fail({}, "struct literal is only valid as a struct initializer");
        }

        void operator()(const NodeExprCast* cast) const
        {
            gen->gen_expr(*cast->expr);
            Type from = gen->peek_type();
            uint32_t v = gen->pop_vreg();
            Type to = cast->target;
            if (to.kind == Type::Float) {
                if (from.kind != Type::Int && from.kind != Type::Float) {
                    gen->fail(cast->loc, "cannot cast " + type_name(from) + " to " + type_name(to));
                }
                v = gen->to_float(v, from, to.float_type, cast->loc, /*allow_narrow=*/true);
                gen->push_vreg(v);
                gen->m_tstack.back() = to;
                return;
            }
            if (from.kind == Type::Float) {
                if (to.kind != Type::Int) {
                    gen->fail(cast->loc, "cannot cast " + type_name(from) + " to " + type_name(to));
                }
                // Truncates toward zero; the result is then narrowed like any integer cast.
                v = gen->m_ir.f2i(IRValue::fvreg(v, from.float_type == FloatType::f32 ? IRWidth::F32
                                                                                       : IRWidth::F64));
                const size_t dst_sz = type_size(to);
                if (dst_sz <= 1)      v = gen->m_ir.and_(IRValue::vreg(v), IRValue::imm_i64(0xFF));
                else if (dst_sz <= 2) v = gen->m_ir.and_(IRValue::vreg(v), IRValue::imm_i64(0xFFFF));
                else if (dst_sz <= 4) v = gen->m_ir.zext(IRValue::vreg(v), IRWidth::I32);
                if (to.int_type == IntType::i8 || to.int_type == IntType::i16 || to.int_type == IntType::i32) {
                    v = gen->m_ir.sext(IRValue::vreg(v), width_of_int(to.int_type));
                }
                gen->push_vreg(v);
                gen->m_tstack.back() = to;
                return;
            }
            if (from.kind == Type::Int && to.kind == Type::Int) {
                size_t src_sz = type_size(from), dst_sz = type_size(to);
                if (dst_sz < src_sz) {
                    if (dst_sz <= 1)      v = gen->m_ir.and_(IRValue::vreg(v), IRValue::imm_i64(0xFF));
                    else if (dst_sz <= 2) v = gen->m_ir.and_(IRValue::vreg(v), IRValue::imm_i64(0xFFFF));
                    else                  v = gen->m_ir.zext(IRValue::vreg(v), IRWidth::I32);
                } else if (dst_sz > src_sz) {
                    v = int_signed(from.int_type)
                            ? gen->m_ir.sext(IRValue::vreg(v), width_of_int(from.int_type))
                            : gen->m_ir.zext(IRValue::vreg(v), width_of_int(from.int_type));
                }
            }
            gen->push_vreg(v);
            gen->m_tstack.back() = to;
        }

        void operator()(const NodeExprSizeof* sz) const
        {
            auto size = gen->sizeof_value(*sz);
            if (!size.has_value()) {
                gen->fail(sz->loc, "Invalid operand to sizeof");
            }
            uint32_t v = gen->m_ir.load_i64(size.value());
            gen->push_vreg(v);
        }

        void operator()(const NodeExprDeref* deref) const
        {
            // *ptr — evaluate ptr, then load from that address
            gen->gen_expr(*deref->expr);
            Type ptr_type = gen->peek_type();
            uint32_t ptr = gen->pop_vreg();
            if (gen->m_null_check) {
                const uint32_t nonnull = gen->m_ir.cmp_ne(
                    IRValue::vreg(ptr), IRValue::imm_i64(0));
                gen->emit_runtime_check(IRValue::vreg(nonnull), "null dereference\n");
            }

            if (ptr_type.kind == Type::Ptr && ptr_type.pointee && ptr_type.pointee->kind == Type::Float) {
                gen->push_vreg(load_typed_field(gen->m_ir, ptr, *ptr_type.pointee));
                gen->m_tstack.back() = *ptr_type.pointee;
                return;
            }
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

            if (const SysBuiltin* sys = sys_builtin_for(fname)) {
                if (expr_call->args.size() != sys->arity) {
                    gen->fail(expr_call->name.loc,
                              fname + "() takes exactly " + std::to_string(sys->arity)
                              + " argument(s), got " + std::to_string(expr_call->args.size()));
                }
                for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                    gen->gen_expr(**it);
                    gen->require_not_float(expr_source_loc(**it), "a builtin argument");
                }
                std::vector<IRValue> argv;
                argv.reserve(sys->arity);
                for (size_t i = 0; i < sys->arity; i++) {
                    argv.push_back(IRValue::vreg(gen->pop_vreg()));
                }
                uint32_t result = gen->m_ir.call(sys->runtime_name, argv);
                gen->push_vreg(result);
                gen->m_tstack.back() = Type::make_int(IntType::i64);
                return;
            }
            if (fname == "exit") {
                if (expr_call->args.size() != 1) {
                    gen->fail(expr_call->name.loc,
                              "exit() takes exactly 1 argument, got "
                              + std::to_string(expr_call->args.size()));
                }
                gen->gen_expr(*expr_call->args[0]);
                gen->require_not_float(expr_source_loc(*expr_call->args[0]), "a builtin argument");
                uint32_t code = gen->pop_vreg();
                gen->m_ir.call("_sodium_exit", {IRValue::vreg(code)});
                return;
            }
            if (fname == "envp") {
                if (expr_call->args.size() != 1) {
                    gen->fail(expr_call->name.loc,
                              "envp() takes exactly 1 argument (the index), got "
                              + std::to_string(expr_call->args.size()));
                }
                gen->gen_expr(*expr_call->args[0]);
                gen->require_not_float(expr_source_loc(*expr_call->args[0]), "a builtin argument");
                uint32_t idx = gen->pop_vreg();
                uint32_t addr = gen->m_ir.lea_label("__sodium_envp");
                uint32_t envp_ptr = gen->m_ir.load(IRValue::vreg(addr), 0);
                uint32_t offset = gen->m_ir.mul(IRValue::vreg(idx), IRValue::imm_i64(8));
                uint32_t entry = gen->m_ir.add(IRValue::vreg(envp_ptr), IRValue::vreg(offset));
                uint32_t str = gen->m_ir.load(IRValue::vreg(entry), 0);
                gen->push_vreg(str);
                Type* pointee = gen->alloc_type(Type::make_int(IntType::u8));
                gen->m_tstack.back() = Type::make_ptr(pointee);
                return;
            }
            if (fname == "malloc") {
                // Call _sodium_malloc with the size argument
                for (auto it = expr_call->args.rbegin(); it != expr_call->args.rend(); ++it) {
                    gen->gen_expr(**it);
                    gen->require_not_float(expr_source_loc(**it), "a builtin argument");
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
                    gen->require_not_float(expr_source_loc(**it), "a builtin argument");
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
                gen->require_not_float(expr_source_loc(*expr_call->args[0]), "a builtin argument");
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
            // The callee is a function-pointer variable (D-014) or a defined function.
            const NodeFuncDef* callee = nullptr;
            CallTarget target;
            target.name = fname;
            std::vector<std::optional<Type>> sig_params;   // signature of an indirect call
            std::optional<Type> sig_ret;
            auto is_fn_ptr = [](const Type& t) {
                return t.kind == Type::Ptr && t.pointee && t.pointee->kind == Type::Func;
            };
            const Var* fn_var = expr_call->callee ? nullptr : gen->find_var(fname);
            const bool indirect = expr_call->callee || (fn_var && is_fn_ptr(fn_var->type));
            if (indirect) {
                Type callee_type;
                if (expr_call->callee) {
                    // `s.cb(x)`, `table[i](x)`, `f(1)(2)`: the pointer is an expression.
                    gen->gen_expr(*expr_call->callee);
                    callee_type = gen->peek_type();
                    target.pointer = gen->pop_vreg();
                    if (!is_fn_ptr(callee_type)) {
                        gen->fail(expr_call->name.loc, "called object is not a function pointer (it has type "
                                  + type_name(callee_type) + ")");
                    }
                } else {
                    callee_type = fn_var->type;
                    gen->load_var_to_vstack(fname);
                    target.pointer = gen->pop_vreg();
                }
                const Type& sig = *callee_type.pointee;
                for (uint32_t i = 0; i < sig.fn_nparams; i++) sig_params.push_back(sig.fn_params[i]);
                if (sig.fn_ret) sig_ret = *sig.fn_ret;
            } else {
                if (!gen->m_func_names.contains(fname)) {
                    std::string suggestion = gen->did_you_mean(fname);
                    if (!suggestion.empty()) {
                        gen->fail_with_suggestion(expr_call->name.loc, "Undefined function: " + fname + " (did you mean '" + suggestion + "'?)", suggestion);
                    } else {
                        gen->fail(expr_call->name.loc, "Undefined function: " + fname);
                    }
                }
                callee = gen->find_func(fname);
            }
            const std::vector<std::optional<Type>>& param_types =
                indirect ? sig_params : callee->param_types;
            const std::optional<Type>& return_type = indirect ? sig_ret : callee->return_type;

            // Collect arguments (in evaluation order)
            for (size_t i = 0; i < expr_call->args.size(); i++) {
                gen->gen_expr(*expr_call->args[i]);
            }
            if (indirect) {
                gen->check_call_args(*expr_call, expr_call->name.loc, fname, param_types, param_types.size());
            } else if (gen->has_declared_param_types(fname)) {
                gen->check_call_args(*expr_call, expr_call->name.loc, fname, param_types,
                                     callee->params.size());
            }
            // Pop in reverse to get evaluation order
            const size_t n_args = expr_call->args.size();
            std::vector<uint32_t> arg_vregs(n_args);
            std::vector<Type> arg_types(n_args);
            for (size_t i = n_args; i-- > 0;) {
                arg_types[i] = gen->peek_type();
                arg_vregs[i] = gen->pop_vreg();
            }
            // Argument words are placed by the target ABI (D-012): scalars take one word,
            // structs up to 16 bytes take their 8-byte words, larger ones go in memory or by
            // reference. Register words come first in `args`, then the stack words in order.
            ArgPlacer placer(*gen->m_tri_ptr);
            std::vector<IRValue> reg_words;
            std::vector<IRValue> stack_words;
            auto push_value = [&](const ArgPlacement& place, IRValue value) {
                (place.on_stack ? stack_words : reg_words).push_back(value);
            };
            auto push_word = [&](const ArgPlacement& place, uint32_t vreg) {
                push_value(place, IRValue::vreg(vreg));
            };

            // A struct result larger than 16 bytes comes back through a buffer whose
            // address is passed as the hidden first argument.
            std::optional<StructInfo> ret_info;
            std::optional<uint32_t> hidden_result;
            if (return_type && return_type->kind == Type::Struct) {
                const std::string& rname = return_type->struct_name;
                ret_info = gen->get_struct_info(rname);
                if (!ret_info.has_value()) gen->fail(expr_call->name.loc, "Unknown struct type: " + rname);
                if (ret_info->size > 16) {
                    uint32_t slot = gen->alloc_object(ret_info->size);
                    hidden_result = gen->object_addr(slot, Generator::align_frame(ret_info->size, 8));
                    push_word(placer.scalar(), *hidden_result);
                }
            }
            for (size_t i = 0; i < n_args; i++) {
                if (arg_types[i].kind == Type::Float) {
                    if (i >= param_types.size() || !param_types[i]) {
                        gen->fail(expr_call->name.loc, "argument " + std::to_string(i + 1) + " to '"
                                  + fname + "' is a float but the parameter has no declared type");
                    }
                    push_value(placer.fp_scalar(),
                               IRValue::fvreg(arg_vregs[i], arg_types[i].float_type == FloatType::f32
                                                                ? IRWidth::F32 : IRWidth::F64));
                    continue;
                }
                if (arg_types[i].kind != Type::Struct) {
                    push_word(placer.scalar(), arg_vregs[i]);
                    continue;
                }
                const bool wants_struct = i < param_types.size()
                    && param_types[i] && param_types[i]->kind == Type::Struct;
                if (!wants_struct) {
                    gen->fail(expr_call->name.loc, "argument " + std::to_string(i + 1) + " to '"
                              + fname + "' is a struct but the parameter is not declared as one");
                }
                const std::string& sname = arg_types[i].struct_name;
                auto info = gen->get_struct_info(sname);
                if (!info.has_value()) gen->fail(expr_call->name.loc, "Unknown struct type: " + sname);
                const StructPlan plan = gen->plan_struct_arg(placer, *info);
                const std::vector<ArgPlacement>& places = plan.places;
                if (info->size > 16 && places.size() == 1) {
                    // By reference to a caller-made copy.
                    uint32_t slot = gen->alloc_object(info->size);
                    uint32_t copy = gen->object_addr(slot, Generator::align_frame(info->size, 8));
                    gen->copy_memory(copy, arg_vregs[i], info->size, info->align);
                    push_word(places[0], copy);
                    continue;
                }
                for (size_t w = 0; w < places.size(); w++) {
                    if (plan.pieces.empty()) {
                        push_word(places[w], gen->load_struct_word(arg_vregs[i], *info, w));
                    } else {
                        const StructPiece& piece = plan.pieces[w];
                        push_value(places[w], gen->struct_piece_value(
                            gen->load_struct_piece(arg_vregs[i], *info, piece), piece));
                    }
                }
            }
            const int reg_count = static_cast<int>(reg_words.size());
            std::vector<IRValue> args = reg_words;
            args.insert(args.end(), stack_words.begin(), stack_words.end());

            if (ret_info) {
                gen->gen_struct_call(target, args, return_type->struct_name, *ret_info,
                                     hidden_result, reg_count);
                return;
            }

            uint32_t result = (return_type && return_type->kind == Type::Float)
                ? gen->emit_call_f(target, args,
                                   return_type->float_type == FloatType::f32 ? IRWidth::F32 : IRWidth::F64,
                                   reg_count)
                : gen->emit_call(target, args, reg_count);
            gen->push_vreg(result);
            // A call yields its declared return type (so a returned function pointer can be
            // passed on or called); unannotated functions keep the legacy i64 result.
            if (return_type) gen->m_tstack.back() = *return_type;
        }

        void operator()(const BinExpr* bin_expr)
        {
            struct BinVisitor {
                Generator* gen;
                void operator()(const BinExprAdd* add) {
                    gen->gen_expr(*add->lhs);
                    gen->gen_expr(*add->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_arith(IROpcode::FADD, expr_source_loc(*add->lhs), "+");
                        return;
                    }
                    // pop_vreg() pops the matching type as well; peek it first.
                    Type type_b = gen->peek_type();
                    uint32_t b = gen->pop_vreg();
                    Type type_a = gen->peek_type();
                    uint32_t a = gen->pop_vreg();

                    if (type_a.kind == Type::Ptr && type_a.pointee &&
                        type_b.kind == Type::Int) {
                        // ptr + int: scale the int by sizeof(*ptr)
                        size_t elem_size = pointee_size(gen, *type_a.pointee);
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
                        size_t elem_size = pointee_size(gen, *type_b.pointee);
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
                        // int + int — result keeps the wider operand type
                        uint32_t v = gen->m_ir.add(IRValue::vreg(a), IRValue::vreg(b));
                        gen->push_vreg(v);
                        gen->m_tstack.back() = int_arith_type(type_a, type_b);
                    }
                }
                void operator()(const BinExprMulti* mul) {
                    gen->gen_expr(*mul->lhs);
                    gen->gen_expr(*mul->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_arith(IROpcode::FMUL, expr_source_loc(*mul->lhs), "*");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.mul(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprSub* sub) {
                    gen->gen_expr(*sub->lhs);
                    gen->gen_expr(*sub->rhs);
                    if (gen->top_two_have_float()) {
                        // Unary minus is parsed as `0 - x` with a location-less zero: negate.
                        const auto* zero = std::get_if<NodeExprIntLit*>(&sub->lhs->var);
                        if (zero && (*zero)->int_lit.loc.line == 0 &&
                            gen->m_tstack.back().kind == Type::Float) {
                            const Type t = gen->m_tstack.back();
                            const uint32_t v = gen->pop_vreg();
                            gen->pop_vreg();   // the synthetic zero
                            gen->push_vreg(v);
                            gen->m_tstack.back() = t;
                            gen->gen_float_neg();
                            return;
                        }
                        gen->gen_float_arith(IROpcode::FSUB, expr_source_loc(*sub->lhs), "-");
                        return;
                    }
                    // pop_vreg() pops the matching type as well; peek it first.
                    Type type_b = gen->peek_type();
                    uint32_t b = gen->pop_vreg();
                    Type type_a = gen->peek_type();
                    uint32_t a = gen->pop_vreg();

                    if (type_a.kind == Type::Ptr && type_a.pointee &&
                        type_b.kind == Type::Int) {
                        // ptr - int: scale the int by sizeof(*ptr)
                        size_t elem_size = pointee_size(gen, *type_a.pointee);
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
                        size_t elem_size = pointee_size(gen, *type_a.pointee);
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
                        // int - int — result keeps the wider operand type
                        uint32_t v = gen->m_ir.sub(IRValue::vreg(a), IRValue::vreg(b));
                        gen->push_vreg(v);
                        gen->m_tstack.back() = int_arith_type(type_a, type_b);
                    }
                }
                void operator()(const BinExprDiv* div) {
                    gen->gen_expr(*div->lhs);
                    gen->gen_expr(*div->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_arith(IROpcode::FDIV, expr_source_loc(*div->lhs), "/");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.div(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprMod* mod) {
                    gen->gen_expr(*mod->lhs);
                    gen->gen_expr(*mod->rhs);
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*mod->lhs), "operator '%' is not defined for floats");
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.mod(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprLT* lt) {
                    gen->gen_expr(*lt->lhs);
                    gen->gen_expr(*lt->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_compare(IROpcode::FCMP_LT, expr_source_loc(*lt->lhs), "<");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_lt(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprGT* gt) {
                    gen->gen_expr(*gt->lhs);
                    gen->gen_expr(*gt->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_compare(IROpcode::FCMP_GT, expr_source_loc(*gt->lhs), ">");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_gt(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprEQ* eq) {
                    gen->gen_expr(*eq->lhs);
                    gen->gen_expr(*eq->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_compare(IROpcode::FCMP_EQ, expr_source_loc(*eq->lhs), "==");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_eq(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprNEQ* neq) {
                    gen->gen_expr(*neq->lhs);
                    gen->gen_expr(*neq->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_compare(IROpcode::FCMP_NE, expr_source_loc(*neq->lhs), "!=");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_ne(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprLTE* lte) {
                    gen->gen_expr(*lte->lhs);
                    gen->gen_expr(*lte->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_compare(IROpcode::FCMP_LE, expr_source_loc(*lte->lhs), "<=");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_le(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprGTE* gte) {
                    gen->gen_expr(*gte->lhs);
                    gen->gen_expr(*gte->rhs);
                    if (gen->top_two_have_float()) {
                        gen->gen_float_compare(IROpcode::FCMP_GE, expr_source_loc(*gte->lhs), ">=");
                        return;
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.cmp_ge(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprAnd* and_op) {
                    gen->gen_expr(*and_op->lhs);
                    gen->gen_expr(*and_op->rhs);
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*and_op->lhs), "operator '&&' is not defined for floats");
                    }
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
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*or_op->lhs), "operator '||' is not defined for floats");
                    }
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
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*and_op->lhs), "operator '&' is not defined for floats");
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.and_(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprBitOr* or_op) {
                    gen->gen_expr(*or_op->lhs);
                    gen->gen_expr(*or_op->rhs);
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*or_op->lhs), "operator '|' is not defined for floats");
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.or_(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprXor* xor_op) {
                    gen->gen_expr(*xor_op->lhs);
                    gen->gen_expr(*xor_op->rhs);
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*xor_op->lhs), "operator '^' is not defined for floats");
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.xor_(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprShl* shl_op) {
                    gen->gen_expr(*shl_op->lhs);
                    gen->gen_expr(*shl_op->rhs);
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*shl_op->lhs), "operator '<<' is not defined for floats");
                    }
                    uint32_t b = gen->pop_vreg();
                    uint32_t a = gen->pop_vreg();
                    uint32_t v = gen->m_ir.shl(IRValue::vreg(a), IRValue::vreg(b));
                    gen->push_vreg(v);
                }
                void operator()(const BinExprShr* shr_op) {
                    gen->gen_expr(*shr_op->lhs);
                    gen->gen_expr(*shr_op->rhs);
                    if (gen->top_two_have_float()) {
                        gen->fail(expr_source_loc(*shr_op->lhs), "operator '>>' is not defined for floats");
                    }
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

uint32_t Generator::field_addr(const NodeExprFieldAccess& access, Type& out_type) {
    uint32_t base_addr;
    std::string struct_name;

    if (access.base) {
        if (auto* inner = std::get_if<NodeExprFieldAccess*>(&access.base->var)) {
            Type inner_type;
            base_addr = field_addr(**inner, inner_type);
            if (inner_type.kind != Type::Struct) {
                fail(access.field_name.loc, "Field access requires a struct value");
            }
            struct_name = inner_type.struct_name;
        } else if (auto* indexed = std::get_if<NodeExprIndex*>(&access.base->var)) {
            Type element_type;
            base_addr = index_addr(**indexed, element_type);
            if (element_type.kind != Type::Struct) {
                fail(access.field_name.loc, "Field access requires an array of structs");
            }
            struct_name = element_type.struct_name;
        } else {
            fail(access.field_name.loc, "Unsupported field access base");
        }
    } else {
        auto var = lookup_var(access.obj_name.value.value(), access.obj_name.loc);
        if (access.arrow) {
            if (var.type.kind != Type::Ptr || !var.type.pointee ||
                var.type.pointee->kind != Type::Struct) {
                fail(access.obj_name.loc, "Not a pointer to struct: " + access.obj_name.value.value());
            }
            struct_name = var.type.pointee->struct_name;
            load_var_to_vstack(access.obj_name.value.value());
            base_addr = pop_vreg();
        } else {
            if (var.struct_type.empty()) {
                fail(access.obj_name.loc, "Not a struct variable: " + access.obj_name.value.value());
            }
            struct_name = var.struct_type;
            base_addr = var_addr(var);
        }
    }

    auto info = get_struct_info(struct_name);
    if (!info.has_value()) fail(access.field_name.loc, "Unknown struct type: " + struct_name);
    auto offset_it = info.value().field_offsets.find(access.field_name.value.value());
    if (offset_it == info.value().field_offsets.end()) {
        fail(access.field_name.loc, "Unknown field '" + access.field_name.value.value()
             + "' in struct '" + struct_name + "'");
    }
    size_t index = 0;
    for (; index < info.value().field_names.size(); index++) {
        if (info.value().field_names[index] == access.field_name.value.value()) break;
    }
    out_type = index < info.value().field_types.size()
                   ? info.value().field_types[index]
                   : Type::make_int(IntType::i64);
    return m_ir.lea(IRValue::vreg(base_addr), static_cast<int64_t>(offset_it->second));
}


uint32_t Generator::index_addr(const NodeExprIndex& index, Type& out_type) {
    const auto& name = index.name.value.value();
    Var var = lookup_var(name, index.name.loc);
    if (var.array_size == 0) fail(index.name.loc, "Indexing a non-array variable: " + name);

    std::vector<NodeExpr*> indices;
    const NodeExprIndex* cur = &index;
    while (cur) {
        indices.push_back(cur->index);
        if (!cur->base) break;
        auto* inner = std::get_if<NodeExprIndex*>(&cur->base->var);
        if (!inner) break;
        cur = *inner;
    }
    std::reverse(indices.begin(), indices.end());

    std::vector<size_t> dims = var.dim_sizes.empty()
                                   ? std::vector<size_t>{var.array_size}
                                   : var.dim_sizes;
    std::vector<size_t> strides(dims.size(), var.elem_size);
    for (size_t k = dims.size(); k-- > 1;) strides[k - 1] = strides[k] * dims[k];

    uint32_t offset = 0;
    bool have_offset = false;
    const size_t used = std::min(indices.size(), dims.size());
    for (size_t k = 0; k < used; k++) {
        gen_expr(*indices[k]);
        require_not_float(index.name.loc, "an array index");
        uint32_t v = pop_vreg();
        if (m_bounds_check) {
            const uint32_t lower = m_ir.cmp_ge(IRValue::vreg(v), IRValue::imm_i64(0));
            const uint32_t upper = m_ir.cmp_lt(
                IRValue::vreg(v), IRValue::imm_i64(static_cast<int64_t>(dims[k])));
            const uint32_t in_range = m_ir.and_(IRValue::vreg(lower), IRValue::vreg(upper));
            emit_runtime_check(IRValue::vreg(in_range), "index out of bounds\n");
        }
        uint32_t scaled = m_ir.mul(IRValue::vreg(v),
                                   IRValue::imm_i64(static_cast<int64_t>(strides[k])));
        offset = have_offset ? m_ir.add(IRValue::vreg(offset), IRValue::vreg(scaled)) : scaled;
        have_offset = true;
    }

    uint32_t base = var_addr(var);
    out_type = var.type;
    return have_offset ? m_ir.add(IRValue::vreg(base), IRValue::vreg(offset)) : base;
}
