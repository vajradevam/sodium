# Cyan Language — Feature Status

## ✅ Implemented

### Core Language
| Feature | Details |
|---------|---------|
| C-style `function main()` entry point | Required; top-level statements are not allowed |
| `return` in `main()` sets exit code | `return(0)` → `exit(0)`; void `main` → `exit(0)` |
| Global initializers before `main` | Expression-initialized globals run in `_start_globals` |
| Integer literals | Decimal and hex (`0xFF`) |
| String literals | `"hello"`, with Unicode escape sequences |
| Hex integer literals | `0xFF`, `0xAB` |
| Comments | `//` line comments |
| Preprocessor | `#include`, `#pragma once`, `#error` |

### Types & Variables
| Feature | Details |
|---------|---------|
| Integer types | `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64` |
| Type annotations on variables | `var x: i32 = 10;` |
| Type overflow/truncation | On assignment and arithmetic |
| Variable declarations | `var x = expr;` or `var x: type = expr;` |
| Constant expressions | `const NAME = expr;` (compile-time evaluated) |
| Array declarations | `var arr[size];` or `var arr[] = [1, 2, 3];` |
| Array indexing & assignment | `arr[i] = val;` |
| Array literals | `[10, 20, 30]` |
| Global variables | `global var name = expr;` |
| Global arrays | `global var arr[size];` (.bss zero-initialized) |
| Global expression initializers | `global var x = (a + b) * c;` (via `_start_globals`) |
| Static variables (inside functions) | `static var x = 0;` persists across calls |
| Struct declarations | `struct Point { var x; var y; };` |
| Struct variable declarations | `var p: Point;` |
| Field access | `p.x` |
| Field assignment & compound | `p.x = 5;` `p.x += 3;` `p.x *= 2;` |

