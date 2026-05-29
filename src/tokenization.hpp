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

    [[nodiscard]] std::string to_string() const {
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
// Returns "error: line:col: message".
inline std::string format_err(const SourceLoc& loc, const std::string& msg) {
    return "error: " + loc.to_string() + ": " + msg;
}

class Tokenizer {
public:

    inline explicit Tokenizer(std::string src, std::string filename = "")
        : m_src(std::move(src)), m_filename(std::move(filename))
    {

    }

    inline std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        
        std::string buf;

        while (peek().has_value()) {
            if (std::isalpha(peek().value()) || peek().value() == '_') {
                SourceLoc tok_loc = current_loc();
                buf.push_back(consume());
                while (peek().has_value() && (std::isalnum(peek().value()) || peek().value() == '_')) {
                    buf.push_back(consume());
                }

                if (buf == "return") {
                    tokens.push_back({ .type = TokenType::_return, .loc = tok_loc });
                }

                else if (buf == "var") {
                    tokens.push_back({ .type = TokenType::let, .loc = tok_loc });
                }

                else if (buf == "if") {
                    tokens.push_back({ .type = TokenType::_if, .loc = tok_loc });
                }

                else if (buf == "else") {
                    tokens.push_back({ .type = TokenType::_else, .loc = tok_loc });
                }

                else if (buf == "while") {
                    tokens.push_back({ .type = TokenType::_while, .loc = tok_loc });
                }

                else if (buf == "for") {
                    tokens.push_back({ .type = TokenType::_for, .loc = tok_loc });
                }

                else if (buf == "print") {
                    tokens.push_back({ .type = TokenType::_print, .loc = tok_loc });
                }

                else if (buf == "function") {
                    tokens.push_back({ .type = TokenType::_function, .loc = tok_loc });
                }

                else if (buf == "break") {
                    tokens.push_back({ .type = TokenType::_break, .loc = tok_loc });
                }
                else if (buf == "continue") {
                    tokens.push_back({ .type = TokenType::_continue, .loc = tok_loc });
                }
                else if (buf == "do") {
                    tokens.push_back({ .type = TokenType::_do, .loc = tok_loc });
                }
                else if (buf == "switch") {
                    tokens.push_back({ .type = TokenType::_switch, .loc = tok_loc });
                }
                else if (buf == "case") {
                    tokens.push_back({ .type = TokenType::_case, .loc = tok_loc });
                }
                else if (buf == "default") {
                    tokens.push_back({ .type = TokenType::_default, .loc = tok_loc });
                }
                else if (buf == "const") {
                    tokens.push_back({ .type = TokenType::_const, .loc = tok_loc });
                }
                else if (buf == "global") {
                    tokens.push_back({ .type = TokenType::_global, .loc = tok_loc });
                }
                else if (buf == "static") {
                    tokens.push_back({ .type = TokenType::_static, .loc = tok_loc });
                }
                else if (buf == "i8") {
                    tokens.push_back({ .type = TokenType::_i8, .loc = tok_loc });
                }
                else if (buf == "i16") {
                    tokens.push_back({ .type = TokenType::_i16, .loc = tok_loc });
                }
                else if (buf == "i32") {
                    tokens.push_back({ .type = TokenType::_i32, .loc = tok_loc });
                }
                else if (buf == "i64") {
                    tokens.push_back({ .type = TokenType::_i64, .loc = tok_loc });
                }
                else if (buf == "u8") {
                    tokens.push_back({ .type = TokenType::_u8, .loc = tok_loc });
                }
                else if (buf == "u16") {
                    tokens.push_back({ .type = TokenType::_u16, .loc = tok_loc });
                }
                else if (buf == "u32") {
                    tokens.push_back({ .type = TokenType::_u32, .loc = tok_loc });
                }
                else if (buf == "u64") {
                    tokens.push_back({ .type = TokenType::_u64, .loc = tok_loc });
                }
                else {
                    tokens.push_back({ .type = TokenType::ident, .value = buf, .loc = tok_loc });
                }
                buf.clear();
                continue;
            }

            else if (peek().value() == '"') {
                auto loc = current_loc();
                consume(); // "
                buf.clear();
                while (peek().has_value() && peek().value() != '"') {
                    buf.push_back(consume());
                }
                if (!peek().has_value()) {
                    std::cerr << format_err(loc, "Unterminated string literal") << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // "
                tokens.push_back({ .type = TokenType::string_lit, .value = buf, .loc = loc });
                buf.clear();
                continue;
            }

            else if (std::isdigit(peek().value())) {
                SourceLoc tok_loc = current_loc();
                buf.push_back(consume());

                while (peek().has_value() && std::isdigit(peek().value())) {
                    buf.push_back(consume());
                }
                tokens.push_back({ .type = TokenType::int_lit, .value = buf, .loc = tok_loc });
                buf.clear();
                continue;
            }

            else if (peek().value() == '(') {
                tokens.push_back({ .type = TokenType::open_paren, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == ')') {
                tokens.push_back({ .type = TokenType::close_paren, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == '[') {
                tokens.push_back({ .type = TokenType::open_square, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == ']') {
                tokens.push_back({ .type = TokenType::close_square, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == '{') {
                tokens.push_back({ .type = TokenType::open_brace, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == '}') {
                tokens.push_back({ .type = TokenType::close_brace, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == ',') {
                tokens.push_back({ .type = TokenType::comma, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == ';') {
                tokens.push_back({ .type = TokenType::semi, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == '=') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::eq_eq, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::eq, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '+') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '+') {
                    consume();
                    tokens.push_back({ .type = TokenType::plusplus, .loc = loc });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::pluseq, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::plus, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '-') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '-') {
                    consume();
                    tokens.push_back({ .type = TokenType::minusminus, .loc = loc });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::minuseq, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::minus, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '*') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::stareq, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::star, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '/') {
                auto loc = current_loc();
                if (peek(1).has_value() && peek(1).value() == '/') {
                    while (peek().has_value() && peek().value() != '\n') {
                        consume();
                    }
                    continue;
                }
                else if (peek(1).has_value() && peek(1).value() == '*') {
                    consume(); consume();
                    while (peek().has_value()) {
                        if (peek().value() == '*' && peek(1).has_value() && peek(1).value() == '/') {
                            consume(); consume();
                            break;
                        }
                        consume();
                    }
                    continue;
                }
                else if (peek(1).has_value() && peek(1).value() == '=') {
                    consume(); consume();
                    tokens.push_back({ .type = TokenType::slasheq, .loc = loc });
                    continue;
                }
                else {
                    consume();
                    tokens.push_back({ .type = TokenType::slash, .loc = loc });
                    continue;
                }
            }

            else if (peek().value() == '<') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '<'
                    && peek(1).has_value() && peek(1).value() == '=') {
                    consume(); consume();
                    tokens.push_back({ .type = TokenType::shleq, .loc = loc });
                } else if (peek().has_value() && peek().value() == '<') {
                    consume();
                    tokens.push_back({ .type = TokenType::shl, .loc = loc });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::lte, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::lt, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '>') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '>'
                    && peek(1).has_value() && peek(1).value() == '=') {
                    consume(); consume();
                    tokens.push_back({ .type = TokenType::shreq, .loc = loc });
                } else if (peek().has_value() && peek().value() == '>') {
                    consume();
                    tokens.push_back({ .type = TokenType::shr, .loc = loc });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::gte, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::gt, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '!') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::neq, .loc = loc });
                } else {
                    std::cerr << format_err(loc, "Expected '!='") << std::endl;
                    exit(EXIT_FAILURE);
                }
                continue;
            }

            else if (peek().value() == '&') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::ampeq, .loc = loc });
                } else if (peek().has_value() && peek().value() == '&') {
                    consume();
                    tokens.push_back({ .type = TokenType::and_t, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::amp, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '|') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::pipeeq, .loc = loc });
                } else if (peek().has_value() && peek().value() == '|') {
                    consume();
                    tokens.push_back({ .type = TokenType::or_t, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::pipe, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '^') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::careteq, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::caret, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '~') {
                tokens.push_back({ .type = TokenType::tilde, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == '%') {
                auto loc = current_loc();
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::percenteq, .loc = loc });
                } else {
                    tokens.push_back({ .type = TokenType::percent, .loc = loc });
                }
                continue;
            }

            else if (peek().value() == '?') {
                tokens.push_back({ .type = TokenType::question, .loc = current_loc() });
                consume();
                continue;
            }

            else if (peek().value() == ':') {
                tokens.push_back({ .type = TokenType::colon, .loc = current_loc() });
                consume();
                continue;
            }

            else if (std::isspace(peek().value())) {
                consume();
                continue;
            }
            
            else {
                std::cerr << format_err(current_loc(), "Unexpected character: '" + std::string(1, peek().value()) + "'") << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        m_index = 0;
        return tokens;
    }

private:

    [[nodiscard]] inline SourceLoc current_loc() const {
        return { .line = m_line, .col = m_col, .file = m_filename };
    }

    [[nodiscard]] inline std::optional<char> peek(int offset = 0) const {
        if (m_index + offset >= m_src.length()) {
            return {};
        } else {
            return m_src.at(m_index + offset);
        }

    }

    inline char consume() {
        char c = m_src.at(m_index++);
        if (c == '\n') {
            m_line++;
            m_col = 1;
        } else {
            m_col++;
        }
        return c;
    }

    std::string m_src;
    std::string m_filename;
    size_t m_index = 0;
    size_t m_line = 1;
    size_t m_col = 1;
};
