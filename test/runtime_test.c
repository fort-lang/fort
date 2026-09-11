// Unit tests of the C runtime (toolchain.md 5): the formatting of every print
// entry point but the float ones (D11.7), which are in runtime_float_test.c,
// buffering and flush order (D11.5), the message of every failure entry point
// (D11.4), allocation (D10.2, D10.3), the argument vector (D8.6) and exit
// (D11.6).
//
// Output is captured by pointing a descriptor at a temporary file. A failure
// entry point aborts the process, so each one runs in a forked child whose
// stdout and stderr are both tied to one pipe; the parent checks the bytes
// and the SIGABRT.
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/wait.h>

#include "fort_rt.h"
#include "runtime_helpers.h"

#include "test.h"

// The literals below are the sample values and the expected texts of the
// formatting rules; naming each one would only hide what is being checked.
// NOLINTBEGIN(readability-magic-numbers)

enum {
    // The runtime's buffer is at most this large: a program that has printed
    // this many bytes has seen at least one flush.
    BUFFER_BOUND = 65536,
    // Enough to overrun any reasonable buffer several times.
    LARGE_OUTPUT = 4 * BUFFER_BOUND,
    CHUNK = 1024,
    // More descriptors than the runtime keeps buffers for.
    MANY_FDS = 24,
    STATUS_OVER_A_BYTE = 300,
    STATUS_OVER_A_BYTE_TRUNCATED = 44,
};

static const char FILE_NAME[] = "dir/main.ft";
enum { LINE = 12, COL = 14 };

// ---- running a failure in a child --------------------------------------------

typedef struct {
    int status;
    char out[CAPTURE_MAX];
} child_t;

// Forks, ties the child's fd 1 and fd 2 to one pipe, runs body there, and
// returns the wait status and everything the child wrote, in order.
static child_t run_child(void (*body)(void)) {
    child_t r;
    r.status = -1;
    r.out[0] = '\0';
    int fds[2];
    if (pipe(fds) != 0) {
        return r;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        return r;
    }
    if (pid == 0) {
        TEST_UNUSED(dup2(fds[1], 1));
        TEST_UNUSED(dup2(fds[1], 2));
        TEST_UNUSED(close(fds[0]));
        TEST_UNUSED(close(fds[1]));
        body();
        _exit(0);
    }
    TEST_UNUSED(close(fds[1]));
    size_t len = 0;
    while (len < sizeof r.out - 1) {
        const ssize_t got = read(fds[0], r.out + len, sizeof r.out - 1 - len);
        if (got <= 0) {
            break;
        }
        len += (size_t)got;
    }
    r.out[len] = '\0';
    TEST_UNUSED(close(fds[0]));
    TEST_UNUSED(waitpid(pid, &r.status, 0));
    return r;
}

static bool aborted(const child_t* r) {
    return WIFSIGNALED(r->status) && WTERMSIG(r->status) == SIGABRT;
}

#define ASSERT_ABORTS_WITH(body, expected)                                                         \
    do {                                                                                           \
        const child_t r = run_child(body);                                                         \
        ASSERT_SAME_TEXT(r.out, expected);                                                         \
        TEST_ASSERT_TRUE(aborted(&r));                                                             \
    } while (0)

// ---- integers (D11.7) ----------------------------------------------------------

static void print_i64_samples(void) {
    fort_rt_print_i64(1, 0);
    fort_rt_print_char(1, ' ');
    fort_rt_print_i64(1, 7);
    fort_rt_print_char(1, ' ');
    fort_rt_print_i64(1, -1);
    fort_rt_print_char(1, ' ');
    fort_rt_print_i64(1, 1234567890123456789LL);
    fort_rt_print_char(1, ' ');
    fort_rt_print_i64(1, INT64_MAX);
    fort_rt_print_char(1, ' ');
    fort_rt_print_i64(1, INT64_MIN);
}

TEST(i64_prints_decimal_with_sign_including_the_minimum, {
    ASSERT_STDOUT(print_i64_samples,
                  "0 7 -1 1234567890123456789 9223372036854775807 -9223372036854775808");
})

