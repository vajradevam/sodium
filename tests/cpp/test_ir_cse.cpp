#include "test_framework.hpp"
#include "ir/cse.hpp"
#include "ir/verify.hpp"

#include <ostream>

static std::ostream& operator<<(std::ostream& os, IROpcode op) {
    return os << ir_opcode_name(op);
}

static const IRInstruction& at(const IRFunction& f, size_t index) {
    return f.blocks[0].instructions[index];
}

TEST(ir_cse_duplicate_add_becomes_copy) {
    IRFunction f("add");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    auto stats = cse_function(f);
    CHECK_EQ(at(f, 2).op, IROpcode::COPY);
    CHECK(at(f, 2).operands[0] == IRValue::vreg(2));
    CHECK_EQ(stats.expressions, size_t{1});
    CHECK(verify_function(f).ok());
}

TEST(ir_cse_duplicate_constant_becomes_copy) {
    IRFunction f("const");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 5));
    b->emit(IRInstruction::load_i64(2, 5));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    cse_function(f);
    CHECK_EQ(at(f, 1).op, IROpcode::COPY);
    CHECK(at(f, 1).operands[0] == IRValue::vreg(1));
}

TEST(ir_cse_distinct_constants_are_not_merged) {
    IRFunction f("const2");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 5));
    b->emit(IRInstruction::load_i64(2, 6));
    b->emit(IRInstruction::ret_void());
    auto stats = cse_function(f);
    CHECK_EQ(at(f, 1).op, IROpcode::LOAD_I64);
    CHECK_EQ(stats.expressions, size_t{0});
}

TEST(ir_cse_not_across_call) {
    IRFunction f("call");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "f", {}));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret_void());
    cse_function(f);
    CHECK_EQ(at(f, 3).op, IROpcode::ADD);
}

TEST(ir_cse_not_across_div) {
    IRFunction f("divbarrier");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::binary(IROpcode::DIV, 3, IRValue::vreg(1), IRValue::vreg(1)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 4, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret_void());
    cse_function(f);
    // DIV implicitly clobbers rax/rdx, so extending a value's live range over
    // it would let the allocator reuse a clobbered register.
    CHECK_EQ(at(f, 3).op, IROpcode::ADD);
}

TEST(ir_cse_not_when_operand_is_redefined) {
    IRFunction f("redef");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 1));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::load_i64(1, 9));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret_void());
    cse_function(f);
    CHECK_EQ(at(f, 3).op, IROpcode::ADD);
}

TEST(ir_cse_not_when_result_is_redefined) {
    IRFunction f("result");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::load_i64(2, 99));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(1), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret_void());
    cse_function(f);
    CHECK_EQ(at(f, 3).op, IROpcode::ADD);
}

TEST(ir_cse_forwards_store_to_load) {
    IRFunction f("fwd");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::load_param(2, 1));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    auto stats = cse_function(f);
    CHECK_EQ(at(f, 3).op, IROpcode::COPY);
    CHECK(at(f, 3).operands[0] == IRValue::vreg(2));
    CHECK_EQ(stats.loads_forwarded, size_t{1});
    CHECK(verify_function(f).ok());
}

TEST(ir_cse_load_of_different_offset_is_not_forwarded) {
    IRFunction f("offset");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::load_param(2, 1));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::load(3, IRValue::vreg(1), 8));
    b->emit(IRInstruction::ret_void());
    cse_function(f);
    CHECK_EQ(at(f, 3).op, IROpcode::LOAD);
}

TEST(ir_cse_does_not_forward_across_call) {
    IRFunction f("callfwd");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::load_param(2, 1));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "f", {}));
    b->emit(IRInstruction::load(3, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret_void());
    cse_function(f);
    CHECK_EQ(at(f, 4).op, IROpcode::LOAD);
}

TEST(ir_cse_does_not_forward_across_another_store) {
    IRFunction f("two stores");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::load_param(2, 1));
    b->emit(IRInstruction::load_param(3, 2));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(3)));
    b->emit(IRInstruction::load(4, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(4)));
    cse_function(f);
    CHECK_EQ(at(f, 5).op, IROpcode::COPY);
    CHECK(at(f, 5).operands[0] == IRValue::vreg(3));
}

TEST(ir_cse_removes_duplicate_store) {
    IRFunction f("dupstore");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::load_param(2, 1));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::vreg(2)));
    b->emit(IRInstruction::ret_void());
    auto stats = cse_function(f);
    size_t stores = 0;
    for (const auto& insn : f.blocks[0].instructions) {
        if (insn.op == IROpcode::STORE) ++stores;
    }
    CHECK_EQ(stores, size_t{1});
    CHECK_EQ(stats.stores_removed, size_t{1});
}

TEST(ir_cse_forwards_immediate_store) {
    IRFunction f("immstore");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_param(1, 0));
    b->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::imm_i64(42)));
    b->emit(IRInstruction::load(2, IRValue::vreg(1), 0));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    cse_function(f);
    CHECK_EQ(at(f, 2).op, IROpcode::LOAD_I64);
    CHECK_EQ(at(f, 2).imm_arg, int64_t{42});
}

TEST(ir_cse_is_local_to_each_block) {
    IRFunction f("local");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_param(1, 0));
    e->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::imm_i64(1)));
    e->emit(IRInstruction::jmp(".next"));
    f.add_block(".next")->emit(
        IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(1), IRValue::imm_i64(1)));
    cse_function(f);
    CHECK_EQ(f.blocks[1].instructions[0].op, IROpcode::ADD);
    CHECK(verify_function(f).ok());
}
