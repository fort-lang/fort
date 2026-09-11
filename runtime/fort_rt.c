// The fort C runtime (toolchain.md 5).
//
// Every entry point of toolchain.md 5.1. All layout is done by hand, without
// stdio, so that the bytes written are exactly those of D11.4 and D11.7 and
// the code carries over to a fort rewrite unchanged; the two float printers
// are the exception, and use snprintf and strtod only to obtain the shortest
// digit string that round-trips (D18).
#include "fort_rt.h"

#include <errno.h>
#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---- descriptors and buffers (D11.5, toolchain.md 5.3) -----------------------

enum {
    STDOUT_FD = 1,
    STDERR_FD = 2,
    // Bytes held back per descriptor before a write.
    BUFFER_SIZE = 8192,
    // Descriptors buffered at once: stdout plus this many others. A program
    // that spreads output over more descriptors than this evicts the oldest
    // buffer, which is flushed and reassigned (toolchain.md 5.3 leaves the
    // buffer size and count to the runtime).
    BUFFER_POOL_SIZE = 8,
    // Exit statuses are truncated to a byte (D11.6).
    EXIT_STATUS_MASK = 0xFF,
    DECIMAL_BASE = 10,
    HEX_BASE = 16,
    // Longest decimal rendering: -9223372036854775808 or 18446744073709551615.
    DECIMAL_MAX = 20,
    // Longest pointer rendering: 0x followed by 16 hex digits.
    HEX_MAX = 18,
    // Significant digits that always round-trip: 17 for f64, 9 for f32.
    FLOAT_DIGITS_F64 = 17,
    FLOAT_DIGITS_F32 = 9,
    // Longest text printf produces below, a magnitude only: 1.<16 digits>e+308
    // and a NUL.
    FLOAT_SCRATCH_MAX = 32,
    // Longest float rendering: a sign, 0.000 and 17 digits in fixed form, a
    // sign, 17 digits, a point and e+308 in exponent form.
    FLOAT_TEXT_MAX = 40,
    // Exponent form below 1e-4 and at 1e17 and above, fixed form between them
    // (D11.7), each decided on the exponent of the leading digit (D18.3).
    FLOAT_EXP_LOW = -4,
    FLOAT_EXP_HIGH = 17,
    // The exponent is written with a sign and at least two digits (D11.7).
    FLOAT_EXP_MIN_DIGITS = 2,
    // The sign bit of an IEEE 754 binary64 and binary32 (D6.12).
    F64_SIGN_SHIFT = 63,
    F32_SIGN_SHIFT = 31,
};

// One descriptor's pending bytes. fd is -1 while the slot is free.
struct fort_rt_buffer {
    int32_t fd;
    uint64_t len;
    uint8_t data[BUFFER_SIZE];
};

// Slot 0 is stdout, always. The others are handed out in order and evicted
// round-robin once every slot is taken.
static struct fort_rt_buffer buffers[1 + BUFFER_POOL_SIZE];
static bool buffers_ready = false;
static uint64_t next_eviction = 1;

// The line of a failure is assembled here so that it reaches stderr in one
// write when it fits.
static struct fort_rt_buffer message;

static void buffers_init(void) {
    if (buffers_ready) {
        return;
    }
    buffers[0].fd = STDOUT_FD;
    for (uint64_t i = 1; i < 1 + BUFFER_POOL_SIZE; i++) {
        buffers[i].fd = -1;
    }
    message.fd = STDERR_FD;
    buffers_ready = true;
}

// Writes n bytes to fd, retrying after partial writes and EINTR. A write error
// drops the rest (toolchain.md 5.3).
static void write_all(int32_t fd, const uint8_t* p, uint64_t n) {
    while (n > 0) {
        const ssize_t got = write(fd, p, (size_t)n);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        p += got;
        n -= (uint64_t)got;
    }
}

static void buffer_flush(struct fort_rt_buffer* b) {
    if (b->len > 0) {
        const uint64_t len = b->len;
        b->len = 0;
        write_all(b->fd, b->data, len);
    }
}

// Appends n bytes, flushing first when they do not fit and bypassing the
// buffer entirely when they never would.
static void buffer_append(struct fort_rt_buffer* b, const uint8_t* p, uint64_t n) {
    if (n > BUFFER_SIZE - b->len) {
        buffer_flush(b);
    }
    if (n > BUFFER_SIZE) {
        write_all(b->fd, p, n);
        return;
    }
    (void)memcpy(b->data + b->len, p, (size_t)n);
    b->len += n;
}

