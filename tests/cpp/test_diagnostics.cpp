#include "test_framework.hpp"
#include "diagnostic.hpp"

TEST(diagnostics_error_renders_d004_format) {
    DiagnosticEngine engine;
    engine.error({2, 7, "a.cyan"}, "expected ';'");
    CHECK_EQ(engine.size(), size_t(1));
    CHECK(engine.has_errors());
    CHECK_EQ(engine.render_all(), std::string("a.cyan:2:7: error: expected ';'\n"));
}

TEST(diagnostics_location_without_file) {
    DiagnosticEngine engine;
    engine.error({0, 0, ""}, "no main");
    CHECK_EQ(engine.render_all(), std::string("0:0: error: no main\n"));
}

TEST(diagnostics_severity_names) {
    DiagnosticEngine engine;
    engine.note({1, 1, "f"}, "previous declaration here");
    engine.warning({2, 1, "f"}, "unused variable");
    engine.fatal({3, 1, "f"}, "broken");
    const auto rendered = engine.render_all();
    CHECK(rendered.find("f:1:1: note: previous declaration here") != std::string::npos);
    CHECK(rendered.find("f:2:1: warning: unused variable") != std::string::npos);
    CHECK(rendered.find("f:3:1: error: broken") != std::string::npos);
}

TEST(diagnostics_warning_is_not_an_error) {
    DiagnosticEngine engine;
    engine.warning({1, 1, "f"}, "careful");
    CHECK(!engine.has_errors());
    CHECK_EQ(engine.error_count(), size_t(0));
}

TEST(diagnostics_error_counting) {
    DiagnosticEngine engine;
    engine.error({1, 1, "f"}, "one");
    engine.fatal({2, 1, "f"}, "two");
    engine.warning({3, 1, "f"}, "three");
    CHECK_EQ(engine.error_count(), size_t(2));
}

TEST(diagnostics_notes_and_fixits) {
    DiagnosticEngine engine;
    engine.error({4, 9, "f"}, "undeclared identifier");
    CHECK(engine.add_note("declared here"));
    CHECK(engine.add_fixit({{4, 9, "f"}, "count"}));
    const auto rendered = engine.render_all();
    CHECK(rendered.find("f:4:9: note: declared here") != std::string::npos);
    CHECK(rendered.find("f:4:9: fix-it: count") != std::string::npos);
}

TEST(diagnostics_add_note_without_diagnostic_fails) {
    DiagnosticEngine engine;
    CHECK(!engine.add_note("orphan"));
    CHECK(!engine.add_fixit({{1, 1, "f"}, "x"}));
    CHECK_EQ(engine.size(), size_t(0));
}

TEST(diagnostics_clear) {
    DiagnosticEngine engine;
    engine.error({1, 1, "f"}, "bad");
    CHECK(engine.has_errors());
    engine.clear();
    CHECK(engine.empty());
    CHECK(!engine.has_errors());
}

TEST(diagnostics_instances_are_independent) {
    DiagnosticEngine a;
    DiagnosticEngine b;
    a.error({1, 1, "a.cyan"}, "only in a");
    CHECK_EQ(a.size(), size_t(1));
    CHECK(b.empty());
    CHECK(b.render_all().empty());
}

TEST(diagnostics_add_note_at_uses_given_location) {
    DiagnosticEngine engine;
    engine.error({5, 2, "f"}, "redeclared");
    CHECK(engine.add_note_at({2, 9, "f"}, "previous declaration here"));
    const auto rendered = engine.render_all();
    CHECK(rendered.find("f:5:2: error: redeclared") != std::string::npos);
    CHECK(rendered.find("f:2:9: note: previous declaration here") != std::string::npos);
}

TEST(diagnostics_colored_render_uses_ansi_severity) {
    DiagnosticEngine engine;
    engine.set_color(true);
    engine.error({1, 1, "f"}, "bad");
    engine.warning({2, 1, "f"}, "careful");
    engine.note({3, 1, "f"}, "context");
    const std::string out = engine.render_all();
    CHECK(out.find("\033[1mf:1:1") != std::string::npos);
    CHECK(out.find("\033[31merror\033[0m: bad") != std::string::npos);
    CHECK(out.find("\033[35mwarning\033[0m: careful") != std::string::npos);
    CHECK(out.find("\033[36mnote\033[0m: context") != std::string::npos);
    CHECK(out.find("\033[0m") != std::string::npos);
}

TEST(diagnostics_plain_render_has_no_ansi) {
    DiagnosticEngine engine;
    engine.error({1, 1, "f"}, "bad");
    CHECK(engine.render_all().find("\033[") == std::string::npos);
}

TEST(diagnostics_warning_default_is_warning) {
    DiagnosticEngine engine;
    engine.warning({1, 1, "f"}, "careful");
    CHECK_EQ(engine.size(), size_t(1));
    CHECK(!engine.has_errors());
    CHECK(engine.render_all().find("f:1:1: warning: careful") != std::string::npos);
}

TEST(diagnostics_w_flag_suppresses_warnings) {
    DiagnosticEngine engine;
    WarnConfig cfg;
    cfg.enabled = false;
    engine.set_warn_config(cfg);
    engine.warning({1, 1, "f"}, "careful");
    CHECK(engine.empty());
    CHECK_EQ(engine.render_all(), std::string());
}

TEST(diagnostics_werror_promotes_warning_to_error) {
    DiagnosticEngine engine;
    WarnConfig cfg;
    cfg.werror = true;
    engine.set_warn_config(cfg);
    engine.warning({2, 4, "f"}, "unused variable 'x'");
    CHECK_EQ(engine.size(), size_t(1));
    CHECK(engine.has_errors());
    CHECK_EQ(engine.error_count(), size_t(1));
    CHECK(engine.render_all().find("f:2:4: error: unused variable 'x'") != std::string::npos);
}

TEST(diagnostics_w_flag_does_not_suppress_errors_under_werror) {
    DiagnosticEngine engine;
    WarnConfig cfg;
    cfg.enabled = false;
    cfg.werror = true;
    engine.set_warn_config(cfg);
    engine.warning({1, 1, "f"}, "careful");
    engine.error({2, 1, "f"}, "real error");
    CHECK_EQ(engine.size(), size_t(1));
    CHECK(engine.has_errors());
}

TEST(diagnostics_preserve_insertion_order) {
    DiagnosticEngine engine;
    engine.error({1, 1, "f"}, "first");
    engine.warning({2, 1, "f"}, "second");
    engine.note({3, 1, "f"}, "third");
    const auto& diags = engine.diagnostics();
    CHECK_EQ(diags.size(), size_t(3));
    CHECK_EQ(diags[0].message, std::string("first"));
    CHECK_EQ(diags[1].message, std::string("second"));
    CHECK_EQ(diags[2].message, std::string("third"));
}
