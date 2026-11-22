#include "test_framework.hpp"
#include "ir/linear_scan.hpp"

TEST(regalloc_stats_simple_function_has_no_spills) {
    IRFunction f("simple");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 1));
    b->emit(IRInstruction::unary(IROpcode::NEG, 1, IRValue::vreg(0)));
    b->emit(IRInstruction::ret(IRValue::vreg(1)));
    TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    RegisterAllocation result = alloc.allocate(f);
    CHECK_EQ(result.vreg_count, size_t{2});
    CHECK_EQ(result.spill_count, size_t{0});
    CHECK_EQ(result.total_stack_slots, 0u);
}

TEST(regalloc_stats_high_pressure_spills) {
    IRFunction f("pressure");
    auto* b = f.add_block(".entry");
    for (uint32_t i = 0; i < 4; ++i) {
        b->emit(IRInstruction::load_i64(i, static_cast<int64_t>(i)));
    }
    // All four values must stay live at once for the single register to
    // matter: a ret after each load would end each interval immediately.
    b->emit(IRInstruction::binary(IROpcode::ADD, 4, IRValue::vreg(0), IRValue::vreg(1)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 5, IRValue::vreg(4), IRValue::vreg(2)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 6, IRValue::vreg(5), IRValue::vreg(3)));
    b->emit(IRInstruction::ret(IRValue::vreg(6)));
    TargetRegisterInfo tri;
    tri.add(0, "r0", true, false);
    LinearScanAllocator alloc(tri);
    RegisterAllocation result = alloc.allocate(f);
    CHECK_EQ(result.vreg_count, size_t{7});
    CHECK(result.spill_count > 0);
    CHECK_EQ(result.total_stack_slots, result.spill_count);
}

TEST(regalloc_stats_counts_callee_save_registers) {
    IRFunction f("callspan");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 1));
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "clobber", {}));
    b->emit(IRInstruction::ret(IRValue::vreg(0)));
    TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    RegisterAllocation result = alloc.allocate(f);
    CHECK(result.callee_save_count >= 1);
}

TEST(regalloc_stats_div_crossing_uses_callee_save) {
    IRFunction f("divspan");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 7));
    b->emit(IRInstruction::load_i64(1, 2));
    b->emit(IRInstruction::binary(IROpcode::MOD, 2, IRValue::vreg(0), IRValue::vreg(1)));
    b->emit(IRInstruction::ret(IRValue::vreg(0)));
    TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    LinearScanAllocator alloc(tri);
    RegisterAllocation result = alloc.allocate(f);
    CHECK(result.callee_save_count >= 1);
}

TEST(regalloc_spill_evicts_value_used_later) {
    IRFunction f("nextuse");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 1));   // pos 0, used at 4
    b->emit(IRInstruction::load_i64(1, 2));   // pos 1, used at 10
    b->emit(IRInstruction::load_i64(2, 3));   // pos 2, used at 3
    b->emit(IRInstruction::copy(3, IRValue::vreg(2)));  // pos 3
    b->emit(IRInstruction::copy(4, IRValue::vreg(0)));  // pos 4
    for (int i = 0; i < 5; ++i) b->emit(IRInstruction::nop());  // pos 5..9
    b->emit(IRInstruction::copy(5, IRValue::vreg(1)));  // pos 10
    b->emit(IRInstruction::ret_void());  // pos 11

    TargetRegisterInfo tri;
    tri.add(0, "r0", true, false);  // one register forces a choice
    LinearScanAllocator alloc(tri);
    RegisterAllocation result = alloc.allocate(f);
    CHECK(result.is_spilled(1));    // farthest next use (10) gets evicted
    CHECK(!result.is_spilled(2));   // nearest next use (3) keeps the register
}
