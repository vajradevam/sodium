#include <stdio.h>
long long gcd(long long a, long long b) { while (b) { long long t = a % b; a = b; b = t; } return a; }
int main(void) {
    printf("%lld\n", gcd(1071, 462));
    printf("%lld\n", gcd(48, 18));
    return 0;
}
