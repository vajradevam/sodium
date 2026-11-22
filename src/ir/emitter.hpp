#pragma once

#include "function.hpp"
#include "instruction.hpp"
#include "linear_scan.hpp"
#include "target_regs.hpp"
#include "../backend/interface.hpp"
#include <cstdint>
#include <algorithm>
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
    IREmitter(Backend& backend, const TargetRegisterInfo& tri)
        : m_backend(backend), m_tri(tri) {}

    /// Enable source-line debug markers (D-037).
    void set_debug_info(bool enabled) { m_debug_info = enabled; }

    /// Enable the optional entry stack-overflow probe (X4c).
    void set_stack_probe(bool enabled, const std::string& message_label) {
        m_stack_probe = enabled;
        m_stack_message_label = message_label;
    }

    /// Emit a complete function.
    void emit_function(const IRFunction& func) {
        m_func_name = func.name;

        // Function label
        m_backend.label(func.name);
        m_backend.func_prologue();

        if (m_stack_probe) {
            m_backend.stack_probe_check(
                "__sodium_stack_limit", m_stack_message_label,
                mangle_label(".Lstack_ok"), mangle_label(".Lstack_fail"));
        }

        // Allocate stack space for frame slots
        if (func.stack_slots > 0) {
            m_backend.adjust_stack(-static_cast<int64_t>(func.stack_slots));
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
        if (m_debug_info && insn.src_line != 0 &&
            (insn.src_line != m_last_debug_line || insn.src_file != m_last_debug_file)) {
            m_backend.debug_line(insn.src_file, insn.src_line);
            m_last_debug_line = insn.src_line;
            m_last_debug_file = insn.src_file;
        }
        switch (insn.op) {
            case IROpcode::NOP:
                break;

            case IROpcode::ASM: {
                // Verbatim assembly. Marker labels keep the peephole pass from
                // rewriting user instructions (X5b); clobbers are handled by
                // liveness and the allocator.
                const std::string tag = ".L" + m_func_name +
                    "_sodium_asm_" + std::to_string(m_asm_counter++);
                m_backend.label(tag + "_begin");
                m_backend.emit_insn(insn.asm_text, "");
                m_backend.label(tag + "_end");
                break;
            }

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
                // Frame slot at [fp + imm_arg] where imm_arg = -(byte_offset + 8)
                // (byte offset 0 → fp - 8, byte offset 8 → fp - 16, etc.)
                m_backend.lea(preg_name(insn.dst),
                              m_backend.addr_fp(static_cast<int>(insn.imm_arg)));
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
                        m_backend.movzx(dst, low32(dst), 32);
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

            case IROpcode::ADD: {
                std::string dst = preg_name(insn.dst);
                std::string a = operand_name(insn, 0);
                std::string b = operand_name(insn, 1);
                if (dst == b) std::swap(a, b); // dst aliases op1: swap (commutative)
                m_backend.mov(dst, a);
                m_backend.add(dst, b);
                break;
            }

            case IROpcode::SUB: {
                std::string dst = preg_name(insn.dst);
                std::string a = operand_name(insn, 0);
                std::string b = operand_name(insn, 1);
                m_backend.mov(dst, a);
                m_backend.sub(dst, b);
                break;
            }

            case IROpcode::MUL: {
                std::string dst = preg_name(insn.dst);
                std::string a = operand_name(insn, 0);
                std::string b = operand_name(insn, 1);
                if (dst == b) std::swap(a, b);
                m_backend.mov(dst, a);
                m_backend.mul(dst, b);
                break;
            }

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

            case IROpcode::CALL:
            case IROpcode::CALL_INDIRECT: {
                // The rewriter has already emitted stack-argument pushes
                // (including any alignment padding) immediately before this
                // instruction and recorded their byte count in imm_arg.
                // Only register arguments remain in operands.
                const bool indirect = insn.op == IROpcode::CALL_INDIRECT;
                size_t nargs = insn.call_arg_count();
                size_t n_arg_regs = m_tri.arg_regs.size();

                // Set up register arguments as a parallel move: a source may
                // itself live in another argument register, so moves are
                // ordered so no pending source is overwritten, with cycles
                // broken through the scratch register. Immediates go last.
                // Floats are loaded straight from their homes (arg_loads) into FP argument
                // registers, or into integer ones once those run out; only integer operands
                // take part in the parallel move. With floats present the register area can
                // hold more operands than there are integer argument registers.
                size_t reg_ops = nargs;
                if (insn.reg_args >= 0) reg_ops = std::min<size_t>(nargs, static_cast<size_t>(insn.reg_args));
                else reg_ops = std::min(nargs, n_arg_regs);
                std::vector<bool> fp_mask;
                int fp_in_regs = 0;
                for (size_t i = 0; i < reg_ops; i++) {
                    fp_mask.push_back(is_fp_width(insn.operands[i].width));
                }
                const std::vector<ArgRegAssign> assign = assign_arg_regs(m_tri, fp_mask);
                struct Move { std::string dst, src; };
                std::vector<Move> pending;
                std::vector<Move> imms;
                for (size_t i = 0; i < reg_ops; i++) {
                    if (fp_mask[i]) {
                        if (assign[i].fp) fp_in_regs++;
                        continue;
                    }
                    std::string dst = m_tri.name_of(assign[i].preg);
                    std::string src = operand_name(insn, i);
                    if (insn.operands[i].is_imm()) imms.push_back({dst, src});
                    else if (dst != src) pending.push_back({dst, src});
                }
                if (indirect) {
                    // The target is one more destination of the parallel move.
                    const std::string dst = m_tri.name_of(m_tri.call_target_reg);
                    const std::string src = operand_name(insn, nargs);
                    if (dst != src) pending.push_back({dst, src});
                }
                const std::string tmp = m_tri.name_of(m_tri.scratch_reg);
                while (!pending.empty()) {
                    bool progressed = false;
                    for (size_t m = 0; m < pending.size(); m++) {
                        bool dst_needed = false;
                        for (size_t o = 0; o < pending.size(); o++)
                            if (o != m && pending[o].src == pending[m].dst) { dst_needed = true; break; }
                        if (dst_needed) continue;
                        m_backend.mov(pending[m].dst, pending[m].src);
                        pending.erase(pending.begin() + static_cast<long>(m));
                        progressed = true;
                        break;
                    }
                    if (!progressed) {
                        // Every remaining move is part of a cycle: park one
                        // destination's current value in the scratch register.
                        const std::string parked = pending[0].dst;
                        m_backend.mov(tmp, parked);
                        for (auto& mv : pending)
                            if (mv.src == parked) mv.src = tmp;
                    }
                }
                for (auto& mv : imms) m_backend.mov(mv.dst, mv.src);
                for (auto& al : insn.arg_loads) {
                    const std::string addr =
                        m_backend.addr_reg_offset(m_tri.name_of(m_tri.fp_reg), al.second);
                    if (al.first & FP_REG_FLAG) {
                        m_backend.fp_load(m_tri.fp_name(al.first & FP_REG_MASK), addr,
                                          (al.first & FP_SINGLE_FLAG) ? 32 : 64);
                    } else {
                        m_backend.load(m_tri.name_of(al.first), addr);
                    }
                }
                if (!indirect && fp_in_regs > 0) m_backend.note_fp_arg_count(fp_in_regs);

                if (indirect) m_backend.call_reg(m_tri.name_of(m_tri.call_target_reg));
                else m_backend.call(insn.call_target);

                if (insn.imm_arg != 0) {
                    m_backend.adjust_stack(static_cast<int64_t>(insn.imm_arg));
                }

                // A float result stays in the FP result register; the rewriter stores it.
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

            case IROpcode::STORE_PARAM: {
                // Store parameter word (argument register or stack slot) directly to
                // its frame slot. Only the scratch register is used as a temporary, so
                // no argument register that is still unread can be clobbered.
                const size_t n_arg_regs = m_tri.arg_regs.size();
                const int64_t ordinal_raw = insn.operands[0].imm;
                if (ordinal_raw >= FP_PARAM_ORDINAL_BASE) {
                    // A float parameter arriving in FP argument register #k: store it directly.
                    const size_t k = static_cast<size_t>(ordinal_raw - FP_PARAM_ORDINAL_BASE);
                    assert(k < m_tri.fp_arg_regs.size());
                    m_backend.fp_store(m_backend.addr_fp(static_cast<int>(insn.imm_arg)),
                                       m_tri.fp_name(m_tri.fp_arg_regs[k]), width_bits(insn.dst_width));
                    break;
                }
                const size_t ordinal = static_cast<size_t>(ordinal_raw);
                const int64_t shift = insn.operands[1].imm;
                const std::string tmp = m_tri.name_of(m_tri.scratch_reg);
                std::string src;
                if (ordinal < n_arg_regs) {
                    src = m_tri.name_of(m_tri.arg_regs[ordinal]);
                } else {
                    int offset = static_cast<int>(16 + (ordinal - n_arg_regs) * 8);
                    m_backend.load(tmp, m_backend.addr_fp(offset));
                    src = tmp;
                }
                if (shift != 0) {
                    if (src != tmp) m_backend.mov(tmp, src);
                    m_backend.shr(tmp, std::to_string(shift));
                    src = tmp;
                }
                const std::string addr = m_backend.addr_fp(static_cast<int>(insn.imm_arg));
                switch (insn.dst_width) {
                    case IRWidth::I8:  m_backend.store_8(addr, src); break;
                    case IRWidth::I16: m_backend.store_16(addr, src); break;
                    case IRWidth::I32:
                    case IRWidth::F32: m_backend.store_32(addr, src); break;
                    case IRWidth::I64:
                    case IRWidth::F64: m_backend.store(addr, src); break;
                }
                break;
            }

            case IROpcode::RET: {
                // Move return value to return register
                if (!insn.operands.empty() && !is_fp_width(insn.operands[0].width)) {
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

            case IROpcode::RET2:
                // Lowered to register moves + RET_VOID by the rewriter.
                assert(false && "RET2 must be lowered before emission");
                break;

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

            case IROpcode::PUSH_FRAME:
                m_backend.push_frame_offset(static_cast<int>(insn.imm_arg));
                break;

            case IROpcode::POP:
                m_backend.pop(operand_name(insn, 0));
                break;

            // Floating point (D-038): operands are FP register numbers after lowering.
            case IROpcode::FCONST:
                m_backend.fp_const(fp_name(insn.dst), static_cast<uint64_t>(insn.imm_arg),
                                   width_bits(insn.dst_width), m_tri.name_of(m_tri.scratch_reg));
                break;
            case IROpcode::FNEG:
                m_backend.fp_neg(fp_name(insn.dst), fp_operand(insn, 0), width_bits(insn.dst_width),
                                 m_tri.name_of(m_tri.scratch_reg));
                break;
            case IROpcode::FADD:
            case IROpcode::FSUB:
            case IROpcode::FMUL:
            case IROpcode::FDIV: {
                const char op = insn.op == IROpcode::FADD ? '+' : insn.op == IROpcode::FSUB ? '-'
                              : insn.op == IROpcode::FMUL ? '*' : '/';
                m_backend.fp_arith(op, fp_name(insn.dst), fp_operand(insn, 0), fp_operand(insn, 1),
                                   width_bits(insn.dst_width));
                break;
            }
            case IROpcode::FCMP_EQ: case IROpcode::FCMP_NE: case IROpcode::FCMP_LT:
            case IROpcode::FCMP_LE: case IROpcode::FCMP_GT: case IROpcode::FCMP_GE: {
                const char* cc = insn.op == IROpcode::FCMP_EQ ? "eq" : insn.op == IROpcode::FCMP_NE ? "ne"
                               : insn.op == IROpcode::FCMP_LT ? "lt" : insn.op == IROpcode::FCMP_LE ? "le"
                               : insn.op == IROpcode::FCMP_GT ? "gt" : "ge";
                m_backend.fp_cmp(preg_name(insn.dst), cc, fp_operand(insn, 0), fp_operand(insn, 1),
                                 width_bits(insn.operands[0].width));
                break;
            }
            case IROpcode::I2F:
                m_backend.int_to_fp(fp_name(insn.dst), operand_name(insn, 0), width_bits(insn.dst_width));
                break;
            case IROpcode::F2I:
                m_backend.fp_to_int(preg_name(insn.dst), fp_operand(insn, 0),
                                    width_bits(insn.operands[0].width));
                break;
            case IROpcode::F2F:
                m_backend.fp_convert(fp_name(insn.dst), fp_operand(insn, 0),
                                     width_bits(insn.operands[0].width));
                break;
            case IROpcode::FLOAD:
                m_backend.fp_load(fp_name(insn.dst), mem_addr(insn), width_bits(insn.dst_width));
                break;
            case IROpcode::FSTORE:
                m_backend.fp_store(mem_addr(insn), fp_operand(insn, 1), width_bits(insn.operands[1].width));
                break;
            case IROpcode::FCOPY:
                assert(false && "FCOPY must be lowered before emission");
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
    std::string m_func_name;
    bool m_stack_probe = false;
    std::string m_stack_message_label;
    uint32_t m_asm_counter = 0;
    bool m_debug_info = false;
    uint32_t m_last_debug_line = 0;
    std::string m_last_debug_file;

    std::string mangle_label(const std::string& label) const {
        if (label.empty()) return label;
        if (label[0] == '.') return m_func_name + label;
        return label;
    }

    std::string fp_name(uint32_t freg) const { return m_tri.fp_name(static_cast<int>(freg)); }

    std::string fp_operand(const IRInstruction& insn, size_t idx) const {
        return fp_name(insn.operands[idx].vreg_id);
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
