#include "preprocessor.hpp"
#include "tokenization.hpp"

#include <fstream>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <cstring>
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
// An empty file is valid; only an unreadable one yields nullopt.
static std::optional<std::string> read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
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
    bool function_like = false;
    std::vector<std::string> params;
};
using MacroTable = std::unordered_map<std::string, MacroDef>;

static bool ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// Replace whole-identifier occurrences of `params[i]` with `values[i]` in
/// `text`, skipping string/character literals.
static std::string substitute_params(const std::string& text,
                                     const std::vector<std::string>& params,
                                     const std::vector<std::string>& values) {
    std::string out;
    out.reserve(text.size());
    char quote = 0;
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (quote != 0) {
            out += c;
            if (c == '\\' && i + 1 < text.size()) {
                out += text[i + 1];
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
            while (end < text.size() && ident_char(text[end])) ++end;
            const std::string name = text.substr(i, end - i);
            size_t replaced = params.size();
            for (size_t p = 0; p < params.size(); ++p) {
                if (params[p] == name) { replaced = p; break; }
            }
            out += replaced < params.size() ? values[replaced] : name;
            i = end;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

/// Expand macros in a line. Identifiers inside string or character literals
/// are left alone (D-033). Function-like macros are expanded only when the
/// name is followed by '('. Expansion is recursive with a depth cap so a
/// macro referring to itself cannot hang the compiler.
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
            if (it == macros.end()) {
                out += name;
                i = end;
                continue;
            }
            const MacroDef& macro = it->second;
            if (!macro.function_like) {
                out += expand_macros(macro.replacement, macros, depth + 1);
                i = end;
                continue;
            }
            // Function-like macro: require '(' after optional whitespace.
            size_t next = end;
            while (next < line.size() && std::isspace(static_cast<unsigned char>(line[next]))) ++next;
            if (next >= line.size() || line[next] != '(') {
                out += name;
                i = end;
                continue;
            }
            std::vector<std::string> args;
            size_t arg_start = next + 1;
            int parens = 1;
            size_t k = arg_start;
            for (; k < line.size() && parens > 0; ++k) {
                if (line[k] == '(') ++parens;
                else if (line[k] == ')') {
                    --parens;
                    if (parens == 0) {
                        args.push_back(trim(line.substr(arg_start, k - arg_start)));
                        break;
                    }
                } else if (line[k] == ',' && parens == 1) {
                    args.push_back(trim(line.substr(arg_start, k - arg_start)));
                    arg_start = k + 1;
                }
            }
            if (parens != 0) {  // unterminated call: leave it alone
                out += name;
                i = end;
                continue;
            }
            std::vector<std::string> values;
            values.reserve(args.size());
            for (const auto& arg : args) values.push_back(expand_macros(arg, macros, depth + 1));
            std::string substituted = substitute_params(macro.replacement, macro.params, values);
            out += expand_macros(substituted, macros, depth + 1);
            i = k + 1;  // skip past the closing ')'
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

/// Small recursive-descent evaluator for #if constant expressions (D-033).
/// Grammar: || && == != < <= > >= + - * / % ! unary, parentheses, integer
/// literals, defined(NAME), and identifiers (undefined names are 0).
class IfExpression {
public:
    IfExpression(const std::string& text, const MacroTable& macros, int depth = 0)
        : m_text(text), m_macros(macros), m_depth(depth) {}

    int64_t evaluate() {
        const int64_t value = parse_or();
        skip_space();
        return value;  // trailing text is ignored, like a tolerant C preprocessor
    }

private:
    const std::string& m_text;
    const MacroTable& m_macros;
    size_t m_pos = 0;
    int m_depth = 0;

    void skip_space() {
        while (m_pos < m_text.size() &&
               std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
    }
    bool eat(char c) {
        skip_space();
        if (m_pos < m_text.size() && m_text[m_pos] == c) { ++m_pos; return true; }
        return false;
    }
    bool eat_two(char a, char b) {
        skip_space();
        if (m_pos + 1 < m_text.size() && m_text[m_pos] == a && m_text[m_pos + 1] == b) {
            m_pos += 2;
            return true;
        }
        return false;
    }
    bool peek_word(const char* word) {
        skip_space();
        const size_t n = std::strlen(word);
        if (m_text.compare(m_pos, n, word) != 0) return false;
        return m_pos + n == m_text.size() || !ident_char(m_text[m_pos + n]);
    }
    void expect_word(const char* word) {
        if (peek_word(word)) m_pos += std::strlen(word);
    }

    int64_t parse_or() {
        int64_t value = parse_and();
        while (eat_two('|', '|')) {
            const int64_t rhs = parse_and();
            value = (value != 0 || rhs != 0) ? 1 : 0;
        }
        return value;
    }
    int64_t parse_and() {
        int64_t value = parse_eq();
        while (eat_two('&', '&')) {
            const int64_t rhs = parse_eq();
            value = (value != 0 && rhs != 0) ? 1 : 0;
        }
        return value;
    }
    int64_t parse_eq() {
        int64_t value = parse_rel();
        for (;;) {
            if (eat_two('=', '=')) value = (value == parse_rel()) ? 1 : 0;
            else if (eat_two('!', '=')) value = (value != parse_rel()) ? 1 : 0;
            else return value;
        }
    }
    int64_t parse_rel() {
        int64_t value = parse_add();
        for (;;) {
            if (eat_two('<', '=')) value = (value <= parse_add()) ? 1 : 0;
            else if (eat_two('>', '=')) value = (value >= parse_add()) ? 1 : 0;
            else if (eat('<'))       value = (value <  parse_add()) ? 1 : 0;
            else if (eat('>'))       value = (value >  parse_add()) ? 1 : 0;
            else return value;
        }
    }
    int64_t parse_add() {
        int64_t value = parse_mul();
        for (;;) {
            if (eat('+')) value += parse_mul();
            else if (eat('-')) value -= parse_mul();
            else return value;
        }
    }
    int64_t parse_mul() {
        int64_t value = parse_unary();
        for (;;) {
            if (eat('*')) value *= parse_unary();
            else if (eat('/')) {
                const int64_t rhs = parse_unary();
                value = rhs == 0 ? 0 : value / rhs;
            } else if (eat('%')) {
                const int64_t rhs = parse_unary();
                value = rhs == 0 ? 0 : value % rhs;
            } else return value;
        }
    }
    int64_t parse_unary() {
        if (eat('!')) return parse_unary() == 0 ? 1 : 0;
        if (eat('-')) return -parse_unary();
        if (eat('+')) return parse_unary();
        return parse_primary();
    }
    int64_t parse_primary() {
        skip_space();
        if (eat('(')) {
            const int64_t value = parse_or();
            eat(')');
            return value;
        }
        if (peek_word("defined")) {
            expect_word("defined");
            const bool parens = eat('(');
            skip_space();
            const size_t start = m_pos;
            while (m_pos < m_text.size() && ident_char(m_text[m_pos])) ++m_pos;
            const std::string name = m_text.substr(start, m_pos - start);
            if (parens) eat(')');
            return m_macros.count(name) != 0 ? 1 : 0;
        }
        if (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
            const size_t start = m_pos;
            int base = 10;
            if (m_text[m_pos] == '0' && m_pos + 1 < m_text.size() &&
                (m_text[m_pos + 1] == 'x' || m_text[m_pos + 1] == 'X')) {
                base = 16;
                m_pos += 2;
            }
            while (m_pos < m_text.size()) {
                const char c = m_text[m_pos];
                if (base == 16 ? std::isxdigit(static_cast<unsigned char>(c))
                              : std::isdigit(static_cast<unsigned char>(c))) ++m_pos;
                else break;
            }
            try {
                return static_cast<int64_t>(std::stoll(m_text.substr(start), nullptr, base));
            } catch (...) {
                return 0;
            }
        }
        if (m_pos < m_text.size() && ident_start(m_text[m_pos])) {
            const size_t start = m_pos;
            while (m_pos < m_text.size() && ident_char(m_text[m_pos])) ++m_pos;
            const std::string name = m_text.substr(start, m_pos - start);
            const auto it = m_macros.find(name);
            if (it != m_macros.end() && !it->second.function_like && m_depth < 8) {
                return IfExpression(it->second.replacement, m_macros, m_depth + 1).evaluate();
            }
            return 0;
        }
        if (m_pos < m_text.size()) ++m_pos;  // unknown character: skip it
        return 0;
    }
};

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
    MacroTable& macros,
    std::vector<std::string>& include_stack
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

        if (std::find(include_stack.begin(), include_stack.end(), resolved) != include_stack.end()) {
            out << "#error \"include cycle: " << resolved << " includes itself (directly or indirectly); "
                << "add '#pragma once' or an include guard\"\n";
            return true;
        }

        const std::optional<std::string> inc_text = read_file(resolved);
        if (!inc_text.has_value()) {
            out << "#error \"could not read file: " << resolved << "\"\n";
            return true;
        }

        all_included.push_back(resolved);
        register_source_file(resolved, *inc_text);

        // Recursively process the included file
        include_stack.push_back(resolved);
        std::string processed = preprocess_impl(
            *inc_text, resolved, include_dirs, system_include_dir,
            visited, all_included, macros, include_stack);
        include_stack.pop_back();

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
        if (trimmed.rfind("#if ", 0) == 0 || trimmed == "#if") {
            const std::string expression = trim(trimmed.substr(3));
            const bool value = !expression.empty() &&
                IfExpression(expression, macros).evaluate() != 0;
            const bool parent = active();
            conditions.push_back({parent, parent && value, parent && value});
            continue;
        }
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
            const size_t paren = rest.find('(');
            const size_t space = rest.find_first_of(" \t");
            if (paren != std::string::npos && (space == std::string::npos || paren < space)) {
                // Function-like macro: '(' must immediately follow the name.
                const std::string name = rest.substr(0, paren);
                const size_t close = rest.find(')', paren + 1);
                if (!name.empty() && close != std::string::npos) {
                    MacroDef macro;
                    macro.function_like = true;
                    macro.replacement = trim(rest.substr(close + 1));
                    const std::string param_text = rest.substr(paren + 1, close - paren - 1);
                    size_t begin = 0;
                    while (begin <= param_text.size()) {
                        const size_t comma = param_text.find(',', begin);
                        const std::string param = trim(param_text.substr(
                            begin, comma == std::string::npos ? std::string::npos : comma - begin));
                        if (!param.empty()) macro.params.push_back(param);
                        if (comma == std::string::npos) break;
                        begin = comma + 1;
                    }
                    macros[name] = std::move(macro);
                }
                continue;
            }
            if (space != std::string::npos) {
                const std::string name = rest.substr(0, space);
                if (!name.empty()) {
                    macros[name] = MacroDef{trim(rest.substr(space + 1)), false, {}};
                }
            } else if (!rest.empty()) {
                macros[rest] = MacroDef{"", false, {}};
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
    std::vector<std::string> include_stack{source_filename};
    std::string expanded = preprocess_impl(
        source_text, source_filename,
        include_dirs, system_include_dir,
        visited, result.included_files, macros, include_stack
    );
    result.expanded_source = std::move(expanded);
    return result;
}
