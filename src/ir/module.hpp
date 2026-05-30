#pragma once

#include "function.hpp"
#include <string>
#include <vector>
#include <memory>
#include <memory>
#include <cstdint>

/// An IR module: the top-level container for all compiled code.
/// Holds functions, global variable declarations, and string literals.
struct IRModule {
    std::vector<std::unique_ptr<IRFunction>> functions;

    /// Global data entries: name → initializer value (as a string).
    struct GlobalData {
        std::string name;
        std::string value;  // e.g., "42" or "0"
    };
    std::vector<GlobalData> data_entries;

    /// BSS entries: name → size in qwords.
    struct BSSEntry {
        std::string name;
        size_t size = 1;  // in qwords
    };
    std::vector<BSSEntry> bss_entries;

    /// String literals: label → content.
    struct StringLit {
        std::string label;
        std::string value;
    };
    std::vector<StringLit> strings;

    /// Global variable initializers (code to run in _start before main).
    /// Currently, the name is stored and the Generator emits the initializer
    /// expression directly. Future work: store the IR initializer here.
    struct GlobalInit {
        std::string name;
    };
    std::vector<GlobalInit> global_inits;

    // ---- convenience ----

    void add_function(std::unique_ptr<IRFunction> func) {
        functions.push_back(std::move(func));
    }

    void add_data(const std::string& name, const std::string& value) {
        data_entries.push_back({name, value});
    }

    void add_bss(const std::string& name, size_t size = 1) {
        bss_entries.push_back({name, size});
    }

    void add_string(const std::string& label, const std::string& value) {
        strings.push_back({label, value});
    }

    /// Dump the module.
    std::string to_string() const;
};
