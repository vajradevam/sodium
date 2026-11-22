#include "test_framework.hpp"
#include "parser.hpp"

#include <memory>
#include <string>
#include <variant>

struct ExprFixture {
    std::unique_ptr<Parser> parser;
    NodeExpr* expr = nullptr;

    explicit ExprFixture(const std::string& src, const std::string& file = "unit.cyan") {
        auto toks = Tokenizer(src, file).tokenize();
        parser = std::make_unique<Parser>(std::move(toks));
        auto parsed = parser->parse_expr();
        if (parsed) expr = *parsed;
    }
};

static NodeProg parse_program(const std::string& src) {
    Tokenizer tz(src, "unit.cyan");
    Parser parser(tz.tokenize());
    auto prog = parser.parse_prog();
    return prog ? std::move(*prog) : NodeProg{};
}

static BinExpr* as_bin(NodeExpr* e) {
    if (!e) return nullptr;
    if (auto* b = std::get_if<BinExpr*>(&e->var)) return *b;
    return nullptr;
}

static NodeExpr* bin_lhs(BinExpr* b) {
    if (!b) return nullptr;
    return std::visit([](auto* p) -> NodeExpr* { return p->lhs; }, b->var);
}

static NodeExpr* bin_rhs(BinExpr* b) {
    if (!b) return nullptr;
    return std::visit([](auto* p) -> NodeExpr* { return p->rhs; }, b->var);
}

TEST(parser_integer_literal_shape) {
    ExprFixture f("42");
    CHECK(f.expr != nullptr);
    auto* lit = f.expr ? std::get_if<NodeExprIntLit*>(&f.expr->var) : nullptr;
    CHECK(lit != nullptr);
    if (lit) CHECK_EQ((*lit)->int_lit.value.value(), std::string("42"));
}

TEST(parser_identifier_shape) {
    ExprFixture f("counter");
    auto* id = f.expr ? std::get_if<NodeExprIdent*>(&f.expr->var) : nullptr;
    CHECK(id != nullptr);
    if (id) CHECK_EQ((*id)->ident.value.value(), std::string("counter"));
}

TEST(parser_multiplication_binds_tighter_than_addition) {
    ExprFixture f("1 + 2 * 3");
    BinExpr* top = as_bin(f.expr);
    CHECK(top != nullptr);
    CHECK(std::holds_alternative<BinExprAdd*>(top->var));
    CHECK(as_bin(bin_rhs(top)) != nullptr);
    if (BinExpr* rhs = as_bin(bin_rhs(top))) {
        CHECK(std::holds_alternative<BinExprMulti*>(rhs->var));
    }
}

TEST(parser_subtraction_is_left_associative) {
    ExprFixture f("1 - 2 - 3");
    BinExpr* top = as_bin(f.expr);
    CHECK(top != nullptr);
    CHECK(std::holds_alternative<BinExprSub*>(top->var));
    if (BinExpr* lhs = as_bin(bin_lhs(top))) {
        CHECK(std::holds_alternative<BinExprSub*>(lhs->var));
    } else {
        CHECK(false);
    }
}

TEST(parser_parentheses_override_precedence) {
    ExprFixture f("(1 + 2) * 3");
    BinExpr* top = as_bin(f.expr);
    CHECK(top != nullptr);
    CHECK(std::holds_alternative<BinExprMulti*>(top->var));
    if (BinExpr* lhs = as_bin(bin_lhs(top))) {
        CHECK(std::holds_alternative<BinExprAdd*>(lhs->var));
    } else {
        CHECK(false);
    }
}

TEST(parser_comparison_is_below_arithmetic) {
    ExprFixture f("1 + 2 < 4");
    BinExpr* top = as_bin(f.expr);
    CHECK(top != nullptr);
    CHECK(std::holds_alternative<BinExprLT*>(top->var));
    if (BinExpr* lhs = as_bin(bin_lhs(top))) {
        CHECK(std::holds_alternative<BinExprAdd*>(lhs->var));
    } else {
        CHECK(false);
    }
}

TEST(parser_ternary_shape) {
    ExprFixture f("1 ? 2 : 3");
    auto* t = f.expr ? std::get_if<NodeExprTernary*>(&f.expr->var) : nullptr;
    CHECK(t != nullptr);
    if (t) CHECK((*t)->cond != nullptr && (*t)->then_expr != nullptr && (*t)->else_expr != nullptr);
}

TEST(parser_call_with_arguments) {
    ExprFixture f("sum(1, 2, 3)");
    auto* c = f.expr ? std::get_if<NodeExprCall*>(&f.expr->var) : nullptr;
    CHECK(c != nullptr);
    if (c) {
        CHECK_EQ((*c)->name.value.value(), std::string("sum"));
        CHECK_EQ((*c)->args.size(), 3u);
    }
}

