#include "test_framework.hpp"
#include "preprocessor.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <functional>

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

TEST(preprocessor_plain_source_passes_through) {
    auto r = preprocess("var x = 1;\nreturn x;\n", "plain.cyan", {});
    CHECK_EQ(r.expanded_source, std::string("var x = 1;\nreturn x;\n"));
    CHECK(r.included_files.empty());
}

TEST(preprocessor_removes_pragma_once) {
    auto r = preprocess("#pragma once\nvar x = 1;\n", "p.cyan", {});
    CHECK(!contains(r.expanded_source, "#pragma once"));
    CHECK(contains(r.expanded_source, "var x = 1;"));
}

TEST(preprocessor_passes_error_directive_through) {
    auto r = preprocess("#error deliberate failure\nvar x = 1;\n", "e.cyan", {});
    CHECK(contains(r.expanded_source, "#error deliberate failure"));
}

TEST(preprocessor_emits_error_for_missing_include) {
    auto r = preprocess("#include \"definitely_missing_xyz.cyan\"\n", "m.cyan", {});
    CHECK(contains(r.expanded_source, "#error"));
    CHECK(contains(r.expanded_source, "definitely_missing_xyz.cyan"));
}

TEST(preprocessor_resolves_relative_include) {
    auto r = preprocess("#include \"helper.cyan\"\n",
                        "tests/include/user.cyan", {"tests/include"});
    CHECK(contains(r.expanded_source, "function add(a, b)"));
    CHECK(!r.included_files.empty());
    CHECK(contains(r.included_files.front(), "helper.cyan"));
    CHECK(contains(r.expanded_source, "# 1 \""));
}

TEST(preprocessor_emits_line_marker_for_parent_after_include) {
    auto r = preprocess("#include \"helper.cyan\"\nvar x = 1;\n",
                        "tests/include/user.cyan", {"tests/include"});
    CHECK(contains(r.expanded_source, "var x = 1;"));
    // Marker returning to the parent file names it and points at line 2.
    CHECK(contains(r.expanded_source, "# 2 \"tests/include/user.cyan\""));
}

TEST(preprocessor_pragma_once_prevents_second_inclusion) {
    auto r = preprocess("#include \"test_pragma_once.cyan\"\n",
                        "tests/include/top.cyan", {"tests/include"});
    // helper.cyan has #pragma once; test_pragma_once includes it twice.
    size_t first = r.expanded_source.find("function add(a, b)");
    CHECK(first != std::string::npos);
    size_t second = r.expanded_source.find("function add(a, b)", first + 1);
    CHECK_EQ(second, std::string::npos);
}

TEST(preprocessor_included_files_tracks_resolution) {
    auto r = preprocess("#include \"helper.cyan\"\n", "tests/include/user.cyan", {});
    CHECK_EQ(r.included_files.size(), 1u);
    CHECK(contains(r.included_files[0], "tests/include/helper.cyan"));
}

TEST(preprocessor_handles_angle_include_via_search_dir) {
    auto r = preprocess("#include <helper.cyan>\n", "elsewhere.cyan", {"tests/include"});
    CHECK(contains(r.expanded_source, "function add(a, b)"));
}

TEST(preprocessor_handles_empty_source) {
    auto r = preprocess("", "empty.cyan", {});
    CHECK(r.expanded_source.empty());
    CHECK(r.included_files.empty());
}

TEST(preprocessor_preserves_regular_lines_verbatim) {
    auto r = preprocess("  var   spaced = 1;  \n", "s.cyan", {});
    CHECK_EQ(r.expanded_source, std::string("  var   spaced = 1;  \n"));
}

// --- X2a: object macros and conditionals ----------------------------------

namespace {

std::string pp_x2(const std::string& source) {
    return preprocess(source, "<test>", {}, "").expanded_source;
}

} // namespace

TEST(preprocessor_object_macro_expands_identifiers) {
    std::string out = pp_x2("#define N 42\nvar x = N;\n");
    CHECK(contains(out, "var x = 42;"));
    CHECK(!contains(out, "\nN"));
}

TEST(preprocessor_object_macro_skips_string_literals) {
    std::string out = pp_x2("#define N 42\nprint(\"N = N\", N);\n");
    CHECK(contains(out, "\"N = N\""));
    CHECK(contains(out, "42"));
}

TEST(preprocessor_object_macro_respects_identifier_boundaries) {
    std::string out = pp_x2("#define N 42\nvar NAME = 1;\nvar N2 = 2;\n");
    CHECK(contains(out, "var NAME = 1;"));
    CHECK(contains(out, "var N2 = 2;"));
}

