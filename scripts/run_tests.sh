#!/bin/bash
# Discoverable test runner — see D-005 in docs/DECISIONS.md.
# Every tests/**/*.cyan is a test; expectations come from // EXPECT-* headers.
# Usage: scripts/run_tests.sh [--filter SUBSTR] [--target NAME] [-j N] [--self-test] [-h]

COMPILER="${COMPILER:-./build/sodium}"
FILTER=""
TARGET="all"
JOBS=1
SELF_TEST=0

GREEN='\033[0;32m'
RED='\033[0;31m'
DIM='\033[2m'
NC='\033[0m'

usage() {
    cat <<'EOF'
Usage: scripts/run_tests.sh [options]
  --filter SUBSTR     run only tests whose path contains SUBSTR
  --target NAME       x86_64, riscv64, or all (default: all)
  -j N                run up to N cases in parallel (default: 1)
  --self-test         verify the harness rejects deliberately wrong expectations
  -h, --help          show this help

Tests are discovered under tests/**/*.cyan. Expectations are declared in
// EXPECT-* header comments (docs/DECISIONS.md, D-005). Warning tests may use
// EXPECT-WARNING, NO-WARNING and COMPILE-FLAGS (e.g. -w, -Werror).
EOF
}

need_arg() {
    if [ "$#" -lt 2 ]; then
        echo "error: $1 requires an argument" >&2
        usage >&2
        exit 2
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --filter) need_arg "$@"; FILTER="$2"; shift 2 ;;
        --target) need_arg "$@"; TARGET="$2"; shift 2 ;;
        -j)       need_arg "$@"; JOBS="$2"; shift 2 ;;
        --self-test) SELF_TEST=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

case "$TARGET" in
    all|x86_64|riscv64) ;;
    *) echo "error: invalid --target '$TARGET' (want all, x86_64, riscv64)" >&2; exit 2 ;;
esac
case "$JOBS" in
    ''|*[!0-9]*) echo "error: -j must be a positive integer" >&2; exit 2 ;;
esac
[ "$JOBS" -ge 1 ] || { echo "error: -j must be a positive integer" >&2; exit 2; }

RV_OK=1
if [ "$TARGET" != "x86_64" ]; then
    if ! command -v qemu-riscv64 >/dev/null 2>&1 || \
       ! command -v riscv64-elf-gcc >/dev/null 2>&1; then
        RV_OK=0
        echo "  --- RISC-V backend: SKIPPED (qemu-riscv64 / riscv64-elf-gcc not found) ---"
    fi
fi

TMPBASE="${TMPDIR:-/tmp/opencode}"
WORK=$(mktemp -d "$TMPBASE/cyan_tests.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/cyan_tests.XXXXXX)
mkdir -p "$WORK/results"
trap 'rm -rf "$WORK"' EXIT

# ── header parsing ─────────────────────────────────────────────────
EXPECT_EXIT=""
EXPECT_ERROR=""
EXPECT_WARNING=""
NO_WARNING=""
COMPILE_FLAGS=""
STDIN_VAL=""
ARGS_VAL=""
TARGETS_VAL=""
SKIP_REASON=""
ENV_VALS=()
STDOUT_LINES=()

parse_headers() {
    EXPECT_EXIT=""; EXPECT_ERROR=""; EXPECT_WARNING=""; NO_WARNING=""
    COMPILE_FLAGS=""; STDIN_VAL=""; ARGS_VAL=""
    TARGETS_VAL=""; SKIP_REASON=""; ENV_VALS=(); STDOUT_LINES=()
    local line
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            "// EXPECT-EXIT: "*)   EXPECT_EXIT="${line#// EXPECT-EXIT: }" ;;
            "// EXPECT-EXIT:")     EXPECT_EXIT="0" ;;
            "// EXPECT-ERROR: "*)  EXPECT_ERROR="${line#// EXPECT-ERROR: }" ;;
            "// EXPECT-WARNING: "*) EXPECT_WARNING="${line#// EXPECT-WARNING: }" ;;
            "// NO-WARNING: "*)    NO_WARNING="${line#// NO-WARNING: }" ;;
            "// COMPILE-FLAGS: "*) COMPILE_FLAGS="${line#// COMPILE-FLAGS: }" ;;
            "// STDIN: "*)         STDIN_VAL="${line#// STDIN: }" ;;
            "// ARGS: "*)          ARGS_VAL="${line#// ARGS: }" ;;
            "// ENV: "*)           ENV_VALS+=("${line#// ENV: }") ;;
            "// TARGETS: "*)       TARGETS_VAL="${line#// TARGETS: }" ;;
            "// SKIP-TEST: "*)     SKIP_REASON="${line#// SKIP-TEST: }" ;;
            "// EXPECT-STDOUT: "*) STDOUT_LINES+=("${line#// EXPECT-STDOUT: }") ;;
            "// EXPECT-STDOUT:")   STDOUT_LINES+=("") ;;
        esac
    done < "$1"
}

# ── one case ───────────────────────────────────────────────────────
CASES=()

