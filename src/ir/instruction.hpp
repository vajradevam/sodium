#pragma once

#include "opcodes.hpp"
#include "value.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

/// A single IR instruction.
/// Has an opcode, a destination (virtual register or none), source operands,
/// and optional metadata (label name, function name, block targets).
struct IRInstruction {
    IROpcode op = IROpcode::NOP;

    /// Destination virtual register (NONE_VREG if no result).
    uint32_t dst = NONE_VREG;
    IRWidth dst_width = IRWidth::I64;

    /// Source operands (0-3 operands depending on opcode).
    std::vector<IRValue> operands;

    /// For CALL: the function name.
    std::string call_target;

    /// For JMP/BR: target block label names.
    std::string label_true;
    std::string label_false;

    /// For LOAD_I* / FRAME_ADDR: immediate value or frame slot index.
    int64_t imm_arg = 0;

    /// For CALL (after spill rewriting): register arguments that live in spill
    /// slots, as (argument physical register, fp-relative byte offset). The
    /// emitter loads them directly into their argument registers after the
    /// register-to-register argument moves are done.
    std::vector<std::pair<int, int>> arg_loads;

    /// For CALL returning a two-word value: the vreg receiving the second return
    /// register (NONE_VREG otherwise). `dst` receives the first.
    uint32_t dst2 = UINT32_MAX;

    /// For CALL: how many leading operands travel in argument registers; the rest are
    /// pushed on the stack in order. -1 means "as many as there are argument registers".
    int reg_args = -1;

    /// For ASM: the verbatim instruction line and clobbered physical registers.
    std::string asm_text;
    std::vector<int> asm_clobbers;

    /// Source location of the Cyan statement that produced this instruction.
    uint32_t src_line = 0;
    std::string src_file;

    /// Sentinel for "no destination register".
    static constexpr uint32_t NONE_VREG = UINT32_MAX;

    // ---- convenience constructors ----

