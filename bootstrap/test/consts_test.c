// Tests constant values, queries, ordering, equality,
// the comparison and logical operators, and cv_to_str. The exact untyped
// arithmetic is in consts_fold_test.c and the typed side (cv_fits,
// cv_default_kind, cv_cast, cv_typed_*, cv_wrap_*) in consts_typed_test.c.
#include "consts.h"

#include <stdint.h>

#include "common/fork.h"
#include "prim.h"
#include "str.h"

#include "common/test.h"

// The sample values below are the test data: the boundaries of the constant
// range and the examples of the type rules.
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

static bool is_bool(cval_t v, bool expect) {
    return v.kind == CV_BOOL && v.mag == (expect ? 1U : 0U);
}

// The text `cv_to_str` writes for `v`.
// The next call invalidates the returned shared-buffer result.
static const char* text_of(cval_t v) {
    static sb_t buf;
    sb_clear(&buf);
    cv_to_str(v, &buf);
    return sb_cstr(&buf);
}

// ---- constructors ------------------------------------------------------------

TEST(none_has_kind_none_and_no_payload, {
    const cval_t v = cv_none();
    TEST_ASSERT_TRUE(v.kind == CV_NONE);
    TEST_ASSERT_FALSE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, (uint64_t)0);
    TEST_ASSERT_NULL(v.str.ptr);
})

TEST(from_i64_of_a_positive_is_its_magnitude, {
    const cval_t v = cv_from_i64(42);
    TEST_ASSERT_TRUE(v.kind == CV_INT);
    TEST_ASSERT_FALSE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, (uint64_t)42);
})

TEST(from_i64_of_a_negative_sets_neg_and_the_magnitude, {
    const cval_t v = cv_from_i64(-42);
    TEST_ASSERT_TRUE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, (uint64_t)42);
})

TEST(from_i64_of_zero_is_never_negative, {
    const cval_t v = cv_from_i64(0);
    TEST_ASSERT_FALSE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, (uint64_t)0);
})

TEST(from_i64_of_int64_min_is_neg_two63, {
    const cval_t v = cv_from_i64(INT64_MIN);
    TEST_ASSERT_TRUE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, TWO63);
})

TEST(from_i64_of_int64_max_is_two63_minus_one, {
    const cval_t v = cv_from_i64(INT64_MAX);
    TEST_ASSERT_FALSE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, TWO63 - 1U);
})

TEST(from_u64_holds_the_lexer_magnitude_up_to_u64_max, {
    const cval_t v = cv_from_u64(U64_MAX);
    TEST_ASSERT_TRUE(v.kind == CV_INT);
    TEST_ASSERT_FALSE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, U64_MAX);
})

TEST(from_u64_of_two63_is_the_first_value_above_i64, {
    const cval_t v = cv_from_u64(TWO63);
    TEST_ASSERT_FALSE(v.neg);
    TEST_ASSERT_EQ_UINT64(v.mag, TWO63);
    int64_t out = 0;
    TEST_ASSERT_FALSE(cv_to_i64(v, &out));
})

TEST(from_char_holds_the_code_point, {
    const cval_t v = cv_from_char('a');
    TEST_ASSERT_TRUE(v.kind == CV_CHAR);
    TEST_ASSERT_EQ_UINT64(v.mag, (uint64_t)97);
    TEST_ASSERT_EQ_UINT64(cv_from_char(255).mag, (uint64_t)255);
    TEST_ASSERT_EQ_UINT64(cv_from_char(0).mag, (uint64_t)0);
})

static void char_out_of_range(void) {
    TEST_UNUSED(cv_from_char(256));
}

TEST(from_char_above_255_is_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(char_out_of_range, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: char constant outside 0..255\n");
})

