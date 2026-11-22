#include "test_framework.hpp"
#include "ir/fold.hpp"
#include "ir/verify.hpp"

#include <cstdint>
#include <limits>
#include <ostream>

static std::ostream& operator<<(std::ostream& os, IROpcode op) {
    return os << ir_opcode_name(op);
}

/// The instruction just before the block's terminating ret.
static IRInstruction& last(IRFunction& f) {
    auto& insns = f.blocks[0].instructions;
    return insns[insns.size() - 2];
}

TEST(ir_fold_add_two_constants) {
    IRFunction f("add");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 2));
    b->emit(IRInstruction::load_i64(1, 3));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(0), IRValue::vreg(1)));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    auto stats = fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::LOAD_I64);
    CHECK_EQ(last(f).imm_arg, int64_t{5});
    CHECK_EQ(stats.constants, size_t{1});
    CHECK(verify_function(f).ok());
}

TEST(ir_fold_add_zero_becomes_copy) {
    IRFunction f("add0");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(0)));
    b->emit(IRInstruction::ret_void());
    auto stats = fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::COPY);
    CHECK(last(f).operands[0] == IRValue::vreg(1));
    CHECK_EQ(stats.simplifications, size_t{1});
}

TEST(ir_fold_zero_add_becomes_copy) {
    IRFunction f("zeroadd");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::imm_i64(0), IRValue::vreg(1)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::COPY);
    CHECK(last(f).operands[0] == IRValue::vreg(1));
}

TEST(ir_fold_sub_zero_and_mul_one) {
    IRFunction f("submul");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::SUB, 2, IRValue::vreg(1), IRValue::imm_i64(0)));
    b->emit(IRInstruction::binary(IROpcode::MUL, 3, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns[1].op, IROpcode::COPY);
    CHECK_EQ(insns[2].op, IROpcode::COPY);
}

TEST(ir_fold_mul_zero_becomes_constant) {
    IRFunction f("mulzero");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::MUL, 2, IRValue::vreg(1), IRValue::imm_i64(0)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::LOAD_I64);
    CHECK_EQ(last(f).imm_arg, int64_t{0});
}

TEST(ir_fold_mul_power_of_two_becomes_shift) {
    IRFunction f("strength");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::MUL, 2, IRValue::vreg(1), IRValue::imm_i64(8)));
    b->emit(IRInstruction::ret_void());
    auto stats = fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::SHL);
    CHECK(last(f).operands[0] == IRValue::vreg(1));
    CHECK(last(f).operands[1].is_imm());
    CHECK_EQ(last(f).operands[1].imm, int64_t{3});
    CHECK_EQ(stats.simplifications, size_t{1});
    CHECK(verify_function(f).ok());
}

TEST(ir_fold_wrapping_add_matches_runtime) {
    IRFunction f("wrapadd");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::binary(IROpcode::ADD, 0, IRValue::imm_i64(INT64_MAX), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::LOAD_I64);
    CHECK_EQ(last(f).imm_arg, std::numeric_limits<int64_t>::min());
}

TEST(ir_fold_wrapping_mul_matches_runtime) {
    IRFunction f("wrapmul");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::binary(IROpcode::MUL, 0, IRValue::imm_i64(INT64_MAX), IRValue::imm_i64(2)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).imm_arg, int64_t{-2});
}

TEST(ir_fold_div_by_zero_is_not_folded) {
    IRFunction f("divzero");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::binary(IROpcode::DIV, 0, IRValue::imm_i64(1), IRValue::imm_i64(0)));
    b->emit(IRInstruction::ret_void());
    auto stats = fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::DIV);
    CHECK_EQ(stats.constants, size_t{0});
}

TEST(ir_fold_div_overflow_is_not_folded) {
    IRFunction f("divovf");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::binary(IROpcode::DIV, 0, IRValue::imm_i64(INT64_MIN), IRValue::imm_i64(-1)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::DIV);
}

TEST(ir_fold_constant_division_is_folded) {
    IRFunction f("div");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::binary(IROpcode::DIV, 0, IRValue::imm_i64(-17), IRValue::imm_i64(5)));
    b->emit(IRInstruction::binary(IROpcode::MOD, 1, IRValue::imm_i64(-17), IRValue::imm_i64(5)));
    b->emit(IRInstruction::ret_void());
    auto stats = fold_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns[0].imm_arg, int64_t{-3});
    CHECK_EQ(insns[1].imm_arg, int64_t{-2});
    CHECK_EQ(stats.constants, size_t{2});
}

