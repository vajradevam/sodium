; x86-64 runtime: exit syscall
; void _sodium_exit(int code)
global _sodium_exit

_sodium_exit:
    mov rax, 60     ; syscall: exit
    syscall
