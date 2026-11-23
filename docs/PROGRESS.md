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

## State of the project — end of Phase 1 (re-verified against code, 2026-11-22)

- Gate: `scripts/check.sh --full` passes with strict `-Werror` build and ASan/UBSan. 416 integration
  tests passed / 0 failed / 2 skipped across x86_64 and riscv64; 209 discovered `tests/**/*.cyan`
  files (418 run-equivalents). `git status` is clean after the gate.
- C++ unit tests: 106 cases / 3813 checks (<0.01 s). Oracle: 29 C/Cyan twin pairs, 58 comparisons on
  both targets, 0 mismatches. Sweep: 836 `--emit-ir`/`-S` invocations, 0 crashes. Fuzz: 1000 seeded
  mutants per gate, >50k in ad-hoc campaigns. Harness self-test, path-injection test and oracle all
  run in the gate.
- Phase 1 fixes, all with tests: V1 call-site stack alignment; V2 four spill/codegen bugs (vreg vs
  physical-reg confusion, shared scratch for spilled args, spill slots overlapping locals, ADD/MUL
  source clobber); V6 16-byte allocator alignment; V7 `read_eof()` builtin + bounded `read(buf)` +
  write-all in print; V8 included-file caret context; V11 riscv64 auipc+jalr calls. V12 and the signed
  `>>` bug were found by the oracle and fixed earlier. V3/V4/V9 verified with no further bugs.
- Verified but not reproducible with current syntax: V5 (typed array/struct element stores need Phase 3
  Y2/Y3) and parts of V3 (typed params/returns, u64 literals).
- Known open items: V2b (nested/recursive calls with ~20 live values clobber caller-save registers) is
  documented in docs/BLOCKED.md after three unsuccessful fixes. riscv64 still requires `--no-relax`:
  relaxation corrupts label-address computations used by pointer arithmetic (oracle mismatches when
  disabled); the older JAL range limit is gone. README.md remains unverified (D1).

