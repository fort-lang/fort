// Tests allocation helpers and fatal memory errors.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "common/fork.h"
#include "str.h"

#include "common/test.h"

enum { ERR_MAX = 256 };

// ---- fatal errors and allocation ---------------------------------------------

static void call_fatal_oom(void) {
    fatal_oom();
}

static void call_fatal_internal(void) {
    fatal_internal("something broke");
}

TEST(fatal_oom_prints_the_message_and_exits_2, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(call_fatal_oom, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
    TEST_ASSERT_EQ_INT32(FATAL_EXIT_STATUS, 2);
})

TEST(fatal_internal_prints_the_reason_and_exits_2, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(call_fatal_internal, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: something broke\n");
})

TEST(mem_alloc_returns_zeroed_storage, {
    const size_t n = 64;
    unsigned char* p = mem_alloc(n);
    TEST_ASSERT_NONNULL(p);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQ_INT32(p[i], 0);
    }
    mem_free(p);
})

TEST(mem_alloc_of_zero_bytes_is_a_distinct_non_null_block, {
    void* a = mem_alloc(0);
    void* b = mem_alloc(0);
    TEST_ASSERT_NONNULL(a);
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_TRUE(a != b);
    mem_free(a);
    mem_free(b);
})

TEST(mem_free_of_null_is_a_no_op, { mem_free(NULL); })

TEST(mem_add_and_mul_compute_in_range_values, {
    TEST_ASSERT_EQ_UINT64(mem_add(0, 0), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(mem_add(1, 2), (uint64_t)3);
    TEST_ASSERT_EQ_UINT64(mem_add(UINT64_MAX - 1, 1), UINT64_MAX);
    TEST_ASSERT_EQ_UINT64(mem_add(UINT64_MAX, 0), UINT64_MAX);
    TEST_ASSERT_EQ_UINT64(mem_mul(0, UINT64_MAX), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(mem_mul(UINT64_MAX, 0), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(mem_mul(3, 4), (uint64_t)12);
    TEST_ASSERT_EQ_UINT64(mem_mul(UINT64_MAX / 2, 2), UINT64_MAX - 1);
    TEST_ASSERT_EQ_UINT64(mem_mul(1, UINT64_MAX), UINT64_MAX);
})

static void add_overflow(void) {
    TEST_UNUSED(mem_add(UINT64_MAX, 1));
}

static void mul_overflow(void) {
    TEST_UNUSED(mem_mul(UINT64_MAX / 2 + 1, 2));
}

TEST(mem_add_overflow_is_out_of_memory, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(add_overflow, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
})

TEST(mem_mul_overflow_is_out_of_memory, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(mul_overflow, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
})

TEST(mem_grown_cap_is_the_largest_of_minimum_double_and_need, {
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(0, 0), (uint64_t)MEM_MIN_CAP);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(0, 1), (uint64_t)MEM_MIN_CAP);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(0, 16), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(0, 17), (uint64_t)17);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(16, 17), (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(8, 9), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(9, 10), (uint64_t)18);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(100, 101), (uint64_t)200);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(100, 1000), (uint64_t)1000);
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(1024, 1025), (uint64_t)2048);
    // The largest capacity whose double still fits.
    TEST_ASSERT_EQ_UINT64(mem_grown_cap(UINT64_MAX / 2, 1), UINT64_MAX - 1);
})

static void grown_cap_overflow(void) {
    TEST_UNUSED(mem_grown_cap(UINT64_MAX / 2 + 1, 1));
}

TEST(mem_grown_cap_doubling_overflow_is_out_of_memory, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(grown_cap_overflow, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
})

int main(int argc, char** argv) {
    TEST_INIT("mem", argc, argv);

    TEST_RUN(fatal_oom_prints_the_message_and_exits_2);
    TEST_RUN(fatal_internal_prints_the_reason_and_exits_2);
    TEST_RUN(mem_alloc_returns_zeroed_storage);
    TEST_RUN(mem_alloc_of_zero_bytes_is_a_distinct_non_null_block);
    TEST_RUN(mem_free_of_null_is_a_no_op);
    TEST_RUN(mem_add_and_mul_compute_in_range_values);
    TEST_RUN(mem_add_overflow_is_out_of_memory);
    TEST_RUN(mem_mul_overflow_is_out_of_memory);
    TEST_RUN(mem_grown_cap_is_the_largest_of_minimum_double_and_need);
    TEST_RUN(mem_grown_cap_doubling_overflow_is_out_of_memory);

    TEST_EXIT();
}
