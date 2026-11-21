#!/bin/bash
# The single quality gate. Every commit must pass `scripts/check.sh`.
# Usage: scripts/check.sh          fast gate: build + full test suite (x86_64 + riscv64)
#        scripts/check.sh --full   also builds with ASan+UBSan and re-runs the suite
set -u
cd "$(dirname "$0")/.."
FULL=0; [ "${1:-}" = "--full" ] && FULL=1
step() { echo; echo "=== $* ==="; }
die()  { echo "CHECK FAILED: $*" >&2; exit 1; }

step "build (build/)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null || die "cmake configure"
cmake --build build -j"$(nproc)" 2>&1 | tee build/last_build.log | tail -3
[ "${PIPESTATUS[0]}" -eq 0 ] || die "build"
if grep -q "warning:" build/last_build.log; then
    echo "note: build produced warnings (see build/last_build.log); Phase 0 makes this fatal"
fi

step "test suite"
./scripts/run_tests.sh > build/last_tests.log 2>&1; rc=$?
tail -4 build/last_tests.log
grep -E "SKIP|FAIL" build/last_tests.log | grep -v "^Results" | head -20
[ $rc -eq 0 ] || die "tests (see build/last_tests.log)"
grep -q "RISC-V backend: SKIPPED" build/last_tests.log && die "riscv64 tests were skipped (qemu-riscv64 missing)"

step "harness self-test (wrong EXPECT-* must fail)"
./scripts/run_tests.sh --self-test > build/last_selftest.log 2>&1 || die "harness self-test (see build/last_selftest.log)"
tail -6 build/last_selftest.log

if [ $FULL -eq 1 ]; then
    step "sanitizer build (build-asan/)"
    cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" >/dev/null || die "asan configure"
    cmake --build build-asan -j"$(nproc)" --target sodium 2>&1 | tail -2
    [ "${PIPESTATUS[0]}" -eq 0 ] || die "asan build"
    step "test suite under ASan/UBSan"
    ASAN_OPTIONS=detect_leaks=0 COMPILER=./build-asan/sodium ./scripts/run_tests.sh > build/last_tests_asan.log 2>&1 \
        || die "tests under sanitizers (see build/last_tests_asan.log)"
    grep -E "runtime error|AddressSanitizer" build/last_tests_asan.log && die "sanitizer reports"
    tail -3 build/last_tests_asan.log
fi
echo; echo "CHECK OK"
