# Sodium Research Compiler — Architecture Roadmap

**Goal:** Restructure the Cyan compiler into a clean,
multi-architecture research platform with swappable backends,
a freestanding runtime library, a proper intermediate representation,
and self-hosting capability.

---

## Phase 1: Runtime Library Extraction ✅

**What:** Move all inline syscall/asm from `generation.cpp` into
standalone `.asm` files in `sodium-rt/`. Compiler emits `call _sodium_*`
instead of inline syscall sequences. Linker step adds `sodium-rt.a`.

**Components:**
- [x] `sodium-rt/x86_64/exit.asm` — `_sodium_exit`
- [x] `sodium-rt/x86_64/malloc.asm` — `_sodium_malloc`, `_sodium_free`
- [x] `sodium-rt/x86_64/print.asm` — `_sodium_print_int`
- [x] `sodium-rt/x86_64/read.asm` — `_sodium_read_int`
- [x] `sodium-rt/Makefile` — produces `sodium-rt.a`
- [x] Modify `src/generation.cpp` — emit `call _sodium_*` instead of inline asm
- [x] Modify `src/main.cpp` — link against `sodium-rt.a`
- [x] All 84+ tests pass

---

## Phase 2: Move _start / Top-Level Code into Runtime

**What:** Refactor `gen_prog()` so `_start` is emitted by `crt0.asm`,
not by the compiler. Top-level code becomes `_sodium_main()` function.
Global variable initialization becomes `_sodium_init_globals()`.

**Components:**
- [ ] `sodium-rt/crt0.asm` — owns `_start`, calls `_sodium_init_globals`, `_sodium_main`, `_sodium_exit`
- [ ] `src/generation.cpp` — emit `_sodium_main` instead of inline `_start`
- [ ] `src/generation.cpp` — emit `_sodium_init_globals` for global/static init
- [ ] Remove implicit exit emission from compiler
- [ ] All tests pass

---

## Phase 3: Backend Interface (current) ✅

**What:** A thin `Backend` interface wrapping architecture-specific
assembly emission. Currently being used as a stepping stone.

**Components:**
- [x] `src/backend/interface.hpp` — abstract class (60+ methods)
- [x] `src/backend/x86_64/backend.{hpp,cpp}` — NASM implementation
- [x] `src/generation.cpp` — all assembly emission goes through `m_backend->*()`
- [x] All 84+ tests pass

**Status:** Working, but insufficient for real multi-architecture support.
The generator still thinks in x86-64 registers and instruction selection.
To be replaced by a proper IR in Phase 5.

---

## Phase 4: IR Definition ✅

**What:** Define a lightweight three-address code intermediate
representation with infinite virtual registers. The IR has ~30 opcodes
(load, store, add, sub, mul, div, br, call, ret, etc.) and is
architecture-independent. A builder API accumulates IR instructions
per function.

**Why:** Breaks the generator's dependency on x86-64 register names
and instruction selection. The generator emits IR using virtual
registers (%0, %1, %2...) — no `rax`, no `rdi`, no architecture-specific
decisions. The IR is the contract; backends consume it.

**Components:**
- [x] `src/ir/opcodes.hpp` — enum of all IR operations
- [x] `src/ir/value.hpp` — `IRValue` (virtual register + type + width)
- [x] `src/ir/instruction.hpp` — `IRInstruction` (opcode, operands, result)
- [x] `src/ir/block.hpp` — `IRBlock` (basic block with linear instructions)
- [x] `src/ir/function.hpp` — `IRFunction` (blocks, parameters, stack frame)
- [x] `src/ir/builder.hpp` — `IRBuilder` (API for emitting IR; manages
  virtual register allocation, block chaining)
- [x] `src/ir/module.hpp` — `IRModule` (all functions, globals, string literals)
- [x] `src/ir/dump.hpp` — IR printer for debugging

---

## Phase 5: Register Allocator

**What:** A linear-scan register allocator that takes an `IRFunction`
with infinite virtual registers and produces a new `IRFunction` where
every virtual register is replaced with a physical register or a spill
slot. Spills are inserted where necessary.

**Why:** The boundary between architecture-independent IR and
architecture-specific codegen. The allocator knows the physical
register file (caller-saved, callee-saved, fixed registers) and the
calling convention, but it does not know instruction encoding.

**Components:**
- [ ] `src/regalloc/liveness.hpp` — Live interval analysis for virtual regs
- [ ] `src/regalloc/allocator.hpp` — `LinearScanAllocator` (takes IRFunction,
  assigns phys regs, inserts spills)
