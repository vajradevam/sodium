#!/usr/bin/env python3
"""Scripted LSP session: signature help and semantic tokens (D3b)."""

import json
import subprocess
import sys

LSP = sys.argv[1] if len(sys.argv) > 1 else "./build/cyan-lsp"
URI = "file:///tmp/cyan_lsp_signature.cyan"


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


def request(proc, ident, method, params):
    send(proc, {"jsonrpc": "2.0", "id": ident, "method": method, "params": params})
    for _ in range(40):
        msg = recv(proc)
        if msg is not None and msg.get("id") == ident:
            return msg
    return None


SOURCE = (
    'function add(a: i64, b: i64) -> i64 {\n'
    '    return a + b;\n'
    '}\n'
    'function main() {\n'
    '    var x: i64 = add(1, 2);\n'
    '    print("%d", x);\n'
    '    return 0;\n'
    '}\n'
)


def main():
    proc = subprocess.Popen([LSP], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    ok = True

    def check(cond, label):
        nonlocal ok
        if not cond:
            print("LSP signature/tokens: FAIL - " + label)
            ok = False

    try:
        init = request(proc, 1, "initialize", {})
        caps = (init or {}).get("result", {}).get("capabilities", {})
        check(caps.get("signatureHelpProvider") is not None, "signatureHelpProvider missing")
        check(caps.get("semanticTokensProvider") is not None, "semanticTokensProvider missing")

        send(proc, {"jsonrpc": "2.0", "method": "textDocument/didOpen",
                    "params": {"textDocument": {"uri": URI, "languageId": "cyan",
                                                "version": 1, "text": SOURCE}}})
        for _ in range(40):
            msg = recv(proc)
            if msg is not None and msg.get("method") == "textDocument/publishDiagnostics":
                break

        # Cursor on the second argument of add(1, 2): line 4 (0-based), col 22.
        sig = request(proc, 2, "textDocument/signatureHelp", {
            "textDocument": {"uri": URI}, "position": {"line": 4, "character": 22}})
        result = (sig or {}).get("result") or {}
        signatures = result.get("signatures") or []
        check(len(signatures) == 1, "expected 1 signature, got %d" % len(signatures))
        if signatures:
            check(signatures[0].get("label") == "add(a, b)",
                  "wrong signature label: %r" % signatures[0].get("label"))
            params = signatures[0].get("parameters") or []
            check(len(params) == 2, "expected 2 parameters, got %d" % len(params))
        check(result.get("activeParameter") == 1, "activeParameter should be 1")

        tokens = request(proc, 3, "textDocument/semanticTokens/full", {
            "textDocument": {"uri": URI}})
        data = ((tokens or {}).get("result") or {}).get("data") or []
        check(len(data) > 0 and len(data) % 5 == 0, "invalid semantic token data")
        types = set(data[i + 3] for i in range(0, len(data), 5))
        check(0 in types, "no keyword semantic tokens")
        check(3 in types, "no type semantic tokens")
        check(4 in types, "no number semantic tokens")
        check(6 in types, "no function semantic tokens")

        send(proc, {"jsonrpc": "2.0", "id": 4, "method": "shutdown", "params": None})
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

    print("LSP signature/tokens: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
