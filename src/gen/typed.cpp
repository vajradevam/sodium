#include "../generation.hpp"

#include <algorithm>
#include <cstdint>

// ==========================================================================
// Typed signatures: declared parameter types and call-site checking (Y1b).
// ==========================================================================

namespace {

std::string type_name(const Type& t) {
    if (t.kind == Type::Struct) return t.struct_name;
    if (t.kind == Type::Ptr) {
        return t.pointee ? type_name(*t.pointee) + "*" : "?*";
    }
    switch (t.int_type) {
        case IntType::i8:  return "i8";
        case IntType::u8:  return "u8";
        case IntType::i16: return "i16";
        case IntType::u16: return "u16";
        case IntType::i32: return "i32";
        case IntType::u32: return "u32";
        case IntType::i64: return "i64";
        case IntType::u64: return "u64";
    }
    return "?";
}

bool is_signed(IntType t) {
    return t == IntType::i8 || t == IntType::i16 ||
           t == IntType::i32 || t == IntType::i64;
}

IRWidth width_of(IntType t) {
    switch (t) {
        case IntType::i8:  case IntType::u8:  return IRWidth::I8;
        case IntType::i16: case IntType::u16: return IRWidth::I16;
        case IntType::i32: case IntType::u32: return IRWidth::I32;
        case IntType::i64: case IntType::u64: return IRWidth::I64;
    }
    return IRWidth::I64;
}

bool constant_fits(int64_t v, IntType target) {
    switch (target) {
        case IntType::i8:  return v >= -128 && v <= 127;
        case IntType::u8:  return v >= 0 && v <= 255;
        case IntType::i16: return v >= -32768 && v <= 32767;
        case IntType::u16: return v >= 0 && v <= 65535;
        case IntType::i32: return v >= INT32_MIN && v <= INT32_MAX;
        case IntType::u32: return v >= 0 && v <= static_cast<int64_t>(UINT32_MAX);
        case IntType::i64: return true;
        case IntType::u64: return v >= 0;
    }
    return false;
}

bool safe_widening(IntType from, IntType to) {
    if (type_size(Type::make_int(from)) >= type_size(Type::make_int(to))) return false;
    if (is_signed(from) == is_signed(to)) return true;
    // Unsigned -> wider signed is always value-preserving.
    if (!is_signed(from) && is_signed(to)) return true;
    // Signed -> wider unsigned is not: negative values would wrap.
    return false;
}

} // namespace

const NodeFuncDef* Generator::find_func(const std::string& name) const {
    for (const auto* func : m_prog.funcs) {
        if (func->name.value.value() == name) return func;
    }
    return nullptr;
}

bool Generator::has_declared_param_types(const std::string& name) const {
    const NodeFuncDef* func = find_func(name);
    if (!func) return false;
    return std::any_of(func->param_types.begin(), func->param_types.end(),
                       [](const std::optional<Type>& t) { return t.has_value(); });
}

void Generator::check_call_args(const NodeExprCall& call, SourceLoc loc) {
    const std::string& name = call.name.value.value();
    const NodeFuncDef* func = find_func(name);
    if (!func || !has_declared_param_types(name)) return; // builtin / untyped: legacy path

    if (call.args.size() != func->params.size()) {
        fail(loc, "wrong number of arguments to '" + name + "': expected "
                  + std::to_string(func->params.size()) + ", got "
                  + std::to_string(call.args.size()));
    }

    const size_t n = call.args.size();
    if (n == 0 || n > m_tstack.size() || n > m_vstack.size()) return;
    const size_t base = m_tstack.size() - n;

    for (size_t i = 0; i < n; i++) {
        if (i >= func->param_types.size() || !func->param_types[i]) continue;
        const Type want = *func->param_types[i];
        const Type got = m_tstack[base + i];
        if (want == got) continue;

        auto constant = eval_const_expr(call.args[i]);
        if (constant.has_value() && want.kind == Type::Int &&
            constant_fits(constant.value(), want.int_type)) {
            continue; // literal fits: no conversion needed
        }

        if (want.kind == Type::Int && got.kind == Type::Int &&
            safe_widening(got.int_type, want.int_type)) {
            IRValue src = IRValue::vreg(m_vstack[base + i]);
            m_vstack[base + i] = is_signed(got.int_type)
                                     ? m_ir.sext(src, width_of(got.int_type))
                                     : m_ir.zext(src, width_of(got.int_type));
            m_tstack[base + i] = want;
            continue;
        }

        fail(loc, "argument " + std::to_string(i + 1) + " to '" + name
                  + "' has type " + type_name(got) + ", expected " + type_name(want));
    }
}

void Generator::check_return_type(const NodeExpr& expr, SourceLoc loc) {
    if (!m_current_return_type.has_value()) return;
    if (m_tstack.empty() || m_vstack.empty()) return;

    const Type want = *m_current_return_type;
    const Type got = m_tstack.back();
    if (want == got) return;

    // `0` is a valid null pointer literal for any pointer return type (D-003).
    if (want.kind == Type::Ptr && got.kind == Type::Int) {
        auto constant = eval_const_expr(&expr);
        if (constant.has_value() && constant.value() == 0) return;
    }

    if (want.kind == Type::Int && got.kind == Type::Int) {
        auto constant = eval_const_expr(&expr);
        if (constant.has_value() && constant_fits(constant.value(), want.int_type)) return;
        if (safe_widening(got.int_type, want.int_type)) {
            IRValue src = IRValue::vreg(m_vstack.back());
            m_vstack.back() = is_signed(got.int_type)
                                  ? m_ir.sext(src, width_of(got.int_type))
                                  : m_ir.zext(src, width_of(got.int_type));
            m_tstack.back() = want;
            return;
        }
    }

    fail(loc, "return type mismatch in function '" + m_current_func_name
              + "': expected " + type_name(want) + ", got " + type_name(got));
}
