#include <stdio.h>
int main(void) {
    printf("%lld\n", (long long)(-(-5)));
    printf("%lld\n", (long long)(-0));
    printf("%lld\n", (long long)(-(3 * 4)));
    return 0;
}
