#pragma once

#include "linear_scan.hpp"
#include "function.hpp"
#include "instruction.hpp"
#include "target_regs.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <algorithm>
#include <cassert>

/// Applies a RegisterAllocation to an IRFunction.
///
/// Steps:
///   1. Replace virtual register references with physical register numbers
///      for all vregs that got a physical register.
///   2. Insert callee-save prologue (push) at the start of the entry block
///      and epilogue (pop) before each return.
///
/// Spill code (loads/stores for spilled vregs) is NOT inserted here —
/// the backend handles it, since it knows the target's addressing modes
/// and scratch register availability. The allocator marks spilled vregs;
/// the backend emits frame_addr + load/store around their uses/defs.
class IRRewriter {
public:
    IRRewriter(const TargetRegisterInfo& tri,
               RegisterAllocation& alloc,
               IRFunction& func)
        : m_tri(tri), m_alloc(alloc), m_func(func) {}

    /// Rewrite the function in-place.
    void rewrite() {
        replace_virtual_regs();
        insert_callee_save_code();
    }

private:
    const TargetRegisterInfo& m_tri;
    RegisterAllocation& m_alloc;
    IRFunction& m_func;

    /// Replace all virtual register operands with physical register numbers
    /// for vregs that got a physical register. Spilled vregs are left as-is
    /// (the backend will handle them).
    void replace_virtual_regs() {
        for (auto& block : m_func.blocks) {
            for (auto& insn : block.instructions) {
                // Destination
                if (insn.dst != IRInstruction::NONE_VREG) {
                    auto it = m_alloc.entries.find(insn.dst);
                    if (it != m_alloc.entries.end() && it->second.is_physical()) {
                        insn.dst = static_cast<uint32_t>(it->second.preg);
                    }
                }

                // Source operands
                for (auto& op : insn.operands) {
                    if (op.is_vreg()) {
                        auto it = m_alloc.entries.find(op.vreg_id);
                        if (it != m_alloc.entries.end() && it->second.is_physical()) {
                            op.vreg_id = static_cast<uint32_t>(it->second.preg);
                        }
                    }
                }
            }
        }
    }

    /// Insert callee-save register pushes in the entry block and pops
    /// before each return.
    void insert_callee_save_code() {
        // Collect callee-save physical registers that are actually used
        // in this function (after virtual regs have been replaced).
        std::set<int> used_callee_save;
        auto callee_save = m_tri.callee_save_regs();
        std::set<int> cs_set(callee_save.begin(), callee_save.end());

        for (auto& block : m_func.blocks) {
            for (auto& insn : block.instructions) {
                // Destination register
                if (insn.dst != IRInstruction::NONE_VREG) {
                    if (cs_set.count(static_cast<int>(insn.dst)))
                        used_callee_save.insert(static_cast<int>(insn.dst));
                }
                // Source operands
                for (auto& op : insn.operands) {
                    if (op.is_vreg()) {
                        if (cs_set.count(static_cast<int>(op.vreg_id)))
                            used_callee_save.insert(static_cast<int>(op.vreg_id));
                    }
                }
            }
        }

        if (used_callee_save.empty()) return;

        // Insert PUSH instructions at the start of the entry block
        // (after the first block's label, before any other instructions).
        auto& entry_block = m_func.blocks[0];
        std::vector<IRInstruction> pushes;
        for (int preg : used_callee_save) {
            IRInstruction push;
            push.op = IROpcode::PUSH;
            push.operands.push_back(IRValue::vreg(static_cast<uint32_t>(preg)));
            pushes.push_back(push);
        }
        entry_block.instructions.insert(entry_block.instructions.begin(),
                                        pushes.begin(), pushes.end());

        // Insert POP instructions (in reverse order) before each RET/RET_VOID.
        // For RET with a return value, we must ensure the return value is
        // moved to the return register (rax) BEFORE the POPs, since a POP
        // would clobber a callee-save register holding the return value.
        int ret_reg = m_tri.ret_reg;
        for (auto& block : m_func.blocks) {
            for (size_t i = 0; i < block.instructions.size(); i++) {
                auto& insn = block.instructions[i];
                if (insn.op == IROpcode::RET) {
                    // Move return value to return register before POPs
                    if (!insn.operands.empty()) {
                        auto& ret_op = insn.operands[0];
                        if (ret_op.is_vreg() &&
                            static_cast<int>(ret_op.vreg_id) != ret_reg) {
                            // If the return value is in a callee-save register
                            // (or any non-rax register), insert COPY to rax first
                            IRInstruction copy;
                            copy.op = IROpcode::COPY;
                            copy.dst = static_cast<uint32_t>(ret_reg);
                            copy.operands.push_back(ret_op);
                            block.instructions.insert(
                                block.instructions.begin() +
                                static_cast<ptrdiff_t>(i), copy);
                            // Update the RET to reference rax instead
                            ret_op = IRValue::vreg(static_cast<uint32_t>(ret_reg));
                            i++; // skip past the COPY
                        }
                    }
                    // Now insert POPs before the RET
                    std::vector<IRInstruction> pops;
                    for (auto it = used_callee_save.rbegin();
                         it != used_callee_save.rend(); ++it) {
                        IRInstruction pop;
                        pop.op = IROpcode::POP;
                        pop.operands.push_back(
                            IRValue::vreg(static_cast<uint32_t>(*it)));
                        pops.push_back(pop);
                    }
                    block.instructions.insert(
                        block.instructions.begin() + static_cast<ptrdiff_t>(i),
                        pops.begin(), pops.end());
                    i += pops.size();
                } else if (insn.op == IROpcode::RET_VOID) {
                    // Just insert POPs before the RET_VOID
                    std::vector<IRInstruction> pops;
                    for (auto it = used_callee_save.rbegin();
                         it != used_callee_save.rend(); ++it) {
                        IRInstruction pop;
                        pop.op = IROpcode::POP;
                        pop.operands.push_back(
                            IRValue::vreg(static_cast<uint32_t>(*it)));
                        pops.push_back(pop);
                    }
                    block.instructions.insert(
                        block.instructions.begin() + static_cast<ptrdiff_t>(i),
                        pops.begin(), pops.end());
                    i += pops.size();
                }
            }
        }
    }
};
