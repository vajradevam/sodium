#include "test_framework.hpp"
#include "ir/liveness.hpp"
#include "ir/linear_scan.hpp"
#include "ir/target_regs.hpp"

#include <algorithm>
#include <string>

static LiveRange range_for(const LiveRanges& ranges, uint32_t vreg) {
    for (const auto& r : ranges) if (r.vreg == vreg) return r;
    return LiveRange{};
}

TEST(liveness_empty_function_has_no_ranges) {
    IRFunction f("empty");
    CHECK(compute_live_ranges(f).empty());
    CHECK_EQ(number_instructions(f).count, 0u);
}

TEST(liveness_single_def_creates_one_range) {
    IRFunction f("one");
    f.add_block(".entry")->emit(IRInstruction::load_i64(1, 5));
    auto ranges = compute_live_ranges(f);
    CHECK_EQ(ranges.size(), 1u);
    if (!ranges.empty()) {
        CHECK_EQ(ranges[0].vreg, 1u);
        CHECK_EQ(ranges[0].start, 0u);
        CHECK_EQ(ranges[0].end, 1u);
    }
}

TEST(liveness_instruction_count_spans_blocks) {
    IRFunction f("blocks");
    f.add_block(".entry")->emit(IRInstruction::load_i64(0, 1));
    f.add_block(".next")->emit(IRInstruction::load_i64(1, 2));
    f.add_block(".third")->emit(IRInstruction::nop());
    auto num = number_instructions(f);
    CHECK_EQ(num.count, 3u);
    CHECK_EQ(num.block_starts[".entry"], 0u);
    CHECK_EQ(num.block_starts[".next"], 1u);
    CHECK_EQ(num.block_starts[".third"], 2u);
}

TEST(liveness_use_after_def_extends_range) {
    IRFunction f("use");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 10));
    b->emit(IRInstruction::nop());
    b->emit(IRInstruction::unary(IROpcode::NEG, 2, IRValue::vreg(1)));
    auto ranges = compute_live_ranges(f);
    CHECK_EQ(ranges.size(), 2u);
    auto r1 = range_for(ranges, 1);
    CHECK_EQ(r1.start, 0u);
    CHECK_EQ(r1.end, 3u);
}

TEST(liveness_first_use_can_precede_def) {
    IRFunction f("use_first");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    b->emit(IRInstruction::load_i64(1, 7));
    auto r1 = range_for(compute_live_ranges(f), 1);
    CHECK_EQ(r1.start, 0u);
    CHECK_EQ(r1.end, 2u);
}

TEST(liveness_ranges_are_sorted_by_start) {
    IRFunction f("sorted");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(9, 1));
    b->emit(IRInstruction::load_i64(4, 2));
    b->emit(IRInstruction::load_i64(1, 3));
    auto ranges = compute_live_ranges(f);
    CHECK_EQ(ranges.size(), 3u);
    for (size_t i = 1; i < ranges.size(); ++i) {
        CHECK(ranges[i - 1].start <= ranges[i].start);
    }
}

TEST(liveness_overlap_predicate) {
    LiveRange a{}; a.start = 0; a.end = 5;
    LiveRange b{}; b.start = 4; b.end = 8;
    LiveRange c{}; c.start = 5; c.end = 8;
    CHECK(a.overlaps(b));
    CHECK(!a.overlaps(c)); // [start,end) half-open
    CHECK(b.overlaps(b));
}

TEST(liveness_non_overlapping_ranges) {
    IRFunction f("disjoint");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 1));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(2), IRValue::imm_i64(1)));
    auto ranges = compute_live_ranges(f);
    auto r1 = range_for(ranges, 1);
    auto r3 = range_for(ranges, 3);
    CHECK(!r1.overlaps(r3));
}

TEST(liveness_call_spanning_range_is_marked) {
    IRFunction f("callspan");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 1));
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "foo", {}));
    b->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    auto r1 = range_for(compute_live_ranges(f), 1);
    CHECK(r1.spans_call);
}

TEST(liveness_range_not_crossing_call_is_unmarked) {
    IRFunction f("nocallspan");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "foo", {}));
    b->emit(IRInstruction::load_i64(1, 1));
    b->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    auto r1 = range_for(compute_live_ranges(f), 1);
    CHECK(!r1.spans_call);
}

TEST(liveness_immediate_only_instructions_produce_no_ranges) {
    IRFunction f("imm");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::nop());
    b->emit(IRInstruction::jmp(".entry"));
    CHECK(compute_live_ranges(f).empty());
}

TEST(liveness_end_is_clamped_to_instruction_count) {
    IRFunction f("clamp");
    f.add_block(".entry")->emit(IRInstruction::load_i64(0, 1));
    auto ranges = compute_live_ranges(f);
    CHECK_EQ(ranges.size(), 1u);
    if (!ranges.empty()) CHECK(ranges[0].end <= number_instructions(f).count);
}

// --- O0: loop-aware liveness ------------------------------------------------

TEST(liveness_loop_value_is_live_across_later_call) {
    IRFunction f("loopcall");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::load_i64(1, 7));   // pos 0: kept in a vreg
    entry->emit(IRInstruction::jmp(".head"));     // pos 1
    auto* head = f.add_block(".head");            // pos 2
    head->emit(IRInstruction::load_i64(0, 1));    // pos 2
    head->emit(IRInstruction::br(IRValue::vreg(0), ".body", ".exit"));  // pos 3
    auto* body = f.add_block(".body");            // pos 4
    body->emit(IRInstruction::copy(2, IRValue::vreg(1)));  // pos 4: early use
    body->emit(IRInstruction::call(IRInstruction::NONE_VREG, "clobber", {}));  // pos 5
    body->emit(IRInstruction::jmp(".head"));      // pos 6: back edge
    f.add_block(".exit")->emit(IRInstruction::ret_void());  // pos 7

    auto r = range_for(compute_live_ranges(f), 1);
    // The last textual use is at pos 4, but the value is live around the loop
    // and across the call at pos 5. Linear-order liveness stops at pos 5.
    CHECK(r.end > 6u);
    CHECK(r.spans_call);
}

