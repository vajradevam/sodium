# Design decisions (append-only; newest at bottom). Do not relitigate without new evidence.

## D-001 Platform / ABI
Linux only. x86_64 follows System V AMD64 ABI; riscv64 follows the LP64D psABI integer subset
(no floats until Phase 6). Stack 16-byte aligned at calls. Programs are freestanding (no libc);
`sodium-rt` provides the runtime.

## D-002 Explicit types are the future; inference stays as a fallback
Current Cyan has untyped params (`function f(a, b)`) and call-site pointer inference. New syntax
(accepted *in addition*; old programs keep compiling until Phase 3 ends, then untyped params
produce a warning):
```
function strlen(s: u8*) -> i64 { ... }      // typed params + return type
var p: u8* = malloc(16);                    // pointer types are `T*`, nestable `T**`
struct Point { var x: i32; var y: i32; };   // typed fields; untyped field = i64
```
Pointer types print/parse as `T*` everywhere (internal `Ptr(T)` is an implementation detail;
fix any docs/comments that say otherwise).

## D-003 New type syntax
- `bool` (1 byte, values 0/1; `true`/`false` keywords), `char` = alias of `u8`, `null` = `0` as `T*`.
- Cast: `expr as T`; binds tighter than `*` and `/` (same level as unary operators).
- Alias: `type Name = T;`.   Enum: `enum Color { Red, Green = 5, Blue };` (i64 constants, namespaced
  as plain identifiers; duplicates are errors).
- Function pointer type: `fn(i64, i64) -> i64`; call through variable `f(1,2)`; `&func` yields one.
- Sized struct layout follows C rules (natural alignment, trailing padding) once typed fields land;
  `sizeof(T)` and `sizeof(expr)` are compile-time builtins returning bytes.
- Struct/array by-value passing and return use the platform ABI (copy via hidden pointer is acceptable
  internally as long as it is semantically by-value).

## D-004 Diagnostics
Format: `file:line:col: error|warning|note: message` followed by the source line and caret.
Compiler collects multiple errors (cap 20, flag `-ferror-limit=N`), exits nonzero if ≥1 error.
`-Werror` promotes warnings. Warnings: unused variable, unreachable code, narrowing assignment,
missing return, conflicting inferred pointer types.

## D-005 Tests
Tests are discovered, not registered: every `tests/**/*.cyan` is run. Header comments declare expectations:
`// EXPECT-EXIT: N`, `// EXPECT-STDOUT: <line>` (repeatable, ordered, exact), `// EXPECT-STDERR: <substring>` (repeatable), `// EXPECT-ERROR: <substring>`,
`// ARGS: ...`, `// STDIN: ...`, `// ENV: NAME=value` (repeatable, applied with `env`), `// TARGETS: x86_64 riscv64` (default both). Files named `test_bad_*`
must declare `EXPECT-ERROR`. `// SKIP-TEST: <reason>` marks a file that is included by other tests and
is not a standalone test (currently `tests/include/helper.cyan`). An `EXPECT-STDOUT` header declares an
exact, ordered line of stdout; when any are present the full stdout must match them line for line.
`// EXPECT-WARNING: <substring>` asserts a successful compile emits a `warning:` diagnostic containing the substring.
`// NO-WARNING: <substring>` asserts the compile log does not contain the substring (used with `-w`).
`// COMPILE-FLAGS: -w -Werror ...` adds compiler flags to the compile step (used to test warning policy).

## D-006 Optimization policy
Correctness first. `-O0` (default for now) must remain a straightforward translation; optimizations
are opt-in `-O1` until the suite has run clean under them for a full phase, then become default.
Every optimization has a test that checks the *effect* (IR dump) and one that checks *semantics* unchanged.

## D-008 I/O: EOF detection
`read()` returns 0 on EOF, which is indistinguishable from a real zero. The builtin
`read_eof()` returns 1 if the most recent `read()` (integer or buffer form) exhausted
stdin without finding data, and 0 otherwise. Both forms share an input buffer and a
single EOF flag in the runtime, so `read_eof()` works after either form. The flag is
reset whenever a refill succeeds or buffered data is available.

## D-007 Self-hosting prerequisites
Self-hosting is attempted only after: typed params/returns, typed struct fields, arrays of structs,
nested structs, function pointers, enums, a string library, file I/O, a growable heap. Phase 9 stages
must each be verified by running the stage-N output against the full test suite.

