#include "test_framework.hpp"
#include "generation.hpp"

// Where float arguments travel (D-038, D-039): the generator's ArgPlacer.

TEST(float_ninth_argument_goes_to_the_stack_on_system_v_and_a_gpr_on_lp64d) {
    const TargetRegisterInfo sysv = TargetRegisterInfo::x86_64_systemv();
    const TargetRegisterInfo lp64d = TargetRegisterInfo::riscv64_lp64();
    ArgPlacer sv(sysv), rv(lp64d);
    ArgPlacement s[9], r[9];
    for (int i = 0; i < 9; i++) { s[i] = sv.fp_scalar(); r[i] = rv.fp_scalar(); }
    for (int i = 0; i < 8; i++) {
        CHECK(s[i].fp_reg && !s[i].on_stack && s[i].index == static_cast<size_t>(i));
        CHECK(r[i].fp_reg && !r[i].on_stack && r[i].index == static_cast<size_t>(i));
    }
    CHECK(s[8].on_stack && !s[8].fp_reg);                        // System V: the stack
    CHECK(!r[8].on_stack && !r[8].fp_reg && r[8].index == 0);    // LP64D: a0
    // An integer argument placed afterwards takes the next integer register.
    CHECK_EQ(rv.scalar().index, 1u);
    CHECK_EQ(sv.scalar().index, 0u);
}

// ---- small structs with floats (X1b, D-040) ----

TEST(float_struct_pieces_go_to_registers_of_their_class) {
    const TargetRegisterInfo sysv = TargetRegisterInfo::x86_64_systemv();
    const TargetRegisterInfo lp64d = TargetRegisterInfo::riscv64_lp64();
    const std::vector<StructPiece> mixed = {{0, 8, true}, {8, 8, false}};
    for (const TargetRegisterInfo* tri : {&sysv, &lp64d}) {
        ArgPlacer p(*tri);
        auto places = p.fp_struct(mixed);
        CHECK(places.has_value());
        CHECK((*places)[0].fp_reg && !(*places)[0].on_stack && (*places)[0].index == 0);
        CHECK(!(*places)[1].fp_reg && !(*places)[1].on_stack && (*places)[1].index == 0);
        CHECK_EQ(p.scalar().index, 1u);   // the integer register was consumed
    }
}

TEST(float_struct_does_not_fit_when_its_register_class_is_exhausted) {
    const TargetRegisterInfo sysv = TargetRegisterInfo::x86_64_systemv();
    ArgPlacer p(sysv);
    for (int i = 0; i < 7; i++) p.fp_scalar();                      // 7 of 8 FP registers
    const std::vector<StructPiece> two_fp = {{0, 8, true}, {8, 8, true}};
    CHECK(!p.fp_struct(two_fp).has_value());                        // needs two, one is left
    const std::vector<StructPiece> one_fp = {{0, 8, true}};
    CHECK(p.fp_struct(one_fp).has_value());
}
