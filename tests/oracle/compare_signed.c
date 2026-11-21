#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(-1 < 1));
    printf("%lld\n", (long long)(-5 > -10));
    printf("%lld\n", (long long)(0 == 0));
    printf("%lld\n", (long long)(7 != 8));
    printf("%lld\n", (long long)(-1 <= 0));
    printf("%lld\n", (long long)(0 >= -1));
    return 0;
}