2026-11-22  V1  Stack-passed arguments: added integration test with 9/11 args, nested calls as args and recursion with stack args — passes both targets. Assembly inspection found a real ABI bug: with an even number of stack args the stack was 8-byte misaligned at call sites (callee-save pushes toggled parity). IREmitter now pads call sites based on the number of entry PUSHes; verified 50 x86_64 and 20 riscv64 call sites aligned, with four new unit tests checking emitted parity. Also found that >=13 args on x86_64 produce wrong results from spill-slot collisions across calls — that is V2 territory and is left for that task.  (388→390 passed, 0 failed; unit 94→98)
2026-11-22  V2  Register pressure/spills: fixed four distinct bugs. (1) replace_virtual_regs + insert_spill_code were separate passes, so physical register numbers were looked up as spilled vreg IDs; merged into one pass. (2) All spilled CALL arguments were loaded into one scratch register before pushing; added PUSH_FRAME so each is pushed directly from its slot, with cleanup bytes carried on CALL.imm_arg and alignment padding emitted before the args. (3) Spill slots started at [fp-8], colliding with locals/params; they now begin below the locals frame and the frame is enlarged. (4) ADD/MUL emitted `mov dst,op0; add dst,op1`, clobbering op1 when dst aliased it; commutative operands are now swapped. Verified 6..24 args on both targets; new test_spill_pressure.cyan covers 20 values in calls and loops. Remaining nested/recursion caller-save clobber recorded as V2b.  (390→392 passed, 0 failed)
2026-11-22  E2  Semantic error recovery: Generator::fail now records into the engine and throws GenAbort; gen_prog wraps each function and gen_func_def wraps each statement, so undeclared identifiers etc. in later statements/functions are still reported. gen_prog renders all diagnostics at the end. Updated const-eval unit cases to read the generator engine instead of g_lsp_errors. Verified 3 semantic errors across 3 functions; added test_bad_multiple_semantic_errors.cyan and scripts/test_semantic_recovery.sh (wired into check.sh).  (418→420 passed, 0 failed)
2026-11-22  E1  Parser error recovery: errors now throw a recoverable ParseAbort; parse_prog and parse_block catch it and synchronize to ;/}/declaration boundaries, with a 20-error cap (D-004) and all diagnostics printed at the end. Added statement-level recovery to avoid cascades. Verified 3 independent errors in one file produce exactly 3 diagnostics; added test_bad_multiple_errors.cyan and scripts/test_error_recovery.sh (wired into check.sh).  (416→418 passed, 0 failed)
2026-11-22  E0c  Codegen diagnostics: Generator now owns a DiagnosticEngine and exposes diagnostics(); all 56 gen/* error sites route through Generator::fail/fail_at, which record into the engine before the existing CLI/LSP emission path. Parser's remaining 14 explicit-location sites route through fail_at. The global g_lsp_errors list is now used only by the lsp_exit emission shim and lsp.cpp (to be addressed with E5). Gate green.  (416→416 passed, 0 failed)
2026-11-22  E0b  Tokenizer/parser diagnostics: both now own a DiagnosticEngine (not global) and expose it via diagnostics(); Parser::error and the tokenizer's direct error sites record into that engine before emitting. 14 parser sites that wrote g_lsp_errors directly were migrated. CLI/LSP behavior unchanged; gate green.  (416→416 passed, 0 failed)
2026-11-22  E0a  Diagnostic engine core: added src/source_loc.hpp (SourceLoc moved out of tokenization.hpp) and src/diagnostic.hpp with Diagnostic/Severity/FixIt and a stateful DiagnosticEngine (errors, warnings, notes, fix-its, counting, D-004 rendering). format_err now renders through the engine. Ten C++ unit cases cover formatting, severities, notes/fix-its, clearing and per-instance independence.  (416→416 passed, 0 failed; unit 106→116)
2026-11-22  V11  riscv64 call range / relax: replaced `jal ra, target` with the `call` pseudo (auipc+jalr), removing the +-1 MiB R_RISCV_JAL limit; the 300000-element stack array now links and runs on riscv64, and test_large_array.cyan runs on both targets. Tested dropping --no-relax: the oracle then fails ptr_diff and ptr_malloc_store because relaxation rewrites label-address computations. Restored and documented --no-relax as a real remaining constraint (assembler and linker flags).  (415→416 passed, 0 failed)
2026-11-22  V10  Large frames/immediates: added test_large_immediates.cyan (full-range i64 literals, ±2048/2049 boundaries) — passes both targets. A 300000-element stack array (>2 MiB) works on x86_64 but fails to link on riscv64 with 'relocation truncated to fit: R_RISCV_JAL' because JAL's ±1 MiB range is exceeded; recorded on V11. Added test_large_array.cyan with TARGETS: x86_64 documenting that limit.  (412→415 passed, 0 failed)
2026-11-22  V9  Scoping/switch: added test_scoping_switch.cyan covering nested shadowing, global initialization before main, static locals persisting across calls, switch fall-through with declarations in case bodies, and break inside a switch nested in a loop. All behaviors already correct on both targets; no code changes needed.  (410→412 passed, 0 failed)
2026-11-22  V8  #line caret context: reproduced — an error inside an included file reported the right file:line but printed the wrong source line/caret because print_code_context always indexed the main preprocessed source. Added a per-file source map (register_source_file) populated by the compiler, preprocessor and LSP; print_code_context now selects the text for loc.file. Regression test: test_bad_include_error_context.cyan asserts the included line's unique marker appears in the diagnostic.  (406→410 passed, 0 failed)
2026-11-22  V7  I/O: added read_eof() builtin (runtime flag set by both read forms and shared across x86_64/riscv64) and recorded the convention in DECISIONS.md D-008; test_read_eof.cyan distinguishes a real value from EOF, plus a compile-error test for its arity. Found and fixed a buffer overflow: local arrays grow downward but read(buf) passed the highest slot as the start, so long lines corrupted the caller frame; it now passes the lowest slot (byte offset). Added test_read_bounded.cyan (200-byte line into a 32-byte buffer). print helpers now loop on write() until all bytes are emitted on both backends.  (402→406 passed, 0 failed)
2026-11-22  V6  Allocator: fixed the one reproducible claim — payloads were only 8-byte aligned despite the aligned(16) heap. HEADER_SIZE is now 16, MIN_ALIGN 16, and all size classes are multiples of 16. Verified: >2048 B blocks ARE recycled through large_free_list (the "leak" claim is not reproducible), zero-size returns null, reuse-after-free works, and 1024 x 1 MiB native stress (1 GiB) passes without heap growth. mmap/brk growth and full coalescing were NOT needed to meet the stress criterion and were not implemented; the fixed 2 MiB heap recycles blocks successfully. Added native 1 GiB stress case and test_allocator_cycles.cyan (16 x 64 KiB) for both targets. A 1024 x 1 MiB Cyan stress is blocked by V2b (call-clobber bug), not by the allocator.  (398→400 passed, 0 failed; unit 105→106)
2026-11-22  V5  Array/struct stores: not reproducible with currently supported syntax — typed array elements (`var a: i8[4]`) and typed struct fields (`var x: i8`) are rejected by the parser (Phase 3 Y2/Y3), so all arrays/fields are i64 and the unconditional 8-byte stores are correct. Added test_store_widths.cyan checking adjacent array elements and struct fields (including compound field assignment) on both targets. Revisit once typed layouts land.  (396→398 passed, 0 failed)
2026-11-22  V4  Type equality / pointer types: added seven C++ unit cases for Type::operator== (int widths, structurally equal pointers from different arenas, same-pointee pointers, T** shape, T* vs T**, type_size). Added test_pointer_types.cyan for T** dereference, pointer equality and i32/u8 diff scaling; passes on both targets. Pointer-to-struct field access (-> and (*p).f) is not implemented yet (Phase 3 Y7), so that sub-case is deferred.  (394→396 passed, 0 failed; unit 98→105)
2026-11-22  V3  Signed/unsigned semantics: added test_signed_unsigned.cyan covering arithmetic vs logical >> by operand type, signed div/mod with negatives, truncation at i8/u8/i16/u32 boundaries, compound assignment, ++/-- wrap, and mixed-signedness comparisons. All cases match C semantics on both targets — no further bugs reproducible in the currently implemented subset. Deferred to Phase 3: typed parameters/returns and u64 literals (>i64 max) are not yet accepted by the parser, so those V3 sub-cases cannot be tested yet. The >> bug found by the oracle was already fixed and committed earlier.  (392→394 passed, 0 failed)
2026-11-22  V2b  Call-clobber bugs fixed (root causes found by review, not by the earlier allocator experiment). (1) A value live across a CALL could fall back to a caller-save register when callee-save registers ran out; such ranges now get callee-save registers only, else are spilled (eviction restricted to registers the new range may use); a last use AT the call no longer counts as spanning it. (2) Register arguments were moved one at a time, so a source sitting in another argument register was overwritten; the emitter now does a parallel move with cycle breaking via the scratch register, immediates last. (3) Two or more spilled register arguments shared one scratch register; they are now loaded straight into their argument registers (IRInstruction::arg_loads). (4) riscv64 spill slots used raw s0 offsets while locals use addr_fp (-16), so spills overlapped locals; added TargetRegisterInfo::fp_bias. Repro: chains of 3..60 nested add(id(i),..) and s8(id(..)) were wrong on x86_64 and riscv64; test_call_pressure.cyan (30 live temps, 8-arg calls with call args, 20-arg recursion) fails before and passes after on both targets.  (420→422 passed, 0 failed)
2026-11-22  PLAN  Added O0 (loop-aware liveness) before O5: linear-interval liveness ignores back edges; harmless today because variables live in stack slots, but unsafe once values stay in vregs.
2026-11-22  E3a  Warning infrastructure: DiagnosticEngine now carries a WarnConfig (-w disables, -Werror promotes warnings to errors); Parser and Generator expose set_warn_config and print warning diagnostics without exiting, CLI parses -w/-Werror. Test runner gained EXPECT-WARNING, NO-WARNING and COMPILE-FLAGS headers (documented in D-005). Four C++ unit cases cover default/suppressed/promoted warnings.  (422→422 passed, 0 failed; unit 116→120)
2026-11-22  E3b  Unused variable/parameter warnings: Var now records declaration location, read flag and parameter status; all read paths (identifiers, indices, field access, address-of, read(buf), compound assignments) mark a variable used. Scope exit emits `unused variable`, function exit emits `unused parameter`. Added warning fixtures for both, nested scope, write-only vars, a used control, plus -Werror and -w cases (14 run-equivalents on both targets).  (422→434 passed, 0 failed)
2026-11-22  E3c  Flow warnings: new AST pass in src/gen/warnings.cpp detects statements after an unconditional return/exit/break/continue (one warning per dead run, with a source location) and functions whose body can fall off the end (main exempt because it defaults to exit(0)). if/else and switch/default cases are handled; loops may execute zero times. Added unreachable (incl. break), missing-return, all-paths-return control, -Werror and -w fixtures. Fixed a duplicate unused-variable report caused by warning the function scope twice.  (434→452 passed, 0 failed)
2026-11-22  E3d  Final warnings: narrowing assignment warns when a compile-time constant does not fit a declared integer type (declaration and assignment); unused result warns when a pure value-returning function call is discarded; `=` in condition warns at the `=` token with a `==` fix-it (the syntax error remains). Added -Werror and -w cases for each new warning. E3 is complete; all six warning kinds and both flags are covered on both targets.  (452→474 passed, 0 failed)
2026-11-22  E4  Notes and color: DiagnosticEngine gained add_note_at (own location) and configurable ANSI rendering; parser/generator now render through the engine so colors reach both stages. Redeclarations attach "previous declaration here" at the first declaration; "did you mean" errors attach a note and a replacement fix-it (message text unchanged). CLI adds -fcolor-diagnostics/-fno-color-diagnostics with NO_COLOR precedence; scripts/test_color.sh checks all four modes and is wired into check.sh. Three new C++ cases.  (470→472 passed, 0 failed; unit 120→123)
2026-11-22  E5  LSP diagnostics: parse_document now collects from the tokenizer/parser/generator DiagnosticEngines (deduplicated, with legacy lsp_exit sites folded in) instead of only g_lsp_errors; publishDiagnostics maps Error/Warning/Note to LSP severities 1/2/3 and appends notes/fix-its to the message. Generator/LSPAbort recovery now continues across statements and functions so multiple errors and warnings are published from one document. Added tests/lsp/test_lsp_diagnostics.py (scripted stdio session) wired into check.sh.  (470→470 passed, 0 failed)
2026-11-22  P2-END  STATE OF THE PROJECT (verified against code at HEAD de72f9b). Phase 2 complete: DiagnosticEngine is instance-owned by tokenizer/parser/generator with D-004 rendering, error recovery, six warning kinds with -w/-Werror, notes/fix-its and configurable ANSI color; LSP publishes all engine diagnostics with severities. `scripts/check.sh --full` is green: 470 integration passed / 0 failed / 2 skipped (472 run-equivalents, both targets), 123 C++ cases / 3860 checks, differential oracle 58/58, IR/assembly sweep 944 invocations / 0 crashes, LSP and color script checks OK, fuzz 1000 mutations / 0 crashes, ASan/UBSan suite green. Tree clean. Remaining known limitations: tokenizer still has legacy lsp_exit sites feeding g_lsp_errors (folded into LSP output); `=` in condition remains a syntax error with a warning hint; typed fields/arrays/params remain Phase 3 work; README.md still unverified (D1).
2026-11-22  Y1a  Typed signature syntax: added an `->` token and `Type::Ptr` parsing (`T*`, `T**`) via a new Parser::parse_full_type; NodeFuncDef now carries parallel param_types (empty = untyped) and an optional return_type. AST printer shows annotations. Four C++ cases cover typed/pointer/nested-pointer and legacy untyped signatures; generator behavior is unchanged.  (470→470 passed, 0 failed; unit 123→127)
2026-11-22  Y1b  Typed params and call checking: declared parameter types now override call-site inference for frame slots and loads; typed calls check arity and argument types, allow safe widening (with sext/zext) and constants that fit the target type, and reject narrowing/mismatched pointer types with file:line:col diagnostics. Inference is suppressed for typed functions to avoid conflicting-pointer warnings. Eight new tests on both targets (positive i32/u8/pointer, widening, literal fit, arity/type/negative/pointer-mismatch errors).  (470→486 passed, 0 failed)
2026-11-22  Y1c  Return types: declared return types are checked at every `return`, with safe widening (sext/zext), fitting constants, and `0` accepted as a null pointer literal; mismatches are compile errors naming the function. Call inference now uses declared return types, and int +/- propagate the wider operand type on the type stack. Six new tests on both targets (i32 return, widening, literal, null, mismatch). Y1 complete.  (486→498 passed, 0 failed)
2026-11-22  Y2a  Struct fields can be typed (`var x: i8;`), with a parallel field_types list (untyped = i64). StructInfo now computes C-rule byte layout (natural alignment, padding, trailing pad) via a static helper; field offsets and struct size are bytes throughout codegen. Four C++ cases cover parser shapes and layouts (i8/i32/i16, i8/i64, packed bytes).  (498→498 passed, 0 failed; unit 127→131)
2026-11-22  Y2b  Typed field access: struct field reads use width/signedness-aware loads and field writes use width-aware stores with truncation (plain and compound assignments); &obj.field now yields Ptr(field type). This fixes the V5-style neighbor corruption for packed i8 fields. Three tests on both targets (packed bytes, mixed layout, compound assignment). One expectation was corrected after realizing 250+20 truncates to 14, not 7.  (498→504 passed, 0 failed)
2026-11-22  Y2c  sizeof builtin: new NodeExprSizeof AST node parsed for `sizeof(type)`, `sizeof(Struct)` and `sizeof(expr)`; evaluated as a compile-time constant and emitted as an i64 literal. Ident operand resolves to its declared type; all other operands default to 8 bytes (correct for pointers). Three tests on both targets plus a parser unit case. Y2 complete.  (504→508 passed, 0 failed; unit 131→132)
2026-11-22  Y3  Typed local arrays: `var a: i32[10];` is parsed into NodeStmtArrDecl.elem_type; Var records elem_size/elem_type. Index loads/stores and address-of scale by element width and use typed accessors with truncation; array-to-pointer decay now yields Ptr(element type), so typed pointer params accept typed arrays. sizeof(array) returns total bytes. Added a byte-accurate zero-init helper (fixes narrow immediate stores and padding overrun). Four tests on both targets; full gate green.  (508→516 passed, 0 failed)
2026-11-22  Y4  bool/char/literals: added tokenizer keywords (true, false, null, bool, char); bool and char map to 1-byte u8, the three literal keywords map to integer literals 1/0/0 so existing typing/pointer-null paths accept them. Typed locals now carry a full Type, enabling `var p: u8* = null;`. Two integration tests on both targets plus a tokenizer unit case.  (516→520 passed, 0 failed; unit 132→133)
2026-11-22  Y5  Casts: added `as T` keyword/postfix operator at unary precedence with NodeExprCast; codegen truncates/extends integer values and retypes the result, const-eval folds casts, and inference uses the target type. Explicit casts let programs satisfy typed parameter/return rules where implicit narrowing is rejected. Three tests on both targets (narrowing, widening, bad cast type).  (520→526 passed, 0 failed)
2026-11-22  Y6a  Type aliases: `type Name = T;` is parsed into NodeTypeAlias and registered in a parser alias table, so aliases resolve in var/param/return/cast/sizeof annotations to concrete Types (pointer aliases included). Duplicate aliases are errors. Two tests on both targets.  (526→530 passed, 0 failed)
2026-11-22  Y6b  Enums: `enum Color { Red, Green = 5, Blue };` is parsed with auto-incrementing values and duplicate-enumerator errors; the generator registers enumerators as compile-time constants before codegen so they work in expressions and var initializers. Two tests on both targets (one expectation corrected: c = Green = 5). Y6 complete.  (530→534 passed, 0 failed)
2026-11-22  Y7a  Struct identity in the type system: Type gained a Struct kind/name, so struct-typed locals and &s produce Ptr(Struct) that survives parameters, returns and sizeof. Parser accepts struct names as types (including `Point*`). One integration test and one unit case.  (534→536 passed, 0 failed; unit 133→134)
2026-11-22  Y7b  Pointer-to-struct access: `p->field` parses into NodeExprFieldAccess(arrow) and `p->field = value` into NodeStmtArrowAssign; codegen resolves the struct through Ptr(Struct), uses byte offsets and typed accessors, and supports compound assignment. `&s.field` was already supported and now has a test. Two tests on both targets.  (536→540 passed, 0 failed)
2026-11-22  Y7c  Nested struct access: NodeExprFieldAccess/NodeStmtFieldAssign gained a base expression, so `a.b.c` works for reads and writes. Generator::field_addr resolves each chained field through its struct type; nested struct fields use real earlier-computed sizes in layout. Two bad_optional_access crashes in static inference were fixed by guarding empty base tokens. One test on both targets; full gate green.  (540→542 passed, 0 failed)
2026-11-22  Y7d1  Arrays of structs: `var a: Point[2]` uses the struct's real byte size for elements; `a[0].field` reads/writes are resolved through an index-expression field base. Parser keeps plain `a[i] = v` working by reconstructing NodeStmtArrAssign. One test on both targets; full gate green.  (542→544 passed, 0 failed)
2026-11-22  Y7d2  Struct literals: `{ .x = 1, .y = 300 }` parses into NodeExprStructLit and initializes typed struct locals field-by-field with width-aware stores; unknown fields are compile errors. Two tests on both targets. Y7 complete.  (544→548 passed, 0 failed)
2026-11-22  Y8a  Struct assignment: `a = b` for typed struct locals copies the struct's byte size with width-aware chunked loads/stores; source offsets run downward from the struct base. One test on both targets; full gate green.  (548→550 passed, 0 failed)
2026-11-22  Y9a  Multi-dimensional arrays: `var a: i32[2][3]` parses all dimensions and computes row-major strides; `a[i][j]` reads/writes (and field access on indexed structs) resolve through a shared index_addr helper. One test on both targets; full gate green.  (550→552 passed, 0 failed)
2026-11-22  Y9b  Untyped parameters now emit a warning that call-site inference is a fallback (suppressible with -w); cyanlib/string.cyan migrated to typed u8*/i64 signatures and the full gate still passes. Const-write rejection was already covered by test_bad_const_assign.cyan. Y9 complete; remaining test-suite migration is incremental standing work.  (552→556 passed, 0 failed)
2026-11-22  P3-END  STATE OF THE PROJECT (verified against code at current HEAD). Phase 3 complete except Y10 (blocked, see docs/BLOCKED.md: indirect-call lowering needs an ABI decision). Typed signatures, struct/array layouts and access, sizeof, casts, bool/char/null, aliases, enums, pointer-to-struct access, nested structs and literals, struct assignment, multidimensional arrays and untyped-parameter warnings are all implemented and tested on x86_64 and riscv64. `scripts/check.sh --full` green: 554 integration passed / 0 failed / 2 skipped (556 run-equivalents), 134 C++ cases / 3922 checks, oracle 58/58, sweep 1112 invocations / 0 crashes, LSP/color checks OK, fuzz 1000 mutations / 0 crashes, ASan/UBSan green. Tree clean. Two blocked ABI decisions (Y8b, Y10) remain for a dedicated phase. README.md still unverified (D1).
2026-11-22  Y2d  Found while starting Y8b: local arrays/structs with sub-8-byte elements corrupted neighbouring locals (`var a: i32[2]; var b: i64; a[1] = 6;` clobbered `b`), narrow scalars packed before wider ones overlapped, and `ptr + n` on a decayed local array walked backwards. Cause: aggregates were addressed downward and both backends stored 16/32-bit values byte-reversed at `addr-w+1`. Fixed by adopting C layout (D-013): ascending aggregates (`var_addr`/`object_addr`/`alloc_object` helpers), 8-byte-granular frames, plain little-endian narrow loads/stores in both backends, `read(buf)` fills forward. Three regression tests (array neighbours, struct neighbours, little-endian byte view) on both targets; full gate incl. ASan/UBSan green.  (554→560 passed, 0 failed)
2026-11-22  Y8b1  Struct values as call arguments and `p: Point` parameters for structs <= 16 bytes (D-012): a struct expression evaluates to its address with `Type::Struct`, the call site splits it into 8-byte argument words (alignment-aware chunked loads, only real bytes read), the callee rebuilds it in an 8-aligned frame slot. Wider structs, x86_64 register/stack straddles and untyped params are diagnosed. Also fixed: parameters are now stored by a new `STORE_PARAM` IR op straight from the argument register to the frame slot — the old LOAD_PARAM path let an allocator temporary land in an argument register that had not been read yet (exposed by the first test with a third live temporary). Struct-array elements and nested struct fields now yield struct values instead of an 8-byte load. 9 new tests (both targets unless TARGETS-limited); full gate incl. ASan/UBSan green.  (560→580 passed, 0 failed)
2026-11-22  Y8b2  Struct returns <= 16 bytes (D-012): `return s` loads the struct into one word (rax / a0) or two (rax:rdx / a0:a1) through a new `RET2` IR op; a call to a struct-returning function uses `CALL` with a second destination (`dst2`) and the caller stores the words into a temporary frame object whose address is the expression value. The rewriter lowers RET2 and captures dst/dst2 with explicit moves through the scratch register, so homes that are themselves return registers or spill slots are safe. `var p: T = f()` and `p = f()` copy from the temporary; mismatched struct types, untyped struct returns and >16-byte returns are diagnosed. 6 negative tests, 2 run tests (both targets) plus 7 C++ unit cases for the RET2/dst2 register-home combinations; full gate incl. ASan/UBSan green.  (580→596 passed, 134→141 unit cases, 0 failed)
2026-11-22  Y8b3  Structs larger than 16 bytes: returned through a hidden first-argument pointer (the callee stores the pointer in a frame slot, copies the struct into it and returns it; the caller supplies a frame temporary and shifts the visible arguments by one word); on riscv64 passed by reference to a caller-made copy, which the callee uses in place via the new `Var::by_ref` (all aggregate accesses already go through `var_addr`). `copy_memory` now backs every struct copy and honours the struct's alignment instead of always using 8-byte accesses. The removed `test_bad_struct_return_large` is superseded by the positive return test; `test_bad_struct_param_large` is now x86_64-only until Y8b4. Tests: test_struct_return_large (both targets), test_struct_param_large_riscv; full gate incl. ASan/UBSan green.  (596→596 passed: +3 run-equivalents, -2 removed negative test; 0 failed)
2026-11-22  Y8b4a  Argument placement is now decided in one place (`ArgPlacer`) used by call sites and function entry, and CALL carries `reg_args` so register words and stack words are separate lists. Consequences: x86_64 structs > 16 bytes are copied to the stack, a struct that does not fit in the remaining registers goes whole to the stack while later scalars still use the free registers (System V rule), riscv64 keeps its register/stack split and by-reference rule; the "cannot be passed by value yet" and straddle diagnostics are gone, with their two negative tests replaced by positive ones (test_struct_param_large now runs on both targets; test_struct_param_backfill is new). 7 ArgPlacer unit cases; full gate incl. ASan/UBSan green.  (596→597 passed run-test count; 141→148 unit cases; 0 failed)
2026-11-22  Y8b4b / Y8  C-interop proof of D-012: `scripts/test_abi_interop.sh` (now a `check.sh` step) links gcc-compiled C against Cyan output on both targets, in both directions — a C `main` calling Cyan functions with C structs, and a Cyan `main` calling C functions through deliberately wrong Cyan stubs that are overridden at link time (x86_64 needs call-site aliasing via tiny jmp trampolines because NASM binds same-file calls at assembly time; riscv64 uses ordinary relocations). Covers struct params/returns of 1, 3, 6, 8, 12, 16, 24, 40 bytes and a padded mixed struct, register-position mixes, x86_64 backfill, riscv64 register/stack straddle, two large structs, and returns through rax:rdx / hidden pointer — every check passes on both architectures against gcc. Also fixed: riscv64 functions were not global symbols (`.extern` is ignored by GAS; now `.globl`). Y8b and the Y8 struct-by-value work are complete. Full gate incl. ASan/UBSan green.  (597 passed run tests, 6 skipped = 2 included files + 4 interop sources; 148 unit cases; ABI interop 4/4)

