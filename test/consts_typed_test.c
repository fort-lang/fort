// Tests `cv_cast`, typed folding with operand rules, and wrapping operators.
// `consts_fits_test.c` tests `cv_fits` and `cv_default_kind`.
#include <stdint.h>

#include "consts.h"
#include "fork.h"
#include "prim.h"
#include "str.h"

#include "test.h"

// The sample values below are the test data: the edges of every integer
// type and the examples of the type rules and the language rules.
// NOLINTBEGIN(readability-magic-numbers)

enum { ERR_MAX = 256 };

static const uint64_t TWO63 = UINT64_C(1) << 63U; // 2^63
static const uint64_t U64_MAX = UINT64_MAX;       // 2^64 - 1

static cval_t i(int64_t v) {
    return cv_from_i64(v);
}

static cval_t u(uint64_t v) {
    return cv_from_u64(v);
}

static cval_t neg_two63(void) {
    return cv_from_i64(INT64_MIN);
}

static bool is_int(cval_t v, int64_t expect) {
    return v.kind == CV_INT && cv_eq(v, cv_from_i64(expect));
}

static bool is_uint(cval_t v, uint64_t expect) {
    return v.kind == CV_INT && cv_eq(v, cv_from_u64(expect));
}

static bool is_char(cval_t v, uint64_t code) {
    return v.kind == CV_CHAR && v.mag == code;
}

// ---- cv_cast -------------------------------------------------

TEST(cast_0x80000000_to_i32_is_int32_min, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(u(UINT64_C(0x80000000)), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN));
    TEST_ASSERT_TRUE(cv_shl(i(1), i(31), &r));
    TEST_ASSERT_TRUE(cv_cast(r, PRIM_I32, &r)); // cast(1 << 31, i32)
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN));
})

TEST(cast_minus_one_to_unsigned_is_all_ones, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(i(-1), PRIM_U32, &r));
    TEST_ASSERT_TRUE(is_int(r, UINT32_MAX)); // 4294967295
    TEST_ASSERT_TRUE(cv_cast(i(-1), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 255));
    TEST_ASSERT_TRUE(cv_cast(i(-1), PRIM_U16, &r));
    TEST_ASSERT_TRUE(is_int(r, 65535));
    TEST_ASSERT_TRUE(cv_cast(i(-1), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
})

TEST(cast_300_to_u8_is_44, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(i(300), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 44));
    TEST_ASSERT_TRUE(cv_cast(i(300), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, 44));
    TEST_ASSERT_TRUE(cv_cast(i(200), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -56));
    TEST_ASSERT_TRUE(cv_cast(i(-56), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 200));
})

TEST(cast_of_a_char_to_an_integer_is_its_code_point, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(cv_from_char('a'), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 97));
    TEST_ASSERT_TRUE(cv_sub(r, cv_as_int(cv_from_char('0')), &r)); // cast('a', i32) - '0'
    TEST_ASSERT_TRUE(is_int(r, 49));
    TEST_ASSERT_TRUE(cv_cast(cv_from_char(255), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_cast(cv_from_char(255), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_int(r, 255));
})

TEST(cast_to_char_keeps_the_low_byte, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(i(65), PRIM_CHAR, &r)); // cast(65, char)
    TEST_ASSERT_TRUE(is_char(r, 'A'));
    TEST_ASSERT_TRUE(cv_cast(i(321), PRIM_CHAR, &r));
    TEST_ASSERT_TRUE(is_char(r, 'A'));
    TEST_ASSERT_TRUE(cv_cast(i(-1), PRIM_CHAR, &r));
    TEST_ASSERT_TRUE(is_char(r, 255));
    TEST_ASSERT_TRUE(cv_cast(cv_from_char('z'), PRIM_CHAR, &r));
    TEST_ASSERT_TRUE(is_char(r, 'z'));
    TEST_ASSERT_TRUE(cv_cast(i(0x1F600), PRIM_CHAR, &r)); // cast(0x1F600, char) is '\x00'
    TEST_ASSERT_TRUE(is_char(r, 0));
})

