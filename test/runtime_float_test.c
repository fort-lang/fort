// Unit tests of the float printers of the C runtime (toolchain.md 5.1): the
// shortest digits that round-trip in the argument's own type and the layout
// around them (D11.7, D18). They live apart from runtime_test.c so that
// neither suite's main outgrows clang-tidy's function-size threshold.
//
// Output is captured by pointing stdout at a temporary file
// (runtime_helpers.h).
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fort_rt.h"
#include "runtime_helpers.h"

#include "test.h"

// The literals below are the sample values and the expected texts of D11.7's
// formatting rules; naming each one would only hide what is being checked.
// NOLINTBEGIN(readability-magic-numbers)

// The text one print writes, captured on its own so that a case is a value and
// its expected rendering.
static void float_text(void (*body)(int32_t), char* buf, size_t size) {
    capture_t c = capture_begin(1);
    body(1);
    fort_rt_flush_all();
    TEST_UNUSED(capture_read(&c, buf, size));
    capture_end(&c);
}

static double f64_case_value = 0.0;
static float f32_case_value = 0.0F;

static void print_f64_case(int32_t fd) {
    fort_rt_print_f64(fd, f64_case_value);
}

static void print_f32_case(int32_t fd) {
    fort_rt_print_f32(fd, f32_case_value);
}

#define ASSERT_F64_TEXT(v, expected)                                                               \
    do {                                                                                           \
        char actual[CAPTURE_MAX];                                                                  \
        f64_case_value = (v);                                                                      \
        float_text(print_f64_case, actual, sizeof actual);                                         \
        ASSERT_SAME_TEXT(actual, expected);                                                        \
    } while (0)

#define ASSERT_F32_TEXT(v, expected)                                                               \
    do {                                                                                           \
        char actual[CAPTURE_MAX];                                                                  \
        f32_case_value = (v);                                                                      \
        float_text(print_f32_case, actual, sizeof actual);                                         \
        ASSERT_SAME_TEXT(actual, expected);                                                        \
    } while (0)

TEST(f64_appends_a_point_and_a_zero_when_the_text_has_neither_point_nor_exponent, {
    ASSERT_F64_TEXT(0.0, "0.0");
    ASSERT_F64_TEXT(1.0, "1.0");
    ASSERT_F64_TEXT(-1.0, "-1.0");
    ASSERT_F64_TEXT(100.0, "100.0");
    ASSERT_F64_TEXT(-100.0, "-100.0");
})

TEST(f64_keeps_the_sign_of_negative_zero, {
    ASSERT_F64_TEXT(-0.0, "-0.0");
    ASSERT_F64_TEXT(0.0, "0.0");
})

TEST(f64_prints_a_fraction_without_padding_it, {
    ASSERT_F64_TEXT(0.5, "0.5");
    ASSERT_F64_TEXT(2.5, "2.5");
    ASSERT_F64_TEXT(-2.5, "-2.5");
    ASSERT_F64_TEXT(1234.5678, "1234.5678");
})

TEST(f64_prints_the_shortest_digits_that_round_trip, {
    ASSERT_F64_TEXT(0.1, "0.1");
    ASSERT_F64_TEXT(0.2, "0.2");
    ASSERT_F64_TEXT(0.3, "0.3");
    ASSERT_F64_TEXT(0.1 + 0.2, "0.30000000000000004");
    ASSERT_F64_TEXT(1.0 / 3.0, "0.3333333333333333");
})

TEST(f64_uses_exponent_form_below_1e_minus_4_and_fixed_form_at_it, {
    ASSERT_F64_TEXT(1e-4, "0.0001");
    ASSERT_F64_TEXT(4.9e-4, "0.00049");
    ASSERT_F64_TEXT(9.999e-5, "9.999e-05");
    ASSERT_F64_TEXT(1e-5, "1e-05");
    ASSERT_F64_TEXT(3e-5, "3e-05");
    ASSERT_F64_TEXT(1.5e-7, "1.5e-07");
    ASSERT_F64_TEXT(-1.5e-7, "-1.5e-07");
})

