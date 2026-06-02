# Sodium / Cyan Compiler — Full Audit

Generated: 2026-05-31

Status legend:
- 🔴 **open** — unfixed, actionable
- 🟡 **in-progress** — being worked on
- 🟢 **fixed** — resolved
- ⚪ **wontfix** — accepted/out-of-scope
- ❔ **triage** — needs investigation

---

## PHASE 0 — ORIENTATION

### Pipeline Stage Mapping

| Stage | Status | Files | Notes |
|-------|--------|-------|-------|
| Lexer | ✅ Complete | `src/tokenization.hpp`, `tokenization.cpp` | Single-pass, full keyword set |
| Preprocessor | ✅ Complete | `src/preprocessor.cpp`, `preprocessor.hpp` | `#include`, `#pragma once`, `#error`, `#line` |
| Parser | ✅ Complete | `src/parser.hpp`, `parser.cpp` | Recursive descent, ~1800 lines |
| AST | ✅ Complete | `parser.hpp` (NodeXxx) | Variant-based, arena-allocated |
| Semantic Analysis | ⚠️ Partial | `src/gen/eval.cpp`, `gen/scope.cpp` | Type inference via fixed-point iteration; no type checker |
| IR Builder | ✅ Complete | `src/ir/builder.hpp` | SSA-style virtual registers |
| Optimization | ❌ Stub | — | Liveness analysis only |
| Register Allocation | ⚠️ Partial (buggy) | `ir/linear_scan.hpp`, `ir/rewriter.hpp` | Spill code exists but broken |
| Code Generation | ✅ Complete | `backend/x86_64/*`, `backend/riscv64/*` | Two full backends |
| Linker Interface | ❌ Stub | `src/main.cpp` | Shells out to `system()` for nasm/ld |

### Infrastructure Flags

| Item | Finding | Status |
|------|---------|--------|
| Build system | CMake, C++20, no warning flags | 🟢 fixed |
| Test framework | Integration-only (302 tests), no C++ unit tests | 🔴 open |
| Global state | `g_source_text`, `g_show_code`, `g_lsp_mode`, `g_lsp_errors` | 🔴 open |
| Error handling | `exit(EXIT_FAILURE)` on every error; LSP uses `throw LSPAbort` | 🔴 open |
| Shelling out | `system()` for asm, link, copy, temp cleanup | 🔴 open |

---

## PHASE 1 — LEXER & PARSER SHORTCUTS

### Lexer

| # | File | Lines | Description | Severity | Correct Approach | Status |
|---|------|-------|-------------|----------|-----------------|--------|
| L1 | `tokenization.cpp` | entire file | Hardcoded ASCII character classification (`std::isalpha`, `std::isdigit`) | Design debt | Document ASCII-only assumption or add Unicode normalization | ⚪ wontfix |
| L2 | `tokenization.hpp` | `TokenType` enum | No operator category table — can't ask "is this binary op?" without a switch | Design debt | Add `operator_category(TokenType)` array | 🔴 open |
| L3 | `tokenization.cpp:129-148` | string literal loop | **Escape sequences not processed** — `\n`, `\t`, `\"`, `\\`, `\x41` stored as raw bytes | **Correctness bug** | Implement escape processing: `\n`→0x0A, `\t`→0x09, `\"`→0x22, `\\`→0x5C, `\xNN`→byte, `\uNNNN`→UTF-8 | 🔴 open |
| L4 | `tokenization.cpp:60-70` | `consume()` | Line tracking only increments on `\n` — `\r\n` on Windows double-counts lines | Latent bug | Track `\r` as line increment too, or normalize input | 🔴 open |
| L5 | `tokenization.cpp` | after tokenize loop | `m_index` reset to 0 at end but `m_line`/`m_col` not reset | Minor | Reset all state if reusing Tokenizer | 🟢 fixed |
| L6 | `tokenization.hpp:95-100` | `peek()` | No sentinel for EOF — returns `{}`, correct but slightly awkward | Style | Fine | ⚪ wontfix |
| L7 | `tokenization.cpp` | identifier loop | **No identifier interning** — every identifier is a full `std::string` | Performance | Use `std::unordered_map<std::string, uint32_t>` intern table | 🔴 open |

