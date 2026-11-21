#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(-9223372036854775807 / 3LL));
    printf("%lld\n", (long long)(9223372036854775807 / 3LL));
    printf("%lld\n", (long long)(0 / 7LL));
    printf("%lld\n", (long long)(7 / 7LL));
    return 0;
}
