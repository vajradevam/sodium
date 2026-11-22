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

echo "Optimization effects: OK"
