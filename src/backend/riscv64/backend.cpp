#include "backend.hpp"
#include <sstream>
#include <cstdint>
#include <cctype>

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
    // Emit .option norelax once at the start to prevent the assembler
    // from converting la (PC-relative) to gp-relative addressing.
    // We don't initialize gp in _start, so gp-relative would crash.
    if (!m_norelax_emitted) {
        m_norelax_emitted = true;
        *m_output << ".option norelax\n";
    }
    if (ops.empty())
        *m_output << "    " << insn << "\n";
    else
        *m_output << "    " << insn << " " << ops << "\n";
}

void RISCV64Backend::emit_rri(const std::string& op, const std::string& rd,
                               const std::string& rs1, const std::string& rs2) {
    // Convert literal "0" to "x0" (RISC-V gas doesn't accept bare 0 as register)
    std::string r2 = (rs2 == "0") ? "x0" : rs2;
    std::string r1 = (rs1 == "0") ? "x0" : rs1;
    emit_insn(op, rd + ", " + r1 + ", " + r2);
}

void RISCV64Backend::emit_ri(const std::string& op, const std::string& rd,
                              const std::string& rs1, int64_t imm) {
    emit_insn(op, rd + ", " + rs1 + ", " + std::to_string(imm));
}

/// Check if a string looks like an immediate value (not a register name).
static bool is_imm_str(const std::string& s) {
    if (s.empty()) return false;
    if (s[0] == '-' || s[0] == '+') return s.size() > 1 && std::isdigit(s[1]);
    if (std::isdigit(s[0])) return true;
    return false;
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
    // Restore sp to the frame pointer first, THEN restore registers.
    // Order matters: restoring s0 before sp would clobber the frame pointer,
    // causing sp to be set to the caller's s0 (wrong!).
    emit_insn("addi", "sp, s0, 0");   // sp = s0 = original_sp
    emit_insn("ld", "ra, -8(sp)");    // ra = [original_sp - 8]
    emit_insn("ld", "s0, -16(sp)");   // s0 = [original_sp - 16] = caller's s0
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
    // RISC-V gas doesn't accept bare "0" as a register; use x0
    if (reg == "0") {
        emit_insn("sd", "x0, " + addr);
    } else {
        emit_insn("sd", reg + ", " + addr);
    }
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
    // Zero-extend src (which is a smaller register like al/bl/cl) to dst
    switch (src_bits) {
        case 8:
            emit_ri("andi", dst, src, 0xFF);
            break;
        case 16:
            emit_ri("slli", dst, src, 48);
            emit_ri("srli", dst, dst, 48);
            break;
        case 32:
            // 32-bit ops on RISC-V zero-extend to 64 bits
            emit_insn("mv", dst + ", " + src);
            break;
        default:
            emit_insn("mv", dst + ", " + src);
            break;
    }
}

void RISCV64Backend::movsx(const std::string& dst, const std::string& src, size_t src_bits) {
    // Sign-extend src (a smaller subregister) to full dst width
    switch (src_bits) {
        case 8:
            emit_ri("slli", dst, src, 56);
            emit_ri("srai", dst, dst, 56);
            break;
        case 16:
            emit_ri("slli", dst, src, 48);
            emit_ri("srai", dst, dst, 48);
            break;
        case 32:
            // addiw sign-extends 32-bit result to 64 bits
            emit_ri("addiw", dst, src, 0);
            break;
        default:
            emit_insn("mv", dst + ", " + src);
            break;
    }
}

// ---- arithmetic ----

void RISCV64Backend::add(const std::string& dst, const std::string& src) {
    // dst = dst + src
    if (is_imm_str(src)) {
        emit_ri("addi", dst, dst, std::stoll(src));
    } else {
        emit_rri("add", dst, dst, src);
    }
}

void RISCV64Backend::sub(const std::string& dst, const std::string& src) {
    // dst = dst - src; RISC-V has no subi, use addi with negated immediate
    if (is_imm_str(src)) {
        int64_t val = std::stoll(src);
        if (val == 0) {
            // sub with 0 is a no-op for registers; for immediate 0, do nothing
            // (already handled by is_imm_str)
            return;
        }
        // sub dst, src = addi dst, -src
        emit_ri("addi", dst, dst, -val);
    } else {
        emit_rri("sub", dst, dst, src);
    }
}

