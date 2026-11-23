# Cyan Language Compiler — Sodium

**Sodium** compiles **Cyan**, a small C-like systems language, to x86-64 (NASM) and
RISC-V 64 (GAS) Linux. It is a freestanding compiler: the generated executable links
directly against `sodium-rt/` and needs no libc. The long-term goal is a self-hosted
compiler (`PLAN.md`, Phase 9).

This README documents *verified* behavior only. Every ```` ```cyan ```` block is a
complete program, extracted and compiled for both targets by `scripts/doc_check.sh`
(which runs as part of `scripts/check.sh`). For the authoritative task list, design
decisions and progress log see `AGENTS.md`, `PLAN.md` and `docs/`.

## Quick Start

```bash
# Build (requires a C++20 compiler, CMake, NASM and ld)
cmake -B build && cmake --build build

# Compile, link and run a Cyan program (x86-64 is the default target)
cat > hello.cyan <<'CYAN'
function main() {
    print("hello, Cyan");
    return 0;
}
CYAN
./build/sodium hello.cyan
./hello

# Cross-compile for RISC-V 64 and run under qemu
./build/sodium --target riscv64 hello.cyan
qemu-riscv64 ./hello

# Or install system-wide (see D5 / scripts/install.sh)
./scripts/install.sh
```

The return value of `main()` is the process exit code. Top-level statements are not
allowed; a program must define `function main()` ([test](tests/integration/test_comprehensive.cyan)).

## Language Tour

Each subsection links to tests that verify the construct on both targets at `-O0`
and `-O1`.

### Variables and integer types

```cyan
function main() {
    var a = 5;            // inferred i64
    var b: i32 = 10;      // explicit width
    var c: u8 = 255;
    var flag: bool = true;
    var ch: char = 65;
    print("%d %d %d %d %d", a, b, c, flag, ch);
    return 0;
}
```

Integer types are `i8`, `i16`, `i32`, `i64` and their unsigned counterparts `u8`–`u64`.
Narrow integer `+`/`-` results are promoted to `i32` like C. `bool` and `char` are
supported ([test](tests/types/test_bool_char.cyan)). `type` declares an alias
([test](tests/types/test_type_alias.cyan)).

### Arithmetic and operators

```cyan
function main() {
    var x = 7;
    var y = 2;
    print("%d %d %d %d %d", x + y, x - y, x * y, x / y, x % y);
    print("%d %d %d", x == y, x != y, x < y);
    print("%d %d %d", x > 0 && y > 0, x > 0 || y > 0, !x);
    print("%d %d %d %d", x & y, x | y, x ^ y, x << 1);
    var z = 8;
    z >>= 1;
    z += 5;
    print("%d", z);
    print("%d", x > y ? x : y);   // ternary
    return 0;
}
```

Operators: `+ - * / %`, comparisons `== != < <= > >=`, logical `&& || !`, bitwise
`& | ^ ~ << >>`, compound assignment (`+= -= *= /= %= &= |= ^= <<= >>=`), and the
ternary `?:`. Division/modulo by zero trap at runtime. Hex literals `0x…` are accepted
([test](tests/integration/test_comprehensive.cyan)).

### Control flow

```cyan
function main() {
    var x = 5;
    if (x > 0) { print("positive"); } else if (x == 0) { print("zero"); } else { print("negative"); }

    var i = 0;
    while (i < 3) { i = i + 1; }
    do { i = i - 1; } while (i > 0);
    for (var j = 0; j < 3; j = j + 1) { print(j); i = i + j; }

    switch (x) {
        case 1: { print("one"); break; }
        case 5: { print("five"); break; }
        default: { print("other"); }
    }
    return 0;
}
```

`if`/`else if`/`else`, `while`, `do…while`, C-style `for`, `switch`/`case`/`default`
(with explicit fall-through — there is no implicit `break`), `break` and `continue`
([test](tests/integration/test_scoping_switch.cyan)). Blocks introduce nested scopes
with shadowing.

### Arrays

```cyan
function main() {
    var a[5];                    // five i64s, zero-initialized
    var b = [10, 20, 30];        // inferred i64[3]
    const N = 4;
    var c[N];                    // size from a constant
    a[0] = 7;
    b[1] = 99;
    print("%d %d %d", a[0], b[1], c[0]);
    return 0;
}
```

Arrays carry their element count (`var a[N]`) and support array literals
(`var a = [1, 2, 3]`), indexing and assignment ([test](tests/unit/test_arr_lit.cyan)).

### Functions

```cyan
function add(a: i64, b: i64) -> i64 {
    return a + b;
}