static void print_u64_samples(void) {
    fort_rt_print_u64(1, 0);
    fort_rt_print_char(1, ' ');
    fort_rt_print_u64(1, 10);
    fort_rt_print_char(1, ' ');
    fort_rt_print_u64(1, 255);
    fort_rt_print_char(1, ' ');
    fort_rt_print_u64(1, UINT64_MAX);
}

TEST(u64_prints_decimal_up_to_the_maximum,
     { ASSERT_STDOUT(print_u64_samples, "0 10 255 18446744073709551615"); })

static void print_powers_of_ten(void) {
    uint64_t v = 1;
    for (int i = 0; i < 20; i++) {
        fort_rt_print_u64(1, v);
        fort_rt_print_char(1, ',');
        v *= 10;
    }
}

TEST(every_decimal_length_is_rendered_without_padding, {
    ASSERT_STDOUT(print_powers_of_ten,
                  "1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000,"
                  "10000000000,100000000000,1000000000000,10000000000000,100000000000000,"
                  "1000000000000000,10000000000000000,100000000000000000,"
                  "1000000000000000000,10000000000000000000,");
})

// ---- bool, char, string ---------------------------------------------------------

static void print_bools(void) {
    fort_rt_print_bool(1, 1);
    fort_rt_print_char(1, ' ');
    fort_rt_print_bool(1, 0);
}

TEST(bool_prints_true_or_false, { ASSERT_STDOUT(print_bools, "true false"); })

static void print_chars(void) {
    fort_rt_print_char(1, 'a');
    fort_rt_print_char(1, '\n');
    fort_rt_print_char(1, 0);
    fort_rt_print_char(1, 0xFF);
}

TEST(char_prints_its_byte_unchanged, {
    char actual[CAPTURE_MAX];
    const size_t got = stdout_of(print_chars, actual, sizeof actual);
    TEST_ASSERT_EQ_SIZE(got, (size_t)4);
    TEST_ASSERT_EQ_INT32(actual[0], 'a');
    TEST_ASSERT_EQ_INT32(actual[1], '\n');
    TEST_ASSERT_EQ_INT32(actual[2], 0);
    TEST_ASSERT_EQ_INT32((unsigned char)actual[3], 0xFF);
})

static void print_strings(void) {
    fort_rt_print_str(1, "hello", 5);
    fort_rt_print_str(1, "", 0);
    fort_rt_print_str(1, NULL, 0);
    fort_rt_print_str(1, "world!", 3);
    fort_rt_print_str(1, "a\0b", 3);
}

TEST(str_prints_len_bytes_and_ignores_the_pointer_when_empty, {
    char actual[CAPTURE_MAX];
    const size_t got = stdout_of(print_strings, actual, sizeof actual);
    TEST_ASSERT_EQ_SIZE(got, (size_t)11);
    TEST_ASSERT_EQ_INT32(memcmp(actual, "hellowora\0b", 11), 0);
})

// ---- pointers ---------------------------------------------------------------------

// Fixed bit patterns, not real addresses: the text they render to is fixed.
// NOLINTBEGIN(performance-no-int-to-ptr)
static void print_pointers(void) {
    fort_rt_print_ptr(1, NULL);
    fort_rt_print_char(1, ' ');
    fort_rt_print_ptr(1, (const void*)(uintptr_t)0x7fffabcd1234ULL);
    fort_rt_print_char(1, ' ');
    fort_rt_print_ptr(1, (const void*)(uintptr_t)0x10ULL);
    fort_rt_print_char(1, ' ');
    fort_rt_print_ptr(1, (const void*)(uintptr_t)UINT64_MAX);
    fort_rt_print_char(1, ' ');
    fort_rt_print_ptr(1, (const void*)(uintptr_t)0x0123456789ABCDEFULL);
}
// NOLINTEND(performance-no-int-to-ptr)

TEST(ptr_prints_0x_and_lowercase_hex_without_leading_zeros, {
    ASSERT_STDOUT(print_pointers, "0x0 0x7fffabcd1234 0x10 0xffffffffffffffff 0x123456789abcdef");
})

// ---- enums -------------------------------------------------------------------------

static const struct fort_rt_enum_member COLOR[] = {
    {0, "red"},
    {1, "green"},
    {7, "blue"},
    {-3, "ultraviolet"},
};
enum { COLOR_COUNT = sizeof COLOR / sizeof COLOR[0] };

