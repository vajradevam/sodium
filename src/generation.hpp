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
#include "source_loc.hpp"
#include "backend/interface.hpp"
#include "ir/builder.hpp"
#include "ir/module.hpp"
#include "ir/liveness.hpp"
#include "ir/linear_scan.hpp"
#include "ir/inline.hpp"
#include "ir/rewriter.hpp"
#include "ir/target_regs.hpp"

/// Best-effort source location for an AST statement (used for debug lines).
SourceLoc statement_location(const NodeStmt& stmt);
#include "ir/verify.hpp"
#include "ir/fold.hpp"
#include "ir/cleanup.hpp"
#include "ir/cse.hpp"
#include "ir/mem2reg.hpp"
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
    bool by_ref = false;     // aggregate lives behind a pointer stored in its 8-byte frame slot
    size_t elem_size = 8;    // array element size in bytes (arrays only)
    std::vector<size_t> dim_sizes; // compile-time dimensions (multidim arrays)
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
    size_t align = 1; // alignment in bytes (max field alignment, capped at 8)

    /// C-rule layout: natural alignment, internal and trailing padding.
    static StructInfo compute_layout(
        const std::vector<Token>& names,
        const std::vector<std::optional<Type>>& types,
        const std::unordered_map<std::string, size_t>& struct_sizes = {},
        const std::unordered_map<std::string, size_t>& struct_aligns = {}) {
        StructInfo info;
        size_t offset = 0;
        size_t struct_align = 1;
        for (size_t i = 0; i < names.size(); i++) {
            Type t = (i < types.size() && types[i]) ? *types[i]
                                                    : Type::make_int(IntType::i64);
            size_t sz = type_size(t);
            if (t.kind == Type::Struct) {
                auto known = struct_sizes.find(t.struct_name);
                if (known != struct_sizes.end()) sz = known->second;
            }
            size_t align = sz < 8 ? sz : 8;
            if (t.kind == Type::Struct) {
                auto known_align = struct_aligns.find(t.struct_name);
                if (known_align != struct_aligns.end()) align = known_align->second;
            }
            offset = (offset + align - 1) & ~(align - 1);
            info.field_names.push_back(names[i].value.value());
            info.field_offsets[names[i].value.value()] = offset;
            info.field_types.push_back(t);
            offset += sz;
            if (align > struct_align) struct_align = align;
        }
        info.size = (offset + struct_align - 1) & ~(struct_align - 1);
        info.align = struct_align;
        return info;
    }
};

/// Where an argument word travels (D-012). Shared by call sites and function entry so
/// the two always agree: a word is either the n-th register argument or the n-th
/// stack word.
struct ArgPlacement {
    bool on_stack = false;
    size_t index = 0;
    bool fp_reg = false;   ///< `index` names an FP argument register
};

/// One register-sized part of a small struct that contains floats (X1b): `fp` pieces travel in
/// an FP register (`size` 4 = f32, 8 = f64 or two packed f32), the others in an integer one.
struct StructPiece {
    size_t off;
    size_t size;
    bool fp;
};

/// A scalar field of a struct, after flattening nested structs.
struct StructLeaf {
    size_t off;
    size_t size;
    bool fp;
};

class ArgPlacer {
public:
    explicit ArgPlacer(const TargetRegisterInfo& tri) : m_tri(tri) {}

    /// One word: a scalar, a pointer, or a by-reference struct.
    ArgPlacement scalar() {
        if (m_regs_used < m_tri.arg_regs.size()) return {false, m_regs_used++};
        return {true, m_stack_used++};
    }

    /// A float scalar (D-038): the next FP argument register; once those run out, the next
    /// integer register (RISC-V LP64D) or the stack (System V).
    ArgPlacement fp_scalar() {
        if (m_fp_used < m_tri.fp_arg_regs.size()) {
            ArgPlacement p{false, m_fp_used++};
            p.fp_reg = true;
            return p;
        }
        if (m_tri.fp_overflow_to_gpr) return scalar();
        return {true, m_stack_used++};
    }

