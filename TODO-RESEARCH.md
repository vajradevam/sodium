# Sodium Research Compiler — Architecture Roadmap

**Goal:** Restructure the Cyan compiler into a clean,
multi-architecture research platform with swappable backends,
a freestanding runtime library, and self-hosting capability.

---

## Phase 1: Runtime Library Extraction ✅

**What:** Move all inline syscall/asm from `generation.cpp` into
standalone `.asm` files in `sodium-rt/`. Compiler emits `call _sodium_*`
instead of inline syscall sequences. Linker step adds `sodium-rt.a`.

**Why:** Creates a clear boundary between compiler and runtime.
Runtime becomes a swappable component. Architecture-specific syscall
code lives in one place.

**Components:**
- [x] `sodium-rt/crt0.asm` — `_start` entry (currently stays in compiler output for now; crt0 will fully own it in Phase 2)
- [x] `sodium-rt/malloc.asm` — `_sodium_malloc`, `_sodium_free`
- [x] `sodium-rt/print.asm` — `_sodium_print_int`
- [x] `sodium-rt/read.asm` — `_sodium_read_int`
- [x] `sodium-rt/exit.asm` — `_sodium_exit`
- [x] `sodium-rt/Makefile` — produces `sodium-rt.a`
- [x] Modify `src/generation.cpp` — emit `call _sodium_*` instead of inline asm
- [x] Modify `src/main.cpp` — link against `sodium-rt.a`
- [x] All 84+ tests pass

---

## Phase 2: Move _start / Top-Level Code into Runtime

**What:** Refactor `gen_prog()` so `_start` is emitted by `crt0.asm`,
not by the compiler. Top-level code becomes `_sodium_main()` function.
Global variable initialization becomes `_sodium_init_globals()`.

**Why:** The entry point and startup sequence should be part of the
runtime library, not emitted by the compiler. This is required for
multi-architecture support — RISC-V has different startup conventions.

**Components:**
- [ ] `sodium-rt/crt0.asm` — owns `_start`, calls `_sodium_init_globals`, `_sodium_main`, `_sodium_exit`
- [ ] `src/generation.cpp` — emit `_sodium_main` instead of inline `_start`
- [ ] `src/generation.cpp` — emit `_sodium_init_globals` for global/static init
- [ ] Remove implicit exit emission from compiler
- [ ] All tests pass

---

## Phase 3: Define Backend Interface ✅

**What:** Create `src/backend/interface.hpp` — a pure virtual class
`Backend` that abstracts all architecture-specific instruction emission:
register selection, instruction mnemonics, addressing modes, calling
convention, syscall ABI.

**Why:** The `generation.cpp` file currently emits raw NASM x86-64
strings everywhere. We need a contract that any architecture must
implement.

**Components:**
- [x] `src/backend/interface.hpp` — abstract class
- [x] `src/generation.hpp` — `Generator` holds `std::unique_ptr<Backend>`
- [x] `src/generation.cpp` — all `m_output << "..."` replaced with `m_backend->*()`

---

## Phase 4: x86-64 Backend Implementation ✅

**What:** Implement the `Backend` interface for x86-64 using NASM syntax.
This produces identical output to the original hardcoded strings.

**Why:** Validates the interface works and provides a baseline for RISC-V.

**Components:**
- [x] `src/backend/x86_64/backend.hpp`
- [x] `src/backend/x86_64/backend.cpp`
- [x] All 84+ tests pass with identical output

---

## Phase 5: RISC-V Backend

**What:** Implement the `Backend` interface for RV64GC using gas/syntax.

**Why:** This is the primary research target.

**Components:**
- [ ] `src/backend/riscv64/backend.hpp`
- [ ] `src/backend/riscv64/backend.cpp`
- [ ] `sodium-rt/riscv64/crt0.s`
- [ ] `sodium-rt/riscv64/syscall.s`
- [ ] `sodium-rt/riscv64/malloc.s` (or malloc.cyan)
- [ ] `sodium-rt/riscv64/print.s`
- [ ] `sodium-rt/riscv64/read.s`
- [ ] `sodium-rt/riscv64/exit.s`
- [ ] `sodium-rt/Makefile` — builds both x86_64 and riscv64 variants
- [ ] Cross-compilation test via QEMU user-mode emulation
- [ ] All tests pass under `qemu-riscv64`

---

## Phase 6: Self-Hosting (Bootstrap)

**What:** Write a Cyan-to-Cyan compiler that uses the `Backend` interface
and the `sodium-rt` library. The compiler compiles itself.

**Why:** This is the final validation of the architecture. If the compiler
can compile itself, the abstraction boundaries are real.

**Components:**
- [ ] Write `sodium.cyan` — minimal self-hosted compiler
- [ ] Compile `sodium.cyan` with the reference compiler
- [ ] The resulting binary compiles `sodium.cyan` again
- [ ] Byte-for-byte identical output (bootstrap proof)

---

## Phase 7: Research Experiments

**What:** Use the platform for systems research.

**Possible experiments:**
- Alternative allocator designs (slab, buddy, region, GC)
- Tagged pointers / capability-based addressing
- Hardware performance counter instrumentation
- Static analysis / formal verification of generated code
- Custom calling conventions
- Fuzz testing of generated RISC-V code
- Compiler correctness proofs

---

## Milestones

| Milestone | Phases | Target Date |
|-----------|--------|-------------|
| Runtime extraction complete | 1 | Today |
| Top-level code in runtime | 2 | Next |
| Backend interface stable | 3-4 | After Phase 2 |
| RISC-V hello world | 5 | After Phase 4 |
| All tests on RISC-V | 5 | After hello world |
| Self-hosting | 6 | After Phase 5 |
| First research experiment | 7 | After Phase 6 |