TEST(f64_uses_fixed_form_below_1e17_and_exponent_form_from_it_on, {
    ASSERT_F64_TEXT(1e15, "1000000000000000.0");
    ASSERT_F64_TEXT(1e16, "10000000000000000.0");
    ASSERT_F64_TEXT(12345678901234567.0, "12345678901234568.0");
    ASSERT_F64_TEXT(1e17, "1e+17");
    ASSERT_F64_TEXT(-1e17, "-1e+17");
    ASSERT_F64_TEXT(1e18, "1e+18");
})

TEST(f64_writes_the_exponent_with_a_sign_and_at_least_two_digits, {
    ASSERT_F64_TEXT(1e21, "1e+21");
    ASSERT_F64_TEXT(1e100, "1e+100");
    ASSERT_F64_TEXT(1e-300, "1e-300");
    ASSERT_F64_TEXT(2.5e-9, "2.5e-09");
})

TEST(f64_prints_the_extremes_of_the_type, {
    ASSERT_F64_TEXT(DBL_MAX, "1.7976931348623157e+308");
    ASSERT_F64_TEXT(-DBL_MAX, "-1.7976931348623157e+308");
    ASSERT_F64_TEXT(DBL_MIN, "2.2250738585072014e-308");
    ASSERT_F64_TEXT(DBL_TRUE_MIN, "5e-324");
})

TEST(f64_prints_an_exact_integer_in_full, {
    ASSERT_F64_TEXT(16777217.0, "16777217.0");
    ASSERT_F64_TEXT(123456789.0, "123456789.0");
    ASSERT_F64_TEXT(9007199254740992.0, "9007199254740992.0");
    ASSERT_F64_TEXT(9007199254740994.0, "9007199254740994.0");
})

TEST(f64_prints_infinities_and_nan_by_name, {
    ASSERT_F64_TEXT(INFINITY, "inf");
    ASSERT_F64_TEXT(-INFINITY, "-inf");
    ASSERT_F64_TEXT(NAN, "nan");
    ASSERT_F64_TEXT(-NAN, "nan");
})

// A normal power of two above the minimum normal is the one place where the
// values that read back as it are not centred on it: the binade below is
// coarser, so they reach half an ulp above and only a quarter below, and the
// nearest decimal of the shortest length can fall outside while the next one
// up falls inside. Every other value, subnormals and the minimum normal
// included, has a symmetric interval. The expected texts below read back as
// the power of two they stand for, which is what makes them the answer.
TEST(f64_prints_a_power_of_two_in_the_shortest_digits_that_read_back, {
    ASSERT_F64_TEXT(ldexp(1.0, -1017), "7.120236347223045e-307");
    ASSERT_F64_TEXT(ldexp(1.0, -1007), "7.291122019556398e-304");
    ASSERT_F64_TEXT(ldexp(1.0, 87), "1.5474250491067253e+26");
    ASSERT_F64_TEXT(ldexp(1.0, 90), "1.2379400392853803e+27");
})

TEST(f32_prints_a_power_of_two_in_the_shortest_digits_that_read_back, {
    ASSERT_F32_TEXT(ldexpf(1.0F, -96), "1.2621775e-29");
    ASSERT_F32_TEXT(ldexpf(1.0F, -25), "2.9802322e-08");
})

// 2971111.75 is exactly midway between the two shortest decimals that read
// back as it, and a correctly rounded printf breaks the tie towards the even
// last digit (D18.2), as Ryu does.
TEST(f32_breaks_a_tie_between_two_shortest_digits_towards_the_even_one, {
    ASSERT_F32_TEXT(2971111.75F, "2971111.8");
    ASSERT_F32_TEXT(-2971111.75F, "-2971111.8");
})

TEST(f32_prints_the_shortest_digits_of_its_own_type, {
    ASSERT_F32_TEXT(0.1F, "0.1");
    ASSERT_F32_TEXT(1.0F / 3.0F, "0.33333334");
    ASSERT_F64_TEXT(0.1F, "0.10000000149011612");
})

TEST(f32_prints_the_value_it_holds_not_the_one_asked_for, {
    ASSERT_F32_TEXT(16777217.0F, "16777216.0");
    ASSERT_F32_TEXT(123456792.0F, "123456790.0");
    ASSERT_F64_TEXT(16777217.0, "16777217.0");
})

