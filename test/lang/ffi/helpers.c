// C11 helpers linked into the test/lang/run/ffi tests via `//! link: ffi/helpers.c`.
// Every signature uses only scalar types so it is extern-legal in fort.
// D9.8
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

enum { BYTE_MASK = 0xFFU };

// What helper_half divides by: clang-tidy reads a literal in an expression as
// a magic number, and a named constant says what the half is.
static const float HALF_DIVISOR = 2.0F;

// What helper_paint multiplies its enum by, so the answer names the member.
enum { PAINT_SCALE = 10 };

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

// Calls back into a fort function whose signature is extern-legal.
// D9.9
int32_t helper_apply(int32_t (*cb)(int32_t), int32_t x) {
    return cb(x);
}

// Calls back into a fort function through a floating-point signature, so the
// callback's argument and result travel in SSE registers.
// D9.9
double helper_apply_f64(double (*cb)(double), double x) {
    return cb(x);
}

// A binary32 argument and result, which travel in SSE registers of their own
// width: a float passed as a double, or read back as one, gives the wrong
// number rather than a wrong type, which no compiler diagnoses.
// D9.9
float helper_half(float x) {
    return x / HALF_DIVISOR;
}

// Both float widths and an integer in one signature, so the two SSE classes
// and the integer class are assigned together.
// D9.9
double helper_mix(float a, double b, int32_t k) {
    return (double)a + b * (double)k;
}

// Calls back into a fort function through a binary32 signature.
float helper_apply_f32(float (*cb)(float), float x) {
    return cb(x);
}

// Calls back with narrow arguments and a narrow result, which the extension
// attributes normalize on both sides of the boundary.
// D9.9
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
// the direction run/ffi/002 and 005 do not cover: the extension attributes
// normalize both sides, and each value below is read here in a wider type
// so that a truncation or a wrong extension changes the answer.
// D9.9
int64_t helper_narrow_sum(int8_t a, uint8_t b, int16_t c, uint16_t d) {
    return (int64_t)a + (int64_t)b + (int64_t)c + (int64_t)d;
}

// fort `char` is C's `unsigned char` at the boundary, so a byte above 127
// arrives unchanged rather than as a negative number.
// D9.8
uint8_t helper_char_code(unsigned char c) {
    return (uint8_t)c;
}

// `bool` crosses as a single 0 or 1 (module-system.md 8.3).
bool helper_not(bool b) {
    return !b;
}

// A fort enum crosses as `i32`, so C sees a plain integer and the answer
// comes back as the enum it was declared with.
// D9.8
int32_t helper_enum_next(int32_t c) {
    return c + 1;
}

// The C side of run/modules/extern_enum: one symbol that two fort modules
// declare, one spelling the enum parameter `color` and the other
// `shade.color`. The two declarations are identical because they are compared
// as types.
// D9.8, D9.4
int32_t helper_paint(int32_t color) {
    return color * PAINT_SCALE;
}

// Returns to its caller although the fort side declares it `noreturn`, which
// is the one way a program can reach the trap the compiler emits after a call
// to a `noreturn` function. `llvm.trap` is `ud2` on x86-64, so control
// arriving here dies by SIGILL with no message.
// D19.7, D11.4
void helper_returns_anyway(void) {}

// The Mac test reads each C variable argument from its stack slot.
// Linux reads the same types through the System V C ABI.
// D9.8
int64_t helper_tail_mix(int32_t fixed, ...) {
    va_list args;
    va_start(args, fixed);
    int32_t signed_arg = va_arg(args, int32_t);
    uint32_t unsigned_arg = va_arg(args, uint32_t);
    uint64_t wide_arg = va_arg(args, uint64_t);
    double float_arg = va_arg(args, double);
    int32_t* pointer_arg = va_arg(args, int32_t*);
    int32_t (*callback_arg)(int32_t) = va_arg(args, int32_t(*)(int32_t));
    va_end(args);
    return (int64_t)fixed + signed_arg + unsigned_arg + (int64_t)wide_arg + (int64_t)float_arg +
           *pointer_arg + callback_arg(3);
}

// An untyped tail has no fixed parameter type. The caller gives its defaults.
// D4.5, D9.8
int64_t helper_tail_defaults(int32_t fixed, ...) {
    va_list args;
    va_start(args, fixed);
    int32_t small = va_arg(args, int32_t);
    int64_t large = va_arg(args, int64_t);
    double fraction = va_arg(args, double);
    va_end(args);
    return (int64_t)fixed + small + large + (int64_t)fraction;
}

// The nested callback type is extern-legal at each function-pointer level.
// D9.8, D9.9
int32_t helper_tail_nested_callback(int32_t fixed, ...) {
    va_list args;
    va_start(args, fixed);
    int32_t (*direct)(int32_t) = va_arg(args, int32_t(*)(int32_t));
    int32_t (*outer)(int32_t (*)(int32_t)) = va_arg(args, int32_t(*)(int32_t(*)(int32_t)));
    va_end(args);
    return fixed + direct(3) + outer(direct);
}
