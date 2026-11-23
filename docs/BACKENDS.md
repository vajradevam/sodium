# Backends — implementation reference

This document describes how IR becomes assembly, verified against the backend
sources by `scripts/check_arch_docs.sh`. The shared contract is
`src/backend/interface.hpp`; the two real implementations are
`src/backend/x86_64/backend.cpp` (`X8664Backend`) and
`src/backend/riscv64/backend.cpp` (`RISCV64Backend`). A no-op implementation,
`NullBackend` (`src/backend/null_backend.hpp`), lets the LSP run codegen for
semantic diagnostics without producing output. `src/backend/peephole.hpp`
contains the post-emission optimizer.

## The Backend contract

`Backend` is a low-level assembly-emission API; `IREmitter` is its only caller in
normal compilation. It provides:

- output/target: `set_output`, `output`, `target_name`, `emit_insn`;
- stack and frames: `push`, `push_frame_offset`, `pop`, `adjust_stack`,
  `func_prologue`, `func_epilogue`, `ret`, `stack_probe_check`;
- data movement: `load_imm`, `load`, `store`, `mov`, `lea`, the width-specific
  `load_s8`/`load_u8`/`load_s16`/`load_u16`/`load_s32`/`load_u32` and
  `store_8`/`store_16`/`store_32`, `movzx`, `movsx`;
- arithmetic and compares: `add`, `sub`, `mul`, `signed_div`, `signed_mod`,
  `neg`, `not_`, `xor_`, `and_`, `or_`, `shl`, `shr`, `ashr`, `cmp`, `test`,
  `set_cc`, `cmp_result`;
- control flow/calls: `jmp`, `je`, `jne`, `jl`, `jle`, `jg`, `jge`, `jz`, `jnz`,
  `call`, `call_reg`, `syscall`;
- floating point: `fp_load`, `fp_store`, `fp_const`, `fp_arith`, `fp_neg`,
  `fp_cmp`, `int_to_fp`, `fp_to_int`, `fp_convert`, `note_fp_arg_count`;
- debug/labels/data: `debug_line`, `label`, `global_sym`, `extern_sym`,
  `section`, `dq`, `db_str`, `resq`;
- addressing: `addr_label`, `addr_reg_offset`, `addr_reg`, `addr_sp`, `addr_fp`,
  `addr_indexed`, `addr_param`.

`fp_arith` takes an operator character (`+ - * /`), destination and two sources
with their bit widths; `note_fp_arg_count` records the number of FP arguments for
the x86-64 `al` convention. Addressing helpers return target syntax, e.g.
`[rel sym]` / `sym(%rip)` for labels and scaled index forms for `addr_indexed`.

## x86-64 backend

Assembly is NASM (`-felf64`, linked with `ld`). `func_prologue` emits
`push rbp; mov rbp, rsp`; `func_epilogue` reverses it. Integers use `rax`…`r15`
with the System V argument registers; division uses `idiv` and `signed_div`/
`signed_mod` save/restore the count register for variable shifts. FP uses SSE:
`xmm0`–`xmm7` for arguments, `addsd`/`mulsd`/`divsd` (and `f32` variants),
`cvtsi2sd`/`cvttsd2si`, and `movq` for constants through the integer scratch.

`stack_probe_check` compares `rsp` with `__sodium_stack_limit` and branches to a
failure path that prints a message and calls `_sodium_exit(70)`; the message is
passed in `rdi` to `_sodium_print_raw_str`. `debug_line` emits NASM
`%line N+0 "file"`, assembled with `nasm -g -F dwarf` when `-g` is given. With
inline asm, the emitter surrounds user lines with marker labels so
`peephole_optimize` leaves them verbatim.

## riscv64 backend

Assembly is GNU as (`-march=rv64gc -mno-relax`, linked with `riscv64-elf-gcc
-nostdlib -static -Wl,--no-relax`). `.option norelax` is emitted once because the
compiler does not initialize `gp`. `func_prologue` saves `ra` and `s0` and sets
`s0` as the frame pointer (`fp_bias` accounts for the extra saved words). Large
frame offsets use `li` + `add` because `addi` has only a 12-bit immediate;
`addr_indexed` computes effective addresses directly in the destination register
so live values are not clobbered.

FP uses the RISC-V D/F extensions: `fld`/`fsw`, `fadd.d`/`fmul.d` (and `.s`
variants), `fcvt.*`, `fmv.x.d`/`fmv.d.x`; `fa0`–`fa7` carry FP arguments and
`note_fp_arg_count` is a no-op. `stack_probe_check` uses `bge sp, limit, ok`.
`debug_line` emits GAS `.file N "file"` and `.loc N L 0`, with `-g` passed to the
assembler.

## Post-emission peephole

`peephole_optimize(assembly, target_name)` (`src/backend/peephole.hpp`) runs after
code generation under `-O1`. It removes self-moves (`mov r,r`, `mv r,r`, `addi
r,r,0`) and adjacent dead moves, processing segments independently between
labels/directives. Memory operands, flags and user-ASM marker ranges are never
touched, and the pass reports counts through `--dump-regalloc`.
