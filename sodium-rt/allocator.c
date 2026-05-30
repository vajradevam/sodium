// sodium-rt/allocator.c
// Segregated Free List allocator for the Cyan language runtime.

#include <stddef.h>
//
// Freestanding C — no libc dependencies. Compiles for both x86-64 and RISC-V.
//
// Design:
//   - 2 MB BSS heap, no mmap/brk needed
//   - 15 size classes covering 16..2048 bytes (total block size, header included)
//   - Singly-linked free lists per class (fast path is 3 loads + 1 store both ways)
//   - Bump-pointer fallback when a class list is empty (no splitting needed)
//   - Lazy coalescing: free just pushes back to the same class list.
//     The bump backend never reclaims, but same-size reuse means freed blocks
//     get recycled immediately — fragmentation is bounded.
//   - Header: 8 bytes per block (class_idx : 4, magic_or_size : 4)
//     For small blocks: magic_or_size = MAGIC.
//     For large blocks: magic_or_size = total block size (fits in 32 bits on 2 MB heap).
//   - Large-block free list: first-fit search, nodes store {next_ptr, block_size}
//     in the first 16 bytes of the user payload.
//   - Not thread-safe (single-threaded runtime assumed).
//
// Size classes tuned for compiler workloads:
//   AST nodes cluster at 24-128 bytes, strings are large and go to bump.
//   The classes use a Fibonacci-like spacing to bound internal
//   fragmentation to < 50% between any two adjacent classes.

// ── Configuration ───────────────────────────────────────────────────────

#define HEAP_SIZE           (2UL * 1024 * 1024)   // 2 MB
#define HEADER_SIZE         8UL                    // 4 bytes class + 4 bytes magic/size
#define MIN_ALIGN           8UL                    // minimum alignment
#define MAGIC               0x534F4449UL           // "SODI" in hex

// Total block sizes (header + payload). The user gets (class_size - HEADER_SIZE)
// usable bytes. All sizes are multiples of 8 so payloads are naturally aligned.
#define NUM_CLASSES         15

static const unsigned int class_sizes[NUM_CLASSES] = {
    16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048
};

// Sentinel for large allocations (fall through to bump)
#define CLASS_LARGE         ((unsigned int)-1)

// ── Heap memory ─────────────────────────────────────────────────────────

// BSS is zero-initialized by the loader. We get 2 MB automatically.
// The aligned(16) ensures all returned pointers are at least 16-byte aligned.
static char heap[HEAP_SIZE] __attribute__((section(".bss"), aligned(16)));

// Bump pointer starts at the beginning of the heap and only advances.
// It's the fallback when free lists are empty.
static void *bump_ptr __attribute__((used)) = (void*)&heap;

// One singly-linked free list per size class.
// Each entry points to the *payload* of the first free block in that class.
// When a block is free, its first 8 payload bytes store the next pointer.
static void *free_lists[NUM_CLASSES] __attribute__((used));

// Singly-linked free list for large blocks (> 2048 bytes).
// Each node is stored in the user payload area:
//   [next_ptr (8 bytes)] [block_size (8 bytes)]
// The block_size is the total allocation size (header + user payload).
static void *large_free_list __attribute__((used)) = 0;

// Command-line arguments (populated by _start before calling main).
long __sodium_argc __attribute__((used)) = 0;
void *__sodium_argv __attribute__((used)) = 0;

// ── Helpers ─────────────────────────────────────────────────────────────

// Find the smallest class whose total size fits (header + request).
// Returns -1 if the request is too large for any class.
static int size_to_class(size_t n) {
    size_t needed = n + HEADER_SIZE;
    if (needed > class_sizes[NUM_CLASSES - 1])
        return -1;
    for (int i = 0; i < NUM_CLASSES; i++) {
        if (needed <= class_sizes[i])
            return i;
    }
    return -1;
}

// Round value up to the given alignment (must be power of 2).
static inline size_t align_up(size_t val, size_t align) {
    return (val + align - 1) & ~(align - 1);
}

// ── Public API ──────────────────────────────────────────────────────────

void *_sodium_malloc(size_t size) {
    if (size == 0)
        return 0;

    // ── Small allocation: try the class free list first ─────────────
    int ci = size_to_class(size);
    if (ci >= 0) {
        void *block = free_lists[ci];
        if (block) {
            // Pop from the front of the list.
            // While free, the first 8 bytes of the payload store the next pointer.
            void **next_slot = (void**)block;
            free_lists[ci] = *next_slot;
            return block;
        }

        // Fall through to bump allocation with the class size (not the requested size).
        size = class_sizes[ci];
    }

    // ── Large allocation: try the large free list first (first-fit) ──
    size_t total;
    if (ci < 0) {
        total = align_up(size + HEADER_SIZE, MIN_ALIGN);
        struct LargeNode {
            struct LargeNode *next;
            size_t block_size;    // total block size (header + payload)
        };
        struct LargeNode **prev = (struct LargeNode**)&large_free_list;
        struct LargeNode *cur = large_free_list;
        while (cur) {
            if (cur->block_size >= total) {
                // Found a fit — remove from free list
                *prev = cur->next;
                // Set up header: cur points to the user payload (after header)
                unsigned int *hdr = (unsigned int*)((char*)cur - HEADER_SIZE);
                hdr[0] = CLASS_LARGE;
                hdr[1] = (unsigned int)cur->block_size;
                return (char*)cur;
            }
            prev = &cur->next;
            cur = cur->next;
        }
        // Fall through to bump — total is already computed
    } else {
        // Small allocation bump: size is already the total block size (class_sizes[ci])
        total = align_up(size, MIN_ALIGN);
    }

    // ── Bump allocation (small fallback + large allocs) ─────────────
    void *ptr = bump_ptr;
    void *heap_end = (void*)(heap + HEAP_SIZE);
    if ((char*)ptr + total > (char*)heap_end)
        return 0;   // out of memory

    bump_ptr = (char*)bump_ptr + total;

    // Store header: class index + magic/size.
    // The header lives just before the returned pointer.
    unsigned int *hdr = (unsigned int*)ptr;
    hdr[0] = (unsigned int)(ci >= 0 ? (unsigned int)ci : CLASS_LARGE);
    hdr[1] = (unsigned int)(ci >= 0 ? MAGIC : total);

    return (char*)ptr + HEADER_SIZE;
}

void _sodium_free(void *ptr) {
    if (!ptr)
        return;

    // Recover the header (8 bytes before the user pointer).
    unsigned int *hdr = (unsigned int*)((char*)ptr - HEADER_SIZE);
    unsigned int ci   = hdr[0];
    unsigned int val2 = hdr[1];   // MAGIC for small blocks, total size for large

    if (ci == CLASS_LARGE) {
        // Large block — val2 is the total block size.
        size_t block_size = (size_t)val2;

        // Store a free-list node in the user payload:
        //   [next_ptr (8 bytes)] [block_size (8 bytes)]
        struct LargeNode {
            struct LargeNode *next;
            size_t bsize;
        };
        struct LargeNode *node = (struct LargeNode*)ptr;
        node->next = (struct LargeNode*)large_free_list;
        node->bsize = block_size;
        large_free_list = node;
        return;
    }

    // Small class: sanity check magic and class index
    if (val2 != MAGIC)
        return;                         // corrupted or invalid pointer
    if (ci >= NUM_CLASSES)
        return;                         // shouldn't happen

    // Push to the front of the class free list.
    void **next_slot = (void**)ptr;
    *next_slot = free_lists[ci];
    free_lists[ci] = ptr;
}
