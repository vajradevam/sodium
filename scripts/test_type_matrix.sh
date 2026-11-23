#!/bin/bash
# Type matrix (D-044): every integer type x every operator x every context must compile, and
# every result must equal what the same expression gives in C with the result cast back to the
# type (Cyan has no promotion: `u8 + u8` is a u8 that wraps).
#
# For each of the 8 integer types a Cyan program defines, for each expression E, four functions
# with the same body in different contexts (return, call argument, `var` initialiser, assignment)
# and prints all of them for a set of edge values. A C program computes the same table with gcc.
# Both targets are run and diffed against the C output.
# Usage: scripts/test_type_matrix.sh [x86_64|riscv64|both]   (default: both)
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
case "${1:-both}" in
    x86_64) TARGETS="x86_64" ;;
    riscv64) TARGETS="riscv64" ;;
    both) TARGETS="x86_64 riscv64" ;;
    *) echo "usage: $0 [x86_64|riscv64|both]" >&2; exit 2 ;;
esac
for t in $TARGETS; do
    if [ "$t" = riscv64 ] && ! command -v qemu-riscv64 >/dev/null 2>&1; then
        echo "Type matrix: FAILED (qemu-riscv64 not found; the gate does not allow skipping riscv64)" >&2
        exit 1
    fi
done

WORK=$(mktemp -d "${TMPDIR:-/tmp}/typematrix.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# Cyan expression => C expression (T is the C type; every Cyan operator result is cast back to it).
EXPRS=(
    'x + 1=>(T)(x + 1)'
    'x - 1=>(T)(x - 1)'
    'x * 2=>(T)(x * 2)'
    '1 + x=>(T)(1 + x)'
    'x + x=>(T)(x + x)'
    '-x=>(T)(-x)'
    '~x=>(T)(~x)'
    'x << 1=>(T)(x << 1)'
    'x >> 1=>(T)(x >> 1)'
    'x & 1=>(T)(x & 1)'
    'x | 1=>(T)(x | 1)'
    'x ^ 1=>(T)(x ^ 1)'
    'x / 2=>(T)(x / 2)'
    'x % 2=>(T)(x % 2)'
    '(x + 1) * 2=>(T)((T)(x + 1) * 2)'
    'x - x=>(T)(x - x)'
    'x * x=>(T)(x * x)'
    'x - 100=>(T)(x - 100)'
)

# type | C type | edge values (all representable; u64 stays below 2^63 because print is signed)
TYPES=(
    'i8|int8_t|-128 -127 -1 0 1 2 126 127'
    'u8|uint8_t|0 1 2 127 128 200 254 255'
    'i16|int16_t|-32768 -32767 -1 0 1 2 32766 32767'
    'u16|uint16_t|0 1 2 32767 32768 60000 65534 65535'
    'i32|int32_t|-2147483648 -2147483647 -1 0 1 2 2147483646 2147483647'
    'u32|uint32_t|0 1 2 2147483647 2147483648 4000000000 4294967294 4294967295'
    'i64|int64_t|-9223372036854775807 -2 -1 0 1 2 4611686018427387904 9223372036854775807'
    'u64|uint64_t|0 1 2 127 128 4294967296 9223372036854775806 9223372036854775807'
)

fail=0
checked=0
for spec in "${TYPES[@]}"; do
    IFS='|' read -r ty cty values <<<"$spec"
    cy="$WORK/$ty.cyan"
    cc="$WORK/$ty.c"
    {
        echo "function take(a: $ty) -> $ty { return a; }"
        i=0
        for e in "${EXPRS[@]}"; do
            ex="${e%%=>*}"
            echo "function fr_$i(x: $ty) -> $ty { return $ex; }"
            echo "function fa_$i(x: $ty) -> $ty { return take($ex); }"
            echo "function fn_$i(x: $ty) -> $ty { var y: $ty = $ex; return y; }"
            echo "function fs_$i(x: $ty) -> $ty { var y: $ty = 0; y = $ex; return y; }"
            i=$((i + 1))
        done
        echo "function main() {"
        i=0
        for e in "${EXPRS[@]}"; do
            for v in $values; do
                for c in fr_ fa_ fn_ fs_; do
                    echo "    print(${c}${i}($v));"
                done
            done
            i=$((i + 1))
        done
        echo "    return(0);"
        echo "}"
    } >"$cy"
    {
        echo '#include <stdint.h>'
        echo '#include <stdio.h>'
        echo 'int main(void) {'
        echo "    $cty x;"
        for e in "${EXPRS[@]}"; do
            cx="${e#*=>}"
            cx="${cx//T/$cty}"
            for v in $values; do
                lit="${v}ll"
                [ "$v" = "-128" ] && lit="(-127 - 1)"
                [ "$v" = "-32768" ] && lit="(-32767 - 1)"
                [ "$v" = "-2147483648" ] && lit="(-2147483647 - 1)"
                echo "    x = ($cty)${lit};"
                for c in 1 2 3 4; do
                    echo "    printf(\"%lld\\n\", (long long)($cx));"
                done
            done
        done
        echo '    return 0;'
        echo '}'
    } >"$cc"
    gcc -O0 -w -o "$WORK/$ty.ref" "$cc" || { echo "FAIL $ty: reference C does not build"; fail=1; continue; }
    "$WORK/$ty.ref" >"$WORK/$ty.want"

    for t in $TARGETS; do
        checked=$((checked + 1))
        if ! "$COMPILER" --target "$t" -w "$cy" -o "$WORK/$ty.$t" >"$WORK/$ty.$t.err" 2>&1; then
            echo "FAIL $ty [$t]: does not compile"
            head -4 "$WORK/$ty.$t.err"
            fail=1
            continue
        fi
        runner=("$WORK/$ty.$t")
        [ "$t" = riscv64 ] && runner=(qemu-riscv64 "$WORK/$ty.$t")
        "${runner[@]}" >"$WORK/$ty.$t.got" 2>&1
        if ! cmp -s "$WORK/$ty.want" "$WORK/$ty.$t.got"; then
            echo "FAIL $ty [$t]: output differs from C"
            diff "$WORK/$ty.want" "$WORK/$ty.$t.got" | head -6
            fail=1
        fi
    done
done

if [ "$fail" -ne 0 ]; then
    echo "Type matrix: FAILED"
    exit 1
fi
echo "Type matrix: ${#TYPES[@]} types x ${#EXPRS[@]} expressions x 4 contexts, $checked runs matched C"
