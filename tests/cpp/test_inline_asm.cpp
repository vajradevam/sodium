#include "test_framework.hpp"
#include "ir/inline.hpp"
#include "backend/peephole.hpp"
#include "ir/liveness.hpp"
#include "ir/linear_scan.hpp"
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

TEST(inline_asm_value_live_across_it_uses_an_unclobbered_register) {
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    const int rax = tri.by_name("rax");
    CHECK(rax >= 0);
    IRFunction f("asmalloc");
    auto* b = f.add_block(".entry");
    b->emit(IRInstruction::load_i64(0, 7));
    b->emit(IRInstruction::asm_insn("nop", {rax}));
    b->emit(IRInstruction::ret(IRValue::vreg(0)));

    const auto ranges = compute_live_ranges(f);
    const LiveRange* range = nullptr;
    for (const auto& r : ranges) {
        if (r.vreg == 0) range = &r;
    }
    CHECK(range != nullptr);
    if (range) {
        CHECK_EQ(range->clobbered_pregs.count(rax), size_t{1});
        CHECK(!range->spans_call);  // X5b: exact clobbers, not a call barrier
    }

    LinearScanAllocator allocator(tri);
    const RegisterAllocation allocation = allocator.allocate(f);
    CHECK(allocation.preg_of(0) != rax);
    CHECK(!allocation.is_spilled(0));
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

TEST(inline_asm_markers_protect_user_lines_from_peephole) {
    const std::string text =
        "    .Lmain_sodium_asm_0_begin:\n"
        "    mov rax, rax\n"
        "    .Lmain_sodium_asm_0_end:\n"
        "    mov rbx, rbx\n"
        "    ret\n";
    PeepholeStats stats;
    const std::string out = peephole_optimize(text, "x86_64", &stats);
    CHECK_EQ(stats.self_moves_removed, size_t{1});  // only the compiler's mov
    CHECK(out.find("mov rax, rax") != std::string::npos);
    CHECK(out.find("mov rbx, rbx") == std::string::npos);
}
