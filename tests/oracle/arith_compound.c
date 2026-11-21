#include <stdio.h>
int main(void) {
    long long x = 10;
    x += 5; printf("%lld\n", x);
    x -= 3; printf("%lld\n", x);
    x *= 2; printf("%lld\n", x);
    x /= 4; printf("%lld\n", x);
    return 0;
}
