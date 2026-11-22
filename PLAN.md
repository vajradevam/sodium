# PLAN.md — the only task list

Rules: see AGENTS.md. Work top to bottom. A task is done only when its **Done when** holds, `scripts/check.sh`
passes, it is committed, and its box is ticked. Add sub-tasks freely (commit the split). IDs are stable; never reuse.
"Verify" tasks come from the old (deleted) audit: reproduce first; if the claim is false, add the regression test
anyway, tick the box, and note "not reproducible" in PROGRESS.md.

## Phase 0 — Harness (do this before any feature work)
- [x] **T0a Migrate test headers (split from T0).** Add `EXPECT-*` headers to every `tests/**/*.cyan`: `EXPECT-EXIT`
  from the old hardcoded map, `EXPECT-ERROR` substrings from captured diagnostics (no `file:line:col` prefix),
  `STDIN`/`ARGS` where applicable, `EXPECT-STDOUT` from reviewed current output, `SKIP-TEST` for `helper.cyan`, and
  `EXPECT-EXIT: 26` for the previously-unrun `test_comprehensive.cyan`. Append the `SKIP-TEST` directive to D-005.
  The old runner must still pass (comments only).
- [x] **T0b Rewrite test runner (split from T0).** Auto-discover `tests/**/*.cyan`, parse headers, check exit code +
  stdout + error substring, per-target, `mktemp -d`, summary by category, `--filter`, `--target`, `-j`. No hardcoded
  map. Honour `COMPILER` env var and keep the `RISC-V backend: SKIPPED` marker for `check.sh`. Done when `check.sh`
  is green with ≥318 run-equivalents.
- [x] **T0c Harness self-check (split from T0).** Add a `--self-test` mode proving a deliberately wrong expectation
  fails. Verify both targets/counts and record results in PROGRESS.md.
- [x] **V12 riscv64 `print(0)` emits NUL byte (discovered during T0).** `print(0)` outputs byte `0x00` instead of `'0'`
  on riscv64 (x86_64 correct); `test_print.cyan` and `test_print_all.cyan` cannot carry `EXPECT-STDOUT` until fixed.
  Reproduce with a focused test, fix the riscv64 int→string path, then enable stdout checks on both tests.
- [x] **T1 Strict build.** Add `-Wall -Wextra -Wshadow -Wconversion`-class warnings (choose the strictest set that you
  can make clean) and make warnings fatal in `check.sh`. Fix all. Done when: zero warnings from a clean rebuild.
