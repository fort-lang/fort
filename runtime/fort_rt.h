// The fort C runtime: the entry points of toolchain.md 5.1 (D11.4, D11.5,
// D11.6, D10.2, D10.3, D17.11). The compiler emits calls to these functions
// and the standard library declares the ones it needs with extern fn.
//
// FORT_RT_NO_MAIN leaves out main, for the native object linked into the unit
// tests; the cross-compiled fort_rt.o always has it.
#ifndef FORT_RT_H
#define FORT_RT_H

#include <stdint.h>

// Types shared with generated code.
struct fort_string {
    const char* ptr;
    uint64_t len;
};

struct fort_span {
    void* ptr;
    uint64_t len;
};

struct fort_rt_enum_member {
    int32_t value;
    const char* name;
};

// Allocation (D10.2, D10.3).
void* fort_rt_new(
    uint64_t elem_size, uint64_t count, const char* file, uint32_t line, uint32_t col);
void fort_rt_del(void* p);

// Failures (D11.4): flush every buffer, write one line to stderr, abort().
_Noreturn void fort_rt_fail_bounds(
    int64_t index, uint64_t len, const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_span(
    int64_t lo, int64_t hi, uint64_t len, const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_overflow(const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_shift(
    int64_t count, const char* type, const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_div_zero(const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_div_overflow(const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_alloc_count(int64_t n, const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_overwrite(const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_fail_enum(
    int64_t v, const char* type, const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_panic(
    const char* ptr, uint64_t len, const char* file, uint32_t line, uint32_t col);
_Noreturn void fort_rt_assert_fail(const char* text, const char* file, uint32_t line, uint32_t col);

// Printing (D11.5, D11.7, D12.2).
void fort_rt_print_i64(int32_t fd, int64_t v);
void fort_rt_print_u64(int32_t fd, uint64_t v);
void fort_rt_print_f32(int32_t fd, float v);
void fort_rt_print_f64(int32_t fd, double v);
void fort_rt_print_bool(int32_t fd, uint8_t v);
void fort_rt_print_char(int32_t fd, uint8_t c);
void fort_rt_print_ptr(int32_t fd, const void* p);
void fort_rt_print_str(int32_t fd, const char* ptr, uint64_t len);
void fort_rt_print_enum(int32_t fd, int32_t v, const struct fort_rt_enum_member* m, uint64_t n);
void fort_rt_flush(int32_t fd);
void fort_rt_flush_all(void);

// Process (D11.6, D8.6). fort_entry is emitted by the compiler. main calls
// fort_rt_args_init, which builds the args span that fort_rt_args_ptr and
// fort_rt_args_len return, then fort_entry, then fort_rt_flush_all.
#ifndef FORT_RT_NO_MAIN
int main(int argc, char** argv);
#endif
int32_t fort_entry(const struct fort_span* args);
void fort_rt_args_init(int argc, char** argv);
const struct fort_string* fort_rt_args_ptr(void);
uint64_t fort_rt_args_len(void);
_Noreturn void fort_rt_exit(int32_t status);

#endif
