#include "backend.hpp"
#include <sstream>

X8664Backend::X8664Backend(std::ostream& os)
    : m_output(&os) {}

void X8664Backend::set_output(std::ostream& os) {
    m_output = &os;
}

std::ostream& X8664Backend::output() {
    return *m_output;
}

// ---- helpers ----

void X8664Backend::emit_insn(const std::string& insn, const std::string& ops) {
    if (ops.empty())
        *m_output << "    " << insn << "\n";
    else
        *m_output << "    " << insn << " " << ops << "\n";
}

// ---- registers ----

std::string X8664Backend::rax() const { return "rax"; }
std::string X8664Backend::rbx() const { return "rbx"; }
std::string X8664Backend::rcx() const { return "rcx"; }
std::string X8664Backend::rdx() const { return "rdx"; }
std::string X8664Backend::rdi() const { return "rdi"; }
std::string X8664Backend::rsi() const { return "rsi"; }
std::string X8664Backend::r8() const  { return "r8"; }
std::string X8664Backend::r9() const  { return "r9"; }
std::string X8664Backend::r10() const { return "r10"; }
std::string X8664Backend::rsp() const { return "rsp"; }
std::string X8664Backend::rbp() const { return "rbp"; }
std::string X8664Backend::acc() const { return "rax"; }
std::string X8664Backend::arg(size_t n) const {
    static const char* args[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    if (n < 6) return args[n];
    return "";  // stack args not handled yet
}

// ---- stack ----

void X8664Backend::push(const std::string& reg) {
    emit_insn("push", reg);
}

void X8664Backend::pop(const std::string& reg) {
    emit_insn("pop", reg);
}

void X8664Backend::adjust_stack(int64_t bytes) {
    if (bytes == 0) return;
    if (bytes > 0)
        emit_insn("add", "rsp, " + std::to_string(bytes));
    else
        emit_insn("sub", "rsp, " + std::to_string(-bytes));
}

// ---- function frame ----

void X8664Backend::func_prologue() {
    emit_insn("push", "rbp");
    emit_insn("mov", "rbp, rsp");
}

void X8664Backend::func_epilogue() {
    emit_insn("mov", "rsp, rbp");
    emit_insn("pop", "rbp");
}

void X8664Backend::ret() {
    emit_insn("ret");
}

// ---- data movement ----

void X8664Backend::load_imm(const std::string& reg, int64_t value) {
    emit_insn("mov", reg + ", " + std::to_string(value));
}

void X8664Backend::load(const std::string& reg, const std::string& addr) {
    emit_insn("mov", reg + ", " + addr);
}

void X8664Backend::store(const std::string& addr, const std::string& reg) {
    emit_insn("mov", addr + ", " + reg);
}

void X8664Backend::mov(const std::string& dst, const std::string& src) {
    emit_insn("mov", dst + ", " + src);
}

void X8664Backend::lea(const std::string& reg, const std::string& addr) {
    emit_insn("lea", reg + ", " + addr);
}

void X8664Backend::movzx(const std::string& dst, const std::string& src, size_t src_bits) {
    (void)src_bits;  // x86-64 handles this via operand size
    emit_insn("movzx", dst + ", " + src);
}

void X8664Backend::movsx(const std::string& dst, const std::string& src, size_t src_bits) {
    (void)src_bits;
    emit_insn("movsx", dst + ", " + src);
}

// ---- arithmetic ----

void X8664Backend::add(const std::string& dst, const std::string& src) {
    emit_insn("add", dst + ", " + src);
}

void X8664Backend::sub(const std::string& dst, const std::string& src) {
    emit_insn("sub", dst + ", " + src);
}

void X8664Backend::mul(const std::string& dst, const std::string& src) {
    emit_insn("imul", dst + ", " + src);
}

void X8664Backend::div(const std::string& divisor) {
    emit_insn("idiv", divisor);
}

void X8664Backend::neg(const std::string& reg) {
    emit_insn("neg", reg);
}

void X8664Backend::not_(const std::string& reg) {
    emit_insn("not", reg);
}

void X8664Backend::xor_(const std::string& dst, const std::string& src) {
    emit_insn("xor", dst + ", " + src);
}

void X8664Backend::and_(const std::string& dst, const std::string& src) {
    emit_insn("and", dst + ", " + src);
}

void X8664Backend::or_(const std::string& dst, const std::string& src) {
    emit_insn("or", dst + ", " + src);
}

void X8664Backend::shl(const std::string& dst, const std::string& src) {
    emit_insn("shl", dst + ", " + src);
}

void X8664Backend::shr(const std::string& dst, const std::string& src) {
    emit_insn("shr", dst + ", " + src);
}

// ---- comparison ----

void X8664Backend::cmp(const std::string& a, const std::string& b) {
    emit_insn("cmp", a + ", " + b);
}

void X8664Backend::test(const std::string& reg, const std::string& mask) {
    emit_insn("test", reg + ", " + mask);
}

void X8664Backend::cmp_byte_mem_imm(const std::string& addr, int8_t imm) {
    emit_insn("cmp", "byte " + addr + ", " + std::to_string(imm));
}

// ---- branches ----

void X8664Backend::jmp(const std::string& label) {
    emit_insn("jmp", label);
}

void X8664Backend::je(const std::string& label) {
    emit_insn("je", label);
}

void X8664Backend::jne(const std::string& label) {
    emit_insn("jne", label);
}

void X8664Backend::jl(const std::string& label) {
    emit_insn("jl", label);
}

void X8664Backend::jle(const std::string& label) {
    emit_insn("jle", label);
}

void X8664Backend::jg(const std::string& label) {
    emit_insn("jg", label);
}

void X8664Backend::jge(const std::string& label) {
    emit_insn("jge", label);
}

void X8664Backend::jz(const std::string& label) {
    emit_insn("jz", label);
}

void X8664Backend::jnz(const std::string& label) {
    emit_insn("jnz", label);
}

void X8664Backend::set_cc(const std::string& reg, const std::string& condition) {
    emit_insn("set" + condition, reg);
}

// ---- control ----

void X8664Backend::call(const std::string& target) {
    emit_insn("call", target);
}

void X8664Backend::syscall() {
    emit_insn("syscall");
}

void X8664Backend::sign_extend_rax() {
    emit_insn("cqo");
}

// ---- labels / directives ----

void X8664Backend::label(const std::string& name) {
    *m_output << name << ":\n";
}

void X8664Backend::global_sym(const std::string& name) {
    *m_output << "global " << name << "\n";
}

void X8664Backend::extern_sym(const std::string& name) {
    *m_output << "extern " << name << "\n";
}

void X8664Backend::section(const std::string& name) {
    *m_output << "section " << name << "\n";
}

// ---- data ----

void X8664Backend::dq(const std::string& name, const std::string& value) {
    *m_output << name << ": dq " << value << "\n";
}

void X8664Backend::db_str(const std::string& label, const std::string& str) {
    *m_output << label << ": db \"" << str << "\", 0\n";
}

void X8664Backend::resq(const std::string& name, size_t count) {
    if (count == 1)
        *m_output << name << ": resq 1\n";
    else
        *m_output << name << ": resq " << count << "\n";
}

// ---- addressing modes ----

std::string X8664Backend::addr_label(const std::string& sym) const {
    return "[rel " + sym + "]";
}

std::string X8664Backend::addr_reg_offset(const std::string& reg, int offset) const {
    if (offset == 0) return "[" + reg + "]";
    if (offset > 0) return "[" + reg + " + " + std::to_string(offset) + "]";
    return "[" + reg + " - " + std::to_string(-offset) + "]";
}

std::string X8664Backend::addr_reg(const std::string& reg) const {
    return "[" + reg + "]";
}

std::string X8664Backend::addr_sp(int offset) const {
    return addr_reg_offset("rsp", offset);
}

std::string X8664Backend::addr_fp(int offset) const {
    return addr_reg_offset("rbp", offset);
}

std::string X8664Backend::addr_indexed(const std::string& base, const std::string& index, int scale) const {
    return "[" + base + " + " + index + " * " + std::to_string(scale) + "]";
}

std::string X8664Backend::addr_param(size_t index) const {
    return addr_fp(16 + static_cast<int>(index) * 8);
}
