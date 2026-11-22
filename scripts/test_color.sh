#!/bin/bash
# E4: -fcolor-diagnostics emits ANSI colors; NO_COLOR / -fno-color-diagnostics disable.
set -u
COMPILER="${COMPILER:-./build/sodium}"
TMPBASE="${TMPDIR:-/tmp/opencode}"
WORK=$(mktemp -d "$TMPBASE/cyan_color.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/cyan_color.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

SRC="$WORK/t.cyan"
printf 'function main() { var unused = 1; return(0); }\n' > "$SRC"
fail=0

has_ansi() { LC_ALL=C grep -q $'\033\[' "$1"; }
has_warning() { grep -Fq 'unused variable' "$1"; }

"$COMPILER" -fcolor-diagnostics -S -o "$WORK/a.asm" "$SRC" > "$WORK/color.log" 2>&1
if ! has_warning "$WORK/color.log"; then
    echo "color check: warning missing from -fcolor-diagnostics output"; fail=1
elif ! has_ansi "$WORK/color.log"; then
    echo "color check: no ANSI escapes with -fcolor-diagnostics"; fail=1
fi

"$COMPILER" -S -o "$WORK/b.asm" "$SRC" > "$WORK/plain.log" 2>&1
if ! has_warning "$WORK/plain.log"; then
    echo "color check: warning missing from default output"; fail=1
elif has_ansi "$WORK/plain.log"; then
    echo "color check: unexpected ANSI escapes without -fcolor-diagnostics"; fail=1
fi

NO_COLOR=1 "$COMPILER" -fcolor-diagnostics -S -o "$WORK/c.asm" "$SRC" > "$WORK/nocolor.log" 2>&1
if ! has_warning "$WORK/nocolor.log"; then
    echo "color check: warning missing with NO_COLOR"; fail=1
elif has_ansi "$WORK/nocolor.log"; then
    echo "color check: NO_COLOR did not disable colors"; fail=1
fi

"$COMPILER" -fcolor-diagnostics -fno-color-diagnostics -S -o "$WORK/d.asm" "$SRC" > "$WORK/noflag.log" 2>&1
if ! has_warning "$WORK/noflag.log"; then
    echo "color check: warning missing with -fno-color-diagnostics"; fail=1
elif has_ansi "$WORK/noflag.log"; then
    echo "color check: -fno-color-diagnostics did not disable colors"; fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "Color diagnostics: OK"
fi
exit "$fail"