TEST(ir_fold_cmp_constants) {
    IRFunction f("cmp");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::cmp(IROpcode::CMP_LT, 0, IRValue::imm_i64(2), IRValue::imm_i64(3)));
    b->emit(IRInstruction::cmp(IROpcode::CMP_EQ, 1, IRValue::imm_i64(2), IRValue::imm_i64(3)));
    b->emit(IRInstruction::cmp(IROpcode::CMP_GE, 2, IRValue::imm_i64(3), IRValue::imm_i64(3)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns[0].imm_arg, int64_t{1});
    CHECK_EQ(insns[1].imm_arg, int64_t{0});
    CHECK_EQ(insns[2].imm_arg, int64_t{1});
}

TEST(ir_fold_unary_constants) {
    IRFunction f("unary");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::unary(IROpcode::NEG, 0, IRValue::imm_i64(5)));
    b->emit(IRInstruction::unary(IROpcode::NOT_, 1, IRValue::imm_i64(0)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns[0].imm_arg, int64_t{-5});
    CHECK_EQ(insns[1].imm_arg, int64_t{-1});
}

TEST(ir_fold_extend_and_truncate_constants) {
    IRFunction f("conv");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::zext(0, IRValue::imm_i64(0x1FF), IRWidth::I8));
    b->emit(IRInstruction::sext(1, IRValue::imm_i64(0xFF), IRWidth::I8));
    b->emit(IRInstruction::trunc(2, IRValue::imm_i64(0x12345678), IRWidth::I16));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns[0].imm_arg, int64_t{255});
    CHECK_EQ(insns[1].imm_arg, int64_t{-1});
    CHECK_EQ(insns[2].imm_arg, int64_t{0x12345678});  // TRUNC is a no-op in the emitter
}

TEST(ir_fold_shift_by_zero_becomes_copy) {
    IRFunction f("shift0");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::SHL, 2, IRValue::vreg(1), IRValue::imm_i64(0)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::COPY);
}

TEST(ir_fold_xor_self_becomes_constant) {
    IRFunction f("xorself");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::XOR, 2, IRValue::vreg(1), IRValue::vreg(1)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::LOAD_I64);
    CHECK_EQ(last(f).imm_arg, int64_t{0});
}

TEST(ir_fold_does_not_propagate_multi_def_vregs) {
    IRFunction f("phi");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 0));   // dominating default def
    e->emit(IRInstruction::load_i64(1, 1));
    e->emit(IRInstruction::br(IRValue::vreg(1), ".t", ".f"));
    auto* t = f.add_block(".t");
    t->emit(IRInstruction::load_i64(2, 100));
    t->emit(IRInstruction::copy(0, IRValue::vreg(2)));
    t->emit(IRInstruction::jmp(".join"));
    auto* fa = f.add_block(".f");
    fa->emit(IRInstruction::load_i64(3, 7));
    fa->emit(IRInstruction::copy(0, IRValue::vreg(3)));
    fa->emit(IRInstruction::jmp(".join"));
    auto* join = f.add_block(".join");
    join->emit(IRInstruction::binary(IROpcode::ADD, 4, IRValue::vreg(0), IRValue::imm_i64(1)));
    join->emit(IRInstruction::ret(IRValue::vreg(4)));
    fold_function(f);
    // %0 has three defs: it is phi-like, so the join must keep the ADD rather
    // than folding it using the last def's constant seen in linear order.
    auto& insns = f.blocks[3].instructions;
    CHECK(insns[0].op == IROpcode::ADD);
    CHECK(verify_function(f).ok());
}

TEST(ir_fold_propagates_constants) {
    IRFunction f("prop");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 7));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(2), IRValue::imm_i64(2)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns[1].imm_arg, int64_t{8});
    CHECK_EQ(insns[2].imm_arg, int64_t{10});
}

TEST(ir_fold_leaves_calls_and_memory_alone) {
    IRFunction f("sidefx");
    auto* b = f.add_block(".entry");
    IRInstruction call = IRInstruction::call(1, "f", {IRValue::vreg(0)});
    IRInstruction store = IRInstruction::store(IRValue::vreg(0), 0, IRValue::vreg(1));
    b->emit(call);
    b->emit(store);
    b->emit(IRInstruction::ret_void());
    auto stats = fold_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK(insns[0].op == IROpcode::CALL);
    CHECK(insns[1].op == IROpcode::STORE);
    CHECK_EQ(stats.constants, size_t{0});
    CHECK_EQ(stats.simplifications, size_t{0});
}

TEST(ir_fold_shift_with_large_count_is_not_folded) {
    IRFunction f("shiftbig");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::binary(IROpcode::SHL, 0, IRValue::imm_i64(1), IRValue::imm_i64(64)));
    b->emit(IRInstruction::ret_void());
    fold_function(f);
    CHECK_EQ(last(f).op, IROpcode::SHL);
}
