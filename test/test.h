/* The unit-test framework of the bootstrap compiler (toolchain.md 7.5).
 *
 * A suite defines tests with TEST(name, body), registers them with TEST_RUN in
 * main after TEST_INIT, and exits through TEST_EXIT with 0 (every test ok),
 * 1 (an assertion failed) or 2 (a test error). The first command-line argument,
 * when present, is a name prefix selecting the tests to run. */
#ifndef FORT_TEST_H
#define FORT_TEST_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "common.h" // for TEST_UNUSED

typedef enum {
    TEST_RESULT_OK = 0,
    TEST_RESULT_FAIL = 1,
    TEST_RESULT_ERR = 2,
} test_result_t;

#define TEST_RESULT_ test_result_t

#define TEST_SUITENAME_ test_suite_name
#define TEST_FINAL_RESULT_ test_final_result
#define TEST_FILTER_TEST_PREFIX_ test_filter_test_prefix

#define TEST_INIT_(suite_name, filter_test_prefix)                                                 \
    const char* TEST_SUITENAME_ = (suite_name);                                                    \
    TEST_RESULT_ TEST_FINAL_RESULT_ = TEST_RESULT_OK;                                              \
    const char* TEST_FILTER_TEST_PREFIX_ = (filter_test_prefix)

/* The suite result is the worst test result: an error outranks a failure,
 * whatever the order the tests ran in. */
#define TEST_FINAL_RESULT_UPDATE_(test_result)                                                     \
    do {                                                                                           \
        if ((test_result) > TEST_FINAL_RESULT_) {                                                  \
            TEST_FINAL_RESULT_ = (test_result);                                                    \
        }                                                                                          \
    } while (0)

/* The process exit status of a suite result: 0 ok, 1 fail, 2 error. */
static inline int test_exit_status(test_result_t final_result) {
    switch (final_result) {
    case TEST_RESULT_OK:
        return 0;
    case TEST_RESULT_FAIL:
        return 1;
    case TEST_RESULT_ERR:
        return 2;
    }
    return 2;
}

#define TEST_EXIT_(final_result) return test_exit_status(final_result)

#define TEST_INIT(suite_name, argc, argv) TEST_INIT_(suite_name, (argc) > 1 ? (argv)[1] : NULL)

#define TEST_EXIT() TEST_EXIT_(TEST_FINAL_RESULT_)

/* The name of a test result as printed by TEST_RUN. */
static inline const char* test_result_name(test_result_t result) {
    switch (result) {
    case TEST_RESULT_OK:
        return "OK";
    case TEST_RESULT_FAIL:
        return "FAIL";
    case TEST_RESULT_ERR:
        return "ERROR";
    }
    return "UNKNOWN";
}

