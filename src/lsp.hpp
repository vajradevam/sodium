#pragma once

#include <string>
#include <vector>
#include <map>

#include "json.hpp"
#include "tokenization.hpp"

// LSP Server class.
// Communicates via stdin/stdout using the Language Server Protocol.
class LSPServer {
public:
    LSPServer();
    ~LSPServer() = default;

    // Run the main loop: read requests, dispatch, write responses.
    void run();

private:
    bool m_running = true;

    // Open documents: uri → source text
    std::map<std::string, std::string> m_documents;

    // ── I/O helpers ───────────────────────────────────────────────────
    // Read one LSP message from stdin (Content-Length header + JSON body).
    // Returns the parsed JSON body (the "params" field is at top level after unwrap).
    // Returns null value on EOF.
    JsonValue read_message();

    // Send a JSON-RPC response (result or error).
    void send_response(const JsonValue& id, const JsonValue& result);
    void send_error(const JsonValue& id, int code, const std::string& msg);
    void send_notification(const std::string& method, const JsonValue& params);

    // Write raw data to stdout with Content-Length framing.
    void write_message(const std::string& body);

    // ── Method handlers ───────────────────────────────────────────────
    void handle_initialize(const JsonValue& id, const JsonValue& params);
    void handle_shutdown(const JsonValue& id);
    void handle_text_document_did_open(const JsonValue& params);
    void handle_text_document_did_change(const JsonValue& params);
    void handle_text_document_did_close(const JsonValue& params);
    void handle_completion(const JsonValue& id, const JsonValue& params);
    void handle_hover(const JsonValue& id, const JsonValue& params);
    void handle_definition(const JsonValue& id, const JsonValue& params);
    void handle_document_symbol(const JsonValue& id, const JsonValue& params);

    // ── Diagnostics ───────────────────────────────────────────────────
    void publish_diagnostics(const std::string& uri, const std::string& text);

    // ── Helpers ───────────────────────────────────────────────────────
    // Extract a Position {line, character} from a JSON object.
    struct Position { size_t line; size_t col; };

    // Convert Compiler SourceLoc to LSP Position (0-based).
    static Position loc_to_position(const SourceLoc& loc);

    // Tokenize a source text and return all tokens (for completions etc.)
    std::vector<Token> tokenize(const std::string& text);

    // Check if a token at the given position matches.
    static bool position_matches(const Position& pos, const Token& tok);

    // Keywords that can be completed.
    static const std::vector<std::string>& keywords();
};
