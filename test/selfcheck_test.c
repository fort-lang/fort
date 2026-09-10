// Self-check of the unit-test framework in test.h (toolchain.md 7.5).
//
// The positive tests run every TEST_ASSERT_* macro on values that satisfy it.
// The negative tests (neg_*) each violate one macro; they are registered only
// when a prefix is given, so the plain run stays green, and the spawn_* tests
// re-run this executable with a neg_* prefix to check the exit status, the
// logged message and the filter.
#include <stdlib.h>

#include <sys/wait.h>

#include "test.h"

enum { COMMAND_MAX = 4096, OUTPUT_MAX = 8192 };

// argv[0] of this run, set by main before any test runs.
static const char* self_path = NULL;

// Runs this executable with the given name prefix, capturing stdout and
// stderr into out; returns the exit status, or -1 when the run could not be
// started or ended abnormally.
static int run_self(const char* prefix, char* out, size_t out_size) {
    char command[COMMAND_MAX];
    const int n = snprintf(command, sizeof command, "'%s' %s 2>&1", self_path, prefix);
    if (n < 0 || (size_t)n >= sizeof command) {
        return -1;
    }
    FILE* pipe = popen(command, "r");
    if (pipe == NULL) {
        return -1;
    }
    size_t used = 0;
    while (used + 1 < out_size) {
        const size_t got = fread(out + used, 1, out_size - used - 1, pipe);
        if (got == 0) {
            break;
        }
        used += got;
    }
    out[used] = '\0';
    const int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

// ---- positive tests: every macro on satisfying values ---------------------------

TEST(assert_true_and_false, {
    TEST_ASSERT_TRUE(1 == 1);
    TEST_ASSERT_TRUE(true);
    TEST_ASSERT_FALSE(1 == 2);
    TEST_ASSERT_FALSE(false);
})

TEST(assert_null_and_nonnull, {
    const char* null_ptr = NULL;
    const char* text = "text";
    TEST_ASSERT_NULL(null_ptr);
    TEST_ASSERT_NONNULL(text);
    TEST_ASSERT_NONNULL(self_path);
})

TEST(assert_eq_char, {
    const char c = 'x';
    TEST_ASSERT_EQ_CHAR(c, 'x');
    TEST_ASSERT_EQ_CHAR("abc"[1], 'b');
})

TEST(assert_eq_int32, {
    const int32_t v = -7;
    TEST_ASSERT_EQ_INT32(v, -7);
    TEST_ASSERT_EQ_INT32(INT32_MAX, INT32_MAX);
    TEST_ASSERT_EQ_INT32(INT32_MIN, INT32_MIN);
})

TEST(assert_eq_int64, {
    const int64_t v = INT64_MIN;
    TEST_ASSERT_EQ_INT64(v, INT64_MIN);
    TEST_ASSERT_EQ_INT64(INT64_MAX, INT64_MAX);
    TEST_ASSERT_EQ_INT64(v + 1, INT64_MIN + 1);
})

TEST(assert_eq_size, {
    const size_t n = strlen("four");
    TEST_ASSERT_EQ_SIZE(n, (size_t)4);
    TEST_ASSERT_EQ_SIZE(sizeof(int64_t), (size_t)8);
})

TEST(assert_eq_uint64, {
    const uint64_t v = UINT64_MAX;
    TEST_ASSERT_EQ_UINT64(v, UINT64_MAX);
    TEST_ASSERT_EQ_UINT64((uint64_t)0, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(v - 1, UINT64_MAX - 1);
})

TEST(assert_eq_str, {
    const char* text = "text";
    char buf[8];
    TEST_UNUSED(snprintf(buf, sizeof buf, "%s", "text"));
    TEST_ASSERT_EQ_STR(text, "text");
    TEST_ASSERT_EQ_STR(buf, text);
    TEST_ASSERT_EQ_STR("", "");
})

TEST(assert_ne_char, {
    const char c = 'x';
    TEST_ASSERT_NE_CHAR(c, 'y');
    TEST_ASSERT_NE_CHAR(c, '\0');
})

TEST(assert_ge_family, {
    const int32_t i = 3;
    const int64_t l = INT64_MAX;
    const size_t s = 10;
    TEST_ASSERT_GE_INT32(i, 3);
    TEST_ASSERT_GE_INT32(i, -3);
    TEST_ASSERT_GE_INT64(l, INT64_MAX);
    TEST_ASSERT_GE_INT64(l, INT64_MIN);
    TEST_ASSERT_GE_SIZE(s, (size_t)10);
    TEST_ASSERT_GE_SIZE(s, (size_t)1);
})

TEST(assert_le_family, {
    const int32_t i = 3;
    const int64_t l = INT64_MIN;
    const size_t s = 10;
    TEST_ASSERT_LE_INT32(i, 3);
    TEST_ASSERT_LE_INT32(i, INT32_MAX);
    TEST_ASSERT_LE_INT64(l, INT64_MIN);
    TEST_ASSERT_LE_INT64(l, (int64_t)0);
    TEST_ASSERT_LE_SIZE(s, (size_t)10);
    TEST_ASSERT_LE_SIZE(s, SIZE_MAX);
})

TEST(assert_operands_evaluated_once, {
    int calls = 0;
    TEST_ASSERT_EQ_INT32(++calls, 1);
    TEST_ASSERT_EQ_INT32(calls, 1);
})

TEST(error_nonzero_passes_on_zero, {
    TEST_ERROR_NONZERO(0);
    TEST_ERROR_NONZERO(1 - 1);
})

TEST(explicit_ok_returns_early, {
    TEST_OK();
    TEST_FAIL(); // unreachable
})

TEST(body_with_commas_inside_parentheses, {
    // A body is one macro argument: commas must sit inside parentheses.
    char buf[8];
    TEST_ASSERT_EQ_INT32(snprintf(buf, sizeof buf, "%d-%d", 1, 2), 3);
    TEST_ASSERT_EQ_INT32(strcmp(buf, "1-2"), 0);
})

// ---- negative tests: one macro each, run only under a prefix ---------------------

TEST(neg_true, { TEST_ASSERT_TRUE(1 == 2); })
TEST(neg_false, { TEST_ASSERT_FALSE(1 == 1); })
TEST(neg_null, { TEST_ASSERT_NULL(self_path); })
TEST(neg_nonnull, {
    const char* null_ptr = NULL;
    TEST_ASSERT_NONNULL(null_ptr);
})
TEST(neg_eq_char, { TEST_ASSERT_EQ_CHAR('a', 'b'); })
TEST(neg_eq_int32, { TEST_ASSERT_EQ_INT32((int32_t)1, (int32_t)2); })
TEST(neg_eq_int64, {
    const int64_t lo = INT64_MIN;
    const int64_t hi = INT64_MAX;
    TEST_ASSERT_EQ_INT64(lo, hi);
})
TEST(neg_eq_size, { TEST_ASSERT_EQ_SIZE((size_t)1, (size_t)2); })
TEST(neg_eq_uint64, {
    const uint64_t zero = 0;
    const uint64_t big = UINT64_MAX;
    TEST_ASSERT_EQ_UINT64(zero, big);
})
TEST(neg_eq_str, {
    const char* got = "got";
    TEST_ASSERT_EQ_STR(got, "want");
})
TEST(neg_ne_char, { TEST_ASSERT_NE_CHAR('a', 'a'); })
TEST(neg_ge_int32, { TEST_ASSERT_GE_INT32((int32_t)1, (int32_t)2); })
TEST(neg_ge_int64, {
    const int64_t lo = INT64_MIN;
    const int64_t hi = INT64_MAX;
    TEST_ASSERT_GE_INT64(lo, hi);
})
TEST(neg_ge_size, { TEST_ASSERT_GE_SIZE((size_t)1, (size_t)2); })
TEST(neg_le_int32, { TEST_ASSERT_LE_INT32((int32_t)2, (int32_t)1); })
TEST(neg_le_int64, {
    const int64_t lo = INT64_MIN;
    const int64_t hi = INT64_MAX;
    TEST_ASSERT_LE_INT64(hi, lo);
})
TEST(neg_le_size, { TEST_ASSERT_LE_SIZE((size_t)2, (size_t)1); })
TEST(neg_fail, { TEST_FAIL(); })
TEST(neg_error, { TEST_ERROR("deliberate %s %d", "error", 42); })
TEST(neg_error_plain, { TEST_ERROR("plain error"); })
TEST(neg_error_nonzero, { TEST_ERROR_NONZERO(5); })
TEST(neg_after_ok, {
    TEST_ASSERT_TRUE(true);
    TEST_FAIL();
})

// ---- spawn tests: exit statuses, messages and the prefix filter -----------------

// Runs the neg_* test called prefix and checks that the suite exits with the
// expected status and logs each of the expected substrings.
static test_result_t check_negative(const char* prefix,
                                    int expected_status,
                                    const char* expect1,
                                    const char* expect2) {
    char out[OUTPUT_MAX];
    const int status = run_self(prefix, out, sizeof out);
    if (status != expected_status) {
        TEST_UNUSED(fprintf(stderr,
                            "%s: expected exit status %d, got %d\noutput:\n%s",
                            prefix,
                            expected_status,
                            status,
                            out));
        return TEST_RESULT_FAIL;
    }
    if (strstr(out, expect1) == NULL || (expect2 != NULL && strstr(out, expect2) == NULL)) {
        TEST_UNUSED(fprintf(stderr,
                            "%s: expected \"%s\" and \"%s\" in output:\n%s",
                            prefix,
                            expect1,
                            expect2 == NULL ? "" : expect2,
                            out));
        return TEST_RESULT_FAIL;
    }
    return TEST_RESULT_OK;
}

#define CHECK_NEGATIVE(prefix, status, expect1, expect2)                                           \
    do {                                                                                           \
        if (check_negative(prefix, status, expect1, expect2) != TEST_RESULT_OK) {                  \
            return TEST_RESULT_FAIL;                                                               \
        }                                                                                          \
    } while (0)

TEST(spawn_passing_prefix_exits_0, {
    char out[OUTPUT_MAX];
    TEST_ASSERT_EQ_INT32(run_self("assert_true", out, sizeof out), 0);
    TEST_ASSERT_NONNULL(strstr(out, "RUN: selfcheck.assert_true_and_false\n"));
    TEST_ASSERT_NONNULL(strstr(out, "END: selfcheck.assert_true_and_false OK\n"));
})

TEST(spawn_prefix_filter_selects_only_matching_tests, {
    char out[OUTPUT_MAX];
    TEST_ASSERT_EQ_INT32(run_self("assert_eq_int", out, sizeof out), 0);
    TEST_ASSERT_NONNULL(strstr(out, "RUN: selfcheck.assert_eq_int32\n"));
    TEST_ASSERT_NONNULL(strstr(out, "RUN: selfcheck.assert_eq_int64\n"));
    TEST_ASSERT_NULL(strstr(out, "RUN: selfcheck.assert_eq_char"));
    TEST_ASSERT_NULL(strstr(out, "RUN: selfcheck.assert_true"));
    TEST_ASSERT_NULL(strstr(out, "RUN: selfcheck.spawn"));
    TEST_ASSERT_NULL(strstr(out, "RUN: selfcheck.neg_"));
})

TEST(spawn_unmatched_prefix_runs_nothing_and_exits_0, {
    char out[OUTPUT_MAX];
    TEST_ASSERT_EQ_INT32(run_self("no_such_test", out, sizeof out), 0);
    TEST_ASSERT_NULL(strstr(out, "RUN:"));
})

TEST(spawn_bool_failures_exit_1, {
    CHECK_NEGATIVE("neg_true", 1, "assertion failed: (1 == 2) == true", "actual:   false");
    CHECK_NEGATIVE("neg_false", 1, "assertion failed: (1 == 1) == false", "actual:   true");
})

TEST(spawn_pointer_failures_exit_1, {
    CHECK_NEGATIVE(
        "neg_null", 1, "assertion failed: self_path == NULL", "END: selfcheck.neg_null FAIL");
    CHECK_NEGATIVE("neg_nonnull", 1, "assertion failed: null_ptr != NULL", NULL);
})

TEST(spawn_eq_failures_exit_1, {
    CHECK_NEGATIVE("neg_eq_char", 1, "assertion failed: 'a' == 'b'", "actual:   a\n\texpected: b");
    CHECK_NEGATIVE("neg_eq_int32", 1, "(int32_t)1 == (int32_t)2", "actual:   1\n\texpected: 2");
    CHECK_NEGATIVE("neg_eq_int64",
                   1,
                   "assertion failed: lo == hi",
                   "actual:   -9223372036854775808\n\texpected: 9223372036854775807");
    CHECK_NEGATIVE("neg_eq_size", 1, "(size_t)1 == (size_t)2", "actual:   1\n\texpected: 2");
    CHECK_NEGATIVE("neg_eq_uint64",
                   1,
                   "assertion failed: zero == big",
                   "actual:   0\n\texpected: 18446744073709551615");
    CHECK_NEGATIVE("neg_eq_str",
                   1,
                   "assertion failed: got == \"want\"",
                   "actual:   \"got\"\n\texpected: \"want\"");
})

TEST(spawn_ne_failure_exits_1,
     { CHECK_NEGATIVE("neg_ne_char", 1, "assertion failed: 'a' != 'a'", "actual:   a"); })

TEST(spawn_ge_failures_exit_1, {
    CHECK_NEGATIVE("neg_ge_int32", 1, "(int32_t)1 >= (int32_t)2", "actual:   1");
    CHECK_NEGATIVE(
        "neg_ge_int64", 1, "assertion failed: lo >= hi", "actual:   -9223372036854775808");
    CHECK_NEGATIVE("neg_ge_size", 1, "(size_t)1 >= (size_t)2", "expected: 2");
})

TEST(spawn_le_failures_exit_1, {
    CHECK_NEGATIVE("neg_le_int32", 1, "(int32_t)2 <= (int32_t)1", "actual:   2");
    CHECK_NEGATIVE(
        "neg_le_int64", 1, "assertion failed: hi <= lo", "actual:   9223372036854775807");
    CHECK_NEGATIVE("neg_le_size", 1, "(size_t)2 <= (size_t)1", "expected: 1");
})

TEST(spawn_test_fail_exits_1, {
    CHECK_NEGATIVE("neg_fail", 1, "END: selfcheck.neg_fail FAIL", NULL);
    CHECK_NEGATIVE("neg_after_ok", 1, "END: selfcheck.neg_after_ok FAIL", NULL);
})

TEST(spawn_test_error_exits_2, {
    CHECK_NEGATIVE(
        "neg_error", 2, "test error: deliberate error 42", "END: selfcheck.neg_error ERROR");
    CHECK_NEGATIVE("neg_error_plain", 2, "test error: plain error", NULL);
    CHECK_NEGATIVE("neg_error_nonzero", 2, "test error: 5 == 0", "actual: 5");
})

TEST(spawn_log_names_file_and_line, {
    char out[OUTPUT_MAX];
    TEST_ASSERT_EQ_INT32(run_self("neg_fail", out, sizeof out), 1);
    // TEST_FAIL logs nothing; an assertion logs "<file>:<line>".
    TEST_ASSERT_NULL(strstr(out, "selfcheck_test.c:"));
    TEST_ASSERT_EQ_INT32(run_self("neg_true", out, sizeof out), 1);
    TEST_ASSERT_NONNULL(strstr(out, "selfcheck_test.c:"));
})

TEST(spawn_error_outranks_failure, {
    // Every neg_e* test: neg_eq_* fail and neg_error* error, so the suite
    // exits with 2.
    char out[OUTPUT_MAX];
    TEST_ASSERT_EQ_INT32(run_self("neg_e", out, sizeof out), 2);
    TEST_ASSERT_NONNULL(strstr(out, "END: selfcheck.neg_eq_char FAIL"));
    TEST_ASSERT_NONNULL(strstr(out, "END: selfcheck.neg_error ERROR"));
})

TEST(spawn_error_outranks_a_later_failure, {
    // Every neg_* test: the last one registered, neg_after_ok, fails after
    // the neg_error* tests errored; the suite still exits with 2.
    char out[OUTPUT_MAX];
    TEST_ASSERT_EQ_INT32(run_self("neg_", out, sizeof out), 2);
    TEST_ASSERT_NONNULL(strstr(out, "END: selfcheck.neg_error_nonzero ERROR"));
    TEST_ASSERT_NONNULL(strstr(out, "END: selfcheck.neg_after_ok FAIL"));
})

int main(int argc, char** argv) {
    TEST_INIT("selfcheck", argc, argv);
    self_path = argv[0];

    TEST_RUN(assert_true_and_false);
    TEST_RUN(assert_null_and_nonnull);
    TEST_RUN(assert_eq_char);
    TEST_RUN(assert_eq_int32);
    TEST_RUN(assert_eq_uint64);
    TEST_RUN(assert_eq_str);
    TEST_RUN(assert_eq_int64);
    TEST_RUN(assert_eq_size);
    TEST_RUN(assert_ne_char);
    TEST_RUN(assert_ge_family);
    TEST_RUN(assert_le_family);
    TEST_RUN(assert_operands_evaluated_once);
    TEST_RUN(error_nonzero_passes_on_zero);
    TEST_RUN(explicit_ok_returns_early);
    TEST_RUN(body_with_commas_inside_parentheses);

    TEST_RUN(spawn_passing_prefix_exits_0);
    TEST_RUN(spawn_prefix_filter_selects_only_matching_tests);
    TEST_RUN(spawn_unmatched_prefix_runs_nothing_and_exits_0);
    TEST_RUN(spawn_bool_failures_exit_1);
    TEST_RUN(spawn_pointer_failures_exit_1);
    TEST_RUN(spawn_eq_failures_exit_1);
    TEST_RUN(spawn_ne_failure_exits_1);
    TEST_RUN(spawn_ge_failures_exit_1);
    TEST_RUN(spawn_le_failures_exit_1);
    TEST_RUN(spawn_test_fail_exits_1);
    TEST_RUN(spawn_test_error_exits_2);
    TEST_RUN(spawn_log_names_file_and_line);
    TEST_RUN(spawn_error_outranks_failure);
    TEST_RUN(spawn_error_outranks_a_later_failure);

    // The negative tests are selected by prefix only.
    if (argc > 1) {
        TEST_RUN(neg_true);
        TEST_RUN(neg_false);
        TEST_RUN(neg_null);
        TEST_RUN(neg_nonnull);
        TEST_RUN(neg_eq_char);
        TEST_RUN(neg_eq_int32);
        TEST_RUN(neg_eq_int64);
        TEST_RUN(neg_eq_size);
        TEST_RUN(neg_eq_uint64);
        TEST_RUN(neg_eq_str);
        TEST_RUN(neg_ne_char);
        TEST_RUN(neg_ge_int32);
        TEST_RUN(neg_ge_int64);
        TEST_RUN(neg_ge_size);
        TEST_RUN(neg_le_int32);
        TEST_RUN(neg_le_int64);
        TEST_RUN(neg_le_size);
        TEST_RUN(neg_fail);
        TEST_RUN(neg_error);
        TEST_RUN(neg_error_plain);
        TEST_RUN(neg_error_nonzero);
        TEST_RUN(neg_after_ok);
    }
    TEST_EXIT();
}
