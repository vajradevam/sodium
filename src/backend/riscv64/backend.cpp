#include "backend.hpp"
#include <unordered_set>
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

void RISCV64Backend::debug_line(const std::string& file, uint32_t line) {
    auto it = m_debug_files.find(file);
    int file_id = 0;
    if (it == m_debug_files.end()) {
        file_id = static_cast<int>(m_debug_files.size()) + 1;
        m_debug_files.emplace(file, file_id);
        std::string escaped;
        for (char c : file) {
            if (c == '\\' || c == '"') escaped += '\\';
            escaped += c;
        }
        emit_insn(".file", std::to_string(file_id) + " \"" + escaped + "\"");
    } else {
        file_id = it->second;
    }
    emit_insn(".loc", std::to_string(file_id) + " " + std::to_string(line) + " 0");
}

// Loads and stores take a 12-bit signed displacement. Frames with many spills can exceed it
// (BF-011), so such an access is rewritten as `li tmp, off; add tmp, tmp, base; op reg, 0(tmp)`.
// tmp is a register the instruction does not otherwise use; when none of t0/the data register
// is free (a store of t0, or base == t0), t1/t2 is borrowed and restored from just below sp.
bool RISCV64Backend::emit_wide_mem(const std::string& insn, const std::string& ops) {
    static const std::unordered_set<std::string> mem_ops = {
        "ld", "sd", "lb", "lbu", "lh", "lhu", "lw", "lwu", "sb", "sh", "sw",
        "fld", "fsd", "flw", "fsw"};
    if (!mem_ops.contains(insn)) return false;
    const size_t comma = ops.find(", ");
    if (comma == std::string::npos) return false;
    const std::string reg = ops.substr(0, comma);
    const std::string addr = ops.substr(comma + 2);
    const size_t open = addr.find('(');
    if (open == std::string::npos || addr.back() != ')' || open == 0) return false;
    int64_t off = 0;
    try { off = std::stoll(addr.substr(0, open)); } catch (...) { return false; }
    if (off >= -2048 && off <= 2047) return false;
    const std::string base = addr.substr(open + 1, addr.size() - open - 2);

    const bool is_load = insn[0] == 'l' || insn == "fld" || insn == "flw";
    const bool fp_data = insn[0] == 'f';
    std::string tmp;
    if (is_load && !fp_data && reg != base) tmp = reg;       // the destination can hold the address
    else if (reg != "t0" && base != "t0") tmp = "t0";
    if (!tmp.empty()) {
        emit_insn("li", tmp + ", " + std::to_string(off));
        emit_insn("add", tmp + ", " + tmp + ", " + base);
        emit_insn(insn, reg + ", 0(" + tmp + ")");
        return true;
    }
    const std::string spare = (reg != "t1" && base != "t1") ? "t1" : "t2";
    emit_insn("sd", spare + ", -8(sp)");
    emit_insn("li", spare + ", " + std::to_string(off));
    emit_insn("add", spare + ", " + spare + ", " + base);
    emit_insn(insn, reg + ", 0(" + spare + ")");
    emit_insn("ld", spare + ", -8(sp)");
    return true;
}

void RISCV64Backend::emit_insn(const std::string& insn, const std::string& ops) {
    if (emit_wide_mem(insn, ops)) return;
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

void RISCV64Backend::push_frame_offset(int offset) {
    std::string tmp = "t0";
    emit_insn("ld", tmp + ", " + std::to_string(offset) + "(s0)");
    emit_insn("addi", "sp, sp, -8");
    emit_insn("sd", tmp + ", 0(sp)");
}

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
    // addi only supports 12-bit signed immediates (-2048..2047).
    // For larger values, load into a register and use add/sub.
    if (bytes >= -2048 && bytes <= 2047) {
        emit_insn("addi", "sp, sp, " + std::to_string(bytes));
    } else {
        // Use a temporary register (t0) for the offset
        emit_insn("li", "t0, " + std::to_string(bytes));
        emit_insn("add", "sp, sp, t0");
    }
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

void RISCV64Backend::stack_probe_check(const std::string& limit_label,
                                       const std::string& message_label,
                                       const std::string& ok_label,
                                       const std::string& fail_label) {
    // t0/t1/a0 are caller-saved; the probe runs before any live IR value is
    // loaded, so clobbering them here is safe.
    emit_insn("la", "t0, " + limit_label);
    emit_insn("ld", "t1, 0(t0)");
    emit_insn("bge", "sp, t1, " + ok_label);
    label(fail_label);
    emit_insn("la", "a0, " + message_label);
    emit_insn("call", "_sodium_print_raw_str");
    emit_insn("li", "a0, 70");
    emit_insn("call", "_sodium_exit");
    label(ok_label);
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
        if (is_imm_str(src)) {
            emit_insn("li", dst + ", " + src);
        } else {
            emit_insn("mv", dst + ", " + src);  // pseudo: addi dst, src, 0
        }
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
            // addi only supports 12-bit signed immediates (-2048..2047)
            int64_t offset = std::stoll(offset_str);
            if (offset >= -2048 && offset <= 2047) {
                emit_insn("addi", reg + ", " + base + ", " + offset_str);
            } else {
                // Large offset: load into the destination register
                // and add base to it. This avoids clobbering any other
                // register (unlike using a hardcoded temp like t1).
                emit_insn("li", reg + ", " + offset_str);
                emit_insn("add", reg + ", " + base + ", " + reg);
            }
        }
    } else {
        // Label address: use la pseudo-instruction
        emit_insn("la", reg + ", " + addr);
    }
}

