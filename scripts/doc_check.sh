#!/bin/bash
# D1: verify every ```cyan fenced block in README.md is a complete, compilable
# Cyan program. Each sample is compiled for x86_64 and riscv64 and run on the
# native target (stdout ignored). This keeps the README honest: prose claims
# are backed by samples the compiler accepts.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/doccheck.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/doccheck.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

fail() { echo "doc_check: $*" >&2; exit 1; }

# Extract fenced ```cyan blocks into numbered files. A block ends at a line
# containing only ```.
awk '
    /^```cyan[[:space:]]*$/ { inblock=1; n++; next }
    inblock && /^```[[:space:]]*$/ { inblock=0; next }
    inblock { print > (out "/sample_" n ".cyan") }
' out="$WORK" README.md

samples=$(find "$WORK" -maxdepth 1 -name 'sample_*.cyan' | sort)
[ -n "$samples" ] || fail 'README.md contains no cyan code samples'

count=0
for sample in $samples; do
    count=$((count + 1))
    name=$(basename "$sample" .cyan)
    if ! "$COMPILER" -S -I cyanlib -I tests/include -o "$WORK/$name.x86.s" "$sample" \
        > "$WORK/$name.log" 2>&1; then
        fail "$name failed to compile for x86_64: $(head -3 "$WORK/$name.log")"
    fi
    if ! "$COMPILER" --target riscv64 -S -I cyanlib -I tests/include -o "$WORK/$name.rv.s" "$sample" \
        > "$WORK/$name.rv.log" 2>&1; then
        fail "$name failed to compile for riscv64: $(head -3 "$WORK/$name.rv.log")"
    fi
    if ! "$COMPILER" -I cyanlib -I tests/include -o "$WORK/$name.bin" "$sample" \
        > "$WORK/$name.run.log" 2>&1; then
        fail "$name failed to link: $(head -3 "$WORK/$name.run.log")"
    fi
    timeout 5 "$WORK/$name.bin" < /dev/null > /dev/null 2>&1
    rc=$?
    [ "$rc" -eq 0 ] || fail "$name exited with $rc when run"
done

echo "Doc check: $count README samples compiled (x86_64 + riscv64) and ran successfully"
