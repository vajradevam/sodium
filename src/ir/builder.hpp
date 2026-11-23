#pragma once

#include "function.hpp"
#include "block.hpp"
#include "instruction.hpp"
#include "value.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <cassert>

/// IRBuilder: high-level API for emitting IR into the current function.
///
/// Usage:
///   IRBuilder ir;
///   ir.start_function("main");
///   auto v1 = ir.load_i64(42);
///   auto v2 = ir.load_i64(10);
///   auto v3 = ir.add(v1, v2);
///   ir.ret(v3);
///   ir.end_function();
class IRBuilder {
public:
    IRBuilder() = default;

    // ---- function management ----

    /// Start emitting a new function.
    void start_function(const std::string& name) {
        m_func = std::make_unique<IRFunction>(name);
        m_cur_block = m_func->entry_block();
    }

    /// Finish the current function and return it.
    std::unique_ptr<IRFunction> end_function() {
        return std::move(m_func);
    }

    /// Get the current (in-progress) function.
    IRFunction* current_function() { return m_func.get(); }

    // ---- block management ----

    /// Create a new block and set it as current.
    /// If `label` is empty, auto-generate one.
    IRBlock* new_block(const std::string& label = "") {
        std::string lbl = label;
        if (lbl.empty()) {
            lbl = ".L" + std::to_string(m_block_count++);
        }
        auto* block = m_func->add_block(lbl);
        m_cur_block = block;
        return block;
    }

    /// Switch to an existing block.
    void set_cur_block(IRBlock* block) {
        m_cur_block = block;
    }

    /// Get the current block label.
    std::string cur_block_label() const {
        return m_cur_block ? m_cur_block->label : "";
    }

    // ---- instruction emission ----

    /// Set the source location stamped on subsequently emitted instructions.
    void set_loc(uint32_t line, const std::string& file) {
        m_src_line = line;
        m_src_file = file;
    }

    /// Emit an instruction and return the destination vreg (or NONE_VREG).
    uint32_t emit(IRInstruction insn) {
        assert(m_cur_block);
        insn.src_line = m_src_line;
        insn.src_file = m_src_file;
        m_cur_block->instructions.push_back(std::move(insn));
        return m_cur_block->instructions.back().dst;
    }

    // ---- convenience methods ----

    uint32_t nop() {
        return emit(IRInstruction::nop());
    }

    uint32_t asm_insn(const std::string& text, std::vector<int> clobbers) {
        return emit(IRInstruction::asm_insn(text, std::move(clobbers)));
    }

    uint32_t load_i64(int64_t val) {
        auto dst = new_vreg();
        emit(IRInstruction::load_i64(dst, val));
        return dst;
    }

    uint32_t copy(IRValue src) {
        auto dst = new_vreg();
        emit(IRInstruction::copy(dst, src));
        return dst;
    }

    uint32_t frame_addr(int64_t byte_offset) {
        auto dst = new_vreg();
        // The emitter computes fp + imm_arg. We pass -(byte_offset + 8)
        // so the result is fp - byte_offset - 8.
        // First byte (offset 0) → fp - 8.
        emit(IRInstruction::frame_addr(dst, -(byte_offset + 8)));
        return dst;
    }

    /// Store parameter word `ordinal` (shifted right by `shift_bits`) into the frame at
    /// `fp_offset` (raw fp-relative byte offset), `width` bits wide.
    void store_param(int64_t ordinal, int64_t shift_bits, int64_t fp_offset, IRWidth width) {
        emit(IRInstruction::store_param(ordinal, shift_bits, fp_offset, width));
    }

    uint32_t neg(IRValue src) {
        auto dst = new_vreg();
        emit(IRInstruction::unary(IROpcode::NEG, dst, src));
        return dst;
    }

    uint32_t not_(IRValue src) {
        auto dst = new_vreg();
        emit(IRInstruction::unary(IROpcode::NOT_, dst, src));
        return dst;
    }

#define IR_BINOP(name, opcode) \
    uint32_t name(IRValue a, IRValue b) { \
        auto dst = new_vreg(); \
        emit(IRInstruction::binary(IROpcode::opcode, dst, a, b)); \
        return dst; \
    }

    IR_BINOP(add, ADD)
    IR_BINOP(sub, SUB)
    IR_BINOP(mul, MUL)
    IR_BINOP(div, DIV)
    IR_BINOP(mod, MOD)
    IR_BINOP(udiv, UDIV)
    IR_BINOP(umod, UMOD)
    IR_BINOP(and_, AND)
    IR_BINOP(or_, OR)
    IR_BINOP(xor_, XOR)
    IR_BINOP(shl, SHL)
    IR_BINOP(shr, SHR)
    IR_BINOP(ashr, ASHR)

