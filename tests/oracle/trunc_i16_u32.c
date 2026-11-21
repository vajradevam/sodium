#include <stdio.h>
#include <stdint.h>
int main(void) {
    int16_t s = 32767; s = (int16_t)(s + 1); printf("%lld\n", (long long)s);
    uint32_t u = 4294967295U; printf("%lld\n", (long long)u);
    u = u + 1; printf("%lld\n", (long long)u);
    return 0;
}
