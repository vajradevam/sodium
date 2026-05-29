#pragma once

#include "opcodes.hpp"
#include "value.hpp"
#include <string>
#include <vector>
#include <cstdint>

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

    /// Sentinel for "no destination register".
    static constexpr uint32_t NONE_VREG = UINT32_MAX;

    // ---- convenience constructors ----

    static IRInstruction nop() {
        return { IROpcode::NOP, NONE_VREG, IRWidth::I64, {}, "", "", "", 0 };
    }

    static IRInstruction load_i64(uint32_t dst, int64_t val) {
        return { IROpcode::LOAD_I64, dst, IRWidth::I64, {}, "", "", "", val };
    }

    static IRInstruction load_i32(uint32_t dst, int32_t val) {
        return { IROpcode::LOAD_I32, dst, IRWidth::I32, {}, "", "", "", val };
    }

    static IRInstruction copy(uint32_t dst, IRValue src) {
        return { IROpcode::COPY, dst, IRWidth::I64, {src}, "", "", "", 0 };
    }

    static IRInstruction frame_addr(uint32_t dst, int64_t slot) {
        return { IROpcode::FRAME_ADDR, dst, IRWidth::I64, {}, "", "", "", slot };
    }

    static IRInstruction unary(IROpcode op, uint32_t dst, IRValue src) {
        return { op, dst, IRWidth::I64, {src}, "", "", "", 0 };
    }

    static IRInstruction binary(IROpcode op, uint32_t dst, IRValue a, IRValue b) {
        return { op, dst, IRWidth::I64, {a, b}, "", "", "", 0 };
    }

    static IRInstruction cmp(IROpcode op, uint32_t dst, IRValue a, IRValue b) {
        return { op, dst, IRWidth::I64, {a, b}, "", "", "", 0 };
    }

    static IRInstruction load(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LOAD, dst, IRWidth::I64, {base}, "", "", "", offset };
    }

    static IRInstruction store(IRValue base, int64_t offset, IRValue src) {
        return { IROpcode::STORE, NONE_VREG, IRWidth::I64, {base, src}, "", "", "", offset };
    }

    static IRInstruction lea(uint32_t dst, IRValue base, int64_t offset) {
        return { IROpcode::LEA, dst, IRWidth::I64, {base}, "", "", "", offset };
    }

    static IRInstruction lea_label(uint32_t dst, const std::string& label) {
        return { IROpcode::LEA_LABEL, dst, IRWidth::I64, {}, label, "", "", 0 };
    }

    static IRInstruction jmp(const std::string& target) {
        return { IROpcode::JMP, NONE_VREG, IRWidth::I64, {}, "", target, "", 0 };
    }

    static IRInstruction br(IRValue cond, const std::string& t, const std::string& f) {
        return { IROpcode::BR, NONE_VREG, IRWidth::I64, {cond}, "", t, f, 0 };
    }

    static IRInstruction call(uint32_t dst, const std::string& func,
                               const std::vector<IRValue>& args) {
        return { IROpcode::CALL, dst, IRWidth::I64, args, func, "", "", 0 };
    }

    static IRInstruction call_reg(uint32_t dst, const std::string& func,
                                   const std::vector<IRValue>& args) {
        return { IROpcode::CALL_REG, dst, IRWidth::I64, args, func, "", "", 0 };
    }

    static IRInstruction ret(IRValue val) {
        return { IROpcode::RET, NONE_VREG, IRWidth::I64, {val}, "", "", "", 0 };
    }

    static IRInstruction ret_void() {
        return { IROpcode::RET_VOID, NONE_VREG, IRWidth::I64, {}, "", "", "", 0 };
    }

    static IRInstruction zext(uint32_t dst, IRValue src, IRWidth from) {
        return { IROpcode::ZEXT, dst, IRWidth::I64, {src}, "", "", "", static_cast<int64_t>(from) };
    }

    static IRInstruction sext(uint32_t dst, IRValue src, IRWidth from) {
        return { IROpcode::SEXT, dst, IRWidth::I64, {src}, "", "", "", static_cast<int64_t>(from) };
    }

    static IRInstruction trunc(uint32_t dst, IRValue src, IRWidth to) {
        return { IROpcode::TRUNC, dst, to, {src}, "", "", "", 0 };
    }

    static IRInstruction syscall() {
        return { IROpcode::SYSCALL, NONE_VREG, IRWidth::I64, {}, "", "", "", 0 };
    }

    /// Format for IR dump.
    std::string to_string() const;
    size_t operand_count() const;
};
