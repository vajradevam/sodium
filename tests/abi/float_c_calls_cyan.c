/* C-interop (D-038): C calls Cyan functions defined in float_c_calls_cyan.cyan. */
#include <stdint.h>

extern double add(double a, double b);
extern float scale(float x, double k);
extern double mix(int64_t a, double x, int64_t b, float y, int64_t c);
extern double nine(double a, double b, double c, double d, double e, double f, double g, double h, double i);
extern double many(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int64_t h,
                   double x, double y, double z);
extern double twelve(double d0, double d1, double d2, double d3, double d4, double d5, double d6, double d7,
                     double d8, double d9, double d10, double d11, int64_t n, float s);
extern int64_t less(double a, double b);
extern int64_t trunc_to_int(double x);
extern double from_int(int64_t x);
extern float narrow(double x);
extern double widen(float x);
extern double relay(double x);

int main(void) {
    if (add(1.5, 2.25) != 3.75) return 1;
    if (scale(1.5f, 2.0) != 3.0f) return 2;
    if (mix(1, 2.0, 3, 4.0f, 5) != 54321.0) return 3;
    if (nine(1, 1, 1, 1, 1, 1, 1, 1, 1) != 45.0) return 4;
    if (many(1, 1, 1, 1, 1, 1, 1, 1, 1.0, 1.0, 1.0) != 42.0) return 5;
    if (twelve(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 0.5f) != 122.5) return 6;
    if (less(1.0, 2.0) != 1) return 7;
    if (less(2.0, 1.0) != 0) return 8;
    if (trunc_to_int(-7.9) != -7) return 9;
    if (from_int(-3) != -3.0) return 10;
    if (narrow(0.1) != 0.1f) return 11;
    if (widen(0.1f) == 0.1) return 12;
    if (widen(0.5f) != 0.5) return 13;
    if (relay(2.5) != 7.0) return 14;
    return 0;
}
