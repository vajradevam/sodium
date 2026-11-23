# Sodium IR — implementation reference

This document describes the intermediate representation as implemented in `src/ir/`.
It is verified against the source by `scripts/check_arch_docs.sh`: every opcode and
symbol named here must exist in the implementation. Design rationale lives in
`docs/DECISIONS.md` (D-006, D-027…D-031, D-038…D-040).

## Where the IR sits

`src/gen/*` lowers the Cyan AST into IR through `src/ir/builder.hpp`. Each function
is finalized by `Generator::flush_function` (`src/gen/pipeline.cpp`):

```
verify_function → [fold_function → promote_locals → cse_function → cleanup_function] → verify_function
                  (only when -O1 and the function had no codegen error)
   → compute_live_ranges → LinearScanAllocator → IRRewriter → IREmitter
```

With `-O0` the optimization block is skipped but the pipeline is otherwise the
same. Leaf calls are spliced in by `inline_leaves` (`src/ir/inline.hpp`) *before*
this per-function pipeline, while all generated functions are held in a pool.

## Data model

- `src/ir/value.hpp` — `IRValue` is either a virtual register (`vreg`) or an
  `int64_t` immediate. `IRWidth` is `I8`, `I16`, `I32`, `I64` plus `F32`/`F64` for
  floating-point operands (D-038).
- `src/ir/instruction.hpp` — `IRInstruction` has an opcode, `dst`/`dst2`, a
  vector of `IRValue` operands, and metadata fields (`call_target`, branch labels,
  `imm_arg`, `dst2_width`, ASM text/clobbers, source line/file). Every opcode has
  a static factory (`IRInstruction::add`, `::call`, `::asm_insn`, …).
- `src/ir/block.hpp` / `src/ir/function.hpp` — a function is a vector of basic
  blocks; the first is the entry block. `IRFunction::new_vreg()` allocates vregs,
  `stack_slots` records the frame size, and `is_fp` marks FP-containing functions.
- `src/ir/builder.hpp` — `IRBuilder::emit` appends an instruction to the current
  block and stamps the source location set by `set_loc`. Convenience helpers
  (`load_i64`, `add`, `call`, `br`, `ret`, `asm_insn`, FP helpers) build typed IR.

## Opcode reference

Defined in `src/ir/opcodes.hpp`; operand counts are fixed except for calls.

