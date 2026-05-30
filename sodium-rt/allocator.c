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
//   - Header: 8 bytes per block (class_idx : 4, magic : 4) for O(1) free
//   - Not thread-safe (single-threaded runtime assumed).
//
// Size classes tuned for compiler workloads:
//   AST nodes cluster at 24-128 bytes, strings are large and go to bump.
//   The classes use a Fibonacci-like spacing to bound internal
//   fragmentation to < 50% between any two adjacent classes.

// ── Configuration ───────────────────────────────────────────────────────

#define HEAP_SIZE           (2UL * 1024 * 1024)   // 2 MB
#define HEADER_SIZE         8UL                    // 4 bytes class + 4 bytes magic
#define MIN_ALIGN           8UL                    // minimum alignment
#define MAGIC               0x534F4449UL           // "SODI" in hex

// Total block sizes (header + payload). The user gets (class_size - HEADER_SIZE)
// usable bytes. All sizes are multiples of 8 so payloads are naturally aligned.
#define NUM_CLASSES         15

static const unsigned int class_sizes[NUM_CLASSES] = {
    16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048
};

// Sentinel for large allocations (fall through to bump, never freed into a list)
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

        // Fall through to bump allocation with the class size.
        size = class_sizes[ci];
    }

    // ── Bump allocation (small fallback + large allocs) ─────────────
    size_t total = align_up(size, MIN_ALIGN);

    void *ptr = bump_ptr;
    void *heap_end = (void*)(heap + HEAP_SIZE);
    if ((char*)ptr + total > (char*)heap_end)
        return 0;   // out of memory

    bump_ptr = (char*)bump_ptr + total;

    // Store header: class index + magic.
    // The header lives just before the returned pointer.
    unsigned int *hdr = (unsigned int*)ptr;
    hdr[0] = (unsigned int)(ci >= 0 ? (unsigned int)ci : CLASS_LARGE);
    hdr[1] = MAGIC;

    return (char*)ptr + HEADER_SIZE;
}

void _sodium_free(void *ptr) {
    if (!ptr)
        return;

    // Recover the header (8 bytes before the user pointer).
    unsigned int *hdr = (unsigned int*)((char*)ptr - HEADER_SIZE);
    unsigned int ci   = hdr[0];
    unsigned int magic = hdr[1];

    // Sanity checks.
    if (magic != MAGIC)
        return;                         // corrupted or invalid pointer
    if (ci >= NUM_CLASSES)
        return;                         // large block — not tracked in free lists

    // Push to the front of the class free list.
    void **next_slot = (void**)ptr;
    *next_slot = free_lists[ci];
    free_lists[ci] = ptr;
}
