#pragma once

#include "function.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

/// What mem2reg changed.
struct Mem2RegStats {
    size_t promoted_slots = 0;
    size_t loads_replaced = 0;
    size_t stores_replaced = 0;
    size_t frame_addrs_removed = 0;
};

namespace ir_mem2reg_detail {

inline bool is_load(IROpcode op) {
    switch (op) {
        case IROpcode::LOAD:
        case IROpcode::LOAD_S8:
        case IROpcode::LOAD_U8:
        case IROpcode::LOAD_S16:
        case IROpcode::LOAD_U16:
        case IROpcode::LOAD_S32:
        case IROpcode::LOAD_U32:
            return true;
        default:
            return false;
    }
}

inline bool is_store(IROpcode op) {
    switch (op) {
        case IROpcode::STORE:
        case IROpcode::STORE_8:
        case IROpcode::STORE_16:
        case IROpcode::STORE_32:
            return true;
        default:
            return false;
    }
}

inline IRInstruction store_to_copy(uint32_t vreg, const IRInstruction& insn) {
    const IRValue value = insn.operands[1];
    switch (insn.op) {
        case IROpcode::STORE_8:
            return IRInstruction::binary(IROpcode::AND, vreg, value, IRValue::imm_i64(0xFF));
        case IROpcode::STORE_16:
            return IRInstruction::binary(IROpcode::AND, vreg, value, IRValue::imm_i64(0xFFFF));
        case IROpcode::STORE_32:
            return IRInstruction::zext(vreg, value, IRWidth::I32);
        default:
            return IRInstruction::copy(vreg, value);
    }
}

inline IRInstruction load_from_copy(uint32_t dst, const IRInstruction& insn, uint32_t vreg) {
    switch (insn.op) {
        case IROpcode::LOAD_S8:  return IRInstruction::sext(dst, IRValue::vreg(vreg), IRWidth::I8);
        case IROpcode::LOAD_U8:  return IRInstruction::zext(dst, IRValue::vreg(vreg), IRWidth::I8);
        case IROpcode::LOAD_S16: return IRInstruction::sext(dst, IRValue::vreg(vreg), IRWidth::I16);
        case IROpcode::LOAD_U16: return IRInstruction::zext(dst, IRValue::vreg(vreg), IRWidth::I16);
        case IROpcode::LOAD_S32: return IRInstruction::sext(dst, IRValue::vreg(vreg), IRWidth::I32);
        case IROpcode::LOAD_U32: return IRInstruction::zext(dst, IRValue::vreg(vreg), IRWidth::I32);
        default:                 return IRInstruction::copy(dst, IRValue::vreg(vreg));
    }
}

} // namespace ir_mem2reg_detail