// The buffer of a buffered descriptor, assigning a slot when it has none;
// NULL for stderr, which is unbuffered, and for an invalid (negative) fd,
// whose writes fail and are dropped.
static struct fort_rt_buffer* buffer_for(int32_t fd) {
    if (fd == STDERR_FD || fd < 0) {
        return NULL;
    }
    buffers_init();
    uint64_t free_slot = 0;
    for (uint64_t i = 0; i < 1 + BUFFER_POOL_SIZE; i++) {
        if (buffers[i].fd == fd) {
            return &buffers[i];
        }
        if (buffers[i].fd < 0 && free_slot == 0) {
            free_slot = i;
        }
    }
    if (free_slot == 0) {
        free_slot = next_eviction;
        next_eviction = 1 + (next_eviction % BUFFER_POOL_SIZE);
        buffer_flush(&buffers[free_slot]);
    }
    buffers[free_slot].fd = fd;
    buffers[free_slot].len = 0;
    return &buffers[free_slot];
}

// Sends n bytes to fd through its buffer, or straight out for stderr.
static void emit(int32_t fd, const uint8_t* p, uint64_t n) {
    struct fort_rt_buffer* b = buffer_for(fd);
    if (b == NULL) {
        write_all(fd, p, n);
    } else {
        buffer_append(b, p, n);
    }
}

static void emit_cstr(int32_t fd, const char* s) {
    emit(fd, (const uint8_t*)s, (uint64_t)strlen(s));
}

void fort_rt_flush(int32_t fd) {
    buffers_init();
    for (uint64_t i = 0; i < 1 + BUFFER_POOL_SIZE; i++) {
        if (buffers[i].fd == fd) {
            buffer_flush(&buffers[i]);
            return;
        }
    }
}

void fort_rt_flush_all(void) {
    buffers_init();
    for (uint64_t i = 0; i < 1 + BUFFER_POOL_SIZE; i++) {
        if (buffers[i].fd >= 0) {
            buffer_flush(&buffers[i]);
        }
    }
}

// ---- formatting (D11.7) --------------------------------------------------------

static const char HEX_DIGITS[] = "0123456789abcdef";

// Writes the decimal digits of v at the end of out and returns where they
// start; out holds DECIMAL_MAX bytes.
static uint64_t format_u64(uint8_t out[DECIMAL_MAX], uint64_t v) {
    uint64_t start = DECIMAL_MAX;
    do {
        start--;
        out[start] = (uint8_t)('0' + (v % DECIMAL_BASE));
        v /= DECIMAL_BASE;
    } while (v > 0);
    return start;
}

// As format_u64, with a leading minus for a negative v. The magnitude of the
// minimum is taken in unsigned arithmetic, where it is representable.
static uint64_t format_i64(uint8_t out[DECIMAL_MAX], int64_t v) {
    if (v >= 0) {
        return format_u64(out, (uint64_t)v);
    }
    const uint64_t magnitude = (uint64_t)0 - (uint64_t)v;
    const uint64_t start = format_u64(out, magnitude) - 1;
    out[start] = '-';
    return start;
}

// 0x followed by the lowercase hex digits of v, no leading zeros, 0x0 for
// zero; out holds HEX_MAX bytes. Returns where the text starts.
static uint64_t format_hex(uint8_t out[HEX_MAX], uint64_t v) {
    uint64_t start = HEX_MAX;
    do {
        start--;
        out[start] = (uint8_t)HEX_DIGITS[v % HEX_BASE];
        v /= HEX_BASE;
    } while (v > 0);
    out[--start] = 'x';
    out[--start] = '0';
    return start;
}

static void emit_u64(int32_t fd, uint64_t v) {
    uint8_t out[DECIMAL_MAX];
    const uint64_t start = format_u64(out, v);
    emit(fd, out + start, DECIMAL_MAX - start);
}

static void emit_i64(int32_t fd, int64_t v) {
    uint8_t out[DECIMAL_MAX];
    const uint64_t start = format_i64(out, v);
    emit(fd, out + start, DECIMAL_MAX - start);
}

void fort_rt_print_i64(int32_t fd, int64_t v) {
    emit_i64(fd, v);
}

void fort_rt_print_u64(int32_t fd, uint64_t v) {
    emit_u64(fd, v);
}