## 2026-11-22 — Y10a CALL_INDIRECT
Added the `CALL_INDIRECT` opcode (target = last operand) through IR, liveness, dump, rewriter, emitter and both backends (`Backend::call_reg`, `TargetRegisterInfo::call_target_reg` = rax / t6). The target takes part in the argument parallel move, so it is read before argument setup overwrites its register; a spilled target is loaded after the moves. Tests: `tests/cpp/test_indirect_call.cpp` (12 cases covering plain, argument-register, call-register, spilled, immediate and stack-argument layouts on both targets); shared helpers moved to `tests/cpp/emit_helpers.hpp`. Unit cases 148 -> 160; run tests 597 passed; `check.sh --full` OK.

## 2026-11-22 — Y10b / Y10 Function pointers
Language side of D-014: `fn(T, ...) -> R` types (variables, parameters, struct fields, returns, arrays), `&func`, and calls through any callee expression. A function pointer is `Ptr(Func)`; `Type` gained a `Func` kind with an arena-allocated parameter array (`ArenaAllocator::alloc_array`). The parser wraps primary expressions in a call-suffix loop (`NodeExprCall::callee`), call lowering is shared between direct and indirect calls via `CallTarget`, and `check_call_args` takes a signature so indirect calls get the same arity/type checks. Initialization and assignment of function-pointer variables are checked against their declared type; call expressions now carry their declared return type. Tests: run tests `test_fnptr_{basic,abi,infer,expr}` (callbacks, tables, struct fields, 10 arguments, struct args/results of every size class, recursion, shadowing), 9 compile-error tests `test_bad_fnptr_*`, 3 parser and 1 type unit cases, and 4 C-interop programs (`tests/abi/fnptr_*`: Cyan calling C pointers and C calling Cyan pointers, on both targets, vs gcc). Run tests 597 -> 621 passed, unit cases 160 -> 164, ABI interop 4 -> 8.