### Parser

| # | File | Lines | Description | Severity | Correct Approach | Status |
|---|------|-------|-------------|----------|-----------------|--------|
| P1 | `parser.cpp` | 190–650 | Operator precedence via nested function calls (not Pratt/precedence table) | Design debt | Refactor to precedence-climbing or Pratt parser | 🔴 open |
| P2 | `parser.cpp` | entire file | **Unbounded recursion** — deep nesting causes stack overflow | **Correctness bug** | Add recursion limit counter or async trampoline | 🔴 open |
| P3 | `parser.cpp:109-116` | `error()` | **Error recovery: `exit(EXIT_FAILURE)`** — no multiple-error reporting | **User-facing quality** | Sync to `;` or `}` after error; collect multiple errors | 🔴 open |
| P4 | `parser.cpp:233-640` | unary prefix handling | **Unary minus faked as `0 - x`** — creates BinExprSub with int_lit(0) | Design debt | Add proper `NodeExprUnaryMinus` | 🔴 open |
| P5 | `parser.cpp:746-1310` | `parse_stmt()` | **Massive 30-branch if-else chain** with 5-token lookahead | **Maintainability hazard** | Factor into dispatch table + helper predicates | 🔴 open |
| P6 | `parser.cpp:1069-1097` | struct field assign | Hardcoded 5-token lookahead for field assignment detection | Fragile | Extract `is_assignment_operator(TokenType)` helper | 🟢 fixed |
| P7 | `parser.cpp:577-578` | `parse_format_string()` | No error on `%` at end of format string | Latent bug | Check `%` not at end of string | 🟢 fixed |
| P8 | `parser.cpp` | all AST nodes | No visitor pattern, no parent pointers — traversal logic duplicated 3x | Design debt | Add `NodeVisitor` base, add parent pointers | 🔴 open |

---

## PHASE 2 — AST & TYPE SYSTEM DEBT

### AST Structure

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| A1 | `parser.hpp` | Variant-based nodes (not hierarchy) — `std::variant<...15 types...>` | Design debt | ⚪ wontfix (idiomatic modern C++) |
| A2 | `gen/expr.cpp`, `gen/stmt.cpp`, `gen/eval.cpp` | Three separate `std::visit` visitor implementations with 15+ methods each — code duplication | Design debt | 🔴 open |
| A3 | `parser.hpp` | Raw pointer ownership via arena allocator — documented and correct | Acceptable | ⚪ wontfix |
| A4 | all NodeXxx | No parent pointers in AST nodes | Design debt | 🔴 open |
| A5 | all NodeXxx | No deep copies — only one construction path | Acceptable | ⚪ wontfix |
| A6 | `generation.hpp` | Mutable state in Generator side tables, not in AST nodes | Acceptable | ⚪ wontfix |

### Type System

| # | File | Lines | Description | Severity | Status |
|---|------|-------|-------------|----------|--------|
| T1 | `parser.hpp` | 40–73 | Tiny `Type` struct with enum Kind + union — no structural equivalence beyond pointer recursion | Design debt | 🔴 open |
| T2 | `parser.hpp` | 68 | **Type equality: `pointee == o.pointee` compares pointers, not values** — returns false for separately-allocated but structurally-equal types | **Latent correctness bug** | 🔴 open |
| T3 | `gen/scope.cpp`, `gen/eval.cpp` | — | No distinction between declared type and inferred type — merge heuristic fragile | Design debt | 🔴 open |
| T4 | `parser.hpp` | 43–56 | Pointer types = flag + pointee pointer — no const/volatile | Acceptable | ⚪ wontfix |
| T5 | — | — | No generic/template types | Missing feature | ⚪ wontfix |
| T6 | `gen/stmt.cpp`, `gen/vars.cpp` | — | Implicit conversion: ad-hoc truncation masking, no conversion matrix, no precision-loss warning | Design debt | 🔴 open |

