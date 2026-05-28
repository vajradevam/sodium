#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <optional>

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
    colon
};

struct Token {
    TokenType type;
    std::optional <std::string> value {};
};

class Tokenizer {
public:

    inline explicit Tokenizer(const std::string src)
        : m_src(std::move(src))
    {

    }

    inline std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        
        std::string buf;

        while (peek().has_value()) {
            if (std::isalpha(peek().value())) {
                buf.push_back(consume());
                while (peek().has_value() && std::isalnum(peek().value())) {
                    buf.push_back(consume());
                }

                if (buf == "return") {
                    tokens.push_back({ .type = TokenType::_return });
                }

                else if (buf == "var") {
                    tokens.push_back({ .type = TokenType::let });
                }

                else if (buf == "if") {
                    tokens.push_back({ .type = TokenType::_if });
                }

                else if (buf == "else") {
                    tokens.push_back({ .type = TokenType::_else });
                }

                else if (buf == "while") {
                    tokens.push_back({ .type = TokenType::_while });
                }

                else if (buf == "for") {
                    tokens.push_back({ .type = TokenType::_for });
                }

                else if (buf == "print") {
                    tokens.push_back({ .type = TokenType::_print });
                }

                else if (buf == "function") {
                    tokens.push_back({ .type = TokenType::_function });
                }

                else if (buf == "break") {
                    tokens.push_back({ .type = TokenType::_break });
                }
                else if (buf == "continue") {
                    tokens.push_back({ .type = TokenType::_continue });
                }
                
                else {
                    tokens.push_back({ .type = TokenType::ident, .value = buf });
                }
                buf.clear();
                continue;
            }

            else if (peek().value() == '"') {
                consume(); // "
                buf.clear();
                while (peek().has_value() && peek().value() != '"') {
                    buf.push_back(consume());
                }
                if (!peek().has_value()) {
                    std::cerr << "Unterminated string literal" << std::endl;
                    exit(EXIT_FAILURE);
                }
                consume(); // "
                tokens.push_back({ .type = TokenType::string_lit, .value = buf });
                buf.clear();
                continue;
            }

            else if (std::isdigit(peek().value())) {
                buf.push_back(consume());

                while (peek().has_value() && std::isdigit(peek().value())) {
                    buf.push_back(consume());
                }
                tokens.push_back({ .type = TokenType::int_lit, .value = buf });
                buf.clear();
                continue;
            }

            else if (peek().value() == '(') {
                consume();
                tokens.push_back({ .type = TokenType::open_paren });
                continue;
            }

            else if (peek().value() == ')') {
                consume();
                tokens.push_back({ .type = TokenType::close_paren });
                continue;
            }

            else if (peek().value() == '[') {
                consume();
                tokens.push_back({ .type = TokenType::open_square });
                continue;
            }

            else if (peek().value() == ']') {
                consume();
                tokens.push_back({ .type = TokenType::close_square });
                continue;
            }

            else if (peek().value() == '{') {
                consume();
                tokens.push_back({ .type = TokenType::open_brace });
                continue;
            }

            else if (peek().value() == '}') {
                consume();
                tokens.push_back({ .type = TokenType::close_brace });
                continue;
            }

            else if (peek().value() == ',') {
                consume();
                tokens.push_back({ .type = TokenType::comma });
                continue;
            }

            else if (peek().value() == ';') {
                consume();
                tokens.push_back({ .type = TokenType::semi });
                continue;
            }

            else if (peek().value() == '=') {
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::eq_eq });
                } else {
                    tokens.push_back({ .type = TokenType::eq });
                }
                continue;
            }

            else if (peek().value() == '+') {
                consume();
                if (peek().has_value() && peek().value() == '+') {
                    consume();
                    tokens.push_back({ .type = TokenType::plusplus });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::pluseq });
                } else {
                    tokens.push_back({ .type = TokenType::plus });
                }
                continue;
            }

            else if (peek().value() == '-') {
                consume();
                if (peek().has_value() && peek().value() == '-') {
                    consume();
                    tokens.push_back({ .type = TokenType::minusminus });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::minuseq });
                } else {
                    tokens.push_back({ .type = TokenType::minus });
                }
                continue;
            }

            else if (peek().value() == '*') {
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::stareq });
                } else {
                    tokens.push_back({ .type = TokenType::star });
                }
                continue;
            }

            else if (peek().value() == '/') {
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
                    tokens.push_back({ .type = TokenType::slasheq });
                    continue;
                }
                else {
                    consume();
                    tokens.push_back({ .type = TokenType::slash });
                    continue;
                }
            }

            else if (peek().value() == '<') {
                consume();
                if (peek().has_value() && peek().value() == '<') {
                    consume();
                    tokens.push_back({ .type = TokenType::shl });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::lte });
                } else {
                    tokens.push_back({ .type = TokenType::lt });
                }
                continue;
            }

            else if (peek().value() == '>') {
                consume();
                if (peek().has_value() && peek().value() == '>') {
                    consume();
                    tokens.push_back({ .type = TokenType::shr });
                } else if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::gte });
                } else {
                    tokens.push_back({ .type = TokenType::gt });
                }
                continue;
            }

            else if (peek().value() == '!') {
                consume();
                if (peek().has_value() && peek().value() == '=') {
                    consume();
                    tokens.push_back({ .type = TokenType::neq });
                } else {
                    std::cerr << "Bruh moment!" << std::endl;
                    exit(EXIT_FAILURE);
                }
                continue;
            }

            else if (peek().value() == '&') {
                consume();
                if (peek().has_value() && peek().value() == '&') {
                    consume();
                    tokens.push_back({ .type = TokenType::and_t });
                } else {
                    tokens.push_back({ .type = TokenType::amp });
                }
                continue;
            }

            else if (peek().value() == '|') {
                consume();
                if (peek().has_value() && peek().value() == '|') {
                    consume();
                    tokens.push_back({ .type = TokenType::or_t });
                } else {
                    tokens.push_back({ .type = TokenType::pipe });
                }
                continue;
            }

            else if (peek().value() == '^') {
                consume();
                tokens.push_back({ .type = TokenType::caret });
                continue;
            }

            else if (peek().value() == '~') {
                consume();
                tokens.push_back({ .type = TokenType::tilde });
                continue;
            }

            else if (peek().value() == '%') {
                consume();
                tokens.push_back({ .type = TokenType::percent });
                continue;
            }

            else if (peek().value() == '?') {
                consume();
                tokens.push_back({ .type = TokenType::question });
                continue;
            }

            else if (peek().value() == ':') {
                consume();
                tokens.push_back({ .type = TokenType::colon });
                continue;
            }

            else if (std::isspace(peek().value())) {
                consume();
                continue;
            }
            
            else {
                std::cerr << "Bruh moment!" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        m_index = 0;
        return tokens;
    }

private:

    [[nodiscard]] inline std::optional<char> peek(int offset = 0) const {
        if (m_index + offset >= m_src.length()) {
            return {};
        } else {
            return m_src.at(m_index + offset);
        }

    }

    inline char consume() {
        return m_src.at(m_index++);
    }

    const std::string m_src;
    size_t m_index = 0;
};
