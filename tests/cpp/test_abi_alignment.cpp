#include "test_framework.hpp"
#include "ir/emitter.hpp"
#include "backend/x86_64/backend.hpp"
#include "ir/target_regs.hpp"

#include <sstream>
#include <string>
#include <vector>

static std::string emit_function(IRFunction& func) {
    std::ostringstream out;
    X8664Backend backend(out);
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    IREmitter emitter(backend, tri);
    emitter.emit_function(func);
    return out.str();
}

// System V: on entry to main, rsp % 16 == 8; every push subtracts 8.
// A call site is correctly aligned when (8 - pushes*8) % 16 == 0.
static bool aligned_at_call(const std::string& asm_text, int* pushes_out = nullptr) {
    int pushes = 0;
    std::istringstream in(asm_text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("push ") != std::string::npos) ++pushes;
        if (line.find("call foo") != std::string::npos) {
            if (pushes_out) *pushes_out = pushes;
            return (8 - pushes * 8) % 16 == 0;
        }
    }
    if (pushes_out) *pushes_out = pushes;
    return false;
}

static IRFunction call_with(int nargs, int callee_save_pushes) {
    IRFunction func("aligned");
    auto* entry = func.add_block(".entry");
    for (int i = 0; i < callee_save_pushes; ++i) {
        IRInstruction push;
        push.op = IROpcode::PUSH;
        push.operands.push_back(IRValue::vreg(20 + static_cast<uint32_t>(i)));
        entry->emit(push);
    }
    std::vector<IRValue> args;
    for (int i = 0; i < nargs; ++i) args.push_back(IRValue::vreg(static_cast<uint32_t>(i)));
    entry->emit(IRInstruction::call(IRInstruction::NONE_VREG, "foo", args));
    entry->emit(IRInstruction::ret_void());
    return func;
}

TEST(abi_alignment_even_stack_args_without_callee_saves) {
    IRFunction f = call_with(8, 0); // 2 stack args on x86_64
    int pushes = 0;
    CHECK(aligned_at_call(emit_function(f), &pushes));
    CHECK_EQ(pushes, 3); // rbp + 2 args, no padding needed
}

TEST(abi_alignment_even_stack_args_with_callee_save) {
    IRFunction f = call_with(8, 1); // 2 stack args + 1 callee-save push
    int pushes = 0;
    CHECK(aligned_at_call(emit_function(f), &pushes));
    CHECK_EQ(pushes, 5); // rbp + 1 callee-save + 2 args + 1 padding
}

TEST(abi_alignment_odd_stack_args_without_callee_saves) {
    IRFunction f = call_with(7, 0); // 1 stack arg
    int pushes = 0;
    CHECK(aligned_at_call(emit_function(f), &pushes));
    CHECK_EQ(pushes, 3); // rbp + 1 arg + 1 padding
}

TEST(abi_alignment_register_only_call) {
    IRFunction f = call_with(6, 0);
    int pushes = 0;
    CHECK(aligned_at_call(emit_function(f), &pushes));
    CHECK_EQ(pushes, 1); // rbp only
}