// ---- floats: the shortest digits that read back, then D11.7's layout ----------

// One float as a decimal: the significant digits of the shortest text that
// reads back as the value, most significant first, and the power of ten the
// first of them stands for, so that the value is digits[0].digits[1..] times
// ten to the exponent. The sign is kept apart, since -0.0 has no digit to
// carry it.
struct float_decimal {
    uint64_t digit_count;
    int32_t exponent;
    uint8_t digits[FLOAT_DIGITS_F64];
};

// Reads printf's %e text back into digits and an exponent. The text is
// d.ddde+xx, without the point at precision 0, and never carries a sign here:
// only the magnitude is printed. A text with no digit at all, which is what a
// failed printf leaves behind, scans as the decimal zero, so every caller gets
// a decimal it can lay out.
static void float_scan(const char* s, struct float_decimal* d) {
    d->digit_count = 0;
    d->exponent = 0;
    uint64_t i = 0;
    while (s[i] != 'e' && s[i] != '\0') {
        if (s[i] >= '0' && s[i] <= '9' && d->digit_count < FLOAT_DIGITS_F64) {
            d->digits[d->digit_count] = (uint8_t)(s[i] - '0');
            d->digit_count++;
        }
        i++;
    }
    if (d->digit_count == 0) {
        d->digits[0] = 0;
        d->digit_count = 1;
    }
    if (s[i] != 'e') {
        return;
    }
    i++;
    const bool negative = s[i] == '-';
    if (s[i] == '+' || s[i] == '-') {
        i++;
    }
    int32_t exponent = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        exponent = (exponent * DECIMAL_BASE) + (s[i] - '0');
        i++;
    }
    d->exponent = negative ? -exponent : exponent;
}

// Appends the exponent as e, a sign and at least two digits (D11.7).
static uint64_t float_put_exponent(uint8_t* out, uint64_t n, int32_t exponent) {
    out[n++] = 'e';
    out[n++] = exponent < 0 ? '-' : '+';
    const uint64_t magnitude = exponent < 0 ? (uint64_t)0 - (uint64_t)exponent : (uint64_t)exponent;
    uint8_t text[DECIMAL_MAX];
    uint64_t start = format_u64(text, magnitude);
    while (DECIMAL_MAX - start < FLOAT_EXP_MIN_DIGITS) {
        start--;
        text[start] = '0';
    }
    while (start < DECIMAL_MAX) {
        out[n++] = text[start];
        start++;
    }
    return n;
}

// Lays the digits out per D11.7: exponent form below 1e-4 and at 1e17 and
// above, fixed form in between, with .0 appended when the fixed text has
// neither a point nor an exponent. out holds FLOAT_TEXT_MAX bytes; the
// returned length is how many were written.
static uint64_t float_layout(bool negative, const struct float_decimal* d, uint8_t* out) {
    uint64_t n = 0;
    if (negative) {
        out[n++] = '-';
    }
    if (d->exponent < FLOAT_EXP_LOW || d->exponent >= FLOAT_EXP_HIGH) {
        out[n++] = (uint8_t)('0' + d->digits[0]);
        if (d->digit_count > 1) {
            out[n++] = '.';
            for (uint64_t i = 1; i < d->digit_count; i++) {
                out[n++] = (uint8_t)('0' + d->digits[i]);
            }
        }
        return float_put_exponent(out, n, d->exponent);
    }
    if (d->exponent < 0) {
        out[n++] = '0';
        out[n++] = '.';
        for (int32_t i = -1; i > d->exponent; i--) {
            out[n++] = '0';
        }
        for (uint64_t i = 0; i < d->digit_count; i++) {
            out[n++] = (uint8_t)('0' + d->digits[i]);
        }
        return n;
    }
    const uint64_t integer_digits = (uint64_t)d->exponent + 1;
    for (uint64_t i = 0; i < integer_digits; i++) {
        out[n++] = i < d->digit_count ? (uint8_t)('0' + d->digits[i]) : (uint8_t)'0';
    }
    out[n++] = '.';
    if (d->digit_count <= integer_digits) {
        out[n++] = '0';
        return n;
    }
    for (uint64_t i = integer_digits; i < d->digit_count; i++) {
        out[n++] = (uint8_t)('0' + d->digits[i]);
    }
    return n;
}

