# Progress log (append-only)

Format: `YYYY-MM-DD  TASK-ID  summary  (tests A→B)`

2026-11-21  RESET  Old plans (TODO/URGENT/BUGS/FEATURES/audit) removed; AGENTS.md + PLAN.md created.
            Baseline verified from code: build clean, 318 tests pass (x86_64 + riscv64 via qemu), ASan/UBSan clean.
            Verified-fixed from old audit: string escapes (L3), const overflow (U1/U2), div-by-zero (C4), packed frames (G3), did-you-mean (D3).
            Unverified old claims → tasks in Phase 1.

2026-11-21  T0a/b/c  Discoverable test harness: D-005 EXPECT-* headers on all 162 test files (exit codes from the old map, compile-error substrings from real diagnostics, reviewed stdout lines), auto-discovering runner with --filter/--target/-j and mktemp workdirs, exact stdout + diagnostic-substring checks on both targets, --self-test proving wrong expectations fail (wired into check.sh).  (318→322 passed, 0 failed, 2 helper skips; 324 run-equivalents)
2026-11-21  V12  Found during T0a stdout survey: riscv64 print(0) emits byte 0x00 instead of '0' (x86_64 correct). test_print/test_print_all keep exit-code checks only until this is fixed; task recorded in PLAN.md.
2026-11-21  T1  Strict build: -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wno-sign-conversion -Werror on both targets; removed 24 unique warnings (dead params/locals, missing Var::struct_type initializers, redundant std::move, dead walk_expr declaration, unused IREmitter::m_alloc). Clean under GCC 16 and Clang 23; check.sh --full green. Sign-conversion disabled so GCC/Clang enforce the same set.  (322→322 passed, 0 failed)
2026-11-21  T2  CI-equivalent gate: check.sh now runs scripts/check_style.sh (trailing whitespace + tab indentation in changed lines, proven to reject a probe file) and scripts/sweep.sh (--emit-ir/-S over all 162 tests on x86_64+riscv64 = 648 invocations, 0 crashes, 1.7 s). Style conventions documented in docs/STYLE.md; no mass reformat. Also retroactively ticked T0a/b/c and T1 boxes in PLAN.md.  (322→322 passed, 0 failed)
2026-11-21  T3  Fuzz harness: scripts/fuzz_smoke.sh mutates/truncates corpus files with a fixed seed (timeout 5 s, only exit 0/1 allowed), wired into check.sh (1000 iterations). Campaigns over 13 seeds / >50k mutants found two crashes, each fixed in its own commit with regression tests: EOF in Parser::consume() (seed 12345) and too-few print format arguments (seeds 42, 8675309). Final campaigns: 8 seeds x 4000 mutants, 0 crashes.  (322→328 passed, 0 failed)
2026-11-21  T4a-e  Native C++ unit tests: tests/cpp/ harness + CMake unit_tests target (strict flags, wired into check.sh) with 94 cases / 1742 checks across arena, tokenizer, preprocessor, parser AST, const-eval, liveness, linear scan and sodium-rt/allocator.c compiled natively; runs in <0.01 s. V6 note: allocator payloads are only 8-byte aligned, not 16 as the comment claims.  (328→328 integration passed, 0 failed; unit 0→94)
2026-11-21  V12  riscv64 print(0) emitted a NUL byte: the zero path in print.asm wrote '0' at buf[30] but then label 4 advanced to buf[31]. Added the missing decrement. Re-enabled EXPECT-STDOUT on test_print (4 lines) and test_print_all (59 lines); both targets now match. Found by the differential oracle.
2026-11-21  T5a-c  Differential oracle: scripts/oracle.sh + 29 hand-written C/Cyan twin pairs (arith, shifts, signed division/modulo, i8/u8/i16/u32 truncation, comparisons, pointer math/diff, recursion, loops, switch); 58 comparisons on both targets, 0 mismatches, wired into check.sh. Two real bugs found and fixed in separate commits with regression tests: signed >> was logical, and V12.  (368→388 integration passed, 0 failed)
2026-11-22  T6  Build hygiene: removed every system() call from main.cpp. Assemble/link now use fork+execvp with argv arrays; mkdir uses filesystem::create_directories; output copy uses filesystem::copy_file; temp cleanup uses unlink/rmdir on known files. Added scripts/test_paths.sh (spaces, semicolons, and an injection attempt that must not create a marker file) to check.sh. Verified git status clean after check.sh --full.  (388→388 passed, 0 failed)

