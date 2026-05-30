# Sodium Compiler Pipeline Demo

This demo walks through every stage of the Sodium compiler pipeline for
the Cyan programming language, showing *identical source code* compiled
to **x86-64** and **RISC-V** targets.

## The Source Program

**File:** [`source/demo.cyan`](source/demo.cyan)

```
function factorial(n) {
    if (n <= 1) {
        return (1);
    } else {
        return (n * factorial(n - 1));
    }
}

var n = 10;
var k = 3;
var n_fact = factorial(n);
var k_fact = factorial(k);
var nk_fact = factorial(n - k);
var result = n_fact / (k_fact * nk_fact);
print(result);
return (result);
```

Computes the binomial coefficient C(10, 3) = 10! / (3! * 7!) = 120.

---

## Stage 1: Tokenization

The preprocessor expands the source, then the tokenizer produces a
stream of tokens. The parser consumes these tokens to build the AST.

---

## Stage 2: AST (Abstract Syntax Tree)

**File:** [`outputs/02_ast.txt`](outputs/02_ast.txt)

The parser produces a tree representation of the program. Each node
is shown with its children:

```
Program
  Let(n)
    IntLit(10)
  Let(k)
    IntLit(3)
  ...
  Function(factorial)
    params: n
    body:
      If
        BinExpr   op: <=
            Ident(n)
            IntLit(1)
        then:
          Return  IntLit(1)
        else:
          Return
            BinExpr   op: *
                Ident(n)
                Call(factorial)
                  BinExpr   op: -
                      Ident(n)
                      IntLit(1)
```

The AST is **target-independent** — it describes only the program
structure, not how to execute it.

---

## Stage 3: IR (Intermediate Representation)

**File:** [`outputs/03_ir.txt`](outputs/03_ir.txt)

The Generator translates the AST into a **virtual-register-based
linear IR** with ~40 opcodes. This IR is completely
**backend-agnostic** — no x86-64 register names, no flags, no
hardcoded calling conventions.

```
function _start_body(34 vregs, 6 stack slots):
  .entry:
      %0 = load_i64 10
      %1 = frame_addr slot0
      store [%1], %0
      ...
      %6 = call factorial(%5)     ← register-based call
      ...
      %30 = call _sodium_print_int(%29)
      %33 = call _sodium_exit(%32)
      ret_void

function factorial(15 vregs, 1 stack slots):
  .entry:
      %0 = load_param 0           ← ABI-agnostic parameter load
      ...
      %5 = cmp_le %3, %4          ← comparison produces a boolean
      br %5, .L2, .L0             ← branch on boolean
      ...
      %13 = call factorial(%12)   ← recursive call
      ret %14
```

Key abstractions that make the IR target-neutral:

| IR Feature | What it abstracts |
|---|---|
| `call` | Register-based calling convention; backend moves args to `arg_regs` |
| `load_param` | Loads param from arg register or overflow stack per ABI |
| `cmp_*` | Produces boolean (0/1) in a vreg — no flags involved |
| `call` target | Backend's `call()` emits appropriate instruction |
| `frame_addr` | Backend's `addr_fp()` returns correct addressing mode |
| `PUSH`/`POP` | Backend implements with native push/pop or sd/ld+addi |

---

## Stage 4: x86-64 Assembly

**File:** [`outputs/04_x86_assembly.asm`](outputs/04_x86_assembly.asm)

The x86-64 backend emits NASM-syntax x86-64 assembly. Key patterns:

```
_start_body:
    push rbp
    mov rbp, rsp
    sub rsp, 48                    ; 6 stack slots × 8 bytes

    ; Call factorial(10)
    mov rdi, 10                    ; arg in rdi (1st arg reg)
    call factorial
    mov [rbp - 16], rax            ; save result

    ; Call factorial(3)
    mov rdi, 3
    call factorial

    ; Compare: n <= 1
    mov rax, [rbp - 8]             ; load n
    cmp rax, 1
    setle al
    movzx eax, al                  ; cmp_result pattern
    test rax, rax
    jnz .L2                        ; branch if true
```

Key observations:
- Arguments passed in **registers** (`rdi`, `rsi`, ...)
- Parameters loaded via `mov rd, rdi` (from arg register)
- Comparisons use `cmp` + `setcc` + `movzx`
- Callee-save registers `rbx`, `r12` preserved via `push`/`pop`

---

## Stage 5: RISC-V Assembly

