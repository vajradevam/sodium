#include "test_framework.hpp"
#include "backend/peephole.hpp"

#include <string>

namespace {

std::string run(const std::string& text, const std::string& target, PeepholeStats& stats) {
    return peephole_optimize(text, target, &stats);
}

size_t count_of(const std::string& haystack, const std::string& needle) {
    size_t n = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

} // namespace

TEST(peephole_x86_removes_self_move) {
    PeepholeStats st;
    std::string out = run("    mov rbx, rcx\n    mov rax, rax\n    ret\n", "x86_64", st);
    CHECK_EQ(st.self_moves_removed, size_t{1});
    CHECK_EQ(count_of(out, "mov rax, rax"), size_t{0});
    CHECK_EQ(count_of(out, "mov rbx, rcx"), size_t{1});
    CHECK_EQ(count_of(out, "ret"), size_t{1});
}

TEST(peephole_x86_keeps_memory_and_subregister_moves) {
    PeepholeStats st;
    std::string out = run("    mov rax, [rbp - 8]\n    mov eax, eax\n", "x86_64", st);
    // eax/eax is a genuine self-move; the memory operand must stay.
    CHECK_EQ(st.self_moves_removed, size_t{1});
    CHECK_EQ(count_of(out, "mov rax, [rbp - 8]"), size_t{1});
    CHECK_EQ(count_of(out, "mov eax, eax"), size_t{0});
}

TEST(peephole_x86_drops_adjacent_dead_move) {
    PeepholeStats st;
    std::string out = run("    mov rax, 5\n    mov rax, rbx\n    ret\n", "x86_64", st);
    CHECK_EQ(st.dead_moves_removed, size_t{1});
    CHECK_EQ(count_of(out, "mov rax, 5"), size_t{0});
    CHECK_EQ(count_of(out, "mov rax, rbx"), size_t{1});
}

TEST(peephole_does_not_cross_a_label) {
    PeepholeStats st;
    std::string out = run("    mov rax, 5\n.Lnext:\n    mov rax, rbx\n", "x86_64", st);
    CHECK_EQ(st.dead_moves_removed, size_t{0});
    CHECK_EQ(count_of(out, "mov rax, 5"), size_t{1});
}

TEST(peephole_does_not_collapse_across_other_instructions) {
    PeepholeStats st;
    std::string out = run("    mov rax, 5\n    cmp rax, rbx\n    mov rax, 3\n", "x86_64", st);
    CHECK_EQ(st.dead_moves_removed, size_t{0});
    CHECK_EQ(count_of(out, "mov rax, 5"), size_t{1});
}

TEST(peephole_riscv_removes_addi_noop) {
    PeepholeStats st;
    std::string out = run(".option norelax\n    addi t0, t0, 0\n    ret\n", "riscv64", st);
    CHECK_EQ(st.self_moves_removed, size_t{1});
    CHECK_EQ(count_of(out, "addi t0, t0, 0"), size_t{0});
    CHECK_EQ(count_of(out, ".option norelax"), size_t{1});
}

TEST(peephole_riscv_removes_self_mv) {
    PeepholeStats st;
    std::string out = run("    mv t0, t0\n    ret\n", "riscv64", st);
    CHECK_EQ(st.self_moves_removed, size_t{1});
    CHECK_EQ(count_of(out, "mv t0, t0"), size_t{0});
}

TEST(peephole_riscv_drops_adjacent_dead_mv) {
    PeepholeStats st;
    std::string out = run("    mv t0, t1\n    mv t0, t2\n    ret\n", "riscv64", st);
    CHECK_EQ(st.dead_moves_removed, size_t{1});
    CHECK_EQ(count_of(out, "mv t0, t1"), size_t{0});
    CHECK_EQ(count_of(out, "mv t0, t2"), size_t{1});
}

TEST(peephole_unknown_target_is_a_no_op) {
    PeepholeStats st;
    std::string in = "    mov rax, rax\n    addi t0, t0, 0\n";
    std::string out = run(in, "unknown", st);
    CHECK_EQ(out, in);
    CHECK_EQ(st.self_moves_removed, size_t{0});
}
