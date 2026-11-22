#include "test_framework.hpp"
#include "ir/inline.hpp"
#include "ir/liveness.hpp"
#include "ir/verify.hpp"
#include "emit_helpers.hpp"

#include <string>

TEST(inline_asm_ir_is_valid) {
    IRFunction f("asmfunc");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::asm_insn("nop", {}));
    b->emit(IRInstruction::ret_void());
    CHECK(verify_function(f).ok());
}

TEST(inline_asm_emits_verbatim_on_both_targets) {
    for (TestTarget target : {TestTarget::X86_64, TestTarget::RiscV64}) {
        IRFunction f("asmfunc");
        auto* b = f.add_block(".entry");
        b->emit(IRInstruction::asm_insn("nop", {}));
        b->emit(IRInstruction::ret_void());
        std::vector<std::string> lines = emit_lines(f, {}, target);
        bool found = false;
        for (const auto& line : lines) {
            if (line == "nop") found = true;
        }
        CHECK(found);
    }
}

TEST(inline_asm_value_live_across_it_is_constrained) {
    IRFunction f("asmalloc");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 7));
    b->emit(IRInstruction::asm_insn("nop", {}));
    b->emit(IRInstruction::ret(IRValue::vreg(0)));
    const auto ranges = compute_live_ranges(f);
    const LiveRange* range = nullptr;
    for (const auto& r : ranges) {
        if (r.vreg == 0) range = &r;
    }
    CHECK(range != nullptr);
    if (range) CHECK(range->spans_call);  // conservative X5a barrier
}

TEST(inline_asm_cannot_be_inlined) {
    IRFunction caller("main");
    caller.add_block(".entry")->emit(IRInstruction::call(0, "leaf", {}));
    IRFunction leaf("leaf");
    leaf.add_block(".entry")->emit(IRInstruction::asm_insn("nop", {}));
    leaf.add_block(".Lleaf_epilogue")->emit(IRInstruction::ret_void());
    std::vector<ir_inline::InlineItem> items = { {&caller, 0}, {&leaf, 0} };
    const ir_inline::InlineStats stats = ir_inline::inline_leaves(items);
    CHECK_EQ(stats.inlined, size_t{0});
}
