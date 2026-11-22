#include "../generation.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

// ==========================================================================
// Floating-point code generation (D-038).
//
// f32 / f64 values are vregs of the FP class. Typing rules: int -> float and
// f32 -> f64 are implicit; f64 -> f32 and float -> int need an explicit `as`.
// Arithmetic on mixed operands converts to the wider float (an int operand
// takes the float's type). There is no `%` and no float truthiness.
// ==========================================================================

namespace {

IRWidth fwidth(FloatType f) { return f == FloatType::f32 ? IRWidth::F32 : IRWidth::F64; }

IRValue fp_use(uint32_t vreg, const Type& t) { return IRValue::fvreg(vreg, fwidth(t.float_type)); }

bool int_is_signed(IntType t) {
    return t == IntType::i8 || t == IntType::i16 || t == IntType::i32 || t == IntType::i64;
}

IRWidth int_width(IntType t) {
    switch (t) {
        case IntType::i8:  case IntType::u8:  return IRWidth::I8;
        case IntType::i16: case IntType::u16: return IRWidth::I16;
        case IntType::i32: case IntType::u32: return IRWidth::I32;
        case IntType::i64: case IntType::u64: return IRWidth::I64;
    }
    return IRWidth::I64;
}

} // namespace

void Generator::gen_float_literal(const NodeExprFloatLit& lit) {
    std::string text = lit.float_lit.value.value();
    const bool single = !text.empty() && text.back() == 'f';
    if (single) text.pop_back();

    errno = 0;
    uint64_t bits = 0;
    if (single) {
        const float f = std::strtof(text.c_str(), nullptr);
        if (std::isinf(f)) fail(lit.float_lit.loc, "float literal '" + text + "f' is out of range for f32");
        uint32_t b32;
        std::memcpy(&b32, &f, sizeof b32);
        bits = b32;
    } else {
        const double d = std::strtod(text.c_str(), nullptr);
        if (std::isinf(d)) fail(lit.float_lit.loc, "float literal '" + text + "' is out of range for f64");
        std::memcpy(&bits, &d, sizeof bits);
    }
    const FloatType ft = single ? FloatType::f32 : FloatType::f64;
    uint32_t v = m_ir.fconst(fwidth(ft), bits);
    push_vreg(v);
    m_tstack.back() = Type::make_float(ft);
}

uint32_t Generator::to_float(uint32_t v, const Type& from, FloatType to, const SourceLoc& loc,
                             bool allow_narrow) {
    if (from.kind == Type::Int) {
        // Registers can hold stale high bits for narrow integers; extend first.
        if (from.int_type != IntType::i64 && from.int_type != IntType::u64) {
            const IRWidth w = int_width(from.int_type);
            v = int_is_signed(from.int_type) ? m_ir.sext(IRValue::vreg(v), w)
                                             : m_ir.zext(IRValue::vreg(v), w);
        }
        return m_ir.i2f(fwidth(to), IRValue::vreg(v));
    }
    if (from.kind != Type::Float) {
        fail(loc, "cannot convert " + type_name(from) + " to " + type_name(Type::make_float(to)));
    }
    if (from.float_type == to) return v;
    if (from.float_type == FloatType::f64 && !allow_narrow) {
        fail(loc, "cannot implicitly convert f64 to f32 (use an explicit cast: 'as f32')");
    }
    return m_ir.f2f(fwidth(to), fp_use(v, from));
}

bool Generator::top_two_have_float() const {
    const size_t n = m_tstack.size();
    return n >= 2 && (m_tstack[n - 1].kind == Type::Float || m_tstack[n - 2].kind == Type::Float);
}

namespace {

FloatType common_float(const Type& a, const Type& b) {
    if (a.kind == Type::Float && b.kind == Type::Float) {
        return (a.float_type == FloatType::f64 || b.float_type == FloatType::f64) ? FloatType::f64
                                                                                    : FloatType::f32;
    }
    return a.kind == Type::Float ? a.float_type : b.float_type;
}

} // namespace

void Generator::gen_float_arith(IROpcode op, const SourceLoc& loc, const char* op_name) {
    const Type tb = pop_type();
    const Type ta = pop_type();
    if ((ta.kind != Type::Float && ta.kind != Type::Int) ||
        (tb.kind != Type::Float && tb.kind != Type::Int)) {
        fail(loc, std::string("invalid operands to '") + op_name + "': " + type_name(ta) + " and "
                      + type_name(tb));
    }
    const uint32_t b = m_vstack.back(); m_vstack.pop_back();
    const uint32_t a = m_vstack.back(); m_vstack.pop_back();
    const FloatType ft = common_float(ta, tb);
    const Type rt = Type::make_float(ft);
    const uint32_t fa = to_float(a, ta, ft, loc);
    const uint32_t fb = to_float(b, tb, ft, loc);
    uint32_t r = 0;
    switch (op) {
        case IROpcode::FADD: r = m_ir.fadd(fp_use(fa, rt), fp_use(fb, rt)); break;
        case IROpcode::FSUB: r = m_ir.fsub(fp_use(fa, rt), fp_use(fb, rt)); break;
        case IROpcode::FMUL: r = m_ir.fmul(fp_use(fa, rt), fp_use(fb, rt)); break;
        default:             r = m_ir.fdiv(fp_use(fa, rt), fp_use(fb, rt)); break;
    }
    push_vreg(r);
    m_tstack.back() = rt;
}

