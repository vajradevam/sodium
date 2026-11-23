#include "../generation.hpp"

#include <algorithm>
#include <cassert>

// ==========================================================================
// Struct-by-value ABI helpers (D-012, Y8b)
//
// A struct of at most 16 bytes is passed as 8-byte words in consecutive
// integer argument registers. A struct value inside the compiler is the
// address of its bytes; these helpers move it to and from words.
//
// A small struct that contains floats is split into "pieces" instead (X1b, D-040):
// each piece travels in an FP or an integer register, as the target's C ABI says.
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

std::vector<ArgPlacement> Generator::place_struct_arg(ArgPlacer& placer,
                                                      const StructInfo& info) const {
    const size_t words = struct_words(info.size);
    if (info.size <= 16) return placer.small_struct(words);
    if (m_tri_ptr->large_struct_by_ref) return {placer.scalar()};
    return placer.stack_struct(words);
}

namespace {

/// Bytes of the 8-byte `word` of a struct.
size_t word_bytes(const StructInfo& info, size_t word) {
    return std::min<size_t>(8, info.size - word * 8);
}

} // namespace

uint32_t Generator::load_struct_range(uint32_t base, const StructInfo& info, size_t off, size_t n) {
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

uint32_t Generator::load_struct_word(uint32_t base, const StructInfo& info, size_t word) {
    return load_struct_range(base, info, word * 8, word_bytes(info, word));
}

void Generator::store_param_range(const Var& var, const StructInfo& info, size_t off, size_t n,
                                  size_t ordinal) {
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

void Generator::store_param_word(const Var& var, const StructInfo& info, size_t word,
                                 size_t ordinal) {
    store_param_range(var, info, word * 8, word_bytes(info, word), ordinal);
}

void Generator::store_struct_range(uint32_t base, const StructInfo& info, size_t off, size_t n,
                                   uint32_t value) {
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

void Generator::store_struct_word(uint32_t base, const StructInfo& info, size_t word,
                                  uint32_t value) {
    store_struct_range(base, info, word * 8, word_bytes(info, word), value);
}

// ---- small structs with floats (X1b, D-040) ----

namespace {

} // namespace

void Generator::collect_leaves(const StructInfo& info, size_t base,
                               std::vector<StructLeaf>& out) const {
    for (size_t i = 0; i < info.field_names.size(); i++) {
        const Type& t = info.field_types[i];
        const size_t off = base + info.field_offsets.at(info.field_names[i]);
        if (t.kind == Type::Struct) {
            auto sub = get_struct_info(t.struct_name);
            if (sub.has_value()) collect_leaves(*sub, off, out);
            continue;
        }
        out.push_back({off, type_size(t), t.kind == Type::Float});
    }
}

std::vector<StructPiece> Generator::fp_struct_pieces(const StructInfo& info) const {
    std::vector<StructPiece> none;
    if (info.size == 0 || info.size > 16) return none;
    std::vector<StructLeaf> leaves;
    collect_leaves(info, 0, leaves);
    bool any_fp = false;
    for (const StructLeaf& l : leaves) any_fp = any_fp || l.fp;
    if (!any_fp) return none;

    if (m_tri_ptr->split_struct_args) {
        // RISC-V LP64D: a struct that flattens to one float, two floats, or a float and an
        // integer travels field by field; anything else goes by integer words.
        if (leaves.size() > 2) return none;
        std::vector<StructPiece> out;
        for (const StructLeaf& l : leaves) {
            if (!l.fp && l.size > 8) return none;
            out.push_back({l.off, l.size, l.fp});
        }
        return out;
    }

    // System V: each 8-byte word is SSE when every field in it is a float, else INTEGER.
    std::vector<StructPiece> out;
    for (size_t w = 0; w < struct_words(info.size); w++) {
        bool all_fp = true;
        size_t end = 0;
        for (const StructLeaf& l : leaves) {
            if (l.off >= (w + 1) * 8 || l.off + l.size <= w * 8) continue;
            all_fp = all_fp && l.fp;
            end = std::max(end, l.off + l.size);
        }
        const size_t n = word_bytes(info, w);
        out.push_back({w * 8, all_fp ? (end - w * 8 <= 4 ? 4 : 8) : n, all_fp});
    }
    // A float mixed into INTEGER words (e.g. {i32, f32}) leaves no SSE word: plain int words.
    const bool any_sse = std::any_of(out.begin(), out.end(), [](const StructPiece& p) { return p.fp; });
    return any_sse ? out : none;
}

StructPlan Generator::plan_struct_arg(ArgPlacer& placer, const StructInfo& info) const {
    StructPlan plan;
    const std::vector<StructPiece> pieces = fp_struct_pieces(info);
    if (!pieces.empty()) {
        if (auto places = placer.fp_struct(pieces)) {
            plan.places = *places;
            plan.pieces = pieces;
            return plan;
        }
    }
    if (!pieces.empty() && !m_tri_ptr->split_struct_args) {
        // System V: a struct whose eightbytes do not all fit in registers of their classes
        // goes entirely to memory, even if integer registers are still free.
        plan.places = placer.stack_struct(struct_words(info.size));
        return plan;
    }
    plan.places = place_struct_arg(placer, info);
    return plan;
}

uint32_t Generator::load_struct_piece(uint32_t base, const StructInfo& info, const StructPiece& p) {
    if (p.fp) {
        return m_ir.fload(p.size == 4 ? IRWidth::F32 : IRWidth::F64, IRValue::vreg(base),
                          static_cast<int64_t>(p.off));
    }
    return load_struct_range(base, info, p.off, p.size);
}

IRValue Generator::struct_piece_value(uint32_t vreg, const StructPiece& p) const {
    return p.fp ? IRValue::fvreg(vreg, p.size == 4 ? IRWidth::F32 : IRWidth::F64) : IRValue::vreg(vreg);
}

void Generator::store_struct_piece(uint32_t base, const StructInfo& info, const StructPiece& p,
                                   uint32_t value) {
    if (p.fp) {
        m_ir.fstore(IRValue::vreg(base), static_cast<int64_t>(p.off), struct_piece_value(value, p));
        return;
    }
    store_struct_range(base, info, p.off, p.size, value);
}

void Generator::store_param_piece(const Var& var, const StructInfo& info, const StructPiece& p,
                                  size_t ordinal) {
    if (!p.fp) {
        store_param_range(var, info, p.off, p.size, ordinal);
        return;
    }
    const int64_t low = -static_cast<int64_t>(var.stack_loc + frame_bytes(var));
    m_ir.store_param(static_cast<int64_t>(ordinal), 0, low + static_cast<int64_t>(p.off),
                     p.size == 4 ? IRWidth::F32 : IRWidth::F64);
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
    const std::vector<StructPiece> pieces = fp_struct_pieces(info);
    if (!pieces.empty()) {
        std::vector<uint32_t> vals;
        for (const StructPiece& p : pieces) vals.push_back(load_struct_piece(addr, info, p));
        if (pieces.size() == 1) m_ir.ret(struct_piece_value(vals[0], pieces[0]));
        else m_ir.ret2(struct_piece_value(vals[0], pieces[0]), struct_piece_value(vals[1], pieces[1]));
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

void Generator::gen_struct_call(const CallTarget& target, const std::vector<IRValue>& args,
                                const std::string& struct_name, const StructInfo& info,
                                std::optional<uint32_t> hidden_result, int reg_args) {
    if (hidden_result) {
        // `args` already carries the hidden result pointer; the callee fills it.
        emit_call(target, args, reg_args);
        push_vreg(*hidden_result);
        m_tstack.back() = Type::make_struct(struct_name);
        return;
    }
    const std::vector<StructPiece> pieces = fp_struct_pieces(info);
    const auto piece_width = [](const StructPiece& p) {
        return !p.fp ? IRWidth::I64 : p.size == 4 ? IRWidth::F32 : IRWidth::F64;
    };
    if (!pieces.empty()) {
        uint32_t first = 0;
        uint32_t second = 0;
        if (pieces.size() == 2) {
            first = emit_call2(target, args, second, reg_args, piece_width(pieces[0]),
                               piece_width(pieces[1]));
        } else {
            first = emit_call_f(target, args, piece_width(pieces[0]), reg_args);
        }
        uint32_t slot = alloc_object(info.size);
        uint32_t base = object_addr(slot, align_frame(info.size, 8));
        store_struct_piece(base, info, pieces[0], first);
        if (pieces.size() == 2) store_struct_piece(base, info, pieces[1], second);
        push_vreg(base);
        m_tstack.back() = Type::make_struct(struct_name);
        return;
    }
    const size_t words = struct_words(info.size);
    uint32_t first = 0;
    uint32_t second = 0;
    if (words == 2) {
        first = emit_call2(target, args, second, reg_args);
    } else {
        first = emit_call(target, args, reg_args);
    }
    // The result lives in a temporary frame object; the expression's value is its address.
    uint32_t slot = alloc_object(info.size);
    uint32_t base = object_addr(slot, align_frame(info.size, 8));
    if (words >= 1) store_struct_word(base, info, 0, first);
    if (words == 2) store_struct_word(base, info, 1, second);
    push_vreg(base);
    m_tstack.back() = Type::make_struct(struct_name);
}
