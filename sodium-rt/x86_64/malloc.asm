; x86-64 runtime: bump allocator via brk
; void* _sodium_malloc(size_t size)
; void  _sodium_free(void* ptr)  ; no-op for bump allocator
global _sodium_malloc
global _sodium_free

_sodium_malloc:
    push rdi
    mov rax, 12         ; syscall: brk
    xor rdi, rdi        ; arg: 0 = get current break
    syscall
    pop rdi
    push rax            ; save old break = allocated address
    add rdi, rax        ; new break = old break + size
    mov rax, 12         ; syscall: brk
    syscall
    pop rax             ; return old break
    ret

_sodium_free:
    ; rdi = ptr (ignored)
    ret
