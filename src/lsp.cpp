#include "lsp.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>

#include "parser.hpp"

// ── Constructor ────────────────────────────────────────────────────
LSPServer::LSPServer() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
}

// ── Main loop ──────────────────────────────────────────────────────
void LSPServer::run() {
    while (m_running) {
        JsonValue msg = read_message();
        if (msg.is_null()) break;  // EOF

        // Every LSP message has "method" (notification or request)
        // Requests also have "id"
        const JsonValue* method_val = msg.get("method");
        if (!method_val) continue;  // malformed

        std::string method = method_val->as_string();
        const JsonValue* id = msg.get("id");
        const JsonValue* params = msg.get("params");
        JsonValue empty_params = JsonValue::ObjectType{};
        if (!params) params = &empty_params;

        if (method == "initialize") {
            handle_initialize(*id, *params);
        } else if (method == "shutdown") {
            handle_shutdown(*id);
        } else if (method == "exit") {
            m_running = false;
        } else if (method == "textDocument/didOpen") {
            handle_text_document_did_open(*params);
        } else if (method == "textDocument/didChange") {
            handle_text_document_did_change(*params);
        } else if (method == "textDocument/didClose") {
            handle_text_document_did_close(*params);
        } else if (method == "textDocument/completion") {
            handle_completion(*id, *params);
        } else if (method == "textDocument/hover") {
            handle_hover(*id, *params);
        } else if (method == "textDocument/definition") {
            handle_definition(*id, *params);
        } else if (method == "textDocument/documentSymbol") {
            handle_document_symbol(*id, *params);
        } else if (method == "$cancelRequest") {
            // ignore
        } else {
            // Method not found – send error for requests only
            if (id && !id->is_null()) {
                send_error(*id, -32601, "Method not found: " + method);
            }
        }
    }
}

// ── I/O helpers ────────────────────────────────────────────────────
JsonValue LSPServer::read_message() {
    // Read Content-Length header
    std::string header;
    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) return JsonValue(nullptr); // EOF
        if (line.empty() || line == "\r") break;  // end of headers (blank line)
        header += line + "\n";
    }

    // Parse Content-Length
    size_t len = 0;
    auto pos = header.find("Content-Length: ");
    if (pos != std::string::npos) {
        len = std::stoul(header.substr(pos + 16));
    }
    if (len == 0) return JsonValue(nullptr);

    // Read the JSON body
    std::string body(len, '\0');
    std::cin.read(&body[0], len);
    if (std::cin.gcount() < static_cast<std::streamsize>(len)) {
        return JsonValue(nullptr);
    }

    return Json::parse(body);
}

