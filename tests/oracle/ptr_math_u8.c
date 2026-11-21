#include <stdio.h>
#include <stdint.h>
int main(void) {
    uint8_t z = 7;
    uint8_t *p = &z, *q = p + 3;
    printf("%lld\n", (long long)(q - p));
    return 0;
}
