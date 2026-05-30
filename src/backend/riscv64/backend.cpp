#include "backend.hpp"
#include <sstream>
#include <cstdint>

RISCV64Backend::RISCV64Backend(std::ostream& os)
    : m_output(&os) {}

void RISCV64Backend::set_output(std::ostream& os) {
    m_output = &os;
}

std::ostream& RISCV64Backend::output() {
    return *m_output;
}

// ---- helpers ----

void RISCV64Backend::emit_insn(const std::string& insn, const std::string& ops) {
    if (ops.empty())
        *m_output << "    " << insn << "\n";
    else
        *m_output << "    " << insn << " " << ops << "\n";
}

void RISCV64Backend::emit_rri(const std::string& op, const std::string& rd,
                               const std::string& rs1, const std::string& rs2) {
    emit_insn(op, rd + ", " + rs1 + ", " + rs2);
}

void RISCV64Backend::emit_ri(const std::string& op, const std::string& rd,
                              const std::string& rs1, int64_t imm) {
    emit_insn(op, rd + ", " + rs1 + ", " + std::to_string(imm));
}

// ---- stack ----
// RISC-V has no push/pop; implement via sd/ld + addi

void RISCV64Backend::push(const std::string& reg) {
    emit_insn("addi", "sp, sp, -8");
    emit_insn("sd", reg + ", 0(sp)");
}

void RISCV64Backend::pop(const std::string& reg) {
    emit_insn("ld", reg + ", 0(sp)");
    emit_insn("addi", "sp, sp, 8");
}

void RISCV64Backend::adjust_stack(int64_t bytes) {
    if (bytes == 0) return;
    // Convention: positive = add to sp (shrink), negative = sub from sp (grow)
    emit_insn("addi", "sp, sp, " + std::to_string(bytes));
}

// ---- function frame ----
// Prologue saves ra and s0 at bottom of frame, allocates 16 bytes.
// IREmitter calls adjust_stack afterwards for additional variable space.
void RISCV64Backend::func_prologue() {
    emit_insn("addi", "sp, sp, -16");
    emit_insn("sd", "ra, 8(sp)");
    emit_insn("sd", "s0, 0(sp)");
    emit_insn("addi", "s0, sp, 16");
}

void RISCV64Backend::func_epilogue() {
    emit_insn("ld", "ra, -8(s0)");
    emit_insn("ld", "s0, -16(s0)");
    emit_insn("addi", "sp, s0, 0");
}

void RISCV64Backend::ret() {
    emit_insn("ret");
}

// ---- data movement ----

void RISCV64Backend::load_imm(const std::string& reg, int64_t value) {
    if (value >= -2048 && value <= 2047) {
        // Use addi with x0 for small immediates
        emit_ri("addi", reg, "x0", value);
    } else {
        // Use li pseudo-instruction for larger immediates
        emit_insn("li", reg + ", " + std::to_string(value));
    }
}

void RISCV64Backend::load(const std::string& reg, const std::string& addr) {
    emit_insn("ld", reg + ", " + addr);
}

void RISCV64Backend::store(const std::string& addr, const std::string& reg) {
    emit_insn("sd", reg + ", " + addr);
}

void RISCV64Backend::mov(const std::string& dst, const std::string& src) {
    if (dst != src) {
        emit_insn("mv", dst + ", " + src);  // pseudo: addi dst, src, 0
    }
}

void RISCV64Backend::lea(const std::string& reg, const std::string& addr) {
    // addr is either "offset(reg)" (stack address) or a label name
    // Try to parse as "offset(reg)"
    size_t paren = addr.find('(');
    if (paren != std::string::npos && addr.back() == ')') {
        std::string offset_str = addr.substr(0, paren);
        std::string base = addr.substr(paren + 1, addr.size() - paren - 2);
        // Emit: addi rd, base, offset
        if (offset_str.empty() || offset_str == "0") {
            emit_insn("mv", reg + ", " + base);
        } else {
            emit_rri("addi", reg, base, offset_str);
        }
    } else {
        // Label address: use la pseudo-instruction
        emit_insn("la", reg + ", " + addr);
    }
}

void RISCV64Backend::load_s8(const std::string& dst, const std::string& addr) {
    emit_insn("lb", dst + ", " + addr);
}

void RISCV64Backend::load_u8(const std::string& dst, const std::string& addr) {
    emit_insn("lbu", dst + ", " + addr);
}

void RISCV64Backend::load_s16(const std::string& dst, const std::string& addr) {
    emit_insn("lh", dst + ", " + addr);
}

void RISCV64Backend::load_u16(const std::string& dst, const std::string& addr) {
    emit_insn("lhu", dst + ", " + addr);
}

void RISCV64Backend::load_s32(const std::string& dst, const std::string& addr) {
    emit_insn("lw", dst + ", " + addr);
}

void RISCV64Backend::load_u32(const std::string& dst, const std::string& addr) {
    emit_insn("lwu", dst + ", " + addr);
}

void RISCV64Backend::store_8(const std::string& addr, const std::string& src) {
    emit_insn("sb", src + ", " + addr);
}

