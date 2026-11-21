#include <stdio.h>
int main(void) {
    long long x = 1024; x >>= 3; printf("%lld\n", x);
    long long y = -32;  y >>= 2; printf("%lld\n", y);
    long long z = 3;    z <<= 4; printf("%lld\n", z);
    return 0;
}
