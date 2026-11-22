#!/usr/bin/env python3
"""Scripted LSP session: multiple diagnostics with correct severities (E5)."""

import json
import subprocess
import sys

LSP = sys.argv[1] if len(sys.argv) > 1 else "./build/cyan-lsp"
URI = "file:///tmp/cyan_lsp_diagnostics.cyan"


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


def recv_until(proc, predicate, limit=30):
    for _ in range(limit):
        msg = recv(proc)
        if msg is None:
            break
        if predicate(msg):
            return msg
    return None


def check(cond, label):
    if not cond:
        print("LSP diagnostics: FAIL - " + label)
        return False
    return True


def main():
    source = (
        'function main() {\n'
        '    var unused = 1;\n'
        '    print(nope);\n'
        '    print(also_nope);\n'
        '    return(0);\n'
        '}\n'
    )
    proc = subprocess.Popen([LSP], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    ok = True
    try:
        send(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        resp = recv_until(proc, lambda m: m.get("id") == 1)
        ok &= check(resp is not None, "no initialize response")

        send(proc, {"jsonrpc": "2.0", "method": "textDocument/didOpen",
                    "params": {"textDocument": {"uri": URI, "languageId": "cyan",
                                                "version": 1, "text": source}}})
        note = recv_until(proc, lambda m: m.get("method") == "textDocument/publishDiagnostics")
        ok &= check(note is not None, "no publishDiagnostics notification")
        if note is None:
            return 1
        diags = note.get("params", {}).get("diagnostics", [])
        print("LSP diagnostics received: " + str(len(diags)))
        for d in diags:
            print("  severity %s: %s" % (d.get("severity"), d.get("message")))

        errors = [d for d in diags if d.get("severity") == 1]
        warnings = [d for d in diags if d.get("severity") == 2]
        ok &= check(len(errors) >= 2, "expected >=2 errors, got %d" % len(errors))
        ok &= check(any("Undeclared identifier" in d.get("message", "") for d in errors),
                    "no undeclared-identifier error")
        ok &= check(any("unused variable" in d.get("message", "") for d in warnings),
                    "no warning severity (unused variable)")
        ok &= check(all("range" in d and "start" in d["range"] for d in diags),
                    "a diagnostic is missing its range")

        send(proc, {"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None})
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
    print("LSP diagnostics: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
