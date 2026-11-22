/* C-interop (D-012, Y8b4b): C calls Cyan functions defined in c_calls_cyan.cyan. */
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

extern int64_t sum_T1(struct T1 s);
extern struct T1 make_T1(int8_t a);
extern int64_t sum_T3(struct T3 s);
extern struct T3 make_T3(int8_t a, int8_t b, int8_t c);
extern int64_t sum_T6(struct T6 s);
extern struct T6 make_T6(int16_t a, int16_t b, int16_t c);
extern int64_t sum_T8(struct T8 s);
extern struct T8 make_T8(int32_t a, int32_t b);
extern int64_t sum_T12(struct T12 s);
extern struct T12 make_T12(int32_t a, int32_t b, int32_t c);
extern int64_t sum_T16(struct T16 s);
extern struct T16 make_T16(int64_t a, int64_t b);
extern int64_t sum_T24(struct T24 s);
extern struct T24 make_T24(int64_t a, int64_t b, int64_t c);
extern int64_t sum_T40(struct T40 s);
extern struct T40 make_T40(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e);
extern int64_t sum_TM(struct TM s);
extern struct TM make_TM(int8_t a, int64_t b, int16_t c);
extern int64_t pos_mix(int64_t x, struct T16 p, int64_t y, struct T24 q, int64_t z);
extern int64_t backfill(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, struct T16 p, int64_t g);
extern int64_t straddle7(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, struct T16 p);
extern int64_t small_mix(struct T3 p, struct T6 q, struct T12 r, struct T8 s);
extern int64_t big_two(struct T40 p, struct T40 q, int64_t k);
extern int64_t mem_mixed(struct TM m, int64_t x, struct TM n);
extern struct T16 relay16(struct T16 p);
extern struct T24 relay24(struct T24 p);
extern struct T40 relay40(struct T40 p);
extern struct T12 relay12(struct T12 p);
extern struct T3 relay3(struct T3 p);
extern struct T8 relay8(struct T8 p);
extern struct TM relaym(struct TM p);

int main(void) {
    { struct T1 s = { 5 }; if (sum_T1(s) != 5) return 1; }
    { struct T1 m = make_T1(5); if (m.a != 5) return 2; }
    { struct T3 s = { 1, 2, 3 }; if (sum_T3(s) != 321) return 3; }
    { struct T3 m = make_T3(1, 2, 3); if (m.a != 1 || m.b != 2 || m.c != 3) return 4; }
    { struct T6 s = { 4, 5, 6 }; if (sum_T6(s) != 654) return 5; }
    { struct T6 m = make_T6(4, 5, 6); if (m.a != 4 || m.b != 5 || m.c != 6) return 6; }
    { struct T8 s = { 7, 8 }; if (sum_T8(s) != 87) return 7; }
    { struct T8 m = make_T8(7, 8); if (m.a != 7 || m.b != 8) return 8; }
    { struct T12 s = { 1, 2, 3 }; if (sum_T12(s) != 321) return 9; }
    { struct T12 m = make_T12(1, 2, 3); if (m.a != 1 || m.b != 2 || m.c != 3) return 10; }
    { struct T16 s = { 4, 5 }; if (sum_T16(s) != 54) return 11; }
    { struct T16 m = make_T16(4, 5); if (m.a != 4 || m.b != 5) return 12; }
    { struct T24 s = { 6, 7, 8 }; if (sum_T24(s) != 876) return 13; }
    { struct T24 m = make_T24(6, 7, 8); if (m.a != 6 || m.b != 7 || m.c != 8) return 14; }
    { struct T40 s = { 1, 2, 3, 4, 5 }; if (sum_T40(s) != 54321) return 15; }
    { struct T40 m = make_T40(1, 2, 3, 4, 5); if (m.a != 1 || m.b != 2 || m.c != 3 || m.d != 4 || m.e != 5) return 16; }
    { struct TM s = { 3, 4, 5 }; if (sum_TM(s) != 543) return 17; }
    { struct TM m = make_TM(3, 4, 5); if (m.a != 3 || m.b != 4 || m.c != 5) return 18; }
    { struct T16 p_v = { 2, 3 }; struct T24 q_v = { 5, 6, 7 }; if (pos_mix(1, p_v, 4, q_v, 8) != 87654321) return 19; }
    { struct T16 p_v = { 6, 7 }; if (backfill(1, 2, 3, 4, 5, p_v, 8) != 87654321) return 20; }
    { struct T16 p_v = { 8, 9 }; if (straddle7(1, 2, 3, 4, 5, 6, 7, p_v) != 987654321) return 21; }
    { struct T3 p_v = { 1, 2, 3 }; struct T6 q_v = { 4, 5, 6 }; struct T12 r_v = { 1, 2, 3 }; struct T8 s_v = { 7, 8 }; if (small_mix(p_v, q_v, r_v, s_v) != 299) return 22; }
    { struct T40 p_v = { 1, 2, 3, 4, 5 }; struct T40 q_v = { 6, 7, 8, 9, 1 }; if (big_two(p_v, q_v, 7) != 8371651) return 23; }
    { struct TM m_v = { 3, 4, 5 }; struct TM n_v = { 7, 8, 9 }; if (mem_mixed(m_v, 6, n_v) != 9876543) return 24; }
    { struct T16 in = { 4, 5 }; struct T16 o = relay16(in); if (o.a != 12 || o.b != 11) return 25; }
    { struct T24 in = { 6, 7, 8 }; struct T24 o = relay24(in); if (o.a != 13 || o.b != 14 || o.c != 15) return 26; }
    { struct T40 in = { 1, 2, 3, 4, 5 }; struct T40 o = relay40(in); if (o.a != 5 || o.b != 4 || o.c != 3 || o.d != 2 || o.e != 1) return 27; }
    { struct T12 in = { 1, 2, 3 }; struct T12 o = relay12(in); if (o.a != 2 || o.b != 3 || o.c != 1) return 28; }
    { struct T3 in = { 1, 2, 3 }; struct T3 o = relay3(in); if (o.a != 3 || o.b != 2 || o.c != 1) return 29; }
    { struct T8 in = { 7, 8 }; struct T8 o = relay8(in); if (o.a != 15 || o.b != 14) return 30; }
    { struct TM in = { 3, 4, 5 }; struct TM o = relaym(in); if (o.a != 5 || o.b != 11 || o.c != 3) return 31; }
    return 0;
}