TEST(cast_at_the_64_bit_edges, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(u(U64_MAX), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_cast(u(TWO63), PRIM_I64, &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_cast(neg_two63(), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_cast(i(INT64_MAX), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, INT64_MAX));
    TEST_ASSERT_TRUE(cv_cast(u(U64_MAX), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_cast(neg_two63(), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(cast_of_a_value_in_range_is_the_identity, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(i(-128), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -128));
    TEST_ASSERT_TRUE(cv_cast(i(127), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, 127));
    TEST_ASSERT_TRUE(cv_cast(i(INT32_MIN), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN));
    TEST_ASSERT_TRUE(cv_cast(i(UINT32_MAX), PRIM_U32, &r));
    TEST_ASSERT_TRUE(is_int(r, UINT32_MAX));
    TEST_ASSERT_TRUE(cv_cast(i(0), PRIM_U16, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(cast_widens_by_the_source_signedness, {
    // The source sign is in the value: a negative constant sign-extends, a
    // non-negative one zero-extends.
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(i(-1), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_cast(i(255), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, 255));
    TEST_ASSERT_TRUE(cv_cast(i(-32768), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, -32768));
    // `cast(cast(-1, i8), i64)` is -1.
    // `cast(cast(-1, i8), u64)` is 2^64 - 1. The i8 value sign-extends both times.
    TEST_ASSERT_TRUE(cv_cast(i(-1), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    cval_t w = cv_none();
    TEST_ASSERT_TRUE(cv_cast(r, PRIM_I64, &w));
    TEST_ASSERT_TRUE(is_int(w, -1));
    TEST_ASSERT_TRUE(cv_cast(r, PRIM_U64, &w));
    TEST_ASSERT_TRUE(is_uint(w, U64_MAX));
})

TEST(cast_of_a_bool_to_an_integer_is_zero_or_one, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_cast(cv_from_bool(true), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_cast(cv_from_bool(false), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_cast(cv_from_bool(true), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
})

TEST(cast_to_bool_is_forbidden, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_cast(i(1), PRIM_BOOL, &r)); // cast(1, bool)
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_cast(cv_from_char('a'), PRIM_BOOL, &r));
    TEST_ASSERT_FALSE(cv_cast(cv_from_bool(true), PRIM_BOOL, &r));
})

TEST(cast_to_floats_void_and_from_a_bool_to_char_are_unsupported, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_cast(i(1), PRIM_F32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_cast(i(1), PRIM_F64, &r));
    TEST_ASSERT_FALSE(cv_cast(i(1), PRIM_VOID, &r));
    TEST_ASSERT_FALSE(cv_cast(cv_from_bool(true), PRIM_CHAR, &r));
    // `char` to `bool`, a float or an enum is an error too.
    TEST_ASSERT_FALSE(cv_cast(cv_from_char('a'), PRIM_F32, &r));
    TEST_ASSERT_FALSE(cv_cast(cv_from_char('a'), PRIM_F64, &r));
    TEST_ASSERT_FALSE(cv_cast(cv_from_char('a'), PRIM_VOID, &r));
    TEST_ASSERT_FALSE(cv_cast(cv_from_bool(true), PRIM_F64, &r));
})

TEST(cast_of_null_strings_and_none_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_cast(cv_null(), PRIM_U64, &r)); // cast(null, ...)
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_cast(cv_from_str(str_from_cstr("a")), PRIM_I32, &r));
    TEST_ASSERT_FALSE(cv_cast(cv_none(), PRIM_I32, &r));
})

TEST(the_default_type_step_of_d4_1_precedes_cv_cast, {
    // `cast(c, T)` is not a context: the checker gives the untyped `c` its default type and only
    // then converts. The step rejects a value above i64. `cv_cast` is never reached with one, and
    // it cannot change a value that does fit i64, so `cv_cast` needs only the value.
    cval_t r = cv_none();
    prim_kind_t k = PRIM_VOID;
    TEST_ASSERT_TRUE(cv_shl(i(1), i(63), &r)); // cast(1 << 63, i32)
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_FALSE(cv_default_kind(r, &k)); // rejected at the default-type step
    TEST_ASSERT_TRUE(cv_default_kind(u(TWO63 - 1U), &k));
    TEST_ASSERT_TRUE(k == PRIM_I64);
    TEST_ASSERT_TRUE(cv_cast(u(TWO63 - 1U), PRIM_I64, &r)); // its default type: unchanged
    TEST_ASSERT_TRUE(is_int(r, INT64_MAX));
    TEST_ASSERT_TRUE(cv_default_kind(u(UINT64_C(0x80000000)), &k)); // cast(0x80000000, i32)
    TEST_ASSERT_TRUE(k == PRIM_I64);
    TEST_ASSERT_TRUE(cv_cast(u(UINT64_C(0x80000000)), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN));
    TEST_ASSERT_TRUE(cv_default_kind(cv_from_char('a'), &k)); // cast('a', i32) is 97
    TEST_ASSERT_TRUE(k == PRIM_CHAR);
    TEST_ASSERT_TRUE(cv_cast(cv_from_char('a'), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 'a'));
})

// ---- typed folding: the examples of the type rules ----------------------------------

TEST(typed_i32_max_plus_one_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_typed_add(i(INT32_MAX), i(1), PRIM_I32, &r)); // i32 B = A + 1
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_TRUE(cv_typed_add(i(INT32_MAX), i(1), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, (int64_t)INT32_MAX + 1));
})

TEST(typed_i32_max_wrap_plus_one_is_int32_min, {
    const cval_t r = cv_wrap_add(i(INT32_MAX), i(1), PRIM_I32); // i32 C = A +% 1
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN));
})

TEST(typed_u8_200_times_2_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_typed_mul(i(200), i(2), PRIM_U8, &r)); // u8 E = D * 2
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(i(200), i(2), PRIM_U8), 144));
})

