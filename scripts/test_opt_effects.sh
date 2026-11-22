#!/bin/bash
# D-006: every optimization needs an effect test (IR dump) as well as a
# semantics test. -O0 must stay a straightforward translation.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/opteff.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/opteff.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/in.cyan" <<'CYAN'
function scale(v: i64) -> i64 {
    return v * 8;
}
function main() {
    var x = 2 + 3;
    var y = scale(x);
    return y;
}
CYAN

"$COMPILER" -O1 --emit-ir -S -o "$WORK/out.s" "$WORK/in.cyan" 2> "$WORK/ir1"
grep -q 'load_i64 5' "$WORK/ir1" || { echo "effect: 2+3 was not folded"; exit 1; }
grep -q 'shl .* 3' "$WORK/ir1" || { echo "effect: v*8 was not strength-reduced"; exit 1; }

"$COMPILER" -O0 --emit-ir -S -o "$WORK/out.s" "$WORK/in.cyan" 2> "$WORK/ir0"
grep -q 'mul' "$WORK/ir0" || { echo "effect: -O0 did not emit mul"; exit 1; }
if grep -q 'shl .* 3' "$WORK/ir0"; then
    echo "effect: -O1 rewrite leaked into -O0"
    exit 1
fi

cat > "$WORK/cfg.cyan" <<'CYAN'
function f() -> i64 {
    return 7;
}
function main() {
    return f();
}
CYAN

"$COMPILER" -O1 --emit-ir -S -o "$WORK/out.s" "$WORK/cfg.cyan" 2> "$WORK/cfg1"
"$COMPILER" -O0 --emit-ir -S -o "$WORK/out.s" "$WORK/cfg.cyan" 2> "$WORK/cfg0"
ret1=$(grep -c 'ret' "$WORK/cfg1")
ret0=$(grep -c 'ret' "$WORK/cfg0")
[ "$ret1" -lt "$ret0" ] || { echo "effect: dead instructions after ret were not removed"; exit 1; }

cat > "$WORK/cse.cyan" <<'CYAN'
function f(v: i64) -> i64 {
    var a = v + 1;
    var b = v + 1;
    return a + b;
}
function main() {
    return f(5);
}
CYAN

"$COMPILER" -O1 --emit-ir -S -o "$WORK/out.s" "$WORK/cse.cyan" 2> "$WORK/cse1"
"$COMPILER" -O0 --emit-ir -S -o "$WORK/out.s" "$WORK/cse.cyan" 2> "$WORK/cse0"
copy1=$(grep -c 'copy ' "$WORK/cse1")
copy0=$(grep -c 'copy ' "$WORK/cse0")
[ "$copy1" -gt "$copy0" ] || { echo "effect: duplicate expression was not CSE'd"; exit 1; }

cat > "$WORK/mem2reg.cyan" <<'CYAN'
function main() {
    var x = 7;
    var y = x + 1;
    return y;
}
CYAN

"$COMPILER" -O1 --emit-ir -S -o "$WORK/out.s" "$WORK/mem2reg.cyan" 2> "$WORK/mem1"
"$COMPILER" -O0 --emit-ir -S -o "$WORK/out.s" "$WORK/mem2reg.cyan" 2> "$WORK/mem0"
grep -q 'frame_addr' "$WORK/mem0" || { echo "effect: -O0 did not use frame slots"; exit 1; }
if grep -q 'frame_addr' "$WORK/mem1"; then
    echo "effect: scalar local was not promoted to a vreg"
    exit 1
fi

# O7a: the per-target peephole pass must actually remove redundant moves from
# real generated assembly, and none may survive at -O1.
"$COMPILER" -O1 --dump-regalloc -S -I tests/include -I cyanlib \
    -o "$WORK/peephole.s" tests/integration/test_comprehensive.cyan 2> "$WORK/peephole.log"
grep -qE 'peephole self_moves=[1-9]' "$WORK/peephole.log" || {
    echo "effect: peephole removed no self-moves at -O1"
    exit 1
}
if grep -qE '^[[:space:]]*mov[[:space:]]+([a-z0-9]+),[[:space:]]*\1[[:space:]]*$' "$WORK/peephole.s"; then
    echo "effect: a self-move survived the peephole pass"
    exit 1
fi

# O7b1: deferred emission must keep every function, in source order.
"$COMPILER" -O1 -S -o "$WORK/defer.s" tests/integration/test_comprehensive.cyan 2>/dev/null
last_line=0
for sym in inc_g make_id fib classify grade main; do
    line=$(grep -nE "^${sym}:" "$WORK/defer.s" | head -1 | cut -d: -f1)
    [ -n "$line" ] || { echo "effect: $sym missing after deferred emission"; exit 1; }
    [ "$line" -gt "$last_line" ] || { echo "effect: $sym out of source order"; exit 1; }
    last_line=$line
done

echo "Optimization effects: OK"
