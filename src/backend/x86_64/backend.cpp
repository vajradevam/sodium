#include "backend.hpp"
#include <sstream>
#include <unordered_map>
#include <cctype>

// Forward declarations for subregister helpers
static std::string low8(const std::string& r);
static std::string low16(const std::string& r);
static std::string low32(const std::string& r);

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

// Map 64-bit register name to 16-bit subregister
static std::string low16(const std::string& r) {
    static const std::unordered_map<std::string, std::string> m = {
        {"rax", "ax"}, {"rbx", "bx"}, {"rcx", "cx"}, {"rdx", "dx"},
        {"rsi", "si"}, {"rdi", "di"}, {"rbp", "bp"}, {"rsp", "sp"},
        {"r8", "r8w"}, {"r9", "r9w"}, {"r10", "r10w"}, {"r11", "r11w"},
        {"r12", "r12w"}, {"r13", "r13w"}, {"r14", "r14w"}, {"r15", "r15w"},
    };
    auto it = m.find(r);
    return it != m.end() ? it->second : r;
}

// ---- helpers ----

void X8664Backend::emit_insn(const std::string& insn, const std::string& ops) {
    if (ops.empty())
        *m_output << "    " << insn << "\n";
    else
        *m_output << "    " << insn << " " << ops << "\n";
}

void X8664Backend::debug_line(const std::string& file, uint32_t line) {
    // NASM's %line pins the following assembly lines to a source line. The
    // +0 form is required so each marker sets an exact line (D-037).
    std::string escaped;
    for (char c : file) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    *m_output << "%line " << line << "+0 \"" << escaped << "\"\n";
}

void X8664Backend::emit_cqo() {
    emit_insn("cqo");
}

// ---- stack ----

void X8664Backend::push_frame_offset(int offset) {
    std::string addr = "[rbp " + std::string(offset < 0 ? "- " : "+ ")
                     + std::to_string(offset < 0 ? -offset : offset) + "]";
    emit_insn("push", "qword " + addr);
}

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

void X8664Backend::stack_probe_check(const std::string& limit_label,
                                     const std::string& message_label,
                                     const std::string& ok_label,
                                     const std::string& fail_label) {
    // rsp is 16-byte aligned here (after push rbp), so calling the runtime
    // print/exit helpers from the failure path is safe. rax/rdi are caller-saved.
    emit_insn("mov", "rax, [rel " + limit_label + "]");
    emit_insn("cmp", "rsp, rax");
    emit_insn("jge", ok_label);
    label(fail_label);
    emit_insn("lea", "rdi, [rel " + message_label + "]");
    emit_insn("call", "_sodium_print_raw_str");
    emit_insn("mov", "rdi, 70");
    emit_insn("call", "_sodium_exit");
    label(ok_label);
}

// ---- data movement ----

void X8664Backend::load_imm(const std::string& reg, int64_t value) {
    emit_insn("mov", reg + ", " + std::to_string(value));
}

void X8664Backend::load(const std::string& reg, const std::string& addr) {
    emit_insn("mov", reg + ", " + addr);
}

void X8664Backend::store(const std::string& addr, const std::string& reg) {
    // If reg is a numeric immediate, add qword size specifier to avoid
    // NASM defaulting to byte (which truncates larger values).
    bool is_imm = !reg.empty() && (reg[0] == '-' || std::isdigit(reg[0]));
    if (is_imm) {
        emit_insn("mov", "qword " + addr + ", " + reg);
    } else {
        emit_insn("mov", addr + ", " + reg);
    }
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
    emit_insn("movsxd", dst + ", dword " + addr);
}

void X8664Backend::load_u32(const std::string& dst, const std::string& addr) {
    // Writing the 32-bit register zero-extends into the full register.
    emit_insn("mov", low32(dst) + ", dword " + addr);
}

// ---- width-specific stores ----

void X8664Backend::store_8(const std::string& addr, const std::string& src) {
    emit_insn("mov", "byte " + addr + ", " + low8(src));
}

void X8664Backend::store_16(const std::string& addr, const std::string& src) {
    emit_insn("mov", "word " + addr + ", " + low16(src));
}