- [x] **T2 CI-equivalent script.** `scripts/check.sh` additionally: `--emit-ir`/`-S` sweep over every test on both targets
  (no crash), and rejects trailing whitespace/tab-vs-space drift in changed files. Add `.clang-format` or document style
  from the existing code (don't mass-reformat).
- [x] **T3 Fuzz/smoke harness.** `scripts/fuzz_smoke.sh`: feed mutated/truncated test files (deterministic seed) to the
  compiler; it must never crash/hang (timeout 5 s) — it may only exit 0 or 1 with a diagnostic. Fix crashes found (one commit each, with a regression test).
- [x] **T4a Unit-test scaffold (split from T4).** `tests/cpp/` with a tiny no-network assert framework + runner,
  CMake `unit_tests` target linking the compiler sources, first cases (arena), and a `check.sh` step. Gate green before
  adding the rest.
- [x] **T4b Tokenizer + preprocessor cases (split from T4).** Token kinds/locations/escapes/numbers, reset-after-tokenize,
  `#line` markers, `#include`, `#pragma once`, `#error`. ≥16 cases.
- [x] **T4c Parser AST shape + const-eval cases (split from T4).** AST node shapes for expressions/statements, precedence,
  and `Generator::eval_const_expr` over literals/operators/overflow/div-zero. ≥16 cases.
- [x] **T4d IR liveness + linear-scan cases (split from T4).** Live intervals, overlap, call-spanning, allocation of
  non-overlapping ranges, spills under pressure, sorted output. ≥16 cases.
- [x] **T4e Allocator cases + finalise T4 (split from T4).** Compile `sodium-rt/allocator.c` natively; alignment, reuse,
  sizes, zero-size, free-list recycling, stress. Verify ≥60 cases in <10 s and tick T4.
- [x] **T5a Oracle harness + arithmetic corpus (split from T5).** `scripts/oracle.sh` compiles `tests/oracle/<name>.cyan` with sodium and `<name>.c` with gcc, then diffs stdout and exit code. Add ≥10 arithmetic/edge-case pairs. Not yet in the gate.
- [x] **T5b Oracle corpus: shifts, division, truncation (split from T5).** ≥8 more pairs covering shifts, signed division/modulo, i8/u8/i16/u32 truncation and comparisons. Fix any mismatch found, each with a regression test.
- [x] **T5c Oracle corpus: pointers/recursion/control flow + gate (split from T5).** ≥7 more pairs (pointer math, pointer diff, recursion, loops, switch). Fix mismatches (notably V12: riscv64 print(0) emits NUL), run the oracle on both targets, wire into `check.sh`, tick T5, reach ≥25 pairs total.
- [x] **T6 Cleanup of build hygiene.** `.gitignore` covers all outputs; `git status` is clean after `check.sh`; remove `system()`
  shell-outs in `main.cpp` in favor of `fork/execvp` with argv arrays (fixes shell injection via `-o` paths); test with a path containing spaces and `;`.
- [x] **P0-END** `check.sh --full`, tag `phase-0-done`.

## Phase 1 — Verify and fix known suspicions (each: repro test first)
- [x] **V1 >6 args (x86_64) / >8 args (riscv64)** incl. nested calls as arguments and recursion with stack args; RSP alignment at call.
- [x] **V2 Register pressure / spills:** programs with 20+ live values across calls, in loops, on both targets; check spill slots don't collide with locals or arrays.
- [x] **V2b Caller-save values clobbered by nested/recursive calls (follow-up from V2).** With 20+ args and nested
  calls, a call result held in a caller-save arg register (e.g. riscv a7) is overwritten while setting up the next
  call; recursion with 20 args also fails on x86_64. Needs callee-save/spill selection that reserves argument
  registers, or save/restore around calls. Repro: nested `sum20(sum20(...), ...)` and `recurse20` cases.
- [x] **V3 Signed/unsigned semantics:** `>>` arithmetic vs logical by type, `/ %` with negatives and unsigned, comparisons of mixed signedness, `i8..u64` truncation/extension on assign, args, returns, compound ops, `++/--` at type boundaries.
- [x] **V4 Type equality / pointer types:** `Type::operator==` on pointers from different arenas; `T**`; pointer-to-struct; pointer comparison; pointer diff scaling.
- [x] **V5 Array/struct stores:** `arr[i]=v` and `s.f=v` use 8-byte stores unconditionally — find any program where that corrupts neighbors (typed element/field once V-phase types exist); document and add tests.
- [x] **V6 Allocator:** allocations >2048 B leak on free; double-free/invalid-free behavior; alignment (16 B — fixed in V6: 16-byte header and 16-byte-aligned size classes); zero-size; reuse after free; stress with random sizes (native C test + Cyan test). Implement mmap-backed large blocks and heap growth (`brk`/`mmap`) with real coalescing. Done when stress test cycles 1 GiB of alloc/free without growth.
- [x] **V7 I/O:** `read()` EOF indistinguishable from 0 → add a way to detect EOF (e.g. `read_eof()` or return convention per D-decision; record in DECISIONS.md); `read(buf)` overflow safety (bound by buffer size); partial writes in `print`.
- [x] **V8 `#line` caret context** wrong source line in diagnostics for included files; test with include + error inside the include.
- [x] **V9 Scoping & shadowing, static/global init order, switch fall-through with declarations, `break` inside `switch` inside loop** — add tests for every combination found missing.
- [x] **V10 Large frames / large immediates / large constants** on both targets (arrays > 2 MiB on stack with probes, 64-bit immediates, negative immediates at 12-bit boundary on riscv64).
- [x] **V11 riscv64 PIC / `--no-relax` dependence:** fixed the JAL range limit by emitting the `call` pseudo (auipc+jalr); >2 MiB stack frames now link and run. Kept `--no-relax` and documented why: relaxation rewrites the auipc/addi pairs used for label addresses and breaks pointer arithmetic (oracle mismatches in ptr_diff/ptr_malloc_store).
- [x] **P1-END** `check.sh --full`, tag `phase-1-done`, update PROGRESS.md with true state.

## Phase 2 — Diagnostics (makes everything after faster to debug)
- [x] **E0a Diagnostic engine core (split from E0).** `DiagnosticEngine` + `Diagnostic` (severity, loc, message, notes, fix-it), D-004 rendering, filtering; no global state in the class. Add C++ unit tests. Route `format_err` through it.
- [x] **E0b Thread the engine into tokenizer/parser (split from E0).** Pass `DiagnosticEngine&` to Tokenizer and Parser, migrate their error sites off `lsp_exit`/globals; keep CLI/LSP behavior identical.
- [x] **E0c Migrate codegen to the engine (split from E0).** Pass the engine to Generator, migrate gen/* error sites, remove the remaining global diagnostic state; tick E0.
- [x] **E1 Parser error recovery** (sync on `;` `}` keywords); report up to N errors, no cascades. Tests with 3 independent errors in one file.
- [x] **E2 Semantic error recovery** in `src/gen/*`: undeclared identifiers, wrong arg counts, redeclarations continue to the end of the function/file.
- [x] **E3a Warning infrastructure (split from E3).** `DiagnosticEngine` warning config (`-w` disables, `-Werror` promotes to errors); CLI flags; render warnings (not just errors) in CLI path, keep LSP collecting them; test-runner headers `EXPECT-WARNING`, `COMPILE-FLAGS`, `EXPECT-NO-WARNING`; C++ unit tests for config. Gate green before real warnings.
- [x] **E3b Unused variable/parameter warnings (split from E3).** Track reads in `Var`; warn at scope exit and for unused params; positive + `-Werror` + `-w` tests on both targets.
- [x] **E3c Unreachable code + missing return warnings (split from E3).** AST-based flow analysis; warn after `return`/`exit`/`break`/`continue` and for functions whose body can fall off the end; tests on both targets.
- [x] **E3d Narrowing assign + unused result + `=` in condition warnings (split from E3).** Warn on out-of-range/widening stores, discarded pure expression results, and `=` used where `==` is meant in conditions; tests on both targets; tick E3.
- [x] **E4 Notes** ("previous declaration here", "did you mean …"), `-fcolor-diagnostics`/NO_COLOR.
- [x] **E5 LSP uses the engine** (multiple diagnostics, severities); add LSP protocol tests (scripted stdio session) to `tests/lsp/`.
- [x] **P2-END** `check.sh --full`, tag.

## Phase 3 — Real type system (per D-002/D-003)
- [x] **Y1a Typed signature syntax (split from Y1).** Add `->` token, parse `function f(a: i32, p: u8*) -> i64` (pointer types `T*`/`T**` via the parser arena); extend `NodeFuncDef` with param types and return type, keep untyped signatures accepted; AST/tokenizer unit tests. Generator behavior unchanged.
- [x] **Y1b Typed params + call checking (split from Y1).** Generator uses declared param types for frame slots/loads and checks call arity and argument types; implicit conversion only for safe widening or literal fit, otherwise compile error. Positive, negative and boundary tests on both targets.
- [x] **Y1c Return types and conversions (split from Y1).** Generator checks return expressions against declared return type, applies safe widening/truncation rules; positive + compile-error tests on both targets; tick Y1.
- [x] **Y2a Typed field syntax + C layout (split from Y2).** Parse `struct S { var x: i8; var y: i32; };` with a parallel field-type list (untyped field = i64); compute C-rule layout (natural alignment, padding, trailing pad) in Generator's StructInfo using byte offsets/sizes; AST/layout unit tests.
- [x] **Y2b Typed field loads/stores (split from Y2).** Field read/write and address-of use each field's declared width/signedness instead of unconditional 8-byte ops; regression tests for adjacent fields and no neighbor corruption on both targets.
- [x] **Y2c `sizeof` builtin (split from Y2).** Compile-time `sizeof(T)` / `sizeof(expr)` for int types, pointers and structs, usable in const expressions and codegen; tests on both targets; tick Y2.
- [x] **Y3 Typed local arrays** (`var a: i32[10];`) with element-width loads/stores and `sizeof`; array-to-pointer decay in calls.
- [x] **Y4 `bool`, `char`, `true/false/null`.**
- [x] **Y5 Casts** `as`; explicit narrowing required where Y1 rules reject implicit.
- [x] **Y6a `type` aliases (split from Y6).** Parse `type Name = T;` (int/pointer types) at top level, register in the parser's alias table so aliases work in annotations (`var x: Name`, params, returns), and make `sizeof(Name)` work; duplicate/unknown alias diagnostics; tests on both targets.
- [x] **Y6b `enum` (split from Y6).** Parse `enum Color { Red, Green = 5, Blue };` with auto-incrementing i64 values, register names as compile-time constants (plain identifiers, duplicate = error); tests on both targets; tick Y6.
- [x] **Y7a Struct identity in types (split from Y7).** Extend Type with a struct kind/name; struct-typed vars and &s carry it so Ptr(struct) survives into parameters and returns; unit tests.
- [x] **Y7b `p->field` access (split from Y7).** Parse and codegen C-like pointer-to-struct reads/writes per D-011, plus `&s.field`; tests on both targets.
- [x] **Y7c Nested struct field access (split from Y7).** Support `a.b.c` read chains by allowing a field-access base expression and resolving each field's struct type; tests on both targets.
- [x] **Y7d1 Arrays of structs (split from Y7).** `var a: Point[2]` with real struct element size, `a[0].x` reads/writes via a field-access base that is an index expression; tests on both targets.
- [x] **Y7d2 Struct literals (split from Y7).** `{ .x = 1 }` initializer expressions for typed struct locals; tests on both targets; tick Y7 once done.
- [x] **Y8a Struct assignment (split from Y8).** `a = b` copies all bytes between typed struct locals with correct field widths; tests on both targets.
- [x] **Y2d C memory layout (inserted before Y8b; prerequisite of D-012).** Frame objects (arrays, structs) are 8-byte aligned/granular with ascending addresses, narrow (16/32-bit) loads and stores are plain little-endian, `base + offset` addresses fields/elements, pointer arithmetic on decayed arrays walks forward; fixes neighbour corruption for sub-8-byte arrays/structs/scalars. Regression tests on both targets.
- [x] **Y8b Struct params/returns (split from Y8).** ABI chosen in D-012 (platform ABI per target). Sub-tasks below; tick Y8b when all are done.
  - [x] **Y8b1** Struct values as call arguments (<= 16 bytes, in registers) and `p: Point` parameters; struct-valued expressions evaluate to an address with `Type::Struct`; tests on both targets.
  - [x] **Y8b2** Struct returns <= 16 bytes: `RET2`, `dst2` on CALL, `ret_reg2` in target info; tests incl. sizes 1..16 and spilled values on both targets.
  - [x] **Y8b3** Returns > 16 bytes through a hidden first-argument pointer; riscv64 params > 16 bytes by reference.
  - [x] **Y8b4a** Argument-placement classifier shared by caller and callee (CALL `reg_args`): x86_64 structs > 16 bytes and register-straddling structs go entirely on the stack while later scalars still use the free registers; riscv64 keeps its split rule; remove the "cannot be passed by value yet"/straddle diagnostics; tests on both targets.
  - [x] **Y8b4b** C-interop harness (`scripts/test_abi_interop.sh`, in `check.sh`): gcc-built C callers/callees linked against Cyan output (weak stubs for the reverse direction) for struct params and returns of sizes 1..40 on both targets.
- [x] **Y9a Multi-dimensional arrays (split from Y9).** Parse `var a: i32[2][3]` and `a[i][j]`; row-major layout with compile-time dimension sizes; reads/writes on both targets.
- [x] **Y9b Const correctness + untyped fallback (split from Y9).** Keep call-site inference strictly as a fallback that emits a warning for untyped params; verify `const` declarations cannot be written; migrate cyanlib/tests to typed signatures incrementally; tick Y9.
- [x] **Y10 Function pointers** and indirect calls on both targets (D-014). Sub-tasks below; tick Y10 when all are done.
  - [x] **Y10a** `CALL_INDIRECT` through IR, liveness, rewriter, emitter and both backends (`call_reg`, `call_target_reg`); unit tests on emitted code for every target-home combination (physical, argument register, spilled) on both targets.
  - [x] **Y10b** Language: `fn(T,...) -> R` types (variables, params, struct fields, returns), `&func`, calls through them with arity/type checks; run tests on both targets incl. 7+ arguments, struct args, callbacks, tables of functions; compile-error tests; C interop in both directions (`tests/abi/fnptr_*`).
- [x] **P3-END** full gate, tag, rewrite parts of docs that changed.

## Phase 4 — Standard library & runtime (Cyan-written wherever possible)
- [x] **L1 `cyanlib/string.cyan` rewritten with typed signatures:** strlen, strcmp, strncmp, strcpy, strncpy, strcat, strchr, strstr, memcpy, memmove (overlap-safe), memset, memcmp; tests for each incl. empty/overlap/boundary.
- [x] **L2a `print` specifiers:** add `%x` (lowercase hex), `%u` (unsigned decimal), `%c` (single byte) and `%ld` (i64 decimal alias of `%d`) to the format-string parser and codegen, with freestanding runtime helpers on both targets; positive tests for each plus compile-error tests for malformed specifiers.
- [x] **B3 x86_64 variable shift clobbers RCX (found during L2b):** register-counted `shl`/`shr`/`ashr` write CL, destroying a live value the allocator put in RCX; preserve RCX in the reserved scratch r11 (or shift in r11 when RCX is the destination). Three emitted-assembly unit cases.
- [ ] **L2b `cyanlib/fmt.cyan` int→string:** binary/octal/decimal/hex (upper and lower) conversion for signed and unsigned i64, with tests for zero, negative, min/max and base boundaries on both targets.
- [ ] **L2c `cyanlib/fmt.cyan` string→int with error reporting:** parse decimal (and other bases) with explicit success/overflow/invalid diagnostics; tests for valid, empty, invalid characters, sign and overflow on both targets.
- [ ] **L3 `cyanlib/mem.cyan`** wrappers: calloc, realloc, aligned alloc; allocator leak check mode (`SODIUM_ALLOC_DEBUG`).
- [ ] **L4 File I/O via raw syscalls** in sodium-rt (`open/close/read/write/lseek/unlink`) on both arches + `cyanlib/io.cyan` (read file to buffer, write buffer, line reader, `stderr` printing, exit codes).
- [ ] **L5 Dynamic containers in Cyan:** `Vec` (growable array), `Str` builder, hash map (string→i64). Property-style tests with many random operations vs expected checksums.
- [ ] **L6 `argv`/`envp`/`exit` handling polish;** `print` to fd; `assert(cond, "msg")` builtin.
- [ ] **P4-END** full gate, tag.

## Phase 5 — IR & optimization (D-006; all behind `-O1` first)
- [ ] **O1 IR verifier** (`--verify-ir`, run automatically in debug builds): defs dominate uses, operand types, block terminators, no dangling labels. Run it over the whole suite.
- [ ] **O2 Constant folding & algebraic simplification** (x+0, x*1, x*2^n → shift, strength reduction), with overflow semantics identical to runtime.
- [ ] **O3 Dead code / unreachable block elimination**, jump threading, empty-block removal.
- [ ] **O4 Local value numbering / CSE; redundant load-store elimination** within basic blocks.
- [ ] **O0 Loop-aware liveness (precondition for O5 and any optimization that keeps values in vregs).** `compute_live_ranges`
  (`src/ir/liveness.hpp`) builds one interval per vreg from first def/use to last use in linear instruction order and
  ignores back edges. A vreg defined before a loop and used only early in the loop body is live around the whole loop,
  but its interval ends at the last textual use, so its register can be reused (or it can look like it does not span a
  later CALL in the body). Fix with real dataflow liveness (live-in/live-out per block, iterate to fixpoint) and extend
  each interval over every block where the vreg is live. Done when: a hand-built IR/`.cyan` repro that keeps a value in
  a vreg across a loop containing calls and register pressure fails before and passes after on both targets; C++
  unit tests cover straight-line, if/else, nested loops, and loop-carried values.
- [ ] **O5 mem2reg-style promotion** of non-address-taken scalar locals to vregs (decide phi vs. block-args and record in DECISIONS.md).
- [ ] **O6 Register allocator quality:** live-range splitting or better spill heuristics; callee-saved use; measure spills on a benchmark set.
- [ ] **O7 Peephole per backend;** inlining of small leaf functions.
- [ ] **O8 Benchmarks** (`bench/`: fib, sieve, matmul, sort, hash) with a script printing instruction counts (qemu `-d` or `perf stat`) — track no regressions; `-O1` becomes default only if the whole suite plus oracle pass under it.
- [ ] **P5-END** full gate under `-O0` and `-O1`, tag.

## Phase 6 — Language extras
- [ ] **X1 Floats** (`f32`,`f64`) incl. ABI on both targets, literals, conversions, print. (Decide scope in DECISIONS.md first; skip → BLOCKED if it needs >1 phase.)
- [ ] **X2 `#define` object/function macros, `#ifdef/#else/#endif`, `#if` constant expr**, include guards test.
- [ ] **X3 Module hygiene:** `import`-style or keep `#include`; namespacing for `cyanlib` (prefix convention documented).
- [ ] **X4 Bounds-check mode** `--bounds`, null-deref checks `--check-null`, stack-overflow probe diagnostics; each with tests proving they trigger and don't change valid programs.
- [ ] **X5 Inline asm** minimal form, per-arch, with clobber list.
- [ ] **X6 Debug info:** `.loc`/line tables so `gdb`/`qemu -g` can step source lines.
- [ ] **P6-END** gate, tag.

## Phase 7 — Tooling & docs
- [ ] **D1 README rewrite from verified behavior only:** language reference (every construct with a test link), CLI flags, build/test instructions, architecture overview, known limitations. Every code sample in the README is extracted and compiled by a test (`scripts/doc_check.sh`).
- [ ] **D2 `docs/IR.md`, `docs/ABI.md`, `docs/BACKENDS.md`** describing the actual implementation (generated/verified against code).
- [ ] **D3 LSP hardening:** incremental diagnostics, rename, find-references, signature help, semantic tokens; protocol tests. VS Code extension refreshed for new syntax (typed params, enums, etc.).
- [ ] **D4 `sodium fmt`** (canonical formatter; idempotence test over all tests) and `--version`, `--help`, man-style usage text.
- [ ] **D5 Install/packaging script tested in a clean temp prefix.**
- [ ] **P7-END** gate, tag.

## Phase 8 — More targets (optional; do only if Phases 0–7 are done)
- [ ] **B1 Backend interface audit:** document & test the `Backend` contract with a `null_backend` conformance suite.
- [ ] **B2 aarch64 backend** (qemu-aarch64) passing the full suite, or **WASM** (decide in DECISIONS.md).
- [ ] **P8-END** gate, tag.

## Phase 9 — Self-hosting (D-007)
Prerequisite check: Phases 3, 4 complete; compiler source subset needed is listed in `docs/SELFHOST.md` (write it first: which constructs the Cyan-written compiler needs, what's missing).
- [ ] **S0 Selfhost plan doc + gap list;** add any missing language/library features as Phase-3/4 style tasks and do them first.
- [ ] **S1 Tokenizer in Cyan** (`selfhost/tokenizer.cyan`); differential test: token stream identical to C++ tokenizer on every test file.
- [ ] **S2 Preprocessor + parser in Cyan;** AST dump identical (`--print-ast`) for every test file.
- [ ] **S3 Semantic analysis + IR generation in Cyan;** `--emit-ir` identical.
- [ ] **S4 Regalloc + x86_64 backend in Cyan;** output assembly identical or binaries behaviorally identical on the full suite.
- [ ] **S5 riscv64 backend in Cyan.**
- [ ] **S6 Bootstrap:** C++ compiler builds `sodium1` (Cyan source); `sodium1` builds `sodium2`; `sodium2` builds `sodium3`; `sodium2` and `sodium3` binaries identical; full suite passes using `sodium3`.
- [ ] **S7 Make the Cyan compiler the default; keep the C++ bootstrap in `bootstrap/` with a script to rebuild.**
- [ ] **P9-END** gate, tag `selfhosted`.

## Standing chores (do opportunistically when touching the area; each is its own commit)
- [ ] Parser `parse_stmt` 30-branch chain → table/dispatch; precedence climbing (Pratt) for expressions.
- [ ] Remove duplicate AST visitors (printer/LSP/gen walks) → one visitor utility.
- [ ] `std::list<Type>` type pool → arena; `std::map`→`unordered_map` where order unneeded; trim `generation.hpp` includes.
- [ ] Eliminate remaining global mutable state (tokenizer/diagnostics/arena) so compilation is re-entrant (needed by LSP).
- [ ] Thread-safety note/policy for allocator (document single-threaded; or add a lock behind a flag).