// The next decimal of the same length upward: 1.24 after 1.23, and a power of
// ten after all nines, where the digits collapse to a single one. That last
// case is here for totality and no value can be printed by it: digits that are
// all nines mean the value is within a hundredth of the power of ten above, so
// that power of ten was the nearest one-digit decimal and was tried, and
// rejected, at the very first length.
static void float_decimal_step_up(struct float_decimal* d) {
    uint64_t i = d->digit_count;
    while (i > 0) {
        i--;
        if (d->digits[i] < DECIMAL_BASE - 1) {
            d->digits[i]++;
            return;
        }
        d->digits[i] = 0;
    }
    d->digits[0] = 1;
    d->digit_count = 1;
    d->exponent++;
}

// The decimal as a NUL-terminated text, laid out as it would print; buf holds
// FLOAT_TEXT_MAX + 1 bytes.
static void float_decimal_text(const struct float_decimal* d, char* buf) {
    const uint64_t n = float_layout(false, d, (uint8_t*)buf);
    buf[n] = '\0';
}

// Whether the decimal reads back as v. strtod must be correctly rounded, down
// to the subnormals, where it also reports ERANGE (D18.2); only the value it
// returns matters here, so errno is left alone.
static bool float_reads_back_f64(const struct float_decimal* d, double v) {
    char text[FLOAT_TEXT_MAX + 1];
    float_decimal_text(d, text);
    return strtod(text, NULL) == v;
}

static bool float_reads_back_f32(const struct float_decimal* d, float v) {
    char text[FLOAT_TEXT_MAX + 1];
    float_decimal_text(d, text);
    return strtof(text, NULL) == v;
}

// The shortest decimal that reads back as v in f64 and, of the strings of that
// length that read back as it, the one nearest v (D11.7, D18.2; toolchain.md
// 5.1 describes this search): printf is asked for one significant digit, then
// two, and so on, and the first length that reads back wins; 17 digits always
// do, so the search ends. printf rounds correctly, so what it returns is the
// nearest decimal of that length; when that one misses, the neighbour above it
// is tried, because the values that read back as a normal power of two above
// the minimum normal reach half an ulp above it and only a quarter below (the
// binade below it is coarser), so the nearest string can fall short of the
// interval while the next one up lands inside it. Everywhere else the interval
// is symmetric. It never leans the other way, the gap below a float never
// being wider than the gap above, so the neighbour below can never be the only
// string of its length that reads back.
//
// The search starts at one digit rather than at the 15 a %g-style probe would
// use: the shortest answer is what D11.7 asks for at every length, and the
// values programs print are usually short. A program that changes LC_NUMERIC
// through the FFI degrades this and nothing else: printf then writes another
// decimal point, no candidate reads back, and the value prints with all 17
// digits, in D11.7's layout as always. v is a finite magnitude, never negative
// and never NaN.
static void float_decimal_f64(double v, struct float_decimal* d) {
    char scratch[FLOAT_SCRATCH_MAX];
    scratch[0] = '\0';
    for (int32_t precision = 0; precision < FLOAT_DIGITS_F64; precision++) {
        // a printf that fails leaves the text empty, which scans as zero
        if (snprintf(scratch, sizeof scratch, "%.*e", (int)precision, v) < 0) {
            scratch[0] = '\0';
        }
        float_scan(scratch, d);
        if (precision == FLOAT_DIGITS_F64 - 1 || float_reads_back_f64(d, v)) {
            return;
        }
        struct float_decimal up = *d;
        float_decimal_step_up(&up);
        if (float_reads_back_f64(&up, v)) {
            *d = up;
            return;
        }
    }
}

// The same search in f32, where 9 digits always read back and strtof decides
// (D11.7: the shortest decimal that round-trips in the argument's own type).
// The magnitude widens to double for printf without changing value.
static void float_decimal_f32(float v, struct float_decimal* d) {
    char scratch[FLOAT_SCRATCH_MAX];
    scratch[0] = '\0';
    for (int32_t precision = 0; precision < FLOAT_DIGITS_F32; precision++) {
        if (snprintf(scratch, sizeof scratch, "%.*e", (int)precision, (double)v) < 0) {
            scratch[0] = '\0';
        }
        float_scan(scratch, d);
        if (precision == FLOAT_DIGITS_F32 - 1 || float_reads_back_f32(d, v)) {
            return;
        }
        struct float_decimal up = *d;
        float_decimal_step_up(&up);
        if (float_reads_back_f32(&up, v)) {
            *d = up;
            return;
        }
    }
}