void LSPServer::write_message(const std::string& body) {
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

void LSPServer::send_response(const JsonValue& id, const JsonValue& result) {
    JsonValue::ObjectType obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = id;
    obj["result"] = result;
    write_message(Json::serialize(JsonValue(std::move(obj))));
}

void LSPServer::send_error(const JsonValue& id, int code, const std::string& msg) {
    JsonValue::ObjectType obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = id;
    JsonValue::ObjectType err;
    err["code"] = code;
    err["message"] = msg;
    obj["error"] = std::move(err);
    write_message(Json::serialize(JsonValue(std::move(obj))));
}

void LSPServer::send_notification(const std::string& method, const JsonValue& params) {
    JsonValue::ObjectType obj;
    obj["jsonrpc"] = "2.0";
    obj["method"] = method;
    obj["params"] = params;
    write_message(Json::serialize(JsonValue(std::move(obj))));
}

// ── Method handlers ────────────────────────────────────────────────
void LSPServer::handle_initialize(const JsonValue& id, const JsonValue& /*params*/) {
    JsonValue::ObjectType result;
    JsonValue::ObjectType capabilities;

    // TextDocumentSyncKind::Full = 1
    JsonValue::ObjectType text_sync;
    text_sync["openClose"] = true;
    text_sync["change"] = 1;  // Full sync
    text_sync["save"] = true;
    capabilities["textDocumentSync"] = std::move(text_sync);

    // Completion provider (basic)
    JsonValue::ObjectType completion;
    completion["triggerCharacters"] = JsonValue::ArrayType{};
    capabilities["completionProvider"] = std::move(completion);

    // Hover provider
    capabilities["hoverProvider"] = true;

    // Definition provider
    capabilities["definitionProvider"] = true;

    // Document symbols
    capabilities["documentSymbolProvider"] = true;

    result["capabilities"] = std::move(capabilities);
    JsonValue::ObjectType server_info;
    server_info["name"] = "cyan-lsp";
    server_info["version"] = "0.1.0";
    result["serverInfo"] = std::move(server_info);

    send_response(id, JsonValue(std::move(result)));
}

void LSPServer::handle_shutdown(const JsonValue& id) {
    send_response(id, JsonValue(nullptr));
}

void LSPServer::handle_text_document_did_open(const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    if (!td) return;
    const JsonValue* uri = td->get("uri");
    const JsonValue* text = td->get("text");
    if (!uri || !text) return;
    m_documents[uri->as_string()] = text->as_string();
    publish_diagnostics(uri->as_string(), text->as_string());
}

void LSPServer::handle_text_document_did_change(const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    if (!td) return;
    const JsonValue* uri = td->get("uri");
    if (!uri) return;
    // Look for the content in contentChanges[].text
    const JsonValue* changes = params.get("contentChanges");
    if (changes && changes->is_array() && !changes->as_array().empty()) {
        const auto& last = changes->as_array().back();
        const JsonValue* text = last.get("text");
        if (text) {
            m_documents[uri->as_string()] = text->as_string();
        }
    }
    publish_diagnostics(uri->as_string(), m_documents[uri->as_string()]);
}

void LSPServer::handle_text_document_did_close(const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    if (!td) return;
    const JsonValue* uri = td->get("uri");
    if (!uri) return;
    m_documents.erase(uri->as_string());
}

void LSPServer::handle_completion(const JsonValue& id, const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    const JsonValue* pos_val = params.get("position");
    if (!td || !pos_val) {
        send_error(id, -32602, "Missing textDocument or position");
        return;
    }
    const JsonValue* uri = td->get("uri");
    if (!uri) {
        send_error(id, -32602, "Missing URI");
        return;
    }

    std::string uri_str = uri->as_string();
    auto it = m_documents.find(uri_str);
    if (it == m_documents.end()) {
        send_error(id, -32602, "Document not open");
        return;
    }

    // Tokenize to get identifiers in the document
    std::vector<Token> toks = tokenize(it->second);

    // Collect unique identifiers
    std::vector<std::string> words;
    for (const auto& tok : toks) {
        if (tok.type == TokenType::ident && tok.value.has_value()) {
            if (std::find(words.begin(), words.end(), tok.value.value()) == words.end()) {
                words.push_back(tok.value.value());
            }
        }
    }

    // Build completion list (keywords + identifiers)
    JsonValue::ArrayType items;
    // Keywords
    for (const auto& kw : keywords()) {
        JsonValue::ObjectType item;
        item["label"] = kw;
        item["kind"] = 14;  // Keyword
        items.push_back(std::move(item));
    }
    // Identifiers from document
    for (const auto& w : words) {
        JsonValue::ObjectType item;
        item["label"] = w;
        item["kind"] = 6;  // Variable
        items.push_back(std::move(item));
    }

    JsonValue::ObjectType result;
    result["isIncomplete"] = false;
    result["items"] = std::move(items);
    send_response(id, JsonValue(std::move(result)));
}

void LSPServer::handle_hover(const JsonValue& id, const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    const JsonValue* pos_val = params.get("position");
    if (!td || !pos_val) {
        send_error(id, -32602, "Missing textDocument or position");
        return;
    }
    const JsonValue* uri = td->get("uri");
    if (!uri) {
        send_error(id, -32602, "Missing URI");
        return;
    }

    Position pos;
    const JsonValue* line_val = pos_val->get("line");
    const JsonValue* char_val = pos_val->get("character");
    if (!line_val || !char_val) {
        send_error(id, -32602, "Invalid position");
        return;
    }
    pos.line = static_cast<size_t>(line_val->as_int());
    pos.col = static_cast<size_t>(char_val->as_int());

    std::string uri_str = uri->as_string();
    auto it = m_documents.find(uri_str);
    if (it == m_documents.end()) {
        send_error(id, -32602, "Document not open");
        return;
    }

    // Tokenize and find the token at position
    std::vector<Token> toks = tokenize(it->second);
    std::string hover_text;

    for (const auto& tok : toks) {
        if (position_matches(pos, tok)) {
            if (tok.type == TokenType::ident && tok.value.has_value()) {
                // Try to find the identifier info by parsing
                hover_text = "identifier: `" + tok.value.value() + "`";
            } else if (tok.type == TokenType::int_lit && tok.value.has_value()) {
                hover_text = "integer literal: " + tok.value.value();
            } else if (tok.type == TokenType::string_lit && tok.value.has_value()) {
                hover_text = "string literal: \"" + tok.value.value() + "\"";
            } else {
                // Map token type name
                hover_text = "keyword";
            }
            break;
        }
    }

    if (hover_text.empty()) {
        send_response(id, JsonValue(nullptr));
        return;
    }

    JsonValue::ObjectType contents;
    JsonValue::ArrayType parts;
    JsonValue::ObjectType part;
    part["language"] = "cyan";
    part["value"] = hover_text;
    parts.push_back(std::move(part));
    contents["kind"] = "markdown";
    contents["value"] = "```cyan\n" + hover_text + "\n```";

    JsonValue::ObjectType result;
    result["contents"] = std::move(contents);
    send_response(id, JsonValue(std::move(result)));
}

void LSPServer::handle_definition(const JsonValue& id, const JsonValue& params) {
    // For now, return null (no definition found).
    // A full implementation would parse the AST and track symbol declarations.
    send_response(id, JsonValue(nullptr));
}

void LSPServer::handle_document_symbol(const JsonValue& id, const JsonValue& params) {
    // For now, return empty array.
    // A full implementation would parse the AST and report functions/globals.
    send_response(id, JsonValue::ArrayType{});
}

// ── Diagnostics ────────────────────────────────────────────────────
void LSPServer::publish_diagnostics(const std::string& uri, const std::string& text) {
    // Run tokenization + parsing in LSP mode to collect errors
    g_lsp_mode = true;
    g_lsp_errors.clear();
    g_source_text = text;

    JsonValue::ArrayType diags;

    try {
        Tokenizer tokenizer(std::string(text), "");
        std::vector<Token> tokens = tokenizer.tokenize();

        Parser parser(std::move(tokens));
        auto prog = parser.parse_prog();
        (void)prog; // ignore result, we just want errors
    } catch (const LSPAbort&) {
        // Errors are already collected in g_lsp_errors
    }

    // Convert each error to a diagnostic
    for (const auto& err : g_lsp_errors) {
        Position p = loc_to_position(err.loc);
        JsonValue::ObjectType diag;
        JsonValue::ObjectType range;
        JsonValue::ObjectType start, end;
        start["line"] = static_cast<int64_t>(p.line);
        start["character"] = static_cast<int64_t>(p.col);
        end["line"] = static_cast<int64_t>(p.line);
        end["character"] = static_cast<int64_t>(p.col + 1);
        range["start"] = std::move(start);
        range["end"] = std::move(end);
        diag["range"] = std::move(range);
        diag["severity"] = 1; // Error
        diag["source"] = "cyan-lsp";
        diag["message"] = err.msg;
        diags.push_back(std::move(diag));
    }

    g_lsp_mode = false;
    g_lsp_errors.clear();

    // Send notification
    JsonValue::ObjectType params;
    params["uri"] = uri;
    params["diagnostics"] = std::move(diags);
    send_notification("textDocument/publishDiagnostics", JsonValue(std::move(params)));
}

// ── Helpers ────────────────────────────────────────────────────────
LSPServer::Position LSPServer::loc_to_position(const SourceLoc& loc) {
    return { loc.line > 0 ? loc.line - 1 : 0, loc.col > 0 ? loc.col - 1 : 0 };
}

std::vector<Token> LSPServer::tokenize(const std::string& text) {
    g_lsp_mode = true;
    g_lsp_errors.clear();
    std::vector<Token> tokens;
    try {
        Tokenizer tokenizer(std::string(text), "");
        tokens = tokenizer.tokenize();
    } catch (const LSPAbort&) {
        // Incomplete tokenization, return what we have
    }
    g_lsp_mode = false;
    g_lsp_errors.clear();
    return tokens;
}

bool LSPServer::position_matches(const Position& pos, const Token& tok) {
    // LSP positions are 0-based. SourceLoc stores 1-based.
    if (tok.loc.line == 0) return false;
    size_t tok_line = tok.loc.line - 1;
    size_t tok_start_col = tok.loc.col - 1;
    // Token "length" is approximate: ident and keyword tokens are variable length
    // We estimate length from the value if available, otherwise 1
    size_t tok_len = 1;
    if (tok.value.has_value()) {
        tok_len = tok.value.value().size();
    } else {
        // For keyword tokens without value, use a heuristic
        switch (tok.type) {
            case TokenType::_return: tok_len = 6; break;
            case TokenType::let: tok_len = 3; break;
            case TokenType::_if: tok_len = 2; break;
            case TokenType::_else: tok_len = 4; break;
            case TokenType::_while: tok_len = 5; break;
            case TokenType::_for: tok_len = 3; break;
            case TokenType::_print: tok_len = 5; break;
            case TokenType::_function: tok_len = 8; break;
            case TokenType::_break: tok_len = 5; break;
            case TokenType::_continue: tok_len = 8; break;
            case TokenType::_do: tok_len = 2; break;
            case TokenType::_switch: tok_len = 6; break;
            case TokenType::_case: tok_len = 4; break;
            case TokenType::_const: tok_len = 5; break;
            case TokenType::_default: tok_len = 7; break;
            case TokenType::_global: tok_len = 6; break;
            case TokenType::_static: tok_len = 6; break;
            case TokenType::int_lit: tok_len = 1; break;
            default: tok_len = 1; break;
        }
    }
    return pos.line == tok_line &&
           pos.col >= tok_start_col &&
           pos.col < tok_start_col + tok_len;
}

const std::vector<std::string>& LSPServer::keywords() {
    static const std::vector<std::string> kw = {
        "return", "var", "if", "else", "while", "for", "print",
        "function", "break", "continue", "do", "switch", "case",
        "const", "default", "global", "static",
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "true", "false"
    };
    return kw;
}
