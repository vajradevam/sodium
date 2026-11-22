#!/bin/bash
# O6: measure register-allocation spills / callee-save use on a fixed set of
# programs at -O1 on both targets. Prints a single summary line.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/regalloc.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/regalloc.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

CASES=(
    tests/unit/test_large_array.cyan
    tests/unit/test_hashmap.cyan
    tests/unit/test_containers.cyan
    tests/unit/test_fmt_parse.cyan
    tests/integration/test_comprehensive.cyan
)

spills=0
callee=0
functions=0
for path in "${CASES[@]}"; do
    for target in x86_64 riscv64; do
        tflag=()
        [ "$target" = "riscv64" ] && tflag=(--target riscv64)
        "$COMPILER" -O1 --dump-regalloc -S "${tflag[@]}" -I tests/include -I cyanlib \
            -o "$WORK/out.s" "$path" > /dev/null 2> "$WORK/log"
        if ! grep -q '^regalloc ' "$WORK/log"; then
            echo "bench: no allocation stats for $path [$target]" >&2
            exit 1
        fi
        s=$(grep -o 'spills=[0-9]*' "$WORK/log" | cut -d= -f2 | awk '{ n += $1 } END { print n+0 }')
        c=$(grep -o 'callee=[0-9]*' "$WORK/log" | cut -d= -f2 | awk '{ n += $1 } END { print n+0 }')
        f=$(grep -c '^regalloc ' "$WORK/log")
        spills=$((spills + s))
        callee=$((callee + c))
        functions=$((functions + f))
    done
done

echo "Regalloc benchmark: $functions functions, $spills spills, $callee callee-save assignments"
