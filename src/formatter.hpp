#pragma once

#include <string>
#include <vector>

/// Canonical, comment-preserving line formatter for Cyan (D-042).
///
/// Indentation is four spaces per brace depth, trailing whitespace is removed
/// and blank runs collapse to one empty line. Braces are counted outside
/// string/character literals and comments. Lines whose first non-space
/// characters begin a comment are emitted verbatim so test headers and
/// documentation comments are never altered.
namespace formatter {

inline std::string trim_right(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
    return s.substr(0, end);
}

inline std::string trim_left(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
    return s.substr(begin);
}

/// Count net brace depth change in a line, ignoring braces inside literals
/// and after a // comment.
inline int brace_delta(const std::string& line) {
    int delta = 0;
    char quote = 0;
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) { escaped = false; continue; }
        if (quote != 0) {
            if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') break;
        if (c == '{') ++delta;
        else if (c == '}') --delta;
    }
    return delta;
}

inline bool is_comment_line(const std::string& trimmed) {
    return trimmed.rfind("//", 0) == 0 || trimmed.rfind("/*", 0) == 0 ||
           trimmed.rfind("*", 0) == 0 || trimmed.rfind("*/", 0) == 0;
}

inline std::string format_source(const std::string& text) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t newline = text.find('\n', pos);
        if (newline == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, newline - pos));
        pos = newline + 1;
    }

    std::string out;
    int depth = 0;
    bool in_block_comment = false;
    bool previous_blank = false;
    for (const auto& raw : lines) {
        const std::string trimmed = trim_left(raw);
        if (in_block_comment) {
            out += raw;
            out += '\n';
            if (trimmed.find("*/") != std::string::npos) in_block_comment = false;
            continue;
        }
        if (is_comment_line(trimmed)) {
            out += raw;
            out += '\n';
            if (trimmed.rfind("/*", 0) == 0 && trimmed.find("*/") == std::string::npos) {
                in_block_comment = true;
            }
            continue;
        }
        if (trimmed.empty()) {
            if (!previous_blank) out += '\n';
            previous_blank = true;
            continue;
        }
        previous_blank = false;
        const int indent_depth = trimmed[0] == '}' && depth > 0 ? depth - 1 : depth;
        out += std::string(static_cast<size_t>(indent_depth) * 4, ' ');
        out += trimmed;
        out += '\n';
        depth += brace_delta(trimmed);
        if (depth < 0) depth = 0;
    }
    return out;
}

} // namespace formatter
