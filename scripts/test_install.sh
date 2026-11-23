#!/bin/bash
# D5: install into a clean temporary prefix and verify the installed tree is
# complete and usable, without touching the user's real PATH or prefix.
set -u
cd "$(dirname "$0")/.."

WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/installtest.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/installtest.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
PREFIX="$WORK/prefix"
mkdir -p "$PREFIX"

fail() { echo "install: $*" >&2; exit 1; }

INSTALL_PREFIX="$PREFIX" ./scripts/install.sh > "$WORK/install.log" 2>&1 || {
    tail -20 "$WORK/install.log" >&2
    fail "install.sh failed"
}

BIN="$PREFIX/bin"
[ -x "$BIN/sodium" ] || fail "sodium was not installed as executable"
[ -x "$BIN/cyan-lsp" ] || fail "cyan-lsp was not installed as executable"
[ -d "$BIN/sodium-rt" ] || fail "sodium-rt was not installed"
[ -d "$BIN/cyanlib" ] || fail "cyanlib was not installed"
[ ! -d "$BIN/sodium-rt/sodium-rt" ] || fail "nested sodium-rt directory created"

# The installed compiler must be self-contained and actually compile/run code.
"$BIN/sodium" --version > "$WORK/version" 2>&1 || fail "installed sodium --version failed"
grep -q '^sodium ' "$WORK/version" || fail "unexpected version output: $(cat "$WORK/version")"

cat > "$WORK/hello.cyan" <<'CYAN'
function main() {
    print("install ok");
    return 0;
}
CYAN
"$BIN/sodium" -I "$BIN/cyanlib" -o "$WORK/hello" "$WORK/hello.cyan" > "$WORK/compile.log" 2>&1 || {
    cat "$WORK/compile.log" >&2
    fail "installed sodium could not compile a program"
}
[ -x "$WORK/hello" ] || fail "no executable produced"
output=$("$WORK/hello" 2>&1) || fail "installed program failed to run"
[ "$output" = "install ok" ] || fail "unexpected program output: $output"

echo "Install: clean-prefix install verified ($BIN)"
