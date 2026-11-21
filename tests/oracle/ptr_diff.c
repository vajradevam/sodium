#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(void) {
    int64_t *p = malloc(64 * sizeof(int64_t));
    int64_t *q = p + 5;
    printf("%lld\n", (long long)(q - p));
    printf("%lld\n", (long long)(p - q));
    free(p);
    return 0;
}