    /// A struct of 1-2 words that is eligible for registers. RISC-V splits it between the
    /// last register and the stack; System V keeps it whole, so one that does not fit
    /// goes entirely on the stack and later scalars still use the free registers.
    std::vector<ArgPlacement> small_struct(size_t words) {
        std::vector<ArgPlacement> out;
        const bool fits = m_regs_used + words <= m_tri.arg_regs.size();
        for (size_t w = 0; w < words; w++) {
            out.push_back(m_tri.split_struct_args || fits ? scalar() : ArgPlacement{true, m_stack_used++});
        }
        return out;
    }

    /// The pieces of a small float-containing struct, each in an argument register of its
    /// class. Empty when the registers of either class cannot hold all of them (the caller
    /// then passes the struct's integer words instead).
    std::optional<std::vector<ArgPlacement>> fp_struct(const std::vector<StructPiece>& pieces) {
        size_t ints = 0;
        size_t fps = 0;
        for (const StructPiece& p : pieces) (p.fp ? fps : ints)++;
        if (m_regs_used + ints > m_tri.arg_regs.size() || m_fp_used + fps > m_tri.fp_arg_regs.size()) {
            return std::nullopt;
        }
        std::vector<ArgPlacement> out;
        for (const StructPiece& p : pieces) {
            if (p.fp) {
                ArgPlacement a{false, m_fp_used++};
                a.fp_reg = true;
                out.push_back(a);
            } else {
                out.push_back({false, m_regs_used++});
            }
        }
        return out;
    }

    /// A struct passed in memory: every word goes on the stack.
    std::vector<ArgPlacement> stack_struct(size_t words) {
        std::vector<ArgPlacement> out;
        for (size_t w = 0; w < words; w++) out.push_back({true, m_stack_used++});
        return out;
    }

    /// The `ordinal` STORE_PARAM expects: register index, or register count + stack index.
    size_t ordinal(const ArgPlacement& p) const {
        if (p.fp_reg) return static_cast<size_t>(FP_PARAM_ORDINAL_BASE) + p.index;
        return p.on_stack ? m_tri.arg_regs.size() + p.index : p.index;
    }

private:
    const TargetRegisterInfo& m_tri;
    size_t m_regs_used = 0;
    size_t m_fp_used = 0;
    size_t m_stack_used = 0;
};

/// How a struct passed by value is placed: `pieces` is empty for the integer-word form.
struct StructPlan {
    std::vector<ArgPlacement> places;
    std::vector<StructPiece> pieces;
};

/// The source location of an expression (its leftmost token), for diagnostics.
SourceLoc expr_source_loc(const NodeExpr& expr);

/// Human-readable type name for diagnostics (function pointers print as their signature).
std::string type_name(const Type& t);

/// What a call transfers control to: a named function, or the address in a vreg (D-014).
struct CallTarget {
    std::string name;                  ///< direct callee symbol
    std::optional<uint32_t> pointer;   ///< vreg holding a function address (indirect call)
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

    // ---- floating point (D-038), gen/float.cpp ----

    /// Push the value of a float literal as an f32/f64 constant.
    void gen_float_literal(const NodeExprFloatLit& lit);

    /// Convert the value `v` of type `from` (int or float) to the float type `to`. Widening
    /// is implicit everywhere; with `allow_narrow` (explicit casts) f64 may become f32.
    uint32_t to_float(uint32_t v, const Type& from, FloatType to, const SourceLoc& loc,
                      bool allow_narrow = false);

    /// True when either of the two values on top of the type stack is a float.
    bool top_two_have_float() const;

    /// Pop two operands and push `a OP b` where OP is FADD/FSUB/FMUL/FDIV, converting an
    /// integer or f32 operand as needed (the result type is f64 unless both are f32 / f32 and int).
    void gen_float_arith(IROpcode op, const SourceLoc& loc, const char* op_name);

