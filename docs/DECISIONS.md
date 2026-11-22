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
`// EXPECT-EXIT: N`, `// EXPECT-STDOUT: <line>` (repeatable, ordered), `// EXPECT-ERROR: <substring>`,
`// ARGS: ...`, `// STDIN: ...`, `// TARGETS: x86_64 riscv64` (default both). Files named `test_bad_*`
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