// The sign bit of v: -0.0 == 0.0 (D6.12), so a comparison cannot tell the two
// apart and the bits decide. Both targets are IEEE 754 (D6.12).
static bool f64_negative(double v) {
    uint64_t bits = 0;
    (void)memcpy(&bits, &v, sizeof v);
    return (bits >> F64_SIGN_SHIFT) != 0;
}

static bool f32_negative(float v) {
    uint32_t bits = 0;
    (void)memcpy(&bits, &v, sizeof v);
    return (bits >> F32_SIGN_SHIFT) != 0;
}

static void emit_float(int32_t fd, bool negative, const struct float_decimal* d) {
    uint8_t out[FLOAT_TEXT_MAX];
    const uint64_t n = float_layout(negative, d, out);
    emit(fd, out, n);
}

// NaN prints as nan whatever its sign bit, since D11.7 lists inf, -inf and nan
// and no -nan; every finite double is at most DBL_MAX in magnitude, so a
// larger one is an infinity.
void fort_rt_print_f64(int32_t fd, double v) {
    if (v != v) {
        emit_cstr(fd, "nan");
        return;
    }
    const bool negative = f64_negative(v);
    if (v > DBL_MAX || v < -DBL_MAX) {
        emit_cstr(fd, negative ? "-inf" : "inf");
        return;
    }
    struct float_decimal d;
    float_decimal_f64(negative ? -v : v, &d);
    emit_float(fd, negative, &d);
}

void fort_rt_print_f32(int32_t fd, float v) {
    if (v != v) {
        emit_cstr(fd, "nan");
        return;
    }
    const bool negative = f32_negative(v);
    if (v > FLT_MAX || v < -FLT_MAX) {
        emit_cstr(fd, negative ? "-inf" : "inf");
        return;
    }
    struct float_decimal d;
    float_decimal_f32(negative ? -v : v, &d);
    emit_float(fd, negative, &d);
}

void fort_rt_print_bool(int32_t fd, uint8_t v) {
    emit_cstr(fd, v != 0 ? "true" : "false");
}

void fort_rt_print_char(int32_t fd, uint8_t c) {
    emit(fd, &c, 1);
}

void fort_rt_print_ptr(int32_t fd, const void* p) {
    uint8_t out[HEX_MAX];
    const uint64_t start = format_hex(out, (uint64_t)(uintptr_t)p);
    emit(fd, out + start, HEX_MAX - start);
}

void fort_rt_print_str(int32_t fd, const char* ptr, uint64_t len) {
    if (len > 0) {
        emit(fd, (const uint8_t*)ptr, len);
    }
}

void fort_rt_print_enum(int32_t fd, int32_t v, const struct fort_rt_enum_member* m, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        if (m[i].value == v) {
            emit_cstr(fd, m[i].name);
            return;
        }
    }
    emit_i64(fd, v);
}

// ---- failures (D11.4, toolchain.md 5.2) ---------------------------------------

static void message_str(const char* s) {
    buffer_append(&message, (const uint8_t*)s, (uint64_t)strlen(s));
}

static void message_u64(uint64_t v) {
    uint8_t out[DECIMAL_MAX];
    const uint64_t start = format_u64(out, v);
    buffer_append(&message, out + start, DECIMAL_MAX - start);
}

static void message_i64(int64_t v) {
    uint8_t out[DECIMAL_MAX];
    const uint64_t start = format_i64(out, v);
    buffer_append(&message, out + start, DECIMAL_MAX - start);
}

// Flushes every buffer, then starts the failure line with the position and
// the kind ("runtime error", "panic" or "assertion failed").
static void fail_begin(const char* file, uint32_t line, uint32_t col, const char* kind) {
    fort_rt_flush_all();
    message.len = 0;
    message_str(file);
    message_str(":");
    message_u64(line);
    message_str(":");
    message_u64(col);
    message_str(": ");
    message_str(kind);
    message_str(": ");
}

static _Noreturn void fail_end(void) {
    message_str("\n");
    buffer_flush(&message);
    abort();
}

static _Noreturn void fail_runtime(const char* file,
                                   uint32_t line,
                                   uint32_t col,
                                   const char* what) {
    fail_begin(file, line, col, "runtime error");
    message_str(what);
    fail_end();
}

