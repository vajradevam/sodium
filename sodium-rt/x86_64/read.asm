; x86-64 runtime: read a signed 64-bit integer from stdin
; int64_t _sodium_read_int(void)
global _sodium_read_int

_sodium_read_int:
    sub rsp, 32
    mov rax, 0              ; syscall: read
    mov rdi, 0              ; fd = stdin
    mov rsi, rsp
    mov rdx, 31
    syscall
    xor r8, r8              ; accumulator
    xor r9, r9              ; negative flag
    mov rcx, rsp
    cmp byte [rcx], '-'
    jne .Lloop
    inc rcx
    mov r9, 1
.Lloop:
    cmp byte [rcx], 10      ; newline
    je .Ldone
    cmp byte [rcx], 0       ; null
    je .Ldone
    movzx r10, byte [rcx]
    sub r10, 48             ; '0' → 0
    imul r8, r8, 10
    add r8, r10
    inc rcx
    jmp .Lloop
.Ldone:
    test r9, r9
    jz .Lpos
    neg r8
.Lpos:
    add rsp, 32
    mov rax, r8
    ret
