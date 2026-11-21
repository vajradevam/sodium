#include "test_framework.hpp"
#include "preprocessor.hpp"

#include <string>

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
