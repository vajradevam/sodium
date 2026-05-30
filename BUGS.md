# Sodium / Cyan Compiler — Bug & Issue Tracker

## Known Issues

### 1. Register allocator lacks spill code

The linear scan register allocator assigns physical registers to virtual
registers, but when registers are exhausted, it does **not** spill values
to the stack. Values that span across more than ~5 function calls on x86-64
(or ~11 on RISC-V) may be incorrectly assigned to caller-save registers
that get silently clobbered.

**Workaround:** Keep call-spanning live ranges short.

**Fix:** Implement spill code — store callee-save values to the stack when
registers are exhausted.

### 2. No string operations

String literals can be printed but cannot be concatenated, compared,
indexed, or measured for length. There is no runtime string library.

### 3. `read()` returns 0 at EOF, cannot distinguish

The `read()` builtin returns a single integer from stdin. If stdin is
empty or at EOF, it returns 0 — but 0 is also a valid input value.

### 4. Source code annotation off for `#line`-adjusted locations

When a `#line` directive adjusts the tokenizer's line numbers (for included
files), the caret pointer from `print_code_context()` points to the wrong
line in the expanded source. The error message's file:line:col is correct,
but the inline code preview shows the wrong source line.

### 5. Large blocks leak on free

The Segregated Free List allocator covers size classes up to 2048 bytes.
Allocations larger than 2048 bytes fall through to the bump pointer and
are leaked on `free()`. Acceptable for compiler workloads where large
allocations (e.g., source text buffers) are typically permanent.

### 6. No `!` logical NOT operator

The `!` operator for boolean NOT is not parsed. Use `x == 0` instead.

### 7. No hex literal syntax

Literal values must be decimal. `0xFF`, `0xAB`, etc. are not recognized.

### 8. Struct limitations

- Structs cannot be passed to functions by value (all fields pushed as
  individual args)
- Nested structs not yet supported
- Struct return values from functions not yet supported
- Arrays of structs not yet supported
- Struct literal initializers (`{ .x = 10 }`) not yet supported

### 9. Pointer limitations

- Pointer arithmetic (`ptr + 1`) not supported
- No null pointer safety (dereferencing null segfaults)
- No pointer type annotations (`var p: int*`)
- No array-to-pointer decay

### 10. Single-threaded

The allocator and runtime assume single-threaded execution. No locks or atomic
operations are used.

## Resolved

| Issue | Fixed In |
|-------|----------|
| Const members blocking move semantics in Tokenizer | `5c6d9c4` |
| `system()` calls without error checking | `3fd1dc9` |
| For-loop update parser incomplete | `3905a85` |
| Global variables in for-update | `45b816e` |
| Static variable initializers lost | `c87989e` |
| Dead exit syscall after top-level return | `0222519` |
| Switch reusing `m_loop_stack` | `6bac32d` |
| Array assignment non-literal corruption | `11276f7` |
| No source locations in errors | `a6f0a45` |
| Arena allocator bottleneck | `d118d19` |
| String literals in writable `.data` | `5eeae7e` |
| Duplicated function epilogue | `1a53d68` |
| No-op `free()` (replaced with SFL allocator) | `ab04088` |
| RISC-V GP relaxation segfault | `ab04088` |
| `_start_globals` entry point crash (global inits) | `0f32eea` |
| Stack argument push order (calling convention) | `0f32eea` |
| Register argument clobbering (calling convention) | `0f32eea` |
| Global compound assignment LHS/RHS swap | `0f32eea` |
| RISC-V u32 zero-extension was no-op | `54a3a89` |
| RISC-V `andi` with large immediate (>12-bit) | `54a3a89` |
| BSS array globals used NASM-only `resq` syntax | `54a3a89` |