void RISCV64Backend::mul(const std::string& dst, const std::string& src) {
    // RISC-V mul requires three registers; load immediate into scratch if needed
    if (is_imm_str(src)) {
        emit_insn("li", m_scratch + ", " + src);
        emit_rri("mul", dst, dst, m_scratch);
    } else {
        emit_rri("mul", dst, dst, src);
    }
}

void RISCV64Backend::signed_div(const std::string& dst, const std::string& dividend,
                                 const std::string& divisor) {
    // RISC-V div is a true three-operand instruction: div rd, rs1, rs2
    // Both source registers are read before the destination is written,
    // so rd may equal rs1 or rs2. No scratch register needed.
    emit_rri("div", dst, dividend, divisor);
}

void RISCV64Backend::signed_mod(const std::string& dst, const std::string& dividend,
                                 const std::string& divisor) {
    // RISC-V rem is also three-operand: rem rd, rs1, rs2
    emit_rri("rem", dst, dividend, divisor);
}

void RISCV64Backend::neg(const std::string& reg) {
    emit_rri("sub", reg, "x0", reg);  // neg rd, rs = sub rd, x0, rs
}

void RISCV64Backend::not_(const std::string& reg) {
    emit_ri("xori", reg, reg, -1);   // not rd, rs = xori rd, rs, -1
}

void RISCV64Backend::xor_(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        emit_ri("xori", dst, dst, std::stoll(src));
    } else {
        emit_rri("xor", dst, dst, src);
    }
}

void RISCV64Backend::and_(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        emit_ri("andi", dst, dst, std::stoll(src));
    } else {
        emit_rri("and", dst, dst, src);
    }
}

void RISCV64Backend::or_(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        emit_ri("ori", dst, dst, std::stoll(src));
    } else {
        emit_rri("or", dst, dst, src);
    }
}

void RISCV64Backend::shl(const std::string& dst, const std::string& src) {
    // RISC-V: sll rd, rs1, rs2 (register) or slli rd, rs1, shamt (immediate)
    if (is_imm_str(src)) {
        emit_ri("slli", dst, dst, std::stoll(src));
    } else {
        emit_rri("sll", dst, dst, src);
    }
}

void RISCV64Backend::shr(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        emit_ri("srli", dst, dst, std::stoll(src));
    } else {
        emit_rri("srl", dst, dst, src);
    }
}

void RISCV64Backend::ashr(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        emit_ri("srai", dst, dst, std::stoll(src));
    } else {
        emit_rri("sra", dst, dst, src);
    }
}

// ---- comparison ----
// RISC-V has no flags. cmp stores operands; set_cc emits actual compare.

void RISCV64Backend::cmp(const std::string& a, const std::string& b) {
    m_cmp_a = a;
    m_cmp_b = b;
}

void RISCV64Backend::test(const std::string& reg, const std::string& mask) {
    // test reg, mask sets flags based on reg & mask
    // On RISC-V: andi scratch, reg, mask; then branch if scratch != 0
    // Store state for the branch
    m_cmp_a = reg;
    m_cmp_b = mask;
}

// ---- branches ----
// RISC-V branches use registers directly, not flags

void RISCV64Backend::jmp(const std::string& label) {
    emit_insn("j", label);  // pseudo: jal x0, label
}

void RISCV64Backend::je(const std::string& label) {
    emit_rri("beq", m_cmp_a, m_cmp_b, label);
}

void RISCV64Backend::jne(const std::string& label) {
    emit_rri("bne", m_cmp_a, m_cmp_b, label);
}

void RISCV64Backend::jl(const std::string& label) {
    emit_rri("blt", m_cmp_a, m_cmp_b, label);
}

void RISCV64Backend::jle(const std::string& label) {
    // a <= b → !(b < a) → use bge b, a (b >= a → a <= b)
    emit_rri("bge", m_cmp_b, m_cmp_a, label);
}

