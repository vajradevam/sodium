# RISC-V 64-bit crt0 — _start entry point
# Sets up stack and calls _sodium_main, then _sodium_exit

.section .text
.globl _start
_start:
    # SP should already be set by the loader (QEMU or kernel)
    # Call the compiled _sodium_main
    # Arguments (argc, argv, envp) are on stack — we ignore them for now
    call _sodium_main
    
    # Call exit with the return value (a0)
    mv a1, a0          # save return value
    li a0, 0           # exit with 0? No, use return value
    mv a0, a1
    call _sodium_exit

.section .note.GNU-stack, "", @progbits