## D-009 Warning policy (E3)
Warnings are emitted by default; `-w` suppresses them and `-Werror` promotes them to errors
(a promoted warning is rendered with severity `error` and makes compilation fail). Implemented
warnings: unused variable, unused parameter, unreachable code, missing return (functions other
than `main`, which is defined to exit(0) when it falls off the end), narrowing assignment (a
compile-time constant does not fit the declared integer type), unused result of a value-returning
function with no observable side effects, and `=` in condition (with a `==` fix-it). An `=` in a
condition is still a syntax error; its warning is a hint about the likely typo.

## D-010 Notes and color (E4)
Notes are first-class diagnostics with their own source location (`add_note_at`), used for
"previous declaration here" on redeclarations. "Did you mean" suggestions are attached to the
preceding error as a note plus a replacement fix-it; the error text is unchanged for compatibility.
`-fcolor-diagnostics` enables ANSI colors (bold location, red error, magenta warning, cyan note,
green fix-it); `-fno-color-diagnostics` and a non-empty `NO_COLOR` disable them. Plain output is
the default so compiler logs stay deterministic.

## D-011 Pointer-to-struct access (Y7)
Cyan uses C-like `p->field` for accessing a field through a pointer to a struct.
`(*p).field` is not supported: field access accepts only an identifier (or another field
access) as its base, not an arbitrary parenthesized expression. `&s.field` yields a pointer
to the field. Struct identity is carried in `Type` so pointer types retain their pointee struct.

## D-012 Struct-by-value ABI (Y8b)
Structs passed or returned by value follow the platform ABI of each target (System V AMD64 and
RISC-V LP64), so Cyan code stays binary-compatible with C. Scope: integer and pointer fields only
(no floating point exists in Cyan), natural-alignment C layout (D-002 style).
- **Value representation inside the compiler:** an expression of struct type evaluates to the
  *address* of its bytes (a variable, or a temporary for call results); consumers copy from it.
- **Parameters:** size <= 16 bytes: the struct is split into 8-byte words in order, each word in
  the next integer argument register (a trailing partial word is zero-extended; only the real
  bytes are read from the source). Larger structs: x86_64 copies them to the stack argument area;
  riscv64 passes a pointer to a caller-made copy.
- **Returns:** size <= 8 in rax / a0; size <= 16 in rax:rdx / a0:a1; larger via a hidden pointer
  passed as the first argument (rdi / a0) which the callee also returns.
- Known non-goals: floating-point/SSE classification, structs with 16-byte alignment.
- IR consequence: two-register results need `RET2` and a CALL with a second destination (`dst2`);
  `TargetRegisterInfo::ret_reg2` names the second return register (rdx / a1).

## D-013 Memory layout is C layout (Y2d)
Objects in memory follow C: arrays and struct fields ascend in address (`base + i*size`,
`base + field_offset`), 16/32/64-bit values are stored little-endian at their own address, local
arrays and structs are 8-byte aligned and occupy a multiple of 8 bytes of frame, and scalars
are packed at their natural alignment. Earlier compilers addressed local aggregates downward and
stored narrow values byte-reversed at `addr-w+1`; that corrupted neighbouring locals and made
`ptr + n` on a decayed local array walk backwards. Struct ABI (D-012) and C interop depend on this.

## D-014 Indirect calls use a dedicated opcode (Y10)
Calls through a function pointer are a separate IR opcode, `CALL_INDIRECT`, not a thunk or a
trampoline: `%dst = call_indirect %target(args...)`. The target is the *last* operand, so the
argument operands keep their positions (argument i is operand i) and liveness sees the target as an
ordinary use. `reg_args`, `dst2` and stack arguments behave exactly as for `CALL` (D-012), so
struct arguments and returns work through pointers too.
- The call-target register is a fixed non-argument, caller-saved register
  (`TargetRegisterInfo::call_target_reg`: rax on x86_64, t6 on riscv64). The emitter treats it as one
  more destination of the argument parallel move, so a target living in an argument register, in
  another argument's source, or in a spill slot is read before it is overwritten.