TEST(typed_constant_expression_examples, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_mul(i(4), i(2), PRIM_I32, &r)); // i32[N * 2] with N = 4
    TEST_ASSERT_TRUE(is_int(r, 8));
    TEST_ASSERT_TRUE(cv_add(i(8), i(1), &r)); // buf.len + 1: untyped
    TEST_ASSERT_TRUE(is_int(r, 9));
    TEST_ASSERT_TRUE(cv_cast(i(6), PRIM_I32, &r)); // cast(color.blue, i32) + 1
    TEST_ASSERT_TRUE(cv_typed_add(r, i(1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 7));
})

// ---- typed add, sub, mul, neg --------------------------------------------------------

TEST(typed_add_and_sub_stay_in_range, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_add(i(100), i(27), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, 127));
    TEST_ASSERT_FALSE(cv_typed_add(i(100), i(28), PRIM_I8, &r));
    TEST_ASSERT_TRUE(cv_typed_sub(i(-100), i(28), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -128));
    TEST_ASSERT_FALSE(cv_typed_sub(i(-100), i(29), PRIM_I8, &r));
    TEST_ASSERT_TRUE(cv_typed_add(u(U64_MAX - 1U), i(1), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_FALSE(cv_typed_add(u(U64_MAX), i(1), PRIM_U64, &r));
})

