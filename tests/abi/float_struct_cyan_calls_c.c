/* C-interop (X1b): C definitions called by float_struct_cyan_calls_c.cyan. */
#include <stdint.h>
typedef struct { double a; double b; } FF;
typedef struct { float a; float b; } F2;
typedef struct { double a; int64_t n; } DI;
typedef struct { int64_t n; double a; } ID;
typedef struct { float a; int32_t n; } FI;
typedef struct { float a; float b; float c; } F3;
typedef struct { double a; } D1;
typedef struct { float a; } S1;
typedef struct { F2 p; F2 q; } NN;
typedef struct { double a; float b; } DF;

FF c_ff_make(double a, double b) { FF r; r.a = a; r.b = b; return r; }
F2 c_f2_make(float a, float b) { F2 r; r.a = a; r.b = b; return r; }
DI c_di_make(double a, int64_t n) { DI r; r.a = a; r.n = n; return r; }
ID c_id_make(int64_t n, double a) { ID r; r.n = n; r.a = a; return r; }
FI c_fi_make(float a, int32_t n) { FI r; r.a = a; r.n = n; return r; }
F3 c_f3_make(float a, float b, float c) { F3 r; r.a = a; r.b = b; r.c = c; return r; }
D1 c_d1_make(double a) { D1 r; r.a = a; return r; }
S1 c_s1_make(float a) { S1 r; r.a = a; return r; }
DF c_df_make(double a, float b) { DF r; r.a = a; r.b = b; return r; }
NN c_nn_make(float a, float b, float c, float d) { NN r; r.p.a = a; r.p.b = b; r.q.a = c; r.q.b = d; return r; }
double c_ff_sum(FF p) { return p.a * 10.0 + p.b; }
double c_f2_sum(F2 p) { return p.a * 10.0 + p.b; }
double c_di_sum(DI p) { return p.a * 10.0 + p.n; }
double c_id_sum(ID p) { return p.a * 10.0 + p.n; }
double c_fi_sum(FI p) { return p.a * 10.0 + p.n; }
double c_f3_sum(F3 p) { return p.a * 100.0 + p.b * 10.0 + p.c; }
double c_d1_sum(D1 p) { return p.a * 2.0; }
double c_s1_sum(S1 p) { return p.a * 2.0; }
double c_nn_sum(NN p) { return p.p.a * 1000.0 + p.p.b * 100.0 + p.q.a * 10.0 + p.q.b; }
double c_df_sum(DF p) { return p.a * 10.0 + p.b; }
double c_five(FF a, FF b, FF c, FF d, FF e) { return a.a + b.a * 10.0 + c.a * 100.0 + d.a * 1000.0 + e.a * 10000.0 + e.b; }
double c_order(int64_t n, DI p, int64_t m, F2 q, double k) { return n * 1.0 + p.a * 10.0 + p.n * 100.0 + m * 1000.0 + q.a * 10000.0 + q.b * 100000.0 + k * 1000000.0; }
F2 c_f2_swap(F2 p) { F2 r; r.a = p.b; r.b = p.a; return r; }
FF c_relay_ff(FF p) { FF r; r.a = p.a; r.b = p.b; return r; }
DI c_relay_di(DI p) { DI r; r.a = p.a; r.n = p.n; return r; }
ID c_relay_id(ID p) { ID r; r.n = p.n; r.a = p.a; return r; }
F3 c_relay_f3(F3 p) { F3 r; r.a = p.a; r.b = p.b; r.c = p.c; return r; }
DF c_relay_df(DF p) { DF r; r.a = p.a; r.b = p.b; return r; }
double c_drain6(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, DI p) { return p.a * 10.0 + p.n + a + b * 2.0 + c * 3.0 + d * 4.0 + e * 5.0 + f * 6.0; }
double c_drain8(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int64_t h, DI p) { return p.a * 10.0 + p.n + a + b * 2.0 + c * 3.0 + d * 4.0 + e * 5.0 + f * 6.0 + g * 7.0 + h * 8.0; }