    /// Pop two operands and push the 0/1 result of comparing them (FCMP_*).
    void gen_float_compare(IROpcode op, const SourceLoc& loc, const char* op_name);

    /// Negate the float on top of the stack.
    void gen_float_neg();

    /// Fail unless the value on top of the stack is not a float (conditions, bit operations, ...).
    void require_not_float(const SourceLoc& loc, const std::string& what);

    /// Convert the stack entry at `pos` to `want` when a float is involved. Returns true when
    /// `want` or the entry is a float (the entry is then converted or an error is reported).
    bool coerce_float_at(size_t pos, const Type& want, const SourceLoc& loc, const std::string& what);
    bool coerce_float_top(const Type& want, const SourceLoc& loc, const std::string& what) {
        return !m_tstack.empty() && coerce_float_at(m_tstack.size() - 1, want, loc, what);
    }

    /// Store the float on top of the stack to `addr` as `ft` and pop it.
    void store_float_top(uint32_t addr, FloatType ft);

    /// Store the value on top of the stack (a float or an integer) to the float at `addr`; for a
    /// compound `op` the current value is read first and the result is converted back to `ft`.
    void store_float_at(uint32_t addr, FloatType ft, AssignOp op, const SourceLoc& loc);

    /// Load/store a float scalar variable and the compound assignments on one.
    void load_float_var(const Var& var);
    void store_float_var(const Var& var, AssignOp op);

    /// Call returning a float.
    uint32_t emit_call_f(const CallTarget& target, const std::vector<IRValue>& args, IRWidth w,
                         int reg_args);

    /// Record an error in the engine and report it through the CLI/LSP path.
    [[noreturn]] void fail(const SourceLoc& loc, const std::string& msg) const;

    /// Record an error plus a "did you mean" note and replacement fix-it.
    [[noreturn]] void fail_with_suggestion(const SourceLoc& loc, const std::string& msg,
                                            const std::string& suggestion) const;

    void set_no_alloc(bool v) { m_no_alloc = v; }
    void set_emit_ir(bool v) { m_emit_ir = v; }
    void set_verify_ir(bool v) { m_verify_ir = v; }
    void set_dump_regalloc(bool v) { m_dump_regalloc = v; }
    void set_opt_level(int level) { m_opt_level = level; }
    void set_bounds_check(bool v) { m_bounds_check = v; }
    void set_null_check(bool v) { m_null_check = v; }
    void set_stack_probe(bool v) { m_stack_probe = v; }
    void set_debug_info(bool v) { m_debug_info = v; }
    void set_debug_source_path(const std::string& path) { m_debug_source_path = path; }

    /// Emit a conditional runtime check. A false `condition` prints `message`
    /// and exits with the check-failure status (D-035).
    void emit_runtime_check(IRValue condition, const char* message);

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

    /// Frame layout (Y2d). Every local object occupies `frame_bytes(var)` bytes
    /// of the frame starting at `stack_loc`, and its bytes ascend in memory from
    /// the lowest address (the C layout), so `base + offset` addresses a field or
    /// element and pointers into it can be walked forward.
    static size_t align_frame(size_t bytes, size_t align) { return (bytes + align - 1) & ~(align - 1); }
    /// Bytes of frame storage a variable occupies (arrays/structs: 8-byte granular).
    size_t frame_bytes(const Var& var) const;
    /// Lowest address of a frame object of `bytes` bytes starting at frame offset `slot`.
    uint32_t object_addr(size_t slot, size_t bytes);
    /// Lowest address of a local variable's storage.
    uint32_t var_addr(const Var& var);
    /// Reserve `bytes` of frame storage for an array/struct (8-byte aligned); returns its slot.
    uint32_t alloc_object(size_t bytes);

