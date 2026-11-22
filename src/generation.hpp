#pragma once

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <memory>

/// Thrown by Generator::fail to unwind to a statement/function boundary.
struct GenAbort {};

#include "parser.hpp"
#include "backend/interface.hpp"
#include "ir/builder.hpp"
#include "ir/module.hpp"
#include "ir/liveness.hpp"
#include "ir/linear_scan.hpp"
#include "ir/rewriter.hpp"
#include "ir/target_regs.hpp"
#include <list>
#include "ir/emitter.hpp"

enum class IntType;

struct Var {
    size_t stack_loc;       // index in current stack frame (0 = RSP, 1 = RSP+8, ...)
    size_t array_size = 0;
    Type type = Type::make_int(IntType::i64);
    std::string struct_type; // empty if not a struct type
    SourceLoc decl_loc{};    // for unused-variable/parameter warnings
    bool used = false;       // set when the variable is read
    bool is_param = false;   // parameters get a different warning message
    size_t elem_size = 8;    // array element size in bytes (arrays only)
};

struct Scope {
    size_t var_count;
    std::unordered_map<std::string, Var> vars;
};

struct LoopContext {
    std::string begin_label;
    std::string end_label;
    std::string continue_label;
};

struct StringEntry {
    std::string label;
    std::string value;
};

struct GlobalInit {
    std::string name;
    NodeExpr* expr;
};

struct StructInfo {
    size_t size = 0; // total size in bytes (including trailing padding)
    std::vector<std::string> field_names;
    std::unordered_map<std::string, size_t> field_offsets; // byte offsets from struct base
    std::vector<Type> field_types;

    /// C-rule layout: natural alignment, internal and trailing padding.
    static StructInfo compute_layout(const std::vector<Token>& names,
                                     const std::vector<std::optional<Type>>& types) {
        StructInfo info;
        size_t offset = 0;
        size_t struct_align = 1;
        for (size_t i = 0; i < names.size(); i++) {
            Type t = (i < types.size() && types[i]) ? *types[i]
                                                    : Type::make_int(IntType::i64);
            size_t sz = type_size(t);
            size_t align = sz < 8 ? sz : 8;
            offset = (offset + align - 1) & ~(align - 1);
            info.field_names.push_back(names[i].value.value());
            info.field_offsets[names[i].value.value()] = offset;
            info.field_types.push_back(t);
            offset += sz;
            if (align > struct_align) struct_align = align;
        }
        info.size = (offset + struct_align - 1) & ~(struct_align - 1);
        return info;
    }
};

class Generator {
public:
    explicit Generator(NodeProg root, Backend& backend, const TargetRegisterInfo& tri,
                       std::unique_ptr<Backend> owned_backend = nullptr);

    /// Diagnostics collected during code generation. Owned by this generator.
    const DiagnosticEngine& diagnostics() const { return m_diag; }

    /// Apply the command-line warning policy (`-w`, `-Werror`).
    void set_warn_config(WarnConfig cfg) { m_diag.set_warn_config(cfg); }
    void set_color(bool v) { m_diag.set_color(v); }

    /// Record an error in the engine and report it through the CLI/LSP path.
    [[noreturn]] void fail(const SourceLoc& loc, const std::string& msg) const;

    /// Record an error plus a "did you mean" note and replacement fix-it.
    [[noreturn]] void fail_with_suggestion(const SourceLoc& loc, const std::string& msg,
                                            const std::string& suggestion) const;

    void set_no_alloc(bool v) { m_no_alloc = v; }
    void set_emit_ir(bool v) { m_emit_ir = v; }

    [[nodiscard]] std::string gen_prog();

private:
    std::string gen_prog_impl();

public:

    void declare_var(const std::string& name, Type type = Type::make_int(IntType::i64), SourceLoc loc = {});
    Var lookup_var(const std::string& name, SourceLoc loc = {});
    /// Mark a local variable (or parameter) as read, for -Wunused warnings.
    void mark_var_used(const std::string& name);

    /// Suggest a close match for an undeclared identifier, or empty string if none found.
    std::string did_you_mean(const std::string& name) const;

    void gen_expr(const NodeExpr& expr);
    void gen_stmt(const NodeStmt& stmt);
    void gen_func_def(const NodeFuncDef& func);

    std::optional<int64_t> eval_const_expr(const NodeExpr* expr) const;
    /// Byte size of a sizeof() operand (type, struct name, or expression).
    std::optional<int64_t> sizeof_value(const NodeExprSizeof& sz) const;
    void collect_globals(const std::vector<NodeStmt>& stmts);
    /// Register enumerators as compile-time integer constants (Y6b).
    void collect_enums();

