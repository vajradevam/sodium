// cyan-lsp — Language Server Protocol server for Cyan.
// Communicates via stdin/stdout.
//
// Usage:
//   cyan-lsp
//
// Then connect your editor's LSP client to this process.

#include "lsp.hpp"

int main() {
    LSPServer server;
    server.run();
    return 0;
}
