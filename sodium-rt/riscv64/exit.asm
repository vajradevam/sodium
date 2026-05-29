# RISC-V 64-bit _sodium_exit
# Exit syscall: a7 = 93, a0 = exit code

.section .text
.globl _sodium_exit
_sodium_exit:
    li a7, 93          # SYS_exit
    ecall
    # Should not return
1:  j 1b               # loop forever if we do
