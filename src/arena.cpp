#include "arena.hpp"

ArenaAllocator::ArenaAllocator(size_t initial_size)
    : m_head(alloc_block(initial_size))
    , m_current(m_head)
{
}

ArenaAllocator::~ArenaAllocator() {
    Block* block = m_head;
    while (block) {
        Block* next = block->next;
        free(block);
        block = next;
    }
}

ArenaAllocator::Block* ArenaAllocator::alloc_block(size_t size) {
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
