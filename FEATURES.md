# Cyan Language — Feature Status

## ✅ Implemented

### Core Language
| Feature | Status |
|---------|--------|
| Integer literals | ✅ |
| Arithmetic (`+`, `-`, `*`, `/`, `%`) | ✅ |
| Comparison (`<`, `>`, `<=`, `>=`, `==`, `!=`) | ✅ |
| Logical (`&&`, `\|\|`) | ✅ |
| Bitwise (`&`, `\|`, `^`, `~`, `<<`, `>>`) | ✅ |
| Logical NOT (`!`) | ✅ |
| Compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `\|=`, `^=`, `<<=`, `>>=`) | ✅ |
| Increment/decrement (`++x`, `x++`, `--x`, `x--`) | ✅ |
| Ternary conditional (`cond ? a : b`) | ✅ |
| Variable declarations (`var x = expr`) | ✅ |
| Type annotations (`var x: i32 = expr`) | ✅ |
| Integer types (`i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`) | ✅ |
| Type overflow/truncation on assignment and arithmetic | ✅ |
| Constant expressions (`const NAME = expr`) | ✅ |
| Arrays (declaration, indexing, assignment, literals) | ✅ |
| Functions (definition, parameters, return values) | ✅ |
| Forward function references (mutual recursion) | ✅ |
| Recursion | ✅ |
| `if` / `else if` / `else` | ✅ |
| `while` loops | ✅ |
| `do-while` loops | ✅ |
| `for` loops (init; cond; update) | ✅ |
| `break` / `continue` | ✅ |
| `switch` / `case` / `default` (fall-through) | ✅ |
| Global variables (`global var`) | ✅ |
| Global expression initializers | ✅ |
| Global array declarations (`global var arr[size]`) | ✅ |
| Static variables (`static var` inside functions) | ✅ |
| For-loop variable scoping (`for var i` scoped to loop) | ✅ |
| Undefined function call detection (compile-time error) | ✅ |
| Include mechanism (`#include` + `#pragma once`) | ✅ |
| `print()` builtin (integer to stdout) | ✅ |
| `print()` with string literals (unified dispatch) | ✅ |
| `print()` with C-style format strings (`%d`, `%s`, `%%`) | ✅ |
| `print()` with multiple arguments (comma-separated) | ✅ |
| `print()` with format string as literal + expressions/function calls in args | ✅ |
| `print()` with no arguments (just newline) | ✅ |
| `read()` builtin (integer from stdin) | ✅ |
| `read(buf)` builtin (line from stdin into array buffer, returns byte count) | ✅ |
| Mixed `read()` / `read(buf)` on same input stream | ✅ |
| `argc()` builtin (argument count) | ✅ |
| `argv()` builtin (argument vector) | ✅ |
| C-style `function main()` entry point (top-level statements are not allowed) | ✅ |
| `return` inside `main()` produces the exit code; void main → exit(0) | ✅ |
| Global initializers run before `main()` via `_start_globals` | ✅ |
| String literals (`"hello"`) | ✅ |
| Hex integer literals (`0xFF`) | ✅ |
| Struct declarations (`struct Name { var field; ... }`) | ✅ |
| Struct variable declarations (`var p: Point;`) | ✅ |
| Field access (`p.x`) | ✅ |
| Field assignment and compound assignment (`p.x += 5`, `p.x *= 2`) | ✅ |
| Address-of operator (`&var`, `&p.x`) | ✅ |
| Dereference operator (`*ptr`) | ✅ |
| Assignment through pointer (`*ptr = expr`) | ✅ |
| Compound assignment through pointer (`*ptr += expr`) | ✅ |
| Double dereference (`**ptr`) | ✅ |
| Pointer to struct fields | ✅ |
| Type-aware pointer arithmetic (`ptr + n` scales by `sizeof(*ptr)`) | ✅ |
| Pointer to global variables | ✅ |
| `malloc()` / `free()` builtins (SFL allocator) | ✅ |
| Empty blocks (`{}`) | ✅ |
| Unicode escape sequences in strings | ✅ |
| Packed stack allocation (byte-granularity for all types) | ✅ |

### Runtime
| Feature | Status |
|---------|--------|
| Large-block free list (first-fit, no leak on free) | ✅ |
| Shared `.bss` input buffer (`read()`/`read(buf)` share a 256-byte buffer with cursor) | ✅ |
| Shared `.rodata` string table (`%s` and string literals share one pool) | ✅ |

### Compiler Infrastructure
| Feature | Status |
|---------|--------|
| SSA-style intermediate representation | ✅ |
| Basic blocks and control flow graphs | ✅ |
| Liveness analysis | ✅ |
| Linear scan register allocation | ✅ |
| Virtual registers → physical register assignment | ✅ |
| x86-64 backend (NASM assembly) | ✅ |
| RISC-V 64 backend (GAS assembly) | ✅ |
| `--target riscv64` CLI flag | ✅ |
| Source locations in errors (file:line:col) | ✅ |
| Source code annotation (`^` caret pointing to error) | ✅ |
| `--print-ast` CLI flag | ✅ |
| `--emit-ir` CLI flag (dump IR before register allocation) | ✅ |
| Freestanding C runtime library | ✅ |
| Segregated Free List (SFL) heap allocator | ✅ |
| BSS memory pool (no syscalls needed) | ✅ |
| Separate compilation units (`.hpp` + `.cpp`) | ✅ |
| Chained arena allocator (no fixed size limit) | ✅ |
| LSP server (`cyan-lsp`) | ✅ |
| VS Code extension (syntax + theme + LSP) | ✅ |
| Go-to-definition | ✅ |
| Document symbols | ✅ |
| Hover information | ✅ |
| Completions | ✅ |
| Compile-failure test support | ✅ |
| Dual-architecture test runner | ✅ |
| Compile-failure tests (48 tests covering ~30 error categories) | ✅ |
| Install script (`install.sh`) | ✅ |
| RISC-V GP relaxation fix (`-Wl,--no-relax`) | ✅ |
| Multi-byte loads/stores with direction fix for packed stack offsets | ✅ |
| Large stack-frame support (RISC-V `addi` > 2047 → `li`+`add`) | ✅ |
| `lea` without hardcoded temp register (RISC-V, preserves live values) | ✅ |
| Spill code in register allocator (store/reload for values spanning many calls) | ✅ |

## 🔜 High Priority

| Feature | Notes |
|---------|-------|
| String operations | Concatenation, comparison, length |
| File I/O | `fopen`, `fread`, `fwrite`, `fprintf` |

## 🧭 Medium Priority

| Feature | Notes |
|---------|-------|
| True coalescing | Reduce fragmentation |
| Growable heap | Via `mmap` / `brk` |
| Passing structs by value to functions | |
| Struct return values | |
| Arrays of structs | |
| Nested structs | |
| Multi-dimensional arrays | |
| Error recovery | Report multiple errors instead of aborting |

## 🚀 Long-term

| Feature | Notes |
|---------|-------|
| Basic optimization | Constant folding, dead code elimination |
| Enum types | |
| Type aliases | `typedef` |
| Function pointers | |
| Inline assembly | |
| Bounds checking | Opt-in array bounds checks |
| Thread safety | For the allocator |
| WASM backend | |
| Self-hosting | Compiler written in Cyan |
