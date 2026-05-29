# RISC-V 64-bit _sodium_read_int
# Reads a 64-bit signed integer from stdin
# Uses read syscall: a7 = 63, a0 = fd (0=stdin), a1 = buf, a2 = count
# Returns the parsed integer in a0

.section .bss
.align 3
_read_buf:
    .zero 32

.section .text
.globl _sodium_read_int
_sodium_read_int:
    # read(0, _read_buf, 31)
    li a7, 63           # SYS_read
    li a0, 0            # stdin
    la a1, _read_buf
    li a2, 31
    ecall
    
    # Parse the integer
    la t0, _read_buf
    li t1, 0            # result
    li t2, 0            # sign: 0=positive, 1=negative
    li t3, 10           # base
    
    # Skip whitespace
1:  lb t4, 0(t0)
    li t5, 32           # space
    beq t4, t5, 2f
    li t5, 10           # newline
    beq t4, t5, 2f
    li t5, 13           # carriage return
    beq t4, t5, 2f
    li t5, 9            # tab
    bne t4, t5, 3f
2:  addi t0, t0, 1
    j 1b
    
3:  # Check for minus
    li t5, 45
    bne t4, t5, 4f
    li t2, 1            # negative
    addi t0, t0, 1
    lb t4, 0(t0)
    
4:  # Parse digits
    li t5, 48           # '0'
    blt t4, t5, 6f
    li t5, 57           # '9'
    bgt t4, t5, 6f
    addi t4, t4, -48    # convert to digit
    mul t1, t1, t3      # result *= 10
    add t1, t1, t4      # result += digit
    addi t0, t0, 1
    lb t4, 0(t0)
    j 4b
    
6:  # Apply sign
    beqz t2, 7f
    neg t1, t1
    
7:  mv a0, t1
    ret