TEST(from_bool_holds_zero_or_one, {
    TEST_ASSERT_TRUE(cv_from_bool(true).kind == CV_BOOL);
    TEST_ASSERT_EQ_UINT64(cv_from_bool(true).mag, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(cv_from_bool(false).mag, (uint64_t)0);
})

TEST(null_has_kind_null, {
    const cval_t v = cv_null();
    TEST_ASSERT_TRUE(v.kind == CV_NULL);
    TEST_ASSERT_EQ_UINT64(v.mag, (uint64_t)0);
})

TEST(from_str_keeps_the_view, {
    const char* text = "abc";
    const cval_t v = cv_from_str(str_from_cstr(text));
    TEST_ASSERT_TRUE(v.kind == CV_STR);
    TEST_ASSERT_TRUE(v.str.ptr == text);
    TEST_ASSERT_EQ_UINT64(v.str.len, (uint64_t)3);
})

// ---- queries ------------------------------------------------------------------

TEST(is_int_holds_for_integers_only, {
    TEST_ASSERT_TRUE(cv_is_int(i(0)));
    TEST_ASSERT_TRUE(cv_is_int(u(U64_MAX)));
    TEST_ASSERT_FALSE(cv_is_int(cv_from_char('a')));
    TEST_ASSERT_FALSE(cv_is_int(cv_from_bool(true)));
    TEST_ASSERT_FALSE(cv_is_int(cv_null()));
    TEST_ASSERT_FALSE(cv_is_int(cv_none()));
})

TEST(is_int_like_admits_chars_too, {
    TEST_ASSERT_TRUE(cv_is_int_like(i(-1)));
    TEST_ASSERT_TRUE(cv_is_int_like(cv_from_char('a')));
    TEST_ASSERT_FALSE(cv_is_int_like(cv_from_bool(false)));
    TEST_ASSERT_FALSE(cv_is_int_like(cv_from_str(str_from_cstr("a"))));
})

TEST(as_int_of_a_char_is_its_code_point_d4_3, {
    const cval_t v = cv_as_int(cv_from_char('a'));
    TEST_ASSERT_TRUE(is_int(v, 97));
    TEST_ASSERT_TRUE(is_int(cv_as_int(cv_from_char('\0')), 0));
    TEST_ASSERT_TRUE(is_int(cv_as_int(cv_from_char(255)), 255));
})

TEST(as_int_of_an_int_is_itself, {
    TEST_ASSERT_TRUE(is_int(cv_as_int(i(-5)), -5));
    TEST_ASSERT_TRUE(is_uint(cv_as_int(u(U64_MAX)), U64_MAX));
})

static void as_int_of_bool(void) {
    TEST_UNUSED(cv_as_int(cv_from_bool(true)));
}

TEST(as_int_of_a_bool_is_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(as_int_of_bool, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant folding on a non-integer\n");
})

TEST(is_zero_and_is_neg, {
    TEST_ASSERT_TRUE(cv_is_zero(i(0)));
    TEST_ASSERT_FALSE(cv_is_zero(i(1)));
    TEST_ASSERT_FALSE(cv_is_zero(i(-1)));
    TEST_ASSERT_TRUE(cv_is_neg(i(-1)));
    TEST_ASSERT_FALSE(cv_is_neg(i(0)));
    TEST_ASSERT_FALSE(cv_is_neg(u(U64_MAX)));
    TEST_ASSERT_TRUE(cv_is_neg(neg_two63()));
})

TEST(to_i64_round_trips_the_i64_range, {
    int64_t out = 0;
    TEST_ASSERT_TRUE(cv_to_i64(i(INT64_MIN), &out));
    TEST_ASSERT_EQ_INT64(out, INT64_MIN);
    TEST_ASSERT_TRUE(cv_to_i64(i(INT64_MAX), &out));
    TEST_ASSERT_EQ_INT64(out, INT64_MAX);
    TEST_ASSERT_TRUE(cv_to_i64(i(-1), &out));
    TEST_ASSERT_EQ_INT64(out, (int64_t)-1);
    TEST_ASSERT_TRUE(cv_to_i64(i(0), &out));
    TEST_ASSERT_EQ_INT64(out, (int64_t)0);
})

TEST(to_i64_rejects_values_above_int64_max, {
    int64_t out = 7;
    TEST_ASSERT_FALSE(cv_to_i64(u(TWO63), &out));
    TEST_ASSERT_FALSE(cv_to_i64(u(U64_MAX), &out));
    TEST_ASSERT_EQ_INT64(out, (int64_t)7);
})

TEST(to_u64_accepts_non_negatives_and_rejects_negatives, {
    uint64_t out = 0;
    TEST_ASSERT_TRUE(cv_to_u64(u(U64_MAX), &out));
    TEST_ASSERT_EQ_UINT64(out, U64_MAX);
    TEST_ASSERT_TRUE(cv_to_u64(i(0), &out));
    TEST_ASSERT_EQ_UINT64(out, (uint64_t)0);
    TEST_ASSERT_FALSE(cv_to_u64(i(-1), &out));
    TEST_ASSERT_FALSE(cv_to_u64(neg_two63(), &out));
})

TEST(bits_is_the_two_s_complement_pattern, {
    TEST_ASSERT_EQ_UINT64(cv_bits(i(0)), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(cv_bits(i(1)), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(cv_bits(i(-1)), U64_MAX);
    TEST_ASSERT_EQ_UINT64(cv_bits(i(-2)), U64_MAX - 1U);
    TEST_ASSERT_EQ_UINT64(cv_bits(neg_two63()), TWO63);
    TEST_ASSERT_EQ_UINT64(cv_bits(u(TWO63)), TWO63);
    TEST_ASSERT_EQ_UINT64(cv_bits(u(U64_MAX)), U64_MAX);
})

// ---- ordering and equality ----------------------------------------------------

TEST(cmp_orders_integers_across_the_whole_range, {
    TEST_ASSERT_EQ_INT32(cv_cmp(i(1), i(2)), -1);
    TEST_ASSERT_EQ_INT32(cv_cmp(i(2), i(1)), 1);
    TEST_ASSERT_EQ_INT32(cv_cmp(i(2), i(2)), 0);
    TEST_ASSERT_EQ_INT32(cv_cmp(i(-1), i(0)), -1);
    TEST_ASSERT_EQ_INT32(cv_cmp(i(0), i(-1)), 1);
    TEST_ASSERT_EQ_INT32(cv_cmp(i(-2), i(-1)), -1);
    TEST_ASSERT_EQ_INT32(cv_cmp(i(-1), i(-2)), 1);
    TEST_ASSERT_EQ_INT32(cv_cmp(i(-1), i(-1)), 0);
    TEST_ASSERT_EQ_INT32(cv_cmp(neg_two63(), u(U64_MAX)), -1);
    TEST_ASSERT_EQ_INT32(cv_cmp(u(U64_MAX), u(TWO63)), 1);
    TEST_ASSERT_EQ_INT32(cv_cmp(u(TWO63), i(INT64_MAX)), 1);
    TEST_ASSERT_EQ_INT32(cv_cmp(neg_two63(), i(INT64_MIN + 1)), -1);
})

TEST(cmp_orders_chars_by_code_point_d3_2, {
    TEST_ASSERT_EQ_INT32(cv_cmp(cv_from_char('a'), cv_from_char('b')), -1);
    TEST_ASSERT_EQ_INT32(cv_cmp(cv_from_char('b'), cv_from_char('a')), 1);
    TEST_ASSERT_EQ_INT32(cv_cmp(cv_from_char('a'), cv_from_char('a')), 0);
    TEST_ASSERT_EQ_INT32(cv_cmp(cv_from_char(0), cv_from_char(255)), -1);
})

static void cmp_int_and_char(void) {
    TEST_UNUSED(cv_cmp(i(97), cv_from_char('a')));
}

TEST(cmp_of_an_int_and_a_char_is_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(cmp_int_and_char, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant folding on a non-integer\n");
})

TEST(eq_compares_integers_by_value, {
    TEST_ASSERT_TRUE(cv_eq(i(5), u(5)));
    TEST_ASSERT_TRUE(cv_eq(i(-5), i(-5)));
    TEST_ASSERT_FALSE(cv_eq(i(-5), i(5)));
    TEST_ASSERT_FALSE(cv_eq(i(0), i(1)));
    TEST_ASSERT_TRUE(cv_eq(neg_two63(), cv_from_i64(INT64_MIN)));
    TEST_ASSERT_FALSE(cv_eq(u(TWO63), neg_two63()));
})

TEST(eq_compares_bools_chars_nulls_and_strings, {
    TEST_ASSERT_TRUE(cv_eq(cv_from_bool(true), cv_from_bool(true)));
    TEST_ASSERT_FALSE(cv_eq(cv_from_bool(true), cv_from_bool(false)));
    TEST_ASSERT_TRUE(cv_eq(cv_from_char('x'), cv_from_char('x')));
    TEST_ASSERT_FALSE(cv_eq(cv_from_char('x'), cv_from_char('y')));
    TEST_ASSERT_TRUE(cv_eq(cv_null(), cv_null()));
    TEST_ASSERT_TRUE(cv_eq(cv_from_str(str_from_cstr("ab")), cv_from_str(str_from_cstr("ab"))));
    TEST_ASSERT_FALSE(cv_eq(cv_from_str(str_from_cstr("ab")), cv_from_str(str_from_cstr("ac"))));
})

TEST(eq_is_false_across_kinds_and_for_none, {
    TEST_ASSERT_FALSE(cv_eq(i(97), cv_from_char('a')));
    TEST_ASSERT_FALSE(cv_eq(i(1), cv_from_bool(true)));
    TEST_ASSERT_FALSE(cv_eq(i(0), cv_null()));
    TEST_ASSERT_FALSE(cv_eq(cv_none(), cv_none()));
})

// ---- comparison and logical folding ----------------------------------------------

TEST(compare_orders_integers, {
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_LT, i(1), i(2)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_LT, i(2), i(2)), false));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_LE, i(2), i(2)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_LE, i(3), i(2)), false));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_GT, i(3), i(2)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_GT, i(2), i(2)), false));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_GE, i(2), i(2)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_GE, i(1), i(2)), false));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_LT, neg_two63(), u(U64_MAX)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_GE, i(-1), u(TWO63)), false));
})

