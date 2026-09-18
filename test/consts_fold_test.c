// Tests constant folding and arithmetic limits.
#include <stdint.h>

#include "consts.h"
#include "prim.h"

#include "test.h"

// The sample values below are the test data: the boundaries of the constant
// range and the examples of the type rules.
// NOLINTBEGIN(readability-magic-numbers)

static const uint64_t TWO63 = UINT64_C(1) << 63U; // 2^63
static const uint64_t U64_MAX = UINT64_MAX;       // 2^64 - 1

static cval_t i(int64_t v) {
    return cv_from_i64(v);
}

static cval_t u(uint64_t v) {
    return cv_from_u64(v);
}

// -2^63, the most negative constant.
static cval_t neg_two63(void) {
    return cv_from_i64(INT64_MIN);
}

static bool is_int(cval_t v, int64_t expect) {
    return v.kind == CV_INT && cv_eq(v, cv_from_i64(expect));
}

static bool is_uint(cval_t v, uint64_t expect) {
    return v.kind == CV_INT && cv_eq(v, cv_from_u64(expect));
}

// ---- addition and subtraction ---------------------------------------------------

TEST(add_folds_small_values, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_add(i(1), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, 3));
    TEST_ASSERT_TRUE(cv_add(i(-1), i(-2), &r));
    TEST_ASSERT_TRUE(is_int(r, -3));
    TEST_ASSERT_TRUE(cv_add(i(5), i(-7), &r));
    TEST_ASSERT_TRUE(is_int(r, -2));
    TEST_ASSERT_TRUE(cv_add(i(-5), i(7), &r));
    TEST_ASSERT_TRUE(is_int(r, 2));
    TEST_ASSERT_TRUE(cv_add(i(-5), i(5), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
})

TEST(add_reaches_u64_max_exactly, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_add(u(U64_MAX - 1U), i(1), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_add(i(INT64_MAX), i(INT64_MAX), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX - 1U));
    TEST_ASSERT_TRUE(cv_add(u(TWO63), u(TWO63 - 1U), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
})

TEST(add_one_past_u64_max_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_add(u(U64_MAX), i(1), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_add(u(TWO63), u(TWO63), &r));
    TEST_ASSERT_FALSE(cv_add(u(U64_MAX), u(U64_MAX), &r));
})

TEST(add_reaches_neg_two63_exactly, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_add(i(INT64_MIN + 1), i(-1), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_add(i(INT64_MIN / 2), i(INT64_MIN / 2), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
})

TEST(add_one_below_neg_two63_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_add(neg_two63(), i(-1), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_add(neg_two63(), neg_two63(), &r));
})

TEST(add_of_u64_max_and_a_negative_comes_back_into_range, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_add(u(U64_MAX), i(-1), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX - 1U));
    TEST_ASSERT_TRUE(cv_add(u(U64_MAX), neg_two63(), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63 - 1U));
})

TEST(sub_folds_small_values, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_sub(i(5), i(7), &r));
    TEST_ASSERT_TRUE(is_int(r, -2));
    TEST_ASSERT_TRUE(cv_sub(i(7), i(5), &r));
    TEST_ASSERT_TRUE(is_int(r, 2));
    TEST_ASSERT_TRUE(cv_sub(i(-5), i(-7), &r));
    TEST_ASSERT_TRUE(is_int(r, 2));
    TEST_ASSERT_TRUE(cv_sub(i(3), i(3), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
})

