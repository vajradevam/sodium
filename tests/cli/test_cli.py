#!/usr/bin/env python3
"""Scripted CLI checks for --version and --help (D4a)."""

import re
import subprocess
import sys
from pathlib import Path

SODIUM = sys.argv[1] if len(sys.argv) > 1 else "./build/sodium"
ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    return subprocess.run([SODIUM, *args], capture_output=True, text=True)


def main():
    ok = True

    def check(cond, label):
        nonlocal ok
        if not cond:
            print("CLI: FAIL - " + label)
            ok = False

    version_header = (ROOT / "src" / "version.hpp").read_text()
    version = re.search(r'SODIUM_VERSION\s+"([^"]+)"', version_header).group(1)

    result = run("--version")
    check(result.returncode == 0, "--version exit code")
    check(result.stdout.strip() == "sodium " + version, "--version output")
    check(result.stderr == "", "--version should not write stderr")

    for flag in ("--help", "-h"):
        result = run(flag)
        check(result.returncode == 0, "%s exit code" % flag)
        text = result.stdout
        for section in ("NAME", "SYNOPSIS", "DESCRIPTION", "OPTIONS", "EXAMPLES", "SEE ALSO"):
            check(section in text, "%s missing section %s" % (flag, section))
        for option in ("--target", "--bounds", "--check-null", "--stack-probe",
                       "--verify-ir", "--emit-ir", "--dump-regalloc", "-g", "fmt",
                       "--version", "--help"):
            check(option in text, "%s missing option %s" % (flag, option))
        check("sodium " + version in text, "%s missing version" % flag)

    print("CLI: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
