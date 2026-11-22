#pragma once

#include "liveness.hpp"
#include "target_regs.hpp"
#include "function.hpp"
#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

/// A per-virtual-register allocation decision.
struct RegAllocEntry {
    enum Kind : uint8_t {
        UNALLOCATED = 0,
        PHYSICAL,    /// Assigned to a physical register
        SPILLED,     /// Spilled to a stack slot
    };

    Kind kind = UNALLOCATED;
    int preg = -1;           ///< Physical register number (for PHYSICAL)
    int spill_slot = -1;     ///< Stack slot index (for SPILLED)

    bool is_physical() const { return kind == PHYSICAL; }
    bool is_spilled() const  { return kind == SPILLED;  }
    bool is_allocated() const { return kind != UNALLOCATED; }
};

/// The result of register allocation: mapping vreg → RegAllocEntry.
/// Also includes the number of stack slots needed for spills and callee-saves.
struct RegisterAllocation {
    std::map<uint32_t, RegAllocEntry> entries;

    /// Total stack slots needed (spills + callee-save pushes).
    uint32_t total_stack_slots = 0;

    /// Counters exposed by --dump-regalloc for allocator benchmarks.
    size_t vreg_count = 0;
    size_t spill_count = 0;
    size_t callee_save_count = 0;

    const RegAllocEntry& get(uint32_t vreg) const {
        static RegAllocEntry unallocated;
        auto it = entries.find(vreg);
        if (it != entries.end()) return it->second;
        return unallocated;
    }

    bool is_physical(uint32_t vreg) const {
        auto& e = get(vreg);
        return e.is_physical();
    }

    bool is_spilled(uint32_t vreg) const {
        auto& e = get(vreg);
        return e.is_spilled();
    }

    int preg_of(uint32_t vreg) const {
        auto& e = get(vreg);
        return e.preg;
    }

    int slot_of(uint32_t vreg) const {
        auto& e = get(vreg);
        return e.spill_slot;
    }
};

/// A live interval currently occupying a physical register (for the active list).
struct ActiveInterval {
    uint32_t vreg;
    int phys_reg;
    uint32_t end_pos;
    uint32_t next_use = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> uses;
    bool needs_callee_save = false;
};

/// First recorded use at or after `pos`, or UINT32_MAX when there is none.
inline uint32_t next_use_at(const std::vector<uint32_t>& uses, uint32_t pos) {
    auto it = std::lower_bound(uses.begin(), uses.end(), pos);
    return it == uses.end() ? std::numeric_limits<uint32_t>::max() : *it;
}

/// Linear scan register allocator.
///
/// Algorithm (simplified):
///   1. Compute live intervals for all vregs.
///   2. Sort intervals by start position.
///   3. For each interval in order:
///      a. Expire old intervals (end <= current start).
///      b. If a free register is available, assign it.
///      c. Otherwise, evict the value whose next use is farthest away.
///
/// This is a straightforward implementation of the classic linear
/// scan algorithm (Poletto & Sarkar, 1999).
class LinearScanAllocator {
public:
    explicit LinearScanAllocator(const TargetRegisterInfo& tri)
        : m_tri(tri) {}

