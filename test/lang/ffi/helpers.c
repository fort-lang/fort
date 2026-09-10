/* C11 helpers linked into test/lang/run/ffi/002_helpers.ft via `//! link: ffi/helpers.c`.
 * Every signature uses only scalar types so it is extern-legal in fort (D9.8). */
#include <stdbool.h>
#include <stdint.h>

int32_t helper_add(int32_t a, int32_t b) {
    return a + b;
}

double helper_scale(double x, int32_t k) {
    return x * (double)k;
}

uint8_t helper_low_byte(uint32_t x) {
    return (uint8_t)(x & 0xFFu);
}

bool helper_is_even(int64_t n) {
    return n % 2 == 0;
}

/* Calls back into a fort function whose signature is extern-legal (D9.9). */
int32_t helper_apply(int32_t (*cb)(int32_t), int32_t x) {
    return cb(x);
}

int64_t helper_sum_to(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 1; i <= n; i++) {
        sum += i;
    }
    return sum;
}