_Noreturn void fort_rt_fail_bounds(
    int64_t index, uint64_t len, const char* file, uint32_t line, uint32_t col) {
    fail_begin(file, line, col, "runtime error");
    message_str("index ");
    message_i64(index);
    message_str(" out of range for length ");
    message_u64(len);
    fail_end();
}

_Noreturn void fort_rt_fail_span(
    int64_t lo, int64_t hi, uint64_t len, const char* file, uint32_t line, uint32_t col) {
    fail_begin(file, line, col, "runtime error");
    message_str("span bounds ");
    message_i64(lo);
    message_str("..");
    message_i64(hi);
    message_str(" out of range for length ");
    message_u64(len);
    fail_end();
}

_Noreturn void fort_rt_fail_overflow(const char* file, uint32_t line, uint32_t col) {
    fail_runtime(file, line, col, "integer overflow");
}

_Noreturn void fort_rt_fail_shift(
    int64_t count, const char* type, const char* file, uint32_t line, uint32_t col) {
    fail_begin(file, line, col, "runtime error");
    message_str("shift count ");
    message_i64(count);
    message_str(" out of range for ");
    message_str(type);
    fail_end();
}

_Noreturn void fort_rt_fail_div_zero(const char* file, uint32_t line, uint32_t col) {
    fail_runtime(file, line, col, "division by zero");
}

_Noreturn void fort_rt_fail_div_overflow(const char* file, uint32_t line, uint32_t col) {
    fail_runtime(file, line, col, "division overflow");
}

_Noreturn void fort_rt_fail_alloc_count(int64_t n, const char* file, uint32_t line, uint32_t col) {
    fail_begin(file, line, col, "runtime error");
    message_str("negative allocation count ");
    message_i64(n);
    fail_end();
}

_Noreturn void fort_rt_fail_overwrite(const char* file, uint32_t line, uint32_t col) {
    fail_runtime(file, line, col, "overwriting owned value");
}

_Noreturn void fort_rt_panic(
    const char* ptr, uint64_t len, const char* file, uint32_t line, uint32_t col) {
    fail_begin(file, line, col, "panic");
    if (len > 0) {
        buffer_append(&message, (const uint8_t*)ptr, len);
    }
    fail_end();
}

_Noreturn void fort_rt_assert_fail(const char* text,
                                   const char* file,
                                   uint32_t line,
                                   uint32_t col) {
    fail_begin(file, line, col, "assertion failed");
    message_str(text);
    fail_end();
}

// ---- allocation (D10.2, D10.3) -------------------------------------------------

void* fort_rt_new(
    uint64_t elem_size, uint64_t count, const char* file, uint32_t line, uint32_t col) {
    if (elem_size != 0 && count > UINT64_MAX / elem_size) {
        fail_runtime(file, line, col, "allocation size overflow");
    }
    uint64_t size = elem_size * count;
    if (size == 0) {
        size = 1;
    }
    void* p = calloc(1, (size_t)size);
    if (p == NULL) {
        fail_runtime(file, line, col, "out of memory");
    }
    return p;
}

void fort_rt_del(void* p) {
    free(p);
}

// ---- process (D11.6, D8.6) ------------------------------------------------------

static const struct fort_string* args_ptr = NULL;
static uint64_t args_len = 0;

void fort_rt_args_init(int argc, char** argv) {
    if (argc <= 0) {
        args_ptr = NULL;
        args_len = 0;
        return;
    }
    struct fort_string* args = calloc((size_t)argc, sizeof *args);
    if (args == NULL) {
        fail_runtime("<startup>", 0, 0, "out of memory");
    }
    for (int i = 0; i < argc; i++) {
        args[i].ptr = argv[i];
        args[i].len = (uint64_t)strlen(argv[i]);
    }
    args_ptr = args;
    args_len = (uint64_t)argc;
}

const struct fort_string* fort_rt_args_ptr(void) {
    return args_ptr;
}

uint64_t fort_rt_args_len(void) {
    return args_len;
}

_Noreturn void fort_rt_exit(int32_t status) {
    fort_rt_flush_all();
    exit(status & EXIT_STATUS_MASK);
}

#ifndef FORT_RT_NO_MAIN
int main(int argc, char** argv) {
    fort_rt_args_init(argc, argv);
    struct fort_span args;
    args.ptr = (void*)args_ptr;
    args.len = args_len;
    const int32_t status = fort_entry(&args);
    fort_rt_flush_all();
    return status & EXIT_STATUS_MASK;
}
#endif
