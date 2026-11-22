/* C-interop (D-038): the C implementations called from float_cyan_calls_c.cyan. */
#include <stdint.h>

double c_add(double a, double b) { return a + b; }
float c_scale(float x, double k) { return (float)(x * k); }
double c_mix(int64_t a, double x, int64_t b, float y, int64_t c) {
    return (double)a + x * 10.0 + (double)b * 100.0 + (double)y * 1000.0 + (double)c * 10000.0;
}
double c_nine(double a, double b, double c, double d, double e, double f, double g, double h, double i) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9;
}
double c_many(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int64_t h,
              double x, double y, double z) {
    return (double)(a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8) + x + y * 2 + z * 3;
}
double c_twelve(double d0, double d1, double d2, double d3, double d4, double d5, double d6, double d7,
                double d8, double d9, double d10, double d11, int64_t n, float s) {
    return d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8 * 2 + d9 * 3 + d10 * 4 + d11 * 5 + (double)n + (double)s;
}
int64_t c_less(double a, double b) { return a < b; }
int64_t c_trunc(double x) { return (int64_t)x; }
double c_from_int(int64_t x) { return (double)x; }
float c_narrow(double x) { return (float)x; }
double c_widen(float x) { return (double)x; }
