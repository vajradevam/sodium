#include "../generation.hpp"

// ==========================================================================
// enter_scope / exit_scope
// ==========================================================================

void Generator::enter_scope()
{
    m_scopes.push_back({0, {}});
}

void Generator::exit_scope()
{
    auto& scope = m_scopes.back();
    // var_count counts the number of *variables*, not bytes.
    // Stack size is tracked separately in m_stack_size (bytes).
    m_scopes.pop_back();
}

// Align value up to the given alignment (must be power of 2).
static inline size_t align_up(size_t val, size_t align) {
    return (val + align - 1) & ~(align - 1);
}

void Generator::declare_var(const std::string& name, Type type, SourceLoc loc) {
    auto& scope = m_scopes.back();
    if (scope.vars.contains(name)) {
        lsp_exit(loc, "Identifier already used in this scope: " + name);
    }
    size_t size = type_size(type);
    size_t align = size < 8 ? size : 8;  // natural alignment, capped at 8
    uint32_t offset = static_cast<uint32_t>(align_up(m_next_frame_offset, align));
    m_next_frame_offset = offset + static_cast<uint32_t>(size);
    scope.vars[name] = Var { .stack_loc = offset, .type = type };
    scope.var_count++;
    m_stack_size += size;
}

Var Generator::lookup_var(const std::string& name, SourceLoc loc) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->vars.contains(name)) {
            return it->vars.at(name);
        }
    }
    lsp_exit(loc, "Undeclared identifier: " + name);
}
