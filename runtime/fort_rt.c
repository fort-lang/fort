/* The fort C runtime (toolchain.md 5).
 *
 * Ticket T-003 scaffold: every entry point of toolchain.md 5.1 exists with the
 * signature of fort_rt.h so that the cross-compiled fort_rt.o links and the
 * native object builds under every preset; the bodies are stubs that abort.
 * Ticket T-007 implements them. */
#include "fort_rt.h"

#include <stdlib.h>

void* fort_rt_new(
    uint64_t elem_size, uint64_t count, const char* file, uint32_t line, uint32_t col) {
    (void)elem_size;
    (void)count;
    (void)file;
    (void)line;
    (void)col;
    abort();
}

void fort_rt_del(void* p) {
    (void)p;
    abort();
}

_Noreturn void fort_rt_fail_bounds(
    int64_t index, uint64_t len, const char* file, uint32_t line, uint32_t col) {
    (void)index;
    (void)len;
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_fail_slice(
    int64_t lo, int64_t hi, uint64_t len, const char* file, uint32_t line, uint32_t col) {
    (void)lo;
    (void)hi;
    (void)len;
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_fail_overflow(const char* file, uint32_t line, uint32_t col) {
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_fail_shift(
    int64_t count, const char* type, const char* file, uint32_t line, uint32_t col) {
    (void)count;
    (void)type;
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_fail_div_zero(const char* file, uint32_t line, uint32_t col) {
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_fail_div_overflow(const char* file, uint32_t line, uint32_t col) {
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_fail_alloc_count(int64_t n, const char* file, uint32_t line, uint32_t col) {
    (void)n;
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_fail_overwrite(const char* file, uint32_t line, uint32_t col) {
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_panic(
    const char* ptr, uint64_t len, const char* file, uint32_t line, uint32_t col) {
    (void)ptr;
    (void)len;
    (void)file;
    (void)line;
    (void)col;
    abort();
}

_Noreturn void fort_rt_assert_fail(const char* text,
                                   const char* file,
                                   uint32_t line,
                                   uint32_t col) {
    (void)text;
    (void)file;
    (void)line;
    (void)col;
    abort();
}

void fort_rt_print_i64(int32_t fd, int64_t v) {
    (void)fd;
    (void)v;
    abort();
}

void fort_rt_print_u64(int32_t fd, uint64_t v) {
    (void)fd;
    (void)v;
    abort();
}

void fort_rt_print_f32(int32_t fd, float v) {
    (void)fd;
    (void)v;
    abort();
}

void fort_rt_print_f64(int32_t fd, double v) {
    (void)fd;
    (void)v;
    abort();
}

void fort_rt_print_bool(int32_t fd, uint8_t v) {
    (void)fd;
    (void)v;
    abort();
}

void fort_rt_print_char(int32_t fd, uint8_t c) {
    (void)fd;
    (void)c;
    abort();
}

void fort_rt_print_ptr(int32_t fd, const void* p) {
    (void)fd;
    (void)p;
    abort();
}

void fort_rt_print_str(int32_t fd, const char* ptr, uint64_t len) {
    (void)fd;
    (void)ptr;
    (void)len;
    abort();
}

void fort_rt_print_enum(int32_t fd, int32_t v, const struct fort_rt_enum_member* m, uint64_t n) {
    (void)fd;
    (void)v;
    (void)m;
    (void)n;
    abort();
}

void fort_rt_flush(int32_t fd) {
    (void)fd;
    abort();
}

void fort_rt_flush_all(void) {
    abort();
}

const struct fort_string* fort_rt_args_ptr(void) {
    abort();
}

uint64_t fort_rt_args_len(void) {
    abort();
}

_Noreturn void fort_rt_exit(int32_t status) {
    (void)status;
    abort();
}

#ifndef FORT_RT_NO_MAIN
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    abort();
}
#endif
