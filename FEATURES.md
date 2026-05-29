# Cyan Language — Feature Roadmap

## ✅ Implemented

### Core Language
| Feature | Status |
|---------|--------|
| Integer literals | ✅ |
| Arithmetic (`+`, `-`, `*`, `/`, `%`) | ✅ |
| Comparison (`<`, `>`, `<=`, `>=`, `==`, `!=`) | ✅ |
| Logical (`&&`, `\|\|`) | ✅ |
| Bitwise (`&`, `\|`, `^`, `~`, `<<`, `>>`) | ✅ |
| Compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `\|=`, `^=`, `<<=`, `>>=`) | ✅ |
| Increment/decrement (`++x`, `x++`, `--x`, `x--`) | ✅ |
| Ternary conditional (`cond ? a : b`) | ✅ |
| Variable declarations (`var x = expr`) | ✅ |
| Type annotations (`var x: i32 = expr`) | ✅ |
| Integer types (`i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`) | ✅ |
| Constant expressions (`const NAME = expr`) | ✅ |
| Arrays (declaration, indexing, assignment, literals) | ✅ |
| Functions (definition, parameters, return values) | ✅ |
| Recursion | ✅ |
| `if` / `else if` / `else` | ✅ |
| `while` loops | ✅ |
| `do-while` loops | ✅ |
| `for` loops (init; cond; update) | ✅ |
| `break` / `continue` | ✅ |
| `switch` / `case` / `default` | ✅ |
| Global variables (`global var`) | ✅ |
| Global array declarations (`global var arr[size]`) | ✅ |
| Static variables (`static var` inside functions) | ✅ |
| For-loop variable scoping (`for var i` scoped to loop) | ✅ |
| Undefined function call detection (compile-time error) | ✅ |
| Include mechanism (`#include` + `#pragma once`) | ✅ |
| `print()` builtin (integer to stdout) | ✅ |
| `read()` builtin (integer from stdin) | ✅ |
| Top-level statements (outside functions) | ✅ |
| String literals (`"hello"`) | ✅ |
| Struct declarations (`struct Name { var field; ... }`) | ✅ |
| Struct variable declarations (`var p: Point;`) | ✅ |
| Field access (`p.x`) | ✅ |
| Field assignment and compound assignment (`p.x += 5`, `p.x *= 2`) | ✅ |

### Tooling & Infrastructure
| Feature | Status |
|---------|--------|
| Source locations in errors (file:line:col) | ✅ |
| Source code annotation (`^` caret pointing to error) | ✅ |
| `--print-ast` CLI flag | ✅ |
| Separate compilation units (`.hpp` + `.cpp`) | ✅ |
| Chained arena allocator (no fixed size limit) | ✅ |
| NASM x86-64 code generation | ✅ |
| Linux ELF binary output | ✅ |
| LSP server (`cyan-lsp`) | ✅ |
| VS Code extension (syntax + theme + LSP) | ✅ |
| Go-to-definition | ✅ |
| Document symbols | ✅ |
| Hover information | ✅ |
| Completions | ✅ |
| Compile-failure test support | ✅ |
| Professional error messages | ✅ |
| Install script (`install.sh`) | ✅ |

## 🔜 High Priority (next)

| Feature | Notes |
|---------|-------|
| String operations | Concatenation, comparison, length |

## 🧭 Medium Priority

| Feature | Notes |
|---------|-------|
| Pointers | `*` dereference, `&` address-of |
| Heap allocation | `malloc` / `free` builtins |
| File I/O | `fopen`, `fread`, `fwrite`, `fprintf` |
| Runtime library | `memcpy`, `strlen`, helpers |
| Multi-dimensional arrays | `arr[x][y]` |

## 🚀 Long-term

| Feature | Notes |
|---------|-------|
| Error recovery | Report multiple errors instead of aborting |
| Basic optimization | Constant folding, dead code elimination |
| Function pointers | |
| Enum types | |
| Type aliases | `typedef` |
| Inline assembly | |
| Struct methods / methods on types | |
| Bounds checking | Opt-in array bounds checks |
| Self-hosting | Compiler written in Cyan itself |

## Infrastructure Ideas

- IR / intermediate representation (enables optimization + alternative backends)
- WASM backend
- Better test framework (parameterized tests, expected-failure tests more ergonomic)
- Package manager / standard library
