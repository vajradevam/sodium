#!/usr/bin/env python3
"""Scripted LSP session: rename, find-references and diagnostics lifecycle (D3a)."""

import json
import subprocess
import sys

LSP = sys.argv[1] if len(sys.argv) > 1 else "./build/cyan-lsp"
URI = "file:///tmp/cyan_lsp_features.cyan"


def send(proc, obj):
    body = json.dumps(obj).encode("utf-8")
    proc.stdin.write(b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
    proc.stdin.flush()


def recv(proc):
    length = None
    while True:
        line = proc.stdout.readline()
        if not line:
            return None
        if line.startswith(b"Content-Length:"):
            length = int(line.split(b":", 1)[1].strip())
        if line in (b"\r\n", b"\n"):
            break
    if length is None:
        return None
    return json.loads(proc.stdout.read(length))


def recv_until(proc, predicate, limit=40):
    for _ in range(limit):
        msg = recv(proc)
        if msg is None:
            break
        if predicate(msg):
            return msg
    return None


def request(proc, ident, method, params):
    send(proc, {"jsonrpc": "2.0", "id": ident, "method": method, "params": params})
    return recv_until(proc, lambda m: m.get("id") == ident)


SOURCES = (
    'function main() {\n'
    '    var value = 1;\n'
    '    var total = value + value;\n'
    '    return(total);\n'
    '}\n'
)


def main():
    proc = subprocess.Popen([LSP], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    ok = True

    def check(cond, label):
        nonlocal ok
        if not cond:
            print("LSP features: FAIL - " + label)
            ok = False

    try:
        init = request(proc, 1, "initialize", {})
        check(init is not None, "no initialize response")
        caps = (init or {}).get("result", {}).get("capabilities", {})
        check(caps.get("referencesProvider") is True, "referencesProvider not advertised")
        check(caps.get("renameProvider") is True, "renameProvider not advertised")

        send(proc, {"jsonrpc": "2.0", "method": "textDocument/didOpen",
                    "params": {"textDocument": {"uri": URI, "languageId": "cyan",
                                                "version": 1, "text": SOURCES}}})
        recv_until(proc, lambda m: m.get("method") == "textDocument/publishDiagnostics")

        # `value` occurs on line 1 (declaration) and line 2 (two uses).
        pos = {"line": 2, "character": 17}
        refs = request(proc, 2, "textDocument/references", {
            "textDocument": {"uri": URI}, "position": pos,
            "context": {"includeDeclaration": True}})
        locations = (refs or {}).get("result") or []
        check(len(locations) == 3, "expected 3 references, got %d" % len(locations))
        check(all(loc.get("uri") == URI for loc in locations), "wrong reference URI")

        no_decl = request(proc, 3, "textDocument/references", {
            "textDocument": {"uri": URI}, "position": pos,
            "context": {"includeDeclaration": False}})
        without = (no_decl or {}).get("result") or []
        check(len(without) == 2, "expected 2 uses without declaration, got %d" % len(without))

        renamed = request(proc, 4, "textDocument/rename", {
            "textDocument": {"uri": URI}, "position": pos, "newName": "renamed"})
        changes = ((renamed or {}).get("result") or {}).get("changes", {})
        edits = changes.get(URI, [])
        check(len(edits) == 3, "expected 3 rename edits, got %d" % len(edits))
        check(all(e.get("newText") == "renamed" for e in edits), "wrong rename text")

        # Closing a document clears its diagnostics.
        send(proc, {"jsonrpc": "2.0", "method": "textDocument/didClose",
                    "params": {"textDocument": {"uri": URI}}})
        cleared = recv_until(proc, lambda m: m.get("method") == "textDocument/publishDiagnostics"
                             and m.get("params", {}).get("uri") == URI
                             and m.get("params", {}).get("diagnostics") == [])
        check(cleared is not None, "didClose did not clear diagnostics")

        send(proc, {"jsonrpc": "2.0", "id": 5, "method": "shutdown", "params": None})
        send(proc, {"jsonrpc": "2.0", "method": "exit", "params": None})
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("LSP features: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
