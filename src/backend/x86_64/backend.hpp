#pragma once

#include "../interface.hpp"
#include <iostream>
#include <ostream>
#include <string>
#include <cstdint>

/// x86-64 backend: emits NASM-syntax assembly for the x86-64 architecture.
class X8664Backend : public Backend {
public:
    explicit X8664Backend(std::ostream& os = std::cout);

    void set_output(std::ostream& os) override;
    std::ostream& output() override;

    // Stack
    void push(const std::string& reg) override;
    void pop(const std::string& reg) override;
    void adjust_stack(int64_t bytes) override;

    // Function frame
    void func_prologue() override;
    void func_epilogue() override;
    void ret() override;

    // Data movement
    void load_imm(const std::string& reg, int64_t value) override;
    void load(const std::string& reg, const std::string& addr) override;
    void store(const std::string& addr, const std::string& reg) override;
    void mov(const std::string& dst, const std::string& src) override;
    void lea(const std::string& reg, const std::string& addr) override;
    void load_s8(const std::string& dst, const std::string& addr) override;
    void load_u8(const std::string& dst, const std::string& addr) override;
    void load_s16(const std::string& dst, const std::string& addr) override;
    void load_u16(const std::string& dst, const std::string& addr) override;
    void load_s32(const std::string& dst, const std::string& addr) override;
    void load_u32(const std::string& dst, const std::string& addr) override;
    void store_8(const std::string& addr, const std::string& src) override;
    void store_16(const std::string& addr, const std::string& src) override;
    void store_32(const std::string& addr, const std::string& src) override;
    void movzx(const std::string& dst, const std::string& src, size_t src_bits) override;
    void movsx(const std::string& dst, const std::string& src, size_t src_bits) override;

    // Arithmetic
    void add(const std::string& dst, const std::string& src) override;
    void sub(const std::string& dst, const std::string& src) override;
    void mul(const std::string& dst, const std::string& src) override;
    void signed_div(const std::string& dst, const std::string& dividend,
                    const std::string& divisor) override;
    void signed_mod(const std::string& dst, const std::string& dividend,
                    const std::string& divisor) override;
    void neg(const std::string& reg) override;
    void not_(const std::string& reg) override;
    void xor_(const std::string& dst, const std::string& src) override;
    void and_(const std::string& dst, const std::string& src) override;
    void or_(const std::string& dst, const std::string& src) override;
    void shl(const std::string& dst, const std::string& src) override;
    void shr(const std::string& dst, const std::string& src) override;
    void ashr(const std::string& dst, const std::string& src) override;

    // Comparison
    void cmp(const std::string& a, const std::string& b) override;
    void test(const std::string& reg, const std::string& mask) override;

    // Branches
    void jmp(const std::string& label) override;
    void je(const std::string& label) override;
    void jne(const std::string& label) override;
    void jl(const std::string& label) override;
    void jle(const std::string& label) override;
    void jg(const std::string& label) override;
    void jge(const std::string& label) override;
    void jz(const std::string& label) override;
    void jnz(const std::string& label) override;
    void set_cc(const std::string& reg, const std::string& condition) override;

    // Control
    void call(const std::string& target) override;
    void syscall() override;

    // Generic instruction emission
    void emit_insn(const std::string& insn, const std::string& ops = "") override;

    // Labels / directives
    void label(const std::string& name) override;
    void global_sym(const std::string& name) override;
    void extern_sym(const std::string& name) override;
    void section(const std::string& name) override;

    // Data
    void dq(const std::string& name, const std::string& value) override;
    void db_str(const std::string& label, const std::string& str) override;
    void resq(const std::string& name, size_t count) override;

    // Addressing modes
    std::string addr_label(const std::string& sym) const override;
    std::string addr_reg_offset(const std::string& reg, int offset) const override;
    std::string addr_reg(const std::string& reg) const override;
    std::string addr_sp(int offset) const override;
    std::string addr_fp(int offset) const override;
    std::string addr_indexed(const std::string& base, const std::string& index, int scale) const override;
    std::string addr_param(size_t index) const override;

private:
    std::ostream* m_output;

    /// Helper: emit `cqo` (sign-extend rax into rdx:rax for idiv).
    void emit_cqo();
};
