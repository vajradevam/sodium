#include "preprocessor.hpp"
#include "tokenization.hpp"

#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <sstream>

namespace fs = std::filesystem;

// ── Helpers ────────────────────────────────────────────────────────

// Trim leading/trailing whitespace.
static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

// Read a file into a string.
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Resolve an include path:
//   For #include "..." — try dir_of(parent), then include_dirs, then system.
//   For #include <...> — try include_dirs, then system.
// Returns empty string if not found.
static std::string resolve_include(
    const std::string& inc_path,
    bool is_angle,
    const std::string& parent_dir,
    const std::vector<std::string>& include_dirs,
    const std::string& system_include_dir
) {
    if (!is_angle) {
        // Try directory of the containing file first
        if (!parent_dir.empty()) {
            fs::path p = fs::path(parent_dir) / inc_path;
            if (fs::exists(p)) return p.string();
        }
    }
    for (const auto& dir : include_dirs) {
        fs::path p = fs::path(dir) / inc_path;
        if (fs::exists(p)) return p.string();
    }
    if (!system_include_dir.empty()) {
        fs::path p = fs::path(system_include_dir) / inc_path;
        if (fs::exists(p)) return p.string();
    }
    return "";
}

// ── Macros and conditionals (X2) ──────────────────────────────────

struct MacroDef {
    std::string replacement;
};
using MacroTable = std::unordered_map<std::string, MacroDef>;

