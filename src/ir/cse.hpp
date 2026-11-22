#pragma once

#include "function.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

/// What local value numbering removed.
struct CSEStats {
    size_t expressions = 0;     ///< repeated pure expressions turned into copies
    size_t loads_forwarded = 0; ///< loads replaced by the value of a prior store
    size_t stores_removed = 0;  ///< duplicate stores dropped
};

namespace ir_cse_detail {

inline bool is_store(IROpcode op) {
    switch (op) {
        case IROpcode::STORE:
        case IROpcode::STORE_8:
        case IROpcode::STORE_16:
        case IROpcode::STORE_32:
        case IROpcode::STORE_PARAM:
            return true;
        default:
            return false;
    }
}

inline bool is_cse_op(IROpcode op) {
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

inline std::string key_for(const IRInstruction& insn) {
    std::string key = ir_opcode_name(insn.op);
    key += "|" + std::to_string(insn.imm_arg);
    key += "|" + std::to_string(static_cast<int>(insn.dst_width));
    key += "|" + insn.label_true + "|" + insn.label_false + "|" + insn.call_target;
    for (const auto& op : insn.operands) key += "|" + op.to_string();
    return key;
}

inline std::vector<uint32_t> operand_vregs(const IRInstruction& insn) {
    std::vector<uint32_t> out;
    for (const auto& op : insn.operands) {
        if (op.is_vreg()) out.push_back(op.vreg_id);
    }
    return out;
}

inline bool mentions(const std::vector<uint32_t>& regs, uint32_t vreg) {
    for (uint32_t r : regs) if (r == vreg) return true;
    return false;
}

} // namespace ir_cse_detail

/// Local CSE and store-to-load forwarding within each basic block. Only one
/// block is considered at a time, and all remembered facts are discarded at a
/// call, so nothing crosses a control-flow or unknown-memory boundary.
inline CSEStats cse_function(IRFunction& f) {
    using namespace ir_cse_detail;
    CSEStats stats;

    struct Expr {
        uint32_t dst = IRInstruction::NONE_VREG;
        std::vector<uint32_t> operands;
    };
    using Address = std::pair<uint32_t, int64_t>;  // base vreg, byte offset

    for (auto& block : f.blocks) {
        std::map<std::string, Expr> available;
        // Indexes keep invalidation proportional to the number of affected
        // expressions instead of scanning the whole table on every definition.
        std::map<uint32_t, std::vector<std::string>> uses;
        std::map<uint32_t, std::string> result_key;
        std::map<Address, IRValue> pending_store;
        std::map<uint32_t, std::vector<Address>> pending_by_base;

        auto forget_key = [&](const std::string& key) {
            auto found = available.find(key);
            if (found == available.end()) return;
            auto result = result_key.find(found->second.dst);
            if (result != result_key.end() && result->second == key) result_key.erase(result);
            available.erase(found);
        };

        for (auto& insn : block.instructions) {
            const uint32_t defined = insn.dst;
            if (defined != IRInstruction::NONE_VREG) {
                // A redefinition changes the meaning of every expression that
                // used the old value, and of any remembered store address.
                auto result = result_key.find(defined);
                if (result != result_key.end()) {
                    const std::string key = result->second;
                    forget_key(key);  // also removes this result_key entry
                }
                auto used = uses.find(defined);
                if (used != uses.end()) {
                    for (const auto& key : used->second) forget_key(key);
                    uses.erase(used);
                }
                auto by_base = pending_by_base.find(defined);
                if (by_base != pending_by_base.end()) {
                    for (const auto& address : by_base->second) pending_store.erase(address);
                    pending_by_base.erase(by_base);
                }
            }

            if (is_store(insn.op)) {
                bool tracked = false;
                if (insn.op == IROpcode::STORE && insn.operands.size() == 2 &&
                    insn.operands[0].is_vreg()) {
                    Address address{insn.operands[0].vreg_id, insn.imm_arg};
                    auto found = pending_store.find(address);
                    if (found != pending_store.end() && found->second == insn.operands[1]) {
                        insn = IRInstruction::nop();  // same value already stored
                        ++stats.stores_removed;
                    } else {
                        pending_store[address] = insn.operands[1];
                        pending_by_base[address.first].push_back(address);
                    }
                    tracked = true;
                }
                // Any other store may alias an unknown address: forget all
                // forwarded values.
                if (!tracked) {
                    pending_store.clear();
                    pending_by_base.clear();
                }
                continue;
            }

            // DIV/MOD clobber rax/rdx implicitly. Keeping an expression alive
            // across one would extend a live range through those clobbers,
            // which the allocator does not model as a call.
            if (is_call(insn.op) || insn.op == IROpcode::SYSCALL ||
                insn.op == IROpcode::DIV || insn.op == IROpcode::MOD ||
                insn.op == IROpcode::ASM) {
                available.clear();
                uses.clear();
                result_key.clear();
                pending_store.clear();
                pending_by_base.clear();
                continue;
            }

            if (insn.op == IROpcode::LOAD && insn.operands.size() == 1 &&
                insn.operands[0].is_vreg()) {
                Address address{insn.operands[0].vreg_id, insn.imm_arg};
                auto found = pending_store.find(address);
                if (found != pending_store.end()) {
                    const IRValue value = found->second;
                    const IRWidth width = insn.dst_width;
                    if (value.is_vreg()) {
                        insn = IRInstruction::copy(insn.dst, value);
                        insn.dst_width = width;
                    } else {
                        insn = IRInstruction::load_i64(insn.dst, value.imm);
                    }
                    ++stats.loads_forwarded;
                }
                continue;  // loads are not CSE'd; a store may have changed them
            }

            if (is_cse_op(insn.op) && insn.dst != IRInstruction::NONE_VREG) {
                const std::string key = key_for(insn);
                auto found = available.find(key);
                if (found != available.end()) {
                    const uint32_t earlier = found->second.dst;
                    const IRWidth width = insn.dst_width;
                    insn = IRInstruction::copy(insn.dst, IRValue::vreg(earlier));
                    insn.dst_width = width;
                    ++stats.expressions;
                } else {
                    const std::vector<uint32_t> operands = operand_vregs(insn);
                    available.emplace(key, Expr{insn.dst, operands});
                    result_key[insn.dst] = key;
                    for (uint32_t vreg : operands) uses[vreg].push_back(key);
                }
            }
        }
    }
    return stats;
}
