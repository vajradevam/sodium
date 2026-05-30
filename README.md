# Cyan Language Compiler — Sodium

**Sodium** is a compiler for the **Cyan** programming language — a small, clean,
compiled systems language targeting **x86-64 Linux** and **RISC-V 64**. Cyan is
designed to be simple enough to hold in your head while being expressive enough
for real programs.

## Quick Start

```bash
# Build from source
git clone https://github.com/vajradevam/sodium.git
cd sodium
cmake -B build && cmake --build build

# Compile and run a Cyan program (x86-64)
./build/sodium my_program.cyan
./sodium-out/out
echo $?   # exit code
```

**Or install system-wide:**
```bash
./install.sh
sodium my_program.cyan
```

## Language Tour

### Hello world

```cyan
return(42);
```

```bash
sodium hello.cyan && ./sodium-out/out && echo $?
# outputs: 42
```

### Variables and types

```cyan
var x = 5;
var y: i32 = 10;
var z: u8 = 255;
var name = "Cyan";

// All integer types:
// i8, i16, i32, i64, u8, u16, u32, u64
```

### Arithmetic and operators

```cyan
var sum   = x + y;
var diff  = x - y;
var prod  = x * y;
var quot  = x / y;
var rem   = x % y;

// Comparison
var eq  = x == y;
var neq = x != y;
var lt  = x < y;
var gt  = x > y;
var lte = x <= y;
var gte = x >= y;

// Logical
var both   = x > 0 && y < 100;
var either = x > 0 || y > 0;

// Bitwise
var band    = x & y;
var bor     = x | y;
var bxor    = x ^ y;
var shifted = x << 2;
var not     = ~x;

// Compound assignment
x += 5;   y *= 2;   z >>= 1;

// Increment / decrement (statement-level)
a++;   ++a;   a--;   --a;

// Ternary
var max = x > y ? x : y;
```

### Control flow

```cyan
// if / else if / else
if x > 0 {
    // ...
} else if x == 0 {
    // ...
} else {
    // ...
}

// while
while i < 10 { i++; }

// do-while
do { i--; } while i > 0;

// for
for var i = 0; i < 10; i++ { print(i); }

// switch (fall-through, no implicit break)
switch x {
    case 1 { print("one"); }
    case 2 { print("two"); }
    default { print("other"); }
}
```

### Arrays

```cyan
// Declare and initialize
var arr[] = [10, 20, 30, 40, 50];

// Access and assign
arr[2] = 99;
var third = arr[2];

// Size from const expression
const size = 10;
var buffer[size];
buffer[0] = 42;
```

### Functions

```cyan
function add(a, b) {
    return(a + b);
}

var result = add(3, 4);

// Recursion
function factorial(n) {
    if n <= 1 { return(1); }
    return(n * factorial(n - 1));
}
```

### Structs

```cyan
struct Point { var x; var y; var z; };

var p: Point;
p.x = 10;  p.y = 20;  p.z = 30;
p.x += 5;                // compound assignment on fields
var sum = p.x + p.y;     // field reads in expressions
```

### Pointers and heap allocation

```cyan
var x = 42;
var ptr = &x;
*ptr = 100;              // write through pointer
*ptr += 50;              // compound assign through pointer
var y = *ptr;            // read through pointer

// Heap allocation (SFL allocator)
var buf = malloc(256);
*buf = 123;
free(buf);

// Pointer to struct fields
var p: Point;
var px = &p.x;
*px = 42;
```

### Global and static variables

```cyan
global g_count = 0;       // initialized global
global g_buffer[256];     // zero-initialized (.bss)

function counter() {
    static calls = 0;     // persists across calls
    calls++;
    return(calls);
}
```

### Constants (compile-time evaluation)

```cyan
const SIZE = 100;
const MAX = SIZE * 2 + 1;
var arr[SIZE];
```

### Input / output

```cyan
print(42);               // print integer to stdout
var input = read();       // read decimal integer from stdin
```

## Both Architectures

Cyan compiles to **two backends** from the same source:

```bash
# x86-64 (native)
./build/sodium program.cyan
./sodium-out/out

# RISC-V 64 (cross-compile)
./build/sodium --target riscv64 program.cyan
qemu-riscv64 ./sodium-out/out
```

All language features work identically on both targets. The full test suite
(203+ tests) runs on both architectures with 0 failures.

## Building from Source

### Dependencies

- **C++20 compiler** (GCC 11+, Clang 14+)
- **CMake** 3.15+
- **NASM** (for x86-64 assembly)
- **GNU ld** (for x86-64 linking)
- **riscv64-elf-gcc** (for RISC-V cross-compilation — optional)
- **qemu-riscv64** (for running RISC-V binaries — optional)

### Build

```bash
cmake -B build && cmake --build build
```

Produces:
- **`build/sodium`** — the Cyan compiler
- **`build/cyan-lsp`** — the LSP language server

### Debug build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

### Runtime library

The runtime (`sodium-rt/`) includes CRT startup, I/O, and the heap allocator:

```bash
# Build runtime for x86-64
make -C sodium-rt

# Build runtime for RISC-V
make -C sodium-rt/riscv64
```

