#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t initial_size);
    ~ArenaAllocator();

    ArenaAllocator(const ArenaAllocator& other) = delete;
    ArenaAllocator& operator=(const ArenaAllocator& other) = delete;

    template<typename T>
    inline T* alloc() {
        uintptr_t current = reinterpret_cast<uintptr_t>(m_current->offset);
        uintptr_t aligned = (current + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned + sizeof(T) > reinterpret_cast<uintptr_t>(m_current->buffer) + m_current->size) {
            size_t new_size = m_current->size * 2;
            Block* new_block = alloc_block(new_size);
            m_current->next = new_block;
            m_current = new_block;

            current = reinterpret_cast<uintptr_t>(m_current->offset);
            aligned = (current + alignof(T) - 1) & ~(alignof(T) - 1);
            if (aligned + sizeof(T) > reinterpret_cast<uintptr_t>(m_current->buffer) + m_current->size) {
                throw std::bad_alloc();
            }
        }
        m_current->offset = reinterpret_cast<std::byte*>(aligned) + sizeof(T);
        return new (reinterpret_cast<void*>(aligned)) T();
    }

private:
    struct Block {
        std::byte* buffer;
        std::byte* offset;
        size_t size;
        Block* next;
    };

    static Block* alloc_block(size_t size);

    Block* m_head;
    Block* m_current;
};
