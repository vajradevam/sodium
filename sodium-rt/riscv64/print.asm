# RISC-V 64-bit print helpers
# Uses write syscall: a7 = 64, a0 = fd (1=stdout), a1 = buf, a2 = count
# Uses only caller-save registers (t0-t6, a0-a7)

.section .bss
.align 3
_print_buf:
    .zero 32

.section .text

# ── _sodium_print_int: print integer + newline (legacy compat) ─────────────
.globl _sodium_print_int
_sodium_print_int:
    call _sodium_print_raw_int
    # write(1, "\n", 1)
    addi sp, sp, -16
    li t0, 10
    sb t0, 0(sp)
    li a7, 64
    li a0, 1
    mv a1, sp
    li a2, 1
    ecall
    addi sp, sp, 16
    ret

# ── _sodium_print_raw_int: print integer, NO newline ──────────────────────
.globl _sodium_print_raw_int
_sodium_print_raw_int:
    mv t0, a0           # t0 = value to print
    la t1, _print_buf   # t1 = buffer start
    li t2, 30
    add t1, t1, t2      # t1 = &buf[30] (near end, space for 30 digits + sign)

    # Special case: value is 0
    bnez t0, 1f
    li t2, '0'
    sb t2, 0(t1)
    addi t1, t1, -1     # label 4 advances t1; keep it pointing at this digit
    li t3, 1            # digit count = 1
    j 4f

1:  # Check sign
    li t4, 0            # sign flag
    bgez t0, 2f
    neg t0, t0
    li t4, 1            # negative

2:  # Convert digits
    li t3, 0            # digit count
3:  li t2, 10
    remu t5, t0, t2     # t5 = digit
    addi t5, t5, '0'
    sb t5, 0(t1)
    addi t1, t1, -1
    addi t3, t3, 1
    divu t0, t0, t2
    bnez t0, 3b

    # Sign
    beqz t4, 4f
    li t2, '-'
    sb t2, 0(t1)
    addi t3, t3, 1
    addi t1, t1, -1

4:  addi t1, t1, 1      # t1 = start of string
    # write(1, t1, t3)
    li a7, 64
    li a0, 1
    mv a1, t1
    mv a2, t3
    ecall
    ret

# ── _sodium_print_str: print null-terminated string + newline (legacy) ─────
.globl _sodium_print_str
_sodium_print_str:
    call _sodium_print_raw_str
    # write trailing newline
    addi sp, sp, -16
    li t0, 10
    sb t0, 0(sp)
    li a7, 64
    li a0, 1
    mv a1, sp
    li a2, 1
    ecall
    addi sp, sp, 16
    ret

# ── _sodium_print_raw_str: print null-terminated string, NO newline ───────
.globl _sodium_print_raw_str
_sodium_print_raw_str:
    mv t0, a0           # save pointer
    # strlen: scan for null byte
    mv t1, a0
1:  lbu t2, 0(t1)
    beqz t2, 2f
    addi t1, t1, 1
    j 1b
2:  sub t2, t1, t0      # t2 = strlen
    # write(1, t0, t2)
    li a7, 64
    li a0, 1
    mv a1, t0
    mv a2, t2
    ecall
    ret

# ── _sodium_print_nl: print just a newline ────────────────────────────────
.globl _sodium_print_nl
_sodium_print_nl:
    addi sp, sp, -16
    li t0, 10
    sb t0, 0(sp)
    li a7, 64
    li a0, 1
    mv a1, sp
    li a2, 1
    ecall
    addi sp, sp, 16
    ret