### Memory

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| M1 | `arena.hpp` | Arena allocator used for parser + AST — documented, correct, but default-constructs | Acceptable | ⚪ wontfix |
| M2 | `generation.hpp:127` | **`std::list<Type>` for type pool** — individual heap allocations per type | Performance problem | 🔴 open |
| M3 | `generation.hpp` | No `shared_ptr` confusion — correctly uses raw pointers with scope lifetime | Good | 🟢 fixed |
| M4 | `main.cpp:163-164` | `new Backend` without delete in early-exit paths (OS reclaims) | Minor | ⚪ wontfix |

---

## PHASE 3 — SYMBOL TABLE & SCOPE

### Symbol Table

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| S1 | `generation.hpp:80-83`, `gen/scope.cpp` | Scope chain = `vector<Scope>` (stack of maps) — properly nested | Correct | 🟢 fixed |
| S2 | `gen/stmt.cpp` | All `enter_scope()` / `exit_scope()` balanced | Correct | 🟢 fixed |
| S3 | — | Forward function declarations work (all funcs collected before codegen) | Correct | 🟢 fixed |
| S4 | `gen/scope.cpp:declare_var()` | Forward variable declarations not supported | Missing feature | ⚪ wontfix |
| S5 | — | No function overloading — last definition wins | Missing feature | ⚪ wontfix |
| S6 | `ir/emitter.hpp:mangle_label()` | No mangling — plain function names used | Acceptable | ⚪ wontfix |
| S7 | `gen/scope.cpp:declare_var()` | Re-declaration in same scope = error | Correct | 🟢 fixed |

### Name Resolution

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| N1 | `gen/pipeline.cpp` | Two-pass approach via pre-scan (`collect_globals`) — handles most forward refs | Acceptable | 🟢 fixed |
| N2 | — | No qualified name lookup (no namespaces) | Missing feature | ⚪ wontfix |
| N3 | `gen/eval.cpp:collect_call_types()` | **Fixed-point iteration with fragile merge heuristic** — if `&i64_var` seen before `string_lit`, parameter locked to Ptr(i64) | Correctness hazard | 🔴 open |
| N4 | `gen/vars.cpp` | `"Undeclared identifier: X"` — no "did you mean?" suggestions | User-facing quality | 🟢 fixed |

### Scoping Rules Coverage

| Rule | Status |
|------|--------|
| Block scoping | ✅ Correct |
| Nested block shadowing | ✅ Correct |
| Function-level scope | ✅ Correct |
| Global scope | ✅ Correct |
| Forward function declarations | ✅ Works |
| Forward variable declarations | ❌ Not supported |
| Namespaces / qualified lookup | ❌ Not supported |
| Overload resolution | ❌ Not supported |
| Static variables | ✅ Implemented |
| Symbol hiding | ✅ Correct |

---

## PHASE 4 — IR & CODE GENERATION HACKS

### Intermediate Representation

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| I1 | `ir/*.hpp` | Proper SSA-style IR with basic blocks and vregs | Good | 🟢 fixed |
| I2 | `ir/builder.hpp` | **SSA-style, not true SSA** — no phi nodes, multiple defs per vreg possible | Design debt | 🔴 open |
| I3 | `ir/opcodes.hpp` | Phi nodes absent | Missing feature | 🔴 open |
| I4 | `ir/function.hpp:new_vreg()` | Simple `next_vreg++` counter — works for linear construction | Acceptable | 🟢 fixed |
| I5 | `ir/opcodes.hpp` | IR opcode set is reasonable (~40 opcodes) | Good | 🟢 fixed |

### Code Generation

