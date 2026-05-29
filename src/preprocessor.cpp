#include "preprocessor.hpp"

#include <fstream>
#include <filesystem>
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

// Recursive worker.
// visited: set of already-processed file paths (for #pragma once).
// all_included: accumulates all resolved paths.
static std::string preprocess_impl(
    const std::string& source_text,
    const std::string& filename,
    const std::vector<std::string>& include_dirs,
    const std::string& system_include_dir,
    std::unordered_set<std::string>& visited,
    std::vector<std::string>& all_included
) {
    std::ostringstream out;
    std::istringstream in(source_text);
    std::string line;
    size_t input_line_num = 0;
    bool pragma_once = false;

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

        // Recursively process the included file
        std::string processed = preprocess_impl(inc_text, resolved, include_dirs, system_include_dir, visited, all_included);

        // Emit #line marker pointing back to the parent file (line after the include)
        out << "# 1 \"" << resolved << "\"\n";
        out << processed;
        out << "# " << (input_line_num + 1) << " \"" << filename << "\"\n";
        return true;
    };

    while (std::getline(in, line)) {
        input_line_num++;
        std::string trimmed = trim(line);

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

        // Regular line — pass through
        out << line << "\n";
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
    std::string expanded = preprocess_impl(
        source_text, source_filename,
        include_dirs, system_include_dir,
        visited, result.included_files
    );
    result.expanded_source = std::move(expanded);
    return result;
}