static bool ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// Expand object-like macros in a line. Identifiers inside string or character
/// literals are left alone (D-033). Expansion is recursive with a depth cap so
/// a macro referring to itself cannot hang the compiler.
static std::string expand_macros(const std::string& line, const MacroTable& macros,
                                 int depth = 0) {
    if (depth > 8) return line;
    std::string out;
    out.reserve(line.size());
    char quote = 0;
    for (size_t i = 0; i < line.size();) {
        const char c = line[i];
        if (quote != 0) {
            out += c;
            if (c == '\\' && i + 1 < line.size()) {
                out += line[i + 1];
                i += 2;
                continue;
            }
            if (c == quote) quote = 0;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            out += c;
            ++i;
            continue;
        }
        if (ident_start(c)) {
            size_t end = i + 1;
            while (end < line.size() && ident_char(line[end])) ++end;
            const std::string name = line.substr(i, end - i);
            const auto it = macros.find(name);
            if (it != macros.end()) {
                out += expand_macros(it->second.replacement, macros, depth + 1);
            } else {
                out += name;
            }
            i = end;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

struct CondFrame {
    bool parent_active = true;
    bool active = true;
    bool taken = false;   // a branch in this #if group has already been selected
};

// Recursive worker.
// visited: set of already-processed file paths (for #pragma once).
// all_included: accumulates all resolved paths.
// macros: shared across included files so include guards and headers cooperate.
static std::string preprocess_impl(
    const std::string& source_text,
    const std::string& filename,
    const std::vector<std::string>& include_dirs,
    const std::string& system_include_dir,
    std::unordered_set<std::string>& visited,
    std::vector<std::string>& all_included,
    MacroTable& macros
) {
    std::ostringstream out;
    std::istringstream in(source_text);
    std::string line;
    size_t input_line_num = 0;
    bool pragma_once = false;
    std::vector<CondFrame> conditions;

    const auto active = [&]() {
        return conditions.empty() || conditions.back().active;
    };

    auto resolve_and_include = [&](const std::string& inc_path, bool is_angle) -> bool {
        fs::path parent_dir = fs::path(filename).parent_path();
        std::string resolved = resolve_include(inc_path, is_angle, parent_dir.string(), include_dirs, system_include_dir);
        if (resolved.empty()) return false;

        // Check #pragma once
        if (visited.count(resolved)) {
            // Already included via #pragma once — skip
            return true;
        }

        std::string inc_text = read_file(resolved);
        if (inc_text.empty()) {
            // File not found or empty — emit an error marker
            out << "#error \"could not read file: " << resolved << "\"\n";
            return true;
        }

        all_included.push_back(resolved);
        register_source_file(resolved, inc_text);

        // Recursively process the included file
        std::string processed = preprocess_impl(
            inc_text, resolved, include_dirs, system_include_dir,
            visited, all_included, macros);

        // Emit #line marker pointing back to the parent file (line after the include)
        out << "# 1 \"" << resolved << "\"\n";
        out << processed;
        out << "# " << (input_line_num + 1) << " \"" << filename << "\"\n";
        return true;
    };

    while (std::getline(in, line)) {
        input_line_num++;
        std::string trimmed = trim(line);

        // Conditional directives must be tracked even inside inactive branches.
        if (trimmed.rfind("#ifdef", 0) == 0 || trimmed.rfind("#ifndef", 0) == 0) {
            const bool negated = trimmed.rfind("#ifndef", 0) == 0;
            const std::string rest = trim(trimmed.substr(negated ? 7 : 6));
            const bool defined = !rest.empty() && macros.count(rest) != 0;
            const bool value = negated ? !defined : defined;
            const bool parent = active();
            conditions.push_back({parent, parent && value, parent && value});
            continue;
        }
        if (trimmed == "#else") {
            if (!conditions.empty()) {
                CondFrame& frame = conditions.back();
                frame.active = frame.parent_active && !frame.taken;
                frame.taken = frame.taken || frame.active;
            }
            continue;
        }
        if (trimmed == "#endif") {
            if (!conditions.empty()) conditions.pop_back();
            continue;
        }

        // Everything below applies only in active branches.
        if (!active()) continue;

        // ── #define NAME replacement ──────────────────────────────
        if (trimmed.rfind("#define", 0) == 0) {
            const std::string rest = trim(trimmed.substr(7));
            const size_t space = rest.find_first_of(" \t");
            if (space != std::string::npos) {
                const std::string name = rest.substr(0, space);
                if (!name.empty()) {
                    macros[name] = MacroDef{trim(rest.substr(space + 1))};
                }
            } else if (!rest.empty()) {
                macros[rest] = MacroDef{""};
            }
            continue;
        }

        // ── #pragma once ──────────────────────────────────────────
        if (trimmed == "#pragma once") {
            pragma_once = true;
            continue; // don't emit
        }

        // ── #include "..." ────────────────────────────────────────
        if (trimmed.rfind("#include", 0) == 0) {
            std::string rest = trim(trimmed.substr(8)); // after "#include"
            bool is_angle = false;
            char delim = '"';
            if (!rest.empty() && rest[0] == '<') {
                is_angle = true;
                delim = '>';
            } else if (!rest.empty() && rest[0] == '"') {
                is_angle = false;
                delim = '"';
            } else {
                out << line << "\n";
                continue;
            }
            size_t end_pos = rest.find(delim, 1);
            if (end_pos == std::string::npos) {
                out << line << "\n";
                continue;
            }
            std::string inc_path = rest.substr(1, end_pos - 1);
            if (!resolve_and_include(inc_path, is_angle)) {
                // Include not found — emit error
                out << "#error \"include file not found: " << inc_path << "\"\n";
            }
            continue;
        }

        // ── #error ────────────────────────────────────────────────
        if (trimmed.rfind("#error", 0) == 0) {
            out << line << "\n";
            continue;
        }

        // Regular line — expand macros, then pass through.
        out << expand_macros(line, macros) << "\n";
    }

    if (pragma_once) {
        visited.insert(filename);
    }

    return out.str();
}

// ── Public API ────────────────────────────────────────────────────
PreprocessedResult preprocess(
    const std::string& source_text,
    const std::string& source_filename,
    const std::vector<std::string>& include_dirs,
    const std::string& system_include_dir
) {
    PreprocessedResult result;
    std::unordered_set<std::string> visited;
    MacroTable macros;
    std::string expanded = preprocess_impl(
        source_text, source_filename,
        include_dirs, system_include_dir,
        visited, result.included_files, macros
    );
    result.expanded_source = std::move(expanded);
    return result;
}
