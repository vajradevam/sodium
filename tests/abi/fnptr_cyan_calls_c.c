/* C-interop (D-014, Y10b): C functions that Cyan reaches through function pointers. */
#include <stdint.h>

struct P { int64_t a; int64_t b; };
struct Q { int64_t a; int64_t b; int64_t c; };

static int64_t add(int64_t a, int64_t b) { return a + b; }
static int64_t mix10(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                     int64_t f, int64_t g, int64_t h, int64_t i, int64_t j) {
    return a * 1 + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10;
}
static int64_t pair_sum(struct P p) { return p.a + p.b; }
static struct Q make_q(int64_t a) { struct Q q = { a, a + 1, a + 2 }; return q; }
static struct P make_p(int64_t a, int64_t b) { struct P p = { b, a }; return p; }

int64_t (*c_get_add(void))(int64_t, int64_t) { return add; }
int64_t (*c_get_mix10(void))(int64_t, int64_t, int64_t, int64_t, int64_t,
                             int64_t, int64_t, int64_t, int64_t, int64_t) { return mix10; }
int64_t (*c_get_pair_sum(void))(struct P) { return pair_sum; }
struct Q (*c_get_make_q(void))(int64_t) { return make_q; }
struct P (*c_get_make_p(void))(int64_t, int64_t) { return make_p; }

int64_t c_call_back(int64_t (*cb)(int64_t, int64_t), int64_t x) { return cb(x, 3); }