- `Backend::call_reg(reg)` emits `call reg` / `jalr reg`. Direct `CALL` is unchanged.
- Surface language (extends D-003): type `fn(T1, T2) -> R`; the `-> R` part is optional and a missing
  one means "unannotated", exactly like a function definition without `->` (the result is a plain
  i64). Internally a function pointer is `Ptr(Func)`, so it is 8 bytes and loads/stores/compares
  like any pointer. `&func` yields the pointer for a defined function (untyped parameters count as
  i64). A call is indirect when the callee is a local/parameter of that type, or any expression
  (`s.cb(x)`, `p->cb(x)`, `table[i](x)`, `f(1)(2)`, `(&f)(x)`). The compiler checks argument count
  and types against the signature, and initialization/assignment/return against the declared
  `fn` type (a constant `0` is accepted as null in initializers and assignments). A call expression
  now has its declared return type instead of always i64, so returned pointers can be passed on.
  Not covered: `fn` pointers in untyped globals, and a bare function name without `&`.
- Rejected: thunks/trampolines (an extra jump per call, a per-function data table, and the call
  path would differ from a direct call); loading the target into an argument register and
  interpreting it in the backend (breaks for 6+ register arguments).

## D-015 print format specifiers (L2a)
`print`'s first string-literal argument is a C-style format string. In addition to the original
`%d`, `%s` and `%%`, these conversions are supported:

- `%x` — lowercase hexadecimal of the argument's 64 bits, no `0x` prefix (`-1` prints `ffffffffffffffff`).
- `%u` — the argument's 64 bits as unsigned decimal (`-1` prints `18446744073709551615`).
- `%c` — exactly one raw byte, the low 8 bits of the argument (a NUL byte is emitted, not skipped).
- `%ld` — decimal i64. Because Cyan integers are i64 (LP64, D-007), this is an alias of `%d`; the
  `l` modifier is recognized only in front of `d`, and `%l` followed by anything else is an error.

`%%` still emits a literal percent; unknown conversions remain literal text (backward compatibility).
Each conversion consumes one argument, and a shortage is a compile error. Conversion lives in the
freestanding runtime (`sodium-rt/printfmt.c` plus a `_sodium_write_buf` write helper in each
backend's `print.asm`), not in generated IR, so both targets share the same formatting rules.

## D-016 Allocation wrappers and leak-check mode (L3)
`cyanlib/mem.cyan` is the Cyan-side memory wrapper layer:

- `calloc(count, size)` zeroes the allocation and returns null if `count * size` overflows i64.
- `realloc(p, old_size, new_size)` takes the **old size explicitly**: the runtime allocator does not
  expose a block's usable size, so a C-style one-argument `realloc` cannot be both safe and portable
  here. It copies `min(old_size, new_size)` bytes and frees the old block.
- `aligned_alloc(alignment, size)` accepts any power of two and over-allocates to align the address;
  the original malloc pointer is stored in the eight bytes immediately before the result. It must be
  released with `aligned_free`, not `free`.
- `SODIUM_ALLOC_DEBUG` is a **compile-time Cyan constant** (1 by default) controlling a library-level
  live-allocation counter. `mem_malloc`/`mem_free` and everything built on them update it;
  `mem_live_count()` reports it. Setting the constant to 0 removes the tracking. This is deliberately
  a Cyan-level count, not a flag read from the environment: the freestanding runtime has no env
  access on either backend.

## D-017 Raw file syscalls (L4a)
Cyan exposes six thin builtins that map directly to freestanding runtime syscalls on both targets:
`sys_open(path, flags, mode)`, `sys_close(fd)`, `sys_read(fd, buf, n)`, `sys_write(fd, buf, n)`,
`sys_lseek(fd, offset, whence)` and `sys_unlink(path)`. They return the raw syscall result
(non-negative on success, negative errno-style value on failure) as an i64; no errno variable is
used. The runtime provides generic `_sodium_syscall3/4` stubs per backend plus a shared C wrapper
that supplies the per-architecture syscall numbers. RISC-V has no `open`/`unlink` syscall, so those
map to `openat`/`unlinkat` with `AT_FDCWD`. Flag values use the Linux generic (`asm-generic`) values,
which coincide on x86_64 and riscv64. Arity is checked at compile time.

## D-018 io.cyan conventions (L4b)
`cyanlib/io.cyan` builds higher-level helpers on the raw D-017 builtins. `io_write_file` returns
0 on success and a negative code on open/write failure; `io_read_file` returns a malloc'd,
NUL-terminated buffer (caller frees it) or null. `io_next_line` truncates to `cap-1` bytes and
returns the position after the newline (or -1 at EOF). `eprint`/`eprintln` write to fd 2, and
`io_perror(msg, code)` reports on stderr and returns `code` for `main` to return as the exit status.
There is no user-facing `exit()` builtin: a program's exit code is its `main` return value.

## D-019 Cyan container conventions (L5)
`cyanlib/containers.cyan` exposes `Vec`, `Str` and `Map` by pointer: `*_init`, operations, then
`*_free`. Capacity doubles on growth; buffers are malloc'd and the caller must free them (`Str`'s
`str_build` finalises in place, so use `str_free` on the returned buffer). `Map` is an
open-addressed string→i64 table with linear probing and tombstones so removals do not break
lookups; keys are copied into the map. `map_get` returns 0 for a missing key — callers that care
must use `map_contains`. `vec_checksum` and `map_checksum` are deterministic digests intended for
property tests (`map_checksum` is order-independent so it survives table growth).

