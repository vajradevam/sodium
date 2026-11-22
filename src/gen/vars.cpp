#include "../generation.hpp"

// ==========================================================================
// Variable load/store helpers
// ==========================================================================

void Generator::load_var_to_vstack(const std::string& name) {
    // Try local scope first
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->vars.contains(name)) {
            it->vars.at(name).used = true;
            const auto& var = it->vars.at(name);
            if (var.array_size > 0) {
                uint32_t addr = m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                uint32_t val = m_ir.load(IRValue::vreg(addr), 0);
                push_vreg(val);
                m_tstack.back() = Type::make_int(IntType::i64);
                return;
            } else if (var.type.kind == Type::Ptr || var.type.int_type == IntType::i64 || var.type.int_type == IntType::u64) {
                uint32_t addr = m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                uint32_t val = m_ir.load(IRValue::vreg(addr), 0);
                push_vreg(val);
                m_tstack.back() = var.type;
                return;
            } else {
                uint32_t addr = m_ir.frame_addr(static_cast<int64_t>(var.stack_loc));
                // Use a correctly-sized load to avoid reading adjacent variables
                uint32_t val;
                size_t sz = type_size(var.type);
                bool is_signed = (var.type.int_type == IntType::i8 ||
                                  var.type.int_type == IntType::i16 ||
                                  var.type.int_type == IntType::i32);
                if (sz <= 1) {
                    val = is_signed ? m_ir.load_s8(IRValue::vreg(addr), 0)
                                    : m_ir.load_u8(IRValue::vreg(addr), 0);
                } else if (sz <= 2) {
                    val = is_signed ? m_ir.load_s16(IRValue::vreg(addr), 0)
                                    : m_ir.load_u16(IRValue::vreg(addr), 0);
                } else if (sz <= 4) {
                    val = is_signed ? m_ir.load_s32(IRValue::vreg(addr), 0)
                                    : m_ir.load_u32(IRValue::vreg(addr), 0);
                } else {
                    val = m_ir.load(IRValue::vreg(addr), 0);
                }
                push_vreg(val);
                m_tstack.back() = var.type;
                return;
            }
        }
    }
    // Try global
    if (m_globals.contains(name)) {
        uint32_t val = m_ir.lea_label(name);
        uint32_t loaded = m_ir.load(IRValue::vreg(val), 0);
        push_vreg(loaded);
        m_tstack.back() = Type::make_int(IntType::i64);
        return;
    }
    // Try constant
    if (m_constants.contains(name)) {
        uint32_t val = m_ir.load_i64(m_constants.at(name));
        push_vreg(val);
        m_tstack.back() = Type::make_int(IntType::i64);
        return;
    }
    std::string suggestion = did_you_mean(name);
    if (!suggestion.empty()) {
        fail_with_suggestion({}, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)", suggestion);
    } else {
        fail({}, "Undeclared identifier: " + name);
    }
}

void Generator::store_var_from_vstack(const std::string& name, AssignOp op) {
    // Compound assignment: we need to read old value, apply op, store back.
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->vars.contains(name)) {
            const auto& var = it->vars.at(name);
            if (var.array_size > 0) {
                fail({}, "Cannot assign to array variable directly (use arr = [a,b,c] or arr[i] = val)");
            }

            bool is_ptr_type = (var.type.kind == Type::Ptr);
            bool is_64bit_int = (var.type.kind == Type::Int &&
                                 (var.type.int_type == IntType::i64 || var.type.int_type == IntType::u64));

            auto typed_store = [&](uint32_t addr, uint32_t val) {
                if (is_ptr_type || is_64bit_int) {
                    m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                } else {
                    size_t sz = type_size(var.type);
                    if (sz <= 1)      m_ir.store_8(IRValue::vreg(addr), 0, IRValue::vreg(val));
                    else if (sz <= 2) m_ir.store_16(IRValue::vreg(addr), 0, IRValue::vreg(val));
                    else if (sz <= 4) m_ir.store_32(IRValue::vreg(addr), 0, IRValue::vreg(val));
                    else              m_ir.store(IRValue::vreg(addr), 0, IRValue::vreg(val));
                }
            };

            if (op == AssignOp::assign) {
                uint32_t val = pop_vreg();
                if (!is_ptr_type && !is_64bit_int) {
                    // Truncate to variable's type width
                    switch (var.type.int_type) {
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
                typed_store(addr, val);
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
                    case AssignOp::shr_assign:     result = m_ir.ashr(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                    default: break;
                }
                // Truncate if needed
                if (!is_ptr_type && !is_64bit_int) {
                    switch (var.type.int_type) {
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
                typed_store(addr, result);
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
                case AssignOp::shr_assign:     result = m_ir.ashr(IRValue::vreg(lhs), IRValue::vreg(rhs)); break;
                default: break;
            }
            m_ir.store(IRValue::vreg(m_ir.lea_label(name)), 0, IRValue::vreg(result));
        }
        return;
    }
    if (m_constants.contains(name)) {
        fail({}, "Cannot assign to constant '" + name + "'");
    }
    std::string suggestion = did_you_mean(name);
    if (!suggestion.empty()) {
        fail_with_suggestion({}, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)", suggestion);
    } else {
        fail({}, "Undeclared identifier: " + name);
    }
}