The runtime is linked automatically by the compiler.

## Compilation Pipeline

```
.cyan → Tokenizer → Parser → AST → IR Builder → 
  Liveness Analysis → Linear Scan Allocator → IR Rewriter → 
  Backend (x86-64 | RISC-V) → Assembly → Link → ELF
```

The compiler uses an **SSA-style intermediate representation** with virtual
registers, liveness analysis, and linear scan register allocation before
emitting target-specific assembly. This enables clean multi-architecture
support and lays the groundwork for optimization passes.

### Runtime library

A freestanding C runtime (`sodium-rt/allocator.c`) provides:

- **Segregated Free List (SFL) allocator** — 15 size classes (16–2048 bytes),
  O(1) alloc/free, 2 MB BSS heap, bump-pointer fallback for large blocks
- CRT startup (`_start`) and exit
- `print()` / `read()` via raw syscalls

## Project Structure

```
├── src/
│   ├── main.cpp                 Entry point, CLI, linking
│   ├── tokenization.hpp/cpp     Lexer
│   ├── parser.hpp/cpp           Recursive-descent parser → AST
│   ├── generation.hpp/cpp       Code generation (AST → IR → target)
│   ├── preprocessor.hpp/cpp     #include, #pragma once
│   ├── ast_printer.hpp/cpp      Debug AST dump
│   ├── arena.hpp                Bump-pointer arena allocator
│   ├── json.hpp                 Minimal JSON library
│   ├── lsp.hpp/cpp              LSP server (diagnostics, completions, hover, goto-def)
│   ├── lsp_main.cpp             LSP entry point
│   ├── ir/                      Intermediate representation
│   │   ├── builder.hpp          IR builder
│   │   ├── module.hpp           IR module
│   │   ├── function.hpp         IR functions
│   │   ├── instruction.hpp      IR instruction format
│   │   ├── value.hpp            Operand types (vregs, immediates, labels)
│   │   ├── opcodes.hpp          All IR opcodes
│   │   ├── liveness.hpp         Liveness analysis
│   │   ├── linear_scan.hpp      Linear scan register allocation
│   │   ├── rewriter.hpp         Assign physical registers to IR
│   │   ├── emitter.hpp          IR → assembly emission
│   │   ├── dump.hpp/cpp         IR debug dump
│   │   ├── block.hpp            Basic blocks
│   │   └── target_regs.hpp      Target register file descriptions
│   └── backend/
│       ├── interface.hpp        Abstract backend interface
│       ├── x86_64/backend.hpp/cpp    x86-64 codegen
│       ├── riscv64/backend.hpp/cpp   RISC-V 64 codegen
│       └── null_backend.hpp     No-op backend for testing
├── sodium-rt/                   Freestanding runtime library
│   ├── allocator.c              SFL allocator (shared C source)
│   ├── x86_64/                  x86-64 runtime (NASM assembly)
│   │   ├── crt0.asm             CRT entry point
│   │   ├── exit.asm             _sodium_exit
│   │   ├── print.asm            _sodium_print_int
│   │   ├── read.asm             _sodium_read_int
│   │   └── Makefile
│   └── riscv64/                 RISC-V 64 runtime (GAS assembly)
│       ├── crt0.asm
│       ├── exit.asm
│       ├── print.asm
│       ├── read.asm
│       └── Makefile
├── tests/
│   ├── unit/                    100+ unit tests (.cyan)
│   ├── integration/             Integration tests
│   └── include/                 Include mechanism tests
├── examples/                    Example programs
├── vscode-extension/            VS Code extension (syntax + LSP)
├── run_tests.sh                 Test runner (dual-architecture)
├── install.sh                   Install script
└── CMakeLists.txt
```

## Testing

The test suite runs on **both architectures**:

```bash
# Run all tests (x86-64 + RISC-V if qemu is available)
bash run_tests.sh
```

```text
Results: 203 passed, 0 failed
```

Each test file compiles to both backends and the resulting binary's exit
code is checked against an expected value. Compile-failure tests verify
that invalid programs are correctly rejected.

## LSP Server

The `cyan-lsp` server implements the Language Server Protocol over stdin/stdout:

- **Diagnostics** — compile errors as you type
- **Completions** — keywords and identifiers
- **Hover** — token kind and declaration details
- **Go-to-definition** — jumps to declaration
- **Document symbols** — functions, variables, constants, parameters

### VS Code Extension

The `vscode-extension/` directory provides:
- Syntax highlighting (TextMate grammar for `.cyan`)
- Cyan Dark theme (teal/cyan accents)
- LSP integration (errors, completions, hover, goto-def, document symbols)

Install with:
```bash
./install.sh --vscode
```

## Current Status

- **203+ tests passing** across both x86-64 and RISC-V
- **Full IR pipeline** with SSA virtual registers and linear scan allocation
- **Dual backend**: x86-64 (NASM) and RISC-V 64 (GAS)
- **Complete runtime**: SFL allocator, CRT, I/O
- **LSP server**: fully functional with VS Code extension
- **Platform**: Linux (x86-64 native + RISC-V cross-compile via qemu)

## License

MIT
