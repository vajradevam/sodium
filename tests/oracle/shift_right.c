#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(1024 >> 2LL));
    printf("%lld\n", (long long)(-8 >> 1LL));
    printf("%lld\n", (long long)(-17 >> 2LL));
    printf("%lld\n", (long long)(255 >> 4LL));
    return 0;
}
