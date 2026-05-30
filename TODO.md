# Sodium/Cyan — TODO

## Done ✅

- [x] **Spill code in register allocator** — store callee-save values to stack
      when caller-save registers are exhausted. Previously, values spanning >5
      calls on x86-64 got silently corrupted. Now uses fp-relative LOAD/STORE.
- [x] **Hex literal syntax** (`0xFF`, `0xAB`) — tokenizer-level `0x`/`0X` prefix,
      converts to decimal string for pipeline compatibility.
- [x] **`!` logical NOT operator** — parser + codegen. Emits `CMP_EQ %dst, %x, 0`
      in IR, no backend changes needed.

## Phase 5: File I/O & Runtime

- [ ] `fopen` / `fclose` builtins
- [ ] `fread` / `fwrite` builtins
- [ ] `fprintf` / `fscanf` builtins
- [ ] Text file reading/writing
- [ ] Argument to `_start` (argc, argv)
- [ ] Large-block free list for >2048 byte allocations
- [ ] Growable heap via `mmap` / `brk`
- [ ] True coalescing to reduce fragmentation

## Phase 6: String & Math Library

- [ ] `memcpy`, `memset`, `memcmp`
- [ ] String operations (`strlen`, `strcmp`, `strcat`, `strcpy`)
- [ ] `printf`-style formatting
- [ ] `sprintf` / `snprintf`
- [ ] Math helpers (`abs`, `min`, `max`, `clamp`)

## Phase 7: Language Completeness

- [ ] Pointer type annotations (`var p: int*`)
- [ ] Pointer arithmetic (`ptr + 1`, `ptr - 1`)
- [ ] Null pointer safety (check before deref)
- [ ] Passing structs by value to functions
- [ ] Struct return values
- [ ] Arrays of structs
- [ ] Nested structs
- [ ] Struct literal initializers (`{ .x = 10 }`)
- [ ] Multi-dimensional arrays
- [ ] Enum types
- [ ] Type aliases (`typedef`)

## Phase 8: Optimization

- [ ] Constant folding in IR
- [ ] Dead code elimination
- [ ] Peephole optimization pass
- [ ] Function inlining

## Phase 9: Tooling

- [ ] Error recovery (report multiple errors per compilation)
- [ ] Bounds checking (opt-in `--bounds` flag)
- [ ] Inline assembly syntax
- [ ] Package manager / standard library registry

## Phase 10: Bootstrapping

- [ ] Tokenizer written in Cyan
- [ ] Parser written in Cyan
- [ ] Code generator written in Cyan
- [ ] Self-hosted compiler can compile itself
- [ ] Drop the C++ bootstrap compiler

## Done ✅

### Phase 1: Polish for Distribution
- README with install instructions, language tour, examples, LSP/VS Code docs
- `install.sh` — one-command build + install
- Example programs in `examples/`
- BUGS.md, FEATURES.md reflecting current state
- For-loop variable scoping (`for var i` scoped to loop)
- Global/static array declarations
- Undefined function compile-time check

### Phase 2: Include Mechanism
- `#include` directive with search paths (`-I` flag)
- `#pragma once` guard mechanism
- `#error` directive
- Tests for includes, pragma once, missing includes
- LSP handles includes (preprocesses documents)

### Phase 3: Structs
- `struct` declaration and field access
- Struct variable declaration (`var p: Point;`)
- Field assignment and compound assignment
- Multiple struct types

### Phase 4: Pointers & Heap Allocation
- Address-of (`&`) and dereference (`*`)
- `malloc` / `free` builtins (SFL allocator)
- Assignment through pointer (`*ptr = expr`)
- Compound assignment through pointer (`*ptr += expr`)
- Pointer to pointer (double deref)
- Pointer to struct fields and globals

### Phase 4.5: IR & Multi-Architecture
- SSA-style intermediate representation
- Liveness analysis + linear scan register allocation
- x86-64 backend (NASM)
- RISC-V 64 backend (GAS)
- Dual-architecture test runner (203+ tests)
- SFL allocator in shared C runtime
- BSS memory pool (no syscalls)
