#include "test_framework.hpp"
#include "ir/emitter.hpp"
#include "ir/rewriter.hpp"
#include "backend/x86_64/backend.hpp"
#include "ir/target_regs.hpp"

#include <map>
#include <sstream>
#include <string>
#include <vector>

// Two-word results (RET2 / CALL with dst2, D-012) with every combination of
// register homes: the values must reach rax:rdx (or their homes) even when a
// home is the other word's register or a spill slot.

namespace {

struct Home {
    bool spilled;
    int where;   // physical register number, or spill slot index
};

Home reg(int preg) { return {false, preg}; }
Home slot(int index) { return {true, index}; }

// x86_64 numbering: rax=0 rcx=2 rdx=3 rsi=4 rdi=5 r11 is the scratch register.
constexpr int RAX = 0;
constexpr int RCX = 2;
constexpr int RDX = 3;

std::vector<std::string> emit_lines(IRFunction& func, const std::map<uint32_t, Home>& homes) {
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();
    RegisterAllocation alloc;
    for (const auto& [vreg, home] : homes) {
        RegAllocEntry entry;
        if (home.spilled) {
            entry.kind = RegAllocEntry::SPILLED;
            entry.spill_slot = home.where;
            alloc.total_stack_slots = std::max<uint32_t>(alloc.total_stack_slots,
                                                         static_cast<uint32_t>(home.where) + 1);
        } else {
            entry.kind = RegAllocEntry::PHYSICAL;
            entry.preg = home.where;
        }
        alloc.entries[vreg] = entry;
    }
    IRRewriter rewriter(tri, alloc, func);
    rewriter.rewrite();

    std::ostringstream out;
    X8664Backend backend(out);
    IREmitter emitter(backend, tri);
    emitter.emit_function(func);

    std::vector<std::string> lines;
    std::istringstream in(out.str());
    std::string line;
    while (std::getline(in, line)) {
        size_t start = line.find_first_not_of(' ');
        if (start != std::string::npos) lines.push_back(line.substr(start));
    }
    return lines;
}

/// Lines strictly between the marker line and the final `ret`.
std::vector<std::string> after(const std::vector<std::string>& lines, const std::string& marker) {
    std::vector<std::string> out;
    bool seen = false;
    for (const auto& l : lines) {
        if (seen) out.push_back(l);
        if (l == marker) seen = true;
    }
    return out;
}

bool contains_in_order(const std::vector<std::string>& lines, const std::vector<std::string>& want) {
    size_t at = 0;
    for (const auto& l : lines) {
        if (at < want.size() && l == want[at]) ++at;
    }
    return at == want.size();
}

IRFunction ret2_function(IRValue lo, IRValue hi) {
    IRFunction func("r2");
    auto* entry = func.add_block(".entry");
    entry->emit(IRInstruction::ret2(lo, hi));
    func.next_vreg = 8;
    return func;
}

IRFunction call2_function(uint32_t dst, uint32_t dst2) {
    IRFunction func("c2");
    auto* entry = func.add_block(".entry");
    auto call = IRInstruction::call(dst, "callee", {});
    call.dst2 = dst2;
    entry->emit(call);
    entry->emit(IRInstruction::ret_void());
    func.next_vreg = 8;
    return func;
}

} // namespace

TEST(ret2_plain_registers) {
    IRFunction f = ret2_function(IRValue::vreg(0), IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, reg(4)}});
    CHECK(contains_in_order(lines, {"mov r11, rcx", "mov rdx, rsi", "mov rax, r11", "ret"}));
}

TEST(ret2_operands_swapped_between_return_registers) {
    // lo lives in rdx, hi lives in rax: a naive pair of moves would lose one.
    IRFunction f = ret2_function(IRValue::vreg(0), IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, reg(RDX)}, {1, reg(RAX)}});
    CHECK(contains_in_order(lines, {"mov r11, rdx", "mov rdx, rax", "mov rax, r11", "ret"}));
}

TEST(ret2_both_spilled) {
    IRFunction f = ret2_function(IRValue::vreg(0), IRValue::vreg(1));
    auto lines = emit_lines(f, {{0, slot(0)}, {1, slot(1)}});
    // Slot 1's value goes straight to rdx, slot 0's value is parked in r11.
    int loads = 0;
    for (const auto& l : lines) if (l.rfind("mov r11, [rbp", 0) == 0 || l.rfind("mov rdx, [rbp", 0) == 0) ++loads;
    CHECK_EQ(loads, 2);
    CHECK(contains_in_order(lines, {"mov rax, r11", "ret"}));
}

TEST(ret2_immediates) {
    IRFunction f = ret2_function(IRValue::imm_i64(7), IRValue::imm_i64(9));
    auto lines = emit_lines(f, {});
    CHECK(contains_in_order(lines, {"mov rdx, 9", "mov rax, 7", "ret"}));
}

TEST(call2_results_into_plain_registers) {
    IRFunction f = call2_function(0, 1);
    auto lines = emit_lines(f, {{0, reg(RCX)}, {1, reg(4)}});
    CHECK(contains_in_order(after(lines, "call callee"), {"mov r11, rdx", "mov rcx, rax", "mov rsi, r11"}));
}

TEST(call2_results_in_swapped_return_registers) {
    // dst lives in rdx and dst2 lives in rax: the second word must be saved first.
    IRFunction f = call2_function(0, 1);
    auto lines = emit_lines(f, {{0, reg(RDX)}, {1, reg(RAX)}});
    CHECK(contains_in_order(after(lines, "call callee"),
                            {"mov r11, rdx", "mov rdx, rax", "mov rax, r11"}));
}

TEST(call2_results_both_spilled) {
    IRFunction f = call2_function(0, 1);
    auto lines = emit_lines(f, {{0, slot(0)}, {1, slot(1)}});
    auto tail = after(lines, "call callee");
    CHECK(contains_in_order(tail, {"mov r11, rdx"}));
    int stores = 0;
    for (const auto& l : tail) {
        if (l.rfind("mov [rbp", 0) == 0) ++stores;
    }
    CHECK_EQ(stores, 2);
}