**File:** [`outputs/05_riscv_assembly.asm`](outputs/05_riscv_assembly.asm)

The RISC-V backend emits GNU assembler syntax for RV64. Same IR,
completely different assembly:

```
_start_body:
    addi sp, sp, -16
    sd ra, 8(sp)
    sd s0, 0(sp)
    addi s0, sp, 16
    addi sp, sp, -48               ; 6 stack slots × 8 bytes

    ; Call factorial(10)
    mv a0, s0                       ; arg in a0 (1st arg reg)
    jal ra, factorial
    sd a0, -16(s0)                  ; save result

    ; Call factorial(3)
    li s0, 3
    mv a0, s0
    jal ra, factorial

    ; Compare: n <= 1
    ld s0, -8(s0)                   ; load n
    li t0, 1
    slt t0, t0, s0                  ; cmp_le: 1 < n ?
    xori t2, t0, 1                  ; invert: n <= 1
    bnez t2, .L2                    ; branch if true
```

Key observations:
- Arguments passed in **registers** (`a0`, `a1`, ...)
- Parameters loaded via `mv rd, a0` (from arg register)
- Comparisons use `slt` + `xori` — no flags
- Callee-save registers saved via `sd`/`ld` + `addi`
- Frame pointer is `s0`, not `rbp`

---

## Side-by-Side Comparison

### Function call sequence

| Stage | x86-64 | RISC-V |
|---|---|---|
| **IR** | `%6 = call factorial(%5)` | (identical IR) |
| **Arg setup** | `mov rdi, 10` | `mv a0, s0` (then `li s0, 10`) |
| **Call** | `call factorial` | `jal ra, factorial` |
| **Return** | `mov [rbp-16], rax` | `sd a0, -16(s0)` |

### Comparison (n <= 1)

| Stage | x86-64 | RISC-V |
|---|---|---|
| **IR** | `%5 = cmp_le %3, %4` | (identical IR) |
| **Impl** | `cmp rax, 1; setle al; movzx eax, al` | `slt t0, t0, s0; xori t2, t0, 1` |
| **Branch** | `test rax, rax; jnz .L2` | `bnez t2, .L2` |

### Function prologue

| x86-64 | RISC-V |
|---|---|
| `push rbp` | `addi sp, sp, -16` |
| `mov rbp, rsp` | `sd ra, 8(sp)` |
| | `sd s0, 0(sp)` |
| | `addi s0, sp, 16` |
| `sub rsp, 48` | `addi sp, sp, -48` |

### Function epilogue (after callee-save pops)

| x86-64 | RISC-V |
|---|---|
| `mov rsp, rbp` | `addi sp, s0, 0` |
| `pop rbp` | `ld ra, -8(sp)` |
| | `ld s0, -16(sp)` |
| `ret` | `ret` |

---

## The Pipeline Architecture

```
Source (.cyan)
    │
    ▼
  Preprocessor    ─── expands includes, pragmas
    │
    ▼
  Tokenizer       ─── produces token stream
    │
    ▼
  Parser          ─── builds AST (target-independent)
    │
    ▼
  Generator       ─── translates AST → IR (target-independent)
    │                 ├── IRBuilder: emits IR instructions
    │                 ├── Liveness Analysis
    │                 ├── Linear Scan Register Allocation
    │                 ├── IRRewriter: callee-save + fixups
    │                 └── IREmitter: IR → Backend calls
    │
    ├──► X8664Backend   ─── NASM x86-64 assembly
    │
    └──► RISCV64Backend ─── GNU RISC-V assembly
```

The critical insight: **the IR is completely backend-agnostic**.
Architecture-specific details (registers, calling conventions,
addressing modes) are confined to the Backend implementation.
The same IR produced from the same source generates correct code
for both x86-64 and RISC-V targets.

## Verification

Both executables were assembled, linked, and executed successfully:

| Target | Command | Output |
|---|---|---|
| **x86-64 (native)** | `./out` | `120` |
| **RISC-V (QEMU)** | `qemu-riscv64 out` | `120` |

### x86-64 build & run

```
$ ./build/sodium --target x86_64 demo/source/demo.cyan
$ ./out
120
```

### RISC-V build & run

```
$ ./build/sodium --target riscv64 demo/source/demo.cyan
$ qemu-riscv64 out
120
```

The exact same IR produced from the same source file generates correct,
functionally identical code for both architectures.