TEST(compare_tests_equality_of_every_kind, {
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_EQ, i(1), i(1)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_NE, i(1), i(1)), false));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_EQ, i(1), i(-1)), false));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_NE, i(1), i(-1)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_EQ, cv_from_bool(true), cv_from_bool(true)), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_NE, cv_from_char('a'), cv_from_char('b')), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_EQ, cv_null(), cv_null()), true));
    TEST_ASSERT_TRUE(is_bool(
        cv_compare(CV_REL_EQ, cv_from_str(str_from_cstr("x")), cv_from_str(str_from_cstr("x"))),
        true));
})

TEST(compare_orders_chars, {
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_LT, cv_from_char('a'), cv_from_char('b')), true));
    TEST_ASSERT_TRUE(is_bool(cv_compare(CV_REL_GE, cv_from_char('a'), cv_from_char('b')), false));
})

static void compare_across_kinds(void) {
    TEST_UNUSED(cv_compare(CV_REL_EQ, i(1), cv_from_bool(true)));
}

TEST(compare_equality_across_kinds_is_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(compare_across_kinds, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant comparison across kinds\n");
})

static void compare_none(void) {
    TEST_UNUSED(cv_compare(CV_REL_NE, cv_none(), cv_none()));
}

TEST(compare_of_none_is_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(compare_none, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant comparison across kinds\n");
})

