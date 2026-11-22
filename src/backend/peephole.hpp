#pragma once

#include <cstddef>
#include <string>
#include <vector>

/// Counts what the peephole pass removed.
struct PeepholeStats {
    size_t self_moves_removed = 0;
    size_t dead_moves_removed = 0;
};

namespace peephole_detail {

inline std::string trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const size_t last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

struct Move {
    bool valid = false;
    std::string dst;
    std::string src;
};

/// Parse `mnemonic dst, src` when both operands are plain registers or
/// immediates. Memory operands and comment characters are rejected so the
/// rules cannot mistake a load/store for a register move.
inline Move parse_move(const std::string& line, const std::string& mnemonic) {
    const std::string text = trim(line);
    if (!starts_with(text, mnemonic)) return {};
    const size_t comma = text.find(',', mnemonic.size());
    if (comma == std::string::npos) return {};
    Move result;
    result.dst = trim(text.substr(mnemonic.size(), comma - mnemonic.size()));
    result.src = trim(text.substr(comma + 1));
    if (result.dst.empty() || result.src.empty()) return {};
    if (result.dst.find('[') != std::string::npos ||
        result.src.find('[') != std::string::npos) return {};
    if (result.src.find('#') != std::string::npos ||
        result.src.find(';') != std::string::npos) return {};
    result.valid = true;
    return result;
}

} // namespace peephole_detail

/// Apply conservative, target-specific peephole rules to emitted assembly
/// (D-030). Each segment between labels/directives is processed independently,
/// so no rule can cross a control-flow boundary. `mov` does not affect flags,
/// and the no-op rules affect neither flags nor memory.
inline std::string peephole_optimize(const std::string& asm_text,
                                     const std::string& target_name,
                                     PeepholeStats* stats = nullptr) {
    using namespace peephole_detail;
    PeepholeStats local;
    PeepholeStats& st = stats ? *stats : local;
    st = PeepholeStats{};

    const bool x86 = target_name == "x86_64";
    const bool riscv = target_name == "riscv64";
    if (!x86 && !riscv) return asm_text;

    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < asm_text.size()) {
        const size_t newline = asm_text.find('\n', pos);
        if (newline == std::string::npos) {
            lines.push_back(asm_text.substr(pos));
            break;
        }
        lines.push_back(asm_text.substr(pos, newline - pos));
        pos = newline + 1;
    }

    const auto is_barrier = [&](const std::string& line) {
        const std::string text = trim(line);
        if (text.empty()) return false;
        if (text.back() == ':' || text[0] == '.') return true;
        if (x86 && (starts_with(text, "global ") || starts_with(text, "extern ") ||
                    starts_with(text, "section ") || starts_with(text, "bits "))) return true;
        return false;
    };

    // Pass 1: self-moves and arithmetic no-ops.
    for (std::string& line : lines) {
        if (is_barrier(line)) continue;
        if (x86) {
            const Move move = parse_move(line, "mov ");
            if (move.valid && move.dst == move.src) {
                line.clear();
                ++st.self_moves_removed;
            }
        } else {
            const Move move = parse_move(line, "mv ");
            if (move.valid && move.dst == move.src) {
                line.clear();
                ++st.self_moves_removed;
                continue;
            }
            const std::string text = trim(line);
            if (starts_with(text, "addi ")) {
                const size_t comma1 = text.find(',', 5);
                const size_t comma2 = comma1 == std::string::npos
                    ? std::string::npos : text.find(',', comma1 + 1);
                if (comma1 != std::string::npos && comma2 != std::string::npos) {
                    const std::string rd = trim(text.substr(5, comma1 - 5));
                    const std::string rs1 = trim(text.substr(comma1 + 1, comma2 - comma1 - 1));
                    const std::string imm = trim(text.substr(comma2 + 1));
                    if (!rd.empty() && rd == rs1 && imm == "0") {
                        line.clear();
                        ++st.self_moves_removed;
                    }
                }
            }
        }
    }

    // Pass 2: two adjacent moves writing the same register — the first value
    // has no reader between them, and neither move touches condition flags.
    const std::string mnemonic = x86 ? "mov " : "mv ";
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        if (lines[i].empty() || lines[i + 1].empty()) continue;
        if (is_barrier(lines[i]) || is_barrier(lines[i + 1])) continue;
        const Move first = parse_move(lines[i], mnemonic);
        const Move second = parse_move(lines[i + 1], mnemonic);
        if (first.valid && second.valid && first.dst == second.dst) {
            lines[i].clear();
            ++st.dead_moves_removed;
        }
    }

    std::string out;
    for (const std::string& line : lines) {
        if (line.empty()) continue;
        out += line;
        out += '\n';
    }
    return out;
}
