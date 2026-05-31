; x86-64 runtime print helpers
; All functions use only caller-saved registers.

; ── _sodium_print_int: print integer + newline (legacy compat) ─────────────
global _sodium_print_int
_sodium_print_int:
    call _sodium_print_raw_int
    ; fall through to print newline
    sub rsp, 16
    mov byte [rsp], 10
    mov rax, 1
    mov rdi, 1
    mov rsi, rsp
    mov rdx, 1
    syscall
    add rsp, 16
    ret

; ── _sodium_print_raw_int: print integer, NO newline ──────────────────────
global _sodium_print_raw_int
_sodium_print_raw_int:
    mov rax, rdi
    sub rsp, 32
    mov rdi, rsp
    add rdi, 31

    test rax, rax
    jnz .non_zero
    dec rdi
    mov byte [rdi], '0'
    jmp .done_raw

.non_zero:
    mov r8, 0
    cmp rax, 0
    jge .neg_ok
    mov r8, 1               ; negative flag
    neg rax
.neg_ok:

.loop:
    dec rdi
    mov rcx, 10
    xor rdx, rdx
    div rcx
    add dl, '0'
    mov [rdi], dl
    test rax, rax
    jnz .loop

    cmp r8, 1
    jne .done_raw
    dec rdi
    mov byte [rdi], '-'

.done_raw:
    mov rsi, rdi            ; buf = rdi
    lea rdx, [rsp + 31]     ; end of usable buffer (no newline byte)
    sub rdx, rsi            ; len = end - buf  (end excludes garbage at rsp+31)
    mov rax, 1              ; syscall: write
    mov rdi, 1              ; fd = stdout
    syscall
    add rsp, 32
    ret

; ── _sodium_print_str: print null-terminated string + newline (legacy) ────
global _sodium_print_str
_sodium_print_str:
    call _sodium_print_raw_str
    ; fall through to print newline
    sub rsp, 16
    mov byte [rsp], 10
    mov rax, 1
    mov rdi, 1
    mov rsi, rsp
    mov rdx, 1
    syscall
    add rsp, 16
    ret

; ── _sodium_print_raw_str: print null-terminated string, NO newline ───────
global _sodium_print_raw_str
_sodium_print_raw_str:
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
    mov rax, 1
    mov rdi, 1
    mov rsi, rbx
    mov rdx, rcx
    syscall
    pop rbx
    ret

; ── _sodium_print_nl: print just a newline ────────────────────────────────
global _sodium_print_nl
_sodium_print_nl:
    sub rsp, 16
    mov byte [rsp], 10      ; newline
    mov rax, 1
    mov rdi, 1
    mov rsi, rsp
    mov rdx, 1
    syscall
    add rsp, 16
    ret