static void compare_lt_of_nulls(void) {
    TEST_UNUSED(cv_compare(CV_REL_LT, cv_null(), cv_null()));
}

static void compare_lt_of_strings(void) {
    TEST_UNUSED(
        cv_compare(CV_REL_GE, cv_from_str(str_from_cstr("a")), cv_from_str(str_from_cstr("b"))));
}

static void compare_lt_of_bools(void) {
    TEST_UNUSED(cv_compare(CV_REL_LT, cv_from_bool(false), cv_from_bool(true)));
}

TEST(ordering_nulls_strings_and_bools_is_an_internal_error, {
    // `< <= > >=` order integers and chars only: the checker has rejected the
    // others before folding.
    char err[ERR_MAX];
    const int status = run_forked(compare_lt_of_nulls, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant folding on a non-integer\n");
    const int str_status = run_forked(compare_lt_of_strings, err, sizeof err);
    TEST_ASSERT_EQ_INT32(str_status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant folding on a non-integer\n");
    const int bool_status = run_forked(compare_lt_of_bools, err, sizeof err);
    TEST_ASSERT_EQ_INT32(bool_status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: constant folding on a non-integer\n");
})

TEST(logical_operators_fold_bools, {
    const cval_t t = cv_from_bool(true);
    const cval_t f = cv_from_bool(false);
    TEST_ASSERT_TRUE(is_bool(cv_lnot(t), false));
    TEST_ASSERT_TRUE(is_bool(cv_lnot(f), true));
    TEST_ASSERT_TRUE(is_bool(cv_land(t, t), true));
    TEST_ASSERT_TRUE(is_bool(cv_land(t, f), false));
    TEST_ASSERT_TRUE(is_bool(cv_land(f, t), false));
    TEST_ASSERT_TRUE(is_bool(cv_land(f, f), false));
    TEST_ASSERT_TRUE(is_bool(cv_lor(t, t), true));
    TEST_ASSERT_TRUE(is_bool(cv_lor(t, f), true));
    TEST_ASSERT_TRUE(is_bool(cv_lor(f, t), true));
    TEST_ASSERT_TRUE(is_bool(cv_lor(f, f), false));
})

static void lnot_of_int(void) {
    TEST_UNUSED(cv_lnot(i(1)));
}

TEST(logical_operators_on_a_non_bool_are_an_internal_error, {
    char err[ERR_MAX];
    const int status = run_forked(lnot_of_int, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: logical folding on a non-bool\n");
})

// ---- cv_to_str --------------------------------------------------------------------

TEST(to_str_writes_integers_in_decimal, {
    TEST_ASSERT_EQ_STR(text_of(i(0)), "0");
    TEST_ASSERT_EQ_STR(text_of(i(42)), "42");
    TEST_ASSERT_EQ_STR(text_of(i(-42)), "-42");
    TEST_ASSERT_EQ_STR(text_of(i(INT64_MAX)), "9223372036854775807");
    TEST_ASSERT_EQ_STR(text_of(u(TWO63)), "9223372036854775808");
    TEST_ASSERT_EQ_STR(text_of(u(U64_MAX)), "18446744073709551615");
    TEST_ASSERT_EQ_STR(text_of(neg_two63()), "-9223372036854775808");
})

TEST(to_str_writes_chars_as_literals, {
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('a')), "'a'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char(' ')), "' '");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('~')), "'~'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('\n')), "'\\n'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('\t')), "'\\t'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('\r')), "'\\r'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char(0)), "'\\0'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('\\')), "'\\\\'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('\'')), "'\\''");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char('"')), "'\"'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char(0x7F)), "'\\x7F'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char(0xFF)), "'\\xFF'");
    TEST_ASSERT_EQ_STR(text_of(cv_from_char(1)), "'\\x01'");
})