TEST(preprocessor_ifdef_selects_then_branch) {
    std::string out = pp_x2(
        "#define FEATURE 1\n#ifdef FEATURE\nvar a = 1;\n#else\nvar b = 2;\n#endif\n");
    CHECK(contains(out, "var a = 1;"));
    CHECK(!contains(out, "var b = 2;"));
}

TEST(preprocessor_ifndef_selects_else_branch) {
    std::string out = pp_x2("#ifndef FEATURE\nvar a = 1;\n#else\nvar b = 2;\n#endif\n");
    CHECK(contains(out, "var a = 1;"));
    CHECK(!contains(out, "var b = 2;"));
}

TEST(preprocessor_nested_conditionals) {
    std::string out = pp_x2(
        "#define OUTER 1\n"
        "#ifdef OUTER\n"
        "var a = 1;\n"
        "#ifndef INNER\n"
        "var b = 2;\n"
        "#else\n"
        "var c = 3;\n"
        "#endif\n"
        "#endif\n");
    CHECK(contains(out, "var a = 1;"));
    CHECK(contains(out, "var b = 2;"));
    CHECK(!contains(out, "var c = 3;"));
}

TEST(preprocessor_inactive_branch_does_not_expand_macros) {
    std::string out = pp_x2("#define N 42\n#ifdef MISSING\nvar x = N;\n#endif\n");
    CHECK(!contains(out, "var x"));
    CHECK(!contains(out, "42"));
}

TEST(preprocessor_include_guard_prevents_double_definition) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
        ("sodium_pp_guard_" + std::to_string(static_cast<size_t>(
            std::hash<std::string>{}(std::string(__FILE__)) ^ 0xA5A5u)));
    fs::create_directories(dir);
    {
        std::ofstream(dir / "guard.cyan") <<
            "#ifndef GUARD_H\n#define GUARD_H\nvar guarded = 7;\n#endif\n";
    }
    PreprocessedResult result = preprocess(
        "#include \"guard.cyan\"\n#include \"guard.cyan\"\nvar main_var = 1;\n",
        "<test>", {dir.string()}, "");
    fs::remove_all(dir);
    size_t count = 0;
    size_t pos = 0;
    while ((pos = result.expanded_source.find("var guarded", pos)) != std::string::npos) {
        ++count;
        pos += 4;
    }
    CHECK_EQ(count, size_t{1});
}

// --- X2b: function macros and #if constant expressions --------------------

TEST(preprocessor_function_macro_substitutes_arguments) {
    std::string out = pp_x2("#define ADD(a,b) ((a)+(b))\nvar x = ADD(2, 3);\n");
    CHECK(contains(out, "var x = ((2)+(3));"));
}

TEST(preprocessor_function_macro_expands_nested_arguments) {
    std::string out = pp_x2("#define N 4\n#define TWICE(x) ((x)+(x))\nvar y = TWICE(N);\n");
    CHECK(contains(out, "var y = ((4)+(4));"));
}

TEST(preprocessor_function_macro_skips_string_literals) {
    std::string out = pp_x2("#define F(x) x + 1\nvar s = \"F(9)\";\nvar y = F(9);\n");
    CHECK(contains(out, "\"F(9)\""));
    CHECK(contains(out, "var y = 9 + 1;"));
}

TEST(preprocessor_if_constant_zero_or_one) {
    std::string out = pp_x2("#if 1\nvar a = 1;\n#else\nvar b = 2;\n#endif\n");
    CHECK(contains(out, "var a = 1;"));
    CHECK(!contains(out, "var b = 2;"));
    out = pp_x2("#if 0\nvar a = 1;\n#else\nvar b = 2;\n#endif\n");
    CHECK(!contains(out, "var a = 1;"));
    CHECK(contains(out, "var b = 2;"));
}

TEST(preprocessor_if_understands_defined_and_macros) {
    std::string out = pp_x2(
        "#define N 5\n#if defined(N) && N > 3\nvar ok = 1;\n#else\nvar ok = 0;\n#endif\n");
    CHECK(contains(out, "var ok = 1;"));
    out = pp_x2("#if UNKNOWN\nvar bad = 1;\n#else\nvar good = 1;\n#endif\n");
    CHECK(!contains(out, "var bad"));
    CHECK(contains(out, "var good = 1;"));
}

TEST(preprocessor_if_arithmetic_and_comparison) {
    std::string out = pp_x2("#if 2 + 3 * 4 == 14\nvar a = 1;\n#endif\n");
    CHECK(contains(out, "var a = 1;"));
    out = pp_x2("#if (7 - 2) != 5\nvar b = 1;\n#else\nvar b = 2;\n#endif\n");
    CHECK(contains(out, "var b = 2;"));
}
