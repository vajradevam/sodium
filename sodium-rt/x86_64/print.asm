; x86-64 runtime: print signed 64-bit integer followed by newline
; void _sodium_print_int(int64_t value)
global _sodium_print_int

_sodium_print_int:
    ; rdi = value to print
    mov rax, rdi
    sub rsp, 32
    mov rdi, rsp
    add rdi, 31
    mov byte [rdi], 10       ; trailing newline

    test rax, rax
    jnz .Lnon_zero
    dec rdi
    mov byte [rdi], '0'
    jmp .Ldone

.Lnon_zero:
    mov r8, 0
    cmp rax, 0
    jge .Lneg_ok
    mov r8, 1               ; negative flag
    neg rax
.Lneg_ok:

.Lloop:
    dec rdi
    mov rcx, 10
    xor rdx, rdx
    div rcx
    add dl, '0'
    mov [rdi], dl
    test rax, rax
    jnz .Lloop

    cmp r8, 1
    jne .Ldone
    dec rdi
    mov byte [rdi], '-'

.Ldone:
    mov rsi, rdi            ; buf = rdi
    mov rdx, rsp
    add rdx, 32
    sub rdx, rsi            ; len = rsp+32 - buf
    mov rax, 1              ; syscall: write
    mov rdi, 1              ; fd = stdout
    syscall
    add rsp, 32
    ret

; x86-64 runtime: print null-terminated string
; void _sodium_print_str(const char *str)
global _sodium_print_str

_sodium_print_str:
    ; rdi = pointer to null-terminated string
    push rbx
    mov rbx, rdi
    ; strlen(rdi)
    mov rdi, rbx
    xor rax, rax
    mov rcx, -1
    repne scasb
    not rcx
    dec rcx                 ; rcx = strlen (excluding null)
    ; write(1, rbx, rcx)
    mov rax, 1              ; syscall: write
    mov rdi, 1              ; stdout
    mov rsi, rbx            ; buf
    mov rdx, rcx            ; len
    syscall
    pop rbx
    ret
