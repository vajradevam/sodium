#include <stdio.h>
#include <stdint.h>

static double mean(double a, double b, double c) { return (a + b + c) / 3; }

int main(void) {
    int64_t n = 17;
    double h = (double)n / 4.0;
    printf("%f\n", h);
    printf("%ld\n", (long)(int64_t)h);
    printf("%ld\n", (long)(int64_t)(0.0 - h));
    printf("%f\n", mean(1.0, 2.0, 4.0));
    float acc = 0.0f;
    int64_t i = 0;
    while (i < 100) {
        acc += 0.1f;
        i = i + 1;
    }
    printf("%f\n", (double)acc);
    double d = acc;
    printf("%f\n", d * 2.0);
    double big = 123456789012.0;
    printf("%ld\n", (long)(int64_t)big);
    printf("%ld\n", (long)(int32_t)(big / 1000.0));
    double neg = 0.0 - 2.5;
    printf("%f\n", neg * neg);
    printf("%ld\n", (long)(neg < 0));
    printf("%ld\n", (long)(neg >= h));
    return 0;
}
