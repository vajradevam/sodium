#include <stdio.h>
long long add(long long a, long long b) { return a + b; }
long long mul(long long a, long long b) { return a * b; }
long long compose(long long x) { return add(mul(x, 2), 3); }
int main(void) {
    printf("%lld\n", compose(5));
    printf("%lld\n", add(compose(7), compose(1)));
    return 0;
}