void RISCV64Backend::store_16(const std::string& addr, const std::string& src) {
    emit_insn("sh", src + ", " + addr);
}

void RISCV64Backend::store_32(const std::string& addr, const std::string& src) {
    emit_insn("sw", src + ", " + addr);
}

void RISCV64Backend::movzx(const std::string& dst, const std::string& src, size_t src_bits) {
    (void)src_bits;
    // RISC-V loads (lbu/lhu/lwu) already zero-extend. For reg-to-reg:
    // Just a move (already zero-extended by RISC-V convention)
    emit_insn("mv", dst + ", " + src);
}

void RISCV64Backend::movsx(const std::string& dst, const std::string& src, size_t src_bits) {
    (void)src_bits;
    emit_insn("mv", dst + ", " + src);
}

// ---- arithmetic ----

void RISCV64Backend::add(const std::string& dst, const std::string& src) {
    // dst = dst + src — assumes dst already has value
    // For RISC-V: add dst, dst, src
    emit_rri("add", dst, dst, src);
}

void RISCV64Backend::sub(const std::string& dst, const std::string& src) {
    emit_rri("sub", dst, dst, src);
}

void RISCV64Backend::mul(const std::string& dst, const std::string& src) {
    emit_rri("mul", dst, dst, src);
}

void RISCV64Backend::signed_div(const std::string& dst, const std::string& dividend,
                                 const std::string& divisor) {
    // RISC-V: div rd, rs1, rs2 — no fixed register constraints
    // But dst must be one of the operands; div is three-operand
    // We need to do: mov temp, dividend; div dst, temp, divisor
    // But if dividend == dst, we can use directly
    if (dividend == dst) {
        emit_rri("div", dst, dst, divisor);
    } else {
        mov("t0", dividend);
        emit_rri("div", dst, "t0", divisor);
    }
}

void RISCV64Backend::signed_mod(const std::string& dst, const std::string& dividend,
                                 const std::string& divisor) {
    // RISC-V: rem rd, rs1, rs2
    if (dividend == dst) {
        emit_rri("rem", dst, dst, divisor);
    } else {
        mov("t0", dividend);
        emit_rri("rem", dst, "t0", divisor);
    }
}

void RISCV64Backend::neg(const std::string& reg) {
    emit_rri("sub", reg, "x0", reg);  // neg rd, rs = sub rd, x0, rs
}

void RISCV64Backend::not_(const std::string& reg) {
    emit_ri("xori", reg, reg, -1);   // not rd, rs = xori rd, rs, -1
}

void RISCV64Backend::xor_(const std::string& dst, const std::string& src) {
    emit_rri("xor", dst, dst, src);
}

void RISCV64Backend::and_(const std::string& dst, const std::string& src) {
    emit_rri("and", dst, dst, src);
}

void RISCV64Backend::or_(const std::string& dst, const std::string& src) {
    emit_rri("or", dst, dst, src);
}

void RISCV64Backend::shl(const std::string& dst, const std::string& src) {
    // RISC-V: sll rd, rs1, rs2 — no CL constraint
    emit_rri("sll", dst, dst, src);
}

void RISCV64Backend::shr(const std::string& dst, const std::string& src) {
    emit_rri("srl", dst, dst, src);
}

void RISCV64Backend::ashr(const std::string& dst, const std::string& src) {
    emit_rri("sra", dst, dst, src);
}

// ---- comparison ----
// RISC-V has no flags. cmp stores operands; set_cc emits actual compare.

// We store the last cmp operands for use by set_cc
static std::string s_cmp_a, s_cmp_b;

void RISCV64Backend::cmp(const std::string& a, const std::string& b) {
    s_cmp_a = a;
    s_cmp_b = b;
}

void RISCV64Backend::test(const std::string& reg, const std::string& mask) {
    // test reg, mask sets flags based on reg & mask
    // On RISC-V: andi t0, reg, mask; then branch if t0 != 0
    // But we don't emit here — the branch follows immediately
    // Actually, test is used before jz/jnz: test reg,reg; jnz label
    // Store state for the branch
    s_cmp_a = reg;
    s_cmp_b = mask;
}

// ---- branches ----
// RISC-V branches use registers directly, not flags

void RISCV64Backend::jmp(const std::string& label) {
    emit_insn("j", label);  // pseudo: jal x0, label
}

void RISCV64Backend::je(const std::string& label) {
    // After cmp a, b: beq a, b, label
    emit_rri("beq", s_cmp_a, s_cmp_b, label);
}

void RISCV64Backend::jne(const std::string& label) {
    emit_rri("bne", s_cmp_a, s_cmp_b, label);
}

void RISCV64Backend::jl(const std::string& label) {
    // Signed less-than
    emit_rri("blt", s_cmp_a, s_cmp_b, label);
}

void RISCV64Backend::jle(const std::string& label) {
    // a <= b → branch if not (b < a)
    // Use: bge s_cmp_b, s_cmp_a, label  (wait, that's a >= b)
    // Better: not (a > b) → branch if a <= b
    // RISC-V has no ble. Use: bge s_cmp_b, s_cmp_a, label (b >= a → a <= b)
    emit_rri("bge", s_cmp_b, s_cmp_a, label);
}

