.extern _sodium_exit
.extern _sodium_print_int
.extern _sodium_read_int
.extern _sodium_malloc
.extern _sodium_free
.globl _start
_start:
_start_body:
.option norelax
    addi sp, sp, -16
    sd ra, 8(sp)
    sd s0, 0(sp)
    addi s0, sp, 16
    addi sp, sp, -48
_start_body.entry:
    addi sp, sp, -8
    sd s1, 0(sp)
    addi sp, sp, -8
    sd s2, 0(sp)
    addi t1, x0, 10
    addi t2, s0, -24
    sd t1, 0(t2)
    addi t1, x0, 3
    addi t2, s0, -32
    sd t1, 0(t2)
    addi t1, s0, -24
    ld s1, 0(t1)
    mv a0, s1
    jal ra, factorial
    mv t1, a0
    addi t2, s0, -40
    sd t1, 0(t2)
    addi t1, s0, -32
    ld s1, 0(t1)
    mv a0, s1
    jal ra, factorial
    mv t1, a0
    addi t2, s0, -48
    sd t1, 0(t2)
    addi t1, s0, -24
    ld t2, 0(t1)
    addi t1, s0, -32
    ld s1, 0(t1)
    mv s2, t2
    sub s2, s2, s1
    mv a0, s2
    jal ra, factorial
    mv t1, a0
    addi t2, s0, -56
    sd t1, 0(t2)
    addi t1, s0, -40
    ld t2, 0(t1)
    addi t1, s0, -48
    ld s1, 0(t1)
    addi t1, s0, -56
    ld a0, 0(t1)
    mv t1, s1
    mul t1, t1, a0
    div s1, t2, t1
    addi t1, s0, -64
    sd s1, 0(t1)
    addi t1, s0, -64
    ld s1, 0(t1)
    mv a0, s1
    jal ra, _sodium_print_int
    mv t1, a0
    addi t1, s0, -64
    ld s1, 0(t1)
    mv a0, s1
    jal ra, _sodium_exit
    mv t1, a0
    ld s2, 0(sp)
    addi sp, sp, 8
    ld s1, 0(sp)
    addi sp, sp, 8
    addi sp, s0, 0
    ld ra, -8(sp)
    ld s0, -16(sp)
    ret
.extern factorial
factorial:
    addi sp, sp, -16
    sd ra, 8(sp)
    sd s0, 0(sp)
    addi s0, sp, 16
    addi sp, sp, -8
factorial.entry:
    addi sp, sp, -8
    sd s1, 0(sp)
    addi sp, sp, -8
    sd s2, 0(sp)
    mv t1, a0
    addi t2, s0, -24
    sd t1, 0(t2)
    addi t1, s0, -24
    ld t2, 0(t1)
    addi t1, x0, 1
    slt t0, t1, t2
    xori s1, t0, 1
    bnez s1, factorial.L2
    j factorial.L0
factorial.L2:
    addi t1, x0, 1
    mv a0, t1
    ld s2, 0(sp)
    addi sp, sp, 8
    ld s1, 0(sp)
    addi sp, sp, 8
    addi sp, s0, 0
    ld ra, -8(sp)
    ld s0, -16(sp)
    ret
    j factorial.L1
factorial.L0:
    addi t1, s0, -24
    ld s1, 0(t1)
    addi t1, s0, -24
    ld t2, 0(t1)
    addi t1, x0, 1
    mv s2, t2
    sub s2, s2, t1
    mv a0, s2
    jal ra, factorial
    mv t1, a0
    mv t2, s1
    mul t2, t2, t1
    mv a0, t2
    ld s2, 0(sp)
    addi sp, sp, 8
    ld s1, 0(sp)
    addi sp, sp, 8
    addi sp, s0, 0
    ld ra, -8(sp)
    ld s0, -16(sp)
    ret
factorial.L1:
factorial.Lfactorial_epilogue:
    ld s2, 0(sp)
    addi sp, sp, 8
    ld s1, 0(sp)
    addi sp, sp, 8
    addi sp, s0, 0
    ld ra, -8(sp)
    ld s0, -16(sp)
    ret