function fact(n: i64) -> i64 {
    if (n <= 1) { return 1; }
    return n * fact(n - 1);
}

function main() {
    print("%d %d", add(3, 4), fact(5));
    return 0;
}
```

Parameters and return types may be annotated (`function f(x: i64) -> i64`); untyped
parameters are inferred from the call site and emit a warning. Recursion is supported
([test](tests/unit/test_func_rec.cyan)).

### Structs and enums

```cyan
struct Point { var x: i64; var y: i64; };
enum Color { Red, Green = 5, Blue };

function main() {
    var p: Point;
    p.x = 10;
    p.y = p.x + 5;
    print("%d %d", p.x, p.y);
    print("%d %d %d", Red, Green, Blue);
    var c = Green;
    print("%d", c);
    return 0;
}
```

Struct fields are typed; structs can be passed by pointer and returned by value.
Small structs containing floats travel in FP/integer register pieces per the target
ABI. Enum constants are i64 values with optional explicit assignments
([structs](tests/floats/test_float_struct.cyan), [enums](tests/types/test_enum.cyan)).

### Pointers and memory

```cyan
function main() {
    var x = 42;
    var p = &x;
    *p = 100;
    var y = *p;

    var buf = malloc(32);        // segregated-free-list heap allocator
    *buf = 7;
    *(buf + 8) = 9;              // pointer arithmetic scales by element size
    var sum = *buf + *(buf + 8);
    free(buf);

    print("%d %d", y, sum);
    return 0;
}
```

`&x` takes an address, `*p` dereferences (reads and writes), `p + n` and `p - q`
scale by the pointee size. `malloc`/`free` come from the freestanding SFL allocator;
`cyanlib/mem.cyan` adds `calloc`, `realloc` and aligned allocations
([test](tests/oracle/ptr_malloc_store.cyan)).

### Floats

```cyan
function main() {
    var a: f64 = 7.5;
    var b: f64 = 2.5;
    var c: f32 = 1.5f;
    print("%f %f %f", a + b, a * b, a / b);
    print("%f", c);
    var i: i64 = 3;
    print("%f", i * 1.5);          // int converts to float
    print("%d", (a + b) as i64);   // explicit conversion
    print("%f", 1e3 + 2.5e-1);
    return 0;
}
```

`f32` and `f64` literals (`1.5`, `2e-3`, `1.5f`) and arithmetic use the C-compatible
System V / LP64D FP ABI, verified against gcc on both targets. `print(x)` formats by
the value's type; `%f` forces fixed six-digit float formatting (the freestanding
formatter is exact). `%d`/`%x`/`%u`/`%c`/`%s` reject floats; use `as i64` first.
Float literals with `f` suffix are `f32`
([arithmetic](tests/floats/test_float_arith.cyan),
[formatting](tests/floats/test_float_print.cyan),
[conversion](tests/floats/test_float_convert.cyan)).

### Global and static variables, constants

```cyan
const N = 3;
global var g = 0;

function counter() {
    static var calls = 0;
    calls = calls + 1;
    return calls;
}

