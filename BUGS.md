# Sodium / Cyan Compiler — Bug & Issue Tracker

> **Note:** The majority of early bugs have been fixed. This file tracks
> remaining issues and known limitations.

## Known Issues

### 1. No string operations

String literals can be printed but cannot be concatenated, compared,
indexed, or measured for length. There is no runtime string library.

### 2. `read()` returns 0 at EOF, cannot distinguish

The `read()` builtin returns a single integer from stdin. If stdin is
empty or at EOF, it returns 0 — but 0 is also a valid input value.

**Suggested fix:** Return a special sentinel (e.g., `-9223372036854775808`
or switch to a result-struct pattern).

### 3. Source code annotation off for `#line`-adjusted locations

When a `#line` directive adjusts the tokenizer's line numbers (e.g., for
included files), the caret pointer from `print_code_context()` points to
the wrong line in the expanded source. The error message's file:line:col
is correct, but the inline code preview shows the wrong source line.

### 4. Struct limitations

Structs can be declared, variables can be typed with struct types (`var p: Point;`),
fields can be read and assigned. **Not yet supported:**

- Passing structs to functions by value (all fields pushed as individual args)
- Nested structs (struct fields referencing other struct types)
- Struct return values from functions
- Arrays of structs
- Struct literal initializers (`{ .x = 10 }`)
- Pointer/reference semantics

### 5. Pointer limitations

Pointers are supported via `&` and `*`. **Not yet supported:**

- Pointer arithmetic (`ptr + 1` to advance by 8 bytes)
- Null pointer safety (dereferencing null will segfault)
- Pointer type annotations (`var p: int*` syntax — pointers are untyped)
- Array-to-pointer decay
- `free()` is a no-op (bump allocator cannot reclaim memory)

## Resolved

| # | Issue | Fixed In |
|---|-------|----------|
| 1 | `const` members blocking move semantics in Tokenizer | `5c6d9c4` |
| 2 | `system()` calls without error checking | `3fd1dc9` |
| 3 | Array indexing correctness (debunked) | N/A |
| 4 | For-loop update parser incomplete | `3905a85` |
| 5 | Global variables in for-update | `45b816e` |
| 6 | Static variable initializers lost | `c87989e` |
| 7 | Dead exit syscall after top-level return | `0222519` |
| 8 | Switch reusing m_loop_stack | `6bac32d` |
| 9 | Array assignment non-literal corruption | `11276f7` |
| 10 | Profane error messages | `f7a05d5` |
| 11 | No source locations in errors | `a6f0a45` |
| 12 | Arena allocator bottleneck | `d118d19` |
| 13 | String literals in writable .data | `5eeae7e` |
| 14 | Duplicated function epilogue | `1a53d68` |
| — | For-loop variable scope leak | `e8ff257` |
| — | Global/static array declarations | `e8ff257` |
| — | Undefined function compile-time check | `e8ff257` |
| — | Include mechanism (`#include`, `#pragma once`, `-I`) | (Phase 2) |
| — | Struct declarations and field access | (Phase 3) |
| — | Pointers (`&`, `*`), `malloc`/`free` builtins | (Phase 4) |
