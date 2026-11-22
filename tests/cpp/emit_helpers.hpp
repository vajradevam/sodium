#pragma once

#include "ir/emitter.hpp"
#include "ir/rewriter.hpp"
#include "backend/x86_64/backend.hpp"
#include "backend/riscv64/backend.hpp"
#include "ir/target_regs.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Helpers for tests that run a hand-built IR function through the rewriter and emitter with
// chosen register homes, then inspect the emitted assembly lines.

enum class TestTarget { X86_64, RiscV64 };

struct Home {
    bool spilled;
    int where;   // physical register number, or spill slot index
};

inline Home reg(int preg) { return {false, preg}; }
inline Home slot(int index) { return {true, index}; }

inline std::vector<std::string> emit_lines(IRFunction& func, const std::map<uint32_t, Home>& homes,
                                           TestTarget target = TestTarget::X86_64) {
    const TargetRegisterInfo tri = target == TestTarget::X86_64
        ? TargetRegisterInfo::x86_64_systemv() : TargetRegisterInfo::riscv64_lp64();
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
    if (target == TestTarget::X86_64) {
        X8664Backend backend(out);
        IREmitter emitter(backend, tri);
        emitter.emit_function(func);
    } else {
        RISCV64Backend backend(out);
        IREmitter emitter(backend, tri);
        emitter.emit_function(func);
    }

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
inline std::vector<std::string> after(const std::vector<std::string>& lines, const std::string& marker) {
    std::vector<std::string> out;
    bool seen = false;
    for (const auto& l : lines) {
        if (seen) out.push_back(l);
        if (l == marker) seen = true;
    }
    return out;
}

inline bool contains_in_order(const std::vector<std::string>& lines, const std::vector<std::string>& want) {
    size_t at = 0;
    for (const auto& l : lines) {
        if (at < want.size() && l == want[at]) ++at;
    }
    return at == want.size();
}

