#pragma once

#include "block.hpp"
#include <string>
#include <vector>
#include <cstdint>

/// An IR function: a collection of basic blocks forming a CFG.
struct IRFunction {
    std::string name;
    std::vector<IRBlock> blocks;
    uint32_t next_vreg = 0;

    /// Number of stack slots needed (for locals, spills).
    uint32_t stack_slots = 0;

    IRFunction() = default;
    explicit IRFunction(const std::string& n) : name(n) {}

    /// Allocate a new virtual register.
    uint32_t new_vreg() { return next_vreg++; }

    /// Get or create a block by name.
    IRBlock* get_block(const std::string& label) {
        for (auto& b : blocks) {
            if (b.label == label) return &b;
        }
        return nullptr;
    }

    /// Add a block (must have unique label).
    IRBlock* add_block(const std::string& label) {
        blocks.emplace_back(label);
        return &blocks.back();
    }

    /// Entry block is always the first block.
    IRBlock* entry_block() {
        if (blocks.empty()) add_block(".entry");
        return &blocks[0];
    }

    /// Dump the function.
    std::string to_string() const;
};