void RISCV64Backend::jg(const std::string& label) {
    // a > b → b < a
    emit_rri("blt", s_cmp_b, s_cmp_a, label);
}

void RISCV64Backend::jge(const std::string& label) {
    // a >= b
    emit_rri("bge", s_cmp_a, s_cmp_b, label);
}

void RISCV64Backend::jz(const std::string& label) {
    // After test reg, mask: reg & mask == 0
    // If mask is the same as reg (test reg, reg), use beqz
    if (s_cmp_a == s_cmp_b) {
        emit_insn("beqz", s_cmp_a + ", " + label);
    } else {
        emit_insn("and", "t0, " + s_cmp_a + ", " + s_cmp_b);
        emit_insn("beqz", "t0, " + label);
    }
}

void RISCV64Backend::jnz(const std::string& label) {
    // After test reg, reg: reg != 0
    if (s_cmp_a == s_cmp_b) {
        emit_insn("bnez", s_cmp_a + ", " + label);
    } else {
        emit_insn("and", "t0, " + s_cmp_a + ", " + s_cmp_b);
        emit_insn("bnez", "t0, " + label);
    }
}

void RISCV64Backend::set_cc(const std::string& reg, const std::string& condition) {
    // condition is "e", "ne", "l", "le", "g", "ge"
    // On RISC-V, use slt/slti/seqz/snez etc.
    if (condition == "e") {
        // reg = (a == b) ? 1 : 0
        // Use: sub t0, a, b; seqz reg, t0
        emit_rri("sub", "t0", s_cmp_a, s_cmp_b);
        emit_insn("seqz", reg + ", t0");
    } else if (condition == "ne") {
        emit_rri("sub", "t0", s_cmp_a, s_cmp_b);
        emit_insn("snez", reg + ", t0");
    } else if (condition == "l") {
        // a < b (signed)
        emit_rri("slt", reg, s_cmp_a, s_cmp_b);
    } else if (condition == "le") {
        // a <= b → !(b < a)
        emit_rri("slt", "t0", s_cmp_b, s_cmp_a);
        emit_ri("xori", reg, "t0", 1);
    } else if (condition == "g") {
        // a > b → b < a
        emit_rri("slt", reg, s_cmp_b, s_cmp_a);
    } else if (condition == "ge") {
        // a >= b → !(a < b)
        emit_rri("slt", "t0", s_cmp_a, s_cmp_b);
        emit_ri("xori", reg, "t0", 1);
    }
}

// ---- control ----

void RISCV64Backend::call(const std::string& target) {
    // RISC-V: jal ra, target
    emit_insn("jal", "ra, " + target);
}

void RISCV64Backend::syscall() {
    // RISC-V: ecall
    emit_insn("ecall", "");
}

// ---- labels / directives ----

void RISCV64Backend::label(const std::string& name) {
    *m_output << name << ":\n";
}

void RISCV64Backend::global_sym(const std::string& name) {
    *m_output << ".globl " << name << "\n";
}

void RISCV64Backend::extern_sym(const std::string& name) {
    // RISC-V asm uses .extern or just declares the symbol
    *m_output << ".extern " << name << "\n";
}

void RISCV64Backend::section(const std::string& name) {
    *m_output << ".section " << name << "\n";
}

// ---- data ----

void RISCV64Backend::dq(const std::string& name, const std::string& value) {
    *m_output << name << ": .dword " << value << "\n";
}

void RISCV64Backend::db_str(const std::string& label, const std::string& str) {
    *m_output << label << ": .string \"" << str << "\"\n";
}

void RISCV64Backend::resq(const std::string& name, size_t count) {
    *m_output << name << ": .zero " << (count * 8) << "\n";
}

// ---- addressing modes ----

std::string RISCV64Backend::addr_label(const std::string& sym) const {
    // Use the label directly; la pseudo-instruction will handle it
    return sym;
}

std::string RISCV64Backend::addr_reg_offset(const std::string& reg, int offset) const {
    return std::to_string(offset) + "(" + reg + ")";
}

std::string RISCV64Backend::addr_reg(const std::string& reg) const {
    return "0(" + reg + ")";
}

std::string RISCV64Backend::addr_sp(int offset) const {
    return addr_reg_offset("sp", offset);
}

std::string RISCV64Backend::addr_fp(int offset) const {
    return addr_reg_offset("s0", offset);
}

std::string RISCV64Backend::addr_indexed(const std::string& base, const std::string& index, int scale) const {
    // RISC-V doesn't have indexed addressing; compute address manually
    // For now, just use base (caller should compute index*scale separately)
    (void)base;
    (void)index;
    (void)scale;
    return "0(s0)";  // fallback
}

std::string RISCV64Backend::addr_param(size_t index) const {
    // On RISC-V, `jal` does NOT push return address on stack.
    // Args are at [s0 + index*8] where s0 = sp before prologue.
    return addr_fp(static_cast<int>(index) * 8);
}
