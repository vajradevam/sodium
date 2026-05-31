# RISC-V 64 _sodium_read_str (GAS syntax)
# Read a line from stdin into a buffer.
# int64_t _sodium_read_str(void* buf, int64_t max_len)
#
# Uses the shared input buffer (__sodium_read_buf) with cursor,
# same as _sodium_read_int, so both functions can be mixed.
#
# Reads up to max_len-1 bytes or until newline, whichever comes first.
# Leading whitespace/newlines are skipped (like read_int).
# Strips the trailing newline (if present) and null-terminates.
# Returns the number of bytes read (excluding null terminator),
# or 0 on EOF / error.

.section .text
.globl _sodium_read_str

_sodium_read_str:
    # Arguments: a0 = output buf, a1 = max_len
    mv t0, a0              # t0 = output buf
    mv t1, a1              # t1 = max_len
    li t2, 0               # t2 = bytes written

    # If max_len <= 1, just null-terminate and return 0
    li t3, 2
    blt t1, t3, .Lempty

.next_char:
    # Check if output buffer is full (leave room for null)
    addi t3, t2, 1
    bge t3, t1, .Ldone    # output buffer full

    # If input buffer exhausted, refill from stdin
    la t3, __sodium_read_buf_pos
    ld t4, 0(t3)           # t4 = pos
    la t5, __sodium_read_buf_len
    ld t6, 0(t5)           # t6 = len
    blt t4, t6, .Lhave_data

    # Refill: read(0, __sodium_read_buf, 255)
    li a7, 63              # SYS_read
    li a0, 0               # stdin
    la a1, __sodium_read_buf
    li a2, 255
    ecall
    blez a0, .Ldone        # EOF or error

    # Store new len and reset pos
    la t5, __sodium_read_buf_len
    sd a0, 0(t5)
    la t3, __sodium_read_buf_pos
    sd zero, 0(t3)         # pos = 0

.Lhave_data:
    # Load byte at current position (don't advance yet)
    la t3, __sodium_read_buf_pos
    ld t4, 0(t3)           # t4 = pos
    la t5, __sodium_read_buf
    add t5, t5, t4         # t5 = &buf[pos]
    lbu t6, 0(t5)          # t6 = byte (zero-extended)

    # If we haven't started reading the line yet, skip leading whitespace
    bnez t2, .Lstore_char  # already have chars → store everything except newline

    # Leading whitespace: skip space(32), tab(9), newline(10), CR(13)
    li t5, 32
    beq t6, t5, .Lskip
    li t5, 9
    beq t6, t5, .Lskip
    li t5, 10
    beq t6, t5, .Lskip
    li t5, 13
    beq t6, t5, .Lskip

.Lstore_char:
    # Consume the byte
    la t3, __sodium_read_buf_pos
    ld t4, 0(t3)
    addi t4, t4, 1
    sd t4, 0(t3)           # pos++

    # Check for newline (end of line)
    li t5, 10
    beq t6, t5, .Ldone

    # Check for null (shouldn't happen with syscall)
    beqz t6, .Ldone

    # Store byte to output buffer
    add t4, t0, t2         # t4 = output + bytes_written
    sb t6, 0(t4)           # output[bytes_written] = byte
    addi t2, t2, 1
    j .next_char

.Lskip:
    # Consume the whitespace character
    la t3, __sodium_read_buf_pos
    ld t4, 0(t3)
    addi t4, t4, 1
    sd t4, 0(t3)           # pos++
    j .next_char

.Ldone:
    # Null-terminate output
    add t4, t0, t2
    sb zero, 0(t4)         # output[bytes_written] = 0
    mv a0, t2              # return bytes written
    ret

.Lempty:
    sb zero, 0(t0)         # output[0] = 0
    li a0, 0
    ret
