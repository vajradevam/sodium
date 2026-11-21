#include <stdio.h>
#include <stdint.h>
int main(void) {
    int8_t a = 100;  printf("%lld\n", (long long)a);
    a = (int8_t)200; printf("%lld\n", (long long)a);
    a = (int8_t)-129; printf("%lld\n", (long long)a);
    return 0;
}
