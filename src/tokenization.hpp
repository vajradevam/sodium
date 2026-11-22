#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

#include "source_loc.hpp"
#include "diagnostic.hpp"


enum class TokenType {
    exit, 
    int_lit,
    string_lit,
    semi,
    open_paren,
    close_paren,
    ident,
    let,
    eq,
    plus,
    star,
    minus,
    slash,
    open_brace,
    close_brace,
    _if,
    _else,
    _while,
    lt,
    gt,
    eq_eq,
    neq,
    and_t,
    or_t,
    lte,
    gte,
    _for,
    _print,
    _function,
    _return,
    comma,
    open_square,
    close_square,
    _break,
    _continue,
    _do,
    _switch,
    _case,
    _const,
    _default,
    _global,
    _static,
    pluseq,
    minuseq,
    stareq,
    slasheq,
    plusplus,
    minusminus,
    percent,
    amp,
    pipe,
    caret,
    tilde,
    shl,
    shr,
    bang,
    question,
    colon,
    percenteq,
    ampeq,
    pipeeq,
    careteq,
    shleq,
    shreq,
    _i8,
    _i16,
    _i32,
    _i64,
    _u8,
    _u16,
    _u32,
    _u64,
    _struct,
    dot,
    arrow   // ->, used by typed function return types
};

struct Token {
    TokenType type;
    std::optional <std::string> value {};
    SourceLoc loc {};
};

// Format an error message with source location.
inline std::string format_err(const SourceLoc& loc, const std::string& msg) {
    DiagnosticEngine engine;
    engine.error(loc, msg);
    const std::string rendered = engine.render_all();
    return rendered.substr(0, rendered.size() - 1); // drop trailing newline
}

// Globals for --show-code source annotations.
extern bool g_show_code;
extern std::string g_source_text;

/// Source text of every file seen during preprocessing, keyed by the path
/// recorded in SourceLoc. Used to show the right line for errors in includes.
extern std::unordered_map<std::string, std::string> g_source_files;
void register_source_file(const std::string& name, std::string text);

// LSP mode: when true, errors are collected instead of calling exit().
// The LSP server catches LSPAbort to resume after the first error.
struct LSPError {
    SourceLoc loc;
    std::string msg;
};
extern bool g_lsp_mode;
extern std::vector<LSPError> g_lsp_errors;

struct LSPAbort : std::exception {
    const char* what() const noexcept override { return "LSP abort"; }
};

// Print the source line and a caret pointing to the error column.
void print_code_context(const SourceLoc& loc);

// LSP-aware error handler: in LSP mode collects the error and throws LSPAbort;
// in normal mode prints the error and calls exit(EXIT_FAILURE).
[[noreturn]] void lsp_exit(const SourceLoc& loc, const std::string& msg);
[[noreturn]] void lsp_exit(const std::string& msg);

class Tokenizer {
public:
    inline explicit Tokenizer(std::string src, std::string filename = "")
        : m_src(std::move(src)), m_filename(std::move(filename))
    {
    }

    /// Diagnostics collected while tokenizing. Owned by this tokenizer.
    const DiagnosticEngine& diagnostics() const { return m_diag; }

    std::vector<Token> tokenize();

private:
    [[nodiscard]] inline SourceLoc current_loc() const {
        return { .line = m_line, .col = m_col, .file = m_filename };
    }

    [[nodiscard]] inline std::optional<char> peek(int offset = 0) const {
        if (m_index + offset >= m_src.length()) {
            return {};
        }
        return m_src.at(m_index + offset);
    }

    char consume();

    void handle_line_directive();

    std::string m_src;
    std::string m_filename;
    size_t m_index = 0;
    size_t m_line = 1;
    size_t m_col = 1;
    bool m_at_line_start = true;
    DiagnosticEngine m_diag;
};
