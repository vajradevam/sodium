#include <stdio.h>
int main(void) {
    long long total = 0;
    for (long long i = 1; i <= 100; i = i + 1) total += i;
    printf("%lld\n", total);
    long long n = 0;
    while (n < 5) { printf("%lld\n", n); n = n + 1; }
    return 0;
}