| # | File | Lines | Description | Severity | Correct Approach | Status |
|---|------|-------|-------------|----------|-----------------|--------|
| G1 | `ir/rewriter.hpp` | `insert_spill_code()` | **Spill code exists but buggy** — BUGS.md says it's missing, rewriter.hpp has it but uses hardcoded `fp` reg; interactions with prologue not fully correct | **Correctness bug** | Fix spill code to use computed frame offset, test with high register pressure | 🔴 open |
| G2 | `ir/emitter.hpp:CALL` | — | **Calling convention: stack args pushed with `push()`, cleaned up with `adjust_stack()`** — RSP mismatch if compiler is interrupted between push and call. Overflow args saved FIRST, then register args set up in reverse order. Comment acknowledges "CRITICAL" but workaround is fragile | **Correctness bug** | Pre-compute stack frame for overflow args in prologue, use MOV not PUSH | 🔴 open |
| G3 | `gen/scope.cpp:declare_var()` | — | **Stack frame layout wrong for packed types** — `i8` var at offset 0, next at offset 1, but `FRAME_ADDR` formula `-(byte_offset + 8)` assumes 8-byte slots. Small types overlap | **Correctness bug** | Change `FRAME_ADDR` to use byte-precise offset: `fp - offset - size` | 🔴 open |
| G4 | `ir/emitter.hpp` | — | Instruction selection via switch on IROpcode — fine for 40 opcodes | Acceptable | ⚪ wontfix |
| G5 | — | — | No peephole optimization | Missing feature | 🔴 open |
| G6 | `ir/emitter.hpp:mangle_label()` | — | Jump targets resolved by label strings with func-name prefix | Acceptable | 🟢 fixed |
| G7 | — | — | No PIC support | Missing feature | 🔴 open |

### Correctness Shortcuts

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| C1 | `gen/expr.cpp` | Expression evaluation order: left-to-right consistently (accidentally defined) | Acceptable for now | ⚪ wontfix |
| C2 | `gen/expr.cpp` | Signed integer overflow wraps (defined on target, UB in C++ for const eval) | **UB in C++ code** | 🔴 open |
| C3 | — | No floating point | N/A | ⚪ wontfix |
| C4 | `gen/expr.cpp` | **Division by zero not checked** — runtime SIGFPE | **Correctness bug** | 🔴 open |
| C5 | `gen/stmt.cpp:NodeStmtArrDecl` | Const array size expression: if it can't be folded, generic error message | Latent bug | 🔴 open |

---

## PHASE 5 — ERROR HANDLING & DIAGNOSTICS

### Error Propagation

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| E1 | ~30 call sites | **`exit(EXIT_FAILURE)` on every error** — no recovery, no multiple reporting | User-facing quality | 🔴 open |
| E2 | `tokenization.hpp:75-80` | **Global state** (`g_lsp_errors`, `g_lsp_mode`, `g_show_code`, `g_source_text`) — not thread-safe | Internal reliability | 🔴 open |
| E3 | `lsp_exit()` | `throw LSPAbort()` from `lsp_exit()` — callers don't know it may throw; destructors may be skipped | Correctness hazard | 🔴 open |
| E4 | — | Cascading errors not an issue (exit on first error) — would be severe if recovery added | Acceptable | ⚪ wontfix |
| E5 | — | No `exit()`/`abort()` in recovery paths | Acceptable | 🟢 fixed |

### Diagnostic Quality

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| D1 | `tokenization.cpp:135` | Error messages occasionally show raw characters but mostly clean | Acceptable | 🟢 fixed |
| D2 | `tokenization.hpp:5-12` | Source locations tracked at file:line:col — good | Good | 🟢 fixed |
| D3 | `gen/vars.cpp` | **No "did you mean?" suggestions** for typos | User-facing quality | 🟢 fixed |
| D4 | — | Errors reported at correct location (not closing brace) | Good | 🟢 fixed |
| D5 | `gen/eval.cpp:109-114` | **Almost no warnings** — only one (conflicting pointer types). No "unused variable", "unreachable code", "missing return", "implicit narrowing" | User-facing quality | 🔴 open |
| D6 | all `lsp_exit`/`error()` | **No error/warning/note distinction** — everything is "error:" | User-facing quality | 🔴 open |

### Infrastructure

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| F1 | all error paths | Diagnostics printed directly to `std::cerr` — no diagnostic engine | Internal reliability | 🔴 open |
| F2 | — | No machine-readable output (JSON/SARIF) | Missing feature | 🔴 open |
| F3 | — | No error limit (not needed since exit on first error) | Acceptable | ⚪ wontfix |

---

## PHASE 6 — C++ IMPLEMENTATION QUALITY

### Undefined Behavior

