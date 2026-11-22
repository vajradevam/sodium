#include "test_framework.hpp"
#include "tokenization.hpp"

#include <string>
#include <vector>

using TT = TokenType;

static std::vector<Token> tok(const std::string& src) {
    return Tokenizer(src, "unit.cyan").tokenize();
}

TEST(tokenizer_empty_source_yields_no_tokens) {
    CHECK(tok("").empty());
    CHECK(tok("   \n\t\n").empty());
}

TEST(tokenizer_simple_let_statement) {
    auto t = tok("var x = 1 + 2;");
    CHECK_EQ(t.size(), 7u);
    CHECK(t[0].type == TT::let);
    CHECK(t[1].type == TT::ident);
    CHECK(t[1].value.has_value() && *t[1].value == "x");
    CHECK(t[2].type == TT::eq);
    CHECK(t[3].type == TT::int_lit && *t[3].value == "1");
    CHECK(t[4].type == TT::plus);
    CHECK(t[5].type == TT::int_lit && *t[5].value == "2");
    CHECK(t[6].type == TT::semi);
}

TEST(tokenizer_recognizes_declaration_keywords) {
    auto t = tok("function return if else while for print break continue do switch case const default global static struct");
    const TT want[] = {TT::_function, TT::_return, TT::_if, TT::_else, TT::_while,
                       TT::_for, TT::_print, TT::_break, TT::_continue, TT::_do,
                       TT::_switch, TT::_case, TT::_const, TT::_default, TT::_global,
                       TT::_static, TT::_struct};
    CHECK_EQ(t.size(), sizeof(want) / sizeof(want[0]));
    for (size_t i = 0; i < t.size() && i < sizeof(want) / sizeof(want[0]); ++i) {
        CHECK(t[i].type == want[i]);
    }
}

TEST(tokenizer_recognizes_type_keywords) {
    auto t = tok("i8 i16 i32 i64 u8 u16 u32 u64");
    const TT want[] = {TT::_i8, TT::_i16, TT::_i32, TT::_i64,
                       TT::_u8, TT::_u16, TT::_u32, TT::_u64};
    CHECK_EQ(t.size(), 8u);
    for (size_t i = 0; i < t.size() && i < 8; ++i) CHECK(t[i].type == want[i]);
}

TEST(tokenizer_hex_literal_becomes_decimal_string) {
    auto t = tok("0xFF 0X10 0x0");
    CHECK_EQ(t.size(), 3u);
    CHECK(*t[0].value == "255");
    CHECK(*t[1].value == "16");
    CHECK(*t[2].value == "0");
}

TEST(tokenizer_string_literal_decodes_escapes) {
    auto t = tok("\"a\\nb\\t\\x41\"");
    CHECK_EQ(t.size(), 1u);
    CHECK(t[0].type == TT::string_lit);
    CHECK(t[0].value.has_value());
    CHECK_EQ(t[0].value->substr(0, 3), std::string("a\nb"));
    CHECK_EQ(t[0].value->at(3), '\t');
    CHECK_EQ(t[0].value->at(4), 'A');
}

TEST(tokenizer_skips_line_and_block_comments) {
    auto t = tok("var a = 1; // trailing\n/* block\n comment */ var b = 2;");
    CHECK_EQ(t.size(), 10u);
    CHECK(t[0].type == TT::let);
    CHECK(t[4].type == TT::semi);
    CHECK(t[5].type == TT::let);
}

TEST(tokenizer_two_char_operators) {
    auto t = tok("== != <= >= += -= *= /= ++ -- << >> && || %= &= |= ^= <<= >>=");
    const TT want[] = {TT::eq_eq, TT::neq, TT::lte, TT::gte, TT::pluseq, TT::minuseq,
                       TT::stareq, TT::slasheq, TT::plusplus, TT::minusminus,
                       TT::shl, TT::shr, TT::and_t, TT::or_t, TT::percenteq,
                       TT::ampeq, TT::pipeeq, TT::careteq, TT::shleq, TT::shreq};
    CHECK_EQ(t.size(), sizeof(want) / sizeof(want[0]));
    for (size_t i = 0; i < t.size() && i < sizeof(want) / sizeof(want[0]); ++i) {
        CHECK(t[i].type == want[i]);
    }
}

TEST(tokenizer_single_char_operators_and_punctuation) {
    auto t = tok("+ - * / % & | ^ ~ ! ? : . , ; ( ) [ ] { }");
    CHECK_EQ(t.size(), 21u);
    CHECK(t[0].type == TT::plus);
    CHECK(t[5].type == TT::amp);
    CHECK(t[13].type == TT::comma);
    CHECK(t[15].type == TT::open_paren);
    CHECK(t[20].type == TT::close_brace);
}

TEST(tokenizer_tracks_line_and_column) {
    auto t = tok("var a;\n  var b;");
    CHECK_EQ(t[0].loc.line, 1u);
    CHECK_EQ(t[0].loc.col, 1u);
    CHECK_EQ(t[3].loc.line, 2u);
    CHECK_EQ(t[3].loc.col, 3u);
}

TEST(tokenizer_reset_allows_repeated_tokenize) {
    Tokenizer tz("var x = 1;", "again.cyan");
    auto a = tz.tokenize();
    auto b = tz.tokenize();
    CHECK_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        CHECK(a[i].type == b[i].type);
        CHECK_EQ(a[i].loc.line, b[i].loc.line);
        CHECK_EQ(a[i].loc.col, b[i].loc.col);
    }
}

TEST(tokenizer_records_filename_in_location) {
    auto t = Tokenizer("return 1;", "named.cyan").tokenize();
    CHECK_EQ(t.size(), 3u);
    CHECK(t[0].loc.file == "named.cyan");
}

TEST(tokenizer_line_directive_updates_location) {
    auto t = tok("# 42 \"other.cyan\"\nreturn 1;");
    CHECK(!t.empty());
    CHECK(t.front().type == TT::_return);
    CHECK_EQ(t.front().loc.line, 42u);
    CHECK(t.front().loc.file == "other.cyan");
}

TEST(tokenizer_recognizes_arrow_for_return_types) {
    auto t = tok("->");
    CHECK_EQ(t.size(), 1u);
    CHECK(t[0].type == TT::arrow);
}

TEST(tokenizer_recognizes_bool_char_null_keywords) {
    auto t = tok("bool char true false null");
    const TT want[] = {TT::_bool, TT::_char, TT::_true, TT::_false, TT::_null};
    CHECK_EQ(t.size(), sizeof(want) / sizeof(want[0]));
    for (size_t i = 0; i < t.size() && i < sizeof(want) / sizeof(want[0]); ++i)
        CHECK(t[i].type == want[i]);
}
