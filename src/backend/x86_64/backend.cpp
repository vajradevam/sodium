#include "backend.hpp"
#include <sstream>
#include <unordered_map>

// Map 64-bit register name to 32-bit subregister
static std::string low32(const std::string& r) {
    static const std::unordered_map<std::string, std::string> m = {
        {"rax", "eax"}, {"rbx", "ebx"}, {"rcx", "ecx"}, {"rdx", "edx"},
        {"rsi", "esi"}, {"rdi", "edi"}, {"rbp", "ebp"}, {"rsp", "esp"},
        {"r8", "r8d"}, {"r9", "r9d"}, {"r10", "r10d"}, {"r11", "r11d"},
        {"r12", "r12d"}, {"r13", "r13d"}, {"r14", "r14d"}, {"r15", "r15d"},
    };
    auto it = m.find(r);
    return it != m.end() ? it->second : r;
}

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

void X8664Backend::emit_cqo() {
    emit_insn("cqo");
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

// ---- width-specific loads ----

void X8664Backend::load_s8(const std::string& dst, const std::string& addr) {
    emit_insn("movsx", dst + ", byte " + addr);
}

void X8664Backend::load_u8(const std::string& dst, const std::string& addr) {
    emit_insn("movzx", dst + ", byte " + addr);
}

void X8664Backend::load_s16(const std::string& dst, const std::string& addr) {
    emit_insn("movsx", dst + ", word " + addr);
}

void X8664Backend::load_u16(const std::string& dst, const std::string& addr) {
    emit_insn("movzx", dst + ", word " + addr);
}

void X8664Backend::load_s32(const std::string& dst, const std::string& addr) {
    emit_insn("movsx", dst + ", dword " + addr);
}

void X8664Backend::load_u32(const std::string& dst, const std::string& addr) {
    emit_insn("mov", low32(dst) + ", dword " + addr);
}

// ---- width-specific stores ----

void X8664Backend::store_8(const std::string& addr, const std::string& src) {
    emit_insn("mov", "byte " + addr + ", " + src);
}

void X8664Backend::store_16(const std::string& addr, const std::string& src) {
    emit_insn("mov", "word " + addr + ", " + src);
}

void X8664Backend::store_32(const std::string& addr, const std::string& src) {
    emit_insn("mov", "dword " + addr + ", " + src);
}

void X8664Backend::movzx(const std::string& dst, const std::string& src, size_t src_bits) {
    (void)src_bits;
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

void X8664Backend::signed_div(const std::string& dst, const std::string& dividend,
                               const std::string& divisor) {
    // x86-64: idiv requires dividend in rdx:rax, divisor in r/m
    // We use rdi for divisor (safe scratch), rax for dividend, cqo for sign-extend
    mov("rdi", divisor);
    mov("rax", dividend);
    emit_cqo();
    emit_insn("idiv", "rdi");
    if (dst != "rax") {
        mov(dst, "rax");
    }
}

void X8664Backend::signed_mod(const std::string& dst, const std::string& dividend,
                               const std::string& divisor) {
    // x86-64: remainder ends up in rdx after idiv
    mov("rdi", divisor);
    mov("rax", dividend);
    emit_cqo();
    emit_insn("idiv", "rdi");
    if (dst != "rdx") {
        mov(dst, "rdx");
    }
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

void X8664Backend::ashr(const std::string& dst, const std::string& src) {
    emit_insn("sar", dst + ", " + src);
}

// ---- comparison ----

void X8664Backend::cmp(const std::string& a, const std::string& b) {
    emit_insn("cmp", a + ", " + b);
}

void X8664Backend::test(const std::string& reg, const std::string& mask) {
    emit_insn("test", reg + ", " + mask);
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