| # | File | Lines | Description | Severity | Status |
|---|------|-------|-------------|----------|--------|
| U1 | `gen/eval.cpp` | BinExprAdd etc. | **Signed integer overflow in const-eval is C++ UB** — `INT64_MAX + 1` | **UB** | 🔴 open |
| U2 | `gen/expr.cpp` | `std::stoll(...)` | **`std::stoll` throws `std::out_of_range` on overflow** — not caught, `std::terminate()` | **Correctness bug** | 🔴 open |
| U3 | `ir/rewriter.hpp:131` | `assert()` compiled out in release | If spilled vreg not in allocation map, null dereference | Reliability | 🔴 open |

### Ownership & Memory

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| O1 | `generation.hpp:133` | `Backend*` raw pointer — non-owning, externally owned | Acceptable | 🟢 fixed |
| O2 | `main.cpp:191,220` | **`system("cp ... " + output_path)` — command injection** if filename has shell chars | **Security bug** | 🔴 open |
| O3 | `parser.hpp` | AST nodes contain `Token` by value (`std::optional<std::string>`) — moderate copy cost, not hot path | Acceptable | ⚪ wontfix |
| O4 | `gen/stmt.cpp` | `FormatSpec::text` copies string per fragment — could use string_view | Performance | 🔴 open |

### C++ Antipatterns

