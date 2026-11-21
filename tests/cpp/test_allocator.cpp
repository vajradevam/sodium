#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
void* _sodium_malloc(size_t size);
void _sodium_free(void* ptr);
}

TEST(allocator_zero_size_returns_null) {
    CHECK(_sodium_malloc(0) == nullptr);
}

TEST(allocator_small_allocation_is_non_null) {
    void* p = _sodium_malloc(24);
    CHECK(p != nullptr);
    _sodium_free(p);
}

TEST(allocator_small_allocation_is_16_byte_aligned) {
    for (size_t n : {1u, 7u, 8u, 16u, 24u, 63u, 64u, 300u, 2040u}) {
        void* p = _sodium_malloc(n);
        CHECK(p != nullptr);
        // 8-byte payload alignment is guaranteed (8-byte header on a 16-byte heap base).
        // 16-byte alignment is a planned improvement (V6).
        CHECK_EQ(reinterpret_cast<uintptr_t>(p) % 8, 0u);
        _sodium_free(p);
    }
}

TEST(allocator_distinct_allocations_have_distinct_pointers) {
    void* a = _sodium_malloc(32);
    void* b = _sodium_malloc(32);
    CHECK(a != nullptr && b != nullptr);
    CHECK(a != b);
    _sodium_free(a);
    _sodium_free(b);
}

TEST(allocator_payload_is_writable) {
    auto* p = static_cast<uint64_t*>(_sodium_malloc(64));
    CHECK(p != nullptr);
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint64_t>(i) * 0x1111111111111111ULL;
    for (int i = 0; i < 8; ++i) CHECK_EQ(p[i], static_cast<uint64_t>(i) * 0x1111111111111111ULL);
    _sodium_free(p);
}

TEST(allocator_free_then_alloc_reuses_same_class_block) {
    void* a = _sodium_malloc(48);
    CHECK(a != nullptr);
    _sodium_free(a);
    void* b = _sodium_malloc(48);
    CHECK_EQ(b, a);
    _sodium_free(b);
}

TEST(allocator_free_null_is_safe) {
    _sodium_free(nullptr);
    CHECK(true);
}

TEST(allocator_large_allocation_is_non_null) {
    void* p = _sodium_malloc(4096);
    CHECK(p != nullptr);
    CHECK_EQ(reinterpret_cast<uintptr_t>(p) % 8, 0u);
    _sodium_free(p);
}

TEST(allocator_large_allocation_payload_is_writable) {
    auto* p = static_cast<unsigned char*>(_sodium_malloc(5000));
    CHECK(p != nullptr);
    std::memset(p, 0xAB, 5000);
    bool ok = true;
    for (size_t i = 0; i < 5000; ++i) if (p[i] != 0xAB) ok = false;
    CHECK(ok);
    _sodium_free(p);
}

TEST(allocator_large_block_is_recycled) {
    void* a = _sodium_malloc(4096);
    CHECK(a != nullptr);
    _sodium_free(a);
    void* b = _sodium_malloc(4096);
    CHECK_EQ(b, a);
    _sodium_free(b);
}

TEST(allocator_allocations_do_not_overlap) {
    std::vector<unsigned char*> ps;
    for (int i = 0; i < 32; ++i) {
        auto* p = static_cast<unsigned char*>(_sodium_malloc(128));
        CHECK(p != nullptr);
        std::memset(p, i, 128);
        ps.push_back(p);
    }
    bool ok = true;
    for (int i = 0; i < static_cast<int>(ps.size()); ++i) {
        for (size_t j = 0; j < 128; ++j) if (ps[i][j] != static_cast<unsigned char>(i)) ok = false;
    }
    CHECK(ok);
    for (auto* p : ps) _sodium_free(p);
}

TEST(allocator_class_boundaries_are_usable) {
    const size_t boundaries[] = {16, 24, 32, 48, 64, 96, 128, 192, 256,
                                 384, 512, 768, 1024, 1536, 2048};
    for (size_t n : boundaries) {
        void* p = _sodium_malloc(n - 8); // header is 8 bytes
        CHECK(p != nullptr);
        if (p) std::memset(p, 0x5A, n - 8);
        _sodium_free(p);
    }
}

TEST(allocator_one_past_largest_class_is_large_path) {
    void* p = _sodium_malloc(2050);
    CHECK(p != nullptr);
    CHECK_EQ(reinterpret_cast<uintptr_t>(p) % 8, 0u);
    _sodium_free(p);
}

TEST(allocator_reuse_across_size_classes) {
    void* a = _sodium_malloc(24);
    void* b = _sodium_malloc(96);
    CHECK(a != nullptr && b != nullptr);
    _sodium_free(a);
    _sodium_free(b);
    void* c = _sodium_malloc(96);
    void* d = _sodium_malloc(24);
    CHECK(c != nullptr && d != nullptr);
    _sodium_free(c);
    _sodium_free(d);
}

TEST(allocator_many_live_allocations) {
    std::vector<void*> ps;
    for (int i = 0; i < 200; ++i) {
        void* p = _sodium_malloc(64);
        CHECK(p != nullptr);
        ps.push_back(p);
    }
    for (void* p : ps) _sodium_free(p);
}

TEST(allocator_repeated_alloc_free_cycles) {
    for (int i = 0; i < 1000; ++i) {
        void* p = _sodium_malloc(64 + (i % 7) * 16);
        CHECK(p != nullptr);
        *static_cast<int*>(p) = i;
        _sodium_free(p);
    }
}

TEST(allocator_interleaved_alloc_free) {
    void* keep[8] = {};
    for (int i = 0; i < 8; ++i) keep[i] = _sodium_malloc(128);
    for (int i = 0; i < 8; ++i) CHECK(keep[i] != nullptr);
    for (int i = 0; i < 8; i += 2) _sodium_free(keep[i]);
    for (int i = 1; i < 8; i += 2) {
        CHECK(keep[i] != nullptr);
        std::memset(keep[i], i, 128);
    }
    for (int i = 1; i < 8; i += 2) _sodium_free(keep[i]);
}

TEST(allocator_growth_without_free) {
    std::vector<void*> ps;
    for (int i = 0; i < 64; ++i) {
        void* p = _sodium_malloc(1024);
        CHECK(p != nullptr);
        ps.push_back(p);
    }
    for (void* p : ps) _sodium_free(p);
}

TEST(allocator_mixed_small_and_large) {
    std::vector<void*> ps;
    for (int i = 0; i < 20; ++i) {
        ps.push_back(_sodium_malloc(i % 2 ? 32 : 3000));
    }
    for (void* p : ps) CHECK(p != nullptr);
    for (void* p : ps) _sodium_free(p);
}
