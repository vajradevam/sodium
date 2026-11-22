#!/bin/bash
# X6b: verify that -g produces a usable DWARF line table on both targets and
# that without -g no debug information is emitted.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/debuginfo.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/debuginfo.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/program.cyan" <<'CYAN'
function main() {
    var x = 7;
    print("%d", x);
    return 0;
}
CYAN

fail() { echo "debug info: $*" >&2; exit 1; }

if ! "$COMPILER" -g -o "$WORK/x86" "$WORK/program.cyan" > "$WORK/x86.log" 2>&1; then
    fail "x86_64 -g compile failed: $(head -2 "$WORK/x86.log")"
fi
readelf -S "$WORK/x86" 2>/dev/null | grep -q '\.debug_line' || fail "x86_64 -g emitted no .debug_line section"
readelf --debug-dump=decodedline "$WORK/x86" 2>/dev/null | grep -q 'program.cyan' || \
    fail "x86_64 line table has no program.cyan rows"
if command -v gdb >/dev/null 2>&1; then
    gdb -q -batch -ex "info line $WORK/program.cyan:3" "$WORK/x86" 2>/dev/null | \
        grep -q 'starts at address' || fail "gdb cannot resolve an x86_64 source line"
fi

if ! "$COMPILER" -g --target riscv64 -o "$WORK/rv" "$WORK/program.cyan" > "$WORK/rv.log" 2>&1; then
    fail "riscv64 -g compile failed: $(head -2 "$WORK/rv.log")"
fi
riscv64-elf-objdump --dwarf=decodedline "$WORK/rv" 2>/dev/null | grep -q 'program.cyan' || \
    fail "riscv64 line table has no program.cyan rows"
if command -v gdb >/dev/null 2>&1; then
    gdb -q -batch -ex "info line $WORK/program.cyan:3" "$WORK/rv" 2>/dev/null | \
        grep -q 'address' || fail "gdb cannot resolve a riscv64 source line"
fi

# -g is opt-in: the default build must have no debug sections.
"$COMPILER" -o "$WORK/plain" "$WORK/program.cyan" > /dev/null 2>&1 || fail "plain compile failed"
if readelf -S "$WORK/plain" 2>/dev/null | grep -q '\.debug_line'; then
    fail "default build emitted debug info without -g"
fi

echo "Debug info: line tables present with -g on both targets"
