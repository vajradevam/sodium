#include <stdio.h>
#include <stdint.h>
int main(void) {
    uint8_t a = 255; printf("%lld\n", (long long)a);
    a = a + 1;       printf("%lld\n", (long long)a);
    a = (uint8_t)-1; printf("%lld\n", (long long)a);
    return 0;
}
