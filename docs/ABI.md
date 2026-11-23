# Calling conventions and data layout

This document describes the ABI Sodium implements, verified against
`src/ir/target_regs.hpp`, `src/generation.hpp`, `src/ir/rewriter.hpp` and
`src/ir/emitter.hpp` by `scripts/check_arch_docs.sh`. Decisions are recorded in
`docs/DECISIONS.md` (D-001, D-012, D-013, D-014, D-038…D-040).

## Platform (D-001)

Linux only, freestanding: the generated program links against `sodium-rt` and does
not use libc. x86-64 follows the System V AMD64 ABI; riscv64 follows the LP64D
psABI. The stack is 16-byte aligned at every call site. There is no dynamic
linking or position-independent code.

The `TargetRegisterInfo` structure (`src/ir/target_regs.hpp`) is the single source
of truth for physical registers: `arg_regs` (integer argument registers in order),
`ret_reg` / `ret_reg2` (one- and two-word results), `call_target_reg` (the register
holding an indirect call target), `fp_reg` (frame pointer: `rbp` on x86-64, `s0` on RISC-V), `scratch_reg` (spill
scratch), and the FP analogues `fp_arg_regs`, `fp_ret_reg`, `fp_scratch0`,
`fp_scratch1`. Each `Reg` is marked caller-save, callee-save, reserved or
allocatable. `split_struct_args` and `large_struct_by_ref` select the target's
aggregate convention; `fp_bias` adjusts fp-relative offsets for RISC-V's extra
saved registers.

## Integer arguments and returns

- **x86-64 System V:** arguments go in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`
  (`arg_regs = {5, 4, 3, 2, 8, 9}` in `x86_64_systemv()`); further arguments are
  pushed on the stack. Results come back in `rax`, with `rdx` as `ret_reg2`.
- **riscv64 LP64D:** arguments go in `a0`–`a7` (x10–x17); further arguments go on
  the stack. Results come back in `a0`/`a1`. `t0` is reserved as a scratch.

`ArgPlacer` (`src/generation.hpp`) assigns an ordinal to every outgoing argument
as the function is lowered; `assign_arg_regs` (`src/ir/target_regs.hpp`) maps those
ordinals to physical registers or stack offsets. At function entry, `STORE_PARAM`
moves an incoming parameter word directly from its ABI location into a frame slot
(its operands are the ordinal and a chunk shift; `imm_arg` is the fp-relative
byte offset and `dst_width` the stored width). `LOAD_PARAM` loads one into a vreg.

`CALL` and `CALL_INDIRECT` both use the same argument setup. For indirect calls
(D-014) the target vreg is the **last** operand, so argument *i* is operand *i* in
both opcodes; the target is placed in `call_target_reg` (`rax` on x86-64) so the
argument-setting moves cannot overwrite it.

Two-word integer results use `RET2` and a `CALL` with a second destination
(`dst2`); `ret_reg2` names the second register.

## Structures by value (D-012, D-040)

Memory layout is C layout (D-013): fields ascend, narrow integers are stored
little-endian at their natural alignment, and local aggregates are 8-byte aligned
with frame sizes rounded to a multiple of 8.

- **<= 16 bytes, integer/pointer fields:** split into 8-byte words, each in the
  next integer argument register; a trailing partial word is zero-extended.
  Returns use `rax` (<= 8 bytes) or `rax:rdx` (<= 16 bytes) — `a0` / `a0:a1` on
  RISC-V.
- **Larger structs:** x86-64 copies the value into the stack argument area;
  riscv64 passes a pointer to a caller-made copy (`large_struct_by_ref`). Returns
  use a hidden first pointer argument that the callee also returns.
- **<= 16 bytes containing floats (D-040):** classified into at most two pieces,
  each an FP or integer register word, exactly as gcc does. On System V each
  8-byte word is SSE if every field in it is a float (an f32 pair is one packed
  word), else INTEGER; a class that runs out of registers sends the whole struct
  to memory. On LP64D a struct flattening to one float, two floats, or a float
  plus an integer (<= 8 bytes) travels field-by-field in `fa`/`a` registers;
  anything else uses integer words. Result pieces use the next result register
  of their class, so `(float, int)` returns in `xmm0` + `rax` and `(int, float)`
  in `rax` + `xmm0`. `RET2` operands and `CALL` destinations carry a class via
  their width (`dst2_width`).

`tests/abi/` verifies these rules against gcc in both directions on both targets.

## Floating point (D-038, D-039)

FP values carry `IRWidth::F32`/`F64` and a register class; there are eight FP
argument registers (`fp_arg_regs`, `xmm0`–`xmm7` on x86-64, `fa0`–`fa7` on
RISC-V) and one FP result register (`fp_ret_reg`). Integer and FP argument
counters are independent. When the FP registers run out, System V puts further
floats on the stack while LP64D uses the next integer argument registers
(`fp_overflow_to_gpr`). A direct call that passes floats in registers sets `al`
on x86-64 for variadic callees.

`FP_PARAM_ORDINAL_BASE` separates FP parameter ordinals from integer/stack ones:
`STORE_PARAM` at base + *k* names FP argument register *k*. FP values themselves
are stack-resident: the allocator spills every FP vreg and `IRRewriter` lowers
each operation through two scratch FP registers (`fp_scratch0`/`fp_scratch1`,
`xmm0`/`xmm1` or `ft0`/`ft1`), which is correct across calls. f32 arguments are
loaded with `flw` so they are NaN-boxed as LP64D requires. Constants are
materialized through the integer scratch register, so no literal pool is needed.

## Runtime and stack

The runtime is linked automatically. `_start` captures `argc`, `argv` and `envp`
from the initial stack into globals before calling `main`; `main`'s return value
is passed to `_sodium_exit`. `print`/`read` use raw syscalls; `sodium-rt` provides
an exact freestanding `%f` formatter. With `--stack-probe`, `_start` records
`initial_rsp - 1 MiB` in `__sodium_stack_limit` and every function prologue
rejects a stack pointer below it. With `--bounds` and `--check-null`, generated
checks print a fixed message and exit with status 70.
