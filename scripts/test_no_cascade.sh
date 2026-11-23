#!/bin/bash
# T-3: a single source mistake must produce exactly one diagnostic, not a
# cascade. Run on both targets against the normal compiler (not a wrapper).
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/nocascade.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/nocascade.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

fail() { echo "No-cascade: FAILED ($*)"; exit 1; }

cat > "$WORK/undef.cyan" <<'EOF'
function main() {
    var x = missing_name;
    return(0);
}
EOF

cat > "$WORK/badexpr.cyan" <<'EOF'
function main() {
    var x = 1 +;
    return(0);
}
EOF

cat > "$WORK/badfield.cyan" <<'EOF'
struct S { var a: i64; };
function main() {
    var s: S;
    s.nope = 1;
    return(0);
}
EOF

for target in x86_64 riscv64; do
    tflag=(); [ "$target" = "riscv64" ] && tflag=(--target riscv64)
    for name in undef badexpr badfield; do
        "$COMPILER" "${tflag[@]}" -o "$WORK/$name.$target.bin" "$WORK/$name.cyan" \
            > "$WORK/$name.$target.log" 2>&1
        rc=$?
        count=$(grep -c 'error:' "$WORK/$name.$target.log")
        [ "$rc" -ne 0 ] || fail "$target/$name compiled successfully"
        [ "$count" -eq 1 ] || fail "$target/$name produced $count errors, expected 1"
    done
done

echo "No-cascade: OK (3 single-error cases on both targets)"
