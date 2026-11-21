#pragma once

#include <string>

/// A source location: 1-based line and column, optional file name.
struct SourceLoc {
    size_t line = 0;
    size_t col = 0;
    std::string file;

    [[nodiscard]] std::string to_string() const {
        if (file.empty()) {
            return std::to_string(line) + ":" + std::to_string(col);
        }
        return file + ":" + std::to_string(line) + ":" + std::to_string(col);
    }
};
