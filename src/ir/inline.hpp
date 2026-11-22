#pragma once

#include "function.hpp"
#include "instruction.hpp"
#include "value.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ir_inline {

/// A function awaiting inlining; `frame_bytes` is grown when calls are inlined.
struct InlineItem {
    IRFunction* function = nullptr;
    uint32_t frame_bytes = 0;
};

struct InlineStats {
    size_t candidates = 0;
    size_t inlined = 0;
};

constexpr size_t kMaxInlineInstructions = 24;

/// Splice eligible leaf callees into their callers (D-031).
///
/// A callee qualifies when it has one basic block ending in a single RET/RET_VOID,
/// contains no calls, syscalls, branches or second return value, has at most
/// kMaxInlineInstructions instructions, and is never referenced by LEA_LABEL.
/// The callee's frame slots are shifted below the caller's frame and parameter
/// STORE_PARAMs become plain stores of the call's argument vregs. No new IR form
/// is introduced, so the verifier and backends need no changes.
inline InlineStats inline_leaves(std::vector<InlineItem>& items) {
    InlineStats stats;

    std::map<std::string, InlineItem*> by_name;
    for (auto& item : items) {
        if (item.function) by_name.emplace(item.function->name, &item);
    }

    std::set<std::string> address_taken;
    for (const auto& item : items) {
        if (!item.function) continue;
        for (const auto& block : item.function->blocks) {
            for (const auto& insn : block.instructions) {
                // lea_label stores the symbol in call_target (see
                // IRInstruction::lea_label); label_true is kept for safety.
                if (insn.op == IROpcode::LEA_LABEL) {
                    if (!insn.call_target.empty()) address_taken.insert(insn.call_target);
                    if (!insn.label_true.empty()) address_taken.insert(insn.label_true);
                }
            }
        }
    }

    auto eligible = [&](const IRFunction* f) {
        if (!f || address_taken.count(f->name) != 0) return false;
        if (f->blocks.empty() || f->blocks.size() > 2) return false;
        // gen_func_def always appends a safety-net epilogue block containing
        // only ret_void; it is not a real branch in the callee body.
        for (size_t bi = 1; bi < f->blocks.size(); ++bi) {
            const auto& extra = f->blocks[bi].instructions;
            if (extra.size() != 1 || extra[0].op != IROpcode::RET_VOID) return false;
        }
        const auto& insns = f->blocks[0].instructions;
        if (insns.empty() || insns.size() > kMaxInlineInstructions) return false;
        size_t returns = 0;
        for (size_t i = 0; i < insns.size(); ++i) {
            const auto& insn = insns[i];
            switch (insn.op) {
                case IROpcode::CALL:
                case IROpcode::CALL_INDIRECT:
                case IROpcode::SYSCALL:
                case IROpcode::JMP:
                case IROpcode::BR:
                case IROpcode::RET2:
                    return false;
                case IROpcode::RET:
                case IROpcode::RET_VOID:
                    ++returns;
                    if (i + 1 != insns.size()) return false;
                    break;
                case IROpcode::STORE_PARAM:
                    // Only plain, full-width scalar parameters can be mapped
                    // to call operands; packed struct words cannot.
                    if (insn.operands.size() < 2 || !insn.operands[1].is_imm() ||
                        insn.operands[1].imm != 0 || insn.dst_width != IRWidth::I64) return false;
                    break;
                case IROpcode::LEA:
                case IROpcode::LOAD_PARAM:
                    // Aggregates and static locals use computed addresses;
                    // shifting only FRAME_ADDR bases is not sufficient for them.
                    return false;
                case IROpcode::LOAD:
                case IROpcode::LOAD_S8:
                case IROpcode::LOAD_U8:
                case IROpcode::LOAD_S16:
                case IROpcode::LOAD_U16:
                case IROpcode::LOAD_S32:
                case IROpcode::LOAD_U32:
                case IROpcode::STORE:
                case IROpcode::STORE_8:
                case IROpcode::STORE_16:
                case IROpcode::STORE_32:
                    if (insn.imm_arg != 0) return false;
                    break;
                default:
                    break;
            }
        }
        if (returns > 1) return false;
        // A body with no terminator falls through to the epilogue block.
        return returns == 1 || (returns == 0 && f->blocks.size() == 2);
    };

    std::set<std::string> eligible_names;
    for (const auto& item : items) {
        if (eligible(item.function)) eligible_names.insert(item.function->name);
    }
    stats.candidates = eligible_names.size();
    if (eligible_names.empty()) return stats;

    for (auto& caller_item : items) {
        if (!caller_item.function) continue;
        IRFunction& caller = *caller_item.function;
        for (auto& block : caller.blocks) {
            std::vector<IRInstruction> out;
            out.reserve(block.instructions.size());
            for (const auto& insn : block.instructions) {
                if (insn.op != IROpcode::CALL ||
                    eligible_names.count(insn.call_target) == 0) {
                    out.push_back(insn);
                    continue;
                }
                const auto found = by_name.find(insn.call_target);
                if (found == by_name.end()) {
                    out.push_back(insn);
                    continue;
                }
                const IRFunction& callee = *found->second->function;
                const auto& cinsns = callee.blocks[0].instructions;

                // Place the callee's frame below everything the caller already uses.
                const uint32_t delta = (caller_item.frame_bytes + 7u) & ~uint32_t{7u};
                int64_t callee_depth = 0;
                for (const auto& c : cinsns) {
                    if (c.op == IROpcode::FRAME_ADDR || c.op == IROpcode::STORE_PARAM) {
                        callee_depth = std::max(callee_depth, -c.imm_arg);
                    }
                }

                std::map<uint32_t, uint32_t> vmap;
                auto map_vreg = [&](uint32_t v) {
                    if (v == IRInstruction::NONE_VREG) return v;
                    const auto [it, inserted] = vmap.emplace(v, caller.new_vreg());
                    (void)inserted;
                    return it->second;
                };

                const bool has_return = !cinsns.empty() &&
                    (cinsns.back().op == IROpcode::RET ||
                     cinsns.back().op == IROpcode::RET_VOID);
                const size_t body_end = has_return ? cinsns.size() - 1 : cinsns.size();

                // Clone everything but the explicit terminator.
                for (size_t ci = 0; ci < body_end; ++ci) {
                    const auto& c = cinsns[ci];
                    if (c.op == IROpcode::STORE_PARAM) {
                        const int64_t ordinal = c.operands[0].imm;
                        if (ordinal < 0 || static_cast<size_t>(ordinal) >= insn.operands.size()) {
                            continue;  // unmappable parameter: drop the dead store
                        }
                        const IRValue arg = insn.operands[static_cast<size_t>(ordinal)];
                        const uint32_t addr = caller.new_vreg();
                        out.push_back(IRInstruction::frame_addr(addr, c.imm_arg - delta));
                        switch (c.dst_width) {
                            case IRWidth::I8:
                                out.push_back(IRInstruction::store_8(IRValue::vreg(addr), 0, arg));
                                break;
                            case IRWidth::I16:
                                out.push_back(IRInstruction::store_16(IRValue::vreg(addr), 0, arg));
                                break;
                            case IRWidth::I32:
                                out.push_back(IRInstruction::store_32(IRValue::vreg(addr), 0, arg));
                                break;
                            default:
                                out.push_back(IRInstruction::store(IRValue::vreg(addr), 0, arg));
                                break;
                        }
                        continue;
                    }
                    IRInstruction clone = c;
                    clone.dst = map_vreg(c.dst);
                    clone.dst2 = map_vreg(c.dst2);
                    for (auto& operand : clone.operands) {
                        if (operand.is_vreg()) {
                            operand = IRValue::vreg(map_vreg(operand.vreg_id));
                        }
                    }
                    if (clone.op == IROpcode::FRAME_ADDR) {
                        clone.imm_arg -= static_cast<int64_t>(delta);
                    }
                    out.push_back(std::move(clone));
                }

                if (has_return && cinsns.back().op == IROpcode::RET &&
                    insn.dst != IRInstruction::NONE_VREG) {
                    IRValue value = cinsns.back().operands[0];
                    if (value.is_vreg()) value = IRValue::vreg(map_vreg(value.vreg_id));
                    out.push_back(IRInstruction::copy(insn.dst, value));
                }

                caller_item.frame_bytes = static_cast<uint32_t>(callee_depth + delta);
                ++stats.inlined;
            }
            block.instructions = std::move(out);
        }
    }
    return stats;
}

} // namespace ir_inline
