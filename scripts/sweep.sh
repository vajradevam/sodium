#!/bin/bash
# Regression sweep: compile every test file with --emit-ir, -S and --verify-ir
# on both targets.
# A diagnostic (exit 1) is expected for compile-fail tests; a crash or hang is not.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/sweep.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/sweep.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

count=0
crashes=0
for path in $(find tests -type f -name '*.cyan' | sort); do
    for target in x86_64 riscv64; do
        tflag=()
        [ "$target" = "riscv64" ] && tflag=(--target riscv64)
        for mode in ir asm verify; do
            mflag=(-S)
            [ "$mode" = "ir" ] && mflag=(--emit-ir -S)
            [ "$mode" = "verify" ] && mflag=(--verify-ir -S)
            count=$((count + 1))
            timeout 10 "$COMPILER" "${tflag[@]}" "${mflag[@]}" -I tests/include \
                -o "$WORK/out.s" "$path" > /dev/null 2> "$WORK/err"
            rc=$?
            if [ "$rc" -gt 1 ]; then
                echo "CRASH: $path [$target/$mode] exit $rc"
                head -3 "$WORK/err"
                crashes=$((crashes + 1))
            fi
            # Verifier failures exit with the normal compile-error status, so
            # detect them by their message instead of by the exit code.
            if grep -q "IR verification failed" "$WORK/err"; then
                echo "VERIFY FAIL: $path [$target/$mode]"
                head -5 "$WORK/err"
                crashes=$((crashes + 1))
            fi
        done
    done
done

echo "Sweep: $count invocations, $crashes crashes"
[ "$crashes" -eq 0 ]
