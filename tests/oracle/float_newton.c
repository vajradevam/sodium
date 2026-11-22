#include <stdio.h>
#include <stdint.h>

static double sqrt_newton(double x) {
    double g = x / 2.0;
    int64_t i = 0;
    while (i < 30) {
        g = (g + x / g) / 2.0;
        i = i + 1;
    }
    return g;
}

int main(void) {
    printf("%f\n", sqrt_newton(2.0));
    printf("%f\n", sqrt_newton(144.0));
    printf("%f\n", sqrt_newton(0.0001));
    double s = 0.0;
    int64_t k = 1;
    while (k <= 1000) {
        s += 1.0 / (double)(k * k);
        k = k + 1;
    }
    printf("%f\n", s);
    return 0;
}
