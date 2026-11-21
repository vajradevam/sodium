#!/bin/bash
# Differential oracle: every tests/oracle/<name>.cyan has a hand-written C twin
# <name>.c. Both are compiled and run; stdout and exit code must match exactly.
# The C twin is built once natively — these programs use only 64-bit integers,
# so its output is target-independent and can be compared on every Cyan target.
# Usage: scripts/oracle.sh [x86_64|riscv64|both]  (default: both)
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
CC="${CC:-gcc}"
case "${1:-both}" in
    x86_64) TARGETS="x86_64" ;;
    riscv64) TARGETS="riscv64" ;;
    both) TARGETS="x86_64 riscv64" ;;
    *) echo "usage: $0 [x86_64|riscv64|both]" >&2; exit 2 ;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/oracle.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/oracle.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
count=0
for cyan in $(find tests/oracle -type f -name '*.cyan' | sort); do
    c_source="${cyan%.cyan}.c"
    name="$(basename "$cyan" .cyan)"
    if [ ! -f "$c_source" ]; then
        echo "MISSING TWIN: $c_source"
        fail=$((fail + 1))
        continue
    fi
    if ! "$CC" -O0 -std=c11 -o "$WORK/c.bin" "$c_source" 2> "$WORK/c.compile"; then
        echo "C COMPILE FAIL: $c_source"
        cat "$WORK/c.compile"
        fail=$((fail + 1))
        continue
    fi
    "$WORK/c.bin" > "$WORK/c.out" 2>&1
    c_rc=$?

    for target in $TARGETS; do
        count=$((count + 1))
        tflag=()
        runner=()
        if [ "$target" = "riscv64" ]; then
            tflag=(--target riscv64)
            runner=(qemu-riscv64)
        fi
        if ! "$COMPILER" "${tflag[@]}" -o "$WORK/cyan.bin" "$cyan" > "$WORK/cyan.compile" 2>&1; then
            echo "CYAN COMPILE FAIL: $cyan [$target]"
            cat "$WORK/cyan.compile"
            fail=$((fail + 1))
            continue
        fi
        "${runner[@]}" "$WORK/cyan.bin" > "$WORK/cyan.out" 2>&1
        cy_rc=$?
        if [ "$c_rc" -eq "$cy_rc" ] && cmp -s "$WORK/c.out" "$WORK/cyan.out"; then
            pass=$((pass + 1))
        else
            echo "MISMATCH: $name [$target] exit C=$c_rc Cyan=$cy_rc"
            diff -u "$WORK/c.out" "$WORK/cyan.out" | head -20
            fail=$((fail + 1))
        fi
    done
done

echo "Oracle: $count comparisons, $pass matched, $fail mismatched"
[ "$fail" -eq 0 ]