TEST(parser_array_index_shape) {
    ExprFixture f("arr[i + 1]");
    auto* ix = f.expr ? std::get_if<NodeExprIndex*>(&f.expr->var) : nullptr;
    CHECK(ix != nullptr);
    if (ix) {
        CHECK_EQ((*ix)->name.value.value(), std::string("arr"));
        CHECK((*ix)->index != nullptr);
    }
}

TEST(parser_string_literal_shape) {
    ExprFixture f("\"hello\"");
    auto* s = f.expr ? std::get_if<NodeExprStringLit*>(&f.expr->var) : nullptr;
    CHECK(s != nullptr);
    if (s) CHECK_EQ((*s)->value.value.value(), std::string("hello"));
}

TEST(parser_field_access_shape) {
    ExprFixture f("point.x");
    auto* fa = f.expr ? std::get_if<NodeExprFieldAccess*>(&f.expr->var) : nullptr;
    CHECK(fa != nullptr);
    if (fa) {
        CHECK_EQ((*fa)->obj_name.value.value(), std::string("point"));
        CHECK_EQ((*fa)->field_name.value.value(), std::string("x"));
    }
}

TEST(parser_program_records_function_and_body) {
    auto prog = parse_program("function main() { return 7; }");
    CHECK_EQ(prog.funcs.size(), 1u);
    CHECK_EQ(prog.stmts.size(), 0u);
    if (prog.funcs.empty()) return;
    CHECK_EQ(prog.funcs[0]->name.value.value(), std::string("main"));
    CHECK_EQ(prog.funcs[0]->body->stmts.size(), 1u);
    if (prog.funcs[0]->body->stmts.empty()) return;
    CHECK(std::holds_alternative<NodeStmtReturn*>(prog.funcs[0]->body->stmts[0].var));
}

TEST(parser_program_records_global_and_struct) {
    auto prog = parse_program("global var g = 1; struct Point { var x; var y; };");
    CHECK_EQ(prog.stmts.size(), 1u);
    CHECK_EQ(prog.structs.size(), 1u);
    if (!prog.stmts.empty()) CHECK(std::holds_alternative<NodeStmtGlobal*>(prog.stmts[0].var));
    if (!prog.structs.empty()) {
        CHECK_EQ(prog.structs[0]->name.value.value(), std::string("Point"));
        CHECK_EQ(prog.structs[0]->fields.size(), 2u);
    }
}

TEST(parser_print_statement_splits_format_specifiers) {
    auto prog = parse_program("function main() { print(\"%d %s\", 1, \"x\"); }");
    CHECK_EQ(prog.funcs.size(), 1u);
    if (prog.funcs.empty() || prog.funcs[0]->body->stmts.empty()) return;
    auto* p = std::get_if<NodeStmtPrint*>(&prog.funcs[0]->body->stmts[0].var);
    CHECK(p != nullptr);
    if (!p) return;
    CHECK_EQ((*p)->specs.size(), 3u); // Int, Text(" "), String
    CHECK_EQ((*p)->args.size(), 2u);
}

TEST(parser_typed_signature_with_pointer_param) {
    NodeProg prog = parse_program("function f(a: i32, p: u8*) -> i64 { return(0); }");
    CHECK_EQ(prog.funcs.size(), size_t(1));
    if (prog.funcs.empty()) return;
    const NodeFuncDef& f = *prog.funcs[0];
    CHECK_EQ(f.params.size(), size_t(2));
    CHECK_EQ(f.param_types.size(), size_t(2));
    CHECK(f.param_types[0].has_value());
    CHECK(f.param_types[1].has_value());
    if (f.param_types[0]) CHECK(*f.param_types[0] == Type::make_int(IntType::i32));
    if (f.param_types[1]) {
        CHECK_EQ(f.param_types[1]->kind, Type::Ptr);
        CHECK(f.param_types[1]->pointee != nullptr);
        if (f.param_types[1]->pointee)
            CHECK(*f.param_types[1]->pointee == Type::make_int(IntType::u8));
    }
    CHECK(f.return_type.has_value());
    if (f.return_type) CHECK(*f.return_type == Type::make_int(IntType::i64));
}

TEST(parser_untyped_signature_has_no_types) {
    NodeProg prog = parse_program("function f(a, b) { return(a); }");
    CHECK_EQ(prog.funcs.size(), size_t(1));
    if (prog.funcs.empty()) return;
    const NodeFuncDef& f = *prog.funcs[0];
    CHECK_EQ(f.params.size(), size_t(2));
    CHECK_EQ(f.param_types.size(), size_t(2));
    CHECK(!f.param_types[0].has_value());
    CHECK(!f.param_types[1].has_value());
    CHECK(!f.return_type.has_value());
}

