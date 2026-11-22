/* C-interop (D-012, Y8b4b): the C implementations called from cyan_calls_c.cyan. */
#include <stdint.h>

struct T1 { int8_t a; };
struct T3 { int8_t a; int8_t b; int8_t c; };
struct T6 { int16_t a; int16_t b; int16_t c; };
struct T8 { int32_t a; int32_t b; };
struct T12 { int32_t a; int32_t b; int32_t c; };
struct T16 { int64_t a; int64_t b; };
struct T24 { int64_t a; int64_t b; int64_t c; };
struct T40 { int64_t a; int64_t b; int64_t c; int64_t d; int64_t e; };
struct TM { int8_t a; int64_t b; int16_t c; };

int64_t c_sum_T1(struct T1 s) { return s.a * 1; }
struct T1 c_make_T1(int8_t a) {
    struct T1 r;
    r.a = a;
    return r;
}
int64_t c_sum_T3(struct T3 s) { return s.a * 1 + s.b * 10 + s.c * 100; }
struct T3 c_make_T3(int8_t a, int8_t b, int8_t c) {
    struct T3 r;
    r.a = a;
    r.b = b;
    r.c = c;
    return r;
}
int64_t c_sum_T6(struct T6 s) { return s.a * 1 + s.b * 10 + s.c * 100; }
struct T6 c_make_T6(int16_t a, int16_t b, int16_t c) {
    struct T6 r;
    r.a = a;
    r.b = b;
    r.c = c;
    return r;
}
int64_t c_sum_T8(struct T8 s) { return s.a * 1 + s.b * 10; }
struct T8 c_make_T8(int32_t a, int32_t b) {
    struct T8 r;
    r.a = a;
    r.b = b;
    return r;
}
int64_t c_sum_T12(struct T12 s) { return s.a * 1 + s.b * 10 + s.c * 100; }
struct T12 c_make_T12(int32_t a, int32_t b, int32_t c) {
    struct T12 r;
    r.a = a;
    r.b = b;
    r.c = c;
    return r;
}
int64_t c_sum_T16(struct T16 s) { return s.a * 1 + s.b * 10; }
struct T16 c_make_T16(int64_t a, int64_t b) {
    struct T16 r;
    r.a = a;
    r.b = b;
    return r;
}
int64_t c_sum_T24(struct T24 s) { return s.a * 1 + s.b * 10 + s.c * 100; }
struct T24 c_make_T24(int64_t a, int64_t b, int64_t c) {
    struct T24 r;
    r.a = a;
    r.b = b;
    r.c = c;
    return r;
}
int64_t c_sum_T40(struct T40 s) { return s.a * 1 + s.b * 10 + s.c * 100 + s.d * 1000 + s.e * 10000; }
struct T40 c_make_T40(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e) {
    struct T40 r;
    r.a = a;
    r.b = b;
    r.c = c;
    r.d = d;
    r.e = e;
    return r;
}
int64_t c_sum_TM(struct TM s) { return s.a * 1 + s.b * 10 + s.c * 100; }
struct TM c_make_TM(int8_t a, int64_t b, int16_t c) {
    struct TM r;
    r.a = a;
    r.b = b;
    r.c = c;
    return r;
}
int64_t c_pos_mix(int64_t x, struct T16 p, int64_t y, struct T24 q, int64_t z) { return x + p.a * 10 + p.b * 100 + y * 1000 + q.a * 10000 + q.b * 100000 + q.c * 1000000 + z * 10000000; }
int64_t c_backfill(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, struct T16 p, int64_t g) { return a + b * 10 + c * 100 + d * 1000 + e * 10000 + p.a * 100000 + p.b * 1000000 + g * 10000000; }
int64_t c_straddle7(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, struct T16 p) { return a + b * 10 + c * 100 + d * 1000 + e * 10000 + f * 100000 + g * 1000000 + p.a * 10000000 + p.b * 100000000; }
int64_t c_small_mix(struct T3 p, struct T6 q, struct T12 r, struct T8 s) { return p.a + p.b * 2 + p.c * 3 + q.a * 4 + q.b * 5 + q.c * 6 + r.a * 7 + r.b * 8 + r.c * 9 + s.a * 10 + s.b * 11; }
int64_t c_big_two(struct T40 p, struct T40 q, int64_t k) { return p.a + p.e * 10 + q.a * 100 + q.e * 1000 + k * 10000 + p.c * 100000 + q.c * 1000000; }
int64_t c_mem_mixed(struct TM m, int64_t x, struct TM n) { return m.a + m.b * 10 + m.c * 100 + x * 1000 + n.a * 10000 + n.b * 100000 + n.c * 1000000; }
struct T16 c_relay16(struct T16 p) {
    struct T16 r;
    r.a = p.b + 7;
    r.b = p.a + 7;
    return r;
}
struct T24 c_relay24(struct T24 p) {
    struct T24 r;
    r.a = p.a + 7;
    r.b = p.b + 7;
    r.c = p.c + 7;
    return r;
}
struct T40 c_relay40(struct T40 p) {
    struct T40 r;
    r.a = p.e;
    r.b = p.d;
    r.c = p.c;
    r.d = p.b;
    r.e = p.a;
    return r;
}
struct T12 c_relay12(struct T12 p) {
    struct T12 r;
    r.a = p.b;
    r.b = p.c;
    r.c = p.a;
    return r;
}
struct T3 c_relay3(struct T3 p) {
    struct T3 r;
    r.a = p.c;
    r.b = p.b;
    r.c = p.a;
    return r;
}
struct T8 c_relay8(struct T8 p) {
    struct T8 r;
    r.a = p.b + 7;
    r.b = p.a + 7;
    return r;
}
struct TM c_relaym(struct TM p) {
    struct TM r;
    r.a = p.c;
    r.b = p.b + 7;
    r.c = p.a;
    return r;
}
