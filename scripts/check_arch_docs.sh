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