## 2026-11-22 — L1 String library
`cyanlib/string.cyan` now exposes all twelve typed functions: added `strncmp`, `strncpy`, `strchr`, `strstr` and overlap-safe `memmove`, and rewrote `strcmp`/`strcpy` without `while (1)` so including the header is warning-free. Test-first: `tests/unit/test_string_full.cyan` calls the five missing functions and failed to compile until they existed. The new test checks every function on both targets, covering empty strings, embedded NUL, truncation/padding boundaries, n=0, unsigned byte comparison, both overlap directions for memmove, and found/not-found pointer results for strchr/strstr. `scripts/check.sh` green: 621 -> 623 passed, 0 failed; sweep 1264 -> 1268 invocations, 0 crashes.

## 2026-11-22 — L2a print specifiers
Extended `print`'s format string with `%x`, `%u`, `%c` and `%ld` (D-015). `FormatSpec` gained three conversion kinds; the parser now recognizes them and rejects `%l` not followed by `d`; codegen calls new runtime helpers registered as externs on both targets. Added `sodium-rt/printfmt.c` (freestanding conversion into a shared buffer) and `_sodium_write_buf` to x86_64/riscv64 `print.asm` so `%c` writes exactly one byte (NUL included). CMake now tracks runtime sources so both archives rebuild when they change. Tests: positive `test_print_format_ext.cyan` (hex, unsigned full range via `-1`, char, `%ld`, mixed conversions) and two compile-error tests (`%lx`, missing `%x` argument), all on both targets; NUL output verified byte-for-byte with `od`. `scripts/check.sh --full` green: 623 -> 629 passed, 0 failed; sweep 1268 -> 1280 invocations, 0 crashes; ASan/UBSan clean.

## 2026-11-22 — B3 x86_64 variable shift clobbers RCX
Found while running the L2b conversion library: register-counted `shl`/`shr`/`ashr` write the count into CL, destroying a live value the allocator had placed in RCX (the loop variable `r*2` in the new unsigned-division routine). The old backend only protected the case where RCX was the shift destination. All three shift emitters now preserve RCX in the reserved scratch r11 and restore it after the shift; when RCX is the destination the value is shifted in r11 and moved back. Regression: `tests/cpp/test_variable_shift.cpp` with three emitted-assembly cases, which fail before the fix. Unit cases 164 -> 167, checks 4000 -> 4003.

## 2026-11-22 — L2b fmt.cyan int-to-string
Added `cyanlib/fmt.cyan` with `fmt_uint`, `fmt_number` and `itoa`/`binstr`/`octstr`/`hexstr`/`hexstr_upper` wrappers for bases 2..36 (upper and lower alphabets). Because Cyan has no unsigned division, `udivmod` does bit-at-a-time long division over the 64-bit pattern using arithmetic shifts and masks, which makes the full u64 range printable despite the lack of large literals; `fmt_number` prefixes negatives and uses `~value + 1` so i64 min works. Test-first: `tests/unit/test_fmt_int.cyan` covers zero, both i64 boundaries, signed/unsigned hex and decimal, every common base and base 36 on both targets. Running it exposed B3 (variable shifts clobbering RCX on x86_64), fixed separately. `scripts/check.sh --full` green after both fixes.

## 2026-11-22 — L2c fmt.cyan string-to-int
Added `str_to_i64(s, base) -> ParseInt` (with `atoi`/`hex_to_i64` wrappers and `parse_status_name`) to `cyanlib/fmt.cyan`. It handles optional +/-, bases 2..36 and returns a struct with the value plus a status (ok, empty, invalid digit, overflow, invalid base). Overflow is checked with `udivmod` plus an unsigned greater-than helper, so both i64 max and the extra negative value i64 min are accepted exactly. `tests/unit/test_fmt_parse.cyan` covers signs, all bases, empty/invalid inputs and both overflow boundaries on both targets. A first draft forgot to apply the parsed sign; the gate caught it via the runner's exact stdout check and it was fixed. `scripts/check.sh` green: 631 -> 633 passed, 0 failed.

## 2026-11-22 — L3 Memory wrappers
Added `cyanlib/mem.cyan` with tracked `mem_malloc`/`mem_free`, `calloc` (zeroed, overflow-checked), `realloc` with an explicit old size, `aligned_alloc`/`aligned_free` for any power-of-two alignment, and `mem_live_count()` driven by the `SODIUM_ALLOC_DEBUG` compile-time switch (D-016). Test-first: `tests/unit/test_mem_lib.cyan` checks zeroing, the live counter at every stage, copy-preservation across realloc, address alignment, and rejection of overflowed products and non-power-of-two alignments on both targets. The first draft of the EXPECT-STDOUT headers expected prints that the test did not make; corrected the test to report the counter at each stage rather than weakening the expectation. `scripts/check.sh` green: 633 -> 635 passed, 0 failed; sweep 1288 -> 1292 invocations, 0 crashes.

## 2026-11-22 — L4a Raw file syscalls
Added generic `_sodium_syscall3/4` stubs in both backends plus `sodium-rt/syscalls.c` with open/close/read/write/lseek/unlink wrappers (riscv64 maps open/unlink to openat/unlinkat with AT_FDCWD). Compiler builtins `sys_open`, `sys_close`, `sys_read`, `sys_write`, `sys_lseek` and `sys_unlink` emit calls to those symbols, return i64, check arity, and are registered as externs (D-017). Initial link failed because the C wrapper object and the asm stub object shared a name; renamed them so both archives contain both. Tests: `test_syscalls.cyan` writes, seeks, reads back and unlinks a runner-supplied path on both targets plus a missing-file check; `test_bad_syscall_arity.cyan` asserts the arity diagnostic. `scripts/check.sh` green: 635 -> 639 passed, 0 failed; sweep 1292 -> 1300 invocations, 0 crashes.

## 2026-11-22 — L4b io.cyan
Added `cyanlib/io.cyan` with Linux flag/seek/fd constants, `eprint`/`eprintln` to stderr, `io_write_file`, `io_read_file` (malloc'd NUL-terminated buffer), `io_next_line` (with a cap) and `io_perror` (D-018). `tests/unit/test_io_lib.cyan` writes a file with embedded newlines, reads it back, iterates four lines including an empty one, checks the EOF and truncation boundaries, verifies a missing-file lookup returns null, and checks an empty file and the status helper; stderr output is asserted separately because the runner compares stdout. Both targets pass. `scripts/check.sh` green: 639 -> 641 passed, 0 failed; sweep 1300 -> 1304 invocations, 0 crashes.

## 2026-11-22 — L5a Vec and Str
Added `cyanlib/containers.cyan` with `Vec` (growable i64 array) and `Str` (byte-string builder), both passed by pointer with doubling capacity and explicit free; `str_build` finalises in place. `tests/unit/test_containers.cyan` is a property test: 200 pseudo-random pushes plus 50 random overwrites are compared element-by-element and against an incrementally maintained checksum; 60 random characters are appended to a Str and compared byte-for-byte with a reference buffer. Both targets pass. The first draft used `var g_seed: i64 = ...` at top level, which the parser rejects; globals require `global var`. `scripts/check.sh` green: 641 -> 643 passed, 0 failed; sweep 1304 -> 1308 invocations, 0 crashes.

## 2026-11-22 — B4 Struct-pointer arithmetic uses struct size
Found by the L5b hash-map work: `type_size` returns 8 as a fallback for `Type::Struct`, so `T* + n` on a struct pointer advanced 8 bytes instead of `sizeof(T)`, corrupting arrays of structs. Pointer add/sub and pointer difference now use `get_struct_info(name)->size` when the pointee is a struct. Regression `tests/unit/test_struct_ptr_arith.cyan` checks slot offsets byte-for-byte and pointer differences; it fails (exit 1) before the fix and passes on both targets after. Unit/gate otherwise unchanged.

## 2026-11-22 — L5b HashMap
Added an open-addressed `Map` (string→i64) to `cyanlib/containers.cyan` with linear probing, tombstones, doubling growth, copied keys, get/contains/remove/count/checksum/free (D-019). `tests/unit/test_hashmap.cyan` inserts 40 pseudo-random keys against a reference array, verifies every present key and an order-independent checksum, removes random keys, checks absence/presence and checksum again, and reinserts a removed key to exercise tombstone reuse; both targets pass. The first run exposed B4 (struct-pointer arithmetic used 8-byte strides), fixed separately; two test-side assumptions (40 distinct random keys and checking absent keys) were also corrected rather than weakening the implementation. `scripts/check.sh` green: 645 -> 647 passed, 0 failed; sweep 1312 -> 1316 invocations, 0 crashes.

## 2026-11-22 — L6a exit and envp
Added `exit(n)` and `envp(i)` builtins and a `__sodium_envp` global captured in `_start` on both targets. The first envp attempt assumed a fixed stack offset; the vector actually follows argv's NULL terminator, so the address is `sp/rsp + 16 + argc*8` and argc must be reloaded after it is reused for argv (both bugs showed as null/segfault and were caught by the new test). Added repeatable `// ENV: NAME=value` support to the test runner (D-005/D-020). Tests: `test_exit_builtin.cyan` checks an early exit status, `test_envp.cyan` finds a runner-supplied variable, `test_bad_exit_args.cyan` asserts the arity diagnostic; all on both targets. `scripts/check.sh --full` green: 647 -> 653 passed, 0 failed; ASan/UBSan clean.

## 2026-11-22 — L6b fd printing and assert
Added `fprint(fd, s)` and `assert(cond, msg)` to `cyanlib/io.cyan`: fprint writes via sys_write and returns the byte count; assert reports on stderr and calls the L6a `exit(1)` builtin. Added `// EXPECT-STDERR: <substring>` to the test runner (substring match, unlike exact stdout lines). Tests cover writing to a file descriptor, a passing assert, and a failing assert's exact message and exit status on both targets; the first fprint draft triggered an unused-result warning, fixed by asserting the returned byte count. `scripts/check.sh --full` green: 653 -> 659 passed, 0 failed; ASan/UBSan clean; sweep 1340 invocations, 0 crashes.

## 2026-11-22 — P4-END STATE OF THE PROJECT
Phase 4 complete, verified against code at HEAD 93d6c7a. The Cyan standard library now ships `string.cyan` (12 functions incl. overlap-safe memmove), `fmt.cyan` (int-to-string for bases 2..36 and string-to-int with statuses), `mem.cyan` (calloc/realloc/aligned alloc with a leak counter), `io.cyan` (whole-file read/write, line reader, stderr, fd printing, assert), and `containers.cyan` (Vec, Str, open-addressed HashMap). New builtins: `%x/%u/%c/%ld` print conversions, six raw file syscalls, `exit(n)` and `envp(i)`; both backends have syscall stubs and a captured __sodium_envp. Two compiler bugs found by library tests were fixed separately (B3 variable shifts clobbering RCX, B4 struct-pointer arithmetic using the 8-byte fallback). `scripts/check.sh --full` green: 659 integration passed / 0 failed / 10 skipped (669 run-equivalents), 167 C++ cases / 4003 checks, oracle 58/58, ABI interop 8/8, sweep 1340 invocations / 0 crashes across --emit-ir and -S on both targets, LSP/color/path/fuzz checks OK, ASan/UBSan clean. Tree clean; commits are local only. README.md still unverified (D1, Phase 7).

## 2026-11-22 — Test runner: report skipped count in Results line
`scripts/run_tests.sh` now prints `Results: N passed, N failed, N skipped`, so the total matches `Run-equivalents` (659 + 0 + 10 = 669). The 10 skips are intentional: 8 ABI interop cases (run by `scripts/test_abi_interop.sh`, which needs the C object) and the 2 include helper cases. No test added or changed; count unchanged.

## 2026-11-22 — O1 IR verifier
Added `src/ir/verify.hpp`: checks unique block labels, JMP/BR targets (no dangling labels),
terminator shape, operand counts/kinds/widths, and CFG dominance (a use must be dominated by a
def of that same vreg). Fallthrough blocks and dead instructions after a terminator are tolerated,
matching the generator's IR. `--verify-ir` requests a check explicitly; Debug builds (including
the ASan build) define `SODIUM_IR_VERIFY` and always run it. Added 19 C++ cases covering each
check and extended `scripts/sweep.sh` with a `--verify-ir` mode over every test on both targets
that fails on an `IR verification failed` message. The verifier exposed a real ternary codegen
bug: each arm left its own vreg on the vstack and the join used only the else one, which happened
to work because the allocator gave both the same physical register. Ternary now defines a shared
result vreg before the branch and copies each arm into it; the regression
`tests/unit/test_ir_verify_ternary.cyan` uses `// COMPILE-FLAGS: --verify-ir` and fails with the
old codegen on both targets. `scripts/check.sh --full` green: 659 -> 661 passed, 0 failed,
10 skipped; 167 -> 186 C++ cases / 4023 checks; sweep 1340 -> 2016 invocations (added verify
mode), 0 crashes; ASan/UBSan clean.

