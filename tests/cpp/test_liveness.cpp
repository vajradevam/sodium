#include "test_framework.hpp"
#include "ir/liveness.hpp"

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
