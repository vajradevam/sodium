#pragma once

#include "function.hpp"
#include "instruction.hpp"
#include "linear_scan.hpp"
#include "target_regs.hpp"
#include "../backend/interface.hpp"
#include <cstdint>
#include <cassert>
#include <string>
#include <sstream>

/// Emits IR instructions (post-allocation, with physical register numbers)
/// to a Backend for final assembly output.
///
/// This is the bridge between architecture-independent IR and
/// architecture-specific assembly emission.
class IREmitter {
public:
    IREmitter(Backend& backend, const TargetRegisterInfo& tri,
              const RegisterAllocation& alloc)
        : m_backend(backend), m_tri(tri), m_alloc(alloc) {}

    /// Emit a complete function.
    void emit_function(const IRFunction& func) {
        m_func_name = func.name;

        // Function label
        m_backend.label(func.name);
        m_backend.func_prologue();

        // Allocate stack space for frame slots
        if (func.stack_slots > 0) {
            m_backend.adjust_stack(-static_cast<int64_t>(func.stack_slots) * 8);
        }

        // Emit each block with mangled labels
        for (auto& block : func.blocks) {
            std::string mangled_label = mangle_label(block.label);
            m_backend.label(mangled_label);
            for (auto& insn : block.instructions) {
                emit_instruction(insn);
            }
        }
        // Epilogue is emitted by the RET instruction inside the blocks.
        // No trailing epilogue here.
    }

