#pragma once

#include <string>
#include <cstdint>

/// All IR opcodes.
/// Each opcode takes a fixed number of operands (see instruction.hpp).
enum class IROpcode : uint8_t {
    // ---------- no operands ----------
    NOP,

    // ---------- 1 operand (result) ----------
    /// Load signed/unsigned immediate (variants for different widths)
    LOAD_I64,    // %dst = imm_i64
    LOAD_I32,    // %dst = sign-extended imm_i32
    LOAD_I8,     // %dst = sign-extended imm_i8
    LOAD_U64,    // %dst = imm_u64
    LOAD_U32_IMM,    // %dst = zero-extended imm_u32
    LOAD_U8_IMM,     // %dst = zero-extended imm_u8

    /// Copy one virtual register to another
    COPY,        // %dst = %src

    /// Get the address of a stack variable (allocated in prologue)
    FRAME_ADDR,  // %dst = address of stack slot %frame_slot_index

    // ---------- 2 operands (result, operand1) ----------
    /// Unary operations
    NEG,         // %dst = -%src
    NOT_,        // %dst = ~%src

    /// Zero/sign extend
    ZEXT,        // %dst = zero-extend %src (src is smaller width)
    SEXT,        // %dst = sign-extend %src

    /// Truncate
    TRUNC,       // %dst = truncate %src to smaller width

    // ---------- 3 operands (result, operand1, operand2) ----------
    /// Arithmetic
    ADD,         // %dst = %a + %b
    SUB,         // %dst = %a - %b
    MUL,         // %dst = %a * %b
    DIV,         // %dst = %a / %b (signed)
    MOD,         // %dst = %a % %b (signed)
    AND,         // %dst = %a & %b
    OR,          // %dst = %a | %b
    XOR,         // %dst = %a ^ %b
    SHL,         // %dst = %a << %b
    SHR,         // %dst = %a >> %b (logical)
    ASHR,        // %dst = %a >> %b (arithmetic)

    /// Comparisons (result is i1/i64 containing 0 or 1)
    CMP_EQ,      // %dst = (%a == %b) ? 1 : 0
    CMP_NE,      // %dst = (%a != %b) ? 1 : 0
    CMP_LT,      // %dst = (%a < %b)  ? 1 : 0 (signed)
    CMP_LE,      // %dst = (%a <= %b) ? 1 : 0
    CMP_GT,      // %dst = (%a > %b)  ? 1 : 0
    CMP_GE,      // %dst = (%a >= %b) ? 1 : 0

    // ---------- memory ----------
    /// Load from memory: dst = [base + offset]
    LOAD,        // %dst = load(base_reg, offset_bytes)
    /// Load byte from memory: dst = sign-extend([base + offset])
    LOAD_S8,     // signed byte load
    LOAD_U8,     // unsigned byte load
    LOAD_S16,    // signed 16-bit load
    LOAD_U16,    // unsigned 16-bit load
    LOAD_S32,    // signed 32-bit load
    LOAD_U32,    // unsigned 32-bit load

    /// Store to memory: [base + offset] = src
    STORE,       // store(base_reg, offset_bytes, src_reg)  — 8 bytes
    STORE_8,     // store byte
    STORE_16,    // store 16-bit
    STORE_32,    // store 32-bit

    /// Load effective address: dst = base + offset
    LEA,         // %dst = &base[offset]

    /// Address of a global label: dst = address of label
    LEA_LABEL,   // %dst = address of global_label

    // ---------- control flow ----------
    /// Unconditional branch
    JMP,         // jmp %block_label

    /// Conditional branch
    BR,          // br %cond_reg, %true_block, %false_block

    /// Function call
    CALL,        // %dst = call "func"(arg0, arg1, ...)

    /// Return
    RET,         // ret %val
    RET_VOID,    // ret

    /// Syscall (architecture-specific; the backend handles it)
    SYSCALL,     // syscall (args are in arch-specific registers)
};

/// Convert opcode to human-readable string (for IR dumps).
const char* ir_opcode_name(IROpcode op);