function main() {
    g = g + counter();
    var a[N];
    print("%d %d", g, N);
    return 0;
}
```

`global var` is initialized before `main`, `static var` persists across calls, and
`const` names compile-time integer expressions (also usable as array sizes)
([test](tests/integration/test_comprehensive.cyan)).

### Input and output

```cyan
function main() {
    print(42);                       // plain integer
    print("hello");                  // plain string
    print();                         // just a newline
    print("x=%d y=%d", 10, 20);      // C-style format string
    print("%x %u %c %ld", 255, 4294967295, 65, 1234567890123);
    print("100%%");                  // literal %
    var n = read();                  // integer from stdin (EOF -> 0)
    print("%d", n);
    return 0;
}
```

Format specifiers: `%d`, `%s`, `%x`, `%u`, `%c`, `%ld`, `%f`, `%%`. When the first
argument is not a string literal, `print` falls back to plain integer/string output.
`read()` reads an integer; `read(buf)` reads a line into an array buffer and returns
the byte count ([print](tests/unit/test_fprint.cyan),
[read](tests/unit/test_read_str.cyan)). `cyanlib/io.cyan` adds `fprint(fd, s)` and
`assert(cond, msg)`.

### Command-line arguments

```cyan
function main() {
    print("argc = %d", argc());
    var i = 0;
    while (i < argc()) {
        print("argv[%d] = %s", i, argv(i));
        i = i + 1;
    }
    return 0;
}
```

`argc()` and `argv(i)` are builtins backed by values captured in `_start` on both
targets ([test](tests/unit/test_argc.cyan)).

### Preprocessor and inline assembly

```cyan
#define LIMIT 3
#define DOUBLE(x) ((x) * 2)

function main() {
#if defined(LIMIT) && LIMIT == 3
    var n = DOUBLE(2);
#else
    var n = 0;
#endif
    asm("x86_64", "nop", []);
    asm("riscv64", "nop", []);
    print("%d", n);
    return 0;
}
```

The preprocessor supports `#include`, `#pragma once`, `#ifdef`/`#ifndef`/`#else`/
`#endif`, object- and function-like `#define`, and `#if` constant expressions
(`defined`, comparisons, arithmetic, `&&`, `||`, `!`). Inactive branches emit no
tokens. `asm("arch", "instruction", ["clobbers"])` emits one line of target-specific
assembly; the clobber list must contain only caller-saved registers
([macros](tests/unit/test_preprocessor_macros.cyan),
[asm](tests/checks/test_inline_asm.cyan)). See `docs/MODULES.md` for cyanlib naming
and header conventions.

### Optional runtime checks

```cyan
function main() {
    var a[4];
    var i = 0;
    while (i < 4) { a[i] = i; i = i + 1; }
    print("%d", a[2]);
    return 0;
}
```

`--bounds` checks every array index, `--check-null` checks pointer dereferences and
`--stack-probe` checks the stack pointer at function entry. A failed check prints a
fixed message and exits with status 70; valid programs are unchanged. With none of
these flags the generated code is identical to the unchecked build
([bounds](tests/checks/test_bounds_fail.cyan),
[null](tests/checks/test_null_read_fail.cyan),
[stack](tests/checks/test_stack_probe_fail.cyan)).

## Both Architectures

The same source compiles to either backend:

```bash
./build/sodium -o myprog program.cyan      # x86-64 (default)
./myprog

./build/sodium --target riscv64 program.cyan
qemu-riscv64 ./program

./build/sodium -S program.cyan             # emit assembly and stop
./build/sodium -g program.cyan             # DWARF line tables for gdb
```

Both targets run the full suite with identical results. The RISC-V backend handles
large stack frames (frame offsets beyond the 12-bit `addi` range) with `li`+`add`
and materializes addresses directly in the destination register.

## Building from Source

Dependencies: a C++20 compiler (GCC 11+/Clang 14+), CMake 3.20+, NASM, GNU `ld`, and
optionally `riscv64-elf-gcc`/`riscv64-elf-as` plus `qemu-riscv64` for the RISC-V
backend. `gdb`/`readelf` are used only by the debug-info test.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

# Debug/instrumented build (the IR verifier always runs)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug && cmake --build build-asan

