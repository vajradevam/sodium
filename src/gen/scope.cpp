#include "../generation.hpp"
#include <algorithm>
#include <unordered_set>

// ==========================================================================
// Levenshtein distance for "did you mean?" suggestions
// ==========================================================================

static size_t levenshtein_distance(const std::string& a, const std::string& b) {
    size_t m = a.size();
    size_t n = b.size();
    // Use two rows to keep memory O(n)
    std::vector<size_t> prev(n + 1);
    std::vector<size_t> curr(n + 1);
    for (size_t j = 0; j <= n; j++) prev[j] = j;
    for (size_t i = 1; i <= m; i++) {
        curr[0] = i;
        for (size_t j = 1; j <= n; j++) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({prev[j] + 1,        // deletion
                                curr[j - 1] + 1,    // insertion
                                prev[j - 1] + cost  // substitution
                               });
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

std::string Generator::did_you_mean(const std::string& name) const {
    // Collect all known identifiers
    std::unordered_set<std::string> candidates;
    // Local scopes (search all, not just current)
    for (const auto& scope : m_scopes) {
        for (const auto& [id, _] : scope.vars) {
            candidates.insert(id);
        }
    }
    // Globals
    for (const auto& [id, _] : m_globals) {
        candidates.insert(id);
    }
    // Constants
    for (const auto& [id, _] : m_constants) {
        candidates.insert(id);
    }
    // Function names
    for (const auto& [id, _] : m_func_names) {
        candidates.insert(id);
    }
    // Struct types
    for (const auto& [id, _] : m_struct_types) {
        candidates.insert(id);
    }
    // Builtins
    candidates.insert("print");
    candidates.insert("read");
    candidates.insert("read_eof");
    candidates.insert("malloc");
    candidates.insert("free");
    candidates.insert("argc");
    candidates.insert("argv");

    // Find the closest match
    std::string best;
    size_t best_dist = SIZE_MAX;
    for (const auto& candidate : candidates) {
        size_t dist = levenshtein_distance(name, candidate);
        // Only suggest if distance is small enough relative to name length
        size_t threshold = std::max<size_t>(3, name.size() / 2);
        if (dist < best_dist && dist <= threshold && dist < candidate.size()) {
            best_dist = dist;
            best = candidate;
        }
    }
    return best;
}

// ==========================================================================
// enter_scope / exit_scope
// ==========================================================================

void Generator::enter_scope()
{
    m_scopes.push_back({0, {}});
}

void Generator::exit_scope()
{
    if (!m_scopes.empty()) {
        for (const auto& [name, var] : m_scopes.back().vars) {
            if (!var.used && !var.is_param) {
                m_diag.warning(var.decl_loc, "unused variable '" + name + "'");
            }
        }
    }
    // var_count counts the number of *variables*, not bytes.
    // Stack size is tracked separately in m_stack_size (bytes).
    m_scopes.pop_back();
}

void Generator::mark_var_used(const std::string& name)
{
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        auto found = it->vars.find(name);
        if (found != it->vars.end()) {
            found->second.used = true;
            return;
        }
    }
}

// Align value up to the given alignment (must be power of 2).
static inline size_t align_up(size_t val, size_t align) {
    return (val + align - 1) & ~(align - 1);
}

void Generator::declare_var(const std::string& name, Type type, SourceLoc loc) {
    auto& scope = m_scopes.back();
    if (scope.vars.contains(name)) {
        fail(loc, "Identifier already used in this scope: " + name);
    }
    size_t size = type_size(type);
    size_t align = size < 8 ? size : 8;  // natural alignment, capped at 8
    uint32_t offset = static_cast<uint32_t>(align_up(m_next_frame_offset, align));
    m_next_frame_offset = offset + static_cast<uint32_t>(size);
    scope.vars[name] = Var { .stack_loc = offset, .type = type, .struct_type = "", .decl_loc = loc };
    scope.var_count++;
    m_stack_size += size;
}

Var Generator::lookup_var(const std::string& name, SourceLoc loc) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->vars.contains(name)) {
            it->vars.at(name).used = true;
            return it->vars.at(name);
        }
    }
    std::string suggestion = did_you_mean(name);
    if (!suggestion.empty()) {
        fail(loc, "Undeclared identifier: " + name + " (did you mean '" + suggestion + "'?)");
    } else {
        fail(loc, "Undeclared identifier: " + name);
    }
}