    /// Struct-by-value ABI (D-012, Y8b): number of 8-byte argument words for a struct.
    static size_t struct_words(size_t bytes) { return (bytes + 7) / 8; }
    /// Placement of each argument word of a struct passed by value (D-012). A struct larger
    /// than 16 bytes is one pointer word where the ABI passes it by reference, else memory.
    std::vector<ArgPlacement> place_struct_arg(ArgPlacer& placer, const StructInfo& info) const;
    /// A small struct that contains floats travels as pieces (X1b, D-044); empty when the
    /// struct is not one (it then uses the plain integer words).
    std::vector<StructPiece> fp_struct_pieces(const StructInfo& info) const;
    void collect_leaves(const StructInfo& info, size_t base, std::vector<StructLeaf>& out) const;
    /// Where a by-value struct argument goes: pieces in FP/integer registers when it has them
    /// and they fit, else words.
    StructPlan plan_struct_arg(ArgPlacer& placer, const StructInfo& info) const;
    uint32_t load_struct_piece(uint32_t base, const StructInfo& info, const StructPiece& p);
    void store_struct_piece(uint32_t base, const StructInfo& info, const StructPiece& p, uint32_t value);
    void store_param_piece(const Var& var, const StructInfo& info, const StructPiece& p, size_t ordinal);
    IRValue struct_piece_value(uint32_t vreg, const StructPiece& p) const;
    uint32_t load_struct_range(uint32_t base, const StructInfo& info, size_t off, size_t n);
    void store_struct_range(uint32_t base, const StructInfo& info, size_t off, size_t n, uint32_t value);
    void store_param_range(const Var& var, const StructInfo& info, size_t off, size_t n, size_t ordinal);
    /// Read word `word` of the struct at address `base` (only real bytes are read).
    uint32_t load_struct_word(uint32_t base, const StructInfo& info, size_t word);
    /// Write word `word` of the struct at address `base` from a vreg (only real bytes are written).
    void store_struct_word(uint32_t base, const StructInfo& info, size_t word, uint32_t value);
    /// Copy exactly `bytes` bytes between two addresses (ascending), using accesses no wider
    /// than `align`, the alignment of both pointers.
    void copy_memory(uint32_t dst_ptr, uint32_t src_ptr, size_t bytes, size_t align = 8);
    /// `return <struct value at addr>`: one word in the return register, two in the pair.
    void gen_struct_return(uint32_t addr, const StructInfo& info);
    /// Emit a call whose result is a struct and push the temporary holding it as a struct
    /// value. `hidden_result` is the buffer passed as the first argument for results that
    /// do not fit in registers (the callee fills it).
    void gen_struct_call(const CallTarget& target, const std::vector<IRValue>& args,
                         const std::string& struct_name, const StructInfo& info,
                         std::optional<uint32_t> hidden_result = std::nullopt, int reg_args = -1);
    /// Function entry: store incoming argument word `ordinal` into word `word` of a
    /// struct parameter's frame slot (only real bytes are written).
    void store_param_word(const Var& var, const StructInfo& info, size_t word, size_t ordinal);