## 2026-11-22 — O2 Constant folding
Added `src/ir/fold.hpp`, run from `flush_function` under the new `-O1` flag (`-O0` stays the
default). It folds pure constant arithmetic/comparisons/conversions with wrapping two's-complement
semantics, leaves trapping divisions alone, and applies algebraic identities (x+0, x*1, x*0,
x^x, shift-by-zero, x*2^n → SHL). IR is verified before and after the pass, and `--emit-ir` now
dumps the post-pass IR so its effect is observable. Added 20 C++ cases (including wraparound,
trapping division, width conversions, multi-def vregs) and
`tests/unit/test_opt_fold.cyan` with `// COMPILE-FLAGS: -O1` for semantics on both targets.
`scripts/test_opt_effects.sh` checks the IR effect and that `-O0` does none of it; `scripts/check.sh`
now also runs the whole suite through `scripts/sodium-o1.sh`. Running that suite exposed a real
bug: constants from phi-like ternary result vregs were propagated using linear order, so nested
ternaries folded to the wrong arm; folding now propagates only single-def vregs. `scripts/check.sh
--full` green: 661 -> 663 passed, 0 failed, 10 skipped at both -O0 and -O1; 186 -> 206 C++ cases /
4071 checks; sweep 2016 -> 2022 invocations, 0 crashes; ASan/UBSan clean.

## 2026-11-22 — O3 CFG cleanup
Added `src/ir/cleanup.hpp` under `-O1`, after folding: drops unused pure results and dead tails
after terminators, removes unreachable blocks, and threads jumps through empty/jmp-only blocks.
Multi-def vregs are kept as a unit so dominance survives. Added 14 C++ cases and
`tests/unit/test_opt_cleanup.cyan` for loops/branches/ternaries under `-O1`; the IR-dump effect
check now also asserts that dead instructions after `ret` disappear at `-O1` but remain at `-O0`.
Running the whole `-O1` suite exposed a real bug: removing a jmp-only block changed the fallthrough
of its predecessor into the following block, crashing the hashmap library test on both targets;
the predecessor now gets an explicit JMP to the removed block's target, with a regression unit
test. `scripts/check.sh --full` green: 663 -> 665 passed, 0 failed, 10 skipped at both levels;
206 -> 220 C++ cases / 4115 checks; ASan/UBSan clean.

## 2026-11-22 — O4 Local CSE and store-to-load forwarding
Added `src/ir/cse.hpp` under `-O1`, between folding and cleanup. Repeated pure instructions
become copies; an 8-byte load after a store to the same `[base + offset]` is forwarded; duplicate
stores of the same value are dropped. Calls/syscalls/stores discard facts, loads are not CSE'd,
and DIV/MOD are barriers because of their implicit rax/rdx clobber. Added 14 C++ cases and
`tests/unit/test_opt_cse.cyan` for aliasing pointers and calls that write memory; the IR-dump
effect check now asserts CSE produced copies at `-O1` but not `-O0`. The first implementation
was quadratic (20 s timeout on `test_large_array.cyan`), so facts are now indexed by vreg; running
the `-O1` suite then found three wrong results/crashes because CSE extended a frame-address live
range across MOD (rax/rdx clobber), fixed by treating DIV/MOD as barriers. `scripts/check.sh --full`
green: 665 -> 667 passed, 0 failed, 10 skipped at both levels; 220 -> 234 C++ cases / 4141 checks;
sweep 2022 -> 2034 invocations, 0 crashes; ASan/UBSan clean.

## 2026-11-22 — O0 Loop-aware liveness
Replaced linear-order liveness in `src/ir/liveness.hpp` with CFG dataflow: per-block
live-in/live-out sets are iterated to a fixpoint, and each vreg's single interval spans all
blocks where it is live. Added 5 C++ cases (loop with a later call, allocator choice on both
targets, nested loops, loop-carried values, if/else) — the loop/call repro failed before the fix
with `spans_call == false` and a caller-save register on both targets, and passes after. The
hand-built IR is required for that repro because generated Cyan code currently reloads locals
from frame slots every time; keeping a value in a vreg across a back edge becomes possible with
O5. Added `tests/unit/test_loop_liveness.cyan` (loops with calls and nested loops), run by both
the -O0 suite and the -O1 suite on x86_64 and riscv64. `scripts/check.sh --full` green: 667 ->
669 passed, 0 failed, 10 skipped at both levels; 234 -> 239 C++ cases / 4155 checks; sweep
2034 -> 2040 invocations, 0 crashes; ASan/UBSan clean.

## 2026-11-22 — O5a mem2reg IR pass
Added `src/ir/mem2reg.hpp` implementing D-027: frame slots whose addresses never escape and whose
every load is dominated by a store are promoted to a shared vreg. Stores become copies (with
masking/zero-extension for narrow widths) and loads become copies or sign/zero extensions, so
narrow-value semantics are preserved; the frame_addr instructions are removed. Slots used as
pointers, accessed at a nonzero offset, or read before a dominating store are left in the frame.
Added 9 C++ cases covering straight-line promotion, narrow widths, loop locals whose interval
spans a call, address-taken locals, missing dominance, several frame addrs for one slot and
non-frame memory. `scripts/check.sh` green; pass is not yet wired into the pipeline (O5b).

## 2026-11-22 — O5b mem2reg integration
Wired `promote_locals` into the -O1 pipeline after folding and before CSE, with a verifier check
after all passes. Added `tests/unit/test_mem2reg.cyan` (loop locals, i8/u8 narrow types, an
address-taken local written through a pointer, and an array) run at -O0 and -O1 on both targets,
and an IR-dump effect check that scalar locals have no frame slots at -O1. The -O1 suite hung on
`tests/integration/test_comprehensive.cyan`: mem2reg extended a loop variable's live range across
a MOD, and the allocator placed it in rax, which idiv clobbers. Added `LiveRange::crosses_div`
and made linear scan give those ranges callee-save registers; a C++ case checks both targets.
`scripts/check.sh --full` green: 669 -> 671 passed, 0 failed, 10 skipped at both levels;
248 -> 249 C++ cases / 4193 checks; sweep 2040 -> 2046 invocations, 0 crashes; ASan/UBSan clean.

## 2026-11-22 — O6a Regalloc measurement
Added spill/callee-save/vreg counters to `RegisterAllocation` and a `--dump-regalloc` CLI flag
that prints one line per function. `scripts/bench_regalloc.sh` compiles a fixed set of five
programs at -O1 on both targets and reports totals; the current baseline is 248 functions,
2 spills, 301693 callee-save assignments. Added 4 allocator unit tests (no spills in a simple
function, pressure with one register, call-spanning and DIV-crossing callee-save use).
`scripts/check.sh` green; O6b (heuristic improvement) is still pending.

## 2026-11-22 — O6b Next-use spill heuristic
Live intervals now carry sorted use positions and linear scan evicts the active value whose next
use is farthest away, protecting call/DIV-crossing values from being evicted by others. Added a
one-register unit test that checks the intended victim changes; a first implementation scanned
use lists linearly and timed out on `test_large_array.cyan` in the sweep, fixed by binary-searching
the sorted positions. `scripts/bench_regalloc.sh` is now part of `scripts/check.sh`; the benchmark
stays at 248 functions, 2 spills, 301693 callee-save assignments. `scripts/check.sh --full` green:
671 passed, 0 failed, 10 skipped at both levels; 253 -> 254 C++ cases / 4203 checks; sweep 2046
invocations, 0 crashes; ASan/UBSan clean.

