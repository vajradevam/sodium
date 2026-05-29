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

#include "parser.hpp"
#include "backend/interface.hpp"
#include <memory>

enum class IntType;

struct Var {
    size_t stack_loc;
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

// Struct type information for code generation
struct StructInfo {
    size_t size; // total size in qwords
    std::vector<std::string> field_names;
    std::unordered_map<std::string, size_t> field_offsets; // offset in qwords from base
};

class Generator {
public:
    explicit Generator(NodeProg root, std::unique_ptr<Backend> backend = nullptr);

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

    void push(const std::string& reg);
    void pop(const std::string& reg);
    void extend(IntType type);
    void truncate(IntType type);

    /// Access the backend (used by visitor inner classes).
    Backend* backend() const { return m_backend.get(); }

private:
    NodeProg m_prog;
    std::unique_ptr<Backend> m_backend;
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
    std::vector<DataEntry> m_data_entries;
    std::vector<std::string> m_bss_entries;
};
