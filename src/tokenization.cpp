#include "tokenization.hpp"

// Global definitions
bool g_show_code = false;
std::string g_source_text;
bool g_lsp_mode = false;
std::vector<LSPError> g_lsp_errors;

// Small helper for trimming whitespace
static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

void print_code_context(const SourceLoc& loc) {
    if (!g_show_code || loc.line == 0 || loc.col == 0) return;
    if (g_source_text.empty()) return;

    const char* p = g_source_text.data();
    const char* end = p + g_source_text.size();
    size_t current = 1;
    while (p < end && current < loc.line) {
        if (*p == '\n') current++;
        p++;
    }

    const char* line_end = p;
    while (line_end < end && *line_end != '\n') line_end++;

    std::string line(p, line_end - p);
    std::cerr << "  \u2502 " << line << "\n";
    std::cerr << "  \u2502 ";
    for (size_t i = 1; i < loc.col; i++) {
        std::cerr << " ";
    }
    std::cerr << "^\n";
}

void lsp_exit(const SourceLoc& loc, const std::string& msg) {
    if (g_lsp_mode) {
        g_lsp_errors.push_back({loc, msg});
        throw LSPAbort();
    }
    std::cerr << format_err(loc, msg) << std::endl;
    if (g_show_code) print_code_context(loc);
    exit(EXIT_FAILURE);
}

void lsp_exit(const std::string& msg) {
    if (g_lsp_mode) {
        g_lsp_errors.push_back({{}, msg});
        throw LSPAbort();
    }
    std::cerr << msg << std::endl;
    exit(EXIT_FAILURE);
}

char Tokenizer::consume() {
    char c = m_src.at(m_index++);
    if (c == '\n') {
        m_line++;
        m_col = 1;
        m_at_line_start = true;
    } else {
        m_col++;
        m_at_line_start = false;
    }
    return c;
}

// Handle #line directives from the preprocessor:
//   # line_number "filename"\n
// Or just:
//   # line_number\n
// Or #error directives.
void Tokenizer::handle_line_directive() {
    // We've already consumed '#'. Parse the directive.
    // Skip whitespace after '#'
    while (peek().has_value() && (peek().value() == ' ' || peek().value() == '\t')) {
        consume();
    }

    // Read the directive keyword or number
    std::string directive;
    while (peek().has_value() && std::isdigit(peek().value())) {
        directive.push_back(consume());
    }

    if (directive.empty()) {
        // Could be #error
        while (peek().has_value() && std::isalpha(peek().value())) {
            directive.push_back(consume());
        }
        if (directive == "error") {
            // Skip whitespace
            while (peek().has_value() && (peek().value() == ' ' || peek().value() == '\t')) {
                consume();
            }
            // Read the error message (in quotes, or rest of line)
            std::string err_msg;
            if (peek().has_value() && peek().value() == '"') {
                consume(); // skip opening quote
                while (peek().has_value() && peek().value() != '"') {
                    err_msg.push_back(consume());
                }
                if (peek().has_value() && peek().value() == '"') {
                    consume(); // skip closing quote
                }
            } else {
                while (peek().has_value() && peek().value() != '\n') {
                    err_msg.push_back(consume());
                }
                err_msg = trim(err_msg);
            }
            // Skip rest of line
            while (peek().has_value() && peek().value() != '\n') {
                consume();
            }
            if (peek().has_value()) {
                consume(); // consume the '\n'
            }
            m_at_line_start = true;
            // Report the error
            SourceLoc loc = current_loc();
            if (g_lsp_mode) {
                g_lsp_errors.push_back({loc, err_msg});
                throw LSPAbort();
            }
            std::cerr << format_err(loc, err_msg) << std::endl;
            exit(EXIT_FAILURE);
        } else {
            // Unknown directive, skip rest of line
            while (peek().has_value() && peek().value() != '\n') {
                consume();
            }
            if (peek().has_value()) {
                consume(); // consume the '\n'
            }
            m_at_line_start = true;
        }
        return;
    }

    // Parse line number
    size_t new_line = std::stoul(directive);

    // Skip whitespace after number
    while (peek().has_value() && (peek().value() == ' ' || peek().value() == '\t')) {
        consume();
    }

    // Optionally read filename in quotes
    std::string new_filename;
    if (peek().has_value() && peek().value() == '"') {
        consume(); // skip opening quote
        while (peek().has_value() && peek().value() != '"') {
            new_filename.push_back(consume());
        }
        if (peek().has_value() && peek().value() == '"') {
            consume(); // skip closing quote
        }
        // Skip any remaining whitespace before newline
        while (peek().has_value() && (peek().value() == ' ' || peek().value() == '\t')) {
            consume();
        }
    }

    // Skip rest of the line (should just be newline)
    while (peek().has_value() && peek().value() != '\n') {
        consume();
    }
    if (peek().has_value()) {
        consume(); // consume the '\n'
    }

    m_line = new_line;
    if (!new_filename.empty()) {
        m_filename = new_filename;
    }
    m_col = 1;
    m_at_line_start = true;
}

std::vector<Token> Tokenizer::tokenize() {
    std::vector<Token> tokens;
    
    std::string buf;

    while (peek().has_value()) {
        // Handle #line directives at start of line
        if (m_at_line_start && peek().value() == '#') {
            consume(); // consume the '#'
            handle_line_directive();
            continue;
        }

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
            else if (buf == "const") {
                tokens.push_back({ .type = TokenType::_const, .loc = tok_loc });
            }
            else if (buf == "default") {
                tokens.push_back({ .type = TokenType::_default, .loc = tok_loc });
            }
            else if (buf == "global") {
                tokens.push_back({ .type = TokenType::_global, .loc = tok_loc });
            }
            else if (buf == "static") {
                tokens.push_back({ .type = TokenType::_static, .loc = tok_loc });
            }
            else if (buf == "struct") {
                tokens.push_back({ .type = TokenType::_struct, .loc = tok_loc });
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
                if (g_lsp_mode) {
                    g_lsp_errors.push_back({loc, "Unterminated string literal"});
                    throw LSPAbort();
                }
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
                if (g_lsp_mode) {
                    g_lsp_errors.push_back({loc, "Expected '!='"});
                    throw LSPAbort();
                }
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

        else if (peek().value() == '.') {
            tokens.push_back({ .type = TokenType::dot, .loc = current_loc() });
            consume();
            continue;
        }

        else if (std::isspace(peek().value())) {
            consume();
            continue;
        }
        
        else {
            if (g_lsp_mode) {
                g_lsp_errors.push_back({current_loc(), "Unexpected character: '" + std::string(1, peek().value()) + "'"});
                throw LSPAbort();
            }
            std::cerr << format_err(current_loc(), "Unexpected character: '" + std::string(1, peek().value()) + "'") << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    m_index = 0;
    return tokens;
}
