#pragma once

#include "function.hpp"
#include "module.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

/// One problem found by the IR verifier. `index` is the instruction index
/// inside `block` (0 for function/block-level errors).
struct IRVerifyError {
    std::string function;
    std::string block;
    size_t index = 0;
    std::string message;
};

struct IRVerifyResult {
    std::vector<IRVerifyError> errors;

    bool ok() const { return errors.empty(); }

    std::string to_string() const {
        std::string out;
        for (const auto& e : errors) {
            out += e.function + ":" + e.block + ":" + std::to_string(e.index)
                   + ": " + e.message + "\n";
        }
        return out;
    }
};

namespace ir_verify_detail {

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

/// Opcodes whose `dst` receives a value. CALL/CALL_INDIRECT may or may not
/// produce one, so they are handled specially below.
inline bool produces_result(IROpcode op) {
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
        case IROpcode::LOAD:
        case IROpcode::LOAD_S8:
        case IROpcode::LOAD_U8:
        case IROpcode::LOAD_S16:
        case IROpcode::LOAD_U16:
        case IROpcode::LOAD_S32:
        case IROpcode::LOAD_U32:
        case IROpcode::LEA:
        case IROpcode::LEA_LABEL:
        case IROpcode::LOAD_PARAM:
        case IROpcode::CALL:
        case IROpcode::CALL_INDIRECT:
        case IROpcode::FCONST:
        case IROpcode::FCOPY:
        case IROpcode::FNEG:
        case IROpcode::FADD:
        case IROpcode::FSUB:
        case IROpcode::FMUL:
        case IROpcode::FDIV:
        case IROpcode::FCMP_EQ:
        case IROpcode::FCMP_NE:
        case IROpcode::FCMP_LT:
        case IROpcode::FCMP_LE:
        case IROpcode::FCMP_GT:
        case IROpcode::FCMP_GE:
        case IROpcode::I2F:
        case IROpcode::F2I:
        case IROpcode::F2F:
        case IROpcode::FLOAD:
            return true;
        default:
            return false;
    }
}

/// Register class an FP-family opcode requires for its destination and operands.
/// `dst`: 0 = not constrained here, 1 = integer, 2 = float. Operands likewise by position.
struct FpShape { int dst; int op0; int op1; };
inline bool fp_shape(IROpcode op, FpShape& out) {
    switch (op) {
        case IROpcode::FCONST: out = {2, 0, 0}; return true;
        case IROpcode::FCOPY:
        case IROpcode::FNEG:
        case IROpcode::F2F:    out = {2, 2, 0}; return true;
        case IROpcode::FADD:
        case IROpcode::FSUB:
        case IROpcode::FMUL:
        case IROpcode::FDIV:   out = {2, 2, 2}; return true;
        case IROpcode::FCMP_EQ:
        case IROpcode::FCMP_NE:
        case IROpcode::FCMP_LT:
        case IROpcode::FCMP_LE:
        case IROpcode::FCMP_GT:
        case IROpcode::FCMP_GE: out = {1, 2, 2}; return true;
        case IROpcode::I2F:    out = {2, 1, 0}; return true;
        case IROpcode::F2I:    out = {1, 2, 0}; return true;
        case IROpcode::FLOAD:  out = {2, 1, 0}; return true;
        case IROpcode::FSTORE: out = {0, 1, 2}; return true;
        default: return false;
    }
}

/// Fixed operand count for an opcode, or SIZE_MAX for the variable-length calls.
/// BR carries its condition as one operand; the two targets are separate fields.
inline size_t operand_count_for(IROpcode op) {
    switch (op) {
        case IROpcode::NOP:
        case IROpcode::ASM:
        case IROpcode::LOAD_I64:
        case IROpcode::LOAD_I32:
        case IROpcode::LOAD_I8:
        case IROpcode::LOAD_U64:
        case IROpcode::LOAD_U32_IMM:
        case IROpcode::LOAD_U8_IMM:
        case IROpcode::FRAME_ADDR:
        case IROpcode::LEA_LABEL:
        case IROpcode::JMP:
        case IROpcode::LOAD_PARAM:
        case IROpcode::SYSCALL:
        case IROpcode::PUSH_FRAME:
        case IROpcode::RET_VOID:
        case IROpcode::FCONST:
            return 0;
        case IROpcode::FCOPY:
        case IROpcode::FNEG:
        case IROpcode::I2F:
        case IROpcode::F2I:
        case IROpcode::F2F:
        case IROpcode::FLOAD:
        case IROpcode::COPY:
        case IROpcode::NEG:
        case IROpcode::NOT_:
        case IROpcode::ZEXT:
        case IROpcode::SEXT:
        case IROpcode::TRUNC:
        case IROpcode::LOAD:
        case IROpcode::LOAD_S8:
        case IROpcode::LOAD_U8:
        case IROpcode::LOAD_S16:
        case IROpcode::LOAD_U16:
        case IROpcode::LOAD_S32:
        case IROpcode::LOAD_U32:
        case IROpcode::LEA:
        case IROpcode::PUSH:
        case IROpcode::POP:
        case IROpcode::RET:
        case IROpcode::BR:
            return 1;
        case IROpcode::STORE:
        case IROpcode::STORE_8:
        case IROpcode::STORE_16:
        case IROpcode::STORE_32:
        case IROpcode::STORE_PARAM:
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
        case IROpcode::RET2:
        case IROpcode::FSTORE:
        case IROpcode::FADD:
        case IROpcode::FSUB:
        case IROpcode::FMUL:
        case IROpcode::FDIV:
        case IROpcode::FCMP_EQ:
        case IROpcode::FCMP_NE:
        case IROpcode::FCMP_LT:
        case IROpcode::FCMP_LE:
        case IROpcode::FCMP_GT:
        case IROpcode::FCMP_GE:
            return 2;
        case IROpcode::CALL:
        case IROpcode::CALL_INDIRECT:
            return SIZE_MAX;
    }
    return SIZE_MAX;
}

