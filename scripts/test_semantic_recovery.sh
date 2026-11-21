#!/bin/bash
# E2: semantic errors in separate statements/functions must all be reported.
set -u
cd "$(dirname "$0")/.."
COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/semerrors.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/semerrors.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/three.cyan" <<'EOF'
function a() { return(missing_one); }
function b() { return(missing_two); }
function main() {
    var x = missing_three;
    return 0;
}
EOF

"$COMPILER" -o "$WORK/out" "$WORK/three.cyan" > "$WORK/log" 2>&1
rc=$?
count=$(grep -c 'error:' "$WORK/log")
if [ "$rc" -eq 0 ] || [ "$count" -ne 3 ]; then
    echo "Semantic recovery: FAILED (exit=$rc, errors=$count, expected 3)"
    head -20 "$WORK/log"
    exit 1
fi
echo "Semantic recovery: OK (3 errors across functions)"
