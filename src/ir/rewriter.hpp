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
        // Collect all callee-save registers that were actually assigned.
        std::set<int> used_callee_save;
        for (auto& [vreg, entry] : m_alloc.entries) {
            if (entry.is_physical()) {
                for (auto& r : m_tri.regs) {
                    if (r.num == entry.preg && r.callee_save) {
                        used_callee_save.insert(entry.preg);
                        break;
                    }
                }
            }
        }

        if (used_callee_save.empty()) return;

        // Insert pushes at start of entry block.
        if (!m_func.blocks.empty()) {
            auto& entry = m_func.blocks[0];
            // Push in reverse order so they pop in correct order.
            std::vector<int> cs_regs(used_callee_save.rbegin(), used_callee_save.rend());
            for (auto preg : cs_regs) {
                IRInstruction push_insn;
                push_insn.op = IROpcode::PUSH;
                push_insn.operands.push_back(IRValue::vreg(static_cast<uint32_t>(preg)));
                entry.instructions.insert(entry.instructions.begin(), push_insn);
            }
        }

        // Insert pops before each ret/ret_void.
        for (auto& block : m_func.blocks) {
            for (size_t i = 0; i < block.instructions.size(); i++) {
                auto& insn = block.instructions[i];
                if (insn.op == IROpcode::RET || insn.op == IROpcode::RET_VOID) {
                    for (auto preg : used_callee_save) {
                        IRInstruction pop_insn;
                        pop_insn.op = IROpcode::POP;
                        pop_insn.operands.push_back(IRValue::vreg(static_cast<uint32_t>(preg)));
                        block.instructions.insert(
                            block.instructions.begin() + static_cast<long>(i), pop_insn);
                        i++; // skip past the pop we just inserted
                    }
                    break;
                }
            }
        }
    }
};