void X8664Backend::store_32(const std::string& addr, const std::string& src) {
    emit_insn("mov", "dword " + addr + ", " + low32(src));
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
    // We use r11 (designated scratch register) for divisor,
    // rax for dividend, cqo for sign-extend.
    mov("r11", divisor);
    // Runtime zero-division check: exit(1) if divisor == 0
    std::string ok = ".Ldiv_ok_" + std::to_string(m_div_label_counter++);
    emit_insn("cmp", "r11, 0");
    emit_insn("jne", ok);
    emit_insn("mov", "rdi, 1");
    emit_insn("call", "_sodium_exit");
    label(ok);
    mov("rax", dividend);
    emit_cqo();
    emit_insn("idiv", "r11");
    if (dst != "rax") {
        mov(dst, "rax");
    }
}

void X8664Backend::signed_mod(const std::string& dst, const std::string& dividend,
                               const std::string& divisor) {
    // x86-64: remainder ends up in rdx after idiv
    mov("r11", divisor);
    // Runtime zero-division check: exit(1) if divisor == 0
    std::string ok = ".Ldiv_ok_" + std::to_string(m_div_label_counter++);
    emit_insn("cmp", "r11, 0");
    emit_insn("jne", ok);
    emit_insn("mov", "rdi, 1");
    emit_insn("call", "_sodium_exit");
    label(ok);
    mov("rax", dividend);
    emit_cqo();
    emit_insn("idiv", "r11");
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

// Map 64-bit register name to 8-bit subregister
static std::string low8(const std::string& r) {
    static const std::unordered_map<std::string, std::string> m = {
        {"rax", "al"}, {"rbx", "bl"}, {"rcx", "cl"}, {"rdx", "dl"},
        {"rsi", "sil"}, {"rdi", "dil"}, {"rbp", "bpl"}, {"rsp", "spl"},
        {"r8", "r8b"}, {"r9", "r9b"}, {"r10", "r10b"}, {"r11", "r11b"},
        {"r12", "r12b"}, {"r13", "r13b"}, {"r14", "r14b"}, {"r15", "r15b"},
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


// Helper: true if string looks like a register name (starts with letter)
static bool is_register_name(const std::string& s) {
    return !s.empty() && std::isalpha(static_cast<unsigned char>(s[0]));
}

// x86-64 encodes a register shift count only in CL. Writing CL is destructive:
// a live value that the allocator placed in RCX would lose its low byte. r11 is
// reserved (never allocated), so use it to preserve RCX across the shift. When
// the destination is RCX itself, shift in r11 and move the result back.
static void emit_variable_shift(X8664Backend& backend, const char* mnemonic,
                                const std::string& dst, const std::string& src) {
    if (dst == "rcx") {
        backend.mov("r11", dst);
        backend.mov("cl", low8(src));
        backend.emit_insn(mnemonic, "r11, cl");
        backend.mov("rcx", "r11");
    } else {
        backend.mov("r11", "rcx");
        backend.mov("cl", low8(src));
        backend.emit_insn(mnemonic, dst + ", cl");
        backend.mov("rcx", "r11");
    }
}

void X8664Backend::shl(const std::string& dst, const std::string& src) {
    if (is_register_name(src) && src != "cl") {
        emit_variable_shift(*this, "shl", dst, src);
    } else {
        emit_insn("shl", dst + ", " + src);
    }
}

void X8664Backend::shr(const std::string& dst, const std::string& src) {
    if (is_register_name(src) && src != "cl") {
        emit_variable_shift(*this, "shr", dst, src);
    } else {
        emit_insn("shr", dst + ", " + src);
    }
}

void X8664Backend::ashr(const std::string& dst, const std::string& src) {
    if (is_register_name(src) && src != "cl") {
        emit_variable_shift(*this, "sar", dst, src);
    } else {
        emit_insn("sar", dst + ", " + src);
    }
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

void X8664Backend::cmp_result(const std::string& dst, const std::string& a,
                               const std::string& b, const std::string& condition) {
    // Self-contained comparison: dst = (a COND b) ? 1 : 0
    mov(dst, a);
    cmp(dst, b);
    set_cc(low8(dst), condition);
    movzx(dst, low8(dst), 8);
}

// ---- control ----

void X8664Backend::call(const std::string& target) {
    emit_insn("call", target);
}

void X8664Backend::call_reg(const std::string& reg) {
    emit_insn("call", reg);
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
    *m_output << label << ": db";
    for (size_t i = 0; i < str.size(); i++) {
        *m_output << " 0x" << std::hex << (unsigned)(unsigned char)str[i] << std::dec << ",";
    }
    *m_output << " 0\n";
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
