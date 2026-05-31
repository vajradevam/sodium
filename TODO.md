# Sodium/Cyan — Development Roadmap

## ✅ Done

### Language
- [x] C-style `function main()` entry point (top-level statements not allowed)
- [x] Integer types (`i8`–`u64`), type annotations, overflow/truncation
- [x] Arrays (declaration, indexing, assignment, literals, const sizes)
- [x] Functions (definition, parameters, return values, recursion, forward refs)
- [x] Structs (declaration, field access, assignment, compound assignment)
- [x] Pointers (`&`, `*`, `**`, pointer arithmetic with type-aware scaling, pointer diff)
- [x] `malloc`/`free` builtins (SFL allocator)
- [x] Control flow: `if`/`else`, `while`, `do-while`, `for`, `switch`/`case`, `break`/`continue`
- [x] Operators: arithmetic, comparison, logical, bitwise, compound assignment, ++/--, ternary
- [x] Global variables (scalar, array, expression initializers)
- [x] Static variables (inside functions)
- [x] Constants (`const`, compile-time evaluation)
- [x] Hex literals (`0xFF`), logical NOT (`!`)
- [x] String literals with Unicode escape sequences
- [x] Empty blocks (`{}`)

### I/O & Builtins
- [x] Unified `print` with C-style format strings (`%d`, `%s`, `%%`)
- [x] `print()` with multiple comma-separated arguments
- [x] `print()` with expressions/function calls in args
- [x] `print()` with no arguments (just newline)
- [x] `read()` — read integer from stdin
- [x] `read(buf)` — read line into array buffer, returns byte count
- [x] Shared input buffer (correct mixed `read()`/`read(buf)`)
- [x] `argc()` / `argv()` builtins

### Compiler Infrastructure
- [x] Tokenizer → Parser → AST → IR → Backend
- [x] SSA-style IR with basic blocks and control flow
- [x] Liveness analysis + linear scan register allocation
- [x] x86-64 backend (NASM assembly)
- [x] RISC-V 64 backend (GAS assembly)
- [x] Dual-architecture test runner (302 tests passing)
- [x] 48 compile-failure tests (~30 error categories)
- [x] Source locations (`file:line:col`) with caret annotations
- [x] CLI: `-o`, `-S`, `--target`, `--print-ast`, `--emit-ir`, `--no-alloc`, `-I`
- [x] Include mechanism (`#include`, `#pragma once`, `#error`)
- [x] Chained arena allocator (no fixed size limit)
- [x] Packed stack allocation (byte-granularity)
- [x] Spill code in register allocator
- [x] Freestanding C runtime (SFL allocator, CRT, I/O via syscalls)
- [x] LSP server (`cyan-lsp`) — diagnostics, completions, hover, goto-def, document symbols
- [x] VS Code extension (syntax highlighting + Cyan Dark theme + LSP)
- [x] Install script (`install.sh`)

### RISC-V Specific
- [x] GP relaxation fix (`-Wl,--no-relax`)
- [x] Large stack-frame support (`addi` > 2047 → `li` + `add`)
- [x] `lea` without hardcoded temp register
- [x] Stack probe on function entry (frames > 2048 bytes)

## 📋 Next Up

### Phase 1: String & Memory Operations
- [ ] `strlen`, `strcmp`, `strcat`, `strcpy` builtins
- [ ] `memcpy`, `memset`, `memcmp` builtins
- [ ] String buffer manipulation utilities

### Phase 2: File I/O
- [ ] `fopen` / `fclose` builtins
- [ ] `fread` / `fwrite` builtins
- [ ] `fprintf` / `fscanf` builtins
- [ ] Text file reading/writing examples

### Phase 3: Heap & Allocator
- [ ] Growable heap via `mmap` / `brk`
- [ ] True coalescing to reduce fragmentation

### Phase 4: Language Enhancements
- [ ] Passing structs by value to functions
- [ ] Struct return values
- [ ] Arrays of structs
- [ ] Nested structs
- [ ] Struct literal initializers (`{ .x = 10 }`)
- [ ] Multi-dimensional arrays
- [ ] Pointer type annotations (`var p: int*`)
- [ ] Null pointer safety (check before deref)
- [ ] Enum types
- [ ] Type aliases (`typedef`)
- [ ] Function pointers
- [ ] Error recovery (report multiple errors)

### Phase 5: Optimization
- [ ] Constant folding in IR
- [ ] Dead code elimination
- [ ] Peephole optimization pass
- [ ] Function inlining

### Phase 6: Tooling & Portability
- [ ] Inline assembly syntax
- [ ] Bounds checking (opt-in `--bounds` flag)
- [ ] WASM backend
- [ ] Thread safety (for the allocator)

### Phase 7: Self-hosting
- [ ] Tokenizer written in Cyan
- [ ] Parser written in Cyan
- [ ] Code generator written in Cyan
- [ ] Self-hosted compiler can compile itself
- [ ] Drop the C++ bootstrap compiler
