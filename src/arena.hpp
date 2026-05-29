#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

class ArenaAllocator {
public:
    inline explicit ArenaAllocator(size_t initial_size)
        : m_head(alloc_block(initial_size))
        , m_current(m_head)
    {
    }

    template<typename T>
    inline T* alloc() {
        uintptr_t current = reinterpret_cast<uintptr_t>(m_current->offset);
        uintptr_t aligned = (current + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned + sizeof(T) > reinterpret_cast<uintptr_t>(m_current->buffer) + m_current->size) {
            // Current block is full — allocate a new one.
            // Grow geometrically to amortize allocation cost.
            size_t new_size = m_current->size * 2;
            Block* new_block = alloc_block(new_size);
            m_current->next = new_block;
            m_current = new_block;

            // Retry in the new block.
            current = reinterpret_cast<uintptr_t>(m_current->offset);
            aligned = (current + alignof(T) - 1) & ~(alignof(T) - 1);
            // new block is empty so this must succeed, but guard anyway
            if (aligned + sizeof(T) > reinterpret_cast<uintptr_t>(m_current->buffer) + m_current->size) {
                throw std::bad_alloc();
            }
        }
        m_current->offset = reinterpret_cast<std::byte*>(aligned) + sizeof(T);
        return new (reinterpret_cast<void*>(aligned)) T();
    }

    inline ArenaAllocator(const ArenaAllocator& other) = delete;
    inline ArenaAllocator& operator=(const ArenaAllocator& other) = delete;

    inline ~ArenaAllocator() {
        Block* block = m_head;
        while (block) {
            Block* next = block->next;
            free(block);
            block = next;
        }
    }

private:
    struct Block {
        std::byte* buffer;
        std::byte* offset;
        size_t size;
        Block* next;
    };

    static inline Block* alloc_block(size_t size) {
        void* mem = malloc(sizeof(Block) + size);
        if (!mem) {
            throw std::bad_alloc();
        }
        Block* block = static_cast<Block*>(mem);
        block->buffer = static_cast<std::byte*>(mem) + sizeof(Block);
        block->offset = block->buffer;
        block->size = size;
        block->next = nullptr;
        return block;
    }

    Block* m_head;
    Block* m_current;
};