TEST(to_str_writes_the_other_kinds, {
    TEST_ASSERT_EQ_STR(text_of(cv_from_bool(true)), "true");
    TEST_ASSERT_EQ_STR(text_of(cv_from_bool(false)), "false");
    TEST_ASSERT_EQ_STR(text_of(cv_null()), "null");
    TEST_ASSERT_EQ_STR(text_of(cv_from_str(str_from_cstr("abc"))), "\"abc\"");
    TEST_ASSERT_EQ_STR(text_of(cv_none()), "<none>");
})

TEST(to_str_escapes_a_string_as_a_literal_d2_8, {
    // A quote, a newline or a NUL byte in a string constant must not break the diagnostic line. The
    // bytes are written with the escapes. The nine bytes a " \n NUL \\ \t \r ' 0xFF (a brace
    // initializer would split the TEST body at its commas. They come from a literal).
    const str_t s = str_from_range("a\"\n\0\\\t\r'\xFF", 9);
    TEST_ASSERT_EQ_STR(text_of(cv_from_str(s)), "\"a\\\"\\n\\0\\\\\\t\\r'\\xFF\"");
    TEST_ASSERT_EQ_STR(text_of(cv_from_str(str_from_range(NULL, 0))), "\"\"");
})

TEST(to_str_appends_to_the_buffer, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "constant ");
    cv_to_str(i(-7), &b);
    sb_append(&b, " does not fit");
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "constant -7 does not fit");
    sb_free(&b);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("consts", argc, argv);
    TEST_RUN(none_has_kind_none_and_no_payload);
    TEST_RUN(from_i64_of_a_positive_is_its_magnitude);
    TEST_RUN(from_i64_of_a_negative_sets_neg_and_the_magnitude);
    TEST_RUN(from_i64_of_zero_is_never_negative);
    TEST_RUN(from_i64_of_int64_min_is_neg_two63);
    TEST_RUN(from_i64_of_int64_max_is_two63_minus_one);
    TEST_RUN(from_u64_holds_the_lexer_magnitude_up_to_u64_max);
    TEST_RUN(from_u64_of_two63_is_the_first_value_above_i64);
    TEST_RUN(from_char_holds_the_code_point);
    TEST_RUN(from_char_above_255_is_an_internal_error);
    TEST_RUN(from_bool_holds_zero_or_one);
    TEST_RUN(null_has_kind_null);
    TEST_RUN(from_str_keeps_the_view);
    TEST_RUN(is_int_holds_for_integers_only);
    TEST_RUN(is_int_like_admits_chars_too);
    TEST_RUN(as_int_of_a_char_is_its_code_point_d4_3);
    TEST_RUN(as_int_of_an_int_is_itself);
    TEST_RUN(as_int_of_a_bool_is_an_internal_error);
    TEST_RUN(is_zero_and_is_neg);
    TEST_RUN(to_i64_round_trips_the_i64_range);
    TEST_RUN(to_i64_rejects_values_above_int64_max);
    TEST_RUN(to_u64_accepts_non_negatives_and_rejects_negatives);
    TEST_RUN(bits_is_the_two_s_complement_pattern);
    TEST_RUN(cmp_orders_integers_across_the_whole_range);
    TEST_RUN(cmp_orders_chars_by_code_point_d3_2);
    TEST_RUN(cmp_of_an_int_and_a_char_is_an_internal_error);
    TEST_RUN(eq_compares_integers_by_value);
    TEST_RUN(eq_compares_bools_chars_nulls_and_strings);
    TEST_RUN(eq_is_false_across_kinds_and_for_none);
    TEST_RUN(compare_orders_integers);
    TEST_RUN(compare_tests_equality_of_every_kind);
    TEST_RUN(compare_orders_chars);
    TEST_RUN(compare_equality_across_kinds_is_an_internal_error);
    TEST_RUN(compare_of_none_is_an_internal_error);
    TEST_RUN(ordering_nulls_strings_and_bools_is_an_internal_error);
    TEST_RUN(logical_operators_fold_bools);
    TEST_RUN(logical_operators_on_a_non_bool_are_an_internal_error);
    TEST_RUN(to_str_writes_integers_in_decimal);
    TEST_RUN(to_str_writes_chars_as_literals);
    TEST_RUN(to_str_writes_the_other_kinds);
    TEST_RUN(to_str_escapes_a_string_as_a_literal_d2_8);
    TEST_RUN(to_str_appends_to_the_buffer);
    TEST_EXIT();
}