run_case() {
    trap - EXIT
    local idx="$1"
    local spec="${CASES[$idx]}"
    local cat target path
    IFS='|' read -r cat target path <<< "$spec"

    local base="$WORK/case_$idx"
    local result="$WORK/results/$idx"
    local emit
    emit() {
        local reason="${2//|//}"
        reason="${reason//$'\n'/ }"
        printf '%s|%s|%s|%s|%s\n' "$1" "$cat" "$target" "$path" "$reason" > "$result"
    }

    parse_headers "$path"

    if [ -n "$SKIP_REASON" ]; then
        emit SKIP "$SKIP_REASON"
        return
    fi
    if [ -z "$EXPECT_ERROR" ] && [ -z "$EXPECT_EXIT" ] && [ -z "$EXPECT_WARNING" ]; then
        emit FAIL "no EXPECT-EXIT, EXPECT-ERROR or EXPECT-WARNING header"
        return
    fi
    case "${path##*/}" in
        test_bad_*)
            if [ -z "$EXPECT_ERROR" ]; then
                emit FAIL "test_bad_* must declare EXPECT-ERROR"
                return
            fi ;;
    esac

    local tflag=()
    [ "$target" = "riscv64" ] && tflag=(--target riscv64)
    local cflags=()
    [ -n "$COMPILE_FLAGS" ] && read -r -a cflags <<< "$COMPILE_FLAGS"

    "$COMPILER" "${cflags[@]}" "${tflag[@]}" -I tests/include -o "$base.bin" "$path" \
        > "$base.compile.log" 2>&1
    local cc=$?

    if [ -n "$NO_WARNING" ] && grep -Fq -- "$NO_WARNING" "$base.compile.log"; then
        emit FAIL "unexpected warning/diagnostic: $NO_WARNING"
        return
    fi

    if [ -n "$EXPECT_WARNING" ]; then
        if [ "$cc" -ne 0 ]; then
            emit FAIL "compile failed: $(head -1 "$base.compile.log")"
            return
        fi
        if ! grep -Fq "warning:" "$base.compile.log"; then
            emit FAIL "no warning diagnostic emitted"
            return
        fi
        if ! grep -Fq -- "$EXPECT_WARNING" "$base.compile.log"; then
            emit FAIL "warning did not contain: $EXPECT_WARNING"
            return
        fi
        emit PASS ""
        return
    fi

    if [ -n "$EXPECT_ERROR" ]; then
        if [ "$cc" -eq 0 ]; then
            emit FAIL "compiled successfully (expected error)"
            return
        fi
        if ! grep -Fq -- "$EXPECT_ERROR" "$base.compile.log"; then
            emit FAIL "diagnostic did not contain: $EXPECT_ERROR"
            return
        fi
        emit PASS ""
        return
    fi

    if [ "$cc" -ne 0 ]; then
        emit FAIL "compile failed: $(head -1 "$base.compile.log")"
        return
    fi

    local runner=("$base.bin")
    [ "$target" = "riscv64" ] && runner=(qemu-riscv64 "$base.bin")
    local args=()
    [ -n "$ARGS_VAL" ] && read -r -a args <<< "$ARGS_VAL"
    local envcmd=()
    [ ${#ENV_VALS[@]} -gt 0 ] && envcmd=(env "${ENV_VALS[@]}")

    if [ -n "$STDIN_VAL" ]; then
        printf '%s\n' "$STDIN_VAL" > "$base.stdin"
        "${envcmd[@]}" "${runner[@]}" "${args[@]}" < "$base.stdin" > "$base.stdout" 2> "$base.stderr"
    else
        "${envcmd[@]}" "${runner[@]}" "${args[@]}" < /dev/null > "$base.stdout" 2> "$base.stderr"
    fi
    local actual_exit=$?

    local want_exit="$EXPECT_EXIT"
    [ -z "$want_exit" ] && want_exit=0
    if [ "$actual_exit" -ne "$want_exit" ]; then
        emit FAIL "exit $actual_exit, expected $want_exit"
        return
    fi

    if [ "${#STDOUT_LINES[@]}" -gt 0 ]; then
        printf '%s\n' "${STDOUT_LINES[@]}" > "$base.expected"
        if ! cmp -s "$base.expected" "$base.stdout"; then
            emit FAIL "stdout mismatch: $(diff -u "$base.expected" "$base.stdout" | tail -1)"
            return
        fi
    fi
    emit PASS ""
}

# ── harness self-test ──────────────────────────────────────────────
# Proves a wrong EXPECT-* really fails (and a correct one really passes).
# Synthetic files live in the mktemp workdir, so they never enter discovery.
self_test() {
    local td="$WORK/selftest"
    mkdir -p "$td"
    printf '// EXPECT-EXIT: 99\nfunction main() { return(0); }\n' > "$td/wrong_exit.cyan"
    printf '// EXPECT-EXIT: 0\n// EXPECT-STDOUT: wrong line\nfunction main() { print(7); return(0); }\n' > "$td/wrong_stdout.cyan"
    printf '// EXPECT-ERROR: no such diagnostic\nfunction main() { return(0); }\n' > "$td/wrong_error.cyan"
    printf '// EXPECT-EXIT: 0\n// EXPECT-STDOUT: 7\nfunction main() { print(7); return(0); }\n' > "$td/control.cyan"

    local base=${#CASES[@]}
    local specs=(
        "selftest|x86_64|$td/wrong_exit.cyan"
        "selftest|x86_64|$td/wrong_stdout.cyan"
        "selftest|x86_64|$td/wrong_error.cyan"
        "selftest|x86_64|$td/control.cyan"
    )
    local expected=(FAIL FAIL FAIL PASS)
    local specs_names=(wrong_exit.cyan wrong_stdout.cyan wrong_error.cyan control.cyan)
    local want st cat target path reason i rc=0
    for i in "${!specs[@]}"; do
        CASES+=("${specs[$i]}")
        run_case "$((base + i))"
        IFS='|' read -r st cat target path reason < "$WORK/results/$((base + i))"
        want="${expected[$i]}"
        if [ "$st" = "$want" ]; then
            echo -e "${GREEN}[PASS]${NC} self-test ${specs_names[$i]}: got $st as expected"
        else
            echo -e "${RED}[FAIL]${NC} self-test ${specs_names[$i]}: got $st, expected $want"
            rc=1
        fi
    done
    return "$rc"
}

if [ "$SELF_TEST" -eq 1 ]; then
    self_test
    rc=$?
    echo "========================================"
    [ "$rc" -eq 0 ] && echo "Self-test OK: wrong expectations are rejected." \
                    || echo "Self-test FAILED."
    echo "========================================"
    exit "$rc"
fi

# ── discover ───────────────────────────────────────────────────────
while IFS= read -r path; do
    [ -n "$FILTER" ] && [[ "$path" != *"$FILTER"* ]] && continue
    parse_headers "$path"
    local_targets="${TARGETS_VAL:-x86_64 riscv64}"
    for t in $local_targets; do
        case "$TARGET" in
            all) ;;
            *) [ "$t" = "$TARGET" ] || continue ;;
        esac
        if [ "$t" = "riscv64" ] && [ "$RV_OK" -eq 0 ]; then
            continue
        fi
        cat="${path#tests/}"; cat="${cat%%/*}"
        CASES+=("$cat|$t|$path")
    done
