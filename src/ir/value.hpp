#pragma once

#include <cstdint>
#include <string>
#include <cassert>

/// Width of an IR value in bits.
enum class IRWidth : uint8_t {
    I8  = 8,
    I16 = 16,
    I32 = 32,
    I64 = 64,
    /// Floating-point widths (D-038). Bit 7 marks the float class; the low bits are the size.
    F32 = 0x80 | 32,
    F64 = 0x80 | 64,
};

inline bool is_fp_width(IRWidth w) { return w == IRWidth::F32 || w == IRWidth::F64; }
/// Size in bits of an IR width (32 for F32, 64 for F64).
inline int width_bits(IRWidth w) { return static_cast<int>(w) & 0x7F; }

/// An IR value: either a virtual register or an immediate.
/// Virtual registers are just unsigned integers (0, 1, 2, ...).
/// Immediates are 64-bit signed integers.
struct IRValue {
    enum class Kind : uint8_t {
        VREG,   /// Virtual register
        IMM,    /// Immediate integer
    };

    Kind kind;
    IRWidth width = IRWidth::I64;

    /// For VREG: the virtual register index
    uint32_t vreg_id = 0;

    /// For IMM: the immediate value
    int64_t imm = 0;

    static IRValue vreg(uint32_t id, IRWidth w = IRWidth::I64) {
        return { Kind::VREG, w, id, 0 };
    }

    /// A floating-point vreg use; `w` is IRWidth::F32 or F64.
    static IRValue fvreg(uint32_t id, IRWidth w) {
        assert(is_fp_width(w));
        return { Kind::VREG, w, id, 0 };
    }

    static IRValue imm_i64(int64_t val) {
        return { Kind::IMM, IRWidth::I64, 0, val };
    }

    static IRValue imm_u64(uint64_t val) {
        return { Kind::IMM, IRWidth::I64, 0, static_cast<int64_t>(val) };
    }

    bool is_vreg() const { return kind == Kind::VREG; }
    bool is_imm() const  { return kind == Kind::IMM; }

    bool operator==(const IRValue& o) const {
        return kind == o.kind && width == o.width && vreg_id == o.vreg_id && imm == o.imm;
    }
    bool operator!=(const IRValue& o) const { return !(*this == o); }

    /// Format for IR dump.
    std::string to_string() const;
};
