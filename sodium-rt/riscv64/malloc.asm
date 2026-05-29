# RISC-V 64-bit _sodium_malloc / _sodium_free
# Simple bump allocator with a pre-allocated heap in BSS
# _sodium_malloc(size) → pointer in a0
# _sodium_free(ptr) — no-op

.section .data
.align 3
_sodium_heap_ptr:
    .dword _sodium_heap_start

.section .bss
.align 4
.globl _sodium_heap_start
_sodium_heap_start:
    .zero 1048576     # 1MB heap

.section .text
.globl _sodium_malloc
_sodium_malloc:
    # a0 = size requested
    # Load current bump pointer
    la a1, _sodium_heap_ptr
    ld a2, 0(a1)       # a2 = current bump
    
    # Align size to 16 bytes
    addi a0, a0, 15
    li a3, -16
    and a0, a0, a3
    
    # Save old bump for return
    mv a3, a2           # a3 = old bump = return value
    
    # Advance bump
    add a2, a2, a0
    
    # Store new bump pointer
    sd a2, 0(a1)
    
    # Return old bump
    mv a0, a3
    ret

.globl _sodium_free
_sodium_free:
    ret
