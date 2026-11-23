#!/bin/bash
# D4b: the formatter must be idempotent over every test file, preserve test
# headers/comments verbatim, and keep formatted programs compilable and
# behaviourally identical.
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
WORK=$(mktemp -d "${TMPDIR:-/tmp/opencode}/fmtcheck.XXXXXX" 2>/dev/null) || WORK=$(mktemp -d /tmp/fmtcheck.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

fail() { echo "formatter: $*" >&2; exit 1; }

count=0
for path in $(find tests -type f -name '*.cyan' | sort); do
    count=$((count + 1))
    name=$(printf '%04d' "$count")
    "$COMPILER" fmt "$path" > "$WORK/$name.1" 2> "$WORK/$name.err" || \
        fail "fmt failed for $path: $(head -2 "$WORK/$name.err")"
    "$COMPILER" fmt "$WORK/$name.1" > "$WORK/$name.2" 2> "$WORK/$name.err" || \
        fail "fmt failed on its own output for $path"
    cmp -s "$WORK/$name.1" "$WORK/$name.2" || fail "fmt is not idempotent for $path"
    "$COMPILER" fmt --check "$WORK/$name.1" > /dev/null 2>&1 || \
        fail "fmt --check rejected canonical output for $path"

    # EXPECT-* headers must survive byte-for-byte (the test harness reads them).
    grep '^// EXPECT-' "$path" > "$WORK/$name.h0" || true
    grep '^// EXPECT-' "$WORK/$name.1" > "$WORK/$name.h1" || true
    cmp -s "$WORK/$name.h0" "$WORK/$name.h1" || \
        fail "fmt changed EXPECT headers in $path"
done

# A formatted program must still compile and behave identically.
cat > "$WORK/sample.cyan" <<'CYAN'
function main(){
    var xs=[3,1,2];
    var total=0;
    for(var i=0;i<3;i=i+1){
      total=total+xs[i];
    }
    print("%d",total);
    return 0;
}
CYAN
"$COMPILER" fmt "$WORK/sample.cyan" > "$WORK/formatted.cyan" || fail "sample format failed"
"$COMPILER" -I tests/include -o "$WORK/original.bin" "$WORK/sample.cyan" > /dev/null 2>&1 || fail "original did not compile"
"$COMPILER" -I tests/include -o "$WORK/formatted.bin" "$WORK/formatted.cyan" > /dev/null 2>&1 || fail "formatted did not compile"
"$WORK/original.bin" > "$WORK/o1" 2>&1; r1=$?
"$WORK/formatted.bin" > "$WORK/o2" 2>&1; r2=$?
[ "$r1" -eq "$r2" ] || fail "exit codes differ ($r1 vs $r2)"
cmp -s "$WORK/o1" "$WORK/o2" || fail "stdout differs after formatting"

echo "Formatter: $count files idempotent; headers preserved; behaviour unchanged"
