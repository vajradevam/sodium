#pragma once

#include "function.hpp"
#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cassert>

/// A live range interval [start, end) for a virtual register.
struct LiveRange {
    uint32_t vreg = 0;
    uint32_t start = 0;   ///< first instruction index where live
    uint32_t end = 0;     ///< one past last instruction index where live
    bool spans_call = false;  ///< range includes a CALL instruction

    bool overlaps(const LiveRange& other) const {
        return start < other.end && other.start < end;
    }
};

/// Result of liveness analysis: a list of live ranges (one per vreg).
using LiveRanges = std::vector<LiveRange>;

/// Linear instruction numbering for a function.
struct InstructionNumbering {
    /// Total instruction count.
    uint32_t count = 0;

    /// For each block, the start index of its first instruction.
    std::map<std::string, uint32_t> block_starts;
};

/// Compute a linear numbering of all instructions across all blocks.
inline InstructionNumbering number_instructions(const IRFunction& func) {
    InstructionNumbering num;
    uint32_t idx = 0;
    for (auto& block : func.blocks) {
        num.block_starts[block.label] = idx;
        idx += static_cast<uint32_t>(block.instructions.size());
    }
    num.count = idx;
    return num;
}

/// Compute live intervals for all virtual registers in a function.
///
/// For each virtual register, the live interval spans from its first
/// definition (or use) to its last use. This is a conservative
/// approximation — it may overestimate liveness, but it's correct
/// and sufficient for linear scan allocation.
///
/// Also marks intervals that span CALL instructions (values that
/// must survive a function call and therefore need a callee-save
/// register to avoid being clobbered).
inline LiveRanges compute_live_ranges(const IRFunction& func) {
    // Number all instructions linearly.
    auto numbering = number_instructions(func);

    // For each vreg, track first def/use and last use.
    std::map<uint32_t, uint32_t> first_pos;
    std::map<uint32_t, uint32_t> last_pos;
    std::set<uint32_t> all_vregs;

    // Track positions of CALL instructions.
    std::set<uint32_t> call_positions;

    uint32_t idx = 0;
    for (auto& block : func.blocks) {
        for (auto& insn : block.instructions) {
            // Track CALL positions (values that span a call need
            // callee-save registers to avoid being clobbered)
            if (insn.op == IROpcode::CALL) {
                call_positions.insert(idx);
            }

            // Destination (definition)
            if (insn.dst != IRInstruction::NONE_VREG) {
                if (first_pos.find(insn.dst) == first_pos.end())
                    first_pos[insn.dst] = idx;
                last_pos[insn.dst] = idx;
                all_vregs.insert(insn.dst);
            }

            // Source operands (uses)
            for (auto& op : insn.operands) {
                if (op.is_vreg()) {
                    if (first_pos.find(op.vreg_id) == first_pos.end())
                        first_pos[op.vreg_id] = idx;
                    last_pos[op.vreg_id] = idx;
                    all_vregs.insert(op.vreg_id);
                }
            }

            idx++;
        }
    }

    // Build live ranges.
    LiveRanges ranges;
    for (auto vreg : all_vregs) {
        LiveRange r;
        r.vreg = vreg;
        r.start = first_pos[vreg];
        r.end = last_pos[vreg] + 1;  // one past last use
        if (r.end > numbering.count) r.end = numbering.count;

        // Check if this interval spans any CALL instruction.
        r.spans_call = false;
        for (uint32_t cp : call_positions) {
            if (cp > r.start && cp < r.end) {
                r.spans_call = true;
                break;
            }
        }

        ranges.push_back(r);
    }

    // Sort by start position (required by linear scan).
    std::sort(ranges.begin(), ranges.end(),
              [](const LiveRange& a, const LiveRange& b) {
                  return a.start < b.start;
              });

    return ranges;
}
