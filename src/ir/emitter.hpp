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
#include <unordered_map>

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
                        m_backend.movzx(low32(dst), low8(dst), 8);
                    else if (from_bits == 16)
                        m_backend.movzx(low32(dst), low16(dst), 16);
                    else if (from_bits == 32)
                        m_backend.mov(low32(dst), low32(dst));
                } else {
                    if (from_bits == 8)
                        m_backend.movsx(dst, low8(dst), 8);
                    else if (from_bits == 16)
                        m_backend.movsx(dst, low16(dst), 16);
                    else if (from_bits == 32)
                        m_backend.movsx(dst, low32(dst), 32);
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
                auto dst = preg_name(insn.dst);
                auto src = operand_name(insn, 0);
                auto count = insn.operands[1].is_imm()
                    ? std::to_string(insn.operands[1].imm)
                    : operand_name(insn, 1);
                m_backend.mov(dst, src);
                m_backend.shl(dst, count);
                break;
            }

            case IROpcode::SHR: {
                auto dst = preg_name(insn.dst);
                auto src = operand_name(insn, 0);
                auto count = insn.operands[1].is_imm()
                    ? std::to_string(insn.operands[1].imm)
                    : operand_name(insn, 1);
                m_backend.mov(dst, src);
                m_backend.shr(dst, count);
                break;
            }

            case IROpcode::ASHR: {
                auto dst = preg_name(insn.dst);
                auto src = operand_name(insn, 0);
                auto count = insn.operands[1].is_imm()
                    ? std::to_string(insn.operands[1].imm)
                    : operand_name(insn, 1);
                m_backend.mov(dst, src);
                m_backend.ashr(dst, count);
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
                // Target-agnostic register-based calling convention:
                // args go in argument registers (per target ABI);
                // overflow args (> n_arg_regs) are pushed on the stack.
                size_t nargs = insn.operands.size();
                size_t n_arg_regs = m_tri.arg_regs.size();

                // CRITICAL: Save overflow args to the stack FIRST.
                // The register setup below might clobber registers that
                // hold values for overflow args (since the register
                // allocator assigns vregs to physical regs without
                // considering calling-convention constraints).
                //
                // Push right-to-left so the first stack argument ends up
                // at the lowest address (closest to the return address),
                // matching the LOAD_PARAM offset formula:
                //   load_param(idx) = [fp + 16 + (idx - n_arg_regs) * 8]
                if (nargs > n_arg_regs) {
                    for (size_t i = nargs; i-- > n_arg_regs; ) {
                        m_backend.push(operand_name(insn, i));
                    }
                }

                // Now set up register arguments IN REVERSE ORDER
                // (highest-indexed arg first). This avoids clobbering:
                // a later arg register might be used as temporary storage
                // for an earlier arg value, and moving higher-indexed
                // args first ensures those temporaries are consumed before
                // they get overwritten.
                int start = static_cast<int>((nargs < n_arg_regs) ? nargs : n_arg_regs);
                for (int i = start - 1; i >= 0; i--) {
                    std::string arg_reg = m_tri.name_of(m_tri.arg_regs[i]);
                    m_backend.mov(arg_reg, operand_name(insn, static_cast<size_t>(i)));
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

            case IROpcode::LOAD_PARAM: {
                // Load the i-th function parameter from its ABI location:
                // - If i < n_arg_regs: load from argument register arg_regs[i]
                // - If i >= n_arg_regs: load from stack [fp + 16 + (i-n_arg_regs)*8]
                size_t n_arg_regs = m_tri.arg_regs.size();
                size_t idx = static_cast<size_t>(insn.imm_arg);
                if (idx < n_arg_regs) {
                    std::string arg_reg = m_tri.name_of(m_tri.arg_regs[idx]);
                    m_backend.mov(preg_name(insn.dst), arg_reg);
                } else {
                    // Stack slot for overflow param: [fp + 16 + (i-n_arg_regs)*8]
                    int offset = static_cast<int>(16 + (idx - n_arg_regs) * 8);
                    m_backend.load(preg_name(insn.dst),
                                   m_backend.addr_fp(offset));
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
                m_backend.syscall();
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
    static std::string low32(const std::string& r) {
        static const std::unordered_map<std::string, std::string> m = {
            {"rax", "eax"}, {"rbx", "ebx"}, {"rcx", "ecx"}, {"rdx", "edx"},
            {"rsi", "esi"}, {"rdi", "edi"}, {"rbp", "ebp"}, {"rsp", "esp"},
            {"r8", "r8d"}, {"r9", "r9d"}, {"r10", "r10d"}, {"r11", "r11d"},
            {"r12", "r12d"}, {"r13", "r13d"}, {"r14", "r14d"}, {"r15", "r15d"},
        };
        auto it = m.find(r);
        return it != m.end() ? it->second : r;
    }
    static std::string low16(const std::string& r) {
        static const std::unordered_map<std::string, std::string> m = {
            {"rax", "ax"}, {"rbx", "bx"}, {"rcx", "cx"}, {"rdx", "dx"},
            {"rsi", "si"}, {"rdi", "di"}, {"rbp", "bp"}, {"rsp", "sp"},
            {"r8", "r8w"}, {"r9", "r9w"}, {"r10", "r10w"}, {"r11", "r11w"},
            {"r12", "r12w"}, {"r13", "r13w"}, {"r14", "r14w"}, {"r15", "r15w"},
        };
        auto it = m.find(r);
        return it != m.end() ? it->second : r;
    }
    static std::string low8(const std::string& r) {
        static const std::unordered_map<std::string, std::string> m = {
            {"rax", "al"}, {"rbx", "bl"}, {"rcx", "cl"}, {"rdx", "dl"},
            {"rsi", "sil"}, {"rdi", "dil"}, {"rbp", "bpl"}, {"rsp", "spl"},
            {"r8", "r8b"}, {"r9", "r9b"}, {"r10", "r10b"}, {"r11", "r11b"},
            {"r12", "r12b"}, {"r13", "r13b"}, {"r14", "r14b"}, {"r15", "r15b"},
        };
        auto it = m.find(r);
        return it != m.end() ? it->second : r;
    }

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
        m_backend.cmp_result(preg_name(insn.dst),
                             operand_name(insn, 0),
                             operand_name(insn, 1), cc);
    }
};