TEST(f32_uses_the_same_form_thresholds_as_f64, {
    ASSERT_F32_TEXT(1e-4F, "0.0001");
    ASSERT_F32_TEXT(1e-5F, "1e-05");
    ASSERT_F32_TEXT(1e16F, "10000000000000000.0");
    ASSERT_F32_TEXT(1e17F, "1e+17");
    ASSERT_F32_TEXT(1e21F, "1e+21");
})

TEST(f32_prints_the_extremes_of_the_type, {
    ASSERT_F32_TEXT(FLT_MAX, "3.4028235e+38");
    ASSERT_F32_TEXT(-FLT_MAX, "-3.4028235e+38");
    ASSERT_F32_TEXT(FLT_MIN, "1.1754944e-38");
    ASSERT_F32_TEXT(FLT_TRUE_MIN, "1e-45");
})

TEST(f32_prints_zeros_infinities_and_nan_like_f64, {
    ASSERT_F32_TEXT(0.0F, "0.0");
    ASSERT_F32_TEXT(-0.0F, "-0.0");
    ASSERT_F32_TEXT(1.0F, "1.0");
    ASSERT_F32_TEXT(-2.5F, "-2.5");
    ASSERT_F32_TEXT((float)INFINITY, "inf");
    ASSERT_F32_TEXT(-(float)INFINITY, "-inf");
    ASSERT_F32_TEXT((float)NAN, "nan");
})

TEST(f64_switches_form_at_the_last_value_before_each_threshold, {
    ASSERT_F64_TEXT(9.999999999999998e16, "99999999999999980.0");
    ASSERT_F64_TEXT(1e17, "1e+17");
    ASSERT_F64_TEXT(9.999999999999999e-5, "9.999999999999999e-05");
    ASSERT_F64_TEXT(1e-4, "0.0001");
})

TEST(f64_pads_the_integer_part_when_the_digits_run_out_before_the_point, {
    ASSERT_F64_TEXT(2e16, "20000000000000000.0");
    ASSERT_F64_TEXT(9.999999999999998e16, "99999999999999980.0");
    ASSERT_F64_TEXT(-9.999999999999998e16, "-99999999999999980.0");
})

TEST(f64_prints_seventeen_digits_when_the_value_needs_them, {
    ASSERT_F64_TEXT(0.12345678901234566, "0.12345678901234566");
    ASSERT_F64_TEXT(1.2345678901234568e-5, "1.2345678901234568e-05");
    ASSERT_F64_TEXT(123456789012345.67, "123456789012345.67");
})

TEST(f64_prints_a_subnormal_like_any_other_value, {
    ASSERT_F64_TEXT(1.1125369292536007e-308, "1.1125369292536007e-308");
    ASSERT_F64_TEXT(-1.1125369292536007e-308, "-1.1125369292536007e-308");
    // the second smallest subnormal is 9.88e-324, and the shortest decimal
    // that reads back as it stands one power of ten higher
    ASSERT_F64_TEXT(2 * DBL_TRUE_MIN, "1e-323");
    ASSERT_F64_TEXT(-2 * DBL_TRUE_MIN, "-1e-323");
})

TEST(f32_switches_form_at_the_last_value_before_each_threshold, {
    ASSERT_F32_TEXT(9.999999e-5F, "9.999999e-05");
    ASSERT_F32_TEXT(1e-4F, "0.0001");
    ASSERT_F32_TEXT(1.5e-7F, "1.5e-07");
})

static void print_floats_to(int32_t fd) {
    fort_rt_print_f64(fd, 0.5);
    fort_rt_print_char(fd, '|');
    fort_rt_print_f32(fd, 0.25F);
}

TEST(a_float_is_written_to_the_descriptor_it_is_given, {
    capture_t c = capture_begin(-1);
    print_floats_to(c.fd);
    fort_rt_flush(c.fd);
    char actual[CAPTURE_MAX];
    TEST_UNUSED(capture_read(&c, actual, sizeof actual));
    capture_end(&c);
    ASSERT_SAME_TEXT(actual, "0.5|0.25");
})

static void print_floats_in_a_row(void) {
    fort_rt_print_f64(1, 1.0);
    fort_rt_print_f64(1, -0.5);
    fort_rt_print_f32(1, 2.25F);
}

TEST(floats_are_written_one_after_another_without_separators,
     { ASSERT_STDOUT(print_floats_in_a_row, "1.0-0.52.25"); })

