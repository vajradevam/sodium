#pragma once

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <memory>

#include "parser.hpp"
#include "backend/interface.hpp"
#include "ir/builder.hpp"
#include "ir/module.hpp"
#include "ir/liveness.hpp"
#include "ir/linear_scan.hpp"
#include "ir/rewriter.hpp"
#include "ir/target_regs.hpp"
#include "ir/emitter.hpp"

enum class IntType;

struct Var {
    size_t stack_loc;       // index in current stack frame (0 = RSP, 1 = RSP+8, ...)
    size_t array_size = 0;
    IntType type = IntType::i64;
    std::string struct_type; // empty if not a struct type
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
    size_t size; // total size in qwords
    std::vector<std::string> field_names;
    std::unordered_map<std::string, size_t> field_offsets; // offset in qwords from base
};

class Generator {
public:
    explicit Generator(NodeProg root, Backend& backend, const TargetRegisterInfo& tri,
                       std::unique_ptr<Backend> owned_backend = nullptr);

    void set_no_alloc(bool v) { m_no_alloc = v; }
    void set_emit_ir(bool v) { m_emit_ir = v; }

    [[nodiscard]] std::string gen_prog();

    void declare_var(const std::string& name, IntType type = IntType::i64, SourceLoc loc = {});
    Var lookup_var(const std::string& name, SourceLoc loc = {});

    void gen_expr(const NodeExpr& expr);
    void gen_stmt(const NodeStmt& stmt);
    void gen_func_def(const NodeFuncDef& func);

    std::optional<int64_t> eval_const_expr(const NodeExpr* expr) const;
    void collect_globals(const std::vector<NodeStmt>& stmts);

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

    /// Load/store a local or global variable into/from a vreg on the vstack.
    void load_var_to_vstack(const std::string& name);
    void store_var_from_vstack(const std::string& name, AssignOp op = AssignOp::assign);

    /// Finalize the current IR function: run allocator, rewriter, emit to backend.
    void flush_function();

    /// Access the backend.
    Backend& backend() const { return *m_backend_ptr; }

private:
    NodeProg m_prog;
    Backend* m_backend_ptr;
    std::unique_ptr<Backend> m_owned_backend;  // owned only if we created it
    const TargetRegisterInfo* m_tri_ptr;
    std::ostringstream m_output;
    size_t m_label_count = 0;
    size_t m_string_count = 0;
    bool m_in_function = false;
    bool m_emitted_exit = false;
    std::string m_func_epilogue_label;
    size_t m_stack_size = 0;
    std::vector<Scope> m_scopes;
    std::vector<StringEntry> m_strings;
    std::vector<LoopContext> m_loop_stack;
    std::vector<std::string> m_break_stack;
    std::unordered_map<std::string, bool> m_globals;
    std::unordered_map<std::string, Var> m_global_var_info;
    std::unordered_map<std::string, int64_t> m_constants;
    std::unordered_map<std::string, bool> m_func_names;
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
    uint32_t m_frame_slots = 0;     ///< Number of frame slots allocated in current function
    uint32_t m_next_frame_slot = 0; ///< Next free frame slot index

    TargetRegisterInfo m_tri = TargetRegisterInfo::x86_64_systemv();
};
