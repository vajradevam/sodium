#include "test_framework.hpp"
#include "emit_helpers.hpp"

// CALL_INDIRECT (D-014): the target must reach the call-target register (rax / t6) before the
// call, whatever its home is, and without being overwritten by argument setup.

namespace {

// x86_64: rax=0 rcx=2 rdx=3 rsi=4 rdi=5 r8=8; r11 is the scratch register.
constexpr int RAX = 0, RCX = 2, RSI = 4, RDI = 5, R8 = 8;
// riscv64: a0=10 a1=11 a2=12 t1=6 t6=31; t0 is the scratch register.
constexpr int A0 = 10, A1 = 11, T1 = 6, T6 = 31;

IRFunction indirect_function(std::vector<IRValue> args, IRValue target, int reg_args = -1) {
    IRFunction func("ic");
    auto* entry = func.add_block(".entry");
    auto call = IRInstruction::call_indirect(20, target, args);
    call.reg_args = reg_args;
    entry->emit(call);
    entry->emit(IRInstruction::ret_void());
    func.next_vreg = 24;
    return func;
}

} // namespace

TEST(indirect_call_target_in_plain_register_x86) {
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, reg(R8)}, {20, reg(RCX)}});
    CHECK(contains_in_order(lines, {"mov rdi, rcx", "mov rax, r8", "call rax"}));
}

TEST(indirect_call_target_in_argument_register_x86) {
    // The target lives in rdi, which argument 0 is about to overwrite: it must be read first.
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, reg(RDI)}, {20, reg(RCX)}});
    CHECK(contains_in_order(lines, {"mov rax, rdi", "mov rdi, rcx", "call rax"}));
}

TEST(indirect_call_target_already_in_call_register_x86) {
    // The target's own register is the call-target register and argument 1 reads it too.
    IRFunction f = indirect_function({IRValue::vreg(0), IRValue::vreg(1)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, reg(RAX)}, {20, reg(RCX)}});
    CHECK(contains_in_order(lines, {"mov rsi, rax", "call rax"}));
}

TEST(indirect_call_target_spilled_x86) {
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, slot(0)}, {20, reg(RCX)}});
    bool loaded = false;
    for (const auto& l : lines) if (l.rfind("mov rax, [rbp", 0) == 0) loaded = true;
    CHECK(loaded);
    CHECK(contains_in_order(lines, {"mov rdi, rcx", "call rax"}));
}

TEST(indirect_call_argument_and_target_spilled_x86) {
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, slot(0)}, {1, slot(1)}, {20, reg(RCX)}});
    int loads = 0;
    for (const auto& l : lines)
        if (l.rfind("mov rdi, [rbp", 0) == 0 || l.rfind("mov rax, [rbp", 0) == 0) ++loads;
    CHECK_EQ(loads, 2);
    CHECK(contains_in_order(lines, {"call rax"}));
}

TEST(indirect_call_immediate_arguments_x86) {
    IRFunction f = indirect_function({IRValue::imm_i64(5), IRValue::imm_i64(6)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{1, reg(RSI)}, {20, reg(RCX)}});
    // The target is read from rsi before the immediate overwrites rsi.
    CHECK(contains_in_order(lines, {"mov rax, rsi", "mov rdi, 5", "mov rsi, 6", "call rax"}));
}

TEST(indirect_call_stack_arguments_x86) {
    // Eight arguments: six in registers, two pushed; the target must survive the pushes.
    std::vector<IRValue> args;
    for (uint32_t i = 0; i < 8; i++) args.push_back(IRValue::imm_i64(static_cast<int64_t>(i)));
    IRFunction f = indirect_function(args, IRValue::vreg(1));
    auto lines = emit_lines(f, {{1, reg(8)}, {20, reg(RCX)}});
    int pushes = 0;
    for (const auto& l : lines) if (l.rfind("push", 0) == 0) ++pushes;
    CHECK(pushes >= 2);
    CHECK(contains_in_order(lines, {"mov rax, r8", "call rax"}));
}

TEST(indirect_call_result_moves_to_home_x86) {
    IRFunction f = indirect_function({}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{1, reg(RSI)}, {20, reg(RCX)}});
    CHECK(contains_in_order(lines, {"mov rax, rsi", "call rax", "mov rcx, rax"}));
}

TEST(indirect_call_plain_registers_riscv) {
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(T1)}, {1, reg(7)}, {20, reg(T1)}}, TestTarget::RiscV64);
    CHECK(contains_in_order(lines, {"mv a0, t1", "mv t6, t2", "jalr ra, 0(t6)"}));
}

TEST(indirect_call_target_in_argument_register_riscv) {
    // The target lives in a0, which argument 0 is about to overwrite.
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(A1)}, {1, reg(A0)}, {20, reg(T1)}}, TestTarget::RiscV64);
    CHECK(contains_in_order(lines, {"mv t6, a0", "mv a0, a1", "jalr ra, 0(t6)"}));
}

TEST(indirect_call_target_spilled_riscv) {
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(T1)}, {1, slot(0)}, {20, reg(T1)}}, TestTarget::RiscV64);
    bool loaded = false;
    for (const auto& l : lines) if (l.rfind("ld t6,", 0) == 0) loaded = true;
    CHECK(loaded);
    CHECK(contains_in_order(lines, {"mv a0, t1", "jalr ra, 0(t6)"}));
}

TEST(indirect_call_target_in_call_register_riscv) {
    IRFunction f = indirect_function({IRValue::vreg(0)}, IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(T1)}, {1, reg(T6)}, {20, reg(T1)}}, TestTarget::RiscV64);
    CHECK(contains_in_order(lines, {"mv a0, t1", "jalr ra, 0(t6)"}));
}