- [ ] `src/regalloc/interface.hpp` — Abstract `RegisterAllocator` interface
  (allows swapping allocator strategies for research)
- [ ] `src/backend/interface.hpp` — Revised: now a lower-level interface
  for emitting *physical-register* assembly instructions
- [ ] Tests: allocator produces correct code for simple expressions

---

## Phase 6: Port generation.cpp to IR

**What:** Rewrite every expression and statement handler in
`generation.cpp` to emit IR via `IRBuilder` instead of calling
`m_backend->*()` directly. The pipeline becomes:

```
gen_expr → IRBuilder → IRFunction → RegisterAllocator → Backend → asm
```

**Why:** This is the big one. After this phase, the generator is
truly architecture-independent. It never mentions registers.

**Components:**
- [ ] `gen_expr` for int literals
- [ ] `gen_expr` for identifiers (local, global, constant)
- [ ] `gen_expr` for binary operations (add, sub, mul, div, mod, and, or, xor, etc.)
- [ ] `gen_expr` for comparisons
- [ ] `gen_expr` for array indexing
- [ ] `gen_expr` for struct field access
- [ ] `gen_expr` for `&` (address-of) and `*` (deref)
- [ ] `gen_expr` for function calls (including malloc/free builtins)
- [ ] `gen_expr` for ternary, string literals, array literals, read
- [ ] `gen_stmt` for let, assign, compound assign
- [ ] `gen_stmt` for if/else, while, for, do-while
- [ ] `gen_stmt` for return, exit, print, break, continue
- [ ] `gen_stmt` for switch/case
- [ ] `gen_stmt` for deref assign
- [ ] `gen_stmt` for field assign
- [ ] `gen_func_def`, `gen_prog`, global/static init
- [ ] All 84+ tests pass

---

## Phase 7: x86-64 Physical Backend

**What:** The x86-64 backend becomes a "physical instruction emitter"
that takes IR with physical registers and emits NASM. This replaces
the current thin Backend with a cleaner, narrower interface.

**Why:** After the register allocator, the IR has physical registers.
The backend just needs to map each IR instruction to one or more
target instructions. No register allocation, no liveness, no spills —
just instruction selection and encoding.

**Components:**
- [ ] Revised `src/backend/interface.hpp` — narrow interface for
  emitting physical-register IR:
  - `emit(IRInstruction)` — dispatch based on opcode
  - No more `push("rax")`, `pop("rdi")` — those are allocator concerns
- [ ] `src/backend/x86_64/backend.cpp` — rewrite against new interface
- [ ] All 84+ tests pass

---

## Phase 8: RISC-V Backend

**What:** Implement the physical-instruction emitter for RV64GC.

**Why:** Primary research target. After the IR + allocator port, this is
a clean ~500-line implementation.

**Components:**
- [ ] `src/backend/riscv64/backend.{hpp,cpp}` — instruction selection
- [ ] `sodium-rt/riscv64/` — crt0, syscall wrappers, allocator, print, read
- [ ] Cross-compilation via QEMU user-mode emulation
- [ ] All tests pass under `qemu-riscv64`

---

## Phase 9: Self-Hosting (Bootstrap)

**What:** Write a Cyan-to-Cyan compiler that uses the IR infrastructure
and the runtime library. The compiler compiles itself.

**Components:**
- [ ] Write `sodium.cyan` — minimal self-hosted compiler
- [ ] Compile `sodium.cyan` with the reference compiler
- [ ] The resulting binary compiles `sodium.cyan` again
- [ ] Byte-for-byte identical output (bootstrap proof)

---

## Phase 10: Research Experiments

**What:** Use the platform for systems research.

**Possible experiments:**
- Alternative register allocators (graph coloring, PBQP, ML-based)
- Custom instruction set extensions (tagged pointers, capabilities)
- Hardware performance counter instrumentation at IR level
- Fuzz testing of generated RISC-V code
- Compiler correctness proofs via IR semantics
- Custom calling conventions
- Static analysis passes on the IR

---

## Milestones

| Milestone | Phase | Target |
|-----------|-------|--------|
| Runtime extracted | 1 | Done |
| Thin backend interface | 3 | Done |
| **IR definition complete** | **4** | **Next** |
| generation.cpp emits IR | 6 | After 5 |
| All tests pass on IR pipeline | 6 | After 6 |
| RISC-V hello world | 8 | After 7 |
| Self-hosting | 9 | After 8 |
| First research experiment | 10 | After 9 |