    /// Allocate registers for the given function.
    /// Returns the allocation and modifies the function to include spill code.
    RegisterAllocation allocate(IRFunction& func) {
        m_allocation = RegisterAllocation{};
        m_next_spill_slot = 0;

        // Step 1: Compute live intervals.
        auto ranges = compute_live_ranges(func);

        // Step 2: Allocate.
        // Use all allocatable registers. For values that span CALL
        // instructions, prefer callee-save registers (which survive calls).
        // Caller-save registers get clobbered by CALL instructions,
        // so we avoid assigning them to values that span calls.
        auto all_regs = m_tri.allocatable_regs();

        for (auto& range : ranges) {
            // 2a. Expire old intervals.
            expire_old_intervals(range.start);

            // 2b. Register candidates. A value that is live across a CALL
            // may ONLY live in a callee-save register: caller-save registers
            // are clobbered by the callee. If no callee-save register can be
            // had, the value is spilled to the stack (see spill()).
            std::vector<int> preferred;
            if (range.spans_call || range.crosses_div) {
                // Callee-save registers survive both calls and idiv's implicit
                // rax/rdx clobber.
                preferred = m_tri.callee_save_regs();
            } else {
                preferred = all_regs;
            }

            // 2c. Find a free register.
            int free_reg = find_free_register(preferred);

            if (free_reg >= 0) {
                assign_register(range, free_reg, range.start);
            } else {
                // 2d. Need to spill.
                spill(range, preferred, range.start);
            }
        }

        // Step 3: Assign stack slots and count what happened.
        m_allocation.total_stack_slots = m_next_spill_slot;
        const std::vector<int> callee_save = m_tri.callee_save_regs();
        for (const auto& [vreg, entry] : m_allocation.entries) {
            (void)vreg;
            if (entry.is_spilled()) ++m_allocation.spill_count;
            else if (std::find(callee_save.begin(), callee_save.end(), entry.preg) !=
                     callee_save.end()) {
                ++m_allocation.callee_save_count;
            }
        }
        m_allocation.vreg_count = m_allocation.entries.size();

        return m_allocation;
    }

private:
    const TargetRegisterInfo& m_tri;
    RegisterAllocation m_allocation;
    std::vector<ActiveInterval> m_active;
    int m_next_spill_slot = 0;

    void expire_old_intervals(uint32_t current_start) {
        m_active.erase(
            std::remove_if(m_active.begin(), m_active.end(),
                [current_start](const ActiveInterval& ai) {
                    return ai.end_pos <= current_start;
                }),
            m_active.end()
        );
        for (auto& ai : m_active) {
            ai.next_use = next_use_at(ai.uses, current_start);
        }
    }

    int find_free_register(const std::vector<int>& available) {
        for (int preg : available) {
            bool in_use = false;
            for (auto& ai : m_active) {
                if (ai.phys_reg == preg) {
                    in_use = true;
                    break;
                }
            }
            if (!in_use) return preg;
        }
        return -1;  // all registers are occupied
    }

    void assign_register(const LiveRange& range, int preg, uint32_t pos) {
        RegAllocEntry entry;
        entry.kind = RegAllocEntry::PHYSICAL;
        entry.preg = preg;
        m_allocation.entries[range.vreg] = entry;

        ActiveInterval ai;
        ai.vreg = range.vreg;
        ai.phys_reg = preg;
        ai.end_pos = range.end;
        ai.uses = range.use_positions;
        ai.next_use = next_use_at(ai.uses, pos);
        ai.needs_callee_save = range.spans_call || range.crosses_div;
        m_active.push_back(std::move(ai));
    }

    /// No register in `available` is free for `range`. Evict the active value
    /// whose next use is farthest away (so the nearest use keeps its register),
    /// unless that value needs a callee-save register and this one does not.
    void spill(const LiveRange& range, const std::vector<int>& available, uint32_t pos) {
        const bool current_protected = range.spans_call || range.crosses_div;
        const uint32_t current_next = next_use_at(range.use_positions, pos);
        int victim = -1;
        uint32_t victim_next = 0;
        for (size_t i = 0; i < m_active.size(); ++i) {
            const ActiveInterval& ai = m_active[i];
            if (std::find(available.begin(), available.end(), ai.phys_reg) == available.end()) {
                continue;
            }
            if (ai.needs_callee_save && !current_protected) continue;
            if (victim < 0 || ai.next_use > victim_next) {
                victim = static_cast<int>(i);
                victim_next = ai.next_use;
            }
        }

        if (victim >= 0 && victim_next > current_next) {
            const int freed_reg = m_active[victim].phys_reg;
            m_allocation.entries[m_active[victim].vreg] = make_spill_entry();
            m_active.erase(m_active.begin() + victim);
            assign_register(range, freed_reg, pos);
        } else {
            m_allocation.entries[range.vreg] = make_spill_entry();
        }
    }

    RegAllocEntry make_spill_entry() {
        RegAllocEntry entry;
        entry.kind = RegAllocEntry::SPILLED;
        entry.spill_slot = m_next_spill_slot++;
        return entry;
    }
};
