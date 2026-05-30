#pragma once

#include <ostream>
#include <string>
#include <vector>
#include <cstdint>

/// Abstract interface for code generation backends.
/// Each architecture (x86-64, RISC-V, etc.) implements this interface.
///
/// This is a low-level assembly emission API. The IREmitter translates
/// physical-register IR instructions into one or more Backend calls.
class Backend {
public:
    virtual ~Backend() = default;

    /// Set the output stream that assembly is written to.
    virtual void set_output(std::ostream& os) = 0;

    /// Get the output stream.
    virtual std::ostream& output() = 0;

    /// Emit a raw instruction with optional operands (generic fallback).
    virtual void emit_insn(const std::string& insn, const std::string& ops = "") = 0;

    // ----------------------------------------------------------------
    // Stack management
    // ----------------------------------------------------------------

    /// Push register value onto stack.
    virtual void push(const std::string& reg) = 0;

    /// Pop register value from stack.
    virtual void pop(const std::string& reg) = 0;

    /// Adjust stack pointer by `bytes` (positive = add to sp / shrink,
    /// negative = sub from sp / grow).
    virtual void adjust_stack(int64_t bytes) = 0;

    // ----------------------------------------------------------------
    // Function prologue / epilogue
    // ----------------------------------------------------------------

    /// Emit function entry (save frame pointer, set up stack frame).
    virtual void func_prologue() = 0;

    /// Emit function exit (restore frame pointer, return).
    virtual void func_epilogue() = 0;

    /// Emit a return instruction.
    virtual void ret() = 0;

    // ----------------------------------------------------------------
    // Data movement
    // ----------------------------------------------------------------

    /// Load immediate value into register.
    virtual void load_imm(const std::string& reg, int64_t value) = 0;

    /// Load from memory into register (default 8 bytes).
    virtual void load(const std::string& reg, const std::string& addr) = 0;

    /// Store register to memory (default 8 bytes).
    virtual void store(const std::string& addr, const std::string& reg) = 0;

    /// Register-to-register copy.
    virtual void mov(const std::string& dst, const std::string& src) = 0;

    /// Load effective address: dst = &addr (lea or addi).
    virtual void lea(const std::string& reg, const std::string& addr) = 0;

    // ---- Width-specific loads (sign/zero extending) ----

    /// Load signed byte from memory, sign-extend to full register width.
    virtual void load_s8(const std::string& dst, const std::string& addr) = 0;
    /// Load unsigned byte from memory, zero-extend to full register width.
    virtual void load_u8(const std::string& dst, const std::string& addr) = 0;
    /// Load signed 16-bit from memory, sign-extend to full register width.
    virtual void load_s16(const std::string& dst, const std::string& addr) = 0;
    /// Load unsigned 16-bit from memory, zero-extend to full register width.
    virtual void load_u16(const std::string& dst, const std::string& addr) = 0;
    /// Load signed 32-bit from memory, sign-extend to full register width.
    virtual void load_s32(const std::string& dst, const std::string& addr) = 0;
    /// Load unsigned 32-bit from memory, zero-extend to full register width.
    virtual void load_u32(const std::string& dst, const std::string& addr) = 0;

    // ---- Width-specific stores ----

    /// Store 8-bit (byte).
    virtual void store_8(const std::string& addr, const std::string& src) = 0;
    /// Store 16-bit (halfword).
    virtual void store_16(const std::string& addr, const std::string& src) = 0;
    /// Store 32-bit (word).
    virtual void store_32(const std::string& addr, const std::string& src) = 0;

    /// Zero-extend load from smaller size.
    virtual void movzx(const std::string& dst, const std::string& src, size_t src_bits) = 0;

    /// Sign-extend load from smaller size.
    virtual void movsx(const std::string& dst, const std::string& src, size_t src_bits) = 0;

    // ----------------------------------------------------------------
    // Arithmetic (two-operand: dst = dst OP src, except where noted)
    // ----------------------------------------------------------------

    virtual void add(const std::string& dst, const std::string& src) = 0;
    virtual void sub(const std::string& dst, const std::string& src) = 0;
    virtual void mul(const std::string& dst, const std::string& src) = 0;

