#include "test_framework.hpp"
#include "ir/linear_scan.hpp"

#include <set>

static bool allocated(const RegisterAllocation& a, uint32_t vreg) {
    return a.get(vreg).is_allocated();
}

static IRFunction single_vreg_function(uint32_t vreg) {
    IRFunction f("alloc");
    f.add_block(".entry")->emit(IRInstruction::load_i64(vreg, 1));
    return f;
}

TEST(linear_scan_single_vreg_gets_physical_register) {
    IRFunction f = single_vreg_function(1);
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    CHECK(result.is_physical(1));
    CHECK(result.preg_of(1) >= 0);
}

TEST(linear_scan_unknown_vreg_is_unallocated) {
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    IRFunction f = single_vreg_function(1);
    auto result = alloc.allocate(f);
    CHECK(!allocated(result,999));
    CHECK_EQ(result.get(999).kind, RegAllocEntry::UNALLOCATED);
}

TEST(linear_scan_regalloc_entry_predicates) {
    RegAllocEntry physical{};
    physical.kind = RegAllocEntry::PHYSICAL;
    physical.preg = 3;
    CHECK(physical.is_physical());
    CHECK(physical.is_allocated());
    CHECK(!physical.is_spilled());

    RegAllocEntry spilled{};
    spilled.kind = RegAllocEntry::SPILLED;
    spilled.spill_slot = 2;
    CHECK(spilled.is_spilled());
    CHECK(spilled.is_allocated());
    CHECK(!spilled.is_physical());
}

TEST(linear_scan_non_overlapping_ranges_can_share_register) {
    IRFunction f("disjoint");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 1));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(2), IRValue::imm_i64(1)));
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    CHECK(allocated(result,1));
    CHECK(allocated(result,2));
    CHECK(allocated(result,3));
    CHECK_EQ(result.preg_of(1), result.preg_of(3));
}

TEST(linear_scan_overlapping_ranges_get_distinct_registers) {
    IRFunction f("overlap");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 1));
    b->emit(IRInstruction::load_i64(2, 2));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(1), IRValue::vreg(2)));
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    CHECK(result.is_physical(1));
    CHECK(result.is_physical(2));
    CHECK(result.preg_of(1) != result.preg_of(2));
}

TEST(linear_scan_under_pressure_some_values_are_spilled) {
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    const size_t regs = tri.allocatable_regs().size();
    const uint32_t count = static_cast<uint32_t>(regs + 6);

    IRFunction f("pressure");
    auto* b = f.add_block(".entry");
    for (uint32_t i = 0; i < count; ++i) {
        b->emit(IRInstruction::load_i64(i + 1, static_cast<int64_t>(i)));
    }
    for (uint32_t i = 0; i < count; ++i) {
        b->emit(IRInstruction::binary(IROpcode::ADD, count + i + 1,
                                      IRValue::vreg(i + 1), IRValue::imm_i64(0)));
    }

    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    size_t spilled = 0;
    for (uint32_t i = 0; i < count; ++i) {
        CHECK(allocated(result,i + 1));
        if (result.is_spilled(i + 1)) ++spilled;
    }
    CHECK(spilled > 0);
    CHECK(result.total_stack_slots >= spilled);
}

TEST(linear_scan_spill_entries_have_valid_slot) {
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    const uint32_t count = static_cast<uint32_t>(tri.allocatable_regs().size() + 4);
    IRFunction f("spill");
    auto* b = f.add_block(".entry");
    for (uint32_t i = 0; i < count; ++i)
        b->emit(IRInstruction::load_i64(i + 1, static_cast<int64_t>(i)));
    for (uint32_t i = 0; i < count; ++i)
        b->emit(IRInstruction::copy(count + i + 1, IRValue::vreg(i + 1)));

    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    std::set<int> slots;
    for (const auto& [vreg, entry] : result.entries) {
        if (entry.is_spilled()) {
            CHECK(entry.spill_slot >= 0);
            CHECK(entry.preg == -1);
            slots.insert(entry.spill_slot);
        } else {
            CHECK(entry.is_physical());
            CHECK(entry.preg >= 0);
        }
    }
    CHECK_EQ(slots.size(), static_cast<size_t>(result.total_stack_slots));
}

TEST(linear_scan_all_vregs_are_allocated) {
    IRFunction f("all");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(5, 1));
    b->emit(IRInstruction::load_i64(9, 2));
    b->emit(IRInstruction::binary(IROpcode::ADD, 12, IRValue::vreg(5), IRValue::vreg(9)));
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    for (uint32_t v : {5u, 9u, 12u}) CHECK(allocated(result,v));
}

TEST(linear_scan_allocation_is_deterministic) {
    auto build = []() {
        IRFunction f("det");
        auto* b = f.add_block(".entry");
        b->emit(IRInstruction::load_i64(1, 1));
        b->emit(IRInstruction::load_i64(2, 2));
        b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(1), IRValue::vreg(2)));
        return f;
    };
    IRFunction a = build();
    IRFunction b = build();
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc1(tri);
    LinearScanAllocator alloc2(tri);
    auto r1 = alloc1.allocate(a);
    auto r2 = alloc2.allocate(b);
    CHECK_EQ(r1.entries.size(), r2.entries.size());
    for (const auto& [vreg, entry] : r1.entries) {
        CHECK_EQ(entry.kind, r2.get(vreg).kind);
        CHECK_EQ(entry.preg, r2.get(vreg).preg);
        CHECK_EQ(entry.spill_slot, r2.get(vreg).spill_slot);
    }
}

TEST(linear_scan_empty_function_allocates_nothing) {
    IRFunction f("none");
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    CHECK(result.entries.empty());
    CHECK_EQ(result.total_stack_slots, 0u);
}

TEST(linear_scan_prefers_callee_save_for_call_spanning) {
    IRFunction f("call");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 1));
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "foo", {}));
    b->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    auto result = alloc.allocate(f);
    const auto callee = TargetRegisterInfo::x86_64_systemv().callee_save_regs();
    CHECK(allocated(result,1));
    bool in_callee = false;
    for (int r : callee) if (result.preg_of(1) == r) in_callee = true;
    CHECK(in_callee);
}