TEST(parser_nested_pointer_type_is_nestable) {
    NodeProg prog = parse_program("function f(p: i32**) -> u8* { return(0); }");
    CHECK_EQ(prog.funcs.size(), size_t(1));
    if (prog.funcs.empty()) return;
    const NodeFuncDef& f = *prog.funcs[0];
    CHECK(f.param_types[0].has_value());
    if (f.param_types[0]) {
        CHECK_EQ(f.param_types[0]->kind, Type::Ptr);
        CHECK(f.param_types[0]->pointee != nullptr);
        if (f.param_types[0]->pointee) {
            CHECK_EQ(f.param_types[0]->pointee->kind, Type::Ptr);
            CHECK(f.param_types[0]->pointee->pointee != nullptr);
        }
    }
    CHECK(f.return_type.has_value());
    if (f.return_type) CHECK_EQ(f.return_type->kind, Type::Ptr);
}

TEST(parser_typed_struct_fields) {
    NodeProg prog = parse_program("struct S { var a: i8; var b: i32; var c; };");
    CHECK_EQ(prog.structs.size(), size_t(1));
    if (prog.structs.empty()) return;
    const NodeStructDef& s = *prog.structs[0];
    CHECK_EQ(s.fields.size(), size_t(3));
    CHECK_EQ(s.field_types.size(), size_t(3));
    CHECK(s.field_types[0].has_value());
    CHECK(s.field_types[1].has_value());
    CHECK(!s.field_types[2].has_value());
    if (s.field_types[0]) CHECK(*s.field_types[0] == Type::make_int(IntType::i8));
    if (s.field_types[1]) CHECK(*s.field_types[1] == Type::make_int(IntType::i32));
}

TEST(parser_sizeof_type_and_expr) {
    NodeProg prog = parse_program("struct S { var a: i8; }; function f() { print(sizeof(i32)); print(sizeof(x)); print(sizeof(S)); return(0); }");
    CHECK_EQ(prog.funcs.size(), size_t(1));
    if (prog.funcs.empty() || prog.funcs[0]->body->stmts.size() < 3) return;
    auto* p1 = std::get_if<NodeStmtPrint*>(&prog.funcs[0]->body->stmts[0].var);
    CHECK(p1 != nullptr);
    if (!p1 || (*p1)->args.empty()) return;
    auto* sz = std::get_if<NodeExprSizeof*>(&(*p1)->args[0]->var);
    CHECK(sz != nullptr);
    if (!sz) return;
    CHECK((*sz)->type.has_value());
    if ((*sz)->type) CHECK(*((*sz)->type) == Type::make_int(IntType::i32));
}

TEST(parser_call_suffix_makes_indirect_call) {
    // `s.cb(1)(2)`: a call on a call on a field access; both callees are expressions.
    ExprFixture f("s.cb(1)(2)");
    CHECK(f.expr != nullptr);
    auto* outer = f.expr ? std::get_if<NodeExprCall*>(&f.expr->var) : nullptr;
    CHECK(outer != nullptr);
    if (!outer) return;
    CHECK_EQ((*outer)->args.size(), static_cast<size_t>(1));
    CHECK((*outer)->callee != nullptr);
    auto* inner = (*outer)->callee ? std::get_if<NodeExprCall*>(&(*outer)->callee->var) : nullptr;
    CHECK(inner != nullptr);
    if (!inner) return;
    CHECK((*inner)->callee != nullptr);
    CHECK((*inner)->callee ? std::holds_alternative<NodeExprFieldAccess*>((*inner)->callee->var) : false);
}

TEST(parser_plain_call_has_no_callee_expression) {
    ExprFixture f("add(1, 2)");
    auto* call = f.expr ? std::get_if<NodeExprCall*>(&f.expr->var) : nullptr;
    CHECK(call != nullptr);
    if (call) {
        CHECK((*call)->callee == nullptr);
        CHECK_EQ((*call)->name.value.value(), std::string("add"));
    }
}

TEST(parser_function_pointer_type_in_declaration) {
    NodeProg prog = parse_program(
        "function f(cb: fn(i64, i64) -> i64, g: fn(i64)) -> fn(i64) -> i64 { return g; }\n");
    CHECK_EQ(prog.funcs.size(), static_cast<size_t>(1));
    if (prog.funcs.empty()) return;
    const NodeFuncDef& fn = *prog.funcs[0];
    CHECK_EQ(fn.param_types.size(), static_cast<size_t>(2));
    CHECK(fn.param_types[0].has_value());
    CHECK(fn.return_type.has_value());
    if (!fn.param_types[0] || !fn.param_types[1] || !fn.return_type) return;
    const Type& cb = *fn.param_types[0];
    CHECK(cb.kind == Type::Ptr);
    CHECK(cb.pointee != nullptr && cb.pointee->kind == Type::Func);
    if (cb.pointee) {
        CHECK_EQ(cb.pointee->fn_nparams, static_cast<uint32_t>(2));
        CHECK(cb.pointee->fn_ret != nullptr);
    }
    const Type& g = *fn.param_types[1];
    CHECK(g.pointee != nullptr && g.pointee->kind == Type::Func);
    if (g.pointee) CHECK(g.pointee->fn_ret == nullptr);   // no `->`: unannotated
}
