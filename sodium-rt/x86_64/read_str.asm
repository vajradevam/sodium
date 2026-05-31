; x86-64 _sodium_read_str (NASM syntax)
; Read a line from stdin into a buffer.
; int64_t _sodium_read_str(void* buf, int64_t max_len)
;
; Uses the shared input buffer (__sodium_read_buf) with cursor,
; same as _sodium_read_int, so both functions can be mixed.
;
; Reads up to max_len-1 bytes or until newline, whichever comes first.
; Leading whitespace/newlines are skipped (like read_int).
; Strips the trailing newline (if present) and null-terminates.
; Returns the number of bytes read (excluding null terminator),
; or 0 on EOF / error.

extern __sodium_read_buf
extern __sodium_read_buf_len
extern __sodium_read_buf_pos

global _sodium_read_str
_sodium_read_str:
    push rbx
    push r12
    push r13

    mov r12, rdi          ; r12 = output buf
    mov r13, rsi          ; r13 = max_len
    xor rbx, rbx          ; rbx = bytes written so far
    xor r8, r8            ; r8 = have we skipped leading whitespace yet?

    ; If max_len <= 1, just null-terminate and return 0
    cmp r13, 2
    jl .empty

.next_char:
    ; Check if we've filled the output buffer (leave room for null)
    mov rax, rbx
    add rax, 1            ; +1 for null terminator
    cmp rax, r13
    jge .done             ; output buffer full

    ; If we've exhausted the input buffer, refill from stdin
    mov rax, [rel __sodium_read_buf_pos]
    mov rcx, [rel __sodium_read_buf_len]
    cmp rax, rcx
    jl .have_data

    ; Refill: read(0, __sodium_read_buf, 255)
    lea rsi, [rel __sodium_read_buf]
    mov rdx, 255
    mov rdi, 0              ; stdin
    mov rax, 0              ; SYS_read
    syscall
    test rax, rax
    jle .done               ; EOF or error → null-terminate and return

    mov [rel __sodium_read_buf_len], rax
    mov qword [rel __sodium_read_buf_pos], 0

.have_data:
    ; Load byte at current position (don't advance yet)
    mov rax, [rel __sodium_read_buf_pos]
    lea rcx, [rel __sodium_read_buf]
    movzx rdx, byte [rcx + rax]

    ; If we haven't started reading the line yet, skip leading whitespace
    test rbx, rbx
    jnz .store_char         ; already have chars → store everything except newline

    ; Leading whitespace: skip space, tab, newline, CR
    cmp rdx, 32
    je .skip
    cmp rdx, 9
    je .skip
    cmp rdx, 10
    je .skip
    cmp rdx, 13
    je .skip

.store_char:
    ; Consume the byte
    inc qword [rel __sodium_read_buf_pos]

    ; Check for newline (end of line)
    cmp rdx, 10
    je .done

    ; Check for null (shouldn't happen with syscall)
    test rdx, rdx
    jz .done

    ; Store byte to output buffer
    mov [r12 + rbx], dl
    inc rbx
    jmp .next_char

.skip:
    inc qword [rel __sodium_read_buf_pos]   ; consume whitespace
    jmp .next_char

.done:
    ; Null-terminate output
    mov byte [r12 + rbx], 0
    mov rax, rbx           ; return bytes written (excluding null)
    pop r13
    pop r12
    pop rbx
    ret

.empty:
    mov byte [r12], 0
    xor eax, eax
    pop r13
    pop r12
    pop rbx
    ret