    /// Emit a single instruction.
    void emit_instruction(const IRInstruction& insn) {
        switch (insn.op) {
            case IROpcode::NOP:
                break;

            case IROpcode::LOAD_I64:
                m_backend.load_imm(preg_name(insn.dst), insn.imm_arg);
                break;

            case IROpcode::LOAD_I32:
                m_backend.load_imm(preg_name(insn.dst), (int32_t)insn.imm_arg);
                break;

            case IROpcode::LOAD_I8:
                m_backend.load_imm(preg_name(insn.dst), (int8_t)insn.imm_arg);
                break;

            case IROpcode::LOAD_U64:
                m_backend.load_imm(preg_name(insn.dst), (uint64_t)insn.imm_arg);
                break;

            case IROpcode::LOAD_U32_IMM:
                m_backend.load_imm(preg_name(insn.dst), (uint32_t)insn.imm_arg);
                break;

            case IROpcode::LOAD_U8_IMM:
                m_backend.load_imm(preg_name(insn.dst), (uint8_t)insn.imm_arg);
                break;

            case IROpcode::COPY:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                break;

            case IROpcode::FRAME_ADDR:
                // Frame slot at [fp - (slot+1)*8]
                m_backend.lea(preg_name(insn.dst),
                              m_backend.addr_fp(-static_cast<int>((insn.imm_arg + 1) * 8)));
                break;

            case IROpcode::NEG:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.neg(preg_name(insn.dst));
                break;

            case IROpcode::NOT_:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.not_(preg_name(insn.dst));
                break;

            case IROpcode::ZEXT:
            case IROpcode::SEXT: {
                int from_bits = (int)insn.imm_arg;
                auto dst = preg_name(insn.dst);
                auto src = operand_name(insn, 0);
                m_backend.mov(dst, src);
                if (insn.op == IROpcode::ZEXT) {
                    if (from_bits == 8)
                        m_backend.movzx("eax", "al", 8);
                    else if (from_bits == 16)
                        m_backend.movzx("eax", "ax", 16);
                    else if (from_bits == 32)
                        m_backend.mov("eax", "eax");
                } else {
                    if (from_bits == 8)
                        m_backend.movsx(dst, "al", 8);
                    else if (from_bits == 16)
                        m_backend.movsx(dst, "ax", 16);
                    else if (from_bits == 32)
                        m_backend.movsx(dst, "eax", 32);
                }
                break;
            }

            case IROpcode::TRUNC:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                // Truncation: just mask or zero-extend based on dst width
                // The backend's truncate/extend methods handle this
                break;

            case IROpcode::ADD:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.add(preg_name(insn.dst), operand_name(insn, 1));
                break;

            case IROpcode::SUB:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.sub(preg_name(insn.dst), operand_name(insn, 1));
                break;

            case IROpcode::MUL:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.mul(preg_name(insn.dst), operand_name(insn, 1));
                break;

            case IROpcode::DIV:
                // Use backend's signed_div which handles target-specific
                // division constraints (x86-64: cqo + idiv; RISC-V: div)
                m_backend.signed_div(preg_name(insn.dst),
                                     operand_name(insn, 0),
                                     operand_name(insn, 1));
                break;

            case IROpcode::MOD:
                m_backend.signed_mod(preg_name(insn.dst),
                                     operand_name(insn, 0),
                                     operand_name(insn, 1));
                break;

            case IROpcode::AND:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.and_(preg_name(insn.dst), operand_name(insn, 1));
                break;

            case IROpcode::OR:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.or_(preg_name(insn.dst), operand_name(insn, 1));
                break;

            case IROpcode::XOR:
                m_backend.mov(preg_name(insn.dst), operand_name(insn, 0));
                m_backend.xor_(preg_name(insn.dst), operand_name(insn, 1));
                break;

            case IROpcode::SHL: {
                std::string dst = preg_name(insn.dst);
                if (insn.operands[1].is_imm()) {
                    m_backend.mov(dst, operand_name(insn, 0));
                    m_backend.shl(dst, std::to_string(insn.operands[1].imm));
                } else {
                    // x86-64 requires shift count in CL (rcx).
                    // If dst == rcx, use r11 as scratch to avoid clobber.
                    if (dst == "rcx") {
                        m_backend.mov("r11", operand_name(insn, 0));
                        m_backend.mov("rcx", operand_name(insn, 1));
                        m_backend.shl("r11", "cl");
                        m_backend.mov("rcx", "r11");
                    } else {
                        m_backend.mov(dst, operand_name(insn, 0));
                        m_backend.mov("rcx", operand_name(insn, 1));
                        m_backend.shl(dst, "cl");
                    }
                }
                break;
            }

            case IROpcode::SHR: {
                std::string dst = preg_name(insn.dst);
                if (insn.operands[1].is_imm()) {
                    m_backend.mov(dst, operand_name(insn, 0));
                    m_backend.shr(dst, std::to_string(insn.operands[1].imm));
                } else {
                    if (dst == "rcx") {
                        m_backend.mov("r11", operand_name(insn, 0));
                        m_backend.mov("rcx", operand_name(insn, 1));
                        m_backend.shr("r11", "cl");
                        m_backend.mov("rcx", "r11");
                    } else {
                        m_backend.mov(dst, operand_name(insn, 0));
                        m_backend.mov("rcx", operand_name(insn, 1));
                        m_backend.shr(dst, "cl");
                    }
                }
                break;
            }

            case IROpcode::ASHR: {
                std::string dst = preg_name(insn.dst);
                if (dst == "rcx") {
                    m_backend.mov("r11", operand_name(insn, 0));
                    m_backend.mov("rcx", operand_name(insn, 1));
                    m_backend.ashr("r11", "cl");
                    m_backend.mov("rcx", "r11");
                } else {
                    m_backend.mov(dst, operand_name(insn, 0));
                    m_backend.mov("rcx", operand_name(insn, 1));
                    m_backend.ashr(dst, "cl");
                }
                break;
            }

            // Comparisons
            case IROpcode::CMP_EQ:
                emit_cmp(insn, "e");
                break;
            case IROpcode::CMP_NE:
                emit_cmp(insn, "ne");
                break;
            case IROpcode::CMP_LT:
                emit_cmp(insn, "l");
                break;
            case IROpcode::CMP_LE:
                emit_cmp(insn, "le");
                break;
            case IROpcode::CMP_GT:
                emit_cmp(insn, "g");
                break;
            case IROpcode::CMP_GE:
                emit_cmp(insn, "ge");
                break;

            // Memory operations
            case IROpcode::LOAD:
                m_backend.load(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::LOAD_S8:
                m_backend.load_s8(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::LOAD_U8:
                m_backend.load_u8(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::LOAD_S16:
                m_backend.load_s16(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::LOAD_U16:
                m_backend.load_u16(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::LOAD_S32:
                m_backend.load_s32(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::LOAD_U32:
                m_backend.load_u32(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::STORE:
                m_backend.store(mem_addr(insn), operand_name(insn, 1));
                break;

            case IROpcode::STORE_8:
                m_backend.store_8(mem_addr(insn), operand_name(insn, 1));
                break;

            case IROpcode::STORE_16:
                m_backend.store_16(mem_addr(insn), operand_name(insn, 1));
                break;

            case IROpcode::STORE_32:
                m_backend.store_32(mem_addr(insn), operand_name(insn, 1));
                break;

            case IROpcode::LEA:
                m_backend.lea(preg_name(insn.dst), mem_addr(insn));
                break;

            case IROpcode::LEA_LABEL:
                m_backend.lea(preg_name(insn.dst), m_backend.addr_label(mangle_label(insn.call_target)));
                break;

            case IROpcode::JMP:
                m_backend.jmp(mangle_label(insn.label_true));
                break;

            case IROpcode::BR: {
                std::string reg = operand_name(insn, 0);
                m_backend.test(reg, reg);
                m_backend.jnz(mangle_label(insn.label_true));
                m_backend.jmp(mangle_label(insn.label_false));
                break;
            }

            case IROpcode::CALL: {
                // Stack-based calling convention: push arguments in reverse
                // order so arg0 ends up at [rbp+16] in the callee.
                size_t nargs = insn.operands.size();
                for (size_t i = 0; i < nargs; i++) {
                    m_backend.push(operand_name(insn, nargs - 1 - i));
                }
                m_backend.call(insn.call_target);
                if (nargs > 0) {
                    m_backend.adjust_stack(static_cast<int64_t>(nargs) * 8);
                }
                if (insn.dst != IRInstruction::NONE_VREG) {
                    std::string dst_name = preg_name(insn.dst);
                    std::string ret_name = m_tri.name_of(m_tri.ret_reg);
                    if (dst_name != ret_name) {
                        m_backend.mov(dst_name, ret_name);
                    }
                }
                break;
            }

            case IROpcode::CALL_REG: {
                // Register-based calling convention:
                // args in target's argument registers, rest on stack.
                size_t nargs = insn.operands.size();
                size_t n_arg_regs = m_tri.arg_regs.size();
                for (size_t i = 0; i < nargs && i < n_arg_regs; i++) {
                    std::string arg_reg = m_tri.name_of(m_tri.arg_regs[i]);
                    m_backend.mov(arg_reg, operand_name(insn, i));
                }
                if (nargs > n_arg_regs) {
                    for (size_t i = n_arg_regs; i < nargs; i++) {
                        m_backend.push(operand_name(insn, i));
                    }
                }
                m_backend.call(insn.call_target);
                if (nargs > n_arg_regs) {
                    m_backend.adjust_stack(static_cast<int64_t>(nargs - n_arg_regs) * 8);
                }
                if (insn.dst != IRInstruction::NONE_VREG) {
                    std::string dst_name = preg_name(insn.dst);
                    std::string ret_name = m_tri.name_of(m_tri.ret_reg);
                    if (dst_name != ret_name) {
                        m_backend.mov(dst_name, ret_name);
                    }
                }
                break;
            }

            case IROpcode::RET: {
                // Move return value to return register
                if (!insn.operands.empty()) {
                    std::string ret_val = operand_name(insn, 0);
                    std::string ret_name = m_tri.name_of(m_tri.ret_reg);
                    if (ret_val != ret_name) {
                        m_backend.mov(ret_name, ret_val);
                    }
                }
                m_backend.func_epilogue();
                m_backend.ret();
                break;
            }

            case IROpcode::RET_VOID:
                m_backend.func_epilogue();
                m_backend.ret();
                break;

            case IROpcode::SYSCALL:
                m_backend.emit_insn("syscall", "");
                break;

            case IROpcode::PUSH:
                m_backend.push(operand_name(insn, 0));
                break;

            case IROpcode::POP:
                m_backend.pop(operand_name(insn, 0));
                break;
        }
    }

private:
    Backend& m_backend;
    const TargetRegisterInfo& m_tri;
    const RegisterAllocation& m_alloc;
    std::string m_func_name;

    std::string mangle_label(const std::string& label) const {
        if (label.empty()) return label;
        if (label[0] == '.') return m_func_name + label;
        return label;
    }

    std::string preg_name(uint32_t preg) const {
        return m_tri.name_of(static_cast<int>(preg));
    }

    std::string operand_name(const IRInstruction& insn, size_t idx) const {
        if (idx >= insn.operands.size()) return "0";
        auto& op = insn.operands[idx];
        if (op.is_vreg()) {
            return m_tri.name_of(static_cast<int>(op.vreg_id));
        } else {
            return std::to_string(op.imm);
        }
    }

    std::string mem_addr(const IRInstruction& insn) const {
        if (insn.operands.empty()) {
            return m_backend.addr_reg("x0");
        }
        std::string reg = preg_name(static_cast<int>(insn.operands[0].vreg_id));
        return m_backend.addr_reg_offset(reg, static_cast<int>(insn.imm_arg));
    }

    void emit_cmp(const IRInstruction& insn, const std::string& cc) {
        auto dst = preg_name(insn.dst);
        auto a = operand_name(insn, 0);
        auto b = operand_name(insn, 1);
        m_backend.mov(dst, a);
        m_backend.cmp(dst, b);
        m_backend.set_cc("al", cc);
        m_backend.movzx(dst, "al", 8);
    }
};
