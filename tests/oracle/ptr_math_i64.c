#include <stdio.h>
#include <stdint.h>
int main(void) {
    int64_t x = 42;
    int64_t *p = &x, *q = p + 2;
    printf("%lld\n", (long long)(q - p));
    printf("%lld\n", (long long)(*p));
    return 0;
}
