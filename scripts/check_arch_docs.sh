#!/bin/bash
# D2: verify the architecture docs against the source they describe.
# Every required identifier must appear both in the named documentation file
# and in the implementation, so the docs cannot drift silently from the code.
set -u
cd "$(dirname "$0")/.."

fail() { echo "arch docs: $*" >&2; exit 1; }

# check_claims <doc> <source-glob> <label> <claim>...
check_claims() {
    local doc="$1"; shift
    local src_glob="$1"; shift
    local label="$1"; shift
    [ -f "$doc" ] || fail "$doc is missing"
    local claim
    for claim in "$@"; do
        grep -q -- "$claim" "$doc" || fail "$label does not mention '$claim'"
        # shellcheck disable=SC2086
        grep -Rq -- "$claim" $src_glob || fail "source does not contain '$claim' (claimed in $label)"
    done
}

# ---- docs/IR.md: opcodes, passes and IR headers ----
IR_OPCODES=(
    NOP LOAD_I64 COPY FRAME_ADDR ADD DIV MOD CMP_EQ LOAD STORE LEA
    JMP BR CALL CALL_INDIRECT LOAD_PARAM STORE_PARAM RET RET2 RET_VOID
    SYSCALL PUSH POP ASM FCONST FADD FCMP_EQ I2F F2I
)
check_claims docs/IR.md 'src/ir/*.hpp' 'IR.md' "${IR_OPCODES[@]}"

IR_FILES=(
    src/ir/opcodes.hpp src/ir/value.hpp src/ir/instruction.hpp src/ir/builder.hpp
    src/ir/function.hpp src/ir/block.hpp src/ir/liveness.hpp src/ir/linear_scan.hpp
    src/ir/rewriter.hpp src/ir/emitter.hpp src/ir/verify.hpp src/ir/inline.hpp
    src/ir/fold.hpp src/ir/cse.hpp src/ir/mem2reg.hpp src/ir/cleanup.hpp
)
for file in "${IR_FILES[@]}"; do
    [ -f "$file" ] || fail "IR.md references missing file $file"
    grep -q -- "$file" docs/IR.md || fail "IR.md does not mention $file"
done

IR_SYMBOLS=(
    verify_function fold_function promote_locals cse_function cleanup_function
    compute_live_ranges LinearScanAllocator IRRewriter IREmitter inline_leaves
)
check_claims docs/IR.md 'src/ir/*.hpp src/gen/*.cpp' 'IR.md' "${IR_SYMBOLS[@]}"

echo "Arch docs: IR.md claims verified against source"

# ---- docs/ABI.md: calling-convention symbols, registers and decisions ----
ABI_SYMBOLS=(
    TargetRegisterInfo ArgPlacer assign_arg_regs FP_PARAM_ORDINAL_BASE
    STORE_PARAM RET2 split_struct_args large_struct_by_ref fp_overflow_to_gpr
    fp_arg_regs fp_ret_reg arg_regs ret_reg2 call_target_reg
)
check_claims docs/ABI.md 'src/ir/target_regs.hpp src/generation.hpp src/ir/rewriter.hpp src/ir/emitter.hpp' 'ABI.md' "${ABI_SYMBOLS[@]}"

ABI_REGS=(rdi rsi rdx rcx rax rbp a0 a7 s0 t0)
check_claims docs/ABI.md 'src/ir/target_regs.hpp' 'ABI.md' "${ABI_REGS[@]}"

for decision in D-001 D-012 D-014 D-038 D-040; do
    grep -q -- "$decision" docs/ABI.md || fail "ABI.md does not mention $decision"
    grep -q -- "$decision" docs/DECISIONS.md || fail "DECISIONS.md does not contain $decision"
done

# ---- docs/BACKENDS.md: backend interface, implementations and drivers ----
BACKEND_FILES=(
    src/backend/interface.hpp src/backend/null_backend.hpp src/backend/peephole.hpp
    src/backend/x86_64/backend.cpp src/backend/riscv64/backend.cpp
)
for file in "${BACKEND_FILES[@]}"; do
    [ -f "$file" ] || fail "BACKENDS.md references missing file $file"
    grep -q -- "$file" docs/BACKENDS.md || fail "BACKENDS.md does not mention $file"
done

BACKEND_SYMBOLS=(
    emit_insn func_prologue func_epilogue debug_line stack_probe_check
    load_imm signed_div fp_arith call_reg addr_indexed note_fp_arg_count
    NullBackend X8664Backend RISCV64Backend peephole_optimize
)
check_claims docs/BACKENDS.md 'src/backend/*.hpp src/backend/x86_64/*.cpp src/backend/riscv64/*.cpp' 'BACKENDS.md' "${BACKEND_SYMBOLS[@]}"

echo "Arch docs: ABI.md and BACKENDS.md claims verified against source"