TEST(typed_unsigned_subtraction_below_zero_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_typed_sub(i(0), i(1), PRIM_U8, &r)); // len - 1 on an empty span
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_sub(i(0), i(1), PRIM_U64, &r));
    TEST_ASSERT_TRUE(cv_typed_sub(i(1), i(1), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(typed_mul_at_the_edges, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_mul(i(-64), i(2), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -128));
    TEST_ASSERT_FALSE(cv_typed_mul(i(64), i(2), PRIM_I8, &r));
    TEST_ASSERT_TRUE(cv_typed_mul(i(INT32_MIN), i(1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN));
    TEST_ASSERT_FALSE(cv_typed_mul(i(INT32_MIN), i(-1), PRIM_I32, &r));
    TEST_ASSERT_FALSE(cv_typed_mul(neg_two63(), i(-1), PRIM_I64, &r));
    TEST_ASSERT_FALSE(cv_typed_mul(u(TWO63), i(2), PRIM_U64, &r));
})

TEST(typed_neg_of_min_is_an_error, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_neg(i(5), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, -5));
    TEST_ASSERT_TRUE(cv_typed_neg(i(INT32_MIN + 1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, INT32_MAX));
    TEST_ASSERT_FALSE(cv_typed_neg(i(INT32_MIN), PRIM_I32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_neg(i(-128), PRIM_I8, &r));
    TEST_ASSERT_FALSE(cv_typed_neg(neg_two63(), PRIM_I64, &r));
})

// ---- typed div and rem --------------------------------------------------------------

TEST(typed_div_truncates_and_rejects_min_over_minus_one, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_div(i(-7), i(2), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, -3));
    TEST_ASSERT_TRUE(cv_typed_div(i(INT32_MIN), i(2), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN / 2));
    TEST_ASSERT_FALSE(cv_typed_div(i(INT32_MIN), i(-1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_div(i(-128), i(-1), PRIM_I8, &r));
    TEST_ASSERT_FALSE(cv_typed_div(i(-32768), i(-1), PRIM_I16, &r));
    TEST_ASSERT_FALSE(cv_typed_div(neg_two63(), i(-1), PRIM_I64, &r));
    TEST_ASSERT_TRUE(cv_typed_div(i(INT32_MIN + 1), i(-1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, INT32_MAX));
})

TEST(typed_div_by_zero_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_typed_div(i(1), i(0), PRIM_I32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_div(i(0), i(0), PRIM_U64, &r));
})

TEST(typed_rem_keeps_the_dividend_sign_and_rejects_min_rem_minus_one, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_rem(i(-7), i(2), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_typed_rem(i(7), i(-2), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_typed_rem(i(255), i(16), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 15));
    TEST_ASSERT_FALSE(cv_typed_rem(i(INT32_MIN), i(-1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_rem(i(-128), i(-1), PRIM_I8, &r));
    TEST_ASSERT_FALSE(cv_typed_rem(neg_two63(), i(-1), PRIM_I64, &r));
    TEST_ASSERT_TRUE(cv_typed_rem(i(INT32_MIN), i(-2), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_typed_rem(i(INT32_MIN + 1), i(-1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
})

TEST(typed_rem_by_zero_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_typed_rem(i(1), i(0), PRIM_I32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_rem(i(-1), i(0), PRIM_I8, &r));
})

// ---- typed bitwise ------------------------------------------------------------------

TEST(typed_not_complements_within_the_width, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_not(i(0), PRIM_U32, &r));
    TEST_ASSERT_TRUE(is_int(r, UINT32_MAX)); // ~0 on a u32 variable
    TEST_ASSERT_TRUE(cv_typed_not(i(0), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_typed_not(i(0xF0), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 0x0F));
    TEST_ASSERT_TRUE(cv_typed_not(i(-1), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_typed_not(i(127), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -128));
    TEST_ASSERT_TRUE(cv_typed_not(u(U64_MAX), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_int(r, 0));
    TEST_ASSERT_TRUE(cv_typed_not(i(0), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_typed_not(neg_two63(), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, INT64_MAX));
})

TEST(typed_and_or_xor_never_leave_the_type, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_and(i(-1), i(5), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, 5));
    TEST_ASSERT_TRUE(cv_typed_or(i(-128), i(127), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_typed_xor(i(-1), i(127), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -128));
    TEST_ASSERT_TRUE(cv_typed_xor(i(-128), i(-1), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, 127));
    TEST_ASSERT_TRUE(cv_typed_and(u(U64_MAX), u(TWO63), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_typed_or(u(TWO63), u(TWO63 - 1U), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX));
    TEST_ASSERT_TRUE(cv_typed_xor(u(U64_MAX), u(TWO63), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63 - 1U));
    TEST_ASSERT_TRUE(cv_typed_xor(neg_two63(), i(-1), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, INT64_MAX));
    TEST_ASSERT_TRUE(cv_typed_and(i(0xF0F0), i(0x0FF0), PRIM_U16, &r));
    TEST_ASSERT_TRUE(is_int(r, 0x00F0));
})

// ---- typed shifts ------------------------------------------------------------------

TEST(typed_shl_discards_the_bits_shifted_out, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_shl(i(1), i(31), PRIM_I32, &r)); // one << 31 on an i32
    TEST_ASSERT_TRUE(is_int(r, INT32_MIN));
    TEST_ASSERT_TRUE(cv_typed_shl(i(1), i(7), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 128));
    TEST_ASSERT_TRUE(cv_typed_shl(i(3), i(7), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 128));
    TEST_ASSERT_TRUE(cv_typed_shl(i(1), i(7), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -128));
    TEST_ASSERT_TRUE(cv_typed_shl(i(-1), i(1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, -2));
    TEST_ASSERT_TRUE(cv_typed_shl(i(1), i(63), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, TWO63));
    TEST_ASSERT_TRUE(cv_typed_shl(i(1), i(63), PRIM_I64, &r));
    TEST_ASSERT_TRUE(cv_eq(r, neg_two63()));
    TEST_ASSERT_TRUE(cv_typed_shl(u(U64_MAX), i(1), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_uint(r, U64_MAX - 1U));
    TEST_ASSERT_TRUE(cv_typed_shl(i(5), i(0), PRIM_I16, &r));
    TEST_ASSERT_TRUE(is_int(r, 5));
})

TEST(typed_shift_count_at_or_past_the_width_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_typed_shl(i(1), i(32), PRIM_I32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_shl(i(1), i(8), PRIM_U8, &r));
    TEST_ASSERT_FALSE(cv_typed_shl(i(1), i(64), PRIM_U64, &r));
    TEST_ASSERT_FALSE(cv_typed_shr(i(1), i(32), PRIM_U32, &r));
    TEST_ASSERT_FALSE(cv_typed_shr(i(1), i(16), PRIM_I16, &r));
    TEST_ASSERT_FALSE(cv_typed_shr(i(0), i(64), PRIM_I64, &r));
    TEST_ASSERT_FALSE(cv_typed_shl(i(1), u(U64_MAX), PRIM_I64, &r));
})

TEST(typed_negative_shift_count_is_an_error, {
    cval_t r = i(1);
    TEST_ASSERT_FALSE(cv_typed_shl(i(1), i(-1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(r.kind == CV_NONE);
    TEST_ASSERT_FALSE(cv_typed_shr(i(1), i(-1), PRIM_U64, &r));
})

TEST(typed_shr_is_arithmetic_for_signed_and_logical_for_unsigned, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_typed_shr(i(-8), i(1), PRIM_I32, &r));
    TEST_ASSERT_TRUE(is_int(r, -4));
    TEST_ASSERT_TRUE(cv_typed_shr(i(-1), i(7), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_typed_shr(i(-7), i(1), PRIM_I16, &r));
    TEST_ASSERT_TRUE(is_int(r, -4));
    TEST_ASSERT_TRUE(cv_typed_shr(neg_two63(), i(63), PRIM_I64, &r));
    TEST_ASSERT_TRUE(is_int(r, -1));
    TEST_ASSERT_TRUE(cv_typed_shr(i(UINT32_MAX), i(31), PRIM_U32, &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_typed_shr(u(U64_MAX), i(63), PRIM_U64, &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_typed_shr(i(255), i(7), PRIM_U8, &r));
    TEST_ASSERT_TRUE(is_int(r, 1));
    TEST_ASSERT_TRUE(cv_typed_shr(i(64), i(3), PRIM_I8, &r));
    TEST_ASSERT_TRUE(is_int(r, 8));
})

// ---- wrapping operators -------------------------------------------------------------

TEST(wrap_add_wraps_at_every_width, {
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(127), i(1), PRIM_I8), -128));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(255), i(1), PRIM_U8), 0));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(32767), i(1), PRIM_I16), -32768));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(65535), i(2), PRIM_U16), 1));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(UINT32_MAX), i(1), PRIM_U32), 0));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(INT64_MAX), i(1), PRIM_I64), INT64_MIN));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(u(U64_MAX), i(1), PRIM_U64), 0));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(1), i(2), PRIM_I32), 3));
    TEST_ASSERT_TRUE(is_int(cv_wrap_add(i(-1), i(-1), PRIM_I32), -2));
})

