# Progress log (append-only)

Format: `YYYY-MM-DD  TASK-ID  summary  (tests A→B)`

2026-11-21  RESET  Old plans (TODO/URGENT/BUGS/FEATURES/audit) removed; AGENTS.md + PLAN.md created.
            Baseline verified from code: build clean, 318 tests pass (x86_64 + riscv64 via qemu), ASan/UBSan clean.
            Verified-fixed from old audit: string escapes (L3), const overflow (U1/U2), div-by-zero (C4), packed frames (G3), did-you-mean (D3).
            Unverified old claims → tasks in Phase 1.

2026-11-21  T0a/b/c  Discoverable test harness: D-005 EXPECT-* headers on all 162 test files (exit codes from the old map, compile-error substrings from real diagnostics, reviewed stdout lines), auto-discovering runner with --filter/--target/-j and mktemp workdirs, exact stdout + diagnostic-substring checks on both targets, --self-test proving wrong expectations fail (wired into check.sh).  (318→322 passed, 0 failed, 2 helper skips; 324 run-equivalents)
2026-11-21  V12  Found during T0a stdout survey: riscv64 print(0) emits byte 0x00 instead of '0' (x86_64 correct). test_print/test_print_all keep exit-code checks only until this is fixed; task recorded in PLAN.md.
