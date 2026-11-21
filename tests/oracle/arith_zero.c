#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(0));
    printf("%lld\n", (long long)(0 + 0));
    printf("%lld\n", (long long)(7 - 7));
    printf("%lld\n", (long long)(0 * 123));
    return 0;
}
