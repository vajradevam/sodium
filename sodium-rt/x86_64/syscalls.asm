; x86-64 raw syscall stubs (L4a).
; Syscall number in rdi, up to four arguments in rsi/rdx/rcx/r8.
; `syscall` clobbers rcx and r11, both caller-saved.

global _sodium_syscall4
_sodium_syscall4:
    mov rax, rdi            ; syscall number
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8             ; fourth argument lives in r10
    syscall
    ret

global _sodium_syscall3
_sodium_syscall3:
    xor r8, r8
    jmp _sodium_syscall4