## 2026-11-22 — O7a Per-target peephole pass
Added `src/backend/peephole.hpp`: a pure post-emission assembly pass (D-030) with target-specific
safe rules — self-move/no-op removal (`mov r,r`; `mv r,r`; `addi r,r,0`) and adjacent dead-move
elimination. Segments are processed independently between labels/directives so no rule crosses a
control-flow boundary; memory operands and flag-setting instructions are never touched. Wired into
`main.cpp` under `-O1` only, with counters reported by `--dump-regalloc`. Added 9 C++ cases on
emitted snippets and `tests/unit/test_peephole.cyan` (copy-heavy locals, calls, loops) run at -O0
and -O1 on both targets. The IR-effect script now asserts the pass removes at least one real
self-move from `test_comprehensive.cyan` and leaves none behind. `scripts/check.sh --full` green:
671 -> 673 passed, 0 failed, 10 skipped at both levels; 254 -> 263 C++ cases / 4227 checks; sweep
2046 -> 2052 invocations, 0 crashes; benchmark unchanged (2 spills); ASan/UBSan clean.

## 2026-11-22 — O7b1 Deferred function emission
Refactored `flush_function` to take an explicit `IRFunction*`, captured frame size and an
`emit_extern` flag instead of consuming `m_ir.end_function()` itself. Under `-O1` generated
functions are now moved into `m_pending_functions` and optimized/emitted together after all
functions are generated, so a later pass can see callee bodies; `-O0` still flushes immediately.
The extern declaration moved to flush time so the emitted assembly is byte-for-byte identical to
the old path: verified against a pre-refactor worktree build on three programs at both `-O0` and
`-O1`. Added an effect check that all functions remain present and in source order.
`scripts/check.sh` green: 673 passed / 0 failed / 10 skipped at both levels; 263 C++ cases /
4227 checks. No new tests yet — this is pure infrastructure for O7b2.

## 2026-11-22 — O7b2 IR leaf inliner
Added `src/ir/inline.hpp` and ran it over the deferred function pool before the normal -O1
passes. Eligible callees (single body block plus the generated ret_void epilogue, no calls,
branches, syscalls or RET2, at most 24 instructions, no LEA_LABEL reference) are spliced into
each call site with fresh vregs, parameter STORE_PARAMs turned into plain stores of the call's
argument vregs, and frame slots shifted below the caller's frame. Added 12 C++ cases and
`tests/unit/test_inline.cyan` (nested arithmetic leaves and a void leaf with a global side
effect) at -O0/-O1 on both targets; the effect script checks that real leaf calls are inlined.
The first eligibility rule also admitted struct-ABI callees, whose LEA/nonzero-offset accesses
broke nine struct tests at -O1; the rule was narrowed to scalar leaves with plain, full-width
parameter stores (D-031 refinement). `scripts/check.sh --full` green: 673 -> 675 passed / 0
failed / 10 skipped at both levels; 263 -> 274 C++ cases / 4265 checks; sweep 2052 -> 2058
invocations / 0 crashes; benchmark 301693 -> 301688 callee-save assignments (fewer calls);
ASan/UBSan clean.

## 2026-11-23 — O8a Benchmarks and instruction counts
Added five benchmark programs under `bench/` (recursive fib, sieve, 8x8 matmul, bubble sort,
linear-probing hash) with deterministic checksum outputs. `scripts/bench.sh` compiles each at
-O0 and -O1 on x86_64 and riscv64 and counts dynamically executed guest instructions using qemu
`-one-insn-per-tb -d exec`, then compares with committed baselines in `bench/baselines.txt` with
a 20% tolerance. -O1 reduces counts on every benchmark except the recursive fib (e.g. sort x86
342597 -> 296620, hash riscv 65146 -> 42006). The script is now a `check.sh` step and also
verifies each program's stdout. `scripts/check.sh` green: 675 passed / 0 failed / 10 skipped at
both levels; 274 C++ cases / 4265 checks.

## 2026-11-23 — O8b Default optimization level (D-032)
Verified the full suite (675/675 on both targets) and the differential oracle (58/58) under -O1,
then changed the compiler default from -O0 to -O1. Added `scripts/sodium-o0.sh` and made
`check.sh` run the suite and the oracle explicitly at both -O0 and -O1, so flipping the default
did not reduce coverage. Added an effect check that no-flag output equals -O1 output and differs
from -O0. Running all steps under the new default exposed three issues, all fixed: compile-error
tests produced invalid IR after optimization (errored functions now skip the O1 pipeline and
inlining), a void callee's used result vreg was left undefined by inlining (the CALL is kept),
and the ABI harness's `call SYM` rewriting no longer worked with inlined stubs (it now compiles
at -O0 explicitly). `scripts/check.sh --full` green: 675 passed / 0 failed / 10 skipped at both
levels; 274 -> 275 C++ cases / 4267 checks; oracle 58/58 at both levels; sweep 2058 invocations /
0 crashes; ABI 8/8; ASan/UBSan clean.

## 2026-11-23 — P5-END phase 5 state of the project
Re-verified against the code and gate logs before tagging Phase 5. The IR pipeline is
`verify → fold → mem2reg → CSE → cleanup → verify` per function, with leaf inlining run over the
deferred function pool before those passes (`src/gen/pipeline.cpp`). Liveness is CFG dataflow with
`spans_call`/`crosses_div`; linear scan uses next-use eviction and reports stats via
`--dump-regalloc`. The per-target assembly peephole runs after code generation. `-O1` is the
default (`src/main.cpp`), with `-O0` still explicit and covered on every gate run. Benchmarks live
in `bench/` with committed dynamic instruction-count baselines.

Final `scripts/check.sh --full`: 675 passed / 0 failed / 10 skipped at both -O0 and -O1; 275 C++
cases / 4267 checks; sweep 2058 invocations / 0 crashes; oracle 58/58 at both levels; ABI 8/8;
regalloc benchmark 248 functions / 2 spills / 301688 callee-save assignments; ASan/UBSan clean.
The 10 skips are the 8 ABI interop cases (run by their own script) and 2 include helpers. `bench/`
shows -O1 instruction-count reductions on four of five programs (recursive fib is unchanged).

## 2026-11-23 — X2a Object macros and conditionals
Extended `src/preprocessor.cpp` with object-like `#define NAME replacement` expansion (string/
character literals skipped, identifier-boundary aware, recursion depth capped) and
`#ifdef`/`#ifndef`/`#else`/`#endif` handling. Inactive branches emit nothing, so they cannot leak
tokens or trigger includes; the macro table is shared across `#include` boundaries, making the
`#ifndef GUARD / #define GUARD / ... / #endif` idiom work. Added 8 C++ cases (including a
temporary-file include-guard test) and `tests/unit/test_preprocessor_macros.cyan`, which runs at
-O0 and -O1 on both targets. `scripts/check.sh` green: 675 -> 677 passed / 0 failed / 10 skipped
at both levels; 275 -> 283 C++ cases / 4283 checks.

## 2026-11-23 — X2b Function macros and #if
Extended the preprocessor with function-like macros (`#define F(a,b) body`, argument substitution
with balanced-parenthesis parsing and nested macro expansion) and a small recursive-descent
`#if` evaluator supporting integer literals, `defined(NAME)`, `!`, `&&`, `||`, comparisons,
`+ - * / %` and parentheses per D-033. Undefined identifiers evaluate to 0 and macro names in
`#if` are expanded recursively. Added 6 C++ cases and `tests/unit/test_preprocessor_if.cyan`
(function macro plus `#if defined(...) && ...`) at -O0 and -O1 on both targets.
`scripts/check.sh --full` green: 677 -> 679 passed / 0 failed / 10 skipped at both levels;
283 -> 289 C++ cases / 4296 checks; sweep 2058 -> 2070 invocations / 0 crashes; ASan/UBSan clean.

## 2026-11-23 — X4a Bounds-check mode
Added `--bounds` plus a shared `Generator::emit_runtime_check` helper that branches around a
failure block printing a fixed message via `_sodium_print_raw_str` and exiting with status 70
(D-035); no new runtime library is needed. `Generator::index_addr` now emits lower/upper checks
for every array index expression before computing the address. Added three tests under
`tests/checks/`: out-of-range index and negative index both fail with the diagnostic on both
targets, and a valid indexed loop is unchanged. All use `COMPILE-FLAGS: --bounds` and pass at
-O0 and -O1. `scripts/check.sh --full` green: 679 -> 685 passed / 0 failed / 10 skipped at both
levels; 289 C++ cases / 4296 checks; sweep 2070 -> 2088 invocations / 0 crashes; ASan/UBSan clean.

## 2026-11-23 — X4b Null-deref checks
Added `--check-null` and emitted a non-null comparison at pointer dereference reads
(`NodeExprDeref`) and at every `typed_store`/`typed_load_val` write/read path, reusing the X4a
runtime-check emitter (D-035). Added three tests under `tests/checks/`: null read and null write
both fail with the `null dereference` diagnostic and a non-null pointer works unchanged, on both
targets at -O0 and -O1. `scripts/check.sh --full` green: 685 -> 691 passed / 0 failed / 10 skipped
at both levels; 289 C++ cases / 4296 checks; sweep 2088 -> 2106 invocations / 0 crashes;
ASan/UBSan clean.

## 2026-11-23 — X4c Stack-overflow probe
Added `--stack-probe`: `_start` stores `initial_rsp - 1 MiB` into a new `__sodium_stack_limit`
bss global, and each function prologue compares the current SP against it via a new
`Backend::stack_probe_check` implemented for x86-64 (`cmp rsp` + `jge`) and riscv64 (`bge`). The
failure path prints a fixed message and exits 70 using the existing runtime helpers (D-035), with
per-function labels. The first x86 draft passed the message in `rax`, but `_sodium_print_raw_str`
takes it in `rdi` — qemu traced a SIGSEGV at 0x8 until that was fixed. Added two
`tests/checks/` cases: deep recursion with a large frame fails cleanly and shallow recursion is
unchanged, on both targets at -O0 and -O1. `scripts/check.sh --full` green: 691 -> 695 passed /
0 failed / 10 skipped at both levels; 289 C++ cases / 4296 checks; sweep 2106 -> 2118 invocations
/ 0 crashes; ASan/UBSan clean.