## D-020 exit and envp builtins (L6a)
`exit(n)` is an expression-style builtin that calls `_sodium_exit(n)` and produces no value; its
arity is checked at compile time. `envp(i)` returns the i-th environment pointer (`Ptr(u8)`) or null
at the end, mirroring `argv(i)`. `__sodium_envp` is a runtime global captured by each target's
`_start` at `sp/rsp + 16 + argc*8` (environment entries follow argv's NULL terminator), so both
x86_64 and riscv64 see the same vector. The test runner accepts repeatable `// ENV: NAME=value`
headers and runs the program under `env` with those variables set.

## D-021 fd printing and assert (L6b)
`cyanlib/io.cyan` adds `fprint(fd, s)`, which writes a NUL-terminated string to any descriptor via
`sys_write` and returns the byte count, and `assert(cond, msg)`, which reports
`Assertion failed: <msg>` on stderr and exits with status 1 when the condition is zero. `assert` is
a library function rather than a special statement, so it composes with the rest of the language
and needs no new parser form. The runner's `// EXPECT-STDERR: <substring>` header performs a
substring check on stderr (stdout keeps exact ordered-line comparison).

## D-022 IR verifier (O1)
`--verify-ir` checks each IR function after generation and before allocation; Debug builds always
run it (`SODIUM_IR_VERIFY`). The checks are: unique block labels; JMP/BR targets name existing
blocks; terminators have the required shape and never define a result; operand counts, vreg-vs-
immediate kinds and widths are consistent; and every use of a virtual register is dominated by a
definition of that same vreg in the CFG (fallthrough edges included, dead instructions after the
first terminator ignored). Because a join with multiple predecessors has no dominating arm-local
def, multi-arm expressions write a shared result vreg defined before the branch. A verifier failure
is an internal compiler error: print `IR verification failed` and abort compilation rather than
emitting a source diagnostic.

## D-023 Constant folding semantics (O2)
`-O1` runs `src/ir/fold.hpp` over each function before allocation. Folding uses unsigned
two's-complement arithmetic so wrapping addition/subtraction/multiplication matches what the
backend emits; division/modulo are folded only when the divisor is non-zero and the case
`INT64_MIN / -1` cannot occur, because those trap at runtime. Shifts are folded only for counts
0..63. Algebraic rewrites keep the original `dst` and turn into `copy`/`load_i64` rather than
rewriting uses, so dominance is unchanged. Constants are propagated only through vregs with a
single definition; phi-like shared result vregs (from ternary expressions) are never treated as
constants, since their value depends on which branch was taken. `-O0` remains the default and is
verified to contain none of these rewrites; the suite is run under both levels.

## D-024 CFG cleanup under -O1 (O3)
The `-O1` pipeline removes unused pure results (never loads or calls), drops the dead tail after
a block's first terminator, deletes unreachable blocks, and threads jumps through empty and
jmp-only blocks. A vreg with several definitions is kept as a whole: if it has any use, all its
definitions stay so dominance is unaffected. Removing a jmp-only block must make an implicit
fallthrough from the preceding block explicit as a JMP to the block's target; otherwise that
predecessor would fall into the block that follows the removed one. `-O0` never runs this pass.

## D-025 Local CSE under -O1 (O4)
`src/ir/cse.hpp` numbers expressions within a single block: repeated pure instructions become
copies of the earlier result, an 8-byte load directly after a store to the same `[base + offset]`
is replaced by that stored value, and duplicate stores of the same value are dropped. Loads are
never CSE'd (they are only forwarded from a tracked store), any store discards all forwarded
values, and calls/syscalls discard every fact. DIV and MOD are also barriers: they implicitly
clobber rax/rdx, which the allocator does not treat as a call, so keeping a value live across
one could let it be assigned a clobbered register. Expression facts are indexed by result and
operand vregs so invalidation is proportional to affected entries, not the whole block.

## D-026 Loop-aware liveness (O0)
`compute_live_ranges` now solves live-in/live-out per basic block over the CFG to a fixpoint,
using upward-exposed uses (a vreg is a use only before its first def inside the block). Edges are
the first terminator's JMP/BR targets or fallthrough to the next block. Each vreg still gets one
interval for the linear-scan allocator: it spans the earliest live point and the latest live
point in layout order, including whole blocks where the value is live-in/live-out. This can
overestimate when live blocks are not contiguous, but never shortens a lifetime, so values held
in vregs across loop back edges get callee-save registers (or spills) when a later CALL in the
loop body would clobber them. A call that defines the value itself does not make it `spans_call`.

## D-027 O5 uses shared vregs, not phi nodes or block arguments
For mem2reg-style promotion, a scalar local is represented by **one persistent virtual register
with several definitions** (the same idiom already used for ternary result vregs): every
assignment copies into that vreg and every read copies out of it. Neither phi instructions nor
block arguments are added to the IR. This keeps the verifier's rule — a use must be dominated by
at least one definition — sufficient, and avoids teaching the emitter, rewriter and allocator a
new edge-shuffle or parallel-copy mechanism. The cost is a longer live interval per promoted
local, which is exactly what the O0 loop-aware liveness fix makes correct. Promotion is performed
as an IR pass over frame slots whose addresses never escape; a slot is only promoted when every
load is dominated by a store to that slot, so uninitialized reads keep their frame behaviour and
no zero-initialization is invented.

## D-028 DIV/MOD live ranges need callee-save registers
`idiv` writes rax and rdx without the register allocator seeing a definition, so any other value
live across a DIV/MOD cannot occupy those registers. `LiveRange::crosses_div` records this (with
the same "a call that defines the value does not count" rule as `spans_call`), and linear scan
restricts such ranges to callee-save registers just like call-spanning ones. Variable shifts are
already handled by the backends, which save and restore the count register. This became visible
when O5 promoted a loop variable whose live range then crossed a modulo in `test_comprehensive`.

## D-029 Spill heuristic uses next-use positions (O6)
Linear scan now keeps each interval's sorted use positions. When no register is free it evicts the
active value whose next use at the current position is farthest away (binary-searched), rather
than the one with the furthest interval end. A value needing a callee-save register (call- or
DIV/MOD-crossing) is never evicted for one that does not. `scripts/bench_regalloc.sh` reports
spills and callee-save assignments across five programs on both targets on every gate run; the
heuristic change keeps the total at the baseline (2 spills) while the crafted one-register unit
case shows the intended different choice.

## D-030 Peephole is a post-emission, per-target textual pass (O7a)
Rather than adding ad-hoc checks inside each Backend's emission methods, the peephole optimizer is a
pure function `peephole_optimize(assembly, target_name)` over the emitted assembly text. It runs
after `gen_prog()` and before the assembly file is written, only under `-O1` (D-006). Rules are
split into a target-independent set and a small target-specific set, and are deliberately
conservative so they are provably safe on raw text:
  * self-move / no-op removal (`mov r,r` on x86-64; `mv r,r` and `addi r,r,0` on riscv64);
  * adjacent dead-move elimination (`mov A,B` immediately followed by `mov A,C` drops the first).
Segments are processed independently between labels/directives, so a rule never crosses a
basic-block boundary where control flow could have observed the overwritten value. No rule
changes condition flags or removes a memory operation. The pass reports how many instructions it
removed so the effect is measurable in the gate.

## D-031 Leaf-function inlining is IR-level and runs before -O1 passes (O7b)
A callee is eligible for inlining when it has exactly one basic block ending in a single
RET/RET_VOID, contains no CALL, CALL_INDIRECT, SYSCALL, JMP, BR or RET2, has at most 24
instructions, and no other function takes its address with LEA_LABEL. Recursion is excluded by
the leaf rule. Because the Generator currently emits and destroys each function immediately,
all function IR is first generated into a pool, the inliner runs over that pool (under `-O1`
only), and then each function goes through the normal optimize → allocate → rewrite → emit
pipeline. The -O0 path is unchanged. Inlining splices a fresh-vreg copy of the callee's single
block into the call site: parameter STORE_PARAMs become plain stores of the call's argument
vregs, and every callee frame offset is shifted below the caller's frame so slots cannot
collide. mem2reg then promotes the inlined slots, and folding/CSE clean up the copies. No new IR
form is introduced — only existing COPY/STORE/FRAME_ADDR instructions — so the verifier and
backend need no changes.

### D-031 refinement: inlining eligibility
The first eligibility rule allowed any single-block leaf, which let struct-ABI callees through.
Those use `LEA` of a `FRAME_ADDR` plus nonzero-offset loads/stores to build aggregates; shifting
only the `FRAME_ADDR` base is not enough for that pattern, and it corrupted struct-return tests
under `-O1`. Eligibility now additionally requires: no `LEA`/`LOAD_PARAM`, every load/store at
offset zero, and every `STORE_PARAM` full-width (`I64`) with no chunk shift. This keeps inlining
to genuine scalar leaves (e.g. `add(a,b)`, `square(x)`, void helpers) and leaves all aggregate
traffic to the regular calling convention.

## D-032 -O1 becomes the default optimization level (O8b)
D-006 made -O0 the default and -O1 opt-in during development. The O8b gate required the whole
suite (675 tests on x86_64 and riscv64) and the differential oracle (58 comparisons) to pass
under -O1; both do, so the default is now -O1. -O0 remains fully supported and is still covered
by the gate: `check.sh` runs the discoverable suite through `scripts/sodium-o0.sh` and the -O1
suite through `scripts/sodium-o1.sh`, so both levels are tested on every commit. Benchmark
instruction counts and the regalloc benchmark pass explicit levels and are unaffected.

### D-032 consequences for the gate and inliner
Making -O1 the default required two robustness fixes found by running every gate step under it.
Functions with a generation error are no longer optimized or inlined (partial IR from a clean
compile-error diagnostic must not turn into a verifier failure), and the leaf inliner keeps the
CALL when a void-returning callee's result vreg is actually used. The ABI interop harness rewrites
`call SYM` lines to redirect Cyan stubs to C definitions, so it compiles the Cyan side at -O0
explicitly; `check.sh` runs both the suite and the differential oracle at -O0 and -O1, so both
levels remain covered on every commit.

## D-033 Preprocessor macro and conditional semantics (X2)
The preprocessor remains a line-oriented textual stage before tokenization. Object-like macros
(`#define NAME replacement`) and function-like macros (`#define F(a,b) body`) are expanded on
subsequent non-directive lines by whole-identifier substitution; occurrences inside string or
character literals are left untouched. Function-like macros use argument substitution only —
no stringification (`#`) or token pasting (`##`) in this phase. Conditional directives are
`#ifdef`, `#ifndef`, `#else`, `#endif` and `#if` with a small constant-expression grammar
(integer literals, `defined(NAME)`, `!`, `&&`, `||`, comparisons, `+ - * / %` and parentheses);
a name not defined by any macro evaluates to 0 in `#if`. Inactive branches produce no output at
all, so they cannot leak tokens or trigger include errors. Macro definitions are scoped to one
preprocessor run and are visible across `#include` boundaries, which makes the standard
`#ifndef GUARD / #define GUARD / ... / #endif` include-guard idiom work.

## D-034 Modules: keep textual `#include`, document cyanlib prefixes (X3)
Cyan has no module system and gains none in this phase: `#include "..."` / `<...>` stays the only
way to compose source, resolved against the including file's directory, `-I` paths and the system
cyanlib directory. Adding an `import` keyword would need a new AST/IR form, per-file symbol tables
and link-time name mangling; the existing textual stage is backward compatible and, since X2,
supports both `#pragma once` (used by every cyanlib header) and `#ifndef`/`#define`/`#endif`
include guards. Because all included declarations share one global namespace, namespacing is a
convention, not a language feature: module-specific functions and constants carry a module prefix
(`vec_*`, `map_*`, `str_*` in containers; `fmt_*` in fmt; `io_*`, `fprint`, `assert` in io; `mem_*`
in mem), CamelCase names are reserved for struct types, and deliberately unprefixed C-compatible
shims (`strlen`, `strcmp`, `memcpy`, `atoi`, `calloc`, `realloc`) are the exception. New cyanlib
APIs must use a module prefix; collisions are compile errors and are not silently resolved. The
convention is written down in `docs/MODULES.md`.