    IR_BINOP(cmp_eq, CMP_EQ)
    IR_BINOP(cmp_ne, CMP_NE)
    IR_BINOP(cmp_lt, CMP_LT)
    IR_BINOP(cmp_le, CMP_LE)
    IR_BINOP(cmp_gt, CMP_GT)
    IR_BINOP(cmp_ge, CMP_GE)

#undef IR_BINOP

    uint32_t load(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::load(dst, base, offset));
        return dst;
    }

    uint32_t load_s8(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::load_s8(dst, base, offset));
        return dst;
    }
    uint32_t load_u8(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::load_u8(dst, base, offset));
        return dst;
    }
    uint32_t load_s16(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::load_s16(dst, base, offset));
        return dst;
    }
    uint32_t load_u16(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::load_u16(dst, base, offset));
        return dst;
    }
    uint32_t load_s32(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::load_s32(dst, base, offset));
        return dst;
    }
    uint32_t load_u32(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::load_u32(dst, base, offset));
        return dst;
    }

    uint32_t lea(IRValue base, int64_t offset) {
        auto dst = new_vreg();
        emit(IRInstruction::lea(dst, base, offset));
        return dst;
    }

    uint32_t lea_label(const std::string& label) {
        auto dst = new_vreg();
        emit(IRInstruction::lea_label(dst, label));
        return dst;
    }

    void store(IRValue base, int64_t offset, IRValue src) {
        emit(IRInstruction::store(base, offset, src));
    }
    void store_8(IRValue base, int64_t offset, IRValue src) {
        emit(IRInstruction::store_8(base, offset, src));
    }
    void store_16(IRValue base, int64_t offset, IRValue src) {
        emit(IRInstruction::store_16(base, offset, src));
    }
    void store_32(IRValue base, int64_t offset, IRValue src) {
        emit(IRInstruction::store_32(base, offset, src));
    }

    void jmp(const std::string& target) {
        emit(IRInstruction::jmp(target));
    }

    void br(IRValue cond, const std::string& t, const std::string& f) {
        emit(IRInstruction::br(cond, t, f));
    }

    /// `reg_args` (>= 0) says how many leading arguments go in registers; the rest are
    /// stack words (-1: fill the argument registers first).
    uint32_t call(const std::string& func, const std::vector<IRValue>& args, int reg_args = -1) {
        auto dst = new_vreg();
        auto insn = IRInstruction::call(dst, func, args);
        insn.reg_args = reg_args;
        emit(insn);
        return dst;
    }

    /// Call returning a two-word value: the first word is the result, the second is
    /// written to `second`. `w1`/`w2` give each word's class: an F32/F64 width means it
    /// comes back in an FP result register (small structs with float words, X1b).
    uint32_t call2(const std::string& func, const std::vector<IRValue>& args, uint32_t& second,
                   int reg_args = -1, IRWidth w1 = IRWidth::I64, IRWidth w2 = IRWidth::I64) {
        auto dst = is_fp_width(w1) ? new_fp_vreg() : new_vreg();
        second = is_fp_width(w2) ? new_fp_vreg() : new_vreg();
        auto insn = IRInstruction::call(dst, func, args);
        insn.dst_width = w1;
        insn.dst2 = second;
        insn.dst2_width = w2;
        insn.reg_args = reg_args;
        emit(insn);
        return dst;
    }

    /// Call through a function pointer (D-014); same argument rules as `call`.
    uint32_t call_indirect(IRValue target, const std::vector<IRValue>& args, int reg_args = -1) {
        auto dst = new_vreg();
        auto insn = IRInstruction::call_indirect(dst, target, args);
        insn.reg_args = reg_args;
        emit(insn);
        return dst;
    }

    /// Indirect call returning a two-word value.
    uint32_t call2_indirect(IRValue target, const std::vector<IRValue>& args, uint32_t& second,
                            int reg_args = -1, IRWidth w1 = IRWidth::I64, IRWidth w2 = IRWidth::I64) {
        auto dst = is_fp_width(w1) ? new_fp_vreg() : new_vreg();
        second = is_fp_width(w2) ? new_fp_vreg() : new_vreg();
        auto insn = IRInstruction::call_indirect(dst, target, args);
        insn.dst_width = w1;
        insn.dst2 = second;
        insn.dst2_width = w2;
        insn.reg_args = reg_args;
        emit(insn);
        return dst;
    }

    uint32_t load_param(int64_t param_index) {
        auto dst = new_vreg();
        emit(IRInstruction::load_param(dst, param_index));
        return dst;
    }

    void ret(IRValue val) {
        emit(IRInstruction::ret(val));
    }

    void ret2(IRValue lo, IRValue hi) {
        emit(IRInstruction::ret2(lo, hi));
    }

    void ret_void() {
        emit(IRInstruction::ret_void());
    }

    uint32_t zext(IRValue src, IRWidth from) {
        auto dst = new_vreg();
        emit(IRInstruction::zext(dst, src, from));
        return dst;
    }

    uint32_t sext(IRValue src, IRWidth from) {
        auto dst = new_vreg();
        emit(IRInstruction::sext(dst, src, from));
        return dst;
    }

    uint32_t trunc(IRValue src, IRWidth to) {
        auto dst = new_vreg();
        emit(IRInstruction::trunc(dst, src, to));
        return dst;
    }

    void syscall() {
        emit(IRInstruction::syscall());
    }

    // ---- floating point (D-038) ----
    // Results are vregs of the FP class. Operands are passed as FP IRValues (IRValue::fvreg).

    /// A float constant given as its raw IEEE bit pattern (32 bits for F32).
    uint32_t fconst(IRWidth w, uint64_t bits) {
        auto dst = new_fp_vreg();
        emit(IRInstruction::fconst(dst, w, static_cast<int64_t>(bits)));
        return dst;
    }
    uint32_t fcopy(IRValue src) {
        auto dst = new_fp_vreg();
        emit(IRInstruction::fcopy(dst, src));
        return dst;
    }
    uint32_t fneg(IRValue src) {
        auto dst = new_fp_vreg();
        emit(IRInstruction::fneg(dst, src));
        return dst;
    }
    uint32_t fadd(IRValue a, IRValue b) { return fbin(IROpcode::FADD, a, b); }
    uint32_t fsub(IRValue a, IRValue b) { return fbin(IROpcode::FSUB, a, b); }
    uint32_t fmul(IRValue a, IRValue b) { return fbin(IROpcode::FMUL, a, b); }
    uint32_t fdiv(IRValue a, IRValue b) { return fbin(IROpcode::FDIV, a, b); }

    /// Float comparison producing an integer 0/1. `op` is one of FCMP_*.
    uint32_t fcmp(IROpcode op, IRValue a, IRValue b) {
        auto dst = new_vreg();
        emit(IRInstruction::fcmp(op, dst, a, b));
        return dst;
    }
    uint32_t i2f(IRWidth to, IRValue src) {
        auto dst = new_fp_vreg();
        emit(IRInstruction::i2f(dst, to, src));
        return dst;
    }
    uint32_t f2i(IRValue src) {
        auto dst = new_vreg();
        emit(IRInstruction::f2i(dst, src));
        return dst;
    }
    uint32_t f2f(IRWidth to, IRValue src) {
        auto dst = new_fp_vreg();
        emit(IRInstruction::f2f(dst, to, src));
        return dst;
    }
    uint32_t fload(IRWidth w, IRValue base, int64_t offset) {
        auto dst = new_fp_vreg();
        emit(IRInstruction::fload(dst, w, base, offset));
        return dst;
    }
    void fstore(IRValue base, int64_t offset, IRValue src) {
        emit(IRInstruction::fstore(base, offset, src));
    }

    /// Call whose result is a float of width `w` (returned in the FP result register).
    uint32_t call_f(const std::string& func, const std::vector<IRValue>& args, IRWidth w,
                    int reg_args = -1) {
        auto dst = new_fp_vreg();
        auto insn = IRInstruction::call(dst, func, args);
        insn.dst_width = w;
        insn.reg_args = reg_args;
        emit(insn);
        return dst;
    }
    uint32_t call_indirect_f(IRValue target, const std::vector<IRValue>& args, IRWidth w,
                             int reg_args = -1) {
        auto dst = new_fp_vreg();
        auto insn = IRInstruction::call_indirect(dst, target, args);
        insn.dst_width = w;
        insn.reg_args = reg_args;
        emit(insn);
        return dst;
    }

    /// The vreg classes of the function being built.
    const IRFunction* function() const { return m_func.get(); }

private:
    uint32_t new_vreg() {
        assert(m_func);
        return m_func->new_vreg();
    }

    uint32_t new_fp_vreg() {
        assert(m_func);
        return m_func->new_fp_vreg();
    }

    uint32_t fbin(IROpcode op, IRValue a, IRValue b) {
        auto dst = new_fp_vreg();
        emit(IRInstruction::fbinary(op, dst, a, b));
        return dst;
    }

    std::unique_ptr<IRFunction> m_func;
    IRBlock* m_cur_block = nullptr;
    uint32_t m_src_line = 0;
    std::string m_src_file;
    uint32_t m_block_count = 0;
};
