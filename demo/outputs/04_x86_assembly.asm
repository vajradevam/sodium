extern _sodium_exit
extern _sodium_print_int
extern _sodium_read_int
extern _sodium_malloc
extern _sodium_free
global _start
_start:
_start_body:
    push rbp
    mov rbp, rsp
    sub rsp, 48
_start_body.entry:
    push rbx
    push r12
    mov rax, 10
    lea rbx, [rbp - 8]
    mov [rbx], rax
    mov rax, 3
    lea rbx, [rbp - 16]
    mov [rbx], rax
    lea rax, [rbp - 8]
    mov rbx, [rax]
    mov rdi, rbx
    call factorial
    lea rbx, [rbp - 24]
    mov [rbx], rax
    lea rax, [rbp - 16]
    mov rbx, [rax]
    mov rdi, rbx
    call factorial
    lea rbx, [rbp - 32]
    mov [rbx], rax
    lea rax, [rbp - 8]
    mov rbx, [rax]
    lea rax, [rbp - 16]
    mov rcx, [rax]
    mov r12, rbx
    sub r12, rcx
    mov rdi, r12
    call factorial
    lea rbx, [rbp - 40]
    mov [rbx], rax
    lea rax, [rbp - 24]
    mov rbx, [rax]
    lea rax, [rbp - 32]
    mov rcx, [rax]
    lea rax, [rbp - 40]
    mov rdx, [rax]
    mov rax, rcx
    imul rax, rdx
    mov r11, rax
    mov rax, rbx
    cqo
    idiv r11
    mov rcx, rax
    lea rax, [rbp - 48]
    mov [rax], rcx
    lea rax, [rbp - 48]
    mov rbx, [rax]
    mov rdi, rbx
    call _sodium_print_int
    lea rax, [rbp - 48]
    mov rbx, [rax]
    mov rdi, rbx
    call _sodium_exit
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
extern factorial
factorial:
    push rbp
    mov rbp, rsp
    sub rsp, 8
factorial.entry:
    push rbx
    push r12
    mov rax, rdi
    lea rbx, [rbp - 8]
    mov [rbx], rax
    lea rax, [rbp - 8]
    mov rbx, [rax]
    mov rax, 1
    mov rcx, rbx
    cmp rcx, rax
    setle cl
    movzx rcx, cl
    test rcx, rcx
    jnz factorial.L2
    jmp factorial.L0
factorial.L2:
    mov rax, 1
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
    jmp factorial.L1
factorial.L0:
    lea rax, [rbp - 8]
    mov rbx, [rax]
    lea rax, [rbp - 8]
    mov rcx, [rax]
    mov rax, 1
    mov r12, rcx
    sub r12, rax
    mov rdi, r12
    call factorial
    mov rcx, rbx
    imul rcx, rax
    mov rax, rcx
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
factorial.L1:
factorial.Lfactorial_epilogue:
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
