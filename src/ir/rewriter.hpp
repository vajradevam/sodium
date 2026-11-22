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
        // Spill slots live BELOW the locals frame. Reserve room for them now
        // so they cannot collide with local variables or array slots.
        m_frame_base = static_cast<int>(m_func.stack_slots);
        const int spill_bytes = static_cast<int>(m_alloc.total_stack_slots) * 8;
        m_func.stack_slots = static_cast<uint32_t>(
            (m_frame_base + spill_bytes + 15) & ~15);

        // How many callee-save registers will be pushed at entry? The rewriter
        // inserts those pushes after this pass, but call-site alignment needs
        // the count now.
        const auto cs = m_tri.callee_save_regs();
        const std::set<int> cs_set(cs.begin(), cs.end());
        std::set<int> used_cs;
        for (const auto& [vreg, entry] : m_alloc.entries) {
            (void)vreg;
            if (entry.is_physical() && cs_set.count(entry.preg))
                used_cs.insert(entry.preg);
        }
        m_callee_save_count = static_cast<int>(used_cs.size());

        // Replacement and spill insertion must be one pass: doing replacement
        // first turns vregs into physical register *numbers*, which can equal
        // spilled vreg IDs and confuse a second lookup.
        insert_spill_code();
        insert_callee_save_code();
    }

