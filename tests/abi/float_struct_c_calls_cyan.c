/* C-interop (X1b): C calls the Cyan functions of float_struct_c_calls_cyan.cyan. */
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

extern FF ff_make(double, double);
extern F2 f2_make(float, float);
extern DI di_make(double, int64_t);
extern ID id_make(int64_t, double);
extern FI fi_make(float, int32_t);
extern F3 f3_make(float, float, float);
extern D1 d1_make(double);
extern S1 s1_make(float);
extern DF df_make(double, float);
extern NN nn_make(float, float, float, float);
extern double ff_sum(FF);
extern double f2_sum(F2);
extern double di_sum(DI);
extern double id_sum(ID);
extern double fi_sum(FI);
extern double f3_sum(F3);
extern double d1_sum(D1);
extern double s1_sum(S1);
extern double nn_sum(NN);
extern double df_sum(DF);
extern double five(FF, FF, FF, FF, FF);
extern double order(int64_t, DI, int64_t, F2, double);
extern F2 f2_swap(F2);
extern FF relay_ff(FF);
extern DI relay_di(DI);
extern ID relay_id(ID);
extern F3 relay_f3(F3);
extern DF relay_df(DF);
extern double drain6(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, DI);
extern double drain8(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, DI);

int main(void) {
    if (ff_sum(ff_make(1.5, 2.5)) != 17.5) return 1;
    if (f2_sum(f2_make(1.5f, 2.5f)) != 17.5) return 2;
    if (di_sum(di_make(1.5, 7)) != 22.0) return 3;
    if (id_sum(id_make(7, 1.5)) != 22.0) return 4;
    if (fi_sum(fi_make(1.5f, 7)) != 22.0) return 5;
    if (f3_sum(f3_make(1.0f, 2.0f, 3.0f)) != 123.0) return 6;
    if (d1_sum(d1_make(1.25)) != 2.5) return 7;
    if (s1_sum(s1_make(1.25f)) != 2.5) return 8;
    if (nn_sum(nn_make(1.0f, 2.0f, 3.0f, 4.0f)) != 1234.0) return 9;
    if (df_sum(df_make(1.5, 2.5f)) != 17.5) return 10;
    if (f2_sum(f2_swap(f2_make(1.0f, 2.0f))) != 21.0) return 11;
    if (ff_sum(relay_ff(ff_make(3.0, 4.0))) != 34.0) return 12;
    if (di_sum(relay_di(di_make(3.0, 4))) != 34.0) return 13;
    if (id_sum(relay_id(id_make(4, 3.0))) != 34.0) return 14;
    if (f3_sum(relay_f3(f3_make(1.0f, 0.5f, 0.25f))) != 105.25) return 15;
    if (df_sum(relay_df(df_make(2.0, 0.5f))) != 20.5) return 16;
    if (five(ff_make(1.0, 0.0), ff_make(2.0, 0.0), ff_make(3.0, 0.0), ff_make(4.0, 0.0), ff_make(5.0, 0.5)) != 54321.5) return 17;
    if (order(1, di_make(2.0, 3), 4, f2_make(5.0f, 6.0f), 7.0) != 7654321.0) return 18;
    if (drain6(1, 1, 1, 1, 1, 1, di_make(1.5, 7)) != 43.0) return 20;
    if (drain8(1, 1, 1, 1, 1, 1, 1, 1, di_make(1.5, 7)) != 58.0) return 21;
    return 0;
}