static void print_enums(void) {
    fort_rt_print_enum(1, 0, COLOR, COLOR_COUNT);
    fort_rt_print_char(1, ' ');
    fort_rt_print_enum(1, 7, COLOR, COLOR_COUNT);
    fort_rt_print_char(1, ' ');
    fort_rt_print_enum(1, -3, COLOR, COLOR_COUNT);
    fort_rt_print_char(1, ' ');
    fort_rt_print_enum(1, 1, COLOR, COLOR_COUNT);
}

TEST(enum_prints_the_member_name, { ASSERT_STDOUT(print_enums, "red blue ultraviolet green"); })

static void print_enum_fallbacks(void) {
    fort_rt_print_enum(1, 2, COLOR, COLOR_COUNT);
    fort_rt_print_char(1, ' ');
    fort_rt_print_enum(1, -1, COLOR, COLOR_COUNT);
    fort_rt_print_char(1, ' ');
    fort_rt_print_enum(1, INT32_MIN, COLOR, COLOR_COUNT);
    fort_rt_print_char(1, ' ');
    fort_rt_print_enum(1, 5, COLOR, 0);
    fort_rt_print_char(1, ' ');
    fort_rt_print_enum(1, 7, COLOR, 2);
}

TEST(enum_prints_the_number_when_no_member_matches,
     { ASSERT_STDOUT(print_enum_fallbacks, "2 -1 -2147483648 5 7"); })

// ---- buffering (D11.5) ----------------------------------------------------------

TEST(stdout_is_held_until_flushed, {
    capture_t c = capture_begin(1);
    fort_rt_print_str(1, "held", 4);
    ASSERT_SIZE(c, 0);
    fort_rt_flush(1);
    ASSERT_SIZE(c, 4);
    fort_rt_print_str(1, "more", 4);
    ASSERT_SIZE(c, 4);
    fort_rt_flush_all();
    ASSERT_SIZE(c, 8);
    char actual[CAPTURE_MAX];
    TEST_UNUSED(capture_read(&c, actual, sizeof actual));
    capture_end(&c);
    ASSERT_SAME_TEXT(actual, "heldmore");
})

TEST(stderr_is_written_immediately, {
    capture_t c = capture_begin(2);
    fort_rt_print_str(2, "now", 3);
    ASSERT_SIZE(c, 3);
    fort_rt_print_i64(2, -5);
    ASSERT_SIZE(c, 5);
    char actual[CAPTURE_MAX];
    TEST_UNUSED(capture_read(&c, actual, sizeof actual));
    capture_end(&c);
    ASSERT_SAME_TEXT(actual, "now-5");
})

TEST(flush_of_one_descriptor_leaves_the_others_alone, {
    capture_t out = capture_begin(1);
    capture_t other = capture_begin(-1);
    fort_rt_print_str(1, "to stdout", 9);
    fort_rt_print_str(other.fd, "to other", 8);
    ASSERT_SIZE(out, 0);
    ASSERT_SIZE(other, 0);
    fort_rt_flush(other.fd);
    ASSERT_SIZE(out, 0);
    ASSERT_SIZE(other, 8);
    fort_rt_flush(1);
    ASSERT_SIZE(out, 9);
    char actual[CAPTURE_MAX];
    TEST_UNUSED(capture_read(&other, actual, sizeof actual));
    ASSERT_SAME_TEXT(actual, "to other");
    TEST_UNUSED(capture_read(&out, actual, sizeof actual));
    ASSERT_SAME_TEXT(actual, "to stdout");
    capture_end(&other);
    capture_end(&out);
})

TEST(flush_of_an_unknown_descriptor_is_a_no_op, {
    fort_rt_flush(2);
    fort_rt_flush(-7);
    fort_rt_flush(12345);
})

TEST(a_negative_descriptor_drops_its_output, {
    fort_rt_print_str(-1, "nowhere", 7);
    fort_rt_print_i64(-1, 1);
    fort_rt_flush(-1);
    fort_rt_flush_all();
})

