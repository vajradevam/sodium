#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <optional>

struct SourceLoc {
    size_t line = 0;
    size_t col = 0;
    std::string file = "";

    [[nodiscard]] inline std::string to_string() const {
        if (file.empty()) {
            return std::to_string(line) + ":" + std::to_string(col);
        }
        return file + ":" + std::to_string(line) + ":" + std::to_string(col);
    }
};

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
    _u64
};

struct Token {
    TokenType type;
    std::optional <std::string> value {};
    SourceLoc loc {};
};

// Format an error message with source location.
inline std::string format_err(const SourceLoc& loc, const std::string& msg) {
    return "error: " + loc.to_string() + ": " + msg;
}

// Globals for --show-code source annotations.
extern bool g_show_code;
extern std::string g_source_text;

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

class Tokenizer {
public:
    inline explicit Tokenizer(std::string src, std::string filename = "")
        : m_src(std::move(src)), m_filename(std::move(filename))
    {
    }

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

    std::string m_src;
    std::string m_filename;
    size_t m_index = 0;
    size_t m_line = 1;
    size_t m_col = 1;
};
