# Cyan Language Compiler

**Sodium** is a compiler for the **Cyan** programming language — a small, clean,
compiled language targeting x86-64 Linux. Cyan is designed to be simple enough
to hold in your head while being expressive enough for real programs.

## Quick Start

```bash
# Build from source
git clone https://github.com/vajradevam/sodium.git
cd sodium
cmake -B build && cmake --build build

# Compile and run a Cyan program
./build/sodium my_program.cyan
./out
echo $?   # exit code
```

**Or install system-wide:**
```bash
./install.sh
sodium my_program.cyan
```

## Language Tour

### The basics

```cyan
// Hello world via exit code
return(42);
```

```bash
sodium hello.cyan && ./out && echo $?
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
var sum = x + y;
var diff = x - y;
var product = x * y;
var quotient = x / y;
var remainder = x % y;

// Comparison
var eq = x == y;
var neq = x != y;
var lt = x < y;
var gt = x > y;
var lte = x <= y;
var gte = x >= y;

// Logical
var both = x > 0 && y < 100;
var either = x > 0 || y > 0;

// Bitwise
var band = x & y;
var bor = x | y;
var bxor = x ^ y;
var shifted = x << 2;
var unary_not = ~x;

// Compound assignment
x += 5;
y *= 2;
z >>= 1;

// Increment / decrement
var a = 0;
a++;
++a;
a--;
--a;

// Ternary
var max = x > y ? x : y;
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

### Control flow

```cyan
// if / else if / else
if x > 0 {
    print("positive");
} else if x == 0 {
    print("zero");
} else {
    print("negative");
}

// while
while i < 10 {
    i++;
}

// do-while
do {
    i--;
} while i > 0;

// for
for var i = 0; i < 10; i++ {
    print(i);
}

// switch
switch x {
    case 1 { print("one"); }
    case 2 { print("two"); }
    default { print("other"); }
}
```

### Loops: break and continue

```cyan
for var i = 0; i < 100; i++ {
    if i == 5 { break; }
    if i % 2 == 0 { continue; }
    print(i);
}
```

### Functions

```cyan
function add(a, b) {
    return(a + b);
}

var result = add(3, 4);
return(result);
```

Functions can be recursive:

```cyan
function factorial(n) {
    if n <= 1 {
        return(1);
    }
    return(n * factorial(n - 1));
}
```

### Global and static variables

```cyan
global x = 42;         // initialized global
global g_buf[256];     // zero-initialized (.bss)

function counter() {
    static calls = 0;  // persists across calls
    calls++;
    return(calls);
}
```

### Constants (compile-time evaluation)

```cyan
const size = 100;
const max = size * 2 + 1;
const name = size > 50 ? "big" : "small";
var arr[size];
```

### Input

```cyan
var input = read();
```

Reads one decimal integer from stdin. Returns 0 at EOF.

## Building from Source

### Dependencies

- **C++20 compiler** (GCC 11+, Clang 14+, or MSVC 2022+)
- **CMake** 3.15+
- **NASM** (Netwide Assembler)
- **ld** (GNU linker, part of binutils)
- **Node.js** (for VS Code extension — optional)

### Build

```bash
cmake -B build && cmake --build build
```

Produces two executables in `build/`:
- **`sodium`** — the Cyan compiler
- **`cyan-lsp`** — the LSP language server

### Build for development

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

### Install

```bash
./install.sh
```

Installs `sodium` and `cyan-lsp` to `/usr/local/bin`.

## Usage

```text
sodium [options] <file.cyan>

Options:
  --print-ast    Print the AST tree before code generation
  --help         Show this help

Output: out.asm (assembly), out.o (object), out (executable)
```

### Compile-failure tests

The test runner also supports tests that are expected to fail at compile time.
Add your test file to the `COMPILE_FAIL_TESTS` array in `run_tests.sh`.

## LSP Server

The `cyan-lsp` server implements the Language Server Protocol over stdin/stdout.
It provides:

- **Diagnostics** — compile errors as you type (includes parser + generator errors)
- **Completions** — keywords and identifiers from the current file
- **Hover** — token kind and declaration detail
- **Go-to-definition** — jumps to the declaration of any identifier
- **Document symbols** — lists all functions, variables, constants, and parameters

### VS Code Extension

The `vscode-extension/` directory contains a VS Code extension that integrates
the LSP server and provides syntax highlighting. Install it with:

```bash
./install.sh --vscode
```

The extension provides:
- Syntax highlighting (TextMate grammar for `.cyan` files)
- Cyan Dark theme (teal/cyan accents)
- LSP integration (errors, completions, hover, go-to-definition, document symbols)

## Project Structure

```
├── src/
│   ├── main.cpp           ─ Entry point, CLI parsing
│   ├── tokenization.hpp/cpp ─ Lexer / tokenizer
│   ├── parser.hpp/cpp     ─ Recursive-descent parser → AST
│   ├── generation.hpp/cpp ─ x86-64 code generator (NASM)
│   ├── arena.hpp/cpp      ─ Arena allocator for AST nodes
│   ├── ast_printer.hpp/cpp ─ Debug AST dump
│   ├── json.hpp           ─ Minimal JSON library
│   ├── lsp.hpp/cpp        ─ LSP server
│   └── lsp_main.cpp       ─ LSP server entry point
├── tests/                 ─ 68+ test .cyan files
├── examples/              ─ Example programs
├── vscode-extension/      ─ VS Code extension
├── run_tests.sh           ─ Test runner
├── install.sh             ─ Install script
├── CMakeLists.txt
├── README.md
├── TODO.md
├── FEATURES.md
└── BUGS.md
```

## Compilation Pipeline

```
.cyan file → Tokenizer → Parser → Generator → out.asm → nasm → out.o → ld → out
```

The compiler:
1. **Tokenizes** the source into a flat vector of tokens
2. **Parses** tokens into an AST (arena-allocated for performance)
3. **Generates** x86-64 NASM assembly
4. **Assembles** with NASM → object file
5. **Links** with ld → ELF executable

Stack-based evaluation: `push rax` / `pop rdi` with a tracked stack pointer
to correctly resolve variable offsets.

## Status

- **68 tests passing** (62 runtime, 6 compile-failure)
- **LSP server**: fully functional
- **VS Code extension**: syntax highlighting + LSP integration
- **Platform**: Linux x86-64 only (NASM + ld)
- **Self-hosting**: not yet (see TODO.md)

## License

MIT
