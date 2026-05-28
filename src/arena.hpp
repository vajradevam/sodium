#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

class ArenaAllocator {
public:
    inline explicit ArenaAllocator(size_t bytes)
        : m_size(bytes)
        , m_buffer(static_cast<std::byte*>(malloc(m_size)))
        , m_offset(m_buffer)
    {
    }

    template<typename T>
    inline T* alloc() {
        uintptr_t current = reinterpret_cast<uintptr_t>(m_offset);
        uintptr_t aligned = (current + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned + sizeof(T) > reinterpret_cast<uintptr_t>(m_buffer) + m_size) {
            throw std::bad_alloc();
        }
        m_offset = reinterpret_cast<std::byte*>(aligned) + sizeof(T);
        return new (reinterpret_cast<void*>(aligned)) T();
    }

    inline ArenaAllocator(const ArenaAllocator& other) = delete;
    inline ArenaAllocator& operator=(const ArenaAllocator& other) = delete;

    inline ~ArenaAllocator() {
        free(m_buffer);
    }

private:
    size_t m_size;
    std::byte* m_buffer;
    std::byte* m_offset;
};