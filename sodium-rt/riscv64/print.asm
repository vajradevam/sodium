# RISC-V 64-bit _sodium_print_int
# Prints a 64-bit signed integer followed by newline
# Uses write syscall: a7 = 64, a0 = fd (1=stdout), a1 = buf, a2 = count
# Uses only caller-save registers (t0-t6, a0-a7)

.section .bss
.align 3
_print_buf:
    .zero 32

.section .text
.globl _sodium_print_int
_sodium_print_int:
    # a0 = integer to print
    mv t0, a0           # t0 = value
    
    # Point to end of buffer
    la t1, _print_buf
    li t2, 30
    add t1, t1, t2      # t1 ≈ end of buffer
    li t2, 10
    sb t2, 0(t1)        # newline at end
    addi t1, t1, -1     # move back
    
    # Check if negative
    li t3, 0            # digit count
    bgez t0, 1f
    neg t0, t0
    li t4, 1            # sign flag
    j 2f
1:  li t4, 0

2:  # Convert to digits
    li t2, 10
3:  remu t5, t0, t2     # t5 = digit
    addi t5, t5, 48
    sb t5, 0(t1)
    addi t1, t1, -1
    addi t3, t3, 1
    divu t0, t0, t2
    bnez t0, 3b
    
    # Sign
    beqz t4, 4f
    li t0, 45
    sb t0, 0(t1)
    addi t3, t3, 1
    addi t1, t1, -1

4:  addi t1, t1, 1      # t1 = start of string
    addi t3, t3, 1      # include newline
    
    # write(1, t1, t3)
    li a7, 64
    li a0, 1
    mv a1, t1
    mv a2, t3
    ecall
    
    ret
