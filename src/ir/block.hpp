#pragma once

#include "instruction.hpp"
#include <string>
#include <vector>
#include <cstdint>

/// A basic block: a linear sequence of IR instructions with a label.
struct IRBlock {
    std::string label;
    std::vector<IRInstruction> instructions;

    IRBlock() = default;
    explicit IRBlock(const std::string& lbl) : label(lbl) {}

    /// Append an instruction.
    void emit(IRInstruction insn) {
        instructions.push_back(std::move(insn));
    }

    /// Dump this block.
    std::string to_string() const;
};
