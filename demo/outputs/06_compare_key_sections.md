# x86-64 vs RISC-V Key Section Comparison

## Function Prologue

### x86-64
```asm
_start_body:
    push rbp
    mov rbp, rsp
    sub rsp, 48
```

### RISC-V
```asm
_start_body:
    addi sp, sp, -16
    sd ra, 8(sp)
    sd s0, 0(sp)
    addi s0, sp, 16
    addi sp, sp, -48
```

**Note:** RISC-V saves `ra` (return address) and `s0` (frame pointer)
explicitly because `jal` does not push them automatically. x86-64's
`call` pushes the return address, and `push rbp` / `mov rbp, rsp`
saves the old frame pointer.

---

## Setting up args and calling a function

### x86-64
```asm
    mov rdi, 10          ; arg1 → rdi (1st arg reg)
    call factorial       ; call instruction
    mov [rbp - 16], rax  ; result from rax (return reg)
```

### RISC-V
```asm
    li s0, 10            ; load 10 into a temp
    mv a0, s0            ; arg1 → a0 (1st arg reg)
    jal ra, factorial    ; jump-and-link (call)
    sd a0, -16(s0)       ; result from a0 (return reg)
```

---

## Comparison: n <= 1

### x86-64
```asm
    mov rax, [rbp - 8]   ; load n
    cmp rax, 1           ; compare with 1 (sets flags)
    setle al             ; al = (n <= 1) ? 1 : 0
    movzx eax, al        ; zero-extend to full register
    test rax, rax        ; test boolean
    jnz .L2              ; branch if true
```

### RISC-V
```asm
    ld s0, -8(s0)        ; load n
    li t0, 1
    slt t0, t0, s0       ; t0 = (1 < n) ? 1 : 0
    xori t2, t0, 1       ; t2 = (n <= 1) ? 1 : 0  (invert)
    bnez t2, .L2         ; branch if true
```

**Key difference:** x86-64 uses flags (`cmp` sets FLAGS, `setle`
reads FLAGS). RISC-V uses register-to-register comparison (`slt`)
with explicit inversion (`xori`). The **IR is identical** in both
cases: `%5 = cmp_le %3, %4`.

---

## Function prologue (factorial)

### x86-64
```asm
factorial:
    push rbp
    mov rbp, rsp
    sub rsp, 8
factorial.entry:
    push rbx               ; callee-save save
    mov rax, [rbp + 16]    ; OLD: stack-based param read
```

Wait — that's the OLD code! Let me check the actual generated output:

### x86-64 (new, register-based)
```asm
factorial:
    push rbp
    mov rbp, rsp
    sub rsp, 8
factorial.entry:
    push rbx
    push r12
    mov rax, rdi           ; param 0 from arg register rdi
    lea rbx, [rbp - 8]
    mov [rbx], rax
```

### RISC-V (new, register-based)
```asm
factorial:
    addi sp, sp, -16
    sd ra, 8(sp)
    sd s0, 0(sp)
    addi s0, sp, 16
    addi sp, sp, -8
factorial.entry:
    addi sp, sp, -16
    sd s0, 0(sp)
    sd s1, 0(sp)           ; callee-save saves
    mv t0, a0              ; param 0 from arg register a0
    sd t0, -8(s0)          ; store to stack slot
```

**Both** now load parameters from argument registers (`rdi` / `a0`)
instead of reading from the stack. This is the result of the
CALL/CALL_REG consolidation and LOAD_PARAM opcode.

---

## Function epilogue

### x86-64
```asm
    pop r12                ; restore callee-save
    pop rbx                ; restore callee-save
    mov rsp, rbp           ; epilogue: restore sp
    pop rbp                ; restore old rbp
    ret
```

### RISC-V
```asm
    ld s1, 0(sp)           ; restore callee-save
    addi sp, sp, 8
    ld s0, 0(sp)           ; restore callee-save
    addi sp, sp, 8
    addi sp, s0, 0         ; epilogue: sp = s0 FIRST
    ld ra, -8(sp)          ; then load ra from sp-8
    ld s0, -16(sp)         ; then load old s0 from sp-16
    ret
```

**Note:** The RISC-V epilogue restores `sp = s0` *before* loading
`ra` and `s0`. This avoids clobbering the frame pointer that `sp`
restoration depends on (fixed in commit `a9aefd6`).
