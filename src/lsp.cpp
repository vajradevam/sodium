#include "lsp.hpp"
#include "parser.hpp"
#include "generation.hpp"
#include "preprocessor.hpp"
#include "backend/null_backend.hpp"
#include "ir/target_regs.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <optional>

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

        const JsonValue* method_val = msg.get("method");
        if (!method_val) continue;

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
            if (id && !id->is_null()) {
                send_error(*id, -32601, "Method not found: " + method);
            }
        }
    }
}

// ── I/O helpers ────────────────────────────────────────────────────
JsonValue LSPServer::read_message() {
    std::string header;
    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) return JsonValue(nullptr);
        if (line.empty() || line == "\r") break;
        header += line + "\n";
    }
    size_t len = 0;
    auto pos = header.find("Content-Length: ");
    if (pos != std::string::npos) {
        len = std::stoul(header.substr(pos + 16));
    }
    if (len == 0) return JsonValue(nullptr);

    std::string body(len, '\0');
    std::cin.read(&body[0], len);
    if (std::cin.gcount() < static_cast<std::streamsize>(len)) return JsonValue(nullptr);
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

    JsonValue::ObjectType text_sync;
    text_sync["openClose"] = true;
    text_sync["change"] = 1;
    text_sync["save"] = true;
    capabilities["textDocumentSync"] = std::move(text_sync);

    JsonValue::ObjectType completion;
    completion["triggerCharacters"] = JsonValue::ArrayType{};
    capabilities["completionProvider"] = std::move(completion);

    capabilities["hoverProvider"] = true;
    capabilities["definitionProvider"] = true;
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

// ── Symbol table helpers ───────────────────────────────────────────