TEST(a_full_buffer_is_written_out_and_nothing_is_lost, {
    capture_t c = capture_begin(1);
    char chunk[CHUNK];
    TEST_UNUSED(memset(chunk, 'x', sizeof chunk));
    for (int i = 0; i < LARGE_OUTPUT / CHUNK; i++) {
        fort_rt_print_str(1, chunk, sizeof chunk);
    }
    TEST_ASSERT_GE_INT64(capture_size(&c), (int64_t)(LARGE_OUTPUT - BUFFER_BOUND));
    fort_rt_flush(1);
    ASSERT_SIZE(c, LARGE_OUTPUT);
    TEST_UNUSED(fseek(c.file, 0, SEEK_SET));
    long ok = 0;
    int ch = 0;
    while ((ch = fgetc(c.file)) != EOF && ch == 'x') {
        ok++;
    }
    capture_end(&c);
    TEST_ASSERT_EQ_INT64(ok, (int64_t)LARGE_OUTPUT);
})

TEST(a_string_larger_than_any_buffer_arrives_intact_and_in_order, {
    capture_t c = capture_begin(1);
    char* big = malloc(LARGE_OUTPUT);
    TEST_ASSERT_NONNULL(big);
    for (int i = 0; i < LARGE_OUTPUT; i++) {
        big[i] = (char)('a' + (i % 26));
    }
    fort_rt_print_str(1, "<", 1);
    fort_rt_print_str(1, big, LARGE_OUTPUT);
    fort_rt_print_str(1, ">", 1);
    fort_rt_flush(1);
    ASSERT_SIZE(c, LARGE_OUTPUT + 2);
    TEST_UNUSED(fseek(c.file, 0, SEEK_SET));
    TEST_ASSERT_EQ_INT32(fgetc(c.file), '<');
    long ok = 0;
    for (int i = 0; i < LARGE_OUTPUT; i++) {
        if (fgetc(c.file) == big[i]) {
            ok++;
        }
    }
    TEST_ASSERT_EQ_INT32(fgetc(c.file), '>');
    TEST_ASSERT_EQ_INT32(fgetc(c.file), EOF);
    free(big);
    capture_end(&c);
    TEST_ASSERT_EQ_INT64(ok, (int64_t)LARGE_OUTPUT);
})

TEST(every_descriptor_gets_its_own_output_even_beyond_the_buffer_pool, {
    FILE* files[MANY_FDS];
    for (int i = 0; i < MANY_FDS; i++) {
        files[i] = tmpfile();
        TEST_ASSERT_NONNULL(files[i]);
    }
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < MANY_FDS; i++) {
            fort_rt_print_str(fileno(files[i]), "fd", 2);
            fort_rt_print_i64(fileno(files[i]), i);
            fort_rt_print_char(fileno(files[i]), ';');
        }
    }
    fort_rt_flush_all();
    int ok = 0;
    for (int i = 0; i < MANY_FDS; i++) {
        char expected[CAPTURE_MAX];
        TEST_UNUSED(snprintf(expected, sizeof expected, "fd%d;fd%d;fd%d;", i, i, i));
        char actual[CAPTURE_MAX];
        TEST_UNUSED(fseek(files[i], 0, SEEK_SET));
        const size_t got = fread(actual, 1, sizeof actual - 1, files[i]);
        actual[got] = '\0';
        ok += strcmp(actual, expected) == 0;
        TEST_UNUSED(fclose(files[i]));
    }
    TEST_ASSERT_EQ_INT32(ok, MANY_FDS);
})

TEST(a_write_error_is_ignored, {
    const int saved = dup(1);
    TEST_UNUSED(close(1));
    fort_rt_print_str(1, "dropped", 7);
    fort_rt_flush(1);
    TEST_UNUSED(dup2(saved, 1));
    TEST_UNUSED(close(saved));
    FILE* file = tmpfile();
    TEST_ASSERT_NONNULL(file);
    const int closed = dup(fileno(file));
    TEST_UNUSED(close(closed));
    fort_rt_print_str(closed, "dropped", 7);
    fort_rt_flush(closed);
    TEST_UNUSED(fclose(file));
})

// ---- failures (D11.4, toolchain.md 5.2) -----------------------------------------

static void fail_bounds(void) {
    fort_rt_fail_bounds(5, 3, FILE_NAME, LINE, COL);
}

TEST(bounds_message, {
    ASSERT_ABORTS_WITH(fail_bounds,
                       "dir/main.ft:12:14: runtime error: index 5 out of range for length 3\n");
})

