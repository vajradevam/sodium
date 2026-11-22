#!/bin/bash
# O8a: dynamic guest instruction counts for the benchmark programs.
#
# Each program is compiled at -O0 and -O1 for x86_64 and riscv64, then run
# under qemu with -one-insn-per-tb -d exec so every executed guest
# instruction produces one "Trace" line. Counts are compared with the committed
# baselines in bench/baselines.txt (20% tolerance absorbs toolchain version
# differences while catching real regressions).
#
# Usage: scripts/bench.sh [--update]
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
BASELINES=bench/baselines.txt
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/bench.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/bench.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

declare -A BASELINE
if [ -f "$BASELINES" ]; then
    while read -r name target opt count; do
        [ -z "${name:-}" ] && continue
        BASELINE["$name $target $opt"]=$count
    done < "$BASELINES"
fi

printf '%-10s %-9s %-4s %12s %12s %8s\n' BENCH TARGET OPT COUNT BASELINE DELTA
status=0
new_baselines=""

for cyan in bench/*.cyan; do
    [ -e "$cyan" ] || continue
    name="$(basename "$cyan" .cyan)"
    expected="$(sed -n 's|^// EXPECT-STDOUT: ||p' "$cyan" | head -1)"
    for target in x86_64 riscv64; do
        if [ "$target" = "riscv64" ]; then
            tflag="--target riscv64"; runner="qemu-riscv64"
        else
            tflag=""; runner="qemu-x86_64"
        fi
        for opt in -O0 -O1; do
            exe="$WORK/$name-$target-$opt"
            if ! "$COMPILER" $opt $tflag -o "$exe" "$cyan" > "$WORK/compile.log" 2>&1; then
                echo "bench: compile failed for $name [$target/$opt]" >&2
                cat "$WORK/compile.log" >&2
                status=1
                continue
            fi
            "$runner" -one-insn-per-tb -d exec "$exe" > "$WORK/stdout" 2> "$WORK/insns"
            if [ -n "$expected" ] && [ "$(cat "$WORK/stdout")" != "$expected" ]; then
                echo "bench: wrong output for $name [$target/$opt]: $(cat "$WORK/stdout")" >&2
                status=1
            fi
            count=$(grep -c '^Trace ' "$WORK/insns")
            [ -n "$count" ] || count=0
            key="$name $target $opt"
            base="${BASELINE[$key]:-0}"
            if [ "$UPDATE" -eq 1 ] || [ "$base" -eq 0 ]; then
                base=$count
            else
                # Report a regression when the count grows by more than 20%.
                limit=$((base + base / 5))
                if [ "$count" -gt "$limit" ]; then
                    echo "REGRESSION: $key $count > baseline $base" >&2
                    status=1
                fi
            fi
            new_baselines+="$key $base"$'\n'
            printf '%-10s %-9s %-4s %12s %12s %8s\n' \
                "$name" "$target" "$opt" "$count" "$base" "$((count - base))"
        done
    done
done

if [ "$UPDATE" -eq 1 ]; then
    printf '%s' "$new_baselines" | sort > "$BASELINES"
    echo "Updated $BASELINES"
fi

exit $status