### Expressions & Operators
| Feature | Details |
|---------|---------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Comparison | `<`, `>`, `<=`, `>=`, `==`, `!=` |
| Logical | `&&`, `\|\|`, `!` (logical NOT) |
| Bitwise | `&`, `\|`, `^`, `~`, `<<`, `>>` |
| Compound assignment | `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `\|=`, `^=`, `<<=`, `>>=` |
| Increment/decrement | `++x`, `x++`, `--x`, `x--` (statement-level) |
| Ternary conditional | `cond ? a : b` |
| Address-of | `&var`, `&p.x`, `&arr[i]` |
| Dereference | `*ptr` |
| Double dereference | `**ptr` |

### Control Flow
| Feature | Details |
|---------|---------|
| `if` / `else if` / `else` | Standard conditional |
| `while` loops | `while cond { body }` |
| `do-while` loops | `do { body } while cond;` |
| `for` loops | `for init; cond; update { body }` |
| `for var` scoping | Loop variable scoped to loop body |
| `break` / `continue` | Loop control |
| `switch` / `case` / `default` | Fall-through (no implicit break) |
| Empty blocks | `{}` as a no-op statement |

### Functions
| Feature | Details |
|---------|---------|
| Function definition | `function name(params) { body }` |
| Parameters | Any number of parameters (i64) |
| Return values | `return(expr);` or `return;` for void |
| Recursion | Direct and mutual |
| Forward references | Functions can call functions defined later |
| Undefined function detection | Compile-time error on call to unknown function |

### Pointers & Memory
| Feature | Details |
|---------|---------|
| Pointer arithmetic | `ptr + n` scales by `sizeof(*ptr)`; `ptr - n` |
| Pointer difference | `(p + n) - p` → element count |
| Assignment through pointer | `*ptr = expr;` |
| Compound assignment through pointer | `*ptr += expr;` |
| Pointer to struct fields | `var px = &p.x;` |
| Pointer to global variables | `&g_var` |
| `malloc(size)` builtin | Returns pointer to heap memory |
| `free(ptr)` builtin | Frees allocated memory |

### I/O & Builtins
| Feature | Details |
|---------|---------|
| `print()` unified | Handles integers, string literals, format strings |
| `print()` format strings | `%d` (integer), `%s` (string), `%%` (literal `%`) |
| `print()` multiple args | `print("x=%d y=%d", x, y)` |
| `print()` expressions in args | `print("sum=%d", a + b)` |
| `print()` function calls in args | `print("val=%d", double(x))` |
| `print()` no arguments | Just prints a newline |
| `read()` — read integer | Reads decimal integer from stdin |
| `read(buf)` — read line | Reads line into array buffer, returns byte count |
| Shared input buffer | `read()` and `read(buf)` share one `.bss` buffer with cursor |
| `argc()` — argument count | Number of command-line arguments |
| `argv(i)` — argument vector | Returns pointer to i-th argument string |

### Compiler Infrastructure
| Feature | Details |
|---------|---------|
| SSA-style intermediate representation | Virtual registers, basic blocks |
| Liveness analysis | Per-block live-in/live-out |
| Linear scan register allocation | Uses IR liveness intervals |
| Virtual → physical register assignment | IR rewriter |
| x86-64 backend | NASM assembly output |
| RISC-V 64 backend | GAS assembly output (rv64gc) |
| `--target riscv64` CLI flag | Cross-compilation target selection |
| `-o <file>` output path | Custom binary output |
| `-S` emit assembly only | POSIX convention |
| `--print-ast` | Dump AST and exit |
| `--emit-ir` | Dump IR before register allocation |
| `--no-alloc` | Bypass allocator (all values on stack) |
| `-I <dir>` include path | Add directory to include search |
| Source locations in errors | `file:line:col` format |
| Source code annotation | `^` caret pointing to error |
| Separate compilation units | `.hpp` + `.cpp` modules |
| Chained arena allocator | No fixed size limit |
| Spill code in allocator | Store/reload for values spanning many calls |
| Packed stack allocation | Byte-granularity for all integer types |
| Multi-byte loads/stores | With direction fix for packed stack offsets |

### Runtime
| Feature | Details |
|---------|---------|
| Freestanding C runtime | No libc dependency |
| Segregated Free List (SFL) allocator | 15 size classes, O(1) alloc/free |
| 2 MB BSS heap | Bump-pointer fallback for large blocks |
| Large-block free list | First-fit, no leak on free |
| Shared `.bss` input buffer | 256-byte buffer for `read()`/`read(buf)` |
| Shared `.rodata` string table | String literals and `%s` pool |
| x86-64 CRT + syscalls | `_start`, `_sodium_exit` via `write`/`exit` syscalls |
| RISC-V CRT + syscalls | Same, via RISC-V Linux syscalls |
| Growable heap (future) | Via `mmap` / `brk` (not yet implemented) |

### Tooling & Testing
| Feature | Details |
|---------|---------|
| LSP server (`cyan-lsp`) | Language Server Protocol over stdin/stdout |
| Diagnostics | Compile errors as you type |
| Completions | Keywords and identifiers |
| Hover information | Token kind and declaration details |
| Go-to-definition | Jumps to declaration |
| Document symbols | Functions, variables, constants, parameters |
| VS Code extension | Syntax highlighting + Cyan Dark theme + LSP |
| Dual-architecture test runner | 302 tests on both x86-64 and RISC-V |
| Compile-failure tests | 48 tests covering ~30 error categories |
| Install script (`install.sh`) | One-command build + install + VS Code extension |

### RISC-V Specific Fixes
| Feature | Details |
|---------|---------|
| GP relaxation fix | `-Wl,--no-relax` to prevent GP-relative crashes |
| Large stack-frame support | `addi` > 2047 → `li` + `add` |
| `lea` without hardcoded temp | Computes address directly in destination register |
| Stack probe on function entry | Handles frames > 2048 bytes |

## 🔜 High Priority

| Feature | Notes |
|---------|-------|
| String operations | `strlen`, `strcmp`, `strcat`, `strcpy` |
| File I/O | `fopen`, `fread`, `fwrite`, `fclose` |
| Memory functions | `memcpy`, `memset`, `memcmp` |

## 🧭 Medium Priority

| Feature | Notes |
|---------|-------|
| Growable heap | Via `mmap` / `brk` |
| True coalescing | Reduce fragmentation |
| Passing structs by value | |
| Struct return values | |
| Arrays of structs | |
| Nested structs | |
| Struct literal initializers | `{ .x = 10 }` |
| Multi-dimensional arrays | |
| Pointer type annotations | `var p: int*` |
| Null pointer safety | Check before deref |
| Error recovery | Report multiple errors instead of aborting |

## 🚀 Long-term

| Feature | Notes |
|---------|-------|
| Constant folding | In IR |
| Dead code elimination | |
| Peephole optimization | |
| Function inlining | |
| Enum types | |
| Type aliases | `typedef` |
| Function pointers | |
| Inline assembly | |
| Bounds checking | Opt-in `--bounds` flag |
| Thread safety | For the allocator |
| WASM backend | |
| Self-hosting | Compiler written in Cyan |