## 2026-11-23 — X5a Inline asm syntax and IR plumbing
Added the `asm("arch", "instruction", ["clobbers"])` statement (D-036): new keyword, AST node,
parser with target/instruction/clobber diagnostics, a verbatim `IROpcode::ASM` with clobber
metadata, and emission through `Backend::emit_insn` on both targets. Clobber names are validated
against `TargetRegisterInfo` and reserved/callee-save registers are compile errors. ASM is a CSE
barrier, blocks leaf inlining, and conservatively constrains live values to callee-save registers
until X5b. Added tests/checks cases (both-target nop, unknown clobber, callee-save clobber,
missing clobber list), 4 C++ IR/emitter/liveness cases and an assembly effect check.
`scripts/check.sh --full` green: 695 -> 700 passed / 0 failed / 10 skipped at both levels;
289 -> 293 C++ cases / 4302 checks; sweep 2118 -> 2142 invocations / 0 crashes; ASan/UBSan clean.

## 2026-11-23 — X5b Clobber-aware allocation and hardening
Replaced the X5a conservative ASM barrier with exact per-register clobbers: `LiveRange` records
the physical registers clobbered by ASM inside its interval and linear scan excludes them from
assignment and eviction candidates. ASM strings may now contain embedded newlines, emitted as one
assembler line each, and the emitter surrounds user asm with marker labels that make the peephole
pass copy those lines verbatim. Added two Cyan cases (multi-line nops, self-move asm surviving
-O1), updated the liveness/allocator C++ case to assert rax is avoided, added a peephole-barrier
unit case and assembly effect checks. `scripts/check.sh --full` green: 700 -> 704 passed / 0
failed / 10 skipped at both levels; 293 -> 294 C++ cases / 4309 checks; sweep 2142 -> 2154
invocations / 0 crashes; ASan/UBSan clean.

## 2026-11-23 — X6a Debug line plumbing
Carried statement source locations into IR: `IRInstruction` now has `src_line`/`src_file`,
`IRBuilder::set_loc` stamps them on emitted instructions, and `gen_stmt` stamps the current
statement's location via a shared `statement_location()` helper. Added a backend-agnostic
`Backend::debug_line` method: x86_64 emits NASM `%line N+0 "file"` (absolute path and `+0` form),
riscv64 emits `.file N "file"` plus `.loc N L 0`, with no-ops in the null backend. `IREmitter`
optionally writes a marker when the source line changes. Added 4 C++ cases covering both targets,
line-change deduplication and the disabled path; verified end-to-end that a compiled program's
assembly contains `%line` rows from the original `.cyan` lines. `scripts/check.sh` green:
704 passed / 0 failed / 10 skipped; 294 -> 298 C++ cases / 4314 checks.

## 2026-11-23 — X6b -g flag and debug info integration
Added the opt-in `-g` flag, absolute source paths in line markers, and `-g -F dwarf` / `-g` for
NASM and the RISC-V assembler. Added `scripts/test_debug_info.sh` to the gate: it checks that
`-g` produces a `.debug_line` section and source-line rows on both targets, that gdb resolves a
Cyan line to an address, and that the default build has no debug info. `scripts/check.sh --full`
green: 704 passed / 0 failed / 10 skipped at both levels and under ASan/UBSan; 298 C++ cases /
4314 checks; sweep 2154 invocations / 0 crashes.

## 2026-11-23 — P6-END phase 6 state of the project
Re-verified against code and gate logs before tagging. Preprocessor macros/conditionals (D-033),
module conventions (D-034, docs/MODULES.md), three opt-in runtime checks (D-035), statement-level
inline asm with per-register clobbers (D-036), and opt-in DWARF line tables (D-037) are all in
place. X1 floats remain in docs/BLOCKED.md: they need a real FP register class and C-compatible
calling convention across all compiler layers, which is more than one phase task.

Final `scripts/check.sh --full`: 704 passed / 0 failed / 10 skipped at both -O0 and -O1 and under
ASan/UBSan; 298 C++ cases / 4314 checks; sweep 2154 invocations / 0 crashes; oracle 58/58 at both
levels; ABI 8/8; benchmark instruction counts within baseline; regalloc benchmark 248 functions /
2 spills / 301688 callee-save assignments; debug line tables verified with -g on both targets.
The 10 skips are the 8 ABI interop cases (run by their own script) and 2 include helpers.

## 2026-11-23 — X1 floats unblocked, split (D-038)
Owner chose the full C-compatible FP ABI. Created branch `phase-7/floats`, recorded D-038, split X1
into F1a..F5b in PLAN.md and resolved the BLOCKED entry. No code or test changes (tests unchanged).

## 2026-11-23 — F1a float front end (branch phase-7/floats)
Tokenizer: `float_lit` (`1.5`, `2e-3`, `1.5f`), `f32`/`f64` keywords, errors for a bare `1f` and an
empty exponent. Parser: `Type::Float`, `NodeExprFloatLit`, float types in params/returns/lets/casts/
struct fields; AST printer and type inference know floats. Generator rejects any float use with
"floating-point code generation is not implemented yet (D-038, F2)" so no float is silently treated as
an integer (tests/types/test_float_*_not_lowered.cyan are scaffolding that F3 replaces with run tests).
Tests: +5 Cyan types tests (x2 targets), +5 C++ unit cases.

## 2026-11-23 — X1 floats complete: F1b..F5b (branch phase-7/floats, D-039)
IR: FP opcodes, `IRFunction::is_fp`, F32/F64 widths, verifier class checks; rewriter lowers FP ops through
scratch FP registers (FP vregs are stack-resident); SSE and RV D/F emission; SysV and LP64D FP argument/
return ABI incl. overflow rules; gen/float.cpp typing + lowering; exact `%f` formatter in sodium-rt;
`print`/`%f`; float fields, arrays, pointers. Verified against gcc both directions (tests/abi/float_*,
12 ABI cases) and 4 oracle comparisons; formatter matches glibc on 24 values. Fixed a pre-existing
type-stack imbalance and promote narrow int +/- to i32 (D-039). Small float structs by value are a
compile error (X1b, open). `scripts/check.sh --full` green: integration 714 -> 778 passed (14 skipped, 4 new
ABI harness files), unit 303 -> 320 cases, sweep 2388 invocations 0 crashes, oracle 62/62 at -O0 and -O1,
fuzz 1000 inputs 0 crashes, ASan/UBSan clean.

## 2026-11-23 — X1b small float structs by value (branch phase-7/float-structs, D-040)
Classification into register pieces per target (System V eightbytes, LP64D flattening), class-aware
`RET2`/two-result calls (`dst2_width`), piece-wise argument placement with the memory/int-word
fallbacks, params and results. Verified against gcc both directions on both targets with 10 struct
shapes plus register-exhaustion cases (tests/abi/float_struct_*, 16 ABI cases total); run test
tests/floats/test_float_struct_value.cyan; 5 new unit cases. Removed the two by-value compile-error
tests (behaviour intentionally lifted).

## 2026-11-24 — D1 README rewrite from verified behavior
Rewrote README.md from scratch: every language construct is a complete, compilable program with a
link to the test that verifies it, and all CLI flags, build/test instructions, architecture
overview, project layout and known limitations were re-checked against the source and gate logs.
Removed stale claims (302 tests, nonexistent `examples/`, absent flags). Added
`scripts/doc_check.sh`, which extracts all 13 fenced `cyan` blocks, compiles each for x86_64 and
riscv64 and runs it on x86_64; it is now a `check.sh` step. Verified all 21 test-file links resolve
(fixed one pointing at a nonexistent recursion test). Gate counts recorded in the README: 776
integration passed / 0 failed / 18 skipped, 325 C++ cases / 4465 checks, oracle 62/62, ABI 16/16,
sweep 2394 invocations / 0 crashes, fuzz 1000 / 0 crashes.

## 2026-11-24 — D2a IR implementation reference
Added `docs/IR.md` describing the IR from the source: data model, opcode reference, verifier,
-O1 pass pipeline (fold/mem2reg/CSE/cleanup, plus pre-pipeline leaf inlining), liveness, linear
scan allocation, rewriting, emission and a file map. Added `scripts/check_arch_docs.sh`, wired
into `check.sh`, which asserts that every named opcode, IR file and pass symbol actually exists
in `src/ir/` and is mentioned by the doc, so the docs cannot drift silently from the code.
`scripts/check.sh` green: 776 passed / 0 failed / 18 skipped at both levels; doc-check 13 samples;
arch-doc verification OK. Test counts unchanged (documentation only).

## 2026-11-24 — D2b ABI and backend implementation references
Added `docs/ABI.md` (platform ABI, integer arguments/returns, struct-by-value and float-struct
classification, FP conventions, runtime/stack) and `docs/BACKENDS.md` (Backend contract, x86-64
SSE/NASM backend, riscv64 D/F/GAS backend, post-emission peephole), both derived from the source.
Extended `scripts/check_arch_docs.sh` to verify their register names, ABI symbols, backend methods,
file references and decision IDs against `src/ir/target_regs.hpp`, the generator, both backends and
DECISIONS.md. `scripts/check.sh` green: 776 passed / 0 failed / 18 skipped at both levels; all doc
checks pass. Documentation only; test counts unchanged.

## 2026-11-24 — D3a LSP rename, references and diagnostics lifecycle
Added `textDocument/rename` (WorkspaceEdit with one edit per matching identifier token) and
`textDocument/references` (honouring `context.includeDeclaration`) to `cyan-lsp`, advertised both
in `initialize`, and made `didClose` publish an empty diagnostics list so stale errors do not linger
(D-041). Added `tests/lsp/test_lsp_features.py`, wired into `check.sh`, covering 3 references vs 2
uses, 3 rename edits and diagnostics clearing. `scripts/check.sh` green: 776 passed / 0 failed /
18 skipped at both levels; both LSP protocol suites OK. Test counts unchanged (tooling only).

