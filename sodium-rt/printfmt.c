// sodium-rt/printfmt.c
// Freestanding conversion helpers for the %x, %u and %c print specifiers.
// Compiles for both x86-64 and RISC-V with no libc dependencies.

// Implemented in print.asm for each target: write(1, buf, len), looping on
// partial writes. Uses only caller-saved registers.
extern void _sodium_write_buf(const char* buf, unsigned long len);

// Shared scratch buffer. Conversion helpers are called one at a time from
// generated code, so no reentrancy is required.
static char fmt_buf[32];

// Convert an unsigned value into `p` backwards, returning the digit count.
static unsigned long fmt_digits(char* p, unsigned long value, unsigned int base,
                                const char* digits) {
    char* end = p;
    do {
        unsigned int d = (unsigned int)(value % base);
        *--p = digits[d];
        value /= base;
    } while (value != 0);
    return (unsigned long)(end - p);
}

// Lowercase hexadecimal without a prefix, e.g. 255 -> "ff".
void _sodium_print_raw_hex(unsigned long value) {
    char* p = fmt_buf + sizeof(fmt_buf);
    unsigned long n = fmt_digits(p, value, 16, "0123456789abcdef");
    _sodium_write_buf(p - n, n);
}

// Unsigned decimal, including the full u64 range, e.g. -1 -> 18446744073709551615.
void _sodium_print_raw_uint(unsigned long value) {
    char* p = fmt_buf + sizeof(fmt_buf);
    unsigned long n = fmt_digits(p, value, 10, "0123456789");
    _sodium_write_buf(p - n, n);
}

// A single raw byte (the low 8 bits of the argument), NUL included.
void _sodium_print_raw_char(unsigned long value) {
    fmt_buf[0] = (char)(value & 0xffUL);
    _sodium_write_buf(fmt_buf, 1);
}