## State of the project — end of Phase 0 (re-verified against code, 2026-11-22)

- Gate: `scripts/check.sh --full` passes: strict `-Werror` build (GCC 16, also verified with Clang 23),
  388 integration tests passed / 0 failed / 2 skipped (helper.cyan on each target) across x86_64 and
  riscv64, 390 run-equivalents, ASan/UBSan clean. `git status` is clean after the gate.
- Discovered tests: 195 `tests/**/*.cyan` files, all driven by D-005 `// EXPECT-*` headers; no hardcoded
  test map remains. Runner supports `--filter`, `--target`, `-j` and mktemp workdirs.
- Harness proof: `--self-test` (in the gate) rejects deliberately wrong exit/stdout/error expectations.
- Regression sweep: 780 `--emit-ir` and `-S` invocations (every test × 2 modes × 2 targets), 0 crashes.
- Fuzz smoke: deterministic seeded mutations, 1000 iterations per gate; >50k mutants in ad-hoc campaigns.
  Fixed two crashes found this way: EOF in `Parser::consume()` and too-few print format arguments.
- C++ unit tests: 94 cases / 1742 checks, <0.01 s, covering arena, tokenizer, preprocessor, parser AST,
  const-eval, liveness, linear scan, and the natively compiled runtime allocator.
- Differential oracle: 29 hand-written C/Cyan twin pairs, 58 comparisons on both targets, 0 mismatches.
  It exposed two real bugs, both fixed with regression tests: signed `>>` was emitted as logical SHR, and
  riscv64 `print(0)` wrote an uninitialized NUL byte (V12).
- Security hygiene: no `system()` calls remain in main.cpp; output paths with spaces/semicolons are safe.
- Remaining work: all of Phases 1–9 are unchecked. Noted for Phase 1: V3 still covers signed/unsigned
  semantics beyond shifts, and V6 covers allocator 16-byte alignment (payloads are currently 8-aligned).
  README.md is still unverified (D1).

2026-11-22  V1  Stack-passed arguments: added integration test with 9/11 args, nested calls as args and recursion with stack args — passes both targets. Assembly inspection found a real ABI bug: with an even number of stack args the stack was 8-byte misaligned at call sites (callee-save pushes toggled parity). IREmitter now pads call sites based on the number of entry PUSHes; verified 50 x86_64 and 20 riscv64 call sites aligned, with four new unit tests checking emitted parity. Also found that >=13 args on x86_64 produce wrong results from spill-slot collisions across calls — that is V2 territory and is left for that task.  (388→390 passed, 0 failed; unit 94→98)
2026-11-22  V2  Register pressure/spills: fixed four distinct bugs. (1) replace_virtual_regs + insert_spill_code were separate passes, so physical register numbers were looked up as spilled vreg IDs; merged into one pass. (2) All spilled CALL arguments were loaded into one scratch register before pushing; added PUSH_FRAME so each is pushed directly from its slot, with cleanup bytes carried on CALL.imm_arg and alignment padding emitted before the args. (3) Spill slots started at [fp-8], colliding with locals/params; they now begin below the locals frame and the frame is enlarged. (4) ADD/MUL emitted `mov dst,op0; add dst,op1`, clobbering op1 when dst aliased it; commutative operands are now swapped. Verified 6..24 args on both targets; new test_spill_pressure.cyan covers 20 values in calls and loops. Remaining nested/recursion caller-save clobber recorded as V2b.  (390→392 passed, 0 failed)
2026-11-22  V3  Signed/unsigned semantics: added test_signed_unsigned.cyan covering arithmetic vs logical >> by operand type, signed div/mod with negatives, truncation at i8/u8/i16/u32 boundaries, compound assignment, ++/-- wrap, and mixed-signedness comparisons. All cases match C semantics on both targets — no further bugs reproducible in the currently implemented subset. Deferred to Phase 3: typed parameters/returns and u64 literals (>i64 max) are not yet accepted by the parser, so those V3 sub-cases cannot be tested yet. The >> bug found by the oracle was already fixed and committed earlier.  (392→394 passed, 0 failed)