    /// Compute the byte address of a (possibly chained) field access and
    /// return its declared type through `out_type`.
    uint32_t field_addr(const NodeExprFieldAccess& access, Type& out_type);
    /// Compute the byte address of an (possibly chained) array index.
    uint32_t index_addr(const NodeExprIndex& index, Type& out_type);
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
    /// Check argument count and types against a signature (inserting safe widenings).
    /// The argument values are the top `call.args.size()` entries of the value/type stacks.
    void check_call_args(const NodeExprCall& call, SourceLoc loc, const std::string& name,
                         const std::vector<std::optional<Type>>& param_types, size_t param_count);
    /// Result type of an integer binary operator (D-044): an integer constant next to a typed
    /// integer takes that type when it fits, otherwise the wider operand wins (ties keep the left
    /// type). There is no promotion: `u8 + u8` is a `u8`. Non-integer operands give the default i64.
    /// Integer division/modulo/right shift/ordered compare that honour u64 (BF-010): the other
    /// widths hold sign- or zero-extended values, so the signed IR ops already agree with C.
    /// With --bounds: trap ("index out of bounds") unless 0 <= index < dim. Used for loads and stores.
    void emit_bounds_check(uint32_t index, size_t dim);
    uint32_t emit_int_div(uint32_t a, uint32_t b, bool is_u64, bool modulo);
    uint32_t emit_int_shr(uint32_t a, uint32_t b, bool is_u64);
    uint32_t emit_int_cmp(IROpcode op, uint32_t a, uint32_t b, bool is_u64);
    Type int_binop_type(const NodeExpr* lhs, const NodeExpr* rhs, const Type& ta,
                        const Type& tb) const;
    /// Re-normalise the integer on top of the value stack to `t`'s width (sign- or zero-extend
    /// from 8/16/32 bits) so a result wraps at its type's width like C; 64-bit types are a no-op.
    void wrap_int_top(const Type& t);
    /// Type the integer result on top of the stacks as `int_binop_type` says, wrapping it first
    /// when `wrap` is set (operators whose result can leave the type's range).
    void finish_int_binop(const NodeExpr* lhs, const NodeExpr* rhs, const Type& ta,
                          const Type& tb, bool wrap);
    /// If `want` is a function-pointer type, the value on top of the type stack must have
    /// exactly that type (or be the constant 0); otherwise report an error naming `what`.
    void check_fn_ptr_assign(const Type& want, const NodeExpr* expr, SourceLoc loc,
                             const std::string& what);
    /// The function-pointer type `fn(params) -> ret` of a defined function (untyped
    /// parameters are i64).
    Type function_pointer_type(const NodeFuncDef& func);
    /// The innermost variable named `name`, or null.
    const Var* find_var(const std::string& name) const;
    /// Emit a direct or indirect call (see CallTarget) returning one / two words.
    uint32_t emit_call(const CallTarget& target, const std::vector<IRValue>& args, int reg_args);
    uint32_t emit_call2(const CallTarget& target, const std::vector<IRValue>& args,
                        uint32_t& second, int reg_args, IRWidth w1 = IRWidth::I64,
                        IRWidth w2 = IRWidth::I64);

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

    /// Finalize an IR function: run allocator, rewriter, emit to backend.
    /// `frame_bytes` is the frame size captured when the function was generated;
    /// the function object itself is owned by the caller.
    void flush_function(IRFunction* func, uint32_t frame_bytes, bool emit_extern,
                        bool optimize = true);

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
    bool m_verify_ir = false;
    bool m_dump_regalloc = false;
    int m_opt_level = 0;
    bool m_bounds_check = false;
    bool m_null_check = false;
    bool m_stack_probe = false;
    bool m_debug_info = false;
    std::string m_debug_source_path;  ///< absolute path used in debug line markers
    uint32_t m_check_counter = 0;   ///< unique labels for emitted checks

    // ---- IR state ----
    /// A generated function awaiting optimization and emission (O7b1). The
    /// frame size is captured at generation time because generation state is
    /// reset before the deferred function is finally flushed.
    struct PendingFunction {
        std::unique_ptr<IRFunction> ir;
        uint32_t frame_bytes = 0;
    };
    std::vector<PendingFunction> m_pending_functions;

    IRBuilder m_ir;
    std::vector<uint32_t> m_vstack;
    std::vector<Type> m_tstack;     ///< Type stack, parallel to m_vstack
    uint32_t m_frame_slots = 0;     ///< Number of frame slots allocated in current function
    uint32_t m_next_frame_offset = 0; ///< Next free byte offset in the stack frame
    /// Frame slot holding the hidden result pointer while compiling a function that
    /// returns a struct larger than 16 bytes.
    std::optional<size_t> m_ret_ptr_slot;

    // Type pool: persistent storage for Type::pointee pointers
    std::list<Type> m_type_pool;
    /// Parameter arrays of function types built by the generator (stable addresses).
    std::list<std::vector<Type>> m_sig_pool;

    TargetRegisterInfo m_tri = TargetRegisterInfo::x86_64_systemv();
};