| # | File | Lines | Description | Severity | Status |
|---|------|-------|-------------|----------|--------|
| X1 | `ir/emitter.hpp` | — | **Virtual dispatch in inner loop** — each IR instruction calls multiple Backend virtual methods | Performance | 🔴 open |
| X2 | `ir/liveness.hpp:34` | — | **`std::map` where `unordered_map` suffices** — `block_starts` only needs label lookup | Performance | 🔴 open |
| X3 | — | — | No `dynamic_cast` abuse | Good | 🟢 fixed |
| X4 | `ir/builder.hpp:56-57` | — | `#define IR_BINOP` creates 20+ methods — acceptable macro use | Acceptable | ⚪ wontfix |
| X5 | `generation.hpp` | — | **Heavy `#include` chains** — touching any IR header recompiles all gen/*.cpp | Maintainability | 🔴 open |

### Modern C++ Gaps

| # | File | Description | Severity | Status |
|---|------|-------------|----------|--------|
| Z1 | — | `std::variant` and `std::optional` used correctly throughout | Good | 🟢 fixed |
| Z2 | `generation.hpp` | Move semantics used for AST root in constructor | Good | 🟢 fixed |
| Z3 | — | No `noexcept` annotations (except `lsp_exit` `[[noreturn]]`) | Acceptable | ⚪ wontfix |

---

## PHASE 7 — FINAL TRIAGE & PRIORITY RANKING

### 1. CORRECTNESS BUGS (fix before anything else)

| ID | Description | File(s) | Effort | User Likelihood | Status |
|----|-------------|---------|--------|-----------------|--------|
| L3 | **String escape sequences not processed** — `\n`, `\t`, `\"` stored as raw bytes | `tokenization.cpp:129-148` | Small (hours) | **Any string with escapes** | 🔴 open |
| U1/U2 | **Const-expr overflow = C++ UB / `std::stoll` throws** | `gen/eval.cpp`, `gen/expr.cpp` | Small (hours) | Large const integer literals | 🔴 open |
| G3 | **Stack frame layout wrong for packed types** | `gen/scope.cpp`, `ir/builder.hpp` | Medium (days) | Using `i8`/`i16`/`i32`/`u8`/`u16`/`u32` variables | 🔴 open |
| C4 | **Division by zero not checked** | `gen/expr.cpp` | Small (hours) | Any `x / 0` or `x % 0` | 🔴 open |
| T2 | **Type equality compares pointers, not values** | `parser.hpp:68` | Small (hours) | Two identical pointer types from different sources | 🔴 open |
| O2 | **Shell injection via `system()` calls** | `main.cpp:191,220` | Medium (days) | `-o` path with spaces/semicolons | 🔴 open |
| G2 | **Calling convention: RSP mismatch on overflow args** | `ir/emitter.hpp:CALL` | Medium (days) | Calls with >6 args (x86-64) or >8 args (RISC-V) | 🔴 open |
| G1 | **Spill code buggy — uses hardcoded fp, no proper test coverage** | `ir/rewriter.hpp` | Medium (days) | Functions with high register pressure | 🔴 open |

### 2. MISSING FOUNDATIONS (hard to grow without fixing)

| ID | Description | Effort | Blocks | Status |
|----|-------------|--------|--------|--------|
| I2/I3 | **No true SSA (phi nodes absent)** — blocks standard optimizations | Large (weeks) | GVN, SCCP, loop opts, DCE | 🔴 open |
| P3/E1 | **No error recovery — parser exits on first error** | Large (weeks) | Multi-error reporting, LSP quality | 🔴 open |
| T5 | **No generic/template support** — everything monomorphic | Large (weeks) | Code reuse, standard library | 🔴 open |
| I5/F1 | **No diagnostic engine** — errors go to `std::cerr` or globals | Medium (days) | Warnings, SARIF, error limits | 🔴 open |
| E2 | **Global mutable state** — prevents threading, complicates testing | Medium (days) | Parallel compilation, testability | 🔴 open |
| — | **No optimization passes** — liveness+alloc+emit only | Medium (days) | Performance, competitive quality | 🔴 open |

### 3. QUALITY DEBT (user-visible, fixable without restructuring)

| ID | Description | Effort | Status |
|----|-------------|--------|--------|
| L3 | String escape sequences unhandled | Small | 🔴 open (same as P0 correctness) |
| D3 | No "did you mean?" suggestions for typos | Medium | 🟢 fixed |
| D5 | Almost no warnings (unused vars, unreachable code, narrowing) | Medium | 🔴 open |
| E5/D6 | No error/warning/note distinction | Small | 🔴 open |
| — | `read()` returns 0 at EOF, indistinguishable from input 0 | Small | 🔴 open |
| — | RISC-V doesn't use PIC | Medium | 🔴 open |
| — | `print_code_context` wrong for `#line`-adjusted files | Small | 🔴 open |

### 4. INTERNAL DEBT (invisible to users, painful for maintainers)

| ID | Description | Effort | Status |
|----|-------------|--------|--------|
| — | **No C++ unit tests** — all tests are integration, debugging is slow | Large (weeks) | 🔴 open |
| P5 | **Parser `parse_stmt()` is 30-branch if-else with 5-token lookahead** | Medium (days) | 🔴 open |
| M2 | **`std::list<Type>` for type pool — individual heap allocations** | Small | 🔴 open |
| X1 | **Virtual dispatch in hot IR emitter loop** | Medium | 🔴 open |
| X5 | **Heavy `#include` chains in `generation.hpp`** | Small | 🔴 open |
| X2 | `std::map` where `unordered_map` suffices (liveness) | Small | 🔴 open |
| O4 | String copies in FormatSpec | Small | 🔴 open |
| A2 | Three duplicated visitor implementations | Medium | 🔴 open |
| A4/P8 | No parent pointers in AST nodes | Medium | 🔴 open |
| P1 | Precedence via nested function calls (not Pratt) | Large | 🔴 open |

---

## SUMMARY COUNTS

| Category | Count |
|----------|-------|
| 🔴 Open (unfixed) | 40 |
| 🟢 Fixed | 17 |
| ⚪ Wontfix | 15 |
| 🟡 In-progress | 0 |
| ❔ Triage | 0 |

---

## RECOMMENDED ORDER OF ATTACK

### 1. Fix string escape sequence handling (L3) — 2–4 hours
Every string literal with `\n`, `\t`, `\"`, `\\`, `\xNN` is silently wrong. This is the single highest-impact correctness fix. File: `tokenization.cpp:129-148`.

### 2. Fix packed stack frame layout (G3) — 1–2 days
Without this, all typed variables smaller than 64 bits (`i8`, `i16`, `i32`, `u8`, `u16`, `u32`) overlap on the stack. Requires changes to `gen/scope.cpp:declare_var()` and `ir/builder.hpp:frame_addr()`.

### 3. Add error recovery to parser (P3/E1) — 1–2 weeks
Currently shows exactly one error then dies. Adding recovery (skip to next `;` or `}`) requires changing `error()` to not `exit()`, adding synchronization points, suppressing cascading errors, and updating LSP. Single biggest UX improvement.
