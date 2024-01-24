global _start
_start:
    mov rax, 10
    push rax
    mov rax, 1
    push rax
    mov rax, 1
    push rax
    push QWORD [rsp + 0]

    push QWORD [rsp + 0]

    push QWORD [rsp + 32]

    mov rax, 60
    pop rdi
    syscall
    mov rax, 60
    mov rdi, 0
    syscall