void RISCV64Backend::load_s8(const std::string& dst, const std::string& addr) {
    // 1-byte, no direction issue
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
    // 1-byte, no direction issue
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
            // RISC-V: zero-extend 32-bit to 64 bits via left-right shift
            emit_ri("slli", dst, src, 32);
            emit_ri("srli", dst, dst, 32);
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

// Runtime guards shared by signed div/rem: exit(1) on a zero divisor; and INT64_MIN / -1
// exits 136 (BF-009). RISC-V defines that quotient as INT64_MIN instead of trapping, while
// x86_64 raises SIGFPE (status 136), so the two targets are made to agree (D-023).
void RISCV64Backend::emit_div_checks(const std::string& dividend, const std::string& divisor) {
    const std::string id = std::to_string(m_div_label_counter++);
    const std::string ok = ".Ldiv_ok_" + id;
    const std::string safe = ".Ldiv_safe_" + id;
    emit_insn("bnez", divisor + ", " + ok);
    emit_insn("li", "a0, 1");
    emit_insn("call", "_sodium_exit");
    label(ok);
    emit_ri("addi", m_scratch, divisor, 1);
    emit_insn("bnez", m_scratch + ", " + safe);
    emit_insn("li", m_scratch + ", 1");
    emit_ri("slli", m_scratch, m_scratch, 63);
    emit_insn("bne", dividend + ", " + m_scratch + ", " + safe);
    emit_insn("li", "a0, 136");
    emit_insn("call", "_sodium_exit");
    label(safe);
}

void RISCV64Backend::signed_div(const std::string& dst, const std::string& dividend,
                                 const std::string& divisor) {
    // RISC-V div is a true three-operand instruction: div rd, rs1, rs2
    // Both source registers are read before the destination is written,
    // so rd may equal rs1 or rs2. No scratch register needed.
    emit_div_checks(dividend, divisor);
    emit_rri("div", dst, dividend, divisor);
}

void RISCV64Backend::signed_mod(const std::string& dst, const std::string& dividend,
                                 const std::string& divisor) {
    // RISC-V rem is also three-operand: rem rd, rs1, rs2
    emit_div_checks(dividend, divisor);
    emit_rri("rem", dst, dividend, divisor);
}

void RISCV64Backend::unsigned_div(const std::string& dst, const std::string& dividend,
                                  const std::string& divisor) {
    std::string ok = ".Ldiv_ok_" + std::to_string(m_div_label_counter++);
    emit_insn("bnez", divisor + ", " + ok);
    emit_insn("li", "a0, 1");
    emit_insn("call", "_sodium_exit");
    label(ok);
    emit_rri("divu", dst, dividend, divisor);
}

void RISCV64Backend::unsigned_mod(const std::string& dst, const std::string& dividend,
                                  const std::string& divisor) {
    std::string ok = ".Ldiv_ok_" + std::to_string(m_div_label_counter++);
    emit_insn("bnez", divisor + ", " + ok);
    emit_insn("li", "a0, 1");
    emit_insn("call", "_sodium_exit");
    label(ok);
    emit_rri("remu", dst, dividend, divisor);
}

void RISCV64Backend::neg(const std::string& reg) {
    emit_rri("sub", reg, "x0", reg);  // neg rd, rs = sub rd, x0, rs
}

void RISCV64Backend::not_(const std::string& reg) {
    emit_ri("xori", reg, reg, -1);   // not rd, rs = xori rd, rs, -1
}

void RISCV64Backend::xor_(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        const int64_t imm = std::stoll(src);
        // xori/ori take a 12-bit signed immediate; larger values go through the scratch register.
        if (imm >= -2048 && imm <= 2047) {
            emit_ri("xori", dst, dst, imm);
        } else {
            emit_insn("li", "t0, " + std::to_string(imm));
            emit_rri("xor", dst, dst, "t0");
        }
    } else {
        emit_rri("xor", dst, dst, src);
    }
}

void RISCV64Backend::and_(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        int64_t imm = std::stoll(src);
        // RISC-V: andi only accepts a 12-bit sign-extended immediate (-2048..2047).
        // For larger values, load into t0 and use register-register and.
        if (imm >= -2048 && imm <= 2047) {
            emit_ri("andi", dst, dst, imm);
        } else {
            emit_insn("li", "t0, " + std::to_string(imm));
            emit_rri("and", dst, dst, "t0");
        }
    } else {
        emit_rri("and", dst, dst, src);
    }
}

