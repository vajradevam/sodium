#include <stdio.h>
long long classify(long long x) {
    switch (x) { case 0: return 100; case 1: return 200; case 2: return 300; default: return 999; }
}
int main(void) {
    printf("%lld\n", classify(0));
    printf("%lld\n", classify(1));
    printf("%lld\n", classify(2));
    printf("%lld\n", classify(7));
    return 0;
}
