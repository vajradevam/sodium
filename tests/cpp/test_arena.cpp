#include "test_framework.hpp"
#include "arena.hpp"

#include <cstdint>
#include <string>

TEST(arena_allocates_aligned_int) {
    ArenaAllocator arena(64);
    int* p = arena.alloc<int>();
    CHECK(p != nullptr);
    CHECK_EQ(reinterpret_cast<uintptr_t>(p) % alignof(int), 0u);
    *p = 42;
    CHECK_EQ(*p, 42);
}

TEST(arena_multiple_allocations_do_not_overlap) {
    ArenaAllocator arena(64);
    int* a = arena.alloc<int>();
    int* b = arena.alloc<int>();
    CHECK(a != b);
    *a = 1;
    *b = 2;
    CHECK_EQ(*a, 1);
    CHECK_EQ(*b, 2);
}

TEST(arena_grows_to_new_block) {
    ArenaAllocator arena(8);
    int* a = arena.alloc<int>();
    int* b = arena.alloc<int>();
    int* c = arena.alloc<int>();
    CHECK(a != nullptr && b != nullptr && c != nullptr);
    *a = 10;
    *b = 20;
    *c = 30;
    CHECK_EQ(*a, 10);
    CHECK_EQ(*b, 20);
    CHECK_EQ(*c, 30);
}

TEST(arena_overaligned_type_is_aligned) {
    struct alignas(16) Wide { char data[16]; };
    ArenaAllocator arena(32);
    Wide* w = arena.alloc<Wide>();
    CHECK_EQ(reinterpret_cast<uintptr_t>(w) % 16, 0u);
}

TEST(arena_allocates_objects_with_constructor) {
    ArenaAllocator arena(64);
    std::string* s = arena.alloc<std::string>();
    CHECK(s != nullptr);
    CHECK(s->empty());
    *s = "hello";
    CHECK_EQ(*s, std::string("hello"));
}