TEST(wrap_sub_wraps_at_every_width, {
    TEST_ASSERT_TRUE(is_int(cv_wrap_sub(i(0), i(1), PRIM_U8), 255));
    TEST_ASSERT_TRUE(is_int(cv_wrap_sub(i(0), i(1), PRIM_U32), UINT32_MAX));
    TEST_ASSERT_TRUE(is_uint(cv_wrap_sub(i(0), i(1), PRIM_U64), U64_MAX));
    TEST_ASSERT_TRUE(is_int(cv_wrap_sub(i(-128), i(1), PRIM_I8), 127));
    TEST_ASSERT_TRUE(is_int(cv_wrap_sub(i(INT32_MIN), i(1), PRIM_I32), INT32_MAX));
    TEST_ASSERT_TRUE(is_int(cv_wrap_sub(neg_two63(), i(1), PRIM_I64), INT64_MAX));
    TEST_ASSERT_TRUE(is_int(cv_wrap_sub(i(5), i(7), PRIM_I32), -2));
})

TEST(wrap_mul_wraps_at_every_width, {
    // The FNV step of the language rules: h *% 16777619 with h = 2166136261.
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(i(INT64_C(2166136261)), i(16777619), PRIM_U32), 84696351));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(i(255), i(2), PRIM_U8), 254));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(i(200), i(2), PRIM_U8), 144));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(i(-128), i(-1), PRIM_I8), -128));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(i(INT32_MIN), i(-1), PRIM_I32), INT32_MIN));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(neg_two63(), i(-1), PRIM_I64), INT64_MIN));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(u(U64_MAX), u(U64_MAX), PRIM_U64), 1));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(u(TWO63), u(TWO63), PRIM_U64), 0));
    TEST_ASSERT_TRUE(is_int(cv_wrap_mul(i(6), i(7), PRIM_I32), 42));
})

