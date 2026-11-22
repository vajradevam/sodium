#pragma once

#include "function.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

/// What the CFG cleanup pass removed.
struct CleanupStats {
    size_t removed_instructions = 0;
    size_t removed_blocks = 0;
    size_t threaded_jumps = 0;
};

namespace ir_cleanup_detail {

inline bool is_terminator(IROpcode op) {
    switch (op) {
        case IROpcode::JMP:
        case IROpcode::BR:
        case IROpcode::RET:
        case IROpcode::RET2:
        case IROpcode::RET_VOID:
            return true;
        default:
            return false;
    }
}

/// Results that may be dropped when nobody uses them. Memory loads are
/// deliberately excluded: removing a load could remove a fault, and this pass
/// must be semantics-preserving for every input, not just well-formed ones.
inline bool is_removable_result(IROpcode op) {
    switch (op) {
        case IROpcode::LOAD_I64:
        case IROpcode::LOAD_I32:
        case IROpcode::LOAD_I8:
        case IROpcode::LOAD_U64:
        case IROpcode::LOAD_U32_IMM:
        case IROpcode::LOAD_U8_IMM:
        case IROpcode::COPY:
        case IROpcode::FRAME_ADDR:
        case IROpcode::NEG:
        case IROpcode::NOT_:
        case IROpcode::ZEXT:
        case IROpcode::SEXT:
        case IROpcode::TRUNC:
        case IROpcode::ADD:
        case IROpcode::SUB:
        case IROpcode::MUL:
        case IROpcode::DIV:
        case IROpcode::MOD:
        case IROpcode::AND:
        case IROpcode::OR:
        case IROpcode::XOR:
        case IROpcode::SHL:
        case IROpcode::SHR:
        case IROpcode::ASHR:
        case IROpcode::CMP_EQ:
        case IROpcode::CMP_NE:
        case IROpcode::CMP_LT:
        case IROpcode::CMP_LE:
        case IROpcode::CMP_GT:
        case IROpcode::CMP_GE:
        case IROpcode::LEA:
        case IROpcode::LEA_LABEL:
        case IROpcode::LOAD_PARAM:
            return true;
        default:
            return false;
    }
}

} // namespace ir_cleanup_detail

