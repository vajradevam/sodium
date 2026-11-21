#include <stdio.h>
long long fib(long long n) { return n <= 1 ? n : fib(n-1) + fib(n-2); }
int main(void) {
    printf("%lld\n", fib(10));
    printf("%lld\n", fib(15));
    return 0;
}