static void fail_bounds_negative(void) {
    fort_rt_fail_bounds(-1, 0, FILE_NAME, 1, 1);
}

TEST(bounds_message_prints_the_index_signed, {
    ASSERT_ABORTS_WITH(fail_bounds_negative,
                       "dir/main.ft:1:1: runtime error: index -1 out of range for length 0\n");
})

static void fail_bounds_extremes(void) {
    fort_rt_fail_bounds(INT64_MIN, UINT64_MAX, "f", UINT32_MAX, UINT32_MAX);
}

TEST(bounds_message_at_the_extremes, {
    ASSERT_ABORTS_WITH(fail_bounds_extremes,
                       "f:4294967295:4294967295: runtime error: index -9223372036854775808 out "
                       "of range for length 18446744073709551615\n");
})

static void fail_span(void) {
    fort_rt_fail_span(2, 7, 3, FILE_NAME, LINE, COL);
}

TEST(span_message, {
    ASSERT_ABORTS_WITH(
        fail_span,
        "dir/main.ft:12:14: runtime error: span bounds 2..7 out of range for length 3\n");
})

static void fail_span_negative(void) {
    fort_rt_fail_span(-4, -2, 9, FILE_NAME, LINE, COL);
}

TEST(span_message_prints_the_bounds_signed, {
    ASSERT_ABORTS_WITH(
        fail_span_negative,
        "dir/main.ft:12:14: runtime error: span bounds -4..-2 out of range for length 9\n");
})

static void fail_overflow(void) {
    fort_rt_fail_overflow(FILE_NAME, LINE, COL);
}

TEST(overflow_message,
     { ASSERT_ABORTS_WITH(fail_overflow, "dir/main.ft:12:14: runtime error: integer overflow\n"); })

static void fail_shift(void) {
    fort_rt_fail_shift(64, "i64", FILE_NAME, LINE, COL);
}

TEST(shift_message, {
    ASSERT_ABORTS_WITH(fail_shift,
                       "dir/main.ft:12:14: runtime error: shift count 64 out of range for i64\n");
})

static void fail_shift_negative(void) {
    fort_rt_fail_shift(-1, "u8", FILE_NAME, LINE, COL);
}

TEST(shift_message_prints_the_count_signed, {
    ASSERT_ABORTS_WITH(fail_shift_negative,
                       "dir/main.ft:12:14: runtime error: shift count -1 out of range for u8\n");
})

static void fail_div_zero(void) {
    fort_rt_fail_div_zero(FILE_NAME, LINE, COL);
}

TEST(div_zero_message,
     { ASSERT_ABORTS_WITH(fail_div_zero, "dir/main.ft:12:14: runtime error: division by zero\n"); })

static void fail_div_overflow(void) {
    fort_rt_fail_div_overflow(FILE_NAME, LINE, COL);
}

TEST(div_overflow_message, {
    ASSERT_ABORTS_WITH(fail_div_overflow, "dir/main.ft:12:14: runtime error: division overflow\n");
})

static void fail_alloc_count(void) {
    fort_rt_fail_alloc_count(-1, FILE_NAME, LINE, COL);
}

TEST(alloc_count_message, {
    ASSERT_ABORTS_WITH(fail_alloc_count,
                       "dir/main.ft:12:14: runtime error: negative allocation count -1\n");
})

static void fail_alloc_count_minimum(void) {
    fort_rt_fail_alloc_count(INT64_MIN, FILE_NAME, LINE, COL);
}

TEST(alloc_count_message_at_the_minimum, {
    ASSERT_ABORTS_WITH(fail_alloc_count_minimum,
                       "dir/main.ft:12:14: runtime error: negative allocation count "
                       "-9223372036854775808\n");
})

static void fail_overwrite(void) {
    fort_rt_fail_overwrite(FILE_NAME, LINE, COL);
}

TEST(overwrite_message, {
    ASSERT_ABORTS_WITH(fail_overwrite,
                       "dir/main.ft:12:14: runtime error: overwriting owned value\n");
})

static void panic_message_body(void) {
    fort_rt_panic("queue empty", 11, FILE_NAME, LINE, COL);
}

