#include "test_framework.hpp"
#include "parser.hpp"
#include "generation.hpp"
#include "backend/x86_64/backend.hpp"
#include "ir/target_regs.hpp"

#include <memory>
#include <optional>
#include <sstream>
#include <string>

struct ConstFixture {
    std::unique_ptr<Parser> parser;
    std::unique_ptr<X8664Backend> backend;
    std::ostringstream sink;
    std::unique_ptr<Generator> gen;
    NodeExpr* expr = nullptr;

    explicit ConstFixture(const std::string& src) {
        auto toks = Tokenizer(src, "unit.cyan").tokenize();
        parser = std::make_unique<Parser>(std::move(toks));
        auto parsed = parser->parse_expr();
        if (parsed) expr = *parsed;
        backend = std::make_unique<X8664Backend>(sink);
        gen = std::make_unique<Generator>(NodeProg{}, *backend,
                                          TargetRegisterInfo::x86_64_systemv());
    }

    std::optional<int64_t> eval() { return gen->eval_const_expr(expr); }
};

struct LspModeGuard {
    LspModeGuard() { g_lsp_mode = true; g_lsp_errors.clear(); }
    ~LspModeGuard() { g_lsp_mode = false; g_lsp_errors.clear(); }
};

TEST(const_eval_integer_literal) {
    ConstFixture f("7");
    auto v = f.eval();
    CHECK(v.has_value());
    if (v) CHECK_EQ(*v, 7);
}

TEST(const_eval_hex_literal) {
    ConstFixture f("0x2A");
    auto v = f.eval();
    CHECK(v.has_value());
    if (v) CHECK_EQ(*v, 42);
}

TEST(const_eval_arithmetic_precedence) {
    ConstFixture f("1 + 2 * 3");
    auto v = f.eval();
    CHECK(v.has_value());
    if (v) CHECK_EQ(*v, 7);
}

TEST(const_eval_shifts) {
    ConstFixture f("(1 << 10) >> 2");
    auto v = f.eval();
    CHECK(v.has_value());
    if (v) CHECK_EQ(*v, 256);
}

TEST(const_eval_division_and_modulo) {
    ConstFixture f("17 / 5 + 17 % 5");
    auto v = f.eval();
    CHECK(v.has_value());
    if (v) CHECK_EQ(*v, 5);
}

TEST(const_eval_comparisons) {
    ConstFixture f("3 < 4");
    auto v = f.eval();
    CHECK(v.has_value());
    if (v) CHECK_EQ(*v, 1);
}

TEST(const_eval_non_constant_identifier_is_nullopt) {
    ConstFixture f("unknown_name");
    CHECK(!f.eval().has_value());
}

TEST(const_eval_division_by_zero_reports_error) {
    LspModeGuard guard;
    ConstFixture f("1 / 0");
    bool threw = false;
    try { (void)f.eval(); } catch (const LSPAbort&) { threw = true; }
    CHECK(threw);
    CHECK(!g_lsp_errors.empty());
    if (!g_lsp_errors.empty()) {
        CHECK(g_lsp_errors[0].msg.find("Division by zero") != std::string::npos);
    }
}

TEST(const_eval_overflow_reports_error) {
    LspModeGuard guard;
    ConstFixture f("9223372036854775807 + 1");
    bool threw = false;
    try { (void)f.eval(); } catch (const LSPAbort&) { threw = true; }
    CHECK(threw);
    CHECK(!g_lsp_errors.empty());
}