/// Promote simple frame slots into persistent virtual registers (D-027). A
/// slot qualifies when its address never escapes and every load is dominated
/// by a store to that slot; loads/stores then become copies/extends/ands to a
/// shared vreg, with no new phi or block-argument form in the IR.
inline Mem2RegStats promote_locals(IRFunction& f) {
    using namespace ir_mem2reg_detail;
    Mem2RegStats stats;
    if (f.blocks.empty()) return stats;

    std::map<uint32_t, int64_t> frame_vregs;
    std::map<int64_t, std::vector<uint32_t>> slots;
    for (const auto& block : f.blocks) {
        for (const auto& insn : block.instructions) {
            if (insn.op == IROpcode::FRAME_ADDR && insn.dst != IRInstruction::NONE_VREG) {
                frame_vregs[insn.dst] = insn.imm_arg;
                slots[insn.imm_arg].push_back(insn.dst);
            }
        }
    }
    if (slots.empty()) return stats;

    // CFG and dominator sets (the same construction the verifier uses).
    const size_t n = f.blocks.size();
    std::map<std::string, size_t> label_index;
    for (size_t i = 0; i < n; ++i) label_index.emplace(f.blocks[i].label, i);
    std::vector<std::vector<size_t>> succ(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& insns = f.blocks[i].instructions;
        size_t term = insns.size();
        for (size_t k = 0; k < insns.size(); ++k) {
            const IROpcode op = insns[k].op;
            if (op == IROpcode::JMP || op == IROpcode::BR || op == IROpcode::RET ||
                op == IROpcode::RET2 || op == IROpcode::RET_VOID) { term = k; break; }
        }
        if (term == insns.size()) {
            if (i + 1 < n) succ[i].push_back(i + 1);
        } else {
            auto add = [&](const std::string& label) {
                auto it = label_index.find(label);
                if (it != label_index.end()) succ[i].push_back(it->second);
            };
            if (insns[term].op == IROpcode::JMP) add(insns[term].label_true);
            else if (insns[term].op == IROpcode::BR) {
                add(insns[term].label_true);
                add(insns[term].label_false);
            }
        }
    }
    std::vector<bool> reachable(n, false);
    std::vector<size_t> work{0};
    reachable[0] = true;
    while (!work.empty()) {
        size_t b = work.back();
        work.pop_back();
        for (size_t next : succ[b]) {
            if (!reachable[next]) { reachable[next] = true; work.push_back(next); }
        }
    }
    std::vector<std::set<size_t>> dom(n);
    for (size_t b = 0; b < n; ++b) {
        for (size_t i = 0; i < n; ++i) dom[b].insert(i);
    }
    dom[0].clear();
    dom[0].insert(0);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t b = 1; b < n; ++b) {
            std::set<size_t> next;
            bool have_pred = false;
            for (size_t i = 0; i < n; ++i) {
                bool is_pred = false;
                for (size_t s : succ[i]) if (s == b) { is_pred = true; break; }
                if (!is_pred || !reachable[i]) continue;
                if (!have_pred) { next = dom[i]; have_pred = true; }
                else {
                    std::set<size_t> common;
                    std::set_intersection(next.begin(), next.end(), dom[i].begin(), dom[i].end(),
                                          std::inserter(common, common.end()));
                    next = std::move(common);
                }
            }
            if (!have_pred) { next.clear(); next.insert(b); }
            else next.insert(b);
            if (next != dom[b]) { dom[b] = std::move(next); changed = true; }
        }
    }

    struct Access {
        size_t block;
        size_t index;
        bool store;
    };

    for (const auto& [slot, addrs] : slots) {
        const std::set<uint32_t> address_regs(addrs.begin(), addrs.end());
        bool escaped = false;
        std::vector<Access> accesses;
        for (size_t bi = 0; bi < n; ++bi) {
            const auto& insns = f.blocks[bi].instructions;
            for (size_t ii = 0; ii < insns.size(); ++ii) {
                const auto& insn = insns[ii];
                for (size_t pos = 0; pos < insn.operands.size(); ++pos) {
                    const auto& op = insn.operands[pos];
                    if (!op.is_vreg()) continue;
                    auto frame = frame_vregs.find(op.vreg_id);
                    if (frame == frame_vregs.end() || frame->second != slot) continue;
                    const bool plain_access = pos == 0 && insn.imm_arg == 0 &&
                                              (is_load(insn.op) || is_store(insn.op));
                    if (!plain_access) escaped = true;
                }
                const bool access = is_load(insn.op) || is_store(insn.op);
                const size_t needed = is_store(insn.op) ? 2 : 1;
                if (access && insn.operands.size() >= needed && insn.operands[0].is_vreg() &&
                    address_regs.count(insn.operands[0].vreg_id) != 0 && insn.imm_arg == 0) {
                    accesses.push_back({bi, ii, is_store(insn.op)});
                }
            }
        }
        if (escaped || accesses.empty()) continue;

        bool all_dominated = true;
        for (const auto& load : accesses) {
            if (load.store) continue;
            bool dominated = false;
            for (const auto& store : accesses) {
                if (!store.store || !reachable[store.block]) continue;
                if (store.block == load.block) {
                    if (store.index < load.index) { dominated = true; break; }
                } else if (dom[load.block].count(store.block) != 0) {
                    dominated = true;
                    break;
                }
            }
            if (!dominated) { all_dominated = false; break; }
        }
        if (!all_dominated) continue;

        const uint32_t vreg = f.new_vreg();
        for (const auto& access : accesses) {
            auto& insn = f.blocks[access.block].instructions[access.index];
            if (access.store) {
                insn = store_to_copy(vreg, insn);
                ++stats.stores_replaced;
            } else {
                insn = load_from_copy(insn.dst, insn, vreg);
                ++stats.loads_replaced;
            }
        }
        for (auto& block : f.blocks) {
            for (auto& insn : block.instructions) {
                if (insn.op != IROpcode::FRAME_ADDR || insn.dst == IRInstruction::NONE_VREG) continue;
                if (address_regs.count(insn.dst) == 0) continue;
                insn = IRInstruction::nop();
                ++stats.frame_addrs_removed;
            }
        }
        ++stats.promoted_slots;
    }
    return stats;
}
