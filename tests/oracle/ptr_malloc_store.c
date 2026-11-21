#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(void) {
    int64_t *p = malloc(32 * sizeof(int64_t));
    p[0] = 11; p[1] = 22;
    printf("%lld\n", (long long)p[0]);
    printf("%lld\n", (long long)p[1]);
    free(p);
    return 0;
}
