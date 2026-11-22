// sodium-rt/syscalls.c
// Freestanding file-descriptor syscall wrappers shared by both backends
// (L4a). The generic syscall stubs live in <arch>/syscalls.asm.

extern long _sodium_syscall3(long n, long a1, long a2, long a3);
extern long _sodium_syscall4(long n, long a1, long a2, long a3, long a4);

#if defined(__x86_64__)
#define NR_READ   0
#define NR_WRITE  1
#define NR_OPEN   2
#define NR_CLOSE  3
#define NR_LSEEK  8
#define NR_UNLINK 87
#define NATIVE_OPEN 1
#elif defined(__riscv)
// asm-generic/unistd.h has no `open` or `unlink`; use openat/unlinkat with AT_FDCWD.
#define NR_OPENAT  56
#define NR_CLOSE   57
#define NR_LSEEK   62
#define NR_READ    63
#define NR_WRITE   64
#define NR_UNLINKAT 35
#define AT_FDCWD   (-100)
#else
#error "unsupported architecture for sodium-rt/syscalls.c"
#endif

long _sodium_sys_open(const char* path, long flags, long mode) {
#ifdef NATIVE_OPEN
    return _sodium_syscall3(NR_OPEN, (long)path, flags, mode);
#else
    return _sodium_syscall4(NR_OPENAT, AT_FDCWD, (long)path, flags, mode);
#endif
}

long _sodium_sys_close(long fd) {
    return _sodium_syscall3(NR_CLOSE, fd, 0, 0);
}

long _sodium_sys_read(long fd, void* buf, unsigned long count) {
    return _sodium_syscall3(NR_READ, fd, (long)buf, (long)count);
}

long _sodium_sys_write(long fd, const void* buf, unsigned long count) {
    return _sodium_syscall3(NR_WRITE, fd, (long)buf, (long)count);
}

long _sodium_sys_lseek(long fd, long offset, long whence) {
    return _sodium_syscall3(NR_LSEEK, fd, offset, whence);
}

long _sodium_sys_unlink(const char* path) {
#ifdef NATIVE_OPEN
    return _sodium_syscall3(NR_UNLINK, (long)path, 0, 0);
#else
    return _sodium_syscall4(NR_UNLINKAT, AT_FDCWD, (long)path, 0, 0);
#endif
}