    static IRInstruction nop() {
        return { IROpcode::NOP, NONE_VREG, IRWidth::I64, {}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction load_i64(uint32_t dst, int64_t val) {
        return { IROpcode::LOAD_I64, dst, IRWidth::I64, {}, "", "", "", val, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction load_i32(uint32_t dst, int32_t val) {
        return { IROpcode::LOAD_I32, dst, IRWidth::I32, {}, "", "", "", val, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction copy(uint32_t dst, IRValue src) {
        return { IROpcode::COPY, dst, IRWidth::I64, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction frame_addr(uint32_t dst, int64_t slot) {
        return { IROpcode::FRAME_ADDR, dst, IRWidth::I64, {}, "", "", "", slot, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction unary(IROpcode op, uint32_t dst, IRValue src) {
        return { op, dst, IRWidth::I64, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction binary(IROpcode op, uint32_t dst, IRValue a, IRValue b) {
        return { op, dst, IRWidth::I64, {a, b}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction cmp(IROpcode op, uint32_t dst, IRValue a, IRValue b) {
        return { op, dst, IRWidth::I64, {a, b}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction load(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction load_s8(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD_S8, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction load_u8(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD_U8, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction load_s16(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD_S16, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction load_u16(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD_U16, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction load_s32(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD_S32, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction load_u32(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD_U32, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction store(IRValue base, int64_t offset, IRValue src) {
        return { IROpcode::STORE, NONE_VREG, IRWidth::I64, {base, src}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction store_8(IRValue base, int64_t offset, IRValue src) {
        return { IROpcode::STORE_8, NONE_VREG, IRWidth::I64, {base, src}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction store_16(IRValue base, int64_t offset, IRValue src) {
        return { IROpcode::STORE_16, NONE_VREG, IRWidth::I64, {base, src}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction store_32(IRValue base, int64_t offset, IRValue src) {
        return { IROpcode::STORE_32, NONE_VREG, IRWidth::I64, {base, src}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction lea(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LEA, dst, IRWidth::I64, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction lea_label(uint32_t dst, const std::string& label) {
        return { IROpcode::LEA_LABEL, dst, IRWidth::I64, {}, label, "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction jmp(const std::string& target) {
        return { IROpcode::JMP, NONE_VREG, IRWidth::I64, {}, "", target, "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction br(IRValue cond, const std::string& t, const std::string& f) {
        return { IROpcode::BR, NONE_VREG, IRWidth::I64, {cond}, "", t, f, 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction call(uint32_t dst, const std::string& func,
                               const std::vector<IRValue>& args) {
        return { IROpcode::CALL, dst, IRWidth::I64, args, func, "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    /// Call through `target`; the target is stored as the last operand (D-014).
    static IRInstruction call_indirect(uint32_t dst, IRValue target,
                                       const std::vector<IRValue>& args) {
        std::vector<IRValue> ops = args;
        ops.push_back(target);
        return { IROpcode::CALL_INDIRECT, dst, IRWidth::I64, ops, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    /// Number of argument operands of a CALL / CALL_INDIRECT (excludes an indirect target).
    size_t call_arg_count() const {
        return op == IROpcode::CALL_INDIRECT ? operands.size() - 1 : operands.size();
    }

    static IRInstruction load_param(uint32_t dst, int64_t param_index) {
        return { IROpcode::LOAD_PARAM, dst, IRWidth::I64, {}, "", "", "", param_index, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction store_param(int64_t ordinal, int64_t shift_bits, int64_t fp_offset,
                                      IRWidth width) {
        return { IROpcode::STORE_PARAM, NONE_VREG, width,
                 {IRValue::imm_i64(ordinal), IRValue::imm_i64(shift_bits)},
                 "", "", "", fp_offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction ret(IRValue val) {
        return { IROpcode::RET, NONE_VREG, IRWidth::I64, {val}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    /// Return a two-word value in the first and second return registers.
    static IRInstruction ret2(IRValue lo, IRValue hi) {
        return { IROpcode::RET2, NONE_VREG, IRWidth::I64, {lo, hi}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction ret_void() {
        return { IROpcode::RET_VOID, NONE_VREG, IRWidth::I64, {}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction zext(uint32_t dst, IRValue src, IRWidth from) {
        return { IROpcode::ZEXT, dst, IRWidth::I64, {src}, "", "", "", static_cast<int64_t>(from), {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction sext(uint32_t dst, IRValue src, IRWidth from) {
        return { IROpcode::SEXT, dst, IRWidth::I64, {src}, "", "", "", static_cast<int64_t>(from), {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction trunc(uint32_t dst, IRValue src, IRWidth to) {
        return { IROpcode::TRUNC, dst, to, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction syscall() {
        return { IROpcode::SYSCALL, NONE_VREG, IRWidth::I64, {}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    static IRInstruction asm_insn(const std::string& text, std::vector<int> clobbers) {
        IRInstruction insn{ IROpcode::ASM, NONE_VREG, IRWidth::I64, {}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
        insn.asm_text = text;
        insn.asm_clobbers = std::move(clobbers);
        return insn;
    }

    // ---- floating point (D-038) ----
    // FP operands are IRValues whose width is IRWidth::F32 / F64.

    static IRInstruction fconst(uint32_t dst, IRWidth w, int64_t bits) {
        return { IROpcode::FCONST, dst, w, {}, "", "", "", bits, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction fcopy(uint32_t dst, IRValue src) {
        return { IROpcode::FCOPY, dst, src.width, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction fneg(uint32_t dst, IRValue src) {
        return { IROpcode::FNEG, dst, src.width, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction fbinary(IROpcode op, uint32_t dst, IRValue a, IRValue b) {
        return { op, dst, a.width, {a, b}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    /// Float comparison; the result is an integer 0/1.
    static IRInstruction fcmp(IROpcode op, uint32_t dst, IRValue a, IRValue b) {
        return { op, dst, IRWidth::I64, {a, b}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction i2f(uint32_t dst, IRWidth to, IRValue src) {
        return { IROpcode::I2F, dst, to, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction f2i(uint32_t dst, IRValue src) {
        return { IROpcode::F2I, dst, IRWidth::I64, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction f2f(uint32_t dst, IRWidth to, IRValue src) {
        return { IROpcode::F2F, dst, to, {src}, "", "", "", 0, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction fload(uint32_t dst, IRWidth w, IRValue base, int64_t offset) {
        return { IROpcode::FLOAD, dst, w, {base}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }
    static IRInstruction fstore(IRValue base, int64_t offset, IRValue src) {
        return { IROpcode::FSTORE, NONE_VREG, IRWidth::I64, {base, src}, "", "", "", offset, {}, NONE_VREG, -1, {}, {}, 0, "" };
    }

    /// Format for IR dump.
    std::string to_string() const;
    size_t operand_count() const;
};
