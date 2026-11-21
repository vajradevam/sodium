#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(-9223372036854775807 % 10LL));
    printf("%lld\n", (long long)(0 % 5LL));
    printf("%lld\n", (long long)(17 % -5LL));
    printf("%lld\n", (long long)(1000000000 % 7LL));
    return 0;
}
