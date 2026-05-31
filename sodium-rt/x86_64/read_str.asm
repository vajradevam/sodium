; x86-64 _sodium_read_str (NASM syntax)
; Read a line from stdin into a buffer.
; int64_t _sodium_read_str(void* buf, int64_t max_len)
;
; Reads up to max_len-1 bytes or until newline, whichever comes first.
; Strips the trailing newline (if present) and null-terminates.
; Returns the number of bytes read (excluding null terminator),
; or 0 on EOF / error.

global _sodium_read_str
_sodium_read_str:
    push rbx

    mov rbx, rdi          ; rbx = buf
    mov rcx, rsi          ; rcx = max_len

    ; If max_len < 2, return 0 with null terminator
    cmp rcx, 2
    jl .empty

    ; syscall read(0, buf, max_len - 1)
    mov eax, 0            ; SYS_read
    mov edi, 0            ; stdin
    mov rsi, rbx          ; buf
    lea rdx, [rcx - 1]    ; count = max_len - 1
    syscall

    ; Check result
    test rax, rax
    jle .empty            ; EOF or error

    ; rax = n (bytes read)
    mov rdx, rax

    ; Null-terminate
    mov byte [rbx + rdx], 0

    ; Strip trailing newline
    cmp byte [rbx + rdx - 1], 10
    jne .done
    mov byte [rbx + rdx - 1], 0
    dec rax               ; don't count the newline

.done:
    pop rbx
    ret

.empty:
    mov byte [rbx], 0
    xor eax, eax
    pop rbx
    ret
