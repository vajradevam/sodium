#include "test_framework.hpp"
#include "ir/verify.hpp"

#include <string>

static bool has_error(const IRVerifyResult& result, const std::string& needle) {
    for (const auto& e : result.errors) {
        if (e.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

TEST(ir_verify_valid_straight_line) {
    IRFunction f("valid");
    auto* b = f.add_block(".entry");
    uint32_t a = f.new_vreg();
    uint32_t r = f.new_vreg();
    b->emit(IRInstruction::load_i64(a, 5));
    b->emit(IRInstruction::binary(IROpcode::ADD, r, IRValue::vreg(a), IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret(IRValue::vreg(r)));
    auto result = verify_function(f);
    CHECK(result.ok());
    if (!result.ok()) CHECK_EQ(result.to_string(), std::string());
}

TEST(ir_verify_undefined_vreg_is_rejected) {
    IRFunction f("undef");
    f.add_block(".entry")->emit(IRInstruction::ret(IRValue::vreg(7)));
    auto result = verify_function(f);
    CHECK(has_error(result, "not dominated by a definition"));
}

TEST(ir_verify_use_before_def_in_one_block_is_rejected) {
    IRFunction f("before");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    b->emit(IRInstruction::load_i64(1, 7));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    auto result = verify_function(f);
    CHECK(has_error(result, "not dominated by a definition"));
}

TEST(ir_verify_dangling_jmp_target_is_rejected) {
    IRFunction f("jmp");
    f.add_block(".entry")->emit(IRInstruction::jmp(".nowhere"));
    auto result = verify_function(f);
    CHECK(has_error(result, "unknown block label"));
}

TEST(ir_verify_dangling_br_target_is_rejected) {
    IRFunction f("br");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 1));
    e->emit(IRInstruction::br(IRValue::vreg(0), ".yes", ".missing"));
    f.add_block(".yes")->emit(IRInstruction::jmp(".join"));
    f.add_block(".no")->emit(IRInstruction::jmp(".join"));
    f.add_block(".join")->emit(IRInstruction::ret_void());
    auto result = verify_function(f);
    CHECK(has_error(result, "unknown block label"));
}

TEST(ir_verify_duplicate_block_label_is_rejected) {
    IRFunction f("dup");
    f.add_block(".entry")->emit(IRInstruction::ret_void());
    f.add_block(".entry")->emit(IRInstruction::ret_void());
    auto result = verify_function(f);
    CHECK(has_error(result, "duplicate block label"));
}

TEST(ir_verify_def_in_one_branch_does_not_dominate_join) {
    IRFunction f("diamond");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 1));
    e->emit(IRInstruction::br(IRValue::vreg(0), ".t", ".f"));
    auto* t = f.add_block(".t");
    t->emit(IRInstruction::load_i64(2, 10));
    t->emit(IRInstruction::jmp(".join"));
    auto* fa = f.add_block(".f");
    fa->emit(IRInstruction::jmp(".join"));
    f.add_block(".join")->emit(IRInstruction::ret(IRValue::vreg(2)));
    auto result = verify_function(f);
    CHECK(has_error(result, "not dominated by a definition"));
}

TEST(ir_verify_def_in_entry_dominates_both_branches) {
    IRFunction f("diamond_ok");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 1));
    e->emit(IRInstruction::load_i64(2, 10));
    e->emit(IRInstruction::br(IRValue::vreg(0), ".t", ".f"));
    f.add_block(".t")->emit(IRInstruction::ret(IRValue::vreg(2)));
    f.add_block(".f")->emit(IRInstruction::ret(IRValue::vreg(2)));
    CHECK(verify_function(f).ok());
}

TEST(ir_verify_loop_back_edge_keeps_preheader_def_dominant) {
    IRFunction f("loop");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 3));
    e->emit(IRInstruction::load_i64(1, 0));
    e->emit(IRInstruction::jmp(".head"));
    auto* h = f.add_block(".head");
    h->emit(IRInstruction::br(IRValue::vreg(0), ".body", ".exit"));
    auto* body = f.add_block(".body");
    body->emit(IRInstruction::binary(IROpcode::ADD, 2, IRValue::vreg(1), IRValue::vreg(0)));
    body->emit(IRInstruction::jmp(".head"));
    f.add_block(".exit")->emit(IRInstruction::ret(IRValue::vreg(1)));
    CHECK(verify_function(f).ok());
}