TEST(panic_message,
     { ASSERT_ABORTS_WITH(panic_message_body, "dir/main.ft:12:14: panic: queue empty\n"); })

static void panic_empty_body(void) {
    fort_rt_panic(NULL, 0, FILE_NAME, LINE, COL);
}

TEST(panic_with_an_empty_message,
     { ASSERT_ABORTS_WITH(panic_empty_body, "dir/main.ft:12:14: panic: \n"); })

static void panic_len_body(void) {
    fort_rt_panic("abcdef", 3, FILE_NAME, LINE, COL);
}

TEST(panic_prints_len_bytes_not_a_c_string,
     { ASSERT_ABORTS_WITH(panic_len_body, "dir/main.ft:12:14: panic: abc\n"); })

static void assert_fail_body(void) {
    fort_rt_assert_fail("n > 0", FILE_NAME, LINE, COL);
}

TEST(assert_message,
     { ASSERT_ABORTS_WITH(assert_fail_body, "dir/main.ft:12:14: assertion failed: n > 0\n"); })

static void fail_after_output(void) {
    fort_rt_print_str(1, "buffered stdout\n", 16);
    fort_rt_print_str(2, "direct stderr\n", 14);
    fort_rt_print_str(1, "more stdout", 11);
    fort_rt_fail_div_zero(FILE_NAME, LINE, COL);
}

TEST(a_failure_flushes_pending_output_before_its_message, {
    ASSERT_ABORTS_WITH(fail_after_output,
                       "direct stderr\n"
                       "buffered stdout\n"
                       "more stdout"
                       "dir/main.ft:12:14: runtime error: division by zero\n");
})

static void fail_with_a_long_message(void) {
    static char text[3 * BUFFER_BOUND];
    TEST_UNUSED(memset(text, 'q', sizeof text - 1));
    text[sizeof text - 1] = '\0';
    fort_rt_assert_fail(text, FILE_NAME, LINE, COL);
}

TEST(a_message_longer_than_the_runtime_buffer_is_complete, {
    int fds[2];
    TEST_ERROR_NONZERO(pipe(fds));
    const pid_t pid = fork();
    if (pid == 0) {
        TEST_UNUSED(dup2(fds[1], 2));
        TEST_UNUSED(close(fds[0]));
        TEST_UNUSED(close(fds[1]));
        fail_with_a_long_message();
        _exit(0);
    }
    TEST_UNUSED(close(fds[1]));
    long total = 0;
    long qs = 0;
    char last = '\0';
    char chunk[CAPTURE_MAX];
    ssize_t got = 0;
    while ((got = read(fds[0], chunk, sizeof chunk)) > 0) {
        for (ssize_t i = 0; i < got; i++) {
            qs += chunk[i] == 'q';
            last = chunk[i];
        }
        total += got;
    }
    TEST_UNUSED(close(fds[0]));
    int status = 0;
    TEST_UNUSED(waitpid(pid, &status, 0));
    TEST_ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    TEST_ASSERT_EQ_INT64(qs, (int64_t)(3 * BUFFER_BOUND - 1));
    TEST_ASSERT_EQ_INT64(total, (int64_t)strlen("dir/main.ft:12:14: assertion failed: \n") + qs);
    TEST_ASSERT_EQ_INT32(last, '\n');
})

// ---- allocation (D10.2, D10.3) ---------------------------------------------------

TEST(new_returns_zeroed_storage, {
    uint8_t* p = fort_rt_new(4, 8, FILE_NAME, LINE, COL);
    TEST_ASSERT_NONNULL(p);
    int zeros = 0;
    for (int i = 0; i < 32; i++) {
        zeros += p[i] == 0;
    }
    TEST_ASSERT_EQ_INT32(zeros, 32);
    TEST_UNUSED(memset(p, 1, 32));
    fort_rt_del(p);
})

TEST(new_of_zero_elements_is_non_null_and_deletable, {
    void* a = fort_rt_new(8, 0, FILE_NAME, LINE, COL);
    void* b = fort_rt_new(0, 8, FILE_NAME, LINE, COL);
    void* c = fort_rt_new(0, 0, FILE_NAME, LINE, COL);
    TEST_ASSERT_NONNULL(a);
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_NONNULL(c);
    fort_rt_del(a);
    fort_rt_del(b);
    fort_rt_del(c);
})