# Runtime libraries (also built automatically by CMake)
make -C sodium-rt
make -C sodium-rt/riscv64
```

Outputs: `build/sodium` (compiler), `build/cyan-lsp` (language server). The runtime
is linked automatically.

## Compiler Flags

```
-o <file>, --output <file>   Output path (default: input basename without .cyan)
-S, --emit-asm               Emit assembly and stop
--target <arch>              x86_64 (default) or riscv64
-O0, -O1                     Optimization level (-O1 is the default)
--no-alloc                   Bypass register allocation (all values on stack)
--verify-ir                  Run the IR verifier after every pass
--emit-ir                    Print the IR during code generation
--dump-regalloc              Print allocator statistics and pass counters
-I <dir>, --include-dir <dir>        Add an include search directory
--cyanlib-dir <dir>          Override the cyanlib search directory
-w, -Werror                  Suppress warnings / promote them to errors
-fcolor-diagnostics, -fno-color-diagnostics, NO_COLOR
--bounds, --check-null, --stack-probe
-g                           Emit DWARF line tables (NASM %line / GAS .file+.loc)
--print-ast                  Dump the AST and exit
```

## Project Structure

```
src/                        Compiler sources (tokenizer → preprocessor → parser → gen → IR → backend)
  ir/                       IR, verifier, liveness, linear scan, rewriter, emitter, inline, FP opcodes
  backend/{x86_64,riscv64}/ Target backends and a null backend for LSP codegen
sodium-rt/                  Freestanding runtime (CRT, print/read/exit, SFL allocator, float formatter)
cyanlib/                    Library in Cyan (containers, fmt, io, mem, string)
bench/                      Benchmark programs and dynamic instruction-count baselines
docs/                       DECISIONS, PROGRESS, MODULES, BLOCKED, STYLE
tests/                      unit, integration, types, floats, checks, oracle, abi, lsp, warnings, cpp
scripts/                    check.sh, run_tests.sh, doc_check.sh, bench.sh, install.sh, debug-info test
vscode-extension/           VS Code syntax, theme and LSP integration
```

## Testing

```bash
scripts/check.sh            # build + style + sweep + effects + benchmarks + oracle + ABI + suite
scripts/check.sh --full     # also builds with ASan/UBSan and re-runs the suite
scripts/run_tests.sh        # discoverable test runner; --filter SUBSTR, --target NAME, -j N
scripts/bench.sh            # dynamic guest instruction counts vs committed baselines
```

Latest gate (`scripts/check.sh --full`): **776 integration tests passed**, 0 failed,
18 skipped on both targets at `-O0` and `-O1` and under ASan/UBSan; **325 C++ unit
cases / 4465 checks**; **62/62** differential oracle comparisons; **16/16** C ABI
interoperability cases; **2394** sweep invocations with 0 crashes; fuzz smoke 1000
inputs with 0 crashes. The 18 skips are ABI/helper files exercised by their own
scripts. See `docs/PROGRESS.md` for the append-only history.

## LSP Server

`build/cyan-lsp` implements LSP over stdin/stdout: diagnostics, completions, hover,
go-to-definition, document symbols and signature help. The `vscode-extension/`
directory provides a TextMate grammar, a theme and LSP integration.

## Known Limitations

- Floats have no FP register allocation: FP values are stack-resident and lowered
  through scratch FP registers, which is correct across calls but slower. There is
  no float constant folding yet.
- Floats cannot be globals, global array elements or builtin arguments; the arms of
  a float ternary must both be floats. `u64` values above `i64::MAX` convert to float
  as negative (documented, not trapped).
- Structs larger than 16 bytes containing floats use the memory/by-reference ABI,
  not register pieces.
- No WASM or aarch64 backend (`PLAN.md` Phase 8); no self-hosting yet (Phase 9).
- Debug info is line tables only (`-g`), not full DWARF DIEs for locals and types.
- The compiler is single-threaded; incremental LSP reparsing is limited.

## License

MIT