void Generator::gen_float_compare(IROpcode op, const SourceLoc& loc, const char* op_name) {
    const Type tb = pop_type();
    const Type ta = pop_type();
    if ((ta.kind != Type::Float && ta.kind != Type::Int) ||
        (tb.kind != Type::Float && tb.kind != Type::Int)) {
        fail(loc, std::string("invalid operands to '") + op_name + "': " + type_name(ta) + " and "
                      + type_name(tb));
    }
    const uint32_t b = m_vstack.back(); m_vstack.pop_back();
    const uint32_t a = m_vstack.back(); m_vstack.pop_back();
    const FloatType ft = common_float(ta, tb);
    const Type rt = Type::make_float(ft);
    const uint32_t fa = to_float(a, ta, ft, loc);
    const uint32_t fb = to_float(b, tb, ft, loc);
    push_vreg(m_ir.fcmp(op, fp_use(fa, rt), fp_use(fb, rt)));
}

void Generator::gen_float_neg() {
    const Type t = m_tstack.back();
    const uint32_t v = pop_vreg();
    push_vreg(m_ir.fneg(fp_use(v, t)));
    m_tstack.back() = t;
}

void Generator::require_not_float(const SourceLoc& loc, const std::string& what) {
    if (!m_tstack.empty() && m_tstack.back().kind == Type::Float) {
        fail(loc, what + " cannot be a " + type_name(m_tstack.back())
                      + " (compare it explicitly, e.g. 'x != 0.0')");
    }
}

bool Generator::coerce_float_at(size_t pos, const Type& want, const SourceLoc& loc,
                                const std::string& what) {
    if (pos >= m_tstack.size() || pos >= m_vstack.size()) return false;
    const Type got = m_tstack[pos];
    if (want.kind != Type::Float && got.kind != Type::Float) return false;

    if (want.kind == Type::Float) {
        if (got.kind == Type::Float && got.float_type == want.float_type) return true;
        if (got.kind == Type::Float && got.float_type == FloatType::f64) {
            fail(loc, "cannot implicitly convert f64 to f32 in " + what
                          + " (use an explicit cast: 'as f32')");
        }
        if (got.kind != Type::Int && got.kind != Type::Float) {
            fail(loc, "cannot use a value of type " + type_name(got) + " as " + type_name(want)
                          + " in " + what);
        }
        m_vstack[pos] = to_float(m_vstack[pos], got, want.float_type, loc);
        m_tstack[pos] = want;
        return true;
    }
    fail(loc, "cannot implicitly convert " + type_name(got) + " to " + type_name(want) + " in " + what
                  + " (use an explicit cast: 'as " + type_name(want) + "')");
}

void Generator::store_float_top(uint32_t addr, FloatType ft) {
    const uint32_t v = pop_vreg();
    m_ir.fstore(IRValue::vreg(addr), 0, IRValue::fvreg(v, fwidth(ft)));
}

void Generator::load_float_var(const Var& var) {
    const uint32_t addr = var_addr(var);
    const uint32_t v = m_ir.fload(fwidth(var.type.float_type), IRValue::vreg(addr), 0);
    push_vreg(v);
    m_tstack.back() = var.type;
}

void Generator::store_float_var(const Var& var, AssignOp op) {
    store_float_at(var_addr(var), var.type.float_type, op, {});
}

void Generator::store_float_at(uint32_t addr, FloatType ft, AssignOp op, const SourceLoc& loc) {
    const Type want = Type::make_float(ft);
    if (op == AssignOp::assign) {
        coerce_float_top(want, loc, "assignment");
        store_float_top(addr, ft);
        return;
    }
    IROpcode fop = IROpcode::FADD;
    const char* name = "+=";
    switch (op) {
        case AssignOp::add_assign: fop = IROpcode::FADD; name = "+="; break;
        case AssignOp::sub_assign: fop = IROpcode::FSUB; name = "-="; break;
        case AssignOp::mul_assign: fop = IROpcode::FMUL; name = "*="; break;
        case AssignOp::div_assign: fop = IROpcode::FDIV; name = "/="; break;
        default:
            fail(loc, "this compound assignment is not defined for floats");
    }
    // Stack on entry: [..., RHS]; push the current value and swap so it reads LHS op RHS.
    push_vreg(m_ir.fload(fwidth(ft), IRValue::vreg(addr), 0));
    m_tstack.back() = want;
    std::swap(m_vstack[m_vstack.size() - 1], m_vstack[m_vstack.size() - 2]);
    std::swap(m_tstack[m_tstack.size() - 1], m_tstack[m_tstack.size() - 2]);
    gen_float_arith(fop, loc, name);
    // `x += y` keeps the target's type, as in C: narrow the result when needed.
    const Type rt = m_tstack.back();
    if (rt.kind == Type::Float && rt.float_type != ft) {
        const uint32_t v = pop_vreg();
        push_vreg(to_float(v, rt, ft, loc, /*allow_narrow=*/true));
        m_tstack.back() = want;
    }
    store_float_top(addr, ft);
}

bool Generator::struct_has_float(const std::string& name) const {
    auto it = m_struct_types.find(name);
    if (it == m_struct_types.end()) return false;
    for (const Type& t : it->second.field_types) {
        if (t.kind == Type::Float) return true;
        if (t.kind == Type::Struct && struct_has_float(t.struct_name)) return true;
    }
    return false;
}

void Generator::check_struct_by_value(const std::string& name, const SourceLoc& loc) const {
    auto it = m_struct_types.find(name);
    if (it == m_struct_types.end() || it->second.size > 16 || !struct_has_float(name)) return;
    fail(loc, "struct '" + name + "' contains floats and cannot be passed or returned by value yet "
                  "(pass a pointer; D-039)");
}

uint32_t Generator::emit_call_f(const CallTarget& target, const std::vector<IRValue>& args,
                                IRWidth w, int reg_args) {
    if (target.pointer) {
        return m_ir.call_indirect_f(IRValue::vreg(*target.pointer), args, w, reg_args);
    }
    return m_ir.call_f(target.name, args, w, reg_args);
}