TEST(del_of_null_is_a_no_op, { fort_rt_del(NULL); })

static void new_overflowing(void) {
    TEST_UNUSED(fort_rt_new(UINT64_MAX / 2, 3, FILE_NAME, LINE, COL));
}

TEST(new_reports_a_size_overflow, {
    ASSERT_ABORTS_WITH(new_overflowing,
                       "dir/main.ft:12:14: runtime error: allocation size overflow\n");
})

static void new_barely_overflowing(void) {
    TEST_UNUSED(fort_rt_new(UINT64_MAX / 2 + 1, 2, FILE_NAME, LINE, COL));
}

TEST(new_reports_a_size_overflow_by_one_bit, {
    ASSERT_ABORTS_WITH(new_barely_overflowing,
                       "dir/main.ft:12:14: runtime error: allocation size overflow\n");
})

static void new_too_large(void) {
    TEST_UNUSED(fort_rt_new(1ULL << 62, 1, FILE_NAME, LINE, COL));
}

// A sanitizer allocator that returns null first prints a "==<pid>==WARNING"
// line of its own; the runtime's line must be the exact tail of the output.
TEST(new_reports_out_of_memory, {
    const child_t r = run_child(new_too_large);
    const char* line = "dir/main.ft:12:14: runtime error: out of memory\n";
    const size_t got = strlen(r.out);
    TEST_ASSERT_GE_SIZE(got, strlen(line));
    const char* tail = r.out + got - strlen(line);
    ASSERT_SAME_TEXT(tail, line);
    TEST_ASSERT_TRUE(tail == r.out || strncmp(r.out, "==", 2) == 0);
    TEST_ASSERT_TRUE(aborted(&r));
})

static void new_fails_after_output(void) {
    fort_rt_print_str(1, "pending", 7);
    TEST_UNUSED(fort_rt_new(UINT64_MAX, UINT64_MAX, FILE_NAME, LINE, COL));
}

TEST(new_failure_flushes_first, {
    ASSERT_ABORTS_WITH(new_fails_after_output,
                       "pendingdir/main.ft:12:14: runtime error: allocation size overflow\n");
})

// ---- process (D11.6, D8.6) ---------------------------------------------------------

TEST(args_are_empty_before_init, {
    TEST_ASSERT_NULL(fort_rt_args_ptr());
    TEST_ASSERT_EQ_INT64((int64_t)fort_rt_args_len(), (int64_t)0);
})

static char arg_prog[] = "prog";
static char arg_empty[] = "";
static char arg_words[] = "two words";
static char arg_x[] = "x";
static char* fake_argv[] = {arg_prog, arg_empty, arg_words, arg_x, NULL};
enum { FAKE_ARGC = 4 };

TEST(args_init_builds_one_string_per_argument, {
    fort_rt_args_init(FAKE_ARGC, fake_argv);
    const struct fort_string* args = fort_rt_args_ptr();
    TEST_ASSERT_NONNULL(args);
    TEST_ASSERT_EQ_INT64((int64_t)fort_rt_args_len(), (int64_t)FAKE_ARGC);
    TEST_ASSERT_TRUE(args[0].ptr == arg_prog);
    TEST_ASSERT_EQ_INT64((int64_t)args[0].len, (int64_t)4);
    TEST_ASSERT_EQ_INT64((int64_t)args[1].len, (int64_t)0);
    TEST_ASSERT_EQ_INT64((int64_t)args[2].len, (int64_t)9);
    TEST_ASSERT_EQ_INT32(memcmp(args[2].ptr, "two words", 9), 0);
    TEST_ASSERT_EQ_INT32(args[2].ptr[9], 0);
    TEST_ASSERT_EQ_INT64((int64_t)args[3].len, (int64_t)1);
    fort_rt_del((void*)args);
})

static char* empty_argv[] = {NULL};

TEST(args_init_with_no_arguments_is_empty, {
    fort_rt_args_init(0, empty_argv);
    TEST_ASSERT_NULL(fort_rt_args_ptr());
    TEST_ASSERT_EQ_INT64((int64_t)fort_rt_args_len(), (int64_t)0);
})

static void exit_after_output(void) {
    fort_rt_print_str(1, "bye", 3);
    fort_rt_exit(STATUS_OVER_A_BYTE);
}