namespace {

// Describes a single declaration in the source.
struct DeclInfo {
    std::string name;
    SourceLoc loc;
    int symbol_kind; // LSP SymbolKind: 12=Function, 13=Variable, 14=Constant, 23=Parameter
    std::string detail; // human-readable description
};

// Walk the AST and collect all declarations into a vector.
// Also builds a name → first-declaration map for quick lookup.
struct SymbolTable {
    std::vector<DeclInfo> all_decls;
    // name → first occurrence (function params shadow outer vars)
};

void walk_stmt(const NodeStmt& stmt, SymbolTable& table);
void walk_block(const NodeBlock* block, SymbolTable& table);
void walk_expr(const NodeExpr* expr, SymbolTable& table);

void walk_block(const NodeBlock* block, SymbolTable& table) {
    if (!block) return;
    for (const auto& s : block->stmts) {
        walk_stmt(s, table);
    }
}

void walk_stmt(const NodeStmt& stmt, SymbolTable& table) {
    struct Walker {
        SymbolTable& table;
        void operator()(const NodeStmtLet* s) {
            DeclInfo info;
            info.name = s->ident.value.value_or("<anon>");
            info.loc = s->ident.loc;
            info.symbol_kind = 13; // Variable
            info.detail = "var " + info.name;
            table.all_decls.push_back(info);
        }
        void operator()(const NodeStmtGlobal* s) {
            DeclInfo info;
            info.name = s->name.value.value_or("<anon>");
            info.loc = s->name.loc;
            info.symbol_kind = 13; // Variable
            info.detail = "global " + info.name;
            table.all_decls.push_back(info);
        }
        void operator()(const NodeStmtConst* s) {
            DeclInfo info;
            info.name = s->name.value.value_or("<anon>");
            info.loc = s->name.loc;
            info.symbol_kind = 14; // Constant
            info.detail = "const " + info.name;
            table.all_decls.push_back(info);
        }
        void operator()(const NodeStmtIf* s) {
            walk_block(s->then_block, table);
            if (s->else_block) walk_block(s->else_block, table);
        }
        void operator()(const NodeStmtWhile* s) {
            walk_block(s->body, table);
        }
        void operator()(const NodeStmtDoWhile* s) {
            walk_block(s->body, table);
        }
        void operator()(const NodeStmtFor* s) {
            if (s->init.has_value()) walk_stmt(s->init.value(), table);
            walk_block(s->body, table);
        }
        void operator()(const NodeStmtSwitch* s) {
            for (const auto& c : s->cases) {
                for (const auto& ss : c.stmts) walk_stmt(ss, table);
            }
        }
        void operator()(const NodeStmtBlock* s) {
            walk_block(s->block, table);
        }
        void operator()(const NodeStmtExit*) {}
        void operator()(const NodeStmtReturn*) {}
        void operator()(const NodeStmtPrint*) {}
        void operator()(const NodeStmtAssign*) {}
        void operator()(const NodeStmtExpr*) {}
        void operator()(const NodeStmtArrDecl* s) {
            DeclInfo info;
            info.name = s->name.value.value_or("<anon>");
            info.loc = s->name.loc;
            info.symbol_kind = 13; // Variable
            info.detail = "var " + info.name + "[]";
            table.all_decls.push_back(info);
        }
        void operator()(const NodeStmtArrAssign*) {}
        void operator()(const NodeStmtBreak*) {}
        void operator()(const NodeStmtContinue*) {}
        void operator()(const NodeStmtFieldAssign*) {}
        void operator()(const NodeStmtDerefAssign*) {}
    };
    std::visit(Walker{table}, stmt.var);
}

// Parse a document in LSP mode and return the AST + symbol table.
// If parsing fails, the symbol table may be partial (whatever was collected before the error).
struct ParseResult {
    std::optional<NodeProg> prog; // present only if parsing + codegen succeeded
    std::vector<LSPError> errors;
    SymbolTable symbols;
};

ParseResult parse_document(const std::string& text, const std::string& filename = "") {
    ParseResult result;

    g_lsp_mode = true;
    g_lsp_errors.clear();

    // Run preprocessor
    std::vector<std::string> include_dirs;
    PreprocessedResult pp = preprocess(text, filename, include_dirs);
    g_source_text = pp.expanded_source;

    try {
        Tokenizer tokenizer(std::string(pp.expanded_source), filename);
        std::vector<Token> tokens = tokenizer.tokenize();

        Parser parser(std::move(tokens));
        auto prog_opt = parser.parse_prog();
        if (prog_opt.has_value()) {
            NodeProg prog = std::move(prog_opt.value());

            // Walk struct definitions (they're separate from stmts)
            for (const auto* sd : prog.structs) {
                DeclInfo info;
                info.name = sd->name.value.value_or("<anon>");
                info.loc = sd->name.loc;
                info.symbol_kind = 5; // Struct (LSP SymbolKind=5 is Class, close enough)
                info.detail = "struct " + info.name;
                result.symbols.all_decls.push_back(info);
            }

            // Walk functions first (they're separate from stmts)
            for (const auto* func : prog.funcs) {
                DeclInfo info;
                info.name = func->name.value.value_or("<anon>");
                info.loc = func->name.loc;
                info.symbol_kind = 12; // Function
                info.detail = "function " + info.name + "(";
                for (size_t i = 0; i < func->params.size(); i++) {
                    if (i > 0) info.detail += ", ";
                    info.detail += func->params[i].value.value_or("?");
                }
                info.detail += ")";
                result.symbols.all_decls.push_back(info);

                // Add parameters
                for (const auto& p : func->params) {
                    DeclInfo pinfo;
                    pinfo.name = p.value.value_or("<anon>");
                    pinfo.loc = p.loc;
                    pinfo.symbol_kind = 23; // Parameter
                    pinfo.detail = "param " + pinfo.name;
                    result.symbols.all_decls.push_back(pinfo);
                }

                // Walk body for local declarations
                walk_block(func->body, result.symbols);
            }

            // Walk top-level statements
            for (const auto& s : prog.stmts) {
                walk_stmt(s, result.symbols);
            }

            // Also run codegen to catch any semantic errors
            try {
                NullBackend null_backend;
                TargetRegisterInfo lsp_tri = TargetRegisterInfo::dummy();
                Generator generator(std::move(prog), null_backend, lsp_tri);
                std::string _discard = generator.gen_prog();
                (void)_discard; // discard output, we only want errors
                // If we get here, codegen succeeded — store the prog (already moved though)
                // We don't keep prog since we already walked symbols
            } catch (const LSPAbort&) {
                // Codegen errors collected in g_lsp_errors
            }

            result.prog.emplace(); // mark that parsing + codegen succeeded
        }
    } catch (const LSPAbort&) {
        // Parser error — g_lsp_errors already populated
    }

    result.errors = std::move(g_lsp_errors);
    g_lsp_errors.clear();
    g_lsp_mode = false;

    return result;
}

// Find a decl by name (returns first match, preferring outer scope).
// For simplicity this just does a linear search; a real implementation
// would track scopes properly.
const DeclInfo* find_decl(const SymbolTable& table, const std::string& name) {
    for (const auto& d : table.all_decls) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

} // anonymous namespace

// ── Completion ─────────────────────────────────────────────────────
void LSPServer::handle_completion(const JsonValue& id, const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    if (!td) { send_error(id, -32602, "Missing textDocument"); return; }
    const JsonValue* uri = td->get("uri");
    if (!uri) { send_error(id, -32602, "Missing URI"); return; }

    std::string uri_str = uri->as_string();
    auto it = m_documents.find(uri_str);
    if (it == m_documents.end()) { send_error(id, -32602, "Document not open"); return; }

    // Parse to get symbols for completion
    auto parse_result = parse_document(it->second);
    const auto& symbols = parse_result.symbols;

    // Collect unique identifiers from symbol table
    std::vector<std::string> words;
    for (const auto& d : symbols.all_decls) {
        if (std::find(words.begin(), words.end(), d.name) == words.end()) {
            words.push_back(d.name);
        }
    }

    JsonValue::ArrayType items;
    // Keywords
    for (const auto& kw : keywords()) {
        JsonValue::ObjectType item;
        item["label"] = kw;
        item["kind"] = 14; // Keyword
        items.push_back(std::move(item));
    }
    // Identifiers from document (with their detail)
    for (const auto& w : words) {
        JsonValue::ObjectType item;
        item["label"] = w;
        item["kind"] = 6; // Variable
        // Find detail if available
        if (auto* decl = find_decl(symbols, w)) {
            item["detail"] = decl->detail;
        }
        items.push_back(std::move(item));
    }

    JsonValue::ObjectType result;
    result["isIncomplete"] = false;
    result["items"] = std::move(items);
    send_response(id, JsonValue(std::move(result)));
}

// ── Hover ──────────────────────────────────────────────────────────
void LSPServer::handle_hover(const JsonValue& id, const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    const JsonValue* pos_val = params.get("position");
    if (!td || !pos_val) { send_error(id, -32602, "Missing textDocument or position"); return; }
    const JsonValue* uri = td->get("uri");
    if (!uri) { send_error(id, -32602, "Missing URI"); return; }

    Position pos;
    const JsonValue* line_val = pos_val->get("line");
    const JsonValue* char_val = pos_val->get("character");
    if (!line_val || !char_val) { send_error(id, -32602, "Invalid position"); return; }
    pos.line = static_cast<size_t>(line_val->as_int());
    pos.col = static_cast<size_t>(char_val->as_int());

    std::string uri_str = uri->as_string();
    auto it = m_documents.find(uri_str);
    if (it == m_documents.end()) { send_error(id, -32602, "Document not open"); return; }

    // Parse the document to get symbol info
    auto parse_result = parse_document(it->second);
    const auto& symbols = parse_result.symbols;

    // Tokenize and find the token at position
    std::vector<Token> toks;
    {
        g_lsp_mode = true;
        g_lsp_errors.clear();
        try {
            std::string filepath = uri_to_path(uri_str);
            std::vector<std::string> inc_dirs;
            PreprocessedResult pp = preprocess(it->second, filepath, inc_dirs);
            Tokenizer tokenizer(std::string(pp.expanded_source), filepath);
            toks = tokenizer.tokenize();
        } catch (const LSPAbort&) {}
        g_lsp_mode = false;
        g_lsp_errors.clear();
    }

    std::string hover_text;

    for (const auto& tok : toks) {
        if (position_matches(pos, tok)) {
            if (tok.type == TokenType::ident && tok.value.has_value()) {
                const std::string& name = tok.value.value();
                // Look up in symbol table
                if (auto* decl = find_decl(symbols, name)) {
                    hover_text = decl->detail;
                } else {
                    hover_text = "identifier: `" + name + "`";
                }
            } else if (tok.type == TokenType::int_lit && tok.value.has_value()) {
                hover_text = "integer literal: " + tok.value.value();
            } else if (tok.type == TokenType::string_lit && tok.value.has_value()) {
                hover_text = "string literal: \"" + tok.value.value() + "\"";
            } else {
                // Map common keyword token types to names
                switch (tok.type) {
                    case TokenType::_return: hover_text = "keyword: return"; break;
                    case TokenType::let:     hover_text = "keyword: var"; break;
                    case TokenType::_if:     hover_text = "keyword: if"; break;
                    case TokenType::_else:   hover_text = "keyword: else"; break;
                    case TokenType::_while:  hover_text = "keyword: while"; break;
                    case TokenType::_for:    hover_text = "keyword: for"; break;
                    case TokenType::_function: hover_text = "keyword: function"; break;
                    case TokenType::_break:  hover_text = "keyword: break"; break;
                    case TokenType::_continue: hover_text = "keyword: continue"; break;
                    case TokenType::_do:     hover_text = "keyword: do"; break;
                    case TokenType::_switch: hover_text = "keyword: switch"; break;
                    case TokenType::_case:   hover_text = "keyword: case"; break;
                    case TokenType::_const:  hover_text = "keyword: const"; break;
                    case TokenType::_default: hover_text = "keyword: default"; break;
                    case TokenType::_global: hover_text = "keyword: global"; break;
                    case TokenType::_static: hover_text = "keyword: static"; break;
                    case TokenType::_print:  hover_text = "builtin: print"; break;
                    default:                hover_text = "syntax token: " + std::to_string(static_cast<int>(tok.type)); break;
                }
            }
            break;
        }
    }

    if (hover_text.empty()) {
        send_response(id, JsonValue(nullptr));
        return;
    }

    JsonValue::ObjectType contents;
    contents["kind"] = "markdown";
    contents["value"] = "```cyan\n" + hover_text + "\n```";

    JsonValue::ObjectType result;
    result["contents"] = std::move(contents);
    send_response(id, JsonValue(std::move(result)));
}

// ── Go-to-definition ───────────────────────────────────────────────
void LSPServer::handle_definition(const JsonValue& id, const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    const JsonValue* pos_val = params.get("position");
    if (!td || !pos_val) { send_error(id, -32602, "Missing textDocument or position"); return; }
    const JsonValue* uri = td->get("uri");
    if (!uri) { send_error(id, -32602, "Missing URI"); return; }

    Position pos;
    const JsonValue* line_val = pos_val->get("line");
    const JsonValue* char_val = pos_val->get("character");
    if (!line_val || !char_val) { send_error(id, -32602, "Invalid position"); return; }
    pos.line = static_cast<size_t>(line_val->as_int());
    pos.col = static_cast<size_t>(char_val->as_int());

    std::string uri_str = uri->as_string();
    auto it = m_documents.find(uri_str);
    if (it == m_documents.end()) { send_error(id, -32602, "Document not open"); return; }

    // Parse the document to extract symbols
    auto parse_result = parse_document(it->second);
    const auto& symbols = parse_result.symbols;

    // Tokenize and find the identifier at the cursor position
    std::vector<Token> toks;
    {
        g_lsp_mode = true;
        g_lsp_errors.clear();
        try {
            std::string filepath = uri_to_path(uri_str);
            std::vector<std::string> inc_dirs;
            PreprocessedResult pp = preprocess(it->second, filepath, inc_dirs);
            Tokenizer tokenizer(std::string(pp.expanded_source), filepath);
            toks = tokenizer.tokenize();
        } catch (const LSPAbort&) {}
        g_lsp_mode = false;
        g_lsp_errors.clear();
    }

    for (const auto& tok : toks) {
        if (position_matches(pos, tok) && tok.type == TokenType::ident && tok.value.has_value()) {
            const std::string& name = tok.value.value();
            if (auto* decl = find_decl(symbols, name)) {
                // Return the declaration location
                Position decl_pos = loc_to_position(decl->loc);
                JsonValue::ObjectType start_obj, end_obj;
                start_obj["line"] = static_cast<int64_t>(decl_pos.line);
                start_obj["character"] = static_cast<int64_t>(decl_pos.col);
                end_obj["line"] = static_cast<int64_t>(decl_pos.line);
                end_obj["character"] = static_cast<int64_t>(decl_pos.col + 1);

                JsonValue::ObjectType range_obj;
                range_obj["start"] = JsonValue(std::move(start_obj));
                range_obj["end"] = JsonValue(std::move(end_obj));

                JsonValue::ObjectType loc_obj;
                loc_obj["uri"] = uri_str;
                loc_obj["range"] = JsonValue(std::move(range_obj));

                send_response(id, JsonValue(std::move(loc_obj)));
                return;
            }
            break;
        }
    }

    // Not found
    send_response(id, JsonValue(nullptr));
}

// ── Document symbols ───────────────────────────────────────────────
void LSPServer::handle_document_symbol(const JsonValue& id, const JsonValue& params) {
    const JsonValue* td = params.get("textDocument");
    if (!td) { send_error(id, -32602, "Missing textDocument"); return; }
    const JsonValue* uri = td->get("uri");
    if (!uri) { send_error(id, -32602, "Missing URI"); return; }

    std::string uri_str = uri->as_string();
    auto it = m_documents.find(uri_str);
    if (it == m_documents.end()) { send_error(id, -32602, "Document not open"); return; }

    auto parse_result = parse_document(it->second);
    const auto& symbols = parse_result.symbols;

    JsonValue::ArrayType result;
    for (const auto& decl : symbols.all_decls) {
        // Only include top-level declarations (functions, globals, consts)
        // A more precise scope analysis would filter better, but for now
        // we include everything.
        Position p = loc_to_position(decl.loc);
        JsonValue::ObjectType start_obj, end_obj;
        start_obj["line"] = static_cast<int64_t>(p.line);
        start_obj["character"] = static_cast<int64_t>(p.col);
        end_obj["line"] = static_cast<int64_t>(p.line);
        end_obj["character"] = static_cast<int64_t>(p.col + 1);

        JsonValue::ObjectType range_obj;
        range_obj["start"] = JsonValue(std::move(start_obj));
        range_obj["end"] = JsonValue(std::move(end_obj));
        JsonValue range_value(std::move(range_obj));

        JsonValue::ObjectType sym;
        sym["name"] = decl.name;
        sym["kind"] = decl.symbol_kind;
        sym["detail"] = decl.detail;
        sym["range"] = range_value;
        sym["selectionRange"] = range_value;
        result.push_back(std::move(sym));
    }

    send_response(id, JsonValue(std::move(result)));
}

// ── Diagnostics (now includes codegen) ─────────────────────────────
void LSPServer::publish_diagnostics(const std::string& uri, const std::string& text) {
    // Extract file path from URI (strip "file://" prefix)
    std::string filepath = uri_to_path(uri);
    auto parse_result = parse_document(text, filepath);

    JsonValue::ArrayType diags;

    for (const auto& err : parse_result.errors) {
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

    JsonValue::ObjectType params;
    params["uri"] = uri;
    params["diagnostics"] = std::move(diags);
    send_notification("textDocument/publishDiagnostics", JsonValue(std::move(params)));
}

// ── Helpers ────────────────────────────────────────────────────────
LSPServer::Position LSPServer::loc_to_position(const SourceLoc& loc) {
    return { loc.line > 0 ? loc.line - 1 : 0, loc.col > 0 ? loc.col - 1 : 0 };
}

std::vector<Token> LSPServer::tokenize(const std::string& text, const std::string& filename) {
    g_lsp_mode = true;
    g_lsp_errors.clear();
    std::vector<Token> tokens;
    try {
        std::vector<std::string> inc_dirs;
        PreprocessedResult pp = preprocess(text, filename, inc_dirs);
        Tokenizer tokenizer(std::string(pp.expanded_source), filename);
        tokens = tokenizer.tokenize();
    } catch (const LSPAbort&) {}
    g_lsp_mode = false;
    g_lsp_errors.clear();
    return tokens;
}

std::string LSPServer::uri_to_path(const std::string& uri) {
    if (uri.rfind("file://", 0) == 0) {
        return uri.substr(7);
    }
    return uri;
}

bool LSPServer::position_matches(const Position& pos, const Token& tok) {
    if (tok.loc.line == 0) return false;
    size_t tok_line = tok.loc.line - 1;
    size_t tok_start_col = tok.loc.col - 1;
    size_t tok_len = 1;
    if (tok.value.has_value()) {
        tok_len = tok.value.value().size();
    } else {
        // Estimate keyword lengths for tokens without .value
        switch (tok.type) {
            case TokenType::_return:   tok_len = 6; break;
            case TokenType::let:       tok_len = 3; break;
            case TokenType::_if:       tok_len = 2; break;
            case TokenType::_else:     tok_len = 4; break;
            case TokenType::_while:    tok_len = 5; break;
            case TokenType::_for:      tok_len = 3; break;
            case TokenType::_print:    tok_len = 5; break;
            case TokenType::_function: tok_len = 8; break;
            case TokenType::_break:    tok_len = 5; break;
            case TokenType::_continue: tok_len = 8; break;
            case TokenType::_do:       tok_len = 2; break;
            case TokenType::_switch:   tok_len = 6; break;
            case TokenType::_case:     tok_len = 4; break;
            case TokenType::_const:    tok_len = 5; break;
            case TokenType::_default:  tok_len = 7; break;
            case TokenType::_global:   tok_len = 6; break;
            case TokenType::_static:   tok_len = 6; break;
            case TokenType::int_lit:   tok_len = 1; break;
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