private:
    const TargetRegisterInfo& m_tri;
    RegisterAllocation& m_alloc;
    IRFunction& m_func;
    int m_callee_save_count = 0;
    int m_frame_base = 0;   // byte offset where spill slots begin below fp

    /// Compute the fp-relative offset for a given spill slot. Slots start
    /// below the locals frame so they never overlap local variables. The result
    /// is a raw offset from the frame-pointer register (not passed through addr_fp).
    int spill_offset(int slot) const {
        return -(m_frame_base + (slot + 1) * 8) + m_tri.fp_bias;
    }

    /// Where a value lives after allocation.
    struct Loc {
        bool imm = false;
        int64_t imm_val = 0;
        bool spilled = false;
        int preg = -1;
        int spill_off = 0;
    };

    Loc locate(const IRValue& v) const {
        Loc l;
        if (v.is_imm()) {
            l.imm = true;
            l.imm_val = v.imm;
            return l;
        }
        auto it = m_alloc.entries.find(v.vreg_id);
        assert(it != m_alloc.entries.end());
        if (it->second.is_spilled()) {
            l.spilled = true;
            l.spill_off = spill_offset(it->second.spill_slot);
        } else {
            l.preg = it->second.preg;
        }
        return l;
    }

    Loc locate_vreg(uint32_t vreg) const { return locate(IRValue::vreg(vreg)); }

    static IRInstruction phys_copy(int dst, int src) {
        IRInstruction c;
        c.op = IROpcode::COPY;
        c.dst = static_cast<uint32_t>(dst);
        c.operands.push_back(IRValue::vreg(static_cast<uint32_t>(src)));
        return c;
    }

    IRInstruction phys_load_slot(int dst, int offset) const {
        IRInstruction l;
        l.op = IROpcode::LOAD;
        l.dst = static_cast<uint32_t>(dst);
        l.dst_width = IRWidth::I64;
        l.operands.push_back(IRValue::vreg(static_cast<uint32_t>(m_tri.fp_reg)));
        l.imm_arg = offset;
        return l;
    }

    IRInstruction phys_store_slot(int offset, int src) const {
        IRInstruction st;
        st.op = IROpcode::STORE;
        st.operands.push_back(IRValue::vreg(static_cast<uint32_t>(m_tri.fp_reg)));
        st.imm_arg = offset;
        st.operands.push_back(IRValue::vreg(static_cast<uint32_t>(src)));
        return st;
    }

    /// RET2 lo, hi  =>  moves into the two return registers, then RET_VOID.
    /// The low value is parked in the scratch register first so that either
    /// operand may already live in the other's return register.
    void lower_ret2(const IRInstruction& insn, std::vector<IRInstruction>& out) const {
        const int scratch = m_tri.scratch_reg;
        const int r1 = m_tri.ret_reg;
        const int r2 = m_tri.ret_reg2;
        const Loc lo = locate(insn.operands[0]);
        const Loc hi = locate(insn.operands[1]);
        if (!lo.imm) {
            out.push_back(lo.spilled ? phys_load_slot(scratch, lo.spill_off)
                                     : phys_copy(scratch, lo.preg));
        }
        if (hi.imm) {
            out.push_back(IRInstruction::load_i64(static_cast<uint32_t>(r2), hi.imm_val));
        } else {
            out.push_back(hi.spilled ? phys_load_slot(r2, hi.spill_off) : phys_copy(r2, hi.preg));
        }
        if (lo.imm) {
            out.push_back(IRInstruction::load_i64(static_cast<uint32_t>(r1), lo.imm_val));
        } else {
            out.push_back(phys_copy(r1, scratch));
        }
        out.push_back(IRInstruction::ret_void());
    }

    /// Moves that capture a two-word call result (rax:rdx / a0:a1) into the homes
    /// of `dst` and `dst2`. The second word is parked in scratch first, so a home
    /// that is itself a return register cannot clobber the other word.
    std::vector<IRInstruction> capture_call_results(uint32_t dst, uint32_t dst2) const {
        const int scratch = m_tri.scratch_reg;
        const int r1 = m_tri.ret_reg;
        const int r2 = m_tri.ret_reg2;
        const Loc d1 = locate_vreg(dst);
        const Loc d2 = locate_vreg(dst2);
        std::vector<IRInstruction> seq;
        seq.push_back(phys_copy(scratch, r2));
        seq.push_back(d1.spilled ? phys_store_slot(d1.spill_off, r1) : phys_copy(d1.preg, r1));
        seq.push_back(d2.spilled ? phys_store_slot(d2.spill_off, scratch) : phys_copy(d2.preg, scratch));
        return seq;
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
                // Stack-passed CALL arguments are pushed here, one at a time,
                // so a spilled argument is loaded and immediately consumed.
                // Loading them all into one scratch register first would let
                // later loads overwrite earlier argument values.
                if (insn.op == IROpcode::CALL) {
                    const size_t n_arg_regs = m_tri.arg_regs.size();
                    const size_t n_reg = insn.reg_args >= 0
                        ? static_cast<size_t>(insn.reg_args)
                        : std::min(insn.operands.size(), n_arg_regs);
                    assert(n_reg <= n_arg_regs && n_reg <= insn.operands.size());
                    const int stack_args = static_cast<int>(insn.operands.size() - n_reg);
                    if (stack_args > 0) {
                        // Padding (if any) is pushed FIRST so it sits above
                        // the stack arguments in memory and does not shift
                        // their [fp + offset] locations.
                        int pad = ((m_callee_save_count + stack_args) & 1) ? 1 : 0;
                        if (pad) {
                            IRInstruction dummy;
                            dummy.op = IROpcode::PUSH;
                            dummy.operands.push_back(IRValue::vreg(static_cast<uint32_t>(scratch)));
                            new_insns.push_back(dummy);
                        }
                        for (size_t i = insn.operands.size(); i-- > n_reg;) {
                            auto& op = insn.operands[i];
                            if (op.is_vreg()) {
                                auto it = m_alloc.entries.find(op.vreg_id);
                                if (it != m_alloc.entries.end() && it->second.is_spilled()) {
                                    IRInstruction push_frame;
                                    push_frame.op = IROpcode::PUSH_FRAME;
                                    push_frame.imm_arg = spill_offset(it->second.spill_slot);
                                    new_insns.push_back(push_frame);
                                    continue;
                                }
                                if (it != m_alloc.entries.end() && it->second.is_physical()) {
                                    op.vreg_id = static_cast<uint32_t>(it->second.preg);
                                }
                            }
                            IRInstruction push;
                            push.op = IROpcode::PUSH;
                            push.operands.push_back(op);
                            new_insns.push_back(push);
                        }
                        // Keep SP 16-byte aligned at the call: the prologue
                        // leaves it aligned and every push toggles parity.
                        insn.imm_arg = (stack_args + pad) * 8;
                        insn.operands.resize(n_reg);
                    }
                }

                if (insn.op == IROpcode::RET2) {
                    lower_ret2(insn, new_insns);
                    continue;
                }

                // A two-word call result is captured by explicit moves after the call.
                std::vector<IRInstruction> result_moves;
                if (insn.op == IROpcode::CALL && insn.dst2 != IRInstruction::NONE_VREG) {
                    result_moves = capture_call_results(insn.dst, insn.dst2);
                    insn.dst = IRInstruction::NONE_VREG;
                    insn.dst2 = IRInstruction::NONE_VREG;
                }

                uint32_t orig_dst = insn.dst;
                bool dst_spilled = false;
                if (insn.dst != IRInstruction::NONE_VREG) {
                    auto it = m_alloc.entries.find(insn.dst);
                    if (it != m_alloc.entries.end()) {
                        if (it->second.is_physical()) {
                            insn.dst = static_cast<uint32_t>(it->second.preg);
                        } else if (it->second.is_spilled()) {
                            dst_spilled = true;
                            insn.dst = static_cast<uint32_t>(scratch);
                        }
                    }
                }

                // CALL register arguments: physical ones become their register
                // number; spilled ones are loaded by the emitter straight into
                // their argument register (after the parallel register moves),
                // never through the shared scratch register.
                if (insn.op == IROpcode::CALL) {
                    for (size_t i = 0; i < insn.operands.size(); i++) {
                        auto& op = insn.operands[i];
                        if (!op.is_vreg()) continue;
                        auto it = m_alloc.entries.find(op.vreg_id);
                        if (it == m_alloc.entries.end()) continue;
                        if (it->second.is_physical()) {
                            op.vreg_id = static_cast<uint32_t>(it->second.preg);
                        } else if (it->second.is_spilled()) {
                            int areg = m_tri.arg_regs[i];
                            insn.arg_loads.push_back({ areg, spill_offset(it->second.spill_slot) });
                            op = IRValue::vreg(static_cast<uint32_t>(areg));
                        }
                    }
                }

                // --- Emit loads for spilled source operands ---
                int spilled_src_count = 0;
                for (auto& op : insn.operands) {
                    if (insn.op == IROpcode::CALL) break;
                    if (!op.is_vreg()) continue;
                    auto it = m_alloc.entries.find(op.vreg_id);
                    if (it == m_alloc.entries.end()) continue;
                    if (it->second.is_physical()) {
                        op.vreg_id = static_cast<uint32_t>(it->second.preg);
                        continue;
                    }
                    if (!it->second.is_spilled()) continue;

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
                    // Destination was already switched to scratch above.
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

                for (auto& move : result_moves) new_insns.push_back(move);
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