| Group | Opcodes | Notes |
| --- | --- | --- |
| No result | `NOP`, `ASM`, `JMP`, `RET_VOID`, `SYSCALL`, `PUSH`, `PUSH_FRAME`, `POP` | `ASM` carries verbatim assembly; `JMP` uses `label_true`. |
| Constants / moves | `LOAD_I64`, `LOAD_I32`, `LOAD_I8`, `LOAD_U64`, `LOAD_U32_IMM`, `LOAD_U8_IMM`, `COPY`, `FRAME_ADDR` | `FRAME_ADDR` computes a slot address; immediates are sign/zero extended per variant. |
| Unary / casts | `NEG`, `NOT_`, `ZEXT`, `SEXT`, `TRUNC` | Width carried on `dst`/operands. |
| Integer arithmetic | `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `AND`, `OR`, `XOR`, `SHL`, `SHR`, `ASHR` | `DIV`/`MOD` implicitly clobber rax/rdx. |
| Integer compare | `CMP_EQ`, `CMP_NE`, `CMP_LT`, `CMP_LE`, `CMP_GT`, `CMP_GE` | Result is 0 or 1. |
| Memory | `LOAD` (and `LOAD_S8`, `LOAD_U8`, `LOAD_S16`, `LOAD_U16`, `LOAD_S32`, `LOAD_U32`), `STORE` (and `STORE_8`, `STORE_16`, `STORE_32`), `LEA`, `LEA_LABEL` | Address = base vreg + byte offset. |
| Control flow | `BR`, `CALL`, `CALL_INDIRECT`, `LOAD_PARAM`, `STORE_PARAM`, `RET`, `RET2` | `CALL_INDIRECT` keeps its target as the last operand; `RET2` returns two words. |
| Floating point | `FCONST`, `FCOPY`, `FNEG`, `FADD`, `FSUB`, `FMUL`, `FDIV`, `FCMP_EQ`, `FCMP_NE`, `FCMP_LT`, `FCMP_LE`, `FCMP_GT`, `FCMP_GE`, `I2F`, `F2I` | FP values carry `IRWidth::F32`/`F64`; `FCMP_*` results are integer. |

## Verification

`src/ir/verify.hpp` — `verify_function` checks that every use is dominated by a
definition, operand counts/widths/regclasses match the opcode, branch targets
exist, and each block has a terminator. It runs before and after optimization;
`--verify-ir` requests it explicitly and Debug builds always run it.

## Optimization passes

Under `-O1` (and skipped for functions with codegen errors, to avoid turning a
diagnostic into a verifier failure):

- `src/ir/fold.hpp` — `fold_function`: constant folding and algebraic
  simplification with wrapping arithmetic; trapping divisions are not folded.
- `src/ir/mem2reg.hpp` — `promote_locals`: promotes non-escaping frame slots to
  shared vregs when every load is dominated by a store (D-027).
- `src/ir/cse.hpp` — `cse_function`: local value numbering with store-to-load
  forwarding; DIV/MOD, calls, syscalls and ASM are barriers.
- `src/ir/cleanup.hpp` — `cleanup_function`: unreachable-block and dead-code
  removal, jump threading, empty-block removal (D-024).
- `src/ir/inline.hpp` — `inline_leaves`: splices small single-block leaf callees
  into callers with fresh vregs and shifted frame slots; rejects address-taken,
  recursive, aggregate and ASM-containing callees (D-031).

## Register classes and liveness

`src/ir/liveness.hpp` — `compute_live_ranges` performs CFG dataflow (live-in /
live-out fixpoint) and produces one `LiveRange` per vreg with `start`/`end`,
`use_positions`, `spans_call`, `crosses_div` and `clobbered_pregs` (ASM clobbers).
A range spanning a call or DIV is restricted to callee-save registers; a range
crossing ASM avoids the specific clobbered physical registers.

## Register allocation

`src/ir/linear_scan.hpp` — `LinearScanAllocator::allocate` walks ranges in order
of first use, expires old intervals and assigns a free physical register,
preferring callee-save registers for call-spanning ranges. Eviction uses the
farthest next use (`use_positions`); spills get 8-byte slots. `--dump-regalloc`
reports vregs, spills and callee-save assignments; `scripts/bench_regalloc.sh`
tracks them on every gate.

## Rewriting and emission

- `src/ir/rewriter.hpp` — `IRRewriter` replaces vregs with physical registers or
  spill slots, inserts spill/fill code, lowers multi-word and FP operations,
  handles stack-passed arguments and generates callee-save prologues/epilogues.
  FP vregs stay stack-resident and each FP operation is lowered through two
  scratch FP registers (D-039).
- `src/ir/emitter.hpp` — `IREmitter` turns rewritten IR into backend calls:
  labels, argument setup, calls (`CALL`/`CALL_INDIRECT`), returns (`RET`/`RET2`),
  debug line markers when `-g` is set, and user-ASM marker labels that keep the
  peephole pass from rewriting their lines.
- `src/ir/target_regs.hpp` — `TargetRegisterInfo` describes the physical register
  file, argument/return registers, reserved registers, struct-ABI flags and FP
  register info for each target.
- `src/ir/dump.hpp` / `src/ir/dump.cpp` — textual IR dumps for `--emit-ir`.

## File map

`src/ir/opcodes.hpp`, `src/ir/value.hpp`, `src/ir/instruction.hpp`,
`src/ir/builder.hpp`, `src/ir/function.hpp`, `src/ir/block.hpp`,
`src/ir/liveness.hpp`, `src/ir/linear_scan.hpp`, `src/ir/rewriter.hpp`,
`src/ir/emitter.hpp`, `src/ir/verify.hpp`, `src/ir/inline.hpp`,
`src/ir/fold.hpp`, `src/ir/cse.hpp`, `src/ir/mem2reg.hpp`,
`src/ir/cleanup.hpp`.
