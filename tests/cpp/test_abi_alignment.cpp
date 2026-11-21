#include "test_framework.hpp"
#include "ir/emitter.hpp"
#include "ir/rewriter.hpp"
#include "backend/x86_64/backend.hpp"
#include "ir/target_regs.hpp"

#include <sstream>
#include <string>
#include <vector>

static std::string emit_rewritten(IRFunction& func, int nargs) {
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    RegisterAllocation alloc;
    // Map every vreg to a caller-save physical register so the rewriter can
    // turn stack-argument operands into register names. None are callee-save.
    const int regs[] = {2, 3, 4, 5, 8, 9, 10, 0, 2, 3, 4};
    for (int i = 0; i < nargs; ++i) {
        RegAllocEntry entry;
        entry.kind = RegAllocEntry::PHYSICAL;
        entry.preg = regs[i % (sizeof(regs) / sizeof(regs[0]))];
        alloc.entries[static_cast<uint32_t>(i)] = entry;
    }
    IRRewriter rewriter(tri, alloc, func);
    rewriter.rewrite();

    std::ostringstream out;
    X8664Backend backend(out);
    IREmitter emitter(backend, tri);
    emitter.emit_function(func);
    (void)nargs;
    return out.str();
}

// System V: on entry rsp % 16 == 8, and push rbp in the prologue makes it 0.
// Each later push toggles parity, so a call site is aligned when the total
// number of pushes (including the prologue's) is odd.
static bool aligned_at_call(const std::string& asm_text, int* pushes_out = nullptr) {
    int pushes = 0;
    std::istringstream in(asm_text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("push ") != std::string::npos) ++pushes;
        if (line.find("call foo") != std::string::npos) {
            if (pushes_out) *pushes_out = pushes;
            return pushes % 2 == 1;
        }
    }
    if (pushes_out) *pushes_out = pushes;
    return false;
}

static IRFunction call_with(int nargs) {
    IRFunction func("aligned");
    auto* entry = func.add_block(".entry");
    std::vector<IRValue> args;
    for (int i = 0; i < nargs; ++i) args.push_back(IRValue::vreg(static_cast<uint32_t>(i)));
    entry->emit(IRInstruction::call(IRInstruction::NONE_VREG, "foo", args));
    entry->emit(IRInstruction::ret_void());
    return func;
}

TEST(abi_alignment_even_stack_args) {
    IRFunction f = call_with(8); // 2 stack args on x86_64
    int pushes = 0;
    CHECK(aligned_at_call(emit_rewritten(f, 8), &pushes));
    CHECK_EQ(pushes, 3); // rbp + 2 stack arguments
}

TEST(abi_alignment_odd_stack_args) {
    IRFunction f = call_with(7); // 1 stack arg -> padding needed
    int pushes = 0;
    CHECK(aligned_at_call(emit_rewritten(f, 7), &pushes));
    CHECK_EQ(pushes, 3); // rbp + 1 argument + 1 padding
}

TEST(abi_alignment_register_only_call) {
    IRFunction f = call_with(6);
    int pushes = 0;
    CHECK(aligned_at_call(emit_rewritten(f, 6), &pushes));
    CHECK_EQ(pushes, 1); // prologue's push rbp only
}

TEST(abi_alignment_many_stack_args) {
    IRFunction f = call_with(11);
    int pushes = 0;
    CHECK(aligned_at_call(emit_rewritten(f, 11), &pushes));
}
