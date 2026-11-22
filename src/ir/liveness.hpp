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
    bool spans_call = false;    ///< range includes a CALL instruction
    bool crosses_div = false;   ///< range includes a DIV/MOD, whose rax/rdx clobber is implicit

    /// Instruction positions where the vreg is read (sorted), used by the
    /// allocator to prefer spilling the value whose next use is farthest away.
    std::vector<uint32_t> use_positions;

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
/// Liveness is a real CFG dataflow computation: per-block live-in/live-out sets
/// are iterated to a fixpoint, so a value used early in a loop body is still
/// live around the back edge and across a later CALL. Each interval then spans
/// every block where the value is live (the linear scan allocator only accepts
/// a single interval per vreg), from its earliest live point to its latest one.
/// Intervals may be conservative when live blocks are not contiguous in layout,
/// but they are never shorter than the value's true lifetime.
inline LiveRanges compute_live_ranges(const IRFunction& func) {
    auto numbering = number_instructions(func);
    if (func.blocks.empty()) return {};

    const size_t block_count = func.blocks.size();
    std::map<std::string, size_t> label_index;
    for (size_t i = 0; i < block_count; ++i) {
        label_index.emplace(func.blocks[i].label, i);
    }

    // CFG successors: the first terminator's targets, or the next block when
    // the block falls through. Dead instructions after a terminator are not
    // successors.
    std::vector<std::vector<size_t>> succ(block_count);
    for (size_t bi = 0; bi < block_count; ++bi) {
        const auto& insns = func.blocks[bi].instructions;
        size_t term = insns.size();
        for (size_t ii = 0; ii < insns.size(); ++ii) {
            if (insns[ii].op == IROpcode::JMP || insns[ii].op == IROpcode::BR ||
                insns[ii].op == IROpcode::RET || insns[ii].op == IROpcode::RET2 ||
                insns[ii].op == IROpcode::RET_VOID) {
                term = ii;
                break;
            }
        }
        if (term == insns.size()) {
            if (bi + 1 < block_count) succ[bi].push_back(bi + 1);
        } else {
            const auto& insn = insns[term];
            auto add_target = [&](const std::string& label) {
                auto it = label_index.find(label);
                if (it != label_index.end()) succ[bi].push_back(it->second);
            };
            if (insn.op == IROpcode::JMP) add_target(insn.label_true);
            else if (insn.op == IROpcode::BR) {
                add_target(insn.label_true);
                add_target(insn.label_false);
            }
        }
    }

    // Upward-exposed uses and definitions of each block.
    std::vector<std::set<uint32_t>> defs(block_count), uses(block_count);
    for (size_t bi = 0; bi < block_count; ++bi) {
        std::set<uint32_t> seen;
        for (const auto& insn : func.blocks[bi].instructions) {
            for (const auto& op : insn.operands) {
                if (op.is_vreg() && seen.count(op.vreg_id) == 0) {
                    uses[bi].insert(op.vreg_id);
                }
            }
            if (insn.dst != IRInstruction::NONE_VREG) {
                seen.insert(insn.dst);
                defs[bi].insert(insn.dst);
            }
            if (insn.dst2 != IRInstruction::NONE_VREG) {
                seen.insert(insn.dst2);
                defs[bi].insert(insn.dst2);
            }
        }
    }

    std::vector<std::set<uint32_t>> live_in(block_count), live_out(block_count);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = block_count; k-- > 0;) {
            std::set<uint32_t> out;
            for (size_t next : succ[k]) {
                out.insert(live_in[next].begin(), live_in[next].end());
            }
            if (out != live_out[k]) { live_out[k] = std::move(out); changed = true; }

            std::set<uint32_t> in = uses[k];
            for (uint32_t v : live_out[k]) {
                if (defs[k].count(v) == 0) in.insert(v);
            }
            if (in != live_in[k]) { live_in[k] = std::move(in); changed = true; }
        }
    }

    // Linear positions of definitions, uses and calls.
    struct Extent {
        bool seen = false;
        uint32_t first = 0;
        uint32_t last = 0;
        std::vector<uint32_t> uses;
    };
    std::map<uint32_t, Extent> extents;
    std::set<uint32_t> call_positions;
    std::set<uint32_t> div_positions;
    auto extend = [&](uint32_t vreg, uint32_t pos) {
        auto& e = extents[vreg];
        if (!e.seen) { e.seen = true; e.first = pos; e.last = pos; }
        else {
            e.first = std::min(e.first, pos);
            e.last = std::max(e.last, pos);
        }
    };

    uint32_t idx = 0;
    for (size_t bi = 0; bi < block_count; ++bi) {
        for (const auto& insn : func.blocks[bi].instructions) {
            if (is_call(insn.op)) call_positions.insert(idx);
            // X5a: treat ASM as a conservative clobber barrier — values live
            // across it use callee-save registers until X5b tracks exact regs.
            if (insn.op == IROpcode::ASM) call_positions.insert(idx);
            if (insn.op == IROpcode::DIV || insn.op == IROpcode::MOD) div_positions.insert(idx);
            if (insn.dst != IRInstruction::NONE_VREG) extend(insn.dst, idx);
            if (insn.dst2 != IRInstruction::NONE_VREG) extend(insn.dst2, idx);
            for (const auto& op : insn.operands) {
                if (op.is_vreg()) {
                    extend(op.vreg_id, idx);
                    extents[op.vreg_id].uses.push_back(idx);
                }
            }
            ++idx;
        }
    }

    // A live-in at a block start and a live-out at a block end extend the
    // interval even when there is no use at that exact boundary in layout.
    for (size_t bi = 0; bi < block_count; ++bi) {
        const uint32_t start = numbering.block_starts.at(func.blocks[bi].label);
        const uint32_t end = start + static_cast<uint32_t>(func.blocks[bi].instructions.size());
        for (uint32_t v : live_in[bi]) extend(v, start);
        for (uint32_t v : live_out[bi]) extend(v, end == 0 ? 0 : end - 1);
    }

    LiveRanges ranges;
    for (const auto& [vreg, extent] : extents) {
        LiveRange r;
        r.vreg = vreg;
        r.use_positions = extent.uses;  // already in increasing instruction order
        r.start = extent.first;
        r.end = extent.last + 1;
        if (r.end > numbering.count) r.end = numbering.count;
        if (r.end <= r.start) r.end = std::min(numbering.count, r.start + 1);

        for (uint32_t cp : call_positions) {
            // A call that defines the value itself (cp == start) does not make
            // it live across a call; a later call within the interval does.
            if (cp > r.start && cp + 1 < r.end) { r.spans_call = true; break; }
        }
        // idiv writes rax/rdx without the allocator knowing. A value live
        // across it must use a register idiv does not touch, just like a value
        // live across a call.
        for (uint32_t dp : div_positions) {
            if (dp > r.start && dp + 1 < r.end) { r.crosses_div = true; break; }
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
