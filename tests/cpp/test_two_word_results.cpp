#include "test_framework.hpp"
#include "emit_helpers.hpp"

#include <map>
#include <sstream>
#include <string>
#include <vector>

// Two-word results (RET2 / CALL with dst2, D-012) with every combination of
// register homes: the values must reach rax:rdx (or their homes) even when a
// home is the other word's register or a spill slot.

namespace {

// x86_64 numbering: rax=0 rcx=2 rdx=3 rsi=4 rdi=5 r11 is the scratch register.
constexpr int RAX = 0;
constexpr int RCX = 2;
constexpr int RDX = 3;

IRFunction ret2_function(IRValue lo, IRValue hi) {
    IRFunction func("r2");
    auto* entry = func.add_block(".entry");
    entry->emit(IRInstruction::ret2(lo, hi));
    func.next_vreg = 8;
    return func;
}

IRFunction call2_function(uint32_t dst, uint32_t dst2) {
    IRFunction func("c2");
    auto* entry = func.add_block(".entry");
    auto call = IRInstruction::call(dst, "callee", {});
    call.dst2 = dst2;
    entry->emit(call);
    entry->emit(IRInstruction::ret_void());
    func.next_vreg = 8;
    return func;
}

} // namespace

TEST(ret2_plain_registers) {
    IRFunction f = ret2_function(IRValue::vreg(0), IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, reg(4)}});
    CHECK(contains_in_order(lines, {"mov r11, rcx", "mov rdx, rsi", "mov rax, r11", "ret"}));
}

TEST(ret2_operands_swapped_between_return_registers) {
    // lo lives in rdx, hi lives in rax: a naive pair of moves would lose one.
    IRFunction f = ret2_function(IRValue::vreg(0), IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RDX)}, {1, reg(RAX)}});
    CHECK(contains_in_order(lines, {"mov r11, rdx", "mov rdx, rax", "mov rax, r11", "ret"}));
}

TEST(ret2_both_spilled) {
    IRFunction f = ret2_function(IRValue::vreg(0), IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, slot(0)}, {1, slot(1)}});
    // Slot 1's value goes straight to rdx, slot 0's value is parked in r11.
    int loads = 0;
    for (const auto& l : lines) if (l.rfind("mov r11, [rbp", 0) == 0 || l.rfind("mov rdx, [rbp", 0) == 0) ++loads;
    CHECK_EQ(loads, 2);
    CHECK(contains_in_order(lines, {"mov rax, r11", "ret"}));
}

TEST(ret2_immediates) {
    IRFunction f = ret2_function(IRValue::imm_i64(7), IRValue::imm_i64(9));
    auto lines = emit_lines(f, {});
    CHECK(contains_in_order(lines, {"mov rdx, 9", "mov rax, 7", "ret"}));
}

TEST(call2_results_into_plain_registers) {
    IRFunction f = call2_function(0, 1);
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, reg(4)}});
    CHECK(contains_in_order(after(lines, "call callee"), {"mov r11, rdx", "mov rcx, rax", "mov rsi, r11"}));
}

TEST(call2_results_in_swapped_return_registers) {
    // dst lives in rdx and dst2 lives in rax: the second word must be saved first.
    IRFunction f = call2_function(0, 1);
    auto lines = emit_lines(f, {{0, reg(RDX)}, {1, reg(RAX)}});
    CHECK(contains_in_order(after(lines, "call callee"),
                            {"mov r11, rdx", "mov rdx, rax", "mov rax, r11"}));
}

TEST(call2_results_both_spilled) {
    IRFunction f = call2_function(0, 1);
    auto lines = emit_lines(f, {{0, slot(0)}, {1, slot(1)}});
    auto tail = after(lines, "call callee");
    CHECK(contains_in_order(tail, {"mov r11, rdx"}));
    int stores = 0;
    for (const auto& l : tail) {
        if (l.rfind("mov [rbp", 0) == 0) ++stores;
    }
    CHECK_EQ(stores, 2);
}
