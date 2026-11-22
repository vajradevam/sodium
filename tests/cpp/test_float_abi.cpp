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
