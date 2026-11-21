#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(1LL << 10));
    printf("%lld\n", (long long)(3LL << 5));
    printf("%lld\n", (long long)(1LL << 40));
    printf("%lld\n", (long long)(1LL << 62));
    return 0;
}
