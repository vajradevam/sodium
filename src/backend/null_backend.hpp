#pragma once

#include "interface.hpp"
#include <ostream>
#include <string>
#include <cstdint>

/// A backend that discards all output. Used by the LSP to run codegen
/// purely for semantic error detection, without depending on any
/// particular target architecture.
class NullBackend : public Backend {
public:
    explicit NullBackend() = default;

    void set_output(std::ostream& os) override { (void)os; }
    std::ostream& output() override { static std::ostream null(nullptr); return null; }

    void push(const std::string& reg) override { (void)reg; }
    void pop(const std::string& reg) override { (void)reg; }
    void adjust_stack(int64_t bytes) override { (void)bytes; }

    void func_prologue() override {}
    void func_epilogue() override {}
    void ret() override {}

    void emit_insn(const std::string& insn, const std::string& ops) override { (void)insn; (void)ops; }
    void load_imm(const std::string& reg, int64_t value) override { (void)reg; (void)value; }
    void load(const std::string& reg, const std::string& addr) override { (void)reg; (void)addr; }
    void store(const std::string& addr, const std::string& reg) override { (void)addr; (void)reg; }
    void mov(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }
    void lea(const std::string& reg, const std::string& addr) override { (void)reg; (void)addr; }

    void load_s8(const std::string& dst, const std::string& addr) override { (void)dst; (void)addr; }
    void load_u8(const std::string& dst, const std::string& addr) override { (void)dst; (void)addr; }
    void load_s16(const std::string& dst, const std::string& addr) override { (void)dst; (void)addr; }
    void load_u16(const std::string& dst, const std::string& addr) override { (void)dst; (void)addr; }
    void load_s32(const std::string& dst, const std::string& addr) override { (void)dst; (void)addr; }
    void load_u32(const std::string& dst, const std::string& addr) override { (void)dst; (void)addr; }

    void store_8(const std::string& addr, const std::string& src) override { (void)addr; (void)src; }
    void store_16(const std::string& addr, const std::string& src) override { (void)addr; (void)src; }
    void store_32(const std::string& addr, const std::string& src) override { (void)addr; (void)src; }

    void movzx(const std::string& dst, const std::string& src, size_t src_bits) override { (void)dst; (void)src; (void)src_bits; }
    void movsx(const std::string& dst, const std::string& src, size_t src_bits) override { (void)dst; (void)src; (void)src_bits; }

    void add(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }
    void sub(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }
    void mul(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }

    void signed_div(const std::string& dst, const std::string& dividend,
                    const std::string& divisor) override { (void)dst; (void)dividend; (void)divisor; }
    void signed_mod(const std::string& dst, const std::string& dividend,
                    const std::string& divisor) override { (void)dst; (void)dividend; (void)divisor; }

    void neg(const std::string& reg) override { (void)reg; }
    void not_(const std::string& reg) override { (void)reg; }
    void xor_(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }
    void and_(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }
    void or_(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }

    void shl(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }
    void shr(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }
    void ashr(const std::string& dst, const std::string& src) override { (void)dst; (void)src; }

    void cmp(const std::string& a, const std::string& b) override { (void)a; (void)b; }
    void test(const std::string& reg, const std::string& mask) override { (void)reg; (void)mask; }

    void jmp(const std::string& label) override { (void)label; }
    void je(const std::string& label) override { (void)label; }
    void jne(const std::string& label) override { (void)label; }
    void jl(const std::string& label) override { (void)label; }
    void jle(const std::string& label) override { (void)label; }
    void jg(const std::string& label) override { (void)label; }
    void jge(const std::string& label) override { (void)label; }
    void jz(const std::string& label) override { (void)label; }
    void jnz(const std::string& label) override { (void)label; }

    void set_cc(const std::string& reg, const std::string& condition) override { (void)reg; (void)condition; }

    void cmp_result(const std::string& dst, const std::string& a,
                    const std::string& b, const std::string& condition) override {
        (void)dst; (void)a; (void)b; (void)condition;
    }

    void call(const std::string& target) override { (void)target; }
    void syscall() override {}

    void label(const std::string& name) override { (void)name; }
    void global_sym(const std::string& name) override { (void)name; }
    void extern_sym(const std::string& name) override { (void)name; }
    void section(const std::string& name) override { (void)name; }

    void dq(const std::string& name, const std::string& value) override { (void)name; (void)value; }
    void db_str(const std::string& label, const std::string& str) override { (void)label; (void)str; }
    void resq(const std::string& name, size_t count) override { (void)name; (void)count; }

    std::string addr_label(const std::string& sym) const override { (void)sym; return ""; }
    std::string addr_reg_offset(const std::string& reg, int offset) const override { (void)reg; (void)offset; return ""; }
    std::string addr_reg(const std::string& reg) const override { (void)reg; return ""; }
    std::string addr_sp(int offset) const override { (void)offset; return ""; }
    std::string addr_fp(int offset) const override { (void)offset; return ""; }
    std::string addr_indexed(const std::string& base, const std::string& index, int scale) const override {
        (void)base; (void)index; (void)scale; return "";
    }
    std::string addr_param(size_t index) const override { (void)index; return ""; }
};
