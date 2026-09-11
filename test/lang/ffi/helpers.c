// C11 helpers linked into the test/lang/run/ffi tests via `//! link: ffi/helpers.c`.
// Every signature uses only scalar types so it is extern-legal in fort (D9.8).
#include <stdbool.h>
#include <stdint.h>

enum { BYTE_MASK = 0xFFU };

int32_t helper_add(int32_t a, int32_t b) {
    return a + b;
}

double helper_scale(double x, int32_t k) {
    return x * (double)k;
}

uint8_t helper_low_byte(uint32_t x) {
    return (uint8_t)(x & BYTE_MASK);
}

bool helper_is_even(int64_t n) {
    return n % 2 == 0;
}

// Calls back into a fort function whose signature is extern-legal (D9.9).
int32_t helper_apply(int32_t (*cb)(int32_t), int32_t x) {
    return cb(x);
}

// Calls back with narrow arguments and a narrow result, which the extension
// attributes of D9.9 normalize on both sides of the boundary.
uint8_t helper_apply_narrow(uint8_t (*cb)(int8_t, uint16_t), int8_t a, uint16_t b) {
    return cb(a, b);
}

int64_t helper_sum_to(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 1; i <= n; i++) {
        sum += i;
    }
    return sum;
}

// Narrow parameters and a narrow result travelling from fort into C, which is
// the direction run/ffi/002 and 005 do not cover: the extension attributes of
// D9.9 normalize both sides, and each value below is read here in a wider type
// so that a truncation or a wrong extension changes the answer.
int64_t helper_narrow_sum(int8_t a, uint8_t b, int16_t c, uint16_t d) {
    return (int64_t)a + (int64_t)b + (int64_t)c + (int64_t)d;
}

// fort `char` is C's `unsigned char` at the boundary (D9.8), so a byte above
// 127 arrives unchanged rather than as a negative number.
uint8_t helper_char_code(unsigned char c) {
    return (uint8_t)c;
}

// `bool` crosses as a single 0 or 1 (module-system.md 8.3).
bool helper_not(bool b) {
    return !b;
}

// A fort enum crosses as `i32` (D9.8), so C sees a plain integer and the
// answer comes back as the enum it was declared with.
int32_t helper_enum_next(int32_t c) {
    return c + 1;
}
