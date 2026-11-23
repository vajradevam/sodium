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

# A different sodium earlier on PATH must be reported, not silently ignored.
CLEAN_PATH=""
IFS=: read -ra DIRS <<< "$PATH"
for d in "${DIRS[@]}"; do [ -x "$d/sodium" ] || CLEAN_PATH="$CLEAN_PATH:$d"; done
PATH="${CLEAN_PATH#:}" INSTALL_PREFIX="$PREFIX" ./scripts/install.sh > "$WORK/nopath.log" 2>&1 \
    || fail "install.sh failed with no sodium on PATH"
grep -q "warning:.*not on your PATH" "$WORK/nopath.log" || fail "missing not-on-PATH warning"
mkdir -p "$WORK/shadow"
printf '#!/bin/sh\nexit 0\n' > "$WORK/shadow/sodium"; chmod +x "$WORK/shadow/sodium"
PATH="$WORK/shadow:$PATH" INSTALL_PREFIX="$PREFIX" ./scripts/install.sh > "$WORK/shadow.log" 2>&1 \
    || fail "install.sh failed with a shadowing sodium"
grep -q "warning:.*is $WORK/shadow/sodium, not the one just installed" "$WORK/shadow.log" \
    || fail "no warning about a shadowing sodium"
PATH="$BIN:$PATH" INSTALL_PREFIX="$PREFIX" ./scripts/install.sh > "$WORK/ok.log" 2>&1 \
    || fail "install.sh failed with install dir on PATH"
! grep -q "warning:" "$WORK/ok.log" || fail "spurious PATH warning"

echo "Install: clean-prefix install verified ($BIN)"
