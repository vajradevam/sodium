#!/bin/bash
# Regression test for T6: output paths must be treated as literal names, never
# as shell input. Covers spaces, semicolons, and an explicit injection attempt.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/paths.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/paths.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
printf 'function main() { return 42; }\n' > "$WORK/p.cyan"

fail=0
for target in x86_64 riscv64; do
    tflag=()
    runner=()
    if [ "$target" = "riscv64" ]; then
        tflag=(--target riscv64)
        runner=(qemu-riscv64)
    fi
    dir="$WORK/dir with space;semi"
    if ! "$COMPILER" "${tflag[@]}" -o "$dir/prog" "$WORK/p.cyan"; then
        echo "compile failed [$target]"
        fail=1
        continue
    fi
    "${runner[@]}" "$dir/prog"
    rc=$?
    if [ "$rc" -ne 42 ]; then
        echo "wrong exit [$target]: $rc"
        fail=1
    fi
    if ! "$COMPILER" "${tflag[@]}" -S -o "$dir/out.asm" "$WORK/p.cyan" || [ ! -s "$dir/out.asm" ]; then
        echo "-S output failed [$target]"
        fail=1
    fi
done

marker="$WORK/PWNED"
"$COMPILER" -o "$WORK/out; touch $marker" "$WORK/p.cyan"
if [ -e "$marker" ]; then
    echo "shell injection: marker file was created"
    fail=1
fi

echo "Path handling: $([ "$fail" -eq 0 ] && echo OK || echo FAILED)"
exit "$fail"
