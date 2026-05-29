# Sodium/Cyan — TODO

## Phase 1: Polish for Distribution ✅

- [x] **README.md** — install instructions, language tour, examples, LSP/VS Code docs
- [x] **install.sh** — one-command build + install of `sodium` and `cyan-lsp`
- [x] **Example programs** — showcase `.cyan` files in an `examples/` directory
- [x] **Update BUGS.md** — reflect current state; note already-fixed items
- [x] **Update FEATURES.md** — reflect what's actually implemented
- [x] **For-loop variable scoping** — `for (var i = …)` scopes `i` to the loop only
- [x] **Global/static array declarations** — `global var arr[size]` syntax
- [x] **Undefined function check** — clean compile error instead of linker crash

## Phase 2: Include Mechanism ☐

- [ ] `#include` directive — multi-file compilation
- [ ] Include search paths (current dir + system include dir)
- [ ] Guard mechanism (or `#pragma once`)
- [ ] Tests for transitive includes, missing includes, circular includes

## Phase 3: Structs / Compound Types ☐

- [ ] `struct` declaration syntax
- [ ] Field access (`obj.field`)
- [ ] Struct initialization
- [ ] Passing structs to functions (by value? by reference?)
- [ ] Nested structs
- [ ] Tests

## Phase 4: Pointers & Heap Allocation ☐

- [ ] Pointer types (`int*`, `char*`)
- [ ] Address-of operator (`&`)
- [ ] Dereference operator (`*`)
- [ ] `malloc` / `free` builtins
- [ ] Pointer arithmetic
- [ ] Null pointer safety

## Phase 5: File I/O ☐

- [ ] `fopen` / `fclose` builtins
- [ ] `fread` / `fwrite` builtins
- [ ] `fprintf` / `fscanf` builtins
- [ ] Text file reading/writing
- [ ] Argument to `_start` (argc, argv)

## Phase 6: Runtime Library ☐

- [ ] `memcpy`, `memset`, `memcmp`
- [ ] String operations (`strlen`, `strcmp`, `strcat`)
- [ ] `printf`-style formatting
- [ ] `sprintf` / `snprintf`
- [ ] Math helpers (`abs`, `min`, `max`, `clamp`)

## Phase 7: Bootstrapping ☐

- [ ] Tokenizer written in Cyan
- [ ] Parser written in Cyan
- [ ] Code generator written in Cyan
- [ ] Self-hosted compiler can compile itself
- [ ] Drop the C++ bootstrap compiler

## Phase 8: Advanced Features ☐

- [ ] Error recovery (report multiple errors)
- [ ] Basic optimization (constant folding, dead code elimination)
- [ ] Multi-dimensional arrays
- [ ] Enum types
- [ ] Type aliases (`typedef`)
- [ ] Function pointers
- [ ] Inline assembly
- [ ] Bounds checking (opt-in)