TEST(sub_at_the_boundaries, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_sub(i(0), u(TWO63), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_sub(u(U64_MAX), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_sub(i(INT64_MAX), i(INT64_MIN), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_sub(u(TWO63 - 1U), i(-1), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
})

TEST(sub_outside_the_range_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_sub(i(0), u(TWO63 + 1U), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_sub(neg_two63(), i(1), &r));
    TEST_ASSERT_FALSE(cv_sub(u(U64_MAX), i(-1), &r));
    TEST_ASSERT_FALSE(cv_sub(i(0), u(U64_MAX), &r));
    TEST_ASSERT_FALSE(cv_sub(i(-1), u(U64_MAX), &r));
})

// ---- multiplication -------------------------------------------------------------

TEST(mul_folds_with_the_sign_of_the_product, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_mul(i(6), i(7), &r));
    TEST_ASSERT_TRUE(is_int(r, 42));
    TEST_ASSERT_TRUE(cv_mul(i(-6), i(7), &r));
    TEST_ASSERT_TRUE(is_int(r, -42));
    TEST_ASSERT_TRUE(cv_mul(i(-6), i(-7), &r));
    TEST_ASSERT_TRUE(is_int(r, 42));
    TEST_ASSERT_TRUE(cv_mul(i(-6), i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
})

TEST(mul_reaches_the_boundaries, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_mul(u(TWO63), i(1), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_mul(u(TWO63), i(-1), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_mul(u(UINT64_C(0xFFFFFFFF)), u(UINT64_C(0x100000001)), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_mul(i(INT64_MIN / 2), i(2), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_mul(neg_two63(), i(-1), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
})

TEST(mul_outside_the_range_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_mul(u(TWO63), i(2), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_mul(u(TWO63 + 1U), i(-1), &r));
    TEST_ASSERT_FALSE(cv_mul(u(U64_MAX), i(-1), &r));
    TEST_ASSERT_FALSE(cv_mul(neg_two63(), neg_two63(), &r));
    TEST_ASSERT_FALSE(cv_mul(neg_two63(), i(2), &r));
    TEST_ASSERT_FALSE(cv_mul(u(UINT64_C(0x100000000)), u(UINT64_C(0x100000000)), &r));
})

// ---- division and remainder ------------------------------------------------------

TEST(div_truncates_toward_zero, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_div(i(7), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, 3));
    TEST_ASSERT_TRUE(cv_div(i(7), i(-2), &r));
    TEST_ASSERT_TRUE(is_int(r, -3));
    TEST_ASSERT_TRUE(cv_div(i(-7), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, -3));
    TEST_ASSERT_TRUE(cv_div(i(-7), i(-2), &r));
    TEST_ASSERT_TRUE(is_int(r, 3));
})

TEST(div_of_one_by_two_is_zero_d4_4, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_div(i(1), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_div(i(-1), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
})

TEST(div_by_zero_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_div(i(1), i(0), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_div(i(0), i(0), &r));
    TEST_ASSERT_FALSE(cv_div(i(-1), i(0), &r));
})

TEST(div_at_the_boundaries, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_div(neg_two63(), i(-1), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_div(u(U64_MAX), i(1), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_div(u(U64_MAX), i(-2), &r));
    TEST_ASSERT_TRUE(is_int(r, -(INT64_MAX)));
    TEST_ASSERT_TRUE(cv_div(u(TWO63), i(-1), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
})

TEST(div_of_u64_max_by_minus_one_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_div(u(U64_MAX), i(-1), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_div(u(TWO63 + 1U), i(-1), &r));
})

TEST(rem_takes_the_sign_of_the_dividend, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_rem(i(7), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_rem(i(7), i(-2), &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_rem(i(-7), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_rem(i(-7), i(-2), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
})

TEST(rem_of_an_exact_multiple_is_a_non_negative_zero, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_rem(i(-6), i(3), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
    TEST_ASSERT_TRUE(cv_rem(neg_two63(), i(-1), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(rem_by_zero_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_rem(i(1), i(0), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_rem(i(-1), i(0), &r));
})

TEST(rem_at_the_boundaries, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_rem(u(U64_MAX), u(TWO63), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63 - 1U));
    TEST_ASSERT_TRUE(cv_rem(neg_two63(), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_rem(neg_two63(), i(10), &r));
    TEST_ASSERT_TRUE(is_int(r, -8));
})

// ---- negation and complement ---------------------------------------------------

TEST(neg_flips_the_sign_and_keeps_zero_positive, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_neg(i(5), &r));
    TEST_ASSERT_TRUE(is_int(r, -5));
    TEST_ASSERT_TRUE(cv_neg(i(-5), &r));
    TEST_ASSERT_TRUE(is_int(r, 5));
    TEST_ASSERT_TRUE(cv_neg(i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
})

TEST(neg_at_the_boundaries, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_neg(u(TWO63), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_neg(neg_two63(), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_neg(i(INT64_MAX), &r));
    TEST_ASSERT_TRUE(is_int(r, INT64_MIN + 1));
})

TEST(neg_of_a_value_above_two63_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_neg(u(TWO63 + 1U), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_neg(u(U64_MAX), &r));
})

TEST(not_is_minus_c_minus_one_d4_4, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_not(i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_not(i(-1), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
    TEST_ASSERT_TRUE(cv_not(i(5), &r));
    TEST_ASSERT_TRUE(is_int(r, -6));
    TEST_ASSERT_TRUE(cv_not(i(-6), &r));
    TEST_ASSERT_TRUE(is_int(r, 5));
})

TEST(not_at_the_boundaries, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_not(i(INT64_MAX), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_not(neg_two63(), &r));
    TEST_ASSERT_TRUE(is_int(r, INT64_MAX));
    TEST_ASSERT_TRUE(cv_not(u(0xFFFFFFFFU), &r));
    TEST_ASSERT_TRUE(is_int(r, -UINT64_C(0x100000000)));
})

TEST(not_of_two63_or_more_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_not(u(TWO63), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_not(u(U64_MAX), &r));
})

// ---- bitwise operators: infinite two's complement --------------------------------

TEST(and_of_non_negatives_is_the_plain_bit_and, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_and(i(0xF0), i(0x3C), &r));
    TEST_ASSERT_TRUE(is_int(r, 0x30));
    TEST_ASSERT_TRUE(cv_and(u(U64_MAX), u(TWO63), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_and(i(0), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(and_with_minus_one_is_the_identity_even_above_i64, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_and(i(-1), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_and(u(TWO63), i(-1), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_and(i(-1), i(-1), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
})

TEST(and_of_two_negatives_is_negative, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_and(i(-2), i(-3), &r));
    TEST_ASSERT_TRUE(is_int(r, -4));
    TEST_ASSERT_TRUE(cv_and(neg_two63(), i(-1), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_and(i(-8), i(7), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_FALSE(r.neg);
})

TEST(and_never_leaves_the_range, {
    // A negative in-range operand has bit 63 set in its pattern. The result of `&` with a negative
    // operand is at least -2^63.
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_and(neg_two63(), i(-2), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_and(neg_two63(), neg_two63(), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_and(neg_two63(), i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_and(neg_two63(), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
})

TEST(or_of_non_negatives_is_the_plain_bit_or, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_or(i(0xF0), i(0x0F), &r));
    TEST_ASSERT_TRUE(is_int(r, 0xFF));
    TEST_ASSERT_TRUE(cv_or(u(TWO63), u(TWO63 - 1U), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
})

TEST(or_with_a_negative_is_negative, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_or(i(-1), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_or(i(-4), i(1), &r));
    TEST_ASSERT_TRUE(is_int(r, -3));
    TEST_ASSERT_TRUE(cv_or(neg_two63(), u(TWO63), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_or(neg_two63(), i(INT64_MAX), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
})

TEST(or_never_leaves_the_range, {
    // As for `&`: a negative operand keeps bit 63 set in the result.
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_or(neg_two63(), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_or(neg_two63(), neg_two63(), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_or(neg_two63(), i(0), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_or(i(-1), neg_two63(), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
})

TEST(xor_of_non_negatives_is_the_plain_bit_xor, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_xor(i(0xFF), i(0x0F), &r));
    TEST_ASSERT_TRUE(is_int(r, 0xF0));
    TEST_ASSERT_TRUE(cv_xor(u(U64_MAX), u(TWO63), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63 - 1U));
    TEST_ASSERT_TRUE(cv_xor(u(U64_MAX), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(xor_with_minus_one_is_the_complement, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_xor(i(-1), i(5), &r));
    TEST_ASSERT_TRUE(is_int(r, -6));
    TEST_ASSERT_TRUE(cv_xor(i(-1), i(-1), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_xor(i(-2), i(1), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_xor(i(-1), i(INT64_MAX), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
})

TEST(xor_below_neg_two63_is_an_error, {
    cval_t r = i(1);
    // -1 ^ (2^64 - 1) is -2^64.
    TEST_ASSERT_FALSE(cv_xor(i(-1), u(U64_MAX), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    // -2^63 ^ (2^64 - 1) is -2^63 - 1.
    TEST_ASSERT_FALSE(cv_xor(neg_two63(), u(U64_MAX), &r));
    // -1 ^ 2^63 is -2^63 - 1.
    TEST_ASSERT_FALSE(cv_xor(i(-1), u(TWO63), &r));
})

// ---- shifts ---------------------------------------------------------------------

TEST(shl_multiplies_by_a_power_of_two, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_shl(i(1), i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_shl(i(1), i(4), &r));
    TEST_ASSERT_TRUE(is_int(r, 16));
    TEST_ASSERT_TRUE(cv_shl(i(3), i(2), &r));
    TEST_ASSERT_TRUE(is_int(r, 12));
    TEST_ASSERT_TRUE(cv_shl(i(-1), i(1), &r));
    TEST_ASSERT_TRUE(is_int(r, -2));
    TEST_ASSERT_TRUE(cv_shl(i(0), i(63), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(shl_one_by_63_is_two63, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_shl(i(1), i(63), &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_shl(i(-1), i(63), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_shl(u(U64_MAX), i(0), &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
})

TEST(shl_one_by_64_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_shl(i(1), i(64), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_shl(i(0), i(64), &r));
    TEST_ASSERT_FALSE(cv_shl(i(1), i(100), &r));
    TEST_ASSERT_FALSE(cv_shl(i(1), u(U64_MAX), &r));
})

TEST(shl_by_a_negative_count_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_shl(i(1), i(-1), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_shr(i(1), i(-1), &r));
})

TEST(shl_losing_a_bit_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_shl(i(2), i(63), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_shl(u(TWO63), i(1), &r));
    TEST_ASSERT_FALSE(cv_shl(u(U64_MAX), i(1), &r));
    TEST_ASSERT_FALSE(cv_shl(i(-2), i(63), &r));
    TEST_ASSERT_FALSE(cv_shl(neg_two63(), i(1), &r));
    TEST_ASSERT_FALSE(cv_shl(i(3), i(63), &r));
})

TEST(shr_is_a_floor_division_by_a_power_of_two, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_shr(i(16), i(4), &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_shr(i(17), i(4), &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_shr(i(7), i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, 7));
    TEST_ASSERT_TRUE(cv_shr(u(U64_MAX), i(63), &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_shr(u(TWO63), i(63), &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_shr(u(TWO63 - 1U), i(63), &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(shr_of_a_negative_is_arithmetic, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_shr(i(-1), i(1), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_shr(i(-1), i(63), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_shr(i(-2), i(1), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_shr(i(-3), i(1), &r));
    TEST_ASSERT_TRUE(is_int(r, -2));
    TEST_ASSERT_TRUE(cv_shr(i(-16), i(4), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_shr(i(-17), i(4), &r));
    TEST_ASSERT_TRUE(is_int(r, -2));
    TEST_ASSERT_TRUE(cv_shr(neg_two63(), i(63), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_shr(neg_two63(), i(1), &r));
    TEST_ASSERT_TRUE(is_int(r, INT64_MIN / 2));
    // A count of 0 rounds nothing up: `x >> 0` is `x` for a negative x too.
    TEST_ASSERT_TRUE(cv_shr(i(-1), i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_shr(i(-7), i(0), &r));
    TEST_ASSERT_TRUE(is_int(r, -7));
    TEST_ASSERT_TRUE(cv_shr(neg_two63(), i(0), &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
})

TEST(shr_by_64_or_more_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_shr(i(1), i(64), &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_shr(i(0), i(64), &r));
    TEST_ASSERT_FALSE(cv_shr(i(-1), i(64), &r));
})

TEST(one_shl_40_shr_38_is_four_type_system_10_4, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_shl(i(1), i(40), &r));
    TEST_ASSERT_TRUE(is_int(r, INT64_C(1099511627776)));
    TEST_ASSERT_TRUE(cv_shr(r, i(38), &r));
    TEST_ASSERT_TRUE(is_int(r, 4));
    TEST_ASSERT_TRUE(cv_fits(r, PRIM_I32));
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("consts_fold", argc, argv);
    TEST_RUN(add_folds_small_values);
    TEST_RUN(add_reaches_u64_max_exactly);
    TEST_RUN(add_one_past_u64_max_is_an_error);
    TEST_RUN(add_reaches_neg_two63_exactly);
    TEST_RUN(add_one_below_neg_two63_is_an_error);
    TEST_RUN(add_of_u64_max_and_a_negative_comes_back_into_range);
    TEST_RUN(sub_folds_small_values);
    TEST_RUN(sub_at_the_boundaries);
    TEST_RUN(sub_outside_the_range_is_an_error);
    TEST_RUN(mul_folds_with_the_sign_of_the_product);
    TEST_RUN(mul_reaches_the_boundaries);
    TEST_RUN(mul_outside_the_range_is_an_error);
    TEST_RUN(div_truncates_toward_zero);
    TEST_RUN(div_of_one_by_two_is_zero_d4_4);
    TEST_RUN(div_by_zero_is_an_error);
    TEST_RUN(div_at_the_boundaries);
    TEST_RUN(div_of_u64_max_by_minus_one_is_an_error);
    TEST_RUN(rem_takes_the_sign_of_the_dividend);
    TEST_RUN(rem_of_an_exact_multiple_is_a_non_negative_zero);
    TEST_RUN(rem_by_zero_is_an_error);
    TEST_RUN(rem_at_the_boundaries);
    TEST_RUN(neg_flips_the_sign_and_keeps_zero_positive);
    TEST_RUN(neg_at_the_boundaries);
    TEST_RUN(neg_of_a_value_above_two63_is_an_error);
    TEST_RUN(not_is_minus_c_minus_one_d4_4);
    TEST_RUN(not_at_the_boundaries);
    TEST_RUN(not_of_two63_or_more_is_an_error);
    TEST_RUN(and_of_non_negatives_is_the_plain_bit_and);
    TEST_RUN(and_with_minus_one_is_the_identity_even_above_i64);
    TEST_RUN(and_of_two_negatives_is_negative);
    TEST_RUN(and_never_leaves_the_range);
    TEST_RUN(or_of_non_negatives_is_the_plain_bit_or);
    TEST_RUN(or_with_a_negative_is_negative);
    TEST_RUN(or_never_leaves_the_range);
    TEST_RUN(xor_of_non_negatives_is_the_plain_bit_xor);
    TEST_RUN(xor_with_minus_one_is_the_complement);
    TEST_RUN(xor_below_neg_two63_is_an_error);
    TEST_RUN(shl_multiplies_by_a_power_of_two);
    TEST_RUN(shl_one_by_63_is_two63);
    TEST_RUN(shl_one_by_64_is_an_error);
    TEST_RUN(shl_by_a_negative_count_is_an_error);
    TEST_RUN(shl_losing_a_bit_is_an_error);
    TEST_RUN(shr_is_a_floor_division_by_a_power_of_two);
    TEST_RUN(shr_of_a_negative_is_arithmetic);
    TEST_RUN(shr_by_64_or_more_is_an_error);
    TEST_RUN(one_shl_40_shr_38_is_four_type_system_10_4);
    TEST_EXIT();
}
