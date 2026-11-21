#!/bin/bash
# E1: the parser must report independent errors in one file without cascading
# or stopping at the first one.
set -u
cd "$(dirname "$0")/.."
COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/errors.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/errors.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/three_errors.cyan" <<'EOF'
function main() {
    var a = ;
    var b = ;
    var c = ;
    return 0;
}
EOF

"$COMPILER" -o "$WORK/out" "$WORK/three_errors.cyan" > "$WORK/log" 2>&1
rc=$?
count=$(grep -c 'error:' "$WORK/log")
if [ "$rc" -eq 0 ]; then
    echo "Error recovery: FAILED (compilation succeeded)"
    exit 1
fi
if [ "$count" -ne 3 ]; then
    echo "Error recovery: FAILED (expected 3 errors, got $count)"
    head -20 "$WORK/log"
    exit 1
fi
echo "Error recovery: OK (3 independent errors)"
