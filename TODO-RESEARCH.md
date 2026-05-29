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

## Phase 3: Define Backend Interface

**What:** Create `src/backend/interface.hpp` — a pure virtual class
`Backend` that abstracts all architecture-specific instruction emission:
register selection, instruction mnemonics, addressing modes, calling
convention, syscall ABI.

**Why:** The `generation.cpp` file currently emits raw NASM x86-64
strings everywhere. We need a contract that any architecture must
implement.

**Interface sketch:**
```cpp
class Backend {
public:
    virtual ~Backend() = default;

    // Stack / frame
    virtual void push(const std::string& reg) = 0;
    virtual void pop(const std::string& reg) = 0;
    virtual std::string fp_rel(int offset) = 0;   // "[rbp+8]" or "8(sp)"

    // Data movement
    virtual void mov_rr(const std::string& dst, const std::string& src) = 0;
    virtual void mov_rm(const std::string& reg, const std::string& addr) = 0;
    virtual void mov_mr(const std::string& addr, const std::string& reg) = 0;
    virtual void lea(const std::string& reg, const std::string& addr) = 0;

    // Arithmetic
    virtual void add(const std::string& dst, const std::string& src) = 0;
    virtual void sub(const std::string& dst, const std::string& src) = 0;
    virtual void mul(const std::string& dst, const std::string& src) = 0;
    virtual void div(const std::string& divisor) = 0;
    virtual void neg(const std::string& reg) = 0;
    virtual void not_(const std::string& reg) = 0;

    // Comparison / branches
    virtual void cmp(const std::string& a, const std::string& b) = 0;
    virtual void jmp(const std::string& label) = 0;
    virtual void jcc(Condition, const std::string& label) = 0;

    // Control flow
    virtual void call(const std::string& target) = 0;
    virtual void ret() = 0;
    virtual void syscall() = 0;

    // Register file description
    virtual std::string reg(RegClass cls, int index) = 0;
    virtual std::vector<std::string> caller_saved() = 0;
    virtual std::vector<std::string> callee_saved() = 0;
    virtual std::string zero_reg() = 0;
    virtual std::string stack_pointer() = 0;
    virtual std::string frame_pointer() = 0;

    // Syscall ABI
    virtual int syscall_exit() = 0;
    virtual int syscall_brk() = 0;
    virtual int syscall_read() = 0;
    virtual int syscall_write() = 0;
    virtual std::string syscall_arg(int n) = 0;  // rdi, rsi, rdx, rcx, r8, r9
};
```

**Components:**
- [ ] `src/backend/interface.hpp` — abstract class
- [ ] `src/backend/registry.hpp` — `Backend* create_backend(const std::string& arch);`
- [ ] `src/generation.hpp` — `Generator` holds `std::unique_ptr<Backend>`
- [ ] `src/generation.cpp` — all `m_output << "..."` replaced with `m_backend->*()`

---

## Phase 4: x86-64 Backend Implementation

**What:** Implement the `Backend` interface for x86-64 using NASM syntax.
This should produce identical output to the current hardcoded strings.

**Why:** Validates the interface works and provides a baseline for RISC-V.

**Components:**
- [ ] `src/backend/x86_64/backend.hpp`
- [ ] `src/backend/x86_64/backend.cpp`
- [ ] `src/backend/registry.cpp` — registers `x86_64` factory
- [ ] All 84+ tests pass with identical output

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
