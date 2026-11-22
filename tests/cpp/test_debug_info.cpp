#include "test_framework.hpp"
#include "ir/emitter.hpp"
#include "backend/x86_64/backend.hpp"
#include "backend/riscv64/backend.hpp"
#include "ir/target_regs.hpp"

#include <sstream>
#include <string>

namespace {

std::string emit_debug(IRFunction& func, bool riscv) {
    std::ostringstream out;
    const TargetRegisterInfo tri = riscv ? TargetRegisterInfo::riscv64_lp64()
                                         : TargetRegisterInfo::x86_64_systemv();
    if (riscv) {
        RISCV64Backend backend(out);
        IREmitter emitter(backend, tri);
        emitter.set_debug_info(true);
        emitter.emit_function(func);
    } else {
        X8664Backend backend(out);
        IREmitter emitter(backend, tri);
        emitter.set_debug_info(true);
        emitter.emit_function(func);
    }
    return out.str();
}

} // namespace

TEST(debug_info_x86_emits_nasm_line_directive) {
    IRFunction f("dbg");
    auto* b = f.add_block(".entry");
    IRInstruction insn = IRInstruction::nop();
    insn.src_line = 42;
    insn.src_file = "/src/demo.cyan";
    b->emit(insn);
    b->emit(IRInstruction::ret_void());
    const std::string out = emit_debug(f, false);
    CHECK(out.find("%line 42+0 \"/src/demo.cyan\"") != std::string::npos);
}

TEST(debug_info_riscv_emits_file_and_loc) {
    IRFunction f("dbg");
    auto* b = f.add_block(".entry");
    IRInstruction insn = IRInstruction::nop();
    insn.src_line = 42;
    insn.src_file = "/src/demo.cyan";
    b->emit(insn);
    b->emit(IRInstruction::ret_void());
    const std::string out = emit_debug(f, true);
    CHECK(out.find(".file 1 \"/src/demo.cyan\"") != std::string::npos);
    CHECK(out.find(".loc 1 42 0") != std::string::npos);
}

TEST(debug_info_only_emits_markers_on_line_changes) {
    IRFunction f("dbg");
    auto* b = f.add_block(".entry");
    for (int i = 0; i < 3; ++i) {
        IRInstruction insn = IRInstruction::nop();
        insn.src_line = 7;
        insn.src_file = "a.cyan";
        b->emit(insn);
    }
    const std::string out = emit_debug(f, false);
    size_t count = 0;
    size_t pos = 0;
    while ((pos = out.find("%line 7+0", pos)) != std::string::npos) {
        ++count;
        pos += 4;
    }
    CHECK_EQ(count, size_t{1});
}

TEST(debug_info_disabled_emits_no_markers) {
    IRFunction f("dbg");
    auto* b = f.add_block(".entry");
    IRInstruction insn = IRInstruction::nop();
    insn.src_line = 42;
    insn.src_file = "a.cyan";
    b->emit(insn);
    b->emit(IRInstruction::ret_void());

    std::ostringstream out;
    X8664Backend backend(out);
    IREmitter emitter(backend, TargetRegisterInfo::x86_64_systemv());
    emitter.emit_function(f);
    CHECK(out.str().find("%line") == std::string::npos);
}
