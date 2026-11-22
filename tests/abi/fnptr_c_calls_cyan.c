/* C-interop (D-014, Y10b): C calls Cyan function pointers and passes C pointers to Cyan. */
#include <stdint.h>

struct P { int64_t a; int64_t b; };
struct Q { int64_t a; int64_t b; int64_t c; };

extern int64_t (*cy_get_add(void))(int64_t, int64_t);
extern int64_t (*cy_get_pair_sum(void))(struct P);
extern struct Q (*cy_get_make_q(void))(int64_t);
extern int64_t (*cy_get_ten(void))(int64_t, int64_t, int64_t, int64_t, int64_t,
                                   int64_t, int64_t, int64_t, int64_t, int64_t);
extern int64_t cy_call_c(int64_t (*cb)(int64_t, int64_t), int64_t x);

static int64_t c_sub(int64_t a, int64_t b) { return a - b; }

int main(void) {
    if (cy_get_add()(20, 22) != 42) return 1;
    struct P p = { 30, 12 };
    if (cy_get_pair_sum()(p) != 42) return 2;
    struct Q q = cy_get_make_q()(5);
    if (q.a != 5 || q.b != 6 || q.c != 7) return 3;
    if (cy_get_ten()(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 385) return 4;
    /* Cyan calls back into a C function pointer: (7 - 100) + (7 - 200) = -286 */
    if (cy_call_c(c_sub, 7) != -286) return 5;
    return 0;
}
