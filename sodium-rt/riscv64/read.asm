# RISC-V 64-bit _sodium_read_int
# Reads a 64-bit signed integer from stdin
# Uses a shared .bss buffer with cursor, same as _sodium_read_str.
# Returns the parsed integer in a0

.section .bss
.align 3
.globl __sodium_read_buf
.globl __sodium_read_buf_len
.globl __sodium_read_buf_pos

__sodium_read_buf:
    .zero 256
__sodium_read_buf_len:
    .zero 8
__sodium_read_buf_pos:
    .zero 8

.section .text
.globl _sodium_read_int
_sodium_read_int:
    li t1, 0            # result accumulator
    li t2, 0            # sign: 0=positive, 1=negative
    li t3, 0            # digit_seen flag: 0 = no digits yet

.next_char:
    # If buffer exhausted, refill
    la t4, __sodium_read_buf_pos
    ld t5, 0(t4)        # t5 = pos
    la t6, __sodium_read_buf_len
    ld t0, 0(t6)        # t0 = len
    blt t5, t0, .have_data

    # Refill: read(0, __sodium_read_buf, 255)
    li a7, 63           # SYS_read
    li a0, 0            # stdin
    la a1, __sodium_read_buf
    li a2, 255
    ecall
    blez a0, .done      # EOF or error

    # Store new len and reset pos
    la t6, __sodium_read_buf_len
    sd a0, 0(t6)
    la t4, __sodium_read_buf_pos
    sd zero, 0(t4)      # pos = 0
    li t5, 0            # t5 = pos = 0

.have_data:
    # Load byte at current position (don't advance yet)
    la t0, __sodium_read_buf
    add t0, t0, t5      # t0 = &buf[pos]
    lbu t4, 0(t0)       # t4 = current byte (zero-extended)

    # If we've seen digits, any non-digit terminates the number
    bnez t3, .check_non_digit

    # No digits yet: skip leading whitespace: space(32), tab(9), newline(10), CR(13)
    li t5, 32
    beq t4, t5, .skip
    li t5, 9
    beq t4, t5, .skip
    li t5, 10
    beq t4, t5, .skip
    li t5, 13
    beq t4, t5, .skip

    # Check for minus sign
    li t5, 45           # '-'
    bne t4, t5, .try_digit
    li t2, 1            # negative flag
    j .skip

.try_digit:
    li t5, 48           # '0'
    blt t4, t5, .done   # non-digit → nothing to return
    li t5, 57           # '9'
    bgt t4, t5, .done   # non-digit → nothing to return

    # First digit
    la t5, __sodium_read_buf_pos
    ld t6, 0(t5)
    addi t6, t6, 1
    sd t6, 0(t5)        # pos++

    addi t4, t4, -48    # '0' → 0
    mv t1, t4           # result = digit
    li t3, 1            # digit_seen = 1
    j .next_char

.check_non_digit:
    # We've seen at least one digit. Non-digit → terminate.
    li t5, 48           # '0'
    blt t4, t5, .done   # below '0' → terminate, keep char
    li t5, 57           # '9'
    bgt t4, t5, .done   # above '9' → terminate, keep char

    # Another digit: consume and accumulate
    la t5, __sodium_read_buf_pos
    ld t6, 0(t5)
    addi t6, t6, 1
    sd t6, 0(t5)        # pos++

    addi t4, t4, -48    # '0' → 0
    li t5, 10
    mul t1, t1, t5      # result *= 10
    add t1, t1, t4      # result += digit
    j .next_char

.skip:
    # Consume the whitespace/minus character
    la t5, __sodium_read_buf_pos
    ld t6, 0(t5)
    addi t6, t6, 1
    sd t6, 0(t5)        # pos++
    j .next_char

.done:
    beqz t2, .positive
    neg t1, t1
.positive:
    mv a0, t1
    ret