inline bool requires_vreg(IROpcode op, size_t pos, size_t count) {
    switch (op) {
        case IROpcode::COPY:
        case IROpcode::NEG:
        case IROpcode::NOT_:
        case IROpcode::ZEXT:
        case IROpcode::SEXT:
        case IROpcode::TRUNC:
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
        case IROpcode::LEA:
        case IROpcode::PUSH:
        case IROpcode::POP:
        case IROpcode::FCOPY:
        case IROpcode::FNEG:
        case IROpcode::F2F:
        case IROpcode::F2I:
        case IROpcode::I2F:
        case IROpcode::FLOAD:
        case IROpcode::FSTORE:
            return pos == 0;  // base address / source
        case IROpcode::FADD:
        case IROpcode::FSUB:
        case IROpcode::FMUL:
        case IROpcode::FDIV:
        case IROpcode::FCMP_EQ:
        case IROpcode::FCMP_NE:
        case IROpcode::FCMP_LT:
        case IROpcode::FCMP_LE:
        case IROpcode::FCMP_GT:
        case IROpcode::FCMP_GE:
            return pos <= 1;
        case IROpcode::CALL_INDIRECT:
            return pos + 1 == count;  // target is the last operand (D-014)
        default:
            return false;
    }
}

inline bool requires_immediate(IROpcode op, size_t pos) {
    return op == IROpcode::STORE_PARAM && (pos == 0 || pos == 1);
}

inline bool valid_width(IRWidth w) {
    switch (w) {
        case IRWidth::I8:
        case IRWidth::I16:
        case IRWidth::I32:
        case IRWidth::I64:
        case IRWidth::F32:
        case IRWidth::F64:
            return true;
    }
    return false;
}

} // namespace ir_verify_detail

