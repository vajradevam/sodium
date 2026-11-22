#include "test_framework.hpp"
#include "emit_helpers.hpp"

// x86-64 encodes a register shift count only in CL. A live value that happens
// to be allocated to RCX must be preserved across the shift; r11 is the
// reserved scratch register used for that (src/ir/target_regs.hpp).

namespace {

constexpr int RAX = 0, RCX = 2, RDX = 3, RSI = 4, RDI = 5;

} // namespace

TEST(variable_ashr_preserves_live_rcx_x86) {
    IRFunction f("vs");
    auto* entry = f.add_block(".entry");
    uint32_t live = f.new_vreg();     // RCX, read after the shift
    uint32_t value = f.new_vreg();    // RAX
    uint32_t count = f.new_vreg();    // RDX, copied into CL
    uint32_t shifted = f.new_vreg();  // RSI
    uint32_t sum = f.new_vreg();      // RDI
    entry->emit(IRInstruction::binary(IROpcode::ASHR, shifted,
                                      IRValue::vreg(value), IRValue::vreg(count)));
    entry->emit(IRInstruction::binary(IROpcode::ADD, sum,
                                      IRValue::vreg(live), IRValue::vreg(shifted)));
    entry->emit(IRInstruction::ret_void());

    auto lines = emit_lines(f, {{live, reg(RCX)}, {value, reg(RAX)},
                                {count, reg(RDX)}, {shifted, reg(RSI)},
                                {sum, reg(RDI)}});
    CHECK(contains_in_order(lines, {"mov r11, rcx", "mov cl, dl", "sar rsi, cl",
                                    "mov rcx, r11"}));
}

TEST(variable_shl_preserves_live_rcx_x86) {
    IRFunction f("vs");
    auto* entry = f.add_block(".entry");
    uint32_t live = f.new_vreg();
    uint32_t value = f.new_vreg();
    uint32_t count = f.new_vreg();
    uint32_t shifted = f.new_vreg();
    uint32_t sum = f.new_vreg();
    entry->emit(IRInstruction::binary(IROpcode::SHL, shifted,
                                      IRValue::vreg(value), IRValue::vreg(count)));
    entry->emit(IRInstruction::binary(IROpcode::ADD, sum,
                                      IRValue::vreg(live), IRValue::vreg(shifted)));
    entry->emit(IRInstruction::ret_void());

    auto lines = emit_lines(f, {{live, reg(RCX)}, {value, reg(RAX)},
                                {count, reg(RDX)}, {shifted, reg(RSI)},
                                {sum, reg(RDI)}});
    CHECK(contains_in_order(lines, {"mov r11, rcx", "mov cl, dl", "shl rsi, cl",
                                    "mov rcx, r11"}));
}

TEST(variable_shr_with_rcx_destination_x86) {
    // When the destination is RCX the value is shifted in r11 and moved back.
    IRFunction f("vs");
    auto* entry = f.add_block(".entry");
    uint32_t value = f.new_vreg();
    uint32_t count = f.new_vreg();
    uint32_t shifted = f.new_vreg();
    entry->emit(IRInstruction::binary(IROpcode::SHR, shifted,
                                      IRValue::vreg(value), IRValue::vreg(count)));
    entry->emit(IRInstruction::ret_void());

    auto lines = emit_lines(f, {{value, reg(RAX)}, {count, reg(RDX)},
                                {shifted, reg(RCX)}});
    CHECK(contains_in_order(lines, {"mov r11, rcx", "mov cl, dl", "shr r11, cl",
                                    "mov rcx, r11"}));
}
