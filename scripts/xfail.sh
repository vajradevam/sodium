#!/bin/bash
# Run every test in tests/xfail/ and report which ones still fail.
# These are known compiler bugs with the output they *should* produce; they are
# excluded from scripts/run_tests.sh so check.sh stays green. This script is
# deliberately NOT part of check.sh. Run it after a compiler fix to see which
# entries can be moved back into the normal suite.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
GREEN='\033[0;32m'
RED='\033[0;31m'
DIM='\033[2m'
NC='\033[0m'

WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/cyan_xfail.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/cyan_xfail.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

parse_headers() {
    EXPECT_EXIT=""; EXPECT_ERROR=""; COMPILE_FLAGS=""; TARGETS_VAL=""; STDOUT_LINES=()
    local line
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            "// EXPECT-EXIT: "*)   EXPECT_EXIT="${line#// EXPECT-EXIT: }" ;;
            "// EXPECT-EXIT:")     EXPECT_EXIT="0" ;;
            "// EXPECT-ERROR: "*)  EXPECT_ERROR="${line#// EXPECT-ERROR: }" ;;
            "// COMPILE-FLAGS: "*) COMPILE_FLAGS="${line#// COMPILE-FLAGS: }" ;;
            "// TARGETS: "*)       TARGETS_VAL="${line#// TARGETS: }" ;;
            "// EXPECT-STDOUT: "*) STDOUT_LINES+=("${line#// EXPECT-STDOUT: }") ;;
            "// EXPECT-STDOUT:")   STDOUT_LINES+=("") ;;
        esac
    done < "$1"
}

if ! command -v qemu-riscv64 >/dev/null 2>&1; then
    echo "note: qemu-riscv64 not found; riscv64 xfails will be skipped"
    HAVE_RV=0
else
    HAVE_RV=1
fi

still=0; nowpass=0; cases=0
for path in $(find tests/xfail -type f -name '*.cyan' | sort); do
    parse_headers "$path"
    local_targets="${TARGETS_VAL:-x86_64 riscv64}"
    for target in $local_targets; do
        if [ "$target" = "riscv64" ] && [ "$HAVE_RV" -eq 0 ]; then continue; fi
        cases=$((cases + 1))
        base="$WORK/case_$cases"
        tflag=(); [ "$target" = "riscv64" ] && tflag=(--target riscv64)
        cflags=(); [ -n "$COMPILE_FLAGS" ] && read -r -a cflags <<< "$COMPILE_FLAGS"

        ( timeout 10 "$COMPILER" "${cflags[@]}" "${tflag[@]}" -I tests/xfail -I tests/include \
            -o "$base.bin" "$path" > "$base.compile.log" 2>&1 ) 2>/dev/null
        cc=$?
        reason=""
        if [ -n "$EXPECT_ERROR" ]; then
            if [ "$cc" -eq 0 ]; then
                reason="compiled successfully (expected diagnostic '$EXPECT_ERROR')"
            elif ! grep -Fq -- "$EXPECT_ERROR" "$base.compile.log"; then
                reason="diagnostic mismatch: $(head -1 "$base.compile.log")"
            fi
        elif [ "$cc" -ne 0 ]; then
            reason="compile failed: $(head -1 "$base.compile.log")"
        else
            runner=("$base.bin")
            [ "$target" = "riscv64" ] && runner=(qemu-riscv64 "$base.bin")
            timeout 10 "${runner[@]}" < /dev/null > "$base.stdout" 2> "$base.stderr"
            actual_exit=$?
            want_exit="$EXPECT_EXIT"; [ -z "$want_exit" ] && want_exit=0
            if [ "$actual_exit" -ne "$want_exit" ]; then
                reason="exit $actual_exit, expected $want_exit"
            elif [ "${#STDOUT_LINES[@]}" -gt 0 ]; then
                printf '%s\n' "${STDOUT_LINES[@]}" > "$base.expected"
                if ! cmp -s "$base.expected" "$base.stdout"; then
                    reason="stdout: got [$(paste -sd'|' "$base.stdout")], expected [$(paste -sd'|' "$base.expected")]"
                fi
            fi
        fi

        if [ -z "$reason" ]; then
            nowpass=$((nowpass + 1))
            echo -e "${GREEN}[NOW PASSES]${NC} $target: $path — remove from xfail and add to the normal suite"
        else
            still=$((still + 1))
            echo -e "${RED}[STILL FAILING]${NC} $target: $path — $reason"
        fi
    done
done

echo "========================================"
echo "xfail: $cases cases, $still still failing (expected), $nowpass now passing"
echo "Run this script after a compiler fix; a 'now passing' entry should be moved."
echo "========================================"
exit 0
