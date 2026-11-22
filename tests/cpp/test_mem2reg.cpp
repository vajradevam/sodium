#include "test_framework.hpp"
#include "ir/mem2reg.hpp"
#include "ir/liveness.hpp"
#include "ir/verify.hpp"

#include <vector>

static bool has_op(const IRFunction& f, IROpcode op) {
    for (const auto& block : f.blocks) {
        for (const auto& insn : block.instructions) {
            if (insn.op == op) return true;
        }
    }
    return false;
}

static const IRInstruction* find_op(const IRFunction& f, IROpcode op) {
    for (const auto& block : f.blocks) {
        for (const auto& insn : block.instructions) {
            if (insn.op == op) return &insn;
        }
    }
    return nullptr;
}

static LiveRange range_for_vreg(const IRFunction& f, uint32_t vreg) {
    for (const auto& r : compute_live_ranges(f)) if (r.vreg == vreg) return r;
    return LiveRange{};
}

TEST(mem2reg_promotes_simple_local) {
    IRFunction f("simple");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::frame_addr(1, 0));
    b->emit(IRInstruction::load_i64(2, 7));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    auto stats = promote_locals(f);
    CHECK_EQ(stats.promoted_slots, size_t{1});
    CHECK_EQ(stats.loads_replaced, size_t{1});
    CHECK_EQ(stats.stores_replaced, size_t{1});
    CHECK_EQ(stats.frame_addrs_removed, size_t{1});
    CHECK(!has_op(f, IROpcode::FRAME_ADDR));
    CHECK(!has_op(f, IROpcode::STORE));
    CHECK(!has_op(f, IROpcode::LOAD));
    CHECK(verify_function(f).ok());
}

TEST(mem2reg_load_reads_the_promoted_vreg) {
    IRFunction f("value");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::frame_addr(1, 0));
    b->emit(IRInstruction::load_i64(2, 7));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    promote_locals(f);
    std::vector<const IRInstruction*> copies;
    for (const auto& insn : f.blocks[0].instructions) {
        if (insn.op == IROpcode::COPY) copies.push_back(&insn);
    }
    CHECK_EQ(copies.size(), size_t{2});  // store into V, load out of V
    if (copies.size() == 2) {
        CHECK(copies[0]->dst == copies[1]->operands[0].vreg_id);
    }
}

TEST(mem2reg_preserves_narrow_load_sign_extension) {
    IRFunction f("narrow");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::frame_addr(1, 0));
    b->emit(IRInstruction::load_i64(2, 0xFF));
    b->emit(IRInstruction::store_8(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load_s8(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    promote_locals(f);
    const IRInstruction* mask = find_op(f, IROpcode::AND);
    const IRInstruction* extend = find_op(f, IROpcode::SEXT);
    CHECK(mask != nullptr);
    CHECK(extend != nullptr);
    if (mask) CHECK_EQ(mask->operands[1].imm, int64_t{0xFF});
    if (extend) CHECK_EQ(extend->imm_arg, int64_t{8});
    CHECK(verify_function(f).ok());
}

TEST(mem2reg_preserves_unsigned_narrow_load) {
    IRFunction f("unarrow");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::frame_addr(1, 0));
    b->emit(IRInstruction::load_i64(2, 0xFFFF));
    b->emit(IRInstruction::store_16(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load_u16(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    promote_locals(f);
    const IRInstruction* extend = find_op(f, IROpcode::ZEXT);
    CHECK(extend != nullptr);
    if (extend) CHECK_EQ(extend->imm_arg, int64_t{16});
}

TEST(mem2reg_promotes_loop_local_and_liveness_spans_call) {
    IRFunction f("loop");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::frame_addr(1, 0));
    entry->emit(IRInstruction::load_i64(2, 0));
    entry->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    entry->emit(IRInstruction::jmp(".head"));
    auto* head = f.add_block(".head");
    head->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    head->emit(IRInstruction::br(IRValue::vreg(3), ".body", ".exit"));
    auto* body = f.add_block(".body");
    body->emit(IRInstruction::call(IRInstruction::NONE_VREG, "clobber", {}));
    body->emit(IRInstruction::load_i64(4, 1));
    body->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(4)));
    body->emit(IRInstruction::jmp(".head"));
    f.add_block(".exit")->emit(IRInstruction::ret_void());

    auto stats = promote_locals(f);
    CHECK_EQ(stats.promoted_slots, size_t{1});
    CHECK(!has_op(f, IROpcode::LOAD));
    CHECK(!has_op(f, IROpcode::STORE));
    CHECK(verify_function(f).ok());

    const IRInstruction* first_copy = nullptr;
    for (const auto& insn : f.blocks[0].instructions) {
        if (insn.op == IROpcode::COPY) { first_copy = &insn; break; }
    }
    CHECK(first_copy != nullptr);
    if (!first_copy) return;
    const auto r = range_for_vreg(f, first_copy->dst);
    CHECK(r.spans_call);
}

TEST(mem2reg_does_not_promote_address_taken_local) {
    IRFunction f("escaped");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::frame_addr(1, 0));
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "takes_ptr", {IRValue::vreg(1)}));
    b->emit(IRInstruction::load_i64(2, 5));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    auto stats = promote_locals(f);
    CHECK_EQ(stats.promoted_slots, size_t{0});
    CHECK(has_op(f, IROpcode::FRAME_ADDR));
    CHECK(verify_function(f).ok());
}

TEST(mem2reg_does_not_promote_load_without_dominating_store) {
    IRFunction f("nodom");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::frame_addr(1, 0));
    entry->emit(IRInstruction::load_i64(0, 1));
    entry->emit(IRInstruction::br(IRValue::vreg(0), ".t", ".f"));
    auto* t = f.add_block(".t");
    t->emit(IRInstruction::load_i64(2, 9));
    t->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    t->emit(IRInstruction::jmp(".join"));
    f.add_block(".f")->emit(IRInstruction::jmp(".join"));
    auto* join = f.add_block(".join");
    join->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    join->emit(IRInstruction::ret(IRValue::vreg(3)));

    auto stats = promote_locals(f);
    CHECK_EQ(stats.promoted_slots, size_t{0});
    CHECK(has_op(f, IROpcode::STORE));
    CHECK(verify_function(f).ok());
}

TEST(mem2reg_promotes_slot_with_several_frame_addrs) {
    IRFunction f("twoaddrs");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::frame_addr(1, 0));
    entry->emit(IRInstruction::load_i64(2, 3));
    entry->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    entry->emit(IRInstruction::jmp(".next"));
    auto* next = f.add_block(".next");
    next->emit(IRInstruction::frame_addr(4, 0));
    next->emit(IRInstruction::load(3, IRValue::vreg(4), 0));
    next->emit(IRInstruction::ret(IRValue::vreg(3)));
    auto stats = promote_locals(f);
    CHECK_EQ(stats.promoted_slots, size_t{1});
    CHECK_EQ(stats.frame_addrs_removed, size_t{2});
    CHECK(verify_function(f).ok());
}

TEST(mem2reg_ignores_non_frame_memory) {
    IRFunction f("global");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::lea_label(1, "global_var"));
    b->emit(IRInstruction::load_i64(2, 7));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    auto stats = promote_locals(f);
    CHECK_EQ(stats.promoted_slots, size_t{0});
    CHECK(has_op(f, IROpcode::STORE));
}
