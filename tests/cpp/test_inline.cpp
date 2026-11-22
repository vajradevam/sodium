#include "test_framework.hpp"
#include "ir/inline.hpp"
#include "ir/verify.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using ir_inline::InlineItem;
using ir_inline::InlineStats;

IRFunction make_add_leaf() {
    IRFunction leaf("add");
    auto* b = leaf.add_block(".entry");
    b->emit(IRInstruction::store_param(0, 0, -8, IRWidth::I64));
    b->emit(IRInstruction::store_param(1, 0, -16, IRWidth::I64));
    b->emit(IRInstruction::frame_addr(0, -8));
    b->emit(IRInstruction::load(1, IRValue::vreg(0), 0));
    b->emit(IRInstruction::frame_addr(2, -16));
    b->emit(IRInstruction::load(3, IRValue::vreg(2), 0));
    b->emit(IRInstruction::binary(IROpcode::ADD, 4, IRValue::vreg(1), IRValue::vreg(3)));
    b->emit(IRInstruction::ret(IRValue::vreg(4)));
    return leaf;
}

IRFunction make_caller() {
    IRFunction caller("main");
    auto* b = caller.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 5));
    b->emit(IRInstruction::load_i64(1, 7));
    b->emit(IRInstruction::call(2, "add", { IRValue::vreg(0), IRValue::vreg(1) }));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    return caller;
}

bool has_op(const IRFunction& f, IROpcode op) {
    for (const auto& block : f.blocks) {
        for (const auto& insn : block.instructions) {
            if (insn.op == op) return true;
        }
    }
    return false;
}

size_t count_op(const IRFunction& f, IROpcode op) {
    size_t n = 0;
    for (const auto& block : f.blocks) {
        for (const auto& insn : block.instructions) {
            if (insn.op == op) ++n;
        }
    }
    return n;
}

} // namespace

TEST(inline_splices_leaf_and_removes_call) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{1});
    CHECK(!has_op(caller, IROpcode::CALL));
    CHECK_EQ(count_op(caller, IROpcode::STORE), size_t{2});
    CHECK_EQ(count_op(caller, IROpcode::LOAD), size_t{2});
    CHECK(verify_function(caller).ok());
}

TEST(inline_connects_return_value_to_call_vreg) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    ir_inline::inline_leaves(items);
    const auto& insns = caller.blocks[0].instructions;
    CHECK(insns.back().op == IROpcode::RET);
    CHECK(insns.back().operands[0].is_vreg());
    const uint32_t returned = insns.back().operands[0].vreg_id;
    const IRInstruction* copy_to_result = nullptr;
    for (const auto& insn : insns) {
        if (insn.op == IROpcode::COPY && insn.dst == 2) copy_to_result = &insn;
    }
    CHECK(copy_to_result != nullptr);
    if (copy_to_result) {
        CHECK(copy_to_result->operands[0].is_vreg());
        CHECK(copy_to_result->operands[0].vreg_id > 2);  // fresh vreg from the callee
    }
    CHECK(returned == 2u);
}

TEST(inline_maps_arguments_to_parameter_stores) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    ir_inline::inline_leaves(items);
    size_t seen = 0;
    for (const auto& insn : caller.blocks[0].instructions) {
        if (insn.op != IROpcode::STORE) continue;
        CHECK_EQ(insn.operands.size(), size_t{2});
        CHECK(insn.operands[1].is_vreg());
        const uint32_t arg = insn.operands[1].vreg_id;
        CHECK(arg == 0u || arg == 1u);  // the original caller argument vregs
        ++seen;
    }
    CHECK_EQ(seen, size_t{2});
}

TEST(inline_grows_caller_frame_below_its_own_slots) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    ir_inline::inline_leaves(items);
    CHECK_EQ(items[0].frame_bytes, 48u);
    CHECK_EQ(items[1].frame_bytes, 16u);
    int64_t min_offset = 0;
    for (const auto& insn : caller.blocks[0].instructions) {
        if (insn.op == IROpcode::FRAME_ADDR) {
            min_offset = std::min(min_offset, insn.imm_arg);
        }
    }
    CHECK(min_offset <= -24);  // shifted below the caller's -8..-16 slots
}

TEST(inline_rejects_non_leaf_callee) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    leaf.blocks[0].instructions.push_back(
        IRInstruction::call(IRInstruction::NONE_VREG, "helper", {}));
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.candidates, size_t{0});
    CHECK_EQ(stats.inlined, size_t{0});
    CHECK(has_op(caller, IROpcode::CALL));
}

TEST(inline_rejects_multi_block_callee) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    leaf.add_block(".extra")->emit(IRInstruction::jmp(".entry"));
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{0});
    CHECK(has_op(caller, IROpcode::CALL));
}

TEST(inline_rejects_address_taken_callee) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    IRFunction user("user");
    user.add_block(".entry")->emit(IRInstruction::lea_label(0, "add"));
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16}, {&user, 8} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{0});
    CHECK(has_op(caller, IROpcode::CALL));
}

TEST(inline_rejects_oversized_callee) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    for (int i = 0; i < 20; ++i) {
        leaf.blocks[0].instructions.push_back(IRInstruction::nop());
        // Keep the RET last.
        std::swap(leaf.blocks[0].instructions.back(),
                  leaf.blocks[0].instructions[leaf.blocks[0].instructions.size() - 2]);
    }
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{0});
    CHECK(has_op(caller, IROpcode::CALL));
}

TEST(inline_void_leaf_needs_no_result_copy) {
    IRFunction caller("main");
    auto* b = caller.add_block(".entry");
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "noop", {}));
    b->emit(IRInstruction::ret_void());
    IRFunction leaf("noop");
    leaf.add_block(".entry")->emit(IRInstruction::ret_void());
    std::vector<InlineItem> items = { {&caller, 16}, {&leaf, 0} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{1});
    CHECK(!has_op(caller, IROpcode::CALL));
    CHECK_EQ(count_op(caller, IROpcode::COPY), size_t{0});
    CHECK(verify_function(caller).ok());
}

TEST(inline_rejects_packed_narrow_parameters) {
    IRFunction caller("main");
    auto* b = caller.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 255));
    b->emit(IRInstruction::call(1, "take", { IRValue::vreg(0) }));
    b->emit(IRInstruction::ret(IRValue::vreg(1)));
    IRFunction leaf("take");
    auto* lb = leaf.add_block(".entry");
    lb->emit(IRInstruction::store_param(0, 0, -9, IRWidth::I8));
    lb->emit(IRInstruction::frame_addr(0, -9));
    lb->emit(IRInstruction::load_s8(1, IRValue::vreg(0), 0));
    lb->emit(IRInstruction::ret(IRValue::vreg(1)));
    std::vector<InlineItem> items = { {&caller, 16}, {&leaf, 8} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{0});
    CHECK(has_op(caller, IROpcode::CALL));
}

TEST(inline_rejects_callee_with_computed_addresses) {
    IRFunction caller = make_caller();
    IRFunction leaf = make_add_leaf();
    leaf.blocks[0].instructions.insert(leaf.blocks[0].instructions.begin(),
        IRInstruction::lea(90, IRValue::vreg(0), 8));
    std::vector<InlineItem> items = { {&caller, 32}, {&leaf, 16} };
    InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{0});
    CHECK(has_op(caller, IROpcode::CALL));
}