/// Remove dead instructions and unreachable blocks, drop the dead tail after
/// a block's first terminator, and thread jumps through empty / jmp-only
/// blocks. The entry block is never removed.
inline CleanupStats cleanup_function(IRFunction& f) {
    using namespace ir_cleanup_detail;
    CleanupStats stats;
    if (f.blocks.empty()) return stats;

    std::map<uint32_t, size_t> use_counts;
    for (const auto& block : f.blocks) {
        for (const auto& insn : block.instructions) {
            for (const auto& op : insn.operands) {
                if (op.is_vreg()) ++use_counts[op.vreg_id];
            }
        }
    }

    // Drop unreachable-instruction tails and unused pure results. A vreg with
    // several definitions (a ternary's shared result) is kept as a whole: if
    // any use exists, every definition must stay so dominance still holds.
    for (auto& block : f.blocks) {
        bool terminated = false;
        std::vector<IRInstruction> kept;
        for (auto& insn : block.instructions) {
            if (terminated) { ++stats.removed_instructions; continue; }
            if (is_terminator(insn.op)) terminated = true;
            if (insn.op == IROpcode::NOP ||
                (is_removable_result(insn.op) && insn.dst != IRInstruction::NONE_VREG &&
                 use_counts.find(insn.dst) == use_counts.end() &&
                 (insn.dst2 == IRInstruction::NONE_VREG ||
                  use_counts.find(insn.dst2) == use_counts.end()))) {
                ++stats.removed_instructions;
                continue;
            }
            kept.push_back(std::move(insn));
        }
        block.instructions = std::move(kept);
    }

    std::map<std::string, size_t> label_index;
    for (size_t i = 0; i < f.blocks.size(); ++i) {
        label_index.emplace(f.blocks[i].label, i);
    }
    auto successors = [&](size_t bi, std::vector<size_t>& out) {
        const auto& insns = f.blocks[bi].instructions;
        for (const auto& insn : insns) {
            if (!is_terminator(insn.op)) continue;
            if (insn.op == IROpcode::JMP || insn.op == IROpcode::BR) {
                auto add = [&](const std::string& label) {
                    auto it = label_index.find(label);
                    if (it != label_index.end()) out.push_back(it->second);
                };
                add(insn.label_true);
                if (insn.op == IROpcode::BR) add(insn.label_false);
            }
            return;
        }
        if (bi + 1 < f.blocks.size()) out.push_back(bi + 1);  // fallthrough
    };

    std::vector<bool> reachable(f.blocks.size(), false);
    std::vector<size_t> work{0};
    reachable[0] = true;
    while (!work.empty()) {
        size_t b = work.back();
        work.pop_back();
        std::vector<size_t> next;
        successors(b, next);
        for (size_t s : next) {
            if (!reachable[s]) { reachable[s] = true; work.push_back(s); }
        }
    }
    std::vector<IRBlock> live;
    for (size_t i = 0; i < f.blocks.size(); ++i) {
        if (reachable[i]) live.push_back(std::move(f.blocks[i]));
    }
    stats.removed_blocks += f.blocks.size() - live.size();
    f.blocks = std::move(live);
    if (f.blocks.empty()) return stats;

    // Thread through blocks that contain nothing but a jump (or nothing at
    // all), redirecting every branch that names them. Repeat to a fixpoint so
    // chains of jumps collapse in one pass.
    bool changed = true;
    while (changed) {
        changed = false;
        label_index.clear();
        for (size_t i = 0; i < f.blocks.size(); ++i) {
            label_index.emplace(f.blocks[i].label, i);
        }
        for (size_t i = 1; i < f.blocks.size();) {
            IRBlock& block = f.blocks[i];
            bool threadable = block.instructions.size() == 1 &&
                              block.instructions[0].op == IROpcode::JMP;
            std::string target;
            if (threadable) {
                target = block.instructions[0].label_true;
            } else {
                bool all_nop = true;
                for (const auto& insn : block.instructions) {
                    if (insn.op != IROpcode::NOP) { all_nop = false; break; }
                }
                threadable = all_nop && i + 1 < f.blocks.size();
                if (threadable) target = f.blocks[i + 1].label;  // fallthrough
            }
            if (!threadable) { ++i; continue; }
            auto dest = label_index.find(target);
            if (target.empty() || target == block.label || dest == label_index.end() ||
                dest->second == i) {
                ++i;
                continue;
            }

            for (auto& other : f.blocks) {
                for (auto& insn : other.instructions) {
                    if (insn.op == IROpcode::JMP && insn.label_true == block.label) {
                        insn.label_true = target;
                        ++stats.threaded_jumps;
                    } else if (insn.op == IROpcode::BR) {
                        if (insn.label_true == block.label) {
                            insn.label_true = target;
                            ++stats.threaded_jumps;
                        }
                        if (insn.label_false == block.label) {
                            insn.label_false = target;
                            ++stats.threaded_jumps;
                        }
                    }
                }
            }
            const std::string removed_label = block.label;
            const bool jmp_only = block.instructions.size() == 1 &&
                                  block.instructions[0].op == IROpcode::JMP;
            // A jmp-only block can also be entered by falling through from the
            // previous block. Removing it would then fall into whatever block
            // follows it, so make that edge explicit before erasing it. An
            // empty block's fallthrough successor is its target already, so it
            // needs no extra jump.
            if (jmp_only) {
                auto& prev = f.blocks[i - 1];
                bool prev_terminated = false;
                for (const auto& insn : prev.instructions) {
                    if (is_terminator(insn.op)) { prev_terminated = true; break; }
                }
                if (!prev_terminated) prev.emit(IRInstruction::jmp(target));
            }
            f.blocks.erase(f.blocks.begin() + static_cast<ptrdiff_t>(i));
            label_index.erase(removed_label);
            ++stats.removed_blocks;
            changed = true;
            // Do not advance `i`: the next block has shifted into this slot.
        }
    }
    return stats;
}
