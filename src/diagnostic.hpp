#pragma once

#include "source_loc.hpp"

#include <string>
#include <utility>
#include <vector>

enum class Severity {
    Note,
    Warning,
    Error,
    Fatal,
};

inline const char* severity_name(Severity s) {
    switch (s) {
        case Severity::Note:    return "note";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
        case Severity::Fatal:   return "error";
    }
    return "error";
}

/// Command-line warning policy. `-w` sets `enabled = false`;
/// `-Werror` sets `werror = true` so warnings are recorded as errors.
struct WarnConfig {
    bool enabled = true;
    bool werror = false;
};

struct FixIt {
    SourceLoc loc;
    std::string replacement;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    SourceLoc loc;
    std::string message;
    std::vector<std::string> notes;
    std::vector<FixIt> fixits;
};

/// Collects diagnostics for one compilation. Holds no global state: clients
/// own an instance and pass it to the compiler stages.
class DiagnosticEngine {
public:
    void add(Diagnostic d) { m_diags.push_back(std::move(d)); }

    void set_warn_config(WarnConfig cfg) { m_warn = cfg; }
    const WarnConfig& warn_config() const { return m_warn; }

    void note(SourceLoc loc, std::string msg) {
        add({Severity::Note, std::move(loc), std::move(msg), {}, {}});
    }
    void warning(SourceLoc loc, std::string msg) {
        if (!m_warn.enabled) return;
        Severity sev = m_warn.werror ? Severity::Error : Severity::Warning;
        add({sev, std::move(loc), std::move(msg), {}, {}});
    }
    void error(SourceLoc loc, std::string msg) {
        add({Severity::Error, std::move(loc), std::move(msg), {}, {}});
    }
    void fatal(SourceLoc loc, std::string msg) {
        add({Severity::Fatal, std::move(loc), std::move(msg), {}, {}});
    }

    /// Attach a secondary note to the most recently added diagnostic.
    bool add_note(std::string msg) {
        if (m_diags.empty()) return false;
        m_diags.back().notes.push_back(std::move(msg));
        return true;
    }

    /// Attach a fix-it hint to the most recently added diagnostic.
    bool add_fixit(FixIt fix) {
        if (m_diags.empty()) return false;
        m_diags.back().fixits.push_back(std::move(fix));
        return true;
    }

    bool empty() const { return m_diags.empty(); }
    size_t size() const { return m_diags.size(); }

    bool has_errors() const {
        for (const auto& d : m_diags) {
            if (d.severity == Severity::Error || d.severity == Severity::Fatal) return true;
        }
        return false;
    }

    size_t error_count() const {
        size_t n = 0;
        for (const auto& d : m_diags) {
            if (d.severity == Severity::Error || d.severity == Severity::Fatal) ++n;
        }
        return n;
    }

    void clear() { m_diags.clear(); }

    /// D-004: `file:line:col: error|warning|note: message`, then notes and
    /// fix-it hints, one per line. Locations with an empty file render as
    /// `line:col` (matching the legacy format_err output).
    static std::string render(const Diagnostic& d) {
        std::string out = d.loc.to_string() + ": " + severity_name(d.severity) + ": "
                        + d.message + "\n";
        for (const auto& n : d.notes) {
            out += d.loc.to_string() + ": note: " + n + "\n";
        }
        for (const auto& f : d.fixits) {
            out += f.loc.to_string() + ": fix-it: " + f.replacement + "\n";
        }
        return out;
    }

    std::string render_all() const {
        std::string out;
        for (const auto& d : m_diags) out += render(d);
        return out;
    }

    const std::vector<Diagnostic>& diagnostics() const { return m_diags; }

private:
    std::vector<Diagnostic> m_diags;
    WarnConfig m_warn{};
};
