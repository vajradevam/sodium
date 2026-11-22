# RISC-V 64-bit raw syscall stubs (L4a).
# Syscall number in a0, up to four arguments in a1..a4.
# `ecall` clobbers no caller-saved register beyond a0 (the return value).

.globl _sodium_syscall4
_sodium_syscall4:
    mv a7, a0               # syscall number
    mv a0, a1
    mv a1, a2
    mv a2, a3
    mv a3, a4
    ecall
    ret

.globl _sodium_syscall3
_sodium_syscall3:
    li a4, 0
    j _sodium_syscall4
