#pragma once

// Minimal, no-network unit-test harness. Each TEST() registers one function;
// CHECK/CHECK_EQ record failures without aborting the rest of the case.

#include <iostream>
#include <string>
#include <vector>

namespace utf {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline int& run_count() { static int n = 0; return n; }

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& expr) {
    ++failures();
    std::cerr << "  FAIL " << file << ':' << line << ": " << expr << '\n';
}

} // namespace utf

#define TEST(name)                                                             \
    static void name();                                                        \
    static ::utf::Registrar reg_##name(#name, &name);                          \
    static void name()

#define CHECK(expr)                                                            \
    do {                                                                       \
        ++::utf::checks();                                                     \
        if (!(expr)) ::utf::fail(__FILE__, __LINE__, #expr);                   \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++::utf::checks();                                                     \
        const auto& a_val = (a);                                               \
        const auto& b_val = (b);                                               \
        if (!(a_val == b_val)) {                                               \
            std::cerr << "  FAIL " << __FILE__ << ':' << __LINE__ << ": "      \
                      << #a << " == " << #b << " (" << a_val << " vs "         \
                      << b_val << ")\n";                                       \
            ++::utf::failures();                                               \
        }                                                                      \
    } while (0)