TEST(liveness_loop_call_value_uses_callee_save_on_both_targets) {
    IRFunction f("loopalloc");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::load_i64(1, 7));
    entry->emit(IRInstruction::jmp(".head"));
    auto* head = f.add_block(".head");
    head->emit(IRInstruction::load_i64(0, 1));
    head->emit(IRInstruction::br(IRValue::vreg(0), ".body", ".exit"));
    auto* body = f.add_block(".body");
    body->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    body->emit(IRInstruction::call(IRInstruction::NONE_VREG, "clobber", {}));
    body->emit(IRInstruction::jmp(".head"));
    f.add_block(".exit")->emit(IRInstruction::ret_void());

    const auto ranges = compute_live_ranges(f);
    CHECK(range_for(ranges, 1).spans_call);

    const TargetRegisterInfo targets[] = {
        TargetRegisterInfo::x86_64_systemv(),
        TargetRegisterInfo::riscv64_lp64(),
    };
    for (const auto& tri : targets) {
        LinearScanAllocator alloc(tri);
        RegisterAllocation allocation = alloc.allocate(f);
        const std::vector<int> callee_save = tri.callee_save_regs();
        CHECK(std::find(callee_save.begin(), callee_save.end(), allocation.preg_of(1)) !=
              callee_save.end());
        CHECK(!allocation.is_spilled(1));
    }
}

TEST(liveness_nested_loops_extend_outer_value) {
    IRFunction f("nested");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::load_i64(1, 9));
    entry->emit(IRInstruction::jmp(".outer"));
    auto* outer = f.add_block(".outer");          // pos 2
    outer->emit(IRInstruction::load_i64(0, 1));
    outer->emit(IRInstruction::br(IRValue::vreg(0), ".inner", ".exit"));
    auto* inner = f.add_block(".inner");          // pos 4
    inner->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    inner->emit(IRInstruction::call(IRInstruction::NONE_VREG, "clobber", {}));
    inner->emit(IRInstruction::br(IRValue::vreg(0), ".inner", ".outer"));
    f.add_block(".exit")->emit(IRInstruction::ret_void());  // pos 7

    auto r = range_for(compute_live_ranges(f), 1);
    CHECK(r.end >= 7u);   // through the inner back edge
    CHECK(r.spans_call);
}

TEST(liveness_loop_carried_value_covers_back_edge) {
    IRFunction f("carried");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::load_i64(0, 1));   // pos 0
    entry->emit(IRInstruction::jmp(".head"));     // pos 1
    auto* head = f.add_block(".head");            // pos 2
    head->emit(IRInstruction::br(IRValue::vreg(2), ".body", ".exit"));  // pos 2
    auto* body = f.add_block(".body");            // pos 3
    body->emit(IRInstruction::load_i64(1, 5));    // pos 3
    body->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1),
                                     IRValue::vreg(0)));  // pos 4: def of %2
    body->emit(IRInstruction::jmp(".head"));      // pos 5: back edge
    f.add_block(".exit")->emit(IRInstruction::ret_void());  // pos 6

    auto r = range_for(compute_live_ranges(f), 2);
    // The use is at pos 2 (before the def at pos 4 in linear order), but the
    // value must stay live past the back edge into the next iteration.
    CHECK(r.start <= 2u);
    CHECK(r.end >= 6u);
    CHECK(r.end > 4u);
}

TEST(liveness_if_else_value_covers_both_arms) {
    IRFunction f("ifelse");
    auto* entry = f.add_block(".entry");
    entry->emit(IRInstruction::load_i64(1, 7));
    entry->emit(IRInstruction::br(IRValue::vreg(1), ".t", ".f"));
    auto* t = f.add_block(".t");
    t->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    t->emit(IRInstruction::ret(IRValue::vreg(2)));
    auto* fa = f.add_block(".f");
    fa->emit(IRInstruction::copy(3, IRValue::vreg(1)));
    fa->emit(IRInstruction::ret(IRValue::vreg(3)));

    auto r = range_for(compute_live_ranges(f), 1);
    CHECK(r.start == 0u);
    CHECK(r.end >= 5u);  // covers the use in the second arm (pos 4)
}

TEST(liveness_value_across_mod_needs_callee_save) {
    IRFunction f("divclobber");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 7));
    b->emit(IRInstruction::load_i64(2, 2));
    b->emit(IRInstruction::binary(IROpcode::MOD, 3, IRValue::vreg(1), IRValue::vreg(2)));
    b->emit(IRInstruction::copy(4, IRValue::vreg(1)));  // use after the MOD
    b->emit(IRInstruction::ret(IRValue::vreg(4)));

    auto r = range_for(compute_live_ranges(f), 1);
    CHECK(r.crosses_div);
    CHECK(!r.spans_call);

    const TargetRegisterInfo targets[] = {
        TargetRegisterInfo::x86_64_systemv(),
        TargetRegisterInfo::riscv64_lp64(),
    };
    for (const auto& tri : targets) {
        LinearScanAllocator alloc(tri);
        RegisterAllocation allocation = alloc.allocate(f);
        const std::vector<int> callee_save = tri.callee_save_regs();
        CHECK(std::find(callee_save.begin(), callee_save.end(), allocation.preg_of(1)) !=
              callee_save.end());
    }
}