    /// Signed integer division: quotient → dst, remainder → tmp
    /// (x86-64 uses fixed dividend registers; RISC-V has div/rem)
    virtual void signed_div(const std::string& dst, const std::string& dividend,
                            const std::string& divisor) = 0;
    /// Signed integer modulo: remainder → dst
    virtual void signed_mod(const std::string& dst, const std::string& dividend,
                            const std::string& divisor) = 0;

    virtual void neg(const std::string& reg) = 0;
    virtual void not_(const std::string& reg) = 0;
    virtual void xor_(const std::string& dst, const std::string& src) = 0;
    virtual void and_(const std::string& dst, const std::string& src) = 0;
    virtual void or_(const std::string& dst, const std::string& src) = 0;

    /// Shift left: dst = dst << src (src should be a register or immediate)
    virtual void shl(const std::string& dst, const std::string& src) = 0;
    /// Logical shift right: dst = dst >> src (zero-fill)
    virtual void shr(const std::string& dst, const std::string& src) = 0;
    /// Arithmetic shift right: dst = dst >> src (sign-extend)
    virtual void ashr(const std::string& dst, const std::string& src) = 0;

    // ----------------------------------------------------------------
    // Comparison (set condition flags or produce boolean in register)
    // ----------------------------------------------------------------

    /// Compare a with b (sets flags or equivalent).
    virtual void cmp(const std::string& a, const std::string& b) = 0;

    /// Test reg & mask (sets flags or equivalent).
    virtual void test(const std::string& reg, const std::string& mask) = 0;

    // ----------------------------------------------------------------
    // Branches
    // ----------------------------------------------------------------

    virtual void jmp(const std::string& label) = 0;
    virtual void je(const std::string& label) = 0;
    virtual void jne(const std::string& label) = 0;
    virtual void jl(const std::string& label) = 0;
    virtual void jle(const std::string& label) = 0;
    virtual void jg(const std::string& label) = 0;
    virtual void jge(const std::string& label) = 0;
    virtual void jz(const std::string& label) = 0;
    virtual void jnz(const std::string& label) = 0;

    /// Set byte to 0/1 based on condition (sete/setne/setl/etc, or slt/slti).
    virtual void set_cc(const std::string& reg, const std::string& condition) = 0;

    // ----------------------------------------------------------------
    // Control flow
    // ----------------------------------------------------------------

    /// Call a function or runtime helper.
    virtual void call(const std::string& target) = 0;

    /// Emit a software interrupt / syscall instruction.
    virtual void syscall() = 0;

    // ----------------------------------------------------------------
    // Labels and directives
    // ----------------------------------------------------------------

    /// Emit a global label (e.g., "func_name:").
    virtual void label(const std::string& name) = 0;

    /// Emit `global name` directive for ELF symbol visibility.
    virtual void global_sym(const std::string& name) = 0;

    /// Emit `extern name` directive.
    virtual void extern_sym(const std::string& name) = 0;

    /// Switch to an ELF section (e.g., .text, .data, .rodata, .bss).
    virtual void section(const std::string& name) = 0;

    // ----------------------------------------------------------------
    // Data definitions
    // ----------------------------------------------------------------

    /// Define a qword (8-byte) data entry: name: dq value
    virtual void dq(const std::string& name, const std::string& value) = 0;

    /// Define a zero-terminated string in .rodata: label: db "value", 0
    virtual void db_str(const std::string& label, const std::string& str) = 0;

    /// Reserve N qwords in .bss: name: resq N
    virtual void resq(const std::string& name, size_t count) = 0;

    // ----------------------------------------------------------------
    // Addressing mode helpers (return string representations)
    // ----------------------------------------------------------------

    /// Memory reference via label: [rel sym] or just sym
    virtual std::string addr_label(const std::string& sym) const = 0;

    /// Memory reference via base register + offset: [reg + offset] / offset(reg)
    virtual std::string addr_reg_offset(const std::string& reg, int offset) const = 0;

    /// Memory reference via register: [reg] or 0(reg)
    virtual std::string addr_reg(const std::string& reg) const = 0;

    /// Memory reference for stack slot
    virtual std::string addr_sp(int offset) const = 0;

    /// Memory reference for frame slot
    virtual std::string addr_fp(int offset) const = 0;

    /// Indexed addressing: [base + index * scale] (x86-64), not used on RISC-V
    virtual std::string addr_indexed(const std::string& base, const std::string& index, int scale) const = 0;

    /// Return address above frame pointer (for function parameters).
    virtual std::string addr_param(size_t index) const = 0;
};
