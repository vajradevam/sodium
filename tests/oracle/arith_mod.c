#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(100 % 3));
    printf("%lld\n", (long long)(-7 % 2));
    printf("%lld\n", (long long)(7 % -2));
    printf("%lld\n", (long long)(-7 % -2));
    return 0;
}