void RISCV64Backend::or_(const std::string& dst, const std::string& src) {
    if (is_imm_str(src)) {
        const int64_t imm = std::stoll(src);
        if (imm >= -2048 && imm <= 2047) {
            emit_ri("ori", dst, dst, imm);
        } else {
            emit_insn("li", "t0, " + std::to_string(imm));
            emit_rri("or", dst, dst, "t0");
        }
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
    // `jal ra, target` has a +-1 MiB range, which large stack frames can
    // exceed (R_RISCV_JAL relocation truncated). The `call` pseudo expands
    // to auipc+jalr and works at any distance, including with --no-relax.
    emit_insn("call", target);
}

void RISCV64Backend::call_reg(const std::string& reg) {
    emit_insn("jalr", "ra, 0(" + reg + ")");
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
    // GAS ignores `.extern`; `.globl` makes a function visible to the linker (and
    // is harmless for an undefined reference), matching the x86_64 behaviour.
    *m_output << ".globl " << name << "\n";
}

void RISCV64Backend::section(const std::string& name) {
    *m_output << ".section " << name << "\n";
}

// ---- data ----

void RISCV64Backend::dq(const std::string& name, const std::string& value) {
    *m_output << name << ": .dword " << value << "\n";
}

void RISCV64Backend::db_str(const std::string& label, const std::string& str) {
    *m_output << label << ": .byte";
    for (size_t i = 0; i < str.size(); i++) {
        *m_output << " 0x" << std::hex << (unsigned)(unsigned char)str[i] << std::dec << ",";
    }
    *m_output << " 0\n";
}

void RISCV64Backend::resq(const std::string& name, size_t count) {
    *m_output << name << ": .zero " << (count * 8) << "\n";
}

// ---- floating point (D-038) ----

void RISCV64Backend::fp_load(const std::string& freg, const std::string& addr, int bits) {
    emit_insn(bits == 32 ? "flw" : "fld", freg + ", " + addr);
}

void RISCV64Backend::fp_store(const std::string& addr, const std::string& freg, int bits) {
    emit_insn(bits == 32 ? "fsw" : "fsd", freg + ", " + addr);
}

void RISCV64Backend::fp_const(const std::string& freg, uint64_t pattern, int bits,
                              const std::string& gpr) {
    if (bits == 32) {
        emit_insn("li", gpr + ", " + std::to_string(pattern & 0xFFFFFFFFu));
        emit_insn("fmv.w.x", freg + ", " + gpr);
    } else {
        emit_insn("li", gpr + ", " + std::to_string(static_cast<int64_t>(pattern)));
        emit_insn("fmv.d.x", freg + ", " + gpr);
    }
}

void RISCV64Backend::fp_arith(char op, const std::string& dst, const std::string& a,
                              const std::string& b, int bits) {
    const char* name = op == '+' ? "fadd" : op == '-' ? "fsub" : op == '*' ? "fmul" : "fdiv";
    emit_insn(std::string(name) + (bits == 32 ? ".s" : ".d"), dst + ", " + a + ", " + b);
}

void RISCV64Backend::fp_neg(const std::string& dst, const std::string& src, int bits,
                            const std::string& gpr) {
    (void)gpr;
    emit_insn(bits == 32 ? "fneg.s" : "fneg.d", dst + ", " + src);
}

void RISCV64Backend::fp_cmp(const std::string& dst_gpr, const std::string& cc, const std::string& a,
                            const std::string& b, int bits) {
    // feq/flt/fle return 0 for unordered operands, which is the required result for every
    // comparison except ne.
    const std::string sfx = bits == 32 ? ".s" : ".d";
    if (cc == "eq" || cc == "ne") {
        emit_insn("feq" + sfx, dst_gpr + ", " + a + ", " + b);
        if (cc == "ne") emit_insn("xori", dst_gpr + ", " + dst_gpr + ", 1");
    } else if (cc == "lt") {
        emit_insn("flt" + sfx, dst_gpr + ", " + a + ", " + b);
    } else if (cc == "le") {
        emit_insn("fle" + sfx, dst_gpr + ", " + a + ", " + b);
    } else if (cc == "gt") {
        emit_insn("flt" + sfx, dst_gpr + ", " + b + ", " + a);
    } else {
        emit_insn("fle" + sfx, dst_gpr + ", " + b + ", " + a);
    }
}

void RISCV64Backend::int_to_fp(const std::string& freg, const std::string& gpr, int bits) {
    emit_insn(bits == 32 ? "fcvt.s.l" : "fcvt.d.l", freg + ", " + gpr);
}

void RISCV64Backend::fp_to_int(const std::string& gpr, const std::string& freg, int bits) {
    emit_insn(bits == 32 ? "fcvt.l.s" : "fcvt.l.d", gpr + ", " + freg + ", rtz");
}

void RISCV64Backend::fp_convert(const std::string& dst, const std::string& src, int from_bits) {
    emit_insn(from_bits == 32 ? "fcvt.d.s" : "fcvt.s.d", dst + ", " + src);
}

void RISCV64Backend::note_fp_arg_count(int count) {
    (void)count;  // only System V variadic calls need it
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
