#include "../generation.hpp"

Generator::Generator(NodeProg root, Backend& backend, const TargetRegisterInfo& tri,
                     std::unique_ptr<Backend> owned_backend)
    : m_prog(std::move(root))
    , m_backend_ptr(&backend)
    , m_owned_backend(std::move(owned_backend))
    , m_tri_ptr(&tri)
{
    m_scopes.push_back({0, {}});
}

[[noreturn]] void Generator::fail(const SourceLoc& loc, const std::string& msg) const {
    m_diag.error(loc, msg);
    if (g_lsp_mode) throw LSPAbort();
    throw GenAbort{};
}

// ==========================================================================
// IR virtual register stack helpers
// ==========================================================================

void Generator::push_type(Type t) {
    m_tstack.push_back(t);
}

Type Generator::pop_type() {
    if (m_tstack.empty())
        return Type::make_int(IntType::i64);
    Type t = m_tstack.back();
    m_tstack.pop_back();
    return t;
}

Type Generator::peek_type() {
    if (m_tstack.empty())
        return Type::make_int(IntType::i64);
    return m_tstack.back();
}

Type* Generator::alloc_type(const Type& t) {
    m_type_pool.push_back(t);
    return &m_type_pool.back();
}

uint32_t Generator::push_vreg(uint32_t vreg) {
    m_vstack.push_back(vreg);
    push_type(Type::make_int(IntType::i64));  // default type
    return vreg;
}

uint32_t Generator::pop_vreg() {
    if (m_vstack.empty()) {
        pop_type();  // keep stacks in sync
        uint32_t fresh = m_ir.current_function()->new_vreg();
        return fresh;
    }
    uint32_t vreg = m_vstack.back();
    m_vstack.pop_back();
    pop_type();
    return vreg;
}

uint32_t Generator::peek_vreg() {
    if (m_vstack.empty()) return UINT32_MAX;
    return m_vstack.back();
}

// ==========================================================================
// Utility methods
// ==========================================================================

std::string Generator::new_label()
{
    return ".L" + std::to_string(m_label_count++);
}

std::string Generator::new_string_label()
{
    return "str" + std::to_string(m_string_count++);
}

bool Generator::is_struct_type(const std::string& name) const {
    return m_struct_types.find(name) != m_struct_types.end();
}

std::optional<StructInfo> Generator::get_struct_info(const std::string& name) const {
    auto it = m_struct_types.find(name);
    if (it != m_struct_types.end()) return it->second;
    return {};
}
