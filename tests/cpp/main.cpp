#include "test_framework.hpp"

#include <exception>
#include <iostream>

int main() {
    for (const auto& tc : utf::registry()) {
        const int before = utf::failures();
        ++utf::run_count();
        try {
            tc.fn();
        } catch (const std::exception& e) {
            ::utf::fail(tc.name, 0, std::string("exception: ") + e.what());
        } catch (...) {
            ::utf::fail(tc.name, 0, "unknown exception");
        }
        if (utf::failures() == before) std::cout << '.';
    }
    std::cout << "\nUnit tests: " << utf::run_count() << " cases, "
              << utf::checks() << " checks, " << utf::failures()
              << " failures\n";
    return utf::failures() == 0 ? 0 : 1;
}
