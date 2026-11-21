#include <stdio.h>
long long fact(long long n) { return n <= 1 ? 1 : n * fact(n-1); }
int main(void) {
    printf("%lld\n", fact(10));
    printf("%lld\n", fact(20));
    return 0;
}
