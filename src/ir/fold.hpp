#pragma once

#include "function.hpp"
#include "instruction.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

/// Counts what a folding pass changed, so effect tests can assert it ran.
struct FoldStats {
    size_t constants = 0;       ///< instructions replaced by a folded constant
    size_t simplifications = 0; ///< algebraic rewrites (x+0, x*2^n, ...)
};

namespace ir_fold_detail {

inline bool value_const(const IRValue& v, const std::map<uint32_t, int64_t>& consts, int64_t& out) {
    if (v.is_imm()) { out = v.imm; return true; }
    auto it = consts.find(v.vreg_id);
    if (it == consts.end()) return false;
    out = it->second;
    return true;
}

inline int64_t wrap_add(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

inline int64_t wrap_sub(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

inline int64_t wrap_mul(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

inline int64_t mask_to(int64_t v, int width) {
    if (width <= 0 || width >= 64) return v;
    const uint64_t mask = (uint64_t{1} << width) - 1;
    return static_cast<int64_t>(static_cast<uint64_t>(v) & mask);
}

inline int64_t sign_extend_from(int64_t v, int width) {
    if (width <= 0 || width >= 64) return v;
    const int64_t masked = mask_to(v, width);
    const uint64_t sign = uint64_t{1} << (width - 1);
    if ((static_cast<uint64_t>(v) & sign) != 0) {
        const uint64_t low_mask = (uint64_t{1} << width) - 1;
        return static_cast<int64_t>(static_cast<uint64_t>(masked) | ~low_mask);
    }
    return masked;
}

inline bool is_zero(const IRValue& v) { return v.is_imm() && v.imm == 0; }
inline bool is_one(const IRValue& v) { return v.is_imm() && v.imm == 1; }
inline bool is_all_ones(const IRValue& v) { return v.is_imm() && v.imm == -1; }

inline bool power_of_two(int64_t v, int& exponent) {
    if (v <= 0 || (v & (v - 1)) != 0) return false;
    exponent = 0;
    while (v > 1) { v >>= 1; ++exponent; }
    return exponent < 64;
}

/// Fold a pure instruction whose operands are compile-time constants. Never
/// folds traps (division by zero / INT64_MIN / -1) — those must behave exactly
/// as they do at runtime. Wrapping arithmetic uses unsigned arithmetic so it
/// matches the two's-complement result the backend emits.
inline bool try_fold_constant(const IRInstruction& insn,
                              const std::map<uint32_t, int64_t>& consts, int64_t& out) {
    using Op = IROpcode;
    if (insn.operands.size() == 1) {
        int64_t a = 0;
        if (!value_const(insn.operands[0], consts, a)) return false;
        switch (insn.op) {
            case Op::COPY:
            case Op::TRUNC:  // the emitter's TRUNC is a plain mov
                out = a;
                return true;
            case Op::NEG:
                out = wrap_sub(int64_t{0}, a);
                return true;
            case Op::NOT_:
                out = ~a;
                return true;
            case Op::ZEXT:
                out = mask_to(a, static_cast<int>(insn.imm_arg));
                return true;
            case Op::SEXT:
                out = sign_extend_from(a, static_cast<int>(insn.imm_arg));
                return true;
            default:
                return false;
        }
    }
    if (insn.operands.size() != 2) return false;

    int64_t a = 0;
    int64_t b = 0;
    if (!value_const(insn.operands[0], consts, a) ||
        !value_const(insn.operands[1], consts, b)) return false;

    switch (insn.op) {
        case Op::ADD:  out = wrap_add(a, b); return true;
        case Op::SUB:  out = wrap_sub(a, b); return true;
        case Op::MUL:  out = wrap_mul(a, b); return true;
        case Op::AND:  out = a & b; return true;
        case Op::OR:   out = a | b; return true;
        case Op::XOR:  out = a ^ b; return true;
        case Op::SHL:
        case Op::SHR:
        case Op::ASHR:
            if (b < 0 || b >= 64) return false;
            if (insn.op == Op::SHL) out = static_cast<int64_t>(static_cast<uint64_t>(a) << b);
            else if (insn.op == Op::SHR) out = static_cast<int64_t>(static_cast<uint64_t>(a) >> b);
            else out = a >> b;
            return true;
        case Op::DIV:
        case Op::MOD:
            if (b == 0 || (a == std::numeric_limits<int64_t>::min() && b == -1)) return false;
            out = (insn.op == Op::DIV) ? a / b : a % b;
            return true;
        case Op::CMP_EQ: out = a == b; return true;
        case Op::CMP_NE: out = a != b; return true;
        case Op::CMP_LT: out = a < b;  return true;
        case Op::CMP_LE: out = a <= b; return true;
        case Op::CMP_GT: out = a > b;  return true;
        case Op::CMP_GE: out = a >= b; return true;
        default:
            return false;
    }
}

/// Algebraic identities. The rewritten instruction keeps its `dst`, so no use
/// needs to be patched and dominance is preserved (including the multi-def
/// vregs used by ternary expressions).
inline bool simplify_algebra(IRInstruction& insn) {
    using Op = IROpcode;
    const size_t count = insn.operands.size();
    if (count != 2) return false;
    const IRValue& a = insn.operands[0];
    const IRValue& b = insn.operands[1];
    const uint32_t dst = insn.dst;
    const bool same = a == b;

    switch (insn.op) {
        case Op::ADD:
        case Op::OR:
        case Op::XOR:
            if (is_zero(b)) { insn = IRInstruction::copy(dst, a); return true; }
            if (is_zero(a)) { insn = IRInstruction::copy(dst, b); return true; }
            if (insn.op == Op::XOR && same) { insn = IRInstruction::load_i64(dst, 0); return true; }
            if ((insn.op == Op::OR || insn.op == Op::XOR) && same) {
                insn = IRInstruction::copy(dst, a);
                return true;
            }
            return false;
        case Op::SUB:
            if (is_zero(b)) { insn = IRInstruction::copy(dst, a); return true; }
            if (is_zero(a)) { insn = IRInstruction::unary(Op::NEG, dst, b); return true; }
            return false;
        case Op::MUL: {
            if (is_one(b)) { insn = IRInstruction::copy(dst, a); return true; }
            if (is_one(a)) { insn = IRInstruction::copy(dst, b); return true; }
            if (is_zero(a) || is_zero(b)) { insn = IRInstruction::load_i64(dst, 0); return true; }
            int shift = 0;
            if (b.is_imm() && power_of_two(b.imm, shift)) {
                insn = IRInstruction::binary(Op::SHL, dst, a, IRValue::imm_i64(shift));
                return true;
            }
            if (a.is_imm() && power_of_two(a.imm, shift)) {
                insn = IRInstruction::binary(Op::SHL, dst, b, IRValue::imm_i64(shift));
                return true;
            }
            return false;
        }
        case Op::AND:
            if (is_zero(a) || is_zero(b)) { insn = IRInstruction::load_i64(dst, 0); return true; }
            if (is_all_ones(b)) { insn = IRInstruction::copy(dst, a); return true; }
            if (is_all_ones(a)) { insn = IRInstruction::copy(dst, b); return true; }
            if (same) { insn = IRInstruction::copy(dst, a); return true; }
            return false;
        case Op::SHL:
        case Op::SHR:
        case Op::ASHR:
            if (is_zero(b)) { insn = IRInstruction::copy(dst, a); return true; }
            return false;
        default:
            return false;
    }
}

/// Record (or forget) the compile-time value of a definition.
inline void update_constant(IRInstruction& insn, std::map<uint32_t, int64_t>& consts,
                            const std::map<uint32_t, size_t>& def_counts) {
    if (insn.dst == IRInstruction::NONE_VREG) return;
    // A vreg defined in several blocks (the shared result of a ternary, for
    // example) is phi-like: its value depends on the path taken, so no single
    // definition may be propagated as a constant.
    auto count = def_counts.find(insn.dst);
    if (count == def_counts.end() || count->second != 1) {
        consts.erase(insn.dst);
        return;
    }
    switch (insn.op) {
        case IROpcode::LOAD_I64:
        case IROpcode::LOAD_U64:
            consts[insn.dst] = insn.imm_arg;
            return;
        case IROpcode::LOAD_I32:
            consts[insn.dst] = sign_extend_from(insn.imm_arg, 32);
            return;
        case IROpcode::LOAD_I8:
            consts[insn.dst] = sign_extend_from(insn.imm_arg, 8);
            return;
        case IROpcode::LOAD_U32_IMM:
            consts[insn.dst] = mask_to(insn.imm_arg, 32);
            return;
        case IROpcode::LOAD_U8_IMM:
            consts[insn.dst] = mask_to(insn.imm_arg, 8);
            return;
        default:
            consts.erase(insn.dst);
            return;
    }
}

} // namespace ir_fold_detail

/// Constant folding and algebraic simplification over one IR function.
inline FoldStats fold_function(IRFunction& f) {
    using namespace ir_fold_detail;
    FoldStats stats;
    std::map<uint32_t, int64_t> consts;
    std::map<uint32_t, size_t> def_counts;
    for (const auto& block : f.blocks) {
        for (const auto& insn : block.instructions) {
            if (insn.dst != IRInstruction::NONE_VREG) ++def_counts[insn.dst];
            if (insn.dst2 != IRInstruction::NONE_VREG) ++def_counts[insn.dst2];
        }
    }

    for (auto& block : f.blocks) {
        for (auto& insn : block.instructions) {
            // Constants live in vregs in the generator's IR, so make them
            // visible to the algebraic rewrites too. The original instruction
            // stays untouched unless a rewrite actually succeeds, which keeps
            // operands that must be registers (load bases, call targets, ...)
            // as registers.
            IRInstruction work = insn;
            for (auto& op : work.operands) {
                if (!op.is_vreg()) continue;
                auto known = consts.find(op.vreg_id);
                if (known != consts.end()) op = IRValue::imm_i64(known->second);
            }

            int64_t folded = 0;
            if (try_fold_constant(work, consts, folded)) {
                insn = IRInstruction::load_i64(insn.dst, folded);
                ++stats.constants;
            } else if (simplify_algebra(work)) {
                insn = std::move(work);
                ++stats.simplifications;
            }
            if (insn.dst2 != IRInstruction::NONE_VREG) consts.erase(insn.dst2);
            update_constant(insn, consts, def_counts);
        }
    }
    return stats;
}
