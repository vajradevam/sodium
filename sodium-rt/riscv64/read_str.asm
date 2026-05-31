# RISC-V 64 _sodium_read_str (GAS syntax)
# Read a line from stdin into a buffer.
# int64_t _sodium_read_str(void* buf, int64_t max_len)
#
# Reads up to max_len-1 bytes or until newline, whichever comes first.
# Strips the trailing newline (if present) and null-terminates.
# Returns the number of bytes read (excluding null terminator),
# or 0 on EOF / error.

.text
.globl _sodium_read_str
_sodium_read_str:
    # Arguments: a0 = buf, a1 = max_len
    mv t0, a0              # t0 = buf
    mv t1, a1              # t1 = max_len

    # If max_len < 2, return 0 with null terminator
    li t2, 2
    blt t1, t2, .Lempty

    # read(0, buf, max_len - 1)
    li a7, 63              # SYS_read
    li a0, 0               # stdin
    mv a1, t0              # buf
    addi a2, t1, -1        # count = max_len - 1
    ecall

    # a0 = bytes read (or negative error code)
    blez a0, .Lempty

    # a0 = n
    mv t2, a0              # t2 = n

    # Null-terminate: buf[n] = 0
    add t3, t0, t2
    sb zero, 0(t3)

    # Check for trailing newline: buf[n-1]
    addi t3, t0, -1
    add t3, t3, t2         # t3 = buf + n - 1
    lb t4, 0(t3)
    li t5, 10
    bne t4, t5, .Ldone

    # Strip newline: buf[n-1] = 0, return n-1
    sb zero, 0(t3)
    addi a0, t2, -1

.Ldone:
    ret

.Lempty:
    sb zero, 0(t0)         # buf[0] = 0
    li a0, 0
    ret