## 2026-11-24 — D3b LSP signature help and semantic tokens
Added `textDocument/signatureHelp` (finds the enclosing call from the token stream, derives the
callee's parameter list from the symbol-table detail and counts the active argument) and
`textDocument/semanticTokens/full` (delta-encoded keyword/type/number/string/function/variable/
parameter classification using the tokenizer), both advertised in `initialize` (D-041). Added
`tests/lsp/test_lsp_signature.py` to the gate covering the label, parameter list, active parameter
and token types. `scripts/check.sh` green: 776 passed / 0 failed / 18 skipped at both levels; all
three LSP protocol suites OK. Test counts unchanged (tooling only).

## 2026-11-24 — D3c VS Code extension refresh
Updated `vscode-extension/syntaxes/cyan.tmLanguage.json` for the current language: preprocessor
directives, float/hex literals, `f32`/`f64`/`bool`/`char` types, `struct`/`enum`/`asm`/`as`/`type`
keywords, `null` and `->`. Rewrote `extension.js` to drop the hardcoded development path in favour
of a `cyan.lspPath` setting (with a workspace-build fallback) and added that setting to
`package.json`. Added `tests/vscode/test_extension.py`, wired into `check.sh`, which statically
verifies the grammar and client config. `scripts/check.sh` green: 776 passed / 0 failed / 18 skipped
at both levels; all LSP suites and the extension check OK. Tooling only; test counts unchanged.

## 2026-11-24 — D4a Version and help text
Added a single-source `src/version.hpp` (`SODIUM_VERSION 0.1.0`), `--version`, and `--help`/`-h`
with a man-style usage block covering every CLI flag and the `fmt` subcommand. Added
`tests/cli/test_cli.py` to the gate, which checks the version against the header and that all
sections/options appear in help. `scripts/check.sh` green: 776 passed / 0 failed / 18 skipped at
both levels; CLI test OK. Test counts unchanged (tooling only).

## 2026-11-24 — D4b sodium fmt formatter
Added `src/formatter.hpp` and the `sodium fmt` subcommand with stdout, `-o FILE` and `--check`
modes (D-042). The formatter normalizes indentation to four spaces per brace depth, trims trailing
whitespace and collapses blank runs, while preserving comment lines (including `// EXPECT-*` test
headers) and string contents verbatim; braces inside literals/comments are ignored. Added
`scripts/test_formatter.sh` to the gate: all 399 Cyan files under `tests/` are formatted twice and
compared, `--check` accepts canonical output, EXPECT headers are byte-identical and a sample
program keeps the same exit code and stdout after formatting. `scripts/check.sh` green: 776 passed
/ 0 failed / 18 skipped at both levels. Test counts unchanged (tooling only).

## 2026-11-24 — D5 Install script verified in a clean prefix
Added `scripts/test_install.sh` to the gate: it installs into a fresh temporary prefix, checks
that both binaries, `sodium-rt/` and `cyanlib/` are present (with no nested runtime directory),
and verifies the installed compiler compiles and runs a program. Improved `scripts/install.sh`
with a `--prefix DIR` flag and honest verification that reports the installed paths instead of
`which` output. `scripts/check.sh` green: 776 passed / 0 failed / 18 skipped at both levels;
clean-prefix install OK. Test counts unchanged (tooling only).

## 2026-11-24 — P7-END phase 7 state of the project
Re-verified against code and gate logs before tagging. Phase 7 completed all tooling/docs work:
README rewritten from verified behavior with 13 fenced samples compiled on both targets by
`scripts/doc_check.sh`; `docs/IR.md`, `docs/ABI.md`, `docs/BACKENDS.md` verified against the source
by `scripts/check_arch_docs.sh`; `cyan-lsp` extended with rename, references, signature help and
semantic tokens (D-041) plus diagnostics lifecycle; the VS Code grammar/client refreshed;
`sodium fmt` (D-042), `--version` and man-style `--help` added; and the install script verified in
a clean temp prefix. All of these run as steps of `scripts/check.sh`.

Final `scripts/check.sh --full`: 776 passed / 0 failed / 18 skipped at both -O0 and -O1 and under
ASan/UBSan; 325 C++ cases / 4465 checks; sweep 2394 invocations / 0 crashes; oracle 62/62 at both
levels; ABI 16/16; regalloc benchmark 248 functions / 2 spills / 301692 callee-save assignments;
debug line tables verified with -g; doc-check 13 samples; formatter idempotent over 399 files;
three LSP protocol suites, VS Code check, CLI check and clean-prefix install all OK; fuzz 1000
inputs / 0 crashes. The 18 skips are ABI/helper files exercised by their own scripts. Known limits
unchanged: floats have no FP register allocation or constant folding, debug info is line tables
only, and there is no self-hosting yet.

## 2026-11-24 — D5 follow-up: install.sh warns about a shadowing sodium
Found when a stale /usr/local/bin/sodium (root-owned, not replaced because install falls back to
~/.local when /usr/local/bin is unwritable) produced a broken program. install.sh now checks
`command -v sodium` after installing and warns if it is missing from PATH or resolves to a
different binary, with the fix command. scripts/test_install.sh covers not-on-PATH, shadowed and
clean cases. check.sh passes; test counts unchanged.

## 2026-11-23 — T-1 Phase T coverage inventory + xfail infrastructure + BF-001
Baseline verified clean on `main` at `a1ec4d9` (check.sh green: 776 passed / 0 failed / 18 skipped
per level); created branch `phase-T/edge-tests`. Inventoried 399 `.cyan` tests by feature and
diagnostic in `docs/PHASE-T-COVERAGE.md` and listed the concrete gaps that drive T-2..T-7.
Reproduced the known sizeof bug by hand on both targets (type = 8, variable = 128, C reference = 16)
and recorded it as BF-001 in `docs/BUGS-FOUND.md` with the minimal reproducer at
`tests/xfail/bf001_sizeof_struct.cyan`. Added `scripts/xfail.sh` (not wired into check.sh) which
reports 2 still-failing cases; `scripts/run_tests.sh` now excludes `tests/xfail/` so the gate's
test count is unchanged. Tests before → after: 776 / 794 run-equivalents → 776 / 794 (xfail tests
are outside the gate). No compiler code changed.

## 2026-11-23 — T-2 lexer/preprocessor edge tests; BF-002..BF-005 found
Added 12 passing test sources (`tests/edge/`, 24 cases on both targets) for i64/hex literal limits,
all string escapes (byte-exact via memcmp), five malformed-literal diagnostics, recursive macros,
empty input, an eight-deep include chain and a 4000-character literal. All pass on x86_64 and
riscv64. Found four new bugs by hand, reduced each to a minimal reproducer, and filed them in
`docs/BUGS-FOUND.md`: BF-002 hex overflow wraps to 0 (critical, silent wrong code), BF-003
unterminated block comment silently accepted (medium), BF-004 include cycle stack-overflows the
compiler (critical), BF-005 empty include reported as unreadable (low). Their xfail tests are in
`tests/xfail/`; `scripts/xfail.sh` reports 10 still-failing cases (BF-001..BF-005 × 2 targets).
`sweep.sh` excludes `tests/xfail/` so the known crash cannot break the gate. Tests before → after:
776 / 794 run-equivalents → 800 / 818. No compiler code changed.

## 2026-11-23 — T-3 parser/diagnostics edge tests; BF-006 and BF-007 found
Added 17 passing test sources (`tests/edge/`, 34 cases on both targets): a C-cross-checked
precedence/associativity matrix, 1600-deep parentheses plus 120 nested ifs, and 15 EOF/truncation
diagnostics (if, while, for, var, return, print, struct, type, switch, call, cast, index, field,
asm, string). Added `scripts/test_no_cascade.sh` to check.sh (3 single-mistake cases × 2 targets,
exactly one `error:` each). Found two more bugs: BF-006 untyped calls accept any argument count
(high, silent wrong code) and BF-007 ~3000 nested parentheses crash the compiler (medium). Both
have xfail tests; `scripts/xfail.sh` now reports 14 still-failing cases. The first gate run
exposed that `fuzz_smoke.sh` mutated the BF-007 xfail file and rediscovered its crash, so the
fuzz corpus now excludes `tests/xfail/` just like the runner and sweep. Tests before → after:
800 / 818 run-equivalents → 834 / 852. No compiler code changed.

## 2026-11-23 — T-4 type/semantics extremes; BF-008..BF-010 found
Added 5 passing test sources (`tests/edge/`, 10 cases on both targets): a gcc-cross-checked cast
subset, shift counts 0..65, signed division/modulo matrix, IEEE special values (NaN/inf/-0/denormals),
and enums/field offsets/fn-pointer tables/pointer differences. A 152-pair cast matrix against gcc
and targeted probes found three more bugs: BF-008 `as i8/i16/i32` zero-extends (high), BF-009
`MIN / -1` traps on x86_64 but silently returns MIN on riscv64 (high), BF-010 u64 arithmetic and
comparisons use signed i64 semantics (high). All have xfail tests; `scripts/xfail.sh` now reports
19 still-failing cases. Target-specific float→int out-of-range results are documented as a known
limit (D-038/D-039), not filed as a bug. Tests before → after: 834 / 852 → 844 / 862 run-equivalents.
No compiler code changed.

## 2026-11-23 — T-5 codegen/regalloc stress tests
Added 6 passing test sources (`tests/edge/`, 12 cases on both targets): 0..25 integer arguments
with every sum checked, mixed int/float argument sequences across the 8-register boundary
(1/7/8/9/12 FP args), 30 live locals held across a call, a 300-term expression with a call,
1000-deep recursion and a 512 KiB frame under `--stack-probe`. All pass at -O0 and -O1. Found BF-011: riscv64 emits out-of-range sd/ld spill offsets at -O1 for deep expressions; the
300-term case moved to xfail and the gate keeps a passing 120-term version. The entry-SP-only nature of the stack probe (a single 20 MiB frame segfaults) is
documented in `docs/PHASE-T-COVERAGE.md` as a known limit, with the compile-time cost noted.
Tests before → after: 844 / 862 → 856 / 874 run-equivalents. No compiler code changed.
