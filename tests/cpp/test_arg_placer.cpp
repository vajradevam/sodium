#include "test_framework.hpp"
#include "generation.hpp"

// Argument-word placement (D-012) for both ABIs.

namespace {

const TargetRegisterInfo X86 = TargetRegisterInfo::x86_64_systemv();
const TargetRegisterInfo RV = TargetRegisterInfo::riscv64_lp64();

bool is_reg(const ArgPlacement& p, size_t index) { return !p.on_stack && p.index == index; }
bool is_stack(const ArgPlacement& p, size_t index) { return p.on_stack && p.index == index; }

} // namespace

TEST(arg_placer_scalars_fill_registers_then_stack) {
    ArgPlacer placer(X86);
    for (size_t i = 0; i < 6; i++) CHECK(is_reg(placer.scalar(), i));
    CHECK(is_stack(placer.scalar(), 0));
    CHECK(is_stack(placer.scalar(), 1));
}

TEST(arg_placer_ordinal_maps_stack_words_after_registers) {
    ArgPlacer placer(X86);
    CHECK_EQ(placer.ordinal({false, 2}), static_cast<size_t>(2));
    CHECK_EQ(placer.ordinal({true, 0}), static_cast<size_t>(6));
    CHECK_EQ(placer.ordinal({true, 3}), static_cast<size_t>(9));
    ArgPlacer rv(RV);
    CHECK_EQ(rv.ordinal({true, 0}), static_cast<size_t>(8));
}

TEST(arg_placer_x86_struct_that_does_not_fit_goes_whole_to_stack) {
    ArgPlacer placer(X86);
    for (int i = 0; i < 5; i++) placer.scalar();
    auto pair = placer.small_struct(2);
    CHECK(is_stack(pair[0], 0));
    CHECK(is_stack(pair[1], 1));
    // The free register is still available to a later scalar.
    CHECK(is_reg(placer.scalar(), 5));
}

TEST(arg_placer_x86_struct_that_fits_uses_registers) {
    ArgPlacer placer(X86);
    for (int i = 0; i < 4; i++) placer.scalar();
    auto pair = placer.small_struct(2);
    CHECK(is_reg(pair[0], 4));
    CHECK(is_reg(pair[1], 5));
    CHECK(is_stack(placer.scalar(), 0));
}

TEST(arg_placer_riscv_struct_splits_between_register_and_stack) {
    ArgPlacer placer(RV);
    for (int i = 0; i < 7; i++) placer.scalar();
    auto pair = placer.small_struct(2);
    CHECK(is_reg(pair[0], 7));
    CHECK(is_stack(pair[1], 0));
}

TEST(arg_placer_memory_struct_is_all_stack_and_keeps_registers_free) {
    ArgPlacer placer(X86);
    auto big = placer.stack_struct(3);
    CHECK(is_stack(big[0], 0));
    CHECK(is_stack(big[1], 1));
    CHECK(is_stack(big[2], 2));
    CHECK(is_reg(placer.scalar(), 0));
}

TEST(arg_placer_stack_words_across_structs_stay_in_order) {
    ArgPlacer placer(X86);
    auto a = placer.stack_struct(3);
    for (int i = 0; i < 6; i++) placer.scalar();
    auto b = placer.small_struct(1);
    CHECK(is_stack(a[2], 2));
    CHECK(is_stack(b[0], 3));
}
