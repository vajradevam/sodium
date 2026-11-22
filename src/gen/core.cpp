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

[[noreturn]] void Generator::fail_with_suggestion(const SourceLoc& loc, const std::string& msg,
                                                   const std::string& suggestion) const {
    m_diag.error(loc, msg);
    m_diag.add_note("did you mean '" + suggestion + "'?");
    m_diag.add_fixit({loc, suggestion});
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

// ==========================================================================
// Frame layout (Y2d)
// ==========================================================================

size_t Generator::frame_bytes(const Var& var) const {
    if (!var.struct_type.empty()) return align_frame(var.array_size, 8);
    if (var.array_size > 0) return align_frame(var.array_size * var.elem_size, 8);
    return type_size(var.type);
}

uint32_t Generator::object_addr(size_t slot, size_t bytes) {
    // frame_addr(o) is fp - 8 - o; the object's lowest byte is fp - slot - bytes.
    return m_ir.frame_addr(static_cast<int64_t>(slot + bytes) - 8);
}

uint32_t Generator::var_addr(const Var& var) {
    return object_addr(var.stack_loc, frame_bytes(var));
}

uint32_t Generator::alloc_object(size_t bytes) {
    uint32_t slot = static_cast<uint32_t>(align_frame(m_next_frame_offset, 8));
    m_next_frame_offset = slot + static_cast<uint32_t>(align_frame(bytes, 8));
    return slot;
}
