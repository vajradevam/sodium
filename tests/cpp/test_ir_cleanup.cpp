#include "test_framework.hpp"
#include "ir/cleanup.hpp"
#include "ir/verify.hpp"

#include <ostream>

static std::ostream& operator<<(std::ostream& os, IROpcode op) {
    return os << ir_opcode_name(op);
}

static const IRBlock* block_named(const IRFunction& f, const std::string& label) {
    for (const auto& b : f.blocks) if (b.label == label) return &b;
    return nullptr;
}

TEST(ir_cleanup_removes_unused_pure_instruction) {
    IRFunction f("dead");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 5));
    b->emit(IRInstruction::load_i64(2, 7));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    auto stats = cleanup_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns.size(), size_t{2});
    CHECK_EQ(insns[0].op, IROpcode::LOAD_I64);
    CHECK_EQ(insns[0].dst, 2u);
    CHECK(stats.removed_instructions >= 1);
    CHECK(verify_function(f).ok());
}

TEST(ir_cleanup_keeps_side_effects_without_uses) {
    IRFunction f("sidefx");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::call(IRInstruction::NONE_VREG, "f", {}));
    b->emit(IRInstruction::store(IRValue::imm_i64(0), 0, IRValue::imm_i64(1)));
    b->emit(IRInstruction::ret_void());
    auto stats = cleanup_function(f);
    CHECK_EQ(f.blocks[0].instructions.size(), size_t{3});
    CHECK_EQ(stats.removed_instructions, size_t{0});
}

TEST(ir_cleanup_removes_instructions_after_terminator) {
    IRFunction f("afterret");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 5));
    b->emit(IRInstruction::ret(IRValue::vreg(1)));
    b->emit(IRInstruction::load_i64(2, 6));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    cleanup_function(f);
    CHECK_EQ(f.blocks[0].instructions.size(), size_t{2});
    CHECK_EQ(f.blocks[0].instructions[1].op, IROpcode::RET);
}

TEST(ir_cleanup_removes_unreachable_blocks) {
    IRFunction f("unreachable");
    f.add_block(".entry")->emit(IRInstruction::ret_void());
    f.add_block(".dead")->emit(IRInstruction::load_i64(1, 1));
    auto stats = cleanup_function(f);
    CHECK_EQ(f.blocks.size(), size_t{1});
    CHECK(block_named(f, ".dead") == nullptr);
    CHECK_EQ(stats.removed_blocks, size_t{1});
    CHECK(verify_function(f).ok());
}

TEST(ir_cleanup_keeps_loop_blocks_reachable) {
    IRFunction f("loop");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 1));
    e->emit(IRInstruction::jmp(".head"));
    auto* h = f.add_block(".head");
    h->emit(IRInstruction::br(IRValue::vreg(0), ".body", ".exit"));
    auto* body = f.add_block(".body");
    body->emit(IRInstruction::load_i64(9, 42));
    body->emit(IRInstruction::store(IRValue::imm_i64(0), 0, IRValue::vreg(9)));
    body->emit(IRInstruction::jmp(".head"));
    f.add_block(".exit")->emit(IRInstruction::ret_void());
    auto stats = cleanup_function(f);
    CHECK_EQ(f.blocks.size(), size_t{4});
    CHECK_EQ(stats.removed_blocks, size_t{0});
}

TEST(ir_cleanup_threads_jump_through_empty_block) {
    IRFunction f("emptymid");
    f.add_block(".entry")->emit(IRInstruction::jmp(".mid"));
    f.add_block(".mid");
    f.add_block(".end")->emit(IRInstruction::ret_void());
    auto stats = cleanup_function(f);
    CHECK(block_named(f, ".mid") == nullptr);
    CHECK_EQ(f.blocks[0].instructions[0].op, IROpcode::JMP);
    CHECK_EQ(f.blocks[0].instructions[0].label_true, std::string(".end"));
    CHECK(stats.threaded_jumps >= 1);
    CHECK(verify_function(f).ok());
}

TEST(ir_cleanup_threads_jump_through_jmp_only_block) {
    IRFunction f("jmpmid");
    f.add_block(".entry")->emit(IRInstruction::jmp(".mid"));
    f.add_block(".mid")->emit(IRInstruction::jmp(".end"));
    f.add_block(".end")->emit(IRInstruction::ret_void());
    cleanup_function(f);
    CHECK_EQ(f.blocks.size(), size_t{2});
    CHECK_EQ(f.blocks[0].instructions[0].label_true, std::string(".end"));
}

TEST(ir_cleanup_redirects_br_targets) {
    IRFunction f("brtarget");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 1));
    e->emit(IRInstruction::br(IRValue::vreg(0), ".mid", ".alt"));
    f.add_block(".mid");
    f.add_block(".end")->emit(IRInstruction::ret_void());
    f.add_block(".alt")->emit(IRInstruction::ret_void());
    cleanup_function(f);
    auto& insns = f.blocks[0].instructions;
    CHECK_EQ(insns[1].op, IROpcode::BR);
    CHECK_EQ(insns[1].label_true, std::string(".end"));
    CHECK_EQ(insns[1].label_false, std::string(".alt"));
    CHECK(verify_function(f).ok());
}

