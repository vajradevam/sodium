; x86-64 runtime: read a signed 64-bit integer from stdin
; int64_t _sodium_read_int(void)
;
; Uses a shared .bss buffer with a cursor so that multiple calls
; correctly consume input without re-parsing old data.
; The buffer is also shared with _sodium_read_str.

section .bss
global __sodium_read_buf
global __sodium_read_buf_len
global __sodium_read_buf_pos
global __sodium_read_eof

__sodium_read_buf:
    resb 256               ; shared input buffer (256 bytes)
__sodium_read_buf_len:
    resq 1                 ; total bytes in buffer from last syscall
__sodium_read_buf_pos:
    resq 1                 ; current read position within buffer
__sodium_read_eof:
    resq 1                 ; 1 if the most recent read hit EOF, else 0

section .text
global _sodium_read_int

_sodium_read_int:
    push rbx
    push r12
    xor r8, r8              ; accumulator
    xor r9, r9              ; negative flag (0=positive, 1=negative)
    xor rbx, rbx            ; rbx = 0: have we seen any digit yet?

.next_char:
    ; If we've exhausted the buffer, refill from stdin
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
    jle .eof                ; EOF or error → return what we have

    mov qword [rel __sodium_read_eof], 0
    mov [rel __sodium_read_buf_len], rax
    mov qword [rel __sodium_read_buf_pos], 0
    jmp .have_data

.eof:
    mov qword [rel __sodium_read_eof], 1
    jmp .done

.have_data:
    mov qword [rel __sodium_read_eof], 0
    ; Load byte at current position
    mov rax, [rel __sodium_read_buf_pos]
    lea r12, [rel __sodium_read_buf]
    movzx r10, byte [r12 + rax]

    ; If we haven't seen any digits yet, skip leading whitespace
    test rbx, rbx
    jnz .check_non_digit    ; already have digits → non-digit terminates

    ; Skip leading whitespace (space, tab, newline, CR)
    cmp r10, 32
    je .skip
    cmp r10, 9
    je .skip
    cmp r10, 10
    je .skip
    cmp r10, 13
    je .skip

    ; Check for minus sign (only before any digits)
    cmp r10, '-'
    jne .try_digit
    mov r9, 1               ; negative flag
    jmp .skip

.try_digit:
    cmp r10, 48             ; '0'
    jl .done                ; non-digit → nothing to return
    cmp r10, 57             ; '9'
    jg .done                ; non-digit → nothing to return

    ; First digit
    inc qword [rel __sodium_read_buf_pos]   ; consume the digit
    sub r10, 48             ; '0' → 0
    mov r8, r10
    mov rbx, 1              ; seen digit
    jmp .next_char

.check_non_digit:
    ; We've seen at least one digit. Non-digit → terminate number.
    cmp r10, 48             ; '0'
    jl .done                ; below '0' → terminate (keep char in buffer)
    cmp r10, 57             ; '9'
    jg .done                ; above '9' → terminate (keep char in buffer)

    ; Another digit: consume and accumulate
    inc qword [rel __sodium_read_buf_pos]   ; consume the digit
    sub r10, 48             ; '0' → 0
    imul r8, r8, 10
    add r8, r10
    jmp .next_char

.skip:
    inc qword [rel __sodium_read_buf_pos]   ; consume whitespace/minus
    jmp .next_char

.done:
    test r9, r9
    jz .positive
    neg r8
.positive:
    mov rax, r8
    pop r12
    pop rbx
    ret

; int64_t _sodium_read_eof(void)
global _sodium_read_eof
_sodium_read_eof:
    mov rax, [rel __sodium_read_eof]
    ret