    /// AST flow pass: unreachable statements and missing returns (E3c).
    void check_flow_warnings();

    /// Warn when a constant initializer does not fit the declared type (E3d).
    void check_narrowing_assign(Type target, const NodeExpr* value, SourceLoc loc);

    /// Warn when the result of a pure function call is discarded (E3d).
    void warn_unused_result(const NodeExpr& expr, SourceLoc loc);

    /// Look up a user function definition by name.
    const NodeFuncDef* find_func(const std::string& name) const;
    bool has_declared_param_types(const std::string& name) const;
    /// Check arity/types at a call to a typed function; insert safe widening
    /// conversions into the argument vregs. Fails on a mismatch (Y1b).
    void check_call_args(const NodeExprCall& call, SourceLoc loc);

    /// Check/convert the return expression against the current function's
    /// declared return type (Y1c).
    void check_return_type(const NodeExpr& expr, SourceLoc loc);

    void enter_scope();
    void exit_scope();

    std::string new_label();
    std::string new_string_label();

    bool is_struct_type(const std::string& name) const;
    std::optional<StructInfo> get_struct_info(const std::string& name) const;

    /// IR virtual register stack helpers
    uint32_t push_vreg(uint32_t vreg);
    uint32_t pop_vreg();
    uint32_t peek_vreg();

    /// Type stack (parallel to vreg stack). Tracks the type of each value.
    void push_type(Type t);
    Type pop_type();
    Type peek_type();

    /// Load/store a local or global variable into/from a vreg on the vstack.
    void load_var_to_vstack(const std::string& name);
    void store_var_from_vstack(const std::string& name, AssignOp op = AssignOp::assign);

    /// Finalize the current IR function: run allocator, rewriter, emit to backend.
    void flush_function();

    /// Access the backend.
    Backend& backend() const { return *m_backend_ptr; }

    /// Allocate a persistent copy of a Type (for pointee pointers).
    Type* alloc_type(const Type& t);

    /// Static type inference on an expression AST (no IR emitted).
    /// Returns the inferred type and records callee parameter types in m_inferred_param_types.
    Type infer_expr_type(const NodeExpr& expr,
                         const std::unordered_map<std::string, Type>& scope_types);

    /// Walk all function bodies to infer parameter types from call sites.
    void collect_call_types();

    /// Map: function name → inferred parameter types
    std::unordered_map<std::string, std::vector<Type>> m_inferred_param_types;
    int m_type_infer_iteration = 0;  // iteration counter for type inference, used to suppress duplicate warnings

private:
    NodeProg m_prog;
    Backend* m_backend_ptr;
    std::unique_ptr<Backend> m_owned_backend;  // owned only if we created it
    const TargetRegisterInfo* m_tri_ptr;
    mutable DiagnosticEngine m_diag;
    std::ostringstream m_output;
    size_t m_label_count = 0;
    size_t m_string_count = 0;
    bool m_in_function = false;
    bool m_emitted_exit = false;
    std::string m_func_epilogue_label;
    std::optional<Type> m_current_return_type; ///< declared return type of the active function
    std::string m_current_func_name;           ///< for return-type diagnostics
    size_t m_stack_size = 0;
    std::vector<Scope> m_scopes;
    std::vector<StringEntry> m_strings;
    std::vector<LoopContext> m_loop_stack;
    std::vector<std::string> m_break_stack;
    std::unordered_map<std::string, bool> m_globals;
    std::unordered_map<std::string, Var> m_global_var_info;
    std::unordered_map<std::string, int64_t> m_constants;
    std::unordered_map<std::string, bool> m_func_names;
    /// Functions that return a value and have no observable side effects.
    std::unordered_set<std::string> m_pure_value_funcs;
    std::unordered_map<std::string, StructInfo> m_struct_types;
    std::vector<GlobalInit> m_global_inits;
    struct DataEntry {
        std::string name;
        std::string value;
    };
    struct BssEntry {
        std::string name;
        size_t qwords;
    };
    std::vector<DataEntry> m_data_entries;
    std::vector<BssEntry> m_bss_entries;

    // ---- options ----
    bool m_no_alloc = false;
    bool m_emit_ir = false;

    // ---- IR state ----
    IRBuilder m_ir;
    std::vector<uint32_t> m_vstack;
    std::vector<Type> m_tstack;     ///< Type stack, parallel to m_vstack
    uint32_t m_frame_slots = 0;     ///< Number of frame slots allocated in current function
    uint32_t m_next_frame_offset = 0; ///< Next free byte offset in the stack frame

    // Type pool: persistent storage for Type::pointee pointers
    std::list<Type> m_type_pool;

    TargetRegisterInfo m_tri = TargetRegisterInfo::x86_64_systemv();
};