void RISCV64Backend::jg(const std::string& label) {
    // a > b → b < a
    emit_rri("blt", m_cmp_b, m_cmp_a, label);
}

void RISCV64Backend::jge(const std::string& label) {
    emit_rri("bge", m_cmp_a, m_cmp_b, label);
}

void RISCV64Backend::jz(const std::string& label) {
    if (m_cmp_a == m_cmp_b) {
        emit_insn("beqz", m_cmp_a + ", " + label);
    } else {
        emit_insn("and", m_scratch + ", " + m_cmp_a + ", " + m_cmp_b);
        emit_insn("beqz", m_scratch + ", " + label);
    }
}

void RISCV64Backend::jnz(const std::string& label) {
    if (m_cmp_a == m_cmp_b) {
        emit_insn("bnez", m_cmp_a + ", " + label);
    } else {
        emit_insn("and", m_scratch + ", " + m_cmp_a + ", " + m_cmp_b);
        emit_insn("bnez", m_scratch + ", " + label);
    }
}

void RISCV64Backend::set_cc(const std::string& reg, const std::string& condition) {
    if (condition == "e") {
        emit_rri("sub", m_scratch, m_cmp_a, m_cmp_b);
        emit_insn("seqz", reg + ", " + m_scratch);
    } else if (condition == "ne") {
        emit_rri("sub", m_scratch, m_cmp_a, m_cmp_b);
        emit_insn("snez", reg + ", " + m_scratch);
    } else if (condition == "l") {
        emit_rri("slt", reg, m_cmp_a, m_cmp_b);
    } else if (condition == "le") {
        emit_rri("slt", m_scratch, m_cmp_b, m_cmp_a);
        emit_ri("xori", reg, m_scratch, 1);
    } else if (condition == "g") {
        emit_rri("slt", reg, m_cmp_b, m_cmp_a);
    } else if (condition == "ge") {
        emit_rri("slt", m_scratch, m_cmp_a, m_cmp_b);
        emit_ri("xori", reg, m_scratch, 1);
    }
}

void RISCV64Backend::cmp_result(const std::string& dst, const std::string& a,
                                 const std::string& b, const std::string& condition) {
    // Self-contained comparison directly producing a boolean in dst.
    // No dependence on cmp() state — optimal for RISC-V.
    if (condition == "e") {
        // (a == b) → sub t, a, b; seqz dst, t
        emit_rri("sub", m_scratch, a, b);
        emit_insn("seqz", dst + ", " + m_scratch);
    } else if (condition == "ne") {
        emit_rri("sub", m_scratch, a, b);
        emit_insn("snez", dst + ", " + m_scratch);
    } else if (condition == "l") {
        emit_rri("slt", dst, a, b);
    } else if (condition == "le") {
        // a <= b → !(b < a)
        emit_rri("slt", m_scratch, b, a);
        emit_ri("xori", dst, m_scratch, 1);
    } else if (condition == "g") {
        emit_rri("slt", dst, b, a);
    } else if (condition == "ge") {
        // a >= b → !(a < b)
        emit_rri("slt", m_scratch, a, b);
        emit_ri("xori", dst, m_scratch, 1);
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
    // RISC-V prologue saves ra at s0-8 and old s0 at s0-16.
    // Local variables go below the saved frame (s0-16 and lower),
    // so adjust the offset by -16 to skip the prologue frame.
    // This matches x86-64 where rbp points to the saved rbp slot.
    return addr_reg_offset("s0", offset - 16);
}

std::string RISCV64Backend::addr_indexed(const std::string& base, const std::string& index, int scale) const {
    // RISC-V doesn't have indexed addressing; return base + index*scale
    // is not representable as a single string. Fallback: just use base.
    // Caller should compute the address with separate add/mul instructions.
    (void)index;
    (void)scale;
    return "0(" + base + ")";
}

std::string RISCV64Backend::addr_param(size_t index) const {
    // On RISC-V, `jal` does NOT push return address on stack.
    // Args are at [s0 + index*8] where s0 = sp before prologue.
    return addr_fp(static_cast<int>(index) * 8);
}