// ---- preconditions ----------------------------------------------------------------

static void typed_add_on_char_type(void) {
    cval_t r = cv_none();
    TEST_UNUSED(cv_typed_add(i(1), i(2), PRIM_CHAR, &r));
}

TEST(typed_folding_on_a_non_integer_type_is_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(typed_add_on_char_type, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: integer folding on a non-integer type\n");
})

static void wrap_add_on_bool(void) {
    TEST_UNUSED(cv_wrap_add(cv_from_bool(true), i(1), PRIM_I32));
}

TEST(wrapping_on_a_non_integer_value_is_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(wrap_add_on_bool, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant folding on a non-integer\n");
})

static void typed_neg_on_u32(void) {
    cval_t r = cv_none();
    TEST_UNUSED(cv_typed_neg(i(1), PRIM_U32, &r));
}

TEST(typed_neg_on_an_unsigned_type_is_an_internal_error, {
    // Unary minus is signed and float only, so the checker has already refused
    // `-x` on an unsigned constant.
    char err[ERR_MAX];
    const int status = run_forked(typed_neg_on_u32, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: unary minus on an unsigned type\n");
})

static void typed_and_of_minus_one_in_u8(void) {
    cval_t r = cv_none();
    TEST_UNUSED(cv_typed_and(i(-1), i(5), PRIM_U8, &r));
}

static void wrap_add_of_300_in_u8(void) {
    TEST_UNUSED(cv_wrap_add(i(300), i(1), PRIM_U8));
}

TEST(a_value_outside_the_folded_type_is_an_internal_error, {
    // Each cv_typed_* and cv_wrap_* operand must fit its type.
    // An invalid operand is a checker error, not a wrapped fold.
    char err[ERR_MAX];
    const int status = run_forked(typed_and_of_minus_one_in_u8, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(
        err, "fort: error: internal error: typed constant folding on a value outside its type\n");
    const int wrap_status = run_forked(wrap_add_of_300_in_u8, err, sizeof err);
    TEST_ASSERT_EQ_INT32(wrap_status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(
        err, "fort: error: internal error: typed constant folding on a value outside its type\n");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("consts_typed", argc, argv);
    TEST_RUN(cast_0x80000000_to_i32_is_int32_min);
    TEST_RUN(cast_minus_one_to_unsigned_is_all_ones);
    TEST_RUN(cast_300_to_u8_is_44);
    TEST_RUN(cast_of_a_char_to_an_integer_is_its_code_point);
    TEST_RUN(cast_to_char_keeps_the_low_byte);
    TEST_RUN(cast_at_the_64_bit_edges);
    TEST_RUN(cast_of_a_value_in_range_is_the_identity);
    TEST_RUN(cast_widens_by_the_source_signedness);
    TEST_RUN(cast_of_a_bool_to_an_integer_is_zero_or_one);
    TEST_RUN(cast_to_bool_is_forbidden);
    TEST_RUN(cast_to_floats_void_and_from_a_bool_to_char_are_unsupported);
    TEST_RUN(cast_of_null_strings_and_none_is_an_error);
    TEST_RUN(the_default_type_step_of_d4_1_precedes_cv_cast);
    TEST_RUN(typed_i32_max_plus_one_is_an_error);
    TEST_RUN(typed_i32_max_wrap_plus_one_is_int32_min);
    TEST_RUN(typed_u8_200_times_2_is_an_error);
    TEST_RUN(typed_constant_expression_examples);
    TEST_RUN(typed_add_and_sub_stay_in_range);
    TEST_RUN(typed_unsigned_subtraction_below_zero_is_an_error);
    TEST_RUN(typed_mul_at_the_edges);
    TEST_RUN(typed_neg_of_min_is_an_error);
    TEST_RUN(typed_div_truncates_and_rejects_min_over_minus_one);
    TEST_RUN(typed_div_by_zero_is_an_error);
    TEST_RUN(typed_rem_keeps_the_dividend_sign_and_rejects_min_rem_minus_one);
    TEST_RUN(typed_rem_by_zero_is_an_error);
    TEST_RUN(typed_not_complements_within_the_width);
    TEST_RUN(typed_and_or_xor_never_leave_the_type);
    TEST_RUN(typed_shl_discards_the_bits_shifted_out);
    TEST_RUN(typed_shift_count_at_or_past_the_width_is_an_error);
    TEST_RUN(typed_negative_shift_count_is_an_error);
    TEST_RUN(typed_shr_is_arithmetic_for_signed_and_logical_for_unsigned);
    TEST_RUN(wrap_add_wraps_at_every_width);
    TEST_RUN(wrap_sub_wraps_at_every_width);
    TEST_RUN(wrap_mul_wraps_at_every_width);
    TEST_RUN(typed_folding_on_a_non_integer_type_is_an_internal_error);
    TEST_RUN(wrapping_on_a_non_integer_value_is_an_internal_error);
    TEST_RUN(typed_neg_on_an_unsigned_type_is_an_internal_error);
    TEST_RUN(a_value_outside_the_folded_type_is_an_internal_error);
    TEST_EXIT();
}