TEST(ir_cleanup_never_removes_entry_block) {
    IRFunction f("onlyentry");
    f.add_block(".entry");
    cleanup_function(f);
    CHECK_EQ(f.blocks.size(), size_t{1});
    CHECK_EQ(f.blocks[0].label, std::string(".entry"));
}

TEST(ir_cleanup_keeps_defs_of_used_multi_def_vregs) {
    IRFunction f("phi");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(2, 0));  // dominating default def of the shared result
    e->emit(IRInstruction::load_i64(0, 1));
    e->emit(IRInstruction::br(IRValue::vreg(0), ".t", ".f"));
    auto* t = f.add_block(".t");
    t->emit(IRInstruction::load_i64(1, 10));
    t->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    t->emit(IRInstruction::jmp(".join"));
    auto* fa = f.add_block(".f");
    fa->emit(IRInstruction::load_i64(3, 20));
    fa->emit(IRInstruction::copy(2, IRValue::vreg(3)));
    fa->emit(IRInstruction::jmp(".join"));
    f.add_block(".join")->emit(IRInstruction::ret(IRValue::vreg(2)));
    cleanup_function(f);
    CHECK(verify_function(f).ok());
    CHECK_EQ(block_named(f, ".t")->instructions.size(), size_t{3});
    CHECK_EQ(block_named(f, ".f")->instructions.size(), size_t{3});
}

TEST(ir_cleanup_removes_unused_multi_def_vregs) {
    IRFunction f("phiunused");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_i64(0, 1));
    e->emit(IRInstruction::br(IRValue::vreg(0), ".t", ".f"));
    auto* t = f.add_block(".t");
    t->emit(IRInstruction::load_i64(1, 10));
    t->emit(IRInstruction::copy(2, IRValue::vreg(1)));
    t->emit(IRInstruction::jmp(".join"));
    auto* fa = f.add_block(".f");
    fa->emit(IRInstruction::load_i64(3, 20));
    fa->emit(IRInstruction::copy(2, IRValue::vreg(3)));
    fa->emit(IRInstruction::jmp(".join"));
    f.add_block(".join")->emit(IRInstruction::ret_void());
    cleanup_function(f);
    // Only the copies are unused; their source constants are still counted as
    // used in this single pass, so each arm keeps its constant and its jmp.
    CHECK_EQ(block_named(f, ".t")->instructions.size(), size_t{2});
    CHECK_EQ(block_named(f, ".f")->instructions.size(), size_t{2});
}

TEST(ir_cleanup_preserves_fallthrough_into_jmp_only_block) {
    IRFunction f("fallthrough");
    auto* e = f.add_block(".entry");
    e->emit(IRInstruction::load_param(1, 0));
    e->emit(IRInstruction::store(IRValue::vreg(1), 0, IRValue::imm_i64(1)));  // no terminator
    f.add_block(".mid")->emit(IRInstruction::jmp(".end"));
    f.add_block(".other")->emit(IRInstruction::ret_void());
    f.add_block(".end")->emit(IRInstruction::ret_void());
    cleanup_function(f);
    CHECK_EQ(f.blocks.size(), size_t{2});
    CHECK_EQ(f.blocks[0].instructions.back().op, IROpcode::JMP);
    CHECK_EQ(f.blocks[0].instructions.back().label_true, std::string(".end"));
    CHECK(block_named(f, ".other") == nullptr);
    CHECK(verify_function(f).ok());
}

TEST(ir_cleanup_threads_a_chain_of_jumps) {
    IRFunction f("chain");
    f.add_block(".entry")->emit(IRInstruction::jmp(".a"));
    f.add_block(".a")->emit(IRInstruction::jmp(".b"));
    f.add_block(".b")->emit(IRInstruction::jmp(".end"));
    f.add_block(".end")->emit(IRInstruction::ret_void());
    auto stats = cleanup_function(f);
    CHECK_EQ(f.blocks.size(), size_t{2});
    CHECK_EQ(f.blocks[0].instructions[0].label_true, std::string(".end"));
    CHECK(stats.threaded_jumps >= 2);
    CHECK(verify_function(f).ok());
}

TEST(ir_cleanup_dump_shows_no_dead_tail) {
    IRFunction f("dump");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(1, 5));
    b->emit(IRInstruction::ret(IRValue::vreg(1)));
    b->emit(IRInstruction::load_i64(2, 6));
    b->emit(IRInstruction::ret(IRValue::vreg(2)));
    cleanup_function(f);
    const std::string dumped = f.to_string();
    CHECK(dumped.find("load_i64 6") == std::string::npos);
    CHECK(dumped.find("load_i64 5") != std::string::npos);
}