// A pseudo-random bit pattern read as a float: the properties below are
// checked over the whole encoding, not just the samples above.
static uint64_t next_bits(uint64_t* state) {
    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return *state;
}

// Splits printf's %e text into the integer its digits form and the power of
// ten the leading digit stands for.
static void split_exponent_text(const char* s, long long* mantissa, int* exponent) {
    long long digits = 0;
    const char* p = s;
    const bool negative = *p == '-';
    if (*p == '-' || *p == '+') {
        p++;
    }
    for (; *p != '\0' && *p != 'e'; p++) {
        if (*p >= '0' && *p <= '9') {
            digits = (digits * 10) + (*p - '0');
        }
    }
    *mantissa = negative ? -digits : digits;
    *exponent = *p == 'e' ? atoi(p + 1) : 0;
}

// The significant digits of a printed float, counted from the text: a zero
// counts only once a non-zero digit has been seen and another follows it, so
// that "0.0001" and "10000000000000000.0" both count one.
static int text_digits(const char* s) {
    int count = 0;
    int pending_zeros = 0;
    for (const char* p = s; *p != '\0' && *p != 'e'; p++) {
        if (*p < '0' || *p > '9') {
            continue;
        }
        if (*p == '0') {
            if (count > 0) {
                pending_zeros++;
            }
        } else {
            count += pending_zeros + 1;
            pending_zeros = 0;
        }
    }
    return count == 0 ? 1 : count;
}

// The fewest significant digits a decimal needs to read back as v, computed
// without the runtime: at a given length only the nearest decimal and its two
// neighbours can read back as v, anything else being farther away than one of
// them, so those three are tried and the length grows until one works.
static int shortest_digits_f64(double v) {
    for (int precision = 0; precision + 1 < 17; precision++) {
        char nearest[64];
        TEST_UNUSED(snprintf(nearest, sizeof nearest, "%.*e", precision, v));
        long long mantissa = 0;
        int exponent = 0;
        split_exponent_text(nearest, &mantissa, &exponent);
        for (long long m = mantissa - 1; m <= mantissa + 1; m++) {
            char candidate[64];
            TEST_UNUSED(snprintf(candidate, sizeof candidate, "%llde%d", m, exponent - precision));
            if (strtod(candidate, NULL) == v) {
                return precision + 1;
            }
        }
    }
    return 17;
}

static int shortest_digits_f32(float v) {
    for (int precision = 0; precision + 1 < 9; precision++) {
        char nearest[64];
        TEST_UNUSED(snprintf(nearest, sizeof nearest, "%.*e", precision, (double)v));
        long long mantissa = 0;
        int exponent = 0;
        split_exponent_text(nearest, &mantissa, &exponent);
        for (long long m = mantissa - 1; m <= mantissa + 1; m++) {
            char candidate[64];
            TEST_UNUSED(snprintf(candidate, sizeof candidate, "%llde%d", m, exponent - precision));
            if (strtof(candidate, NULL) == v) {
                return precision + 1;
            }
        }
    }
    return 9;
}

TEST(every_f64_pattern_prints_the_shortest_digits_that_read_back_as_it, {
    uint64_t state = 1;
    for (int i = 0; i < 200; i++) {
        const uint64_t bits = next_bits(&state);
        double v = 0.0;
        (void)memcpy(&v, &bits, sizeof v);
        if (v != v || v > DBL_MAX || v < -DBL_MAX) {
            continue;
        }
        char actual[CAPTURE_MAX];
        f64_case_value = v;
        float_text(print_f64_case, actual, sizeof actual);
        TEST_ASSERT_TRUE(strtod(actual, NULL) == v);
        TEST_ASSERT_EQ_INT32(text_digits(actual), shortest_digits_f64(v));
    }
})

// Every power of two, where the values that read back are not centred on the
// value and the nearest decimal of the shortest length can miss.
TEST(every_f64_power_of_two_prints_the_shortest_digits_that_read_back_as_it, {
    for (int e = -1074; e <= 1023; e++) {
        const double v = ldexp(1.0, e);
        char actual[CAPTURE_MAX];
        f64_case_value = v;
        float_text(print_f64_case, actual, sizeof actual);
        TEST_ASSERT_TRUE(strtod(actual, NULL) == v);
        TEST_ASSERT_EQ_INT32(text_digits(actual), shortest_digits_f64(v));
    }
})

