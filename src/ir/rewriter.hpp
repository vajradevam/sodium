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
///   2. Insert spill code (loads/stores) for vregs that were spilled to stack slots.
///   3. Insert callee-save prologue (push) at the start of the entry block
///      and epilogue (pop) before each return.
class IRRewriter {
public:
    IRRewriter(const TargetRegisterInfo& tri,
               RegisterAllocation& alloc,
               IRFunction& func)
        : m_tri(tri), m_alloc(alloc), m_func(func) {}

    /// Rewrite the function in-place.
    void rewrite() {
        replace_virtual_regs();
        insert_spill_code();
        insert_callee_save_code();
    }

private:
    const TargetRegisterInfo& m_tri;
    RegisterAllocation& m_alloc;
    IRFunction& m_func;

    /// Replace all virtual register operands with physical register numbers
    /// for vregs that got a physical register. Spilled vregs are left as-is
    /// (insert_spill_code handles them next).
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

    /// Compute the fp-relative offset for a given spill slot.
    /// Slot 0 → offset -8, slot 1 → offset -16, etc.
    int spill_offset(int slot) const {
        return -(slot + 1) * 8;
    }

    /// Insert spill code for vregs that were spilled to stack slots.
    ///
    /// For each instruction, for every source operand that references a
    /// spilled vreg, insert a LOAD from the spill slot (fp-relative) into
    /// the scratch register before the instruction, and replace the operand
    /// with the scratch register.
    ///
    /// For every instruction whose destination is a spilled vreg, set the
    /// destination to the scratch register and insert a STORE from the
    /// scratch register to the spill slot after the instruction.
    ///
    /// When the first spilled source operand and a non-spilled destination
    /// share the same instruction, the first load reuses the destination
    /// register as the target (saving one scratch move).
    void insert_spill_code() {
        int scratch = m_tri.scratch_reg;
        int fp = m_tri.fp_reg;

        for (auto& block : m_func.blocks) {
            std::vector<IRInstruction> new_insns;
            for (auto& insn : block.instructions) {
                bool dst_spilled = (insn.dst != IRInstruction::NONE_VREG)
                    && m_alloc.is_spilled(insn.dst);
                uint32_t orig_dst = insn.dst;

                // --- Emit loads for spilled source operands ---
                int spilled_src_count = 0;
                for (auto& op : insn.operands) {
                    if (!op.is_vreg()) continue;
                    auto it = m_alloc.entries.find(op.vreg_id);
                    if (it == m_alloc.entries.end() || !it->second.is_spilled())
                        continue;

                    int slot = it->second.spill_slot;
                    int offset = spill_offset(slot);
                    spilled_src_count++;

                    // Choose target register for the load.
                    // If this is the first spilled source and the destination
                    // is a physical register (not spilled), reuse the
                    // destination register to avoid an extra mov.
                    uint32_t load_dst;
                    if (spilled_src_count == 1 && !dst_spilled
                        && insn.dst != IRInstruction::NONE_VREG) {
                        load_dst = insn.dst;
                    } else {
                        load_dst = static_cast<uint32_t>(scratch);
                    }

                    // LOAD load_dst, [fp + offset]
                    IRInstruction load_insn;
                    load_insn.op = IROpcode::LOAD;
                    load_insn.dst = load_dst;
                    load_insn.dst_width = IRWidth::I64;
                    load_insn.operands.push_back(
                        IRValue::vreg(static_cast<uint32_t>(fp)));
                    load_insn.imm_arg = offset;
                    new_insns.push_back(load_insn);

                    // Replace the operand with the load destination register
                    op = IRValue::vreg(load_dst);
                }

                // --- Handle spilled destination ---
                if (dst_spilled) {
                    // Set destination to scratch register
                    insn.dst = static_cast<uint32_t>(scratch);

                    // Also fix up any source operand that happens to be the
                    // same spilled vreg — it was already loaded into scratch
                    // above (or would be, but the spilled-source check above
                    // will also catch it since it iterates first).
                    //
                    // Edge case: %vreg42 = add %vreg42, %vreg1 where vreg42
                    // is spilled. The source load (above) loads from
                    // vreg42's slot into scratch. The destination is also
                    // set to scratch. The instruction computes into scratch,
                    // then the STORE (below) saves it. Correct.
                }

                // --- Emit the instruction itself ---
                new_insns.push_back(insn);

                // --- Emit store for spilled destination ---
                if (dst_spilled) {
                    auto it = m_alloc.entries.find(orig_dst);
                    assert(it != m_alloc.entries.end() && it->second.is_spilled());
                    int slot = it->second.spill_slot;
                    int offset = spill_offset(slot);

                    // STORE [fp + offset], scratch
                    IRInstruction store_insn;
                    store_insn.op = IROpcode::STORE;
                    store_insn.dst = IRInstruction::NONE_VREG;
                    store_insn.operands.push_back(
                        IRValue::vreg(static_cast<uint32_t>(fp)));
                    store_insn.imm_arg = offset;
                    store_insn.operands.push_back(
                        IRValue::vreg(static_cast<uint32_t>(scratch)));
                    new_insns.push_back(store_insn);
                }
            }
            block.instructions = new_insns;
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
                            IRInstruction copy;
                            copy.op = IROpcode::COPY;
                            copy.dst = static_cast<uint32_t>(ret_reg);
                            copy.operands.push_back(ret_op);
                            block.instructions.insert(
                                block.instructions.begin() +
                                static_cast<ptrdiff_t>(i), copy);
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