TEST(ir_verify_wrong_operand_count_is_rejected) {
    IRFunction f("arity");
    auto* b = f.add_block(".entry");
    IRInstruction bad = IRInstruction::binary(IROpcode::ADD, 1, IRValue::imm_i64(1), IRValue::imm_i64(2));
    bad.operands.pop_back();
    b->emit(bad);
    b->emit(IRInstruction::ret_void());
    auto result = verify_function(f);
    CHECK(has_error(result, "operand count"));
}

TEST(ir_verify_terminator_must_not_define_a_result) {
    IRFunction f("term");
    auto* b = f.add_block(".entry");
    IRInstruction bad = IRInstruction::jmp(".entry");
    bad.dst = 1;
    b->emit(bad);
    auto result = verify_function(f);
    CHECK(has_error(result, "terminator must not define a result"));
}

TEST(ir_verify_br_without_condition_is_rejected) {
    IRFunction f("brcond");
    auto* b = f.add_block(".entry");
    IRInstruction bad = IRInstruction::br(IRValue::imm_i64(1), ".a", ".b");
    bad.operands.clear();
    b->emit(bad);
    f.add_block(".a")->emit(IRInstruction::ret_void());
    f.add_block(".b")->emit(IRInstruction::ret_void());
    auto result = verify_function(f);
    CHECK(has_error(result, "operand count"));
}

TEST(ir_verify_void_call_needs_no_destination) {
    IRFunction f("voidcall");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "do_nothing", {}));
    b->emit(IRInstruction::ret_void());
    CHECK(verify_function(f).ok());
}

TEST(ir_verify_indirect_call_target_must_be_a_vreg) {
    IRFunction f("indirect");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::call_indirect(1, IRValue::imm_i64(0), {}));
    b->emit(IRInstruction::ret_void());
    auto result = verify_function(f);
    CHECK(has_error(result, "expected a virtual register"));
}

TEST(ir_verify_store_param_operands_must_be_immediates) {
    IRFunction f("param");
    auto* b = f.add_block(".entry");
    IRInstruction bad = IRInstruction::store_param(0, 0, -8, IRWidth::I64);
    bad.operands[0] = IRValue::vreg(1);
    b->emit(bad);
    b->emit(IRInstruction::ret_void());
    auto result = verify_function(f);
    CHECK(has_error(result, "expected an immediate"));
}

TEST(ir_verify_inconsistent_vreg_width_is_rejected) {
    IRFunction f("width");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 5));
    b->emit(IRInstruction::copy(2, IRValue::vreg(1, IRWidth::I32)));
    b->emit(IRInstruction::copy(3, IRValue::vreg(1, IRWidth::I64)));
    b->emit(IRInstruction::ret_void());
    auto result = verify_function(f);
    CHECK(has_error(result, "inconsistent width"));
}

TEST(ir_verify_dead_code_after_terminator_is_allowed) {
    IRFunction f("dead");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 5));
    b->emit(IRInstruction::load_i64(2, 6));
    b->emit(IRInstruction::ret(IRValue::vreg(1)));
    b->emit(IRInstruction::binary(IROpcode::ADD, 3, IRValue::vreg(2), IRValue::vreg(1)));
    b->emit(IRInstruction::ret(IRValue::vreg(3)));
    CHECK(verify_function(f).ok());
}

TEST(ir_verify_module_reports_function_name) {
    IRModule m;
    auto good = std::make_unique<IRFunction>("good");
    good->add_block(".entry")->emit(IRInstruction::ret_void());
    auto bad = std::make_unique<IRFunction>("bad");
    bad->add_block(".entry")->emit(IRInstruction::jmp(".missing"));
    m.add_function(std::move(good));
    m.add_function(std::move(bad));
    auto result = verify_module(m);
    CHECK(!result.ok());
    bool named = false;
    for (const auto& e : result.errors) {
        if (e.function == "bad") named = true;
    }
    CHECK(named);
}

TEST(ir_verify_function_without_blocks_is_rejected) {
    IRFunction f("empty");
    auto result = verify_function(f);
    CHECK(has_error(result, "no blocks"));
}