#define TEST_RUN(test_function)                                                                    \
    do {                                                                                           \
        if (TEST_FILTER_TEST_PREFIX_ != NULL &&                                                    \
            (strncmp(TEST_FILTER_TEST_PREFIX_,                                                     \
                     #test_function,                                                               \
                     strlen(TEST_FILTER_TEST_PREFIX_)) != 0)) {                                    \
            break;                                                                                 \
        }                                                                                          \
        TEST_UNUSED(fprintf(stderr, "RUN: %s.%s\n", TEST_SUITENAME_, #test_function));             \
        const TEST_RESULT_ test_result = (test_function)();                                        \
        TEST_FINAL_RESULT_UPDATE_(test_result);                                                    \
        TEST_UNUSED(fprintf(stderr,                                                                \
                            "END: %s.%s %s\n\n",                                                   \
                            TEST_SUITENAME_,                                                       \
                            #test_function,                                                        \
                            test_result_name(test_result)));                                       \
    } while (0)

#define TEST(test_name, test_body)                                                                 \
    static __attribute__((noinline)) TEST_RESULT_ test_name(void) {                                \
        test_body;                                                                                 \
        TEST_OK();                                                                                 \
    }

/* TEST_LOG_(fmt, args...) writes "<file>:<line>" and the message on the next
 * line. The whole printf argument list is the variadic part, so a call with a
 * bare format string is well-formed C11. */
#define TEST_LOG_(...)                                                                             \
    do {                                                                                           \
        TEST_UNUSED(fprintf(stderr, "%s:%d\n\t", __FILE__, __LINE__));                             \
        TEST_UNUSED(fprintf(stderr, __VA_ARGS__));                                                 \
        TEST_UNUSED(fputc('\n', stderr));                                                          \
    } while (0)

#define TEST_ASSERT_EQ_BOOL_(val, exp)                                                             \
    do {                                                                                           \
        if ((val) != (exp)) {                                                                      \
            TEST_LOG_("assertion failed: (%s) == %s\n"                                             \
                      "\tactual:   %s\n"                                                           \
                      "\texpected: %s",                                                            \
                      #val,                                                                        \
                      #exp,                                                                        \
                      (val) ? "true" : "false",                                                    \
                      (exp) ? "true" : "false");                                                   \
            return TEST_RESULT_FAIL;                                                               \
        }                                                                                          \
    } while (0)

#define TEST_ASSERT_TRUE(expr) TEST_ASSERT_EQ_BOOL_(expr, true)
#define TEST_ASSERT_FALSE(expr) TEST_ASSERT_EQ_BOOL_(expr, false)

#define TEST_ASSERT_NULL(expr)                                                                     \
    do {                                                                                           \
        if ((expr) != NULL) {                                                                      \
            TEST_LOG_("assertion failed: %s == NULL", #expr);                                      \
            return TEST_RESULT_FAIL;                                                               \
        }                                                                                          \
    } while (0)

#define TEST_ASSERT_NONNULL(expr)                                                                  \
    do {                                                                                           \
        if ((expr) == NULL) {                                                                      \
            TEST_LOG_("assertion failed: %s != NULL", #expr);                                      \
            return TEST_RESULT_FAIL;                                                               \
        }                                                                                          \
    } while (0)

#define TEST_ASSERT_OP_(val, exp, cond, op, fmt)                                                   \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            TEST_LOG_("assertion failed: %s %s %s\n"                                               \
                      "\tactual:   " fmt "\n"                                                      \
                      "\texpected: " fmt,                                                          \
                      #val,                                                                        \
                      op,                                                                          \
                      #exp,                                                                        \
                      val,                                                                         \
                      exp);                                                                        \
            return TEST_RESULT_FAIL;                                                               \
        }                                                                                          \
    } while (0)

#define TEST_ASSERT_EQ_(val, exp, fmt) TEST_ASSERT_OP_(val, exp, (val) == (exp), "==", fmt)
#define TEST_ASSERT_EQ_CHAR(val, exp) TEST_ASSERT_EQ_(val, exp, "%c")
#define TEST_ASSERT_EQ_INT32(val, exp) TEST_ASSERT_EQ_(val, exp, "%" PRId32)
#define TEST_ASSERT_EQ_INT64(val, exp) TEST_ASSERT_EQ_(val, exp, "%" PRId64)
#define TEST_ASSERT_EQ_SIZE(val, exp) TEST_ASSERT_EQ_(val, exp, "%zu")
#define TEST_ASSERT_EQ_UINT64(val, exp) TEST_ASSERT_EQ_(val, exp, "%" PRIu64)

/* Equality of two NUL-terminated strings, neither NULL. */
#define TEST_ASSERT_EQ_STR(val, exp)                                                               \
    TEST_ASSERT_OP_(val, exp, strcmp((val), (exp)) == 0, "==", "\"%s\"")

#define TEST_ASSERT_NE_(val, exp, fmt) TEST_ASSERT_OP_(val, exp, (val) != (exp), "!=", fmt)
#define TEST_ASSERT_NE_CHAR(val, exp) TEST_ASSERT_NE_(val, exp, "%c")

#define TEST_ASSERT_GE_(val, exp, fmt) TEST_ASSERT_OP_(val, exp, (val) >= (exp), ">=", fmt)
#define TEST_ASSERT_GE_INT32(val, exp) TEST_ASSERT_GE_(val, exp, "%" PRId32)
#define TEST_ASSERT_GE_INT64(val, exp) TEST_ASSERT_GE_(val, exp, "%" PRId64)
#define TEST_ASSERT_GE_SIZE(val, exp) TEST_ASSERT_GE_(val, exp, "%zu")

#define TEST_ASSERT_LE_(val, exp, fmt) TEST_ASSERT_OP_(val, exp, (val) <= (exp), "<=", fmt)
#define TEST_ASSERT_LE_INT32(val, exp) TEST_ASSERT_LE_(val, exp, "%" PRId32)
#define TEST_ASSERT_LE_INT64(val, exp) TEST_ASSERT_LE_(val, exp, "%" PRId64)
#define TEST_ASSERT_LE_SIZE(val, exp) TEST_ASSERT_LE_(val, exp, "%zu")

#define TEST_OK() return TEST_RESULT_OK

#define TEST_FAIL() return TEST_RESULT_FAIL

/* TEST_ERROR(fmt, args...) logs "test error: " and the message, then returns
 * TEST_RESULT_ERR. */
#define TEST_ERROR(...)                                                                            \
    do {                                                                                           \
        TEST_UNUSED(fprintf(stderr, "%s:%d\n\ttest error: ", __FILE__, __LINE__));                 \
        TEST_UNUSED(fprintf(stderr, __VA_ARGS__));                                                 \
        TEST_UNUSED(fputc('\n', stderr));                                                          \
        return TEST_RESULT_ERR;                                                                    \
    } while (0)

#define TEST_ERROR_NONZERO(expr)                                                                   \
    do {                                                                                           \
        if ((expr) != 0) {                                                                         \
            TEST_ERROR("%s == 0\n\tactual: %" PRId32 "\n", #expr, expr);                           \
        }                                                                                          \
    } while (0)

#endif