TEST(every_f32_pattern_prints_the_shortest_digits_that_read_back_as_it, {
    uint64_t state = 7;
    for (int i = 0; i < 200; i++) {
        const uint32_t bits = (uint32_t)(next_bits(&state) >> 32);
        float v = 0.0F;
        (void)memcpy(&v, &bits, sizeof v);
        if (v != v || v > FLT_MAX || v < -FLT_MAX) {
            continue;
        }
        char actual[CAPTURE_MAX];
        f32_case_value = v;
        float_text(print_f32_case, actual, sizeof actual);
        TEST_ASSERT_TRUE(strtof(actual, NULL) == v);
        TEST_ASSERT_EQ_INT32(text_digits(actual), shortest_digits_f32(v));
    }
})

TEST(every_f32_power_of_two_prints_the_shortest_digits_that_read_back_as_it, {
    for (int e = -149; e <= 127; e++) {
        const float v = ldexpf(1.0F, e);
        char actual[CAPTURE_MAX];
        f32_case_value = v;
        float_text(print_f32_case, actual, sizeof actual);
        TEST_ASSERT_TRUE(strtof(actual, NULL) == v);
        TEST_ASSERT_EQ_INT32(text_digits(actual), shortest_digits_f32(v));
    }
})

int main(int argc, char** argv) {
    TEST_INIT("runtime_float", argc, argv);
    TEST_RUN(f64_appends_a_point_and_a_zero_when_the_text_has_neither_point_nor_exponent);
    TEST_RUN(f64_keeps_the_sign_of_negative_zero);
    TEST_RUN(f64_prints_a_fraction_without_padding_it);
    TEST_RUN(f64_prints_the_shortest_digits_that_round_trip);
    TEST_RUN(f64_uses_exponent_form_below_1e_minus_4_and_fixed_form_at_it);
    TEST_RUN(f64_uses_fixed_form_below_1e17_and_exponent_form_from_it_on);
    TEST_RUN(f64_writes_the_exponent_with_a_sign_and_at_least_two_digits);
    TEST_RUN(f64_prints_the_extremes_of_the_type);
    TEST_RUN(f64_prints_an_exact_integer_in_full);
    TEST_RUN(f64_prints_infinities_and_nan_by_name);
    TEST_RUN(f32_prints_the_shortest_digits_of_its_own_type);
    TEST_RUN(f32_prints_the_value_it_holds_not_the_one_asked_for);
    TEST_RUN(f32_uses_the_same_form_thresholds_as_f64);
    TEST_RUN(f32_prints_the_extremes_of_the_type);
    TEST_RUN(f32_prints_zeros_infinities_and_nan_like_f64);
    TEST_RUN(f64_switches_form_at_the_last_value_before_each_threshold);
    TEST_RUN(f64_pads_the_integer_part_when_the_digits_run_out_before_the_point);
    TEST_RUN(f64_prints_seventeen_digits_when_the_value_needs_them);
    TEST_RUN(f64_prints_a_subnormal_like_any_other_value);
    TEST_RUN(f32_switches_form_at_the_last_value_before_each_threshold);
    TEST_RUN(a_float_is_written_to_the_descriptor_it_is_given);
    TEST_RUN(floats_are_written_one_after_another_without_separators);
    TEST_RUN(f64_prints_a_power_of_two_in_the_shortest_digits_that_read_back);
    TEST_RUN(f32_prints_a_power_of_two_in_the_shortest_digits_that_read_back);
    TEST_RUN(f32_breaks_a_tie_between_two_shortest_digits_towards_the_even_one);
    TEST_RUN(every_f64_pattern_prints_the_shortest_digits_that_read_back_as_it);
    TEST_RUN(every_f64_power_of_two_prints_the_shortest_digits_that_read_back_as_it);
    TEST_RUN(every_f32_pattern_prints_the_shortest_digits_that_read_back_as_it);
    TEST_RUN(every_f32_power_of_two_prints_the_shortest_digits_that_read_back_as_it);
    TEST_EXIT();
}

// NOLINTEND(readability-magic-numbers)