TEST(exit_flushes_and_truncates_the_status_to_a_byte, {
    const child_t r = run_child(exit_after_output);
    TEST_ASSERT_TRUE(WIFEXITED(r.status));
    TEST_ASSERT_EQ_INT32(WEXITSTATUS(r.status), STATUS_OVER_A_BYTE_TRUNCATED);
    ASSERT_SAME_TEXT(r.out, "bye");
})

static void exit_negative(void) {
    fort_rt_exit(-1);
}

TEST(exit_with_a_negative_status_keeps_the_low_byte, {
    const child_t r = run_child(exit_negative);
    TEST_ASSERT_TRUE(WIFEXITED(r.status));
    TEST_ASSERT_EQ_INT32(WEXITSTATUS(r.status), 0xFF);
    ASSERT_SAME_TEXT(r.out, "");
})

int main(int argc, char** argv) {
    TEST_INIT("runtime", argc, argv);
    TEST_RUN(i64_prints_decimal_with_sign_including_the_minimum);
    TEST_RUN(u64_prints_decimal_up_to_the_maximum);
    TEST_RUN(every_decimal_length_is_rendered_without_padding);
    TEST_RUN(bool_prints_true_or_false);
    TEST_RUN(char_prints_its_byte_unchanged);
    TEST_RUN(str_prints_len_bytes_and_ignores_the_pointer_when_empty);
    TEST_RUN(ptr_prints_0x_and_lowercase_hex_without_leading_zeros);
    TEST_RUN(enum_prints_the_member_name);
    TEST_RUN(enum_prints_the_number_when_no_member_matches);
    TEST_RUN(stdout_is_held_until_flushed);
    TEST_RUN(stderr_is_written_immediately);
    TEST_RUN(flush_of_one_descriptor_leaves_the_others_alone);
    TEST_RUN(flush_of_an_unknown_descriptor_is_a_no_op);
    TEST_RUN(a_negative_descriptor_drops_its_output);
    TEST_RUN(a_full_buffer_is_written_out_and_nothing_is_lost);
    TEST_RUN(a_string_larger_than_any_buffer_arrives_intact_and_in_order);
    TEST_RUN(every_descriptor_gets_its_own_output_even_beyond_the_buffer_pool);
    TEST_RUN(a_write_error_is_ignored);
    TEST_RUN(bounds_message);
    TEST_RUN(bounds_message_prints_the_index_signed);
    TEST_RUN(bounds_message_at_the_extremes);
    TEST_RUN(span_message);
    TEST_RUN(span_message_prints_the_bounds_signed);
    TEST_RUN(overflow_message);
    TEST_RUN(shift_message);
    TEST_RUN(shift_message_prints_the_count_signed);
    TEST_RUN(div_zero_message);
    TEST_RUN(div_overflow_message);
    TEST_RUN(alloc_count_message);
    TEST_RUN(alloc_count_message_at_the_minimum);
    TEST_RUN(overwrite_message);
    TEST_RUN(panic_message);
    TEST_RUN(panic_with_an_empty_message);
    TEST_RUN(panic_prints_len_bytes_not_a_c_string);
    TEST_RUN(assert_message);
    TEST_RUN(a_failure_flushes_pending_output_before_its_message);
    TEST_RUN(a_message_longer_than_the_runtime_buffer_is_complete);
    TEST_RUN(new_returns_zeroed_storage);
    TEST_RUN(new_of_zero_elements_is_non_null_and_deletable);
    TEST_RUN(del_of_null_is_a_no_op);
    TEST_RUN(new_reports_a_size_overflow);
    TEST_RUN(new_reports_a_size_overflow_by_one_bit);
    TEST_RUN(new_reports_out_of_memory);
    TEST_RUN(new_failure_flushes_first);
    TEST_RUN(args_are_empty_before_init);
    TEST_RUN(args_init_builds_one_string_per_argument);
    TEST_RUN(args_init_with_no_arguments_is_empty);
    TEST_RUN(exit_flushes_and_truncates_the_status_to_a_byte);
    TEST_RUN(exit_with_a_negative_status_keeps_the_low_byte);
    TEST_EXIT();
}

// NOLINTEND(readability-magic-numbers)
