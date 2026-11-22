#include "../generation.hpp"

#include <algorithm>

// ==========================================================================
// Struct-by-value ABI helpers (D-012, Y8b)
//
// A struct of at most 16 bytes is passed as 8-byte words in consecutive
// integer argument registers. A struct value inside the compiler is the
// address of its bytes; these helpers move it to and from words.
// ==========================================================================

namespace {

/// Largest power-of-two chunk (<= 4) that fits in `remaining` bytes and does
/// not exceed the struct's alignment, so every access is naturally aligned.
size_t chunk_size(size_t remaining, size_t align) {
    size_t c = 4;
    while (c > remaining || c > align) c >>= 1;
    return c;
}

} // namespace

void Generator::check_struct_by_value(const StructInfo& info, const std::string& name,
                                      size_t first_word, SourceLoc loc) const {
    if (info.size > 16) {
        fail(loc, "struct '" + name + "' (" + std::to_string(info.size)
                      + " bytes) cannot be passed by value yet");
    }
    const TargetRegisterInfo& tri = *m_tri_ptr;
    const size_t regs = tri.arg_regs.size();
    const size_t words = struct_words(info.size);
    if (regs != 0 && !tri.split_struct_args && first_word < regs && first_word + words > regs) {
        fail(loc, "struct '" + name + "' would be split between argument registers and the "
                      "stack, which this target's ABI does not allow; move it earlier in the "
                      "parameter list");
    }
}

uint32_t Generator::load_struct_word(uint32_t base, const StructInfo& info, size_t word) {
    const size_t off = word * 8;
    const size_t n = std::min<size_t>(8, info.size - off);
    if (n == 8 && info.align >= 8) {
        return m_ir.load(IRValue::vreg(base), static_cast<int64_t>(off));
    }
    uint32_t acc = IRInstruction::NONE_VREG;
    for (size_t pos = 0; pos < n;) {
        const size_t c = chunk_size(n - pos, info.align);
        const int64_t at = static_cast<int64_t>(off + pos);
        uint32_t v = c == 4 ? m_ir.load_u32(IRValue::vreg(base), at)
                   : c == 2 ? m_ir.load_u16(IRValue::vreg(base), at)
                            : m_ir.load_u8(IRValue::vreg(base), at);
        if (pos != 0) v = m_ir.shl(IRValue::vreg(v), IRValue::imm_i64(static_cast<int64_t>(pos * 8)));
        acc = acc == IRInstruction::NONE_VREG ? v : m_ir.or_(IRValue::vreg(acc), IRValue::vreg(v));
        pos += c;
    }
    return acc;
}

void Generator::store_param_word(const Var& var, const StructInfo& info, size_t word,
                                 size_t ordinal) {
    const size_t off = word * 8;
    const size_t n = std::min<size_t>(8, info.size - off);
    // Raw fp-relative offset of the struct's lowest byte.
    const int64_t low = -static_cast<int64_t>(var.stack_loc + frame_bytes(var));
    if (n == 8 && info.align >= 8) {
        m_ir.store_param(static_cast<int64_t>(ordinal), 0, low + static_cast<int64_t>(off), IRWidth::I64);
        return;
    }
    for (size_t pos = 0; pos < n;) {
        const size_t c = chunk_size(n - pos, info.align);
        m_ir.store_param(static_cast<int64_t>(ordinal), static_cast<int64_t>(pos * 8),
                         low + static_cast<int64_t>(off + pos),
                         c == 4 ? IRWidth::I32 : c == 2 ? IRWidth::I16 : IRWidth::I8);
        pos += c;
    }
}
