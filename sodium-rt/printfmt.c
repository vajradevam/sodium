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

// ---- floating point (D-038) ----------------------------------------------
//
// `print` of a float writes fixed notation with six fractional digits, like
// printf("%f"): the exact value rounded to nearest-even, "inf", "-inf", "nan".
// The conversion is exact for every double: the scaled value mant * 2^e * 10^6
// is computed in a small multi-limb integer and rounded once.

#define FLT_LIMBS 40  /* 1280 bits: 53 + 20 + 971 plus slack */

typedef struct { unsigned int limb[FLT_LIMBS]; int n; } fbig;

static void fbig_mul_small(fbig* b, unsigned int m) {
    unsigned long long carry = 0;
    for (int i = 0; i < b->n; i++) {
        unsigned long long cur = (unsigned long long)b->limb[i] * m + carry;
        b->limb[i] = (unsigned int)cur;
        carry = cur >> 32;
    }
    if (carry) b->limb[b->n++] = (unsigned int)carry;
}

static void fbig_shl(fbig* b, int bits) {
    int words = bits / 32, rest = bits % 32;
    if (words) {
        for (int i = b->n - 1; i >= 0; i--) b->limb[i + words] = b->limb[i];
        for (int i = 0; i < words; i++) b->limb[i] = 0;
        b->n += words;
    }
    if (rest) {
        unsigned int carry = 0;
        for (int i = 0; i < b->n; i++) {
            unsigned int next = b->limb[i] >> (32 - rest);
            b->limb[i] = (b->limb[i] << rest) | carry;
            carry = next;
        }
        if (carry) b->limb[b->n++] = carry;
    }
}

static int fbig_bit(const fbig* b, int i) {
    int w = i / 32;
    return w < b->n ? (int)((b->limb[w] >> (i % 32)) & 1u) : 0;
}

/* True if any bit below position `i` is set. */
static int fbig_any_below(const fbig* b, int i) {
    int w = i / 32;
    for (int k = 0; k < w && k < b->n; k++) if (b->limb[k]) return 1;
    if (w < b->n && (b->limb[w] & ((1u << (i % 32)) - 1u))) return 1;
    return 0;
}

static void fbig_shr(fbig* b, int bits) {
    int words = bits / 32, rest = bits % 32;
    if (words >= b->n) { b->n = 0; return; }
    for (int i = 0; i + words < b->n; i++) b->limb[i] = b->limb[i + words];
    b->n -= words;
    if (rest) {
        for (int i = 0; i < b->n; i++) {
            unsigned int hi = i + 1 < b->n ? b->limb[i + 1] : 0;
            b->limb[i] = (b->limb[i] >> rest) | (hi << (32 - rest));
        }
    }
    while (b->n > 0 && b->limb[b->n - 1] == 0) b->n--;
}

static void fbig_add_one(fbig* b) {
    for (int i = 0; i < b->n; i++) {
        if (++b->limb[i] != 0) return;
    }
    b->limb[b->n++] = 1;
}

/* Divide by 10^9 in place; returns the remainder. */
static unsigned int fbig_divmod_1e9(fbig* b) {
    unsigned long long rem = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        unsigned long long cur = (rem << 32) | b->limb[i];
        b->limb[i] = (unsigned int)(cur / 1000000000u);
        rem = cur % 1000000000u;
    }
    while (b->n > 0 && b->limb[b->n - 1] == 0) b->n--;
    return (unsigned int)rem;
}

void _sodium_print_raw_float(double value) {
    static char out[400];
    union { double d; unsigned long long u; } bits;
    bits.d = value;
    int negative = (int)(bits.u >> 63);
    int exp = (int)((bits.u >> 52) & 0x7ffu);
    unsigned long long frac = bits.u & 0xfffffffffffffull;
    int len = 0;

    if (exp == 0x7ff) {
        const char* text = frac ? "nan" : (negative ? "-inf" : "inf");
        while (*text) out[len++] = *text++;
        _sodium_write_buf(out, (unsigned long)len);
        return;
    }

    unsigned long long mant = exp ? (frac | (1ull << 52)) : frac;
    int e = (exp ? exp : 1) - 1075;           /* value = mant * 2^e */

    fbig b;
    b.n = 0;
    if (mant) {
        b.limb[0] = (unsigned int)mant;
        b.limb[1] = (unsigned int)(mant >> 32);
        b.n = b.limb[1] ? 2 : 1;
    }
    fbig_mul_small(&b, 1000000u);
    if (e >= 0) {
        fbig_shl(&b, e);
    } else {
        int k = -e;
        int half = fbig_bit(&b, k - 1);
        int sticky = fbig_any_below(&b, k - 1);
        fbig_shr(&b, k);
        int odd = b.n > 0 ? (int)(b.limb[0] & 1u) : 0;
        if (half && (sticky || odd)) fbig_add_one(&b);
    }

    /* b = round(|value| * 10^6): peel decimal digits, least significant first. */
    char digits[360];
    int nd = 0;
    while (b.n > 0) {
        unsigned int chunk = fbig_divmod_1e9(&b);
        for (int i = 0; i < 9; i++) {
            digits[nd++] = (char)('0' + chunk % 10u);
            chunk /= 10u;
        }
    }
    while (nd > 7 && digits[nd - 1] == '0') nd--;   /* drop the zero padding of the top chunk */
    while (nd < 7) digits[nd++] = '0';              /* at least "0.000000" */

    if (negative) out[len++] = '-';
    for (int i = nd - 1; i >= 6; i--) out[len++] = digits[i];
    out[len++] = '.';
    for (int i = 5; i >= 0; i--) out[len++] = digits[i];
    _sodium_write_buf(out, (unsigned long)len);
}