/// Verify one IR function: labels, terminators, operand shapes/types, and that
/// every use of a virtual register is dominated by a definition in the CFG.
inline IRVerifyResult verify_function(const IRFunction& f) {
    using namespace ir_verify_detail;

    IRVerifyResult result;
    auto report = [&](const std::string& block, size_t index, const std::string& msg) {
        result.errors.push_back({f.name, block, index, msg});
    };

    if (f.blocks.empty()) {
        report("", 0, "function has no blocks");
        return result;
    }

    // Block labels must be unique; a map from label to index also resolves
    // branch targets for the CFG built below.
    std::map<std::string, size_t> label_to_block;
    for (size_t bi = 0; bi < f.blocks.size(); ++bi) {
        const std::string& label = f.blocks[bi].label;
        if (!label_to_block.emplace(label, bi).second) {
            report(label, 0, "duplicate block label '" + label + "'");
        }
    }

    struct Use {
        size_t block;
        size_t index;
        uint32_t vreg;
    };
    struct Def {
        size_t block;
        size_t index;
    };
    std::vector<Use> uses;
    // Indexed by vreg so a use only considers defs of the same value.
    std::map<uint32_t, std::vector<Def>> defs;
    std::map<uint32_t, IRWidth> vreg_widths;
    std::vector<std::vector<size_t>> succ(f.blocks.size());
    std::vector<std::vector<size_t>> preds(f.blocks.size());

    auto add_edge = [&](size_t from, size_t to) {
        if (to >= f.blocks.size()) return;
        succ[from].push_back(to);
        preds[to].push_back(from);
    };

    for (size_t bi = 0; bi < f.blocks.size(); ++bi) {
        const IRBlock& block = f.blocks[bi];
        for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
            const IRInstruction& insn = block.instructions[ii];
            const std::string opname(ir_opcode_name(insn.op));
            auto at = [&](const std::string& msg) { report(block.label, ii, msg); };

            size_t expected = operand_count_for(insn.op);
            if (expected == SIZE_MAX) expected = insn.operands.size();
            if (insn.operands.size() != expected) {
                at("operand count for " + opname + ": expected " + std::to_string(expected)
                   + ", got " + std::to_string(insn.operands.size()));
            }

            const bool result_expected = produces_result(insn.op);
            const bool optional_result = insn.op == IROpcode::CALL ||
                                         insn.op == IROpcode::CALL_INDIRECT;
            if (result_expected && !optional_result && insn.dst == IRInstruction::NONE_VREG) {
                at("instruction is missing a destination register");
            }
            if (!result_expected && insn.dst != IRInstruction::NONE_VREG) {
                if (is_terminator(insn.op)) at("terminator must not define a result");
                else at("instruction must not define a result");
            }
            if (insn.dst2 != IRInstruction::NONE_VREG && insn.op != IROpcode::CALL &&
                insn.op != IROpcode::CALL_INDIRECT) {
                at("unexpected second destination register");
            }

            if (is_terminator(insn.op)) {
                switch (insn.op) {
                    case IROpcode::JMP:
                        if (insn.label_true.empty() || !insn.label_false.empty()) {
                            at("jmp needs exactly one target label");
                        }
                        break;
                    case IROpcode::BR:
                        if (insn.label_true.empty() || insn.label_false.empty()) {
                            at("br needs two target labels");
                        }
                        break;
                    case IROpcode::RET:
                        if (insn.operands.size() > 1) at("ret takes at most one operand");
                        break;
                    case IROpcode::RET2:
                        if (insn.operands.size() != 2) at("ret2 needs two operands");
                        break;
                    case IROpcode::RET_VOID:
                        if (!insn.operands.empty()) at("ret_void takes no operands");
                        break;
                    default:
                        break;
                }
            }

            for (size_t pos = 0; pos < insn.operands.size(); ++pos) {
                const IRValue& op = insn.operands[pos];
                const std::string posname = "operand " + std::to_string(pos);
                const std::string kind = op.is_vreg() ? "virtual register" : "immediate";
                if (requires_vreg(insn.op, pos, insn.operands.size()) && !op.is_vreg()) {
                    at(posname + " expected a virtual register, got " + kind);
                }
                if (requires_immediate(insn.op, pos) && !op.is_imm()) {
                    at(posname + " expected an immediate, got " + kind);
                }
                if (!valid_width(op.width)) at(posname + " has an invalid width");
                if (op.is_vreg()) {
                    auto [it, inserted] = vreg_widths.emplace(op.vreg_id, op.width);
                    if (!inserted && it->second != op.width) {
                        at("vreg %" + std::to_string(op.vreg_id) + " has inconsistent width");
                    }
                    uses.push_back({bi, ii, op.vreg_id});
                }
            }

            // Register classes (D-038): FP-family opcodes fix them; every other opcode is
            // integer-only except calls and returns, which carry FP values by operand width.
            {
                FpShape shape{0, 0, 0};
                const bool fp_op = fp_shape(insn.op, shape);
                const bool flexible = insn.op == IROpcode::CALL || insn.op == IROpcode::CALL_INDIRECT ||
                                      insn.op == IROpcode::RET || insn.op == IROpcode::RET2;
                auto class_of = [&](uint32_t vreg) { return f.is_fp(vreg) ? 2 : 1; };
                auto check = [&](int want, uint32_t vreg, const std::string& what) {
                    if (want != 0 && class_of(vreg) != want) {
                        at(what + " must be " + std::string(want == 2 ? "a float" : "an integer")
                           + " vreg (%" + std::to_string(vreg) + ")");
                    }
                };
                if (insn.dst != IRInstruction::NONE_VREG) {
                    if (fp_op) check(shape.dst, insn.dst, "result");
                    else if (insn.op == IROpcode::CALL || insn.op == IROpcode::CALL_INDIRECT)
                        check(is_fp_width(insn.dst_width) ? 2 : 1, insn.dst, "call result");
                    else check(1, insn.dst, "result");
                }
                if (insn.dst2 != IRInstruction::NONE_VREG) {
                    check(is_fp_width(insn.dst2_width) ? 2 : 1, insn.dst2, "second call result");
                }
                for (size_t pos = 0; pos < insn.operands.size(); ++pos) {
                    const IRValue& op = insn.operands[pos];
                    if (!op.is_vreg()) continue;
                    if (fp_op) {
                        check(pos == 0 ? shape.op0 : shape.op1, op.vreg_id, "operand " + std::to_string(pos));
                    } else if (!flexible) {
                        check(1, op.vreg_id, "operand " + std::to_string(pos));
                    }
                    if (is_fp_width(op.width) != f.is_fp(op.vreg_id)) {
                        at("operand " + std::to_string(pos) + " width does not match its register class");
                    }
                }
            }

            if ((insn.op == IROpcode::ZEXT || insn.op == IROpcode::SEXT) &&
                !valid_width(static_cast<IRWidth>(insn.imm_arg))) {
                at("invalid source width for extend");
            }
            if (insn.op == IROpcode::TRUNC && !valid_width(insn.dst_width)) {
                at("invalid destination width for trunc");
            }

            if (insn.dst != IRInstruction::NONE_VREG) {
                defs[insn.dst].push_back({bi, ii});
            }
            if (insn.dst2 != IRInstruction::NONE_VREG) {
                defs[insn.dst2].push_back({bi, ii});
            }
        }

        // The first terminator is the only live one: the generator sometimes
        // emits dead instructions after a ret/jmp, and those are not successors.
        size_t term = block.instructions.size();
        for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
            if (is_terminator(block.instructions[ii].op)) { term = ii; break; }
        }
        auto check_target = [&](const std::string& label, size_t ii, const char* what) {
            if (label.empty()) {
                report(block.label, ii, std::string(what) + " has an empty target label");
                return;
            }
            auto it = label_to_block.find(label);
            if (it == label_to_block.end()) {
                report(block.label, ii, std::string(what) + " targets unknown block label '" + label + "'");
                return;
            }
            add_edge(bi, it->second);
        };
        if (term == block.instructions.size()) {
            if (bi + 1 < f.blocks.size()) add_edge(bi, bi + 1);  // fallthrough
        } else {
            const IRInstruction& insn = block.instructions[term];
            if (insn.op == IROpcode::JMP) {
                check_target(insn.label_true, term, "jmp");
            } else if (insn.op == IROpcode::BR) {
                check_target(insn.label_true, term, "br true edge");
                check_target(insn.label_false, term, "br false edge");
            }
        }
    }

    // Reachable blocks from the entry (block 0).
    std::vector<bool> reachable(f.blocks.size(), false);
    std::vector<size_t> work{0};
    reachable[0] = true;
    while (!work.empty()) {
        size_t b = work.back();
        work.pop_back();
        for (size_t next : succ[b]) {
            if (!reachable[next]) { reachable[next] = true; work.push_back(next); }
        }
    }

    // Dominator sets, by iterative dataflow. Unreachable blocks are left with
    // the trivial set {b} and their uses are not checked.
    std::vector<std::set<size_t>> dom(f.blocks.size());
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        for (size_t i = 0; i < f.blocks.size(); ++i) dom[b].insert(i);
    }
    dom[0].clear();
    dom[0].insert(0);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t b = 1; b < f.blocks.size(); ++b) {
            std::set<size_t> next;
            bool have_pred = false;
            for (size_t p : preds[b]) {
                if (!reachable[p]) continue;
                if (!have_pred) {
                    next = dom[p];
                    have_pred = true;
                } else {
                    std::set<size_t> intersected;
                    std::set_intersection(next.begin(), next.end(), dom[p].begin(), dom[p].end(),
                                          std::inserter(intersected, intersected.end()));
                    next = std::move(intersected);
                }
            }
            if (!have_pred) {
                next.clear();
                next.insert(b);
            } else {
                next.insert(b);
            }
            if (next != dom[b]) { dom[b] = std::move(next); changed = true; }
        }
    }

    for (const Use& use : uses) {
        if (!reachable[use.block]) continue;
        bool dominated = false;
        auto found = defs.find(use.vreg);
        if (found != defs.end()) {
            for (const Def& def : found->second) {
                if (def.block == use.block) {
                    if (def.index < use.index) { dominated = true; break; }
                } else if (reachable[def.block] && dom[use.block].count(def.block) != 0) {
                    dominated = true;
                    break;
                }
            }
        }
        if (!dominated) {
            report(f.blocks[use.block].label, use.index,
                   "use of vreg %" + std::to_string(use.vreg)
                   + " is not dominated by a definition");
        }
    }

    return result;
}

/// Verify every function in a module.
inline IRVerifyResult verify_module(const IRModule& m) {
    IRVerifyResult result;
    for (const auto& func : m.functions) {
        IRVerifyResult one = verify_function(*func);
        result.errors.insert(result.errors.end(), one.errors.begin(), one.errors.end());
    }
    return result;
}
