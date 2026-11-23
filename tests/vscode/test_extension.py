#!/usr/bin/env python3
"""Static checks for the VS Code extension (D3c): grammar and client config."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXT = ROOT / "vscode-extension"


def main():
    ok = True

    def check(cond, label):
        nonlocal ok
        if not cond:
            print("VS Code extension: FAIL - " + label)
            ok = False

    grammar = json.loads((EXT / "syntaxes" / "cyan.tmLanguage.json").read_text())
    patterns = [p.get("include") for p in grammar["patterns"]]
    repo = grammar["repository"]
    check("#preprocessor" in patterns, "preprocessor pattern not included")
    check("#numbers" in patterns and "#types" in patterns, "numbers/types not included")

    types = repo["types"]["patterns"][0]["match"]
    for ty in ("f32", "f64", "bool", "char", "i64", "u8"):
        check(ty in types, "missing type %s in grammar" % ty)

    keywords = repo["keywords"]["patterns"][0]["match"]
    for kw in ("function", "struct", "enum", "asm", "as", "type", "global", "static"):
        check(kw in keywords, "missing keyword %s in grammar" % kw)
    constants = repo["keywords"]["patterns"][1]["match"]
    check("null" in constants, "missing null literal in grammar")

    numbers = " ".join(p["match"] for p in repo["numbers"]["patterns"])
    check("[eE]" in numbers and "[fF]" in numbers, "float/exponent literals not highlighted")
    check("0[xX]" in numbers, "hex literals not highlighted")

    pkg = json.loads((EXT / "package.json").read_text())
    props = pkg["contributes"]["configuration"]["properties"]
    check("cyan.lspPath" in props, "cyan.lspPath setting not declared")
    client = pkg["dependencies"].get("vscode-languageclient")
    check(client is not None, "vscode-languageclient dependency missing")

    js = (EXT / "extension.js").read_text()
    check("LanguageClient" in js, "extension.js does not create a LanguageClient")
    check("myc" not in js, "hardcoded development path still present")
    check("lspPath" in js, "extension.js ignores the cyan.lspPath setting")

    print("VS Code extension: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