done < <(find tests -type f -name '*.cyan' | sort)

echo "========================================"
echo "  Cyan Compiler Test Suite (discovered)"
echo "========================================"
echo ""

# ── execute ────────────────────────────────────────────────────────
running=0
for idx in "${!CASES[@]}"; do
    run_case "$idx" &
    running=$((running + 1))
    if [ "$running" -ge "$JOBS" ]; then
        wait -n 2>/dev/null
        running=$((running - 1))
    fi
done
wait

# ── aggregate ──────────────────────────────────────────────────────
declare -A pass fail skip
CATS=()
TOTAL_PASS=0; TOTAL_FAIL=0; TOTAL_SKIP=0
for idx in "${!CASES[@]}"; do
    IFS='|' read -r st cat target path reason < "$WORK/results/$idx"
    case "$st" in
        PASS) TOTAL_PASS=$((TOTAL_PASS + 1)); pass[$cat]=$(( ${pass[$cat]:-0} + 1 )) ;;
        FAIL) TOTAL_FAIL=$((TOTAL_FAIL + 1)); fail[$cat]=$(( ${fail[$cat]:-0} + 1 )) ;;
        SKIP) TOTAL_SKIP=$((TOTAL_SKIP + 1)); skip[$cat]=$(( ${skip[$cat]:-0} + 1 )) ;;
    esac
    case " ${CATS[*]} " in
        *" $cat "*) ;;
        *) CATS+=("$cat") ;;
    esac
    case "$st" in
        PASS) echo -e "${GREEN}[PASS]${NC} $cat/$target: $path" ;;
        FAIL) echo -e "${RED}[FAIL]${NC} $cat/$target: $path — $reason" ;;
        SKIP) echo -e "${DIM}[skip]${NC} $cat/$target: $path ($reason)" ;;
    esac
done

echo ""
echo "========================================"
echo "Summary by category:"
for cat in "${CATS[@]}"; do
    printf '  %-12s %4d passed, %d failed, %d skipped\n' \
        "$cat" "${pass[$cat]:-0}" "${fail[$cat]:-0}" "${skip[$cat]:-0}"
done
printf '  %-12s %4d passed, %d failed, %d skipped\n' \
    "TOTAL" "$TOTAL_PASS" "$TOTAL_FAIL" "$TOTAL_SKIP"
echo "Run-equivalents: ${#CASES[@]}"
echo -e "Results: ${GREEN}$TOTAL_PASS passed${NC}, ${RED}$TOTAL_FAIL failed${NC}"
echo "========================================"

[ "$TOTAL_FAIL" -gt 0 ] && exit 1
exit 0
