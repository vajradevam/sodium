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

step "README code samples (compile and run, both targets)"
./scripts/doc_check.sh > build/last_doccheck.log 2>&1 || die "README samples (see build/last_doccheck.log)"
tail -1 build/last_doccheck.log

step "architecture docs vs source"
./scripts/check_arch_docs.sh > build/last_archdocs.log 2>&1 || die "architecture docs (see build/last_archdocs.log)"
tail -1 build/last_archdocs.log

step "IR/assembly regression sweep (both targets)"
./scripts/sweep.sh > build/last_sweep.log 2>&1 || die "sweep (see build/last_sweep.log)"
tail -2 build/last_sweep.log

step "optimization IR effects (-O0 vs -O1)"
./scripts/test_opt_effects.sh > build/last_opt.log 2>&1 || die "optimization effects (see build/last_opt.log)"
tail -1 build/last_opt.log

step "register allocator benchmark (both targets)"
./scripts/bench_regalloc.sh > build/last_bench.log 2>&1 || die "regalloc benchmark (see build/last_bench.log)"
tail -1 build/last_bench.log

step "benchmark instruction counts (both targets, -O0 and -O1)"
./scripts/bench.sh > build/last_bench_instr.log 2>&1 || die "benchmark regressions (see build/last_bench_instr.log)"
tail -4 build/last_bench_instr.log

step "differential oracle at -O0 (Cyan vs C, both targets)"
COMPILER=./scripts/sodium-o0.sh ./scripts/oracle.sh > build/last_oracle.log 2>&1 || die "oracle at -O0 (see build/last_oracle.log)"
tail -2 build/last_oracle.log

step "differential oracle under -O1 (both targets)"
COMPILER=./scripts/sodium-o1.sh ./scripts/oracle.sh > build/last_oracle_o1.log 2>&1 || die "oracle under -O1 (see build/last_oracle_o1.log)"
tail -2 build/last_oracle_o1.log

step "type matrix (every int type x operator x context vs gcc, both targets)"
./scripts/test_type_matrix.sh > build/last_typematrix.log 2>&1 || die "type matrix (see build/last_typematrix.log)"
tail -1 build/last_typematrix.log

step "C interop (struct ABI vs gcc, both targets)"
./scripts/test_abi_interop.sh > build/last_abi.log 2>&1 || die "ABI interop (see build/last_abi.log)"
tail -1 build/last_abi.log

step "C++ unit tests"
./build/unit_tests > build/last_unit.log 2>&1 || die "unit tests (see build/last_unit.log)"
tail -2 build/last_unit.log

step "test suite at -O0"
COMPILER=./scripts/sodium-o0.sh ./scripts/run_tests.sh > build/last_tests.log 2>&1; rc=$?
tail -4 build/last_tests.log
grep -E "SKIP|FAIL" build/last_tests.log | grep -v "^Results" | head -20
[ $rc -eq 0 ] || die "tests (see build/last_tests.log)"
grep -q "RISC-V backend: SKIPPED" build/last_tests.log && die "riscv64 tests were skipped (qemu-riscv64 missing)"

step "test suite under -O1"
COMPILER=./scripts/sodium-o1.sh ./scripts/run_tests.sh > build/last_tests_o1.log 2>&1 \
    || die "tests under -O1 (see build/last_tests_o1.log)"
tail -4 build/last_tests_o1.log

step "debug line tables (-g, both targets)"
./scripts/test_debug_info.sh > build/last_debuginfo.log 2>&1 || die "debug info (see build/last_debuginfo.log)"
tail -1 build/last_debuginfo.log

step "harness self-test (wrong EXPECT-* must fail)"
./scripts/run_tests.sh --self-test > build/last_selftest.log 2>&1 || die "harness self-test (see build/last_selftest.log)"
tail -6 build/last_selftest.log

step "parser error recovery (multiple independent errors)"
./scripts/test_error_recovery.sh > build/last_errors.log 2>&1 || die "error recovery (see build/last_errors.log)"
tail -1 build/last_errors.log

step "semantic error recovery (multiple functions)"
./scripts/test_semantic_recovery.sh > build/last_semerrors.log 2>&1 || die "semantic recovery (see build/last_semerrors.log)"
tail -1 build/last_semerrors.log

step "LSP protocol diagnostics (multiple severities)"
python3 tests/lsp/test_lsp_diagnostics.py ./build/cyan-lsp > build/last_lsp.log 2>&1 || die "LSP diagnostics (see build/last_lsp.log)"
tail -1 build/last_lsp.log

step "LSP protocol features (rename, references, diagnostics lifecycle)"
python3 tests/lsp/test_lsp_features.py ./build/cyan-lsp > build/last_lsp_features.log 2>&1 || die "LSP features (see build/last_lsp_features.log)"
tail -1 build/last_lsp_features.log

step "LSP protocol signature help and semantic tokens"
python3 tests/lsp/test_lsp_signature.py ./build/cyan-lsp > build/last_lsp_signature.log 2>&1 || die "LSP signature/tokens (see build/last_lsp_signature.log)"
tail -1 build/last_lsp_signature.log

step "VS Code extension grammar and client config"
python3 tests/vscode/test_extension.py > build/last_vscode.log 2>&1 || die "VS Code extension (see build/last_vscode.log)"
tail -1 build/last_vscode.log

step "CLI version and help text"
python3 tests/cli/test_cli.py ./build/sodium > build/last_cli.log 2>&1 || die "CLI text (see build/last_cli.log)"
tail -1 build/last_cli.log

step "formatter idempotence over all tests"
./scripts/test_formatter.sh > build/last_formatter.log 2>&1 || die "formatter (see build/last_formatter.log)"
tail -1 build/last_formatter.log

step "install script in a clean temp prefix"
./scripts/test_install.sh > build/last_install.log 2>&1 || die "install (see build/last_install.log)"
tail -1 build/last_install.log

step "color diagnostics (-fcolor-diagnostics / NO_COLOR)"
./scripts/test_color.sh > build/last_color.log 2>&1 || die "color diagnostics (see build/last_color.log)"
tail -1 build/last_color.log

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
