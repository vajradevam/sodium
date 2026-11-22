#include "../generation.hpp"

#include <algorithm>
#include <cassert>

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
    const TargetRegisterInfo& tri = *m_tri_ptr;
    if (info.size > 16) {
        // Passed as one pointer word to a caller-made copy where the ABI says so.
        if (!tri.large_struct_by_ref && !tri.arg_regs.empty()) {
            fail(loc, "struct '" + name + "' (" + std::to_string(info.size)
                          + " bytes) cannot be passed by value yet");
        }
        return;
    }
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

void Generator::store_struct_word(uint32_t base, const StructInfo& info, size_t word,
                                  uint32_t value) {
    const size_t off = word * 8;
    const size_t n = std::min<size_t>(8, info.size - off);
    if (n == 8 && info.align >= 8) {
        m_ir.store(IRValue::vreg(base), static_cast<int64_t>(off), IRValue::vreg(value));
        return;
    }
    for (size_t pos = 0; pos < n;) {
        const size_t c = chunk_size(n - pos, info.align);
        const int64_t at = static_cast<int64_t>(off + pos);
        uint32_t v = pos == 0 ? value
                              : m_ir.shr(IRValue::vreg(value),
                                         IRValue::imm_i64(static_cast<int64_t>(pos * 8)));
        if (c == 4)      m_ir.store_32(IRValue::vreg(base), at, IRValue::vreg(v));
        else if (c == 2) m_ir.store_16(IRValue::vreg(base), at, IRValue::vreg(v));
        else             m_ir.store_8(IRValue::vreg(base), at, IRValue::vreg(v));
        pos += c;
    }
}

void Generator::gen_struct_return(uint32_t addr, const StructInfo& info) {
    if (info.size > 16) {
        // Copy into the caller's buffer and return its address (hidden first argument).
        assert(m_ret_ptr_slot.has_value());
        uint32_t holder = object_addr(*m_ret_ptr_slot, 8);
        uint32_t dest = m_ir.load(IRValue::vreg(holder), 0);
        copy_memory(dest, addr, info.size, info.align);
        m_ir.ret(IRValue::vreg(dest));
        return;
    }
    const size_t words = struct_words(info.size);
    if (words == 0) {
        m_ir.ret_void();
    } else if (words == 1) {
        m_ir.ret(IRValue::vreg(load_struct_word(addr, info, 0)));
    } else {
        uint32_t lo = load_struct_word(addr, info, 0);
        uint32_t hi = load_struct_word(addr, info, 1);
        m_ir.ret2(IRValue::vreg(lo), IRValue::vreg(hi));
    }
}

void Generator::gen_struct_call(const std::string& fname, const std::vector<IRValue>& args,
                                const std::string& struct_name, const StructInfo& info,
                                std::optional<uint32_t> hidden_result) {
    if (hidden_result) {
        // `args` already starts with the hidden result pointer; the callee fills it.
        m_ir.call(fname, args);
        push_vreg(*hidden_result);
        m_tstack.back() = Type::make_struct(struct_name);
        return;
    }
    const size_t words = struct_words(info.size);
    uint32_t first = 0;
    uint32_t second = 0;
    if (words == 2) {
        first = m_ir.call2(fname, args, second);
    } else {
        first = m_ir.call(fname, args);
    }
    // The result lives in a temporary frame object; the expression's value is its address.
    uint32_t slot = alloc_object(info.size);
    uint32_t base = object_addr(slot, align_frame(info.size, 8));
    if (words >= 1) store_struct_word(base, info, 0, first);
    if (words == 2) store_struct_word(base, info, 1, second);
    push_vreg(base);
    m_tstack.back() = Type::make_struct(struct_name);
}
