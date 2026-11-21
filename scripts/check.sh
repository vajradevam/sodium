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
grep -q "warning:" build/last_build.log && die "build produced warnings (see build/last_build.log)"

step "style check (changed files; see docs/STYLE.md)"
./scripts/check_style.sh || die "style (trailing whitespace / tab indentation)"

step "IR/assembly regression sweep (both targets)"
./scripts/sweep.sh > build/last_sweep.log 2>&1 || die "sweep (see build/last_sweep.log)"
tail -2 build/last_sweep.log

step "differential oracle (Cyan vs C, both targets)"
./scripts/oracle.sh > build/last_oracle.log 2>&1 || die "oracle (see build/last_oracle.log)"
tail -2 build/last_oracle.log

step "C++ unit tests"
./build/unit_tests > build/last_unit.log 2>&1 || die "unit tests (see build/last_unit.log)"
tail -2 build/last_unit.log

step "test suite"
./scripts/run_tests.sh > build/last_tests.log 2>&1; rc=$?
tail -4 build/last_tests.log
grep -E "SKIP|FAIL" build/last_tests.log | grep -v "^Results" | head -20
[ $rc -eq 0 ] || die "tests (see build/last_tests.log)"
grep -q "RISC-V backend: SKIPPED" build/last_tests.log && die "riscv64 tests were skipped (qemu-riscv64 missing)"

step "harness self-test (wrong EXPECT-* must fail)"
./scripts/run_tests.sh --self-test > build/last_selftest.log 2>&1 || die "harness self-test (see build/last_selftest.log)"
tail -6 build/last_selftest.log

step "parser error recovery (multiple independent errors)"
./scripts/test_error_recovery.sh > build/last_errors.log 2>&1 || die "error recovery (see build/last_errors.log)"
tail -1 build/last_errors.log

step "semantic error recovery (multiple functions)"
./scripts/test_semantic_recovery.sh > build/last_semerrors.log 2>&1 || die "semantic recovery (see build/last_semerrors.log)"
tail -1 build/last_semerrors.log

step "output path handling (spaces, semicolons, injection)"
./scripts/test_paths.sh > build/last_paths.log 2>&1 || die "path handling (see build/last_paths.log)"
tail -1 build/last_paths.log

step "fuzz smoke (deterministic mutations)"
./scripts/fuzz_smoke.sh 20261121 1000 > build/last_fuzz.log 2>&1 || die "fuzz smoke (see build/last_fuzz.log)"
tail -2 build/last_fuzz.log

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
