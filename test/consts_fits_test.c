// Unit tests of consts.h, part three: giving a constant a type. The prim.h
// queries, cv_fits and cv_default_kind. The casts and the typed folding are
// in consts_typed_test.c, the untyped folding in consts_fold_test.c.
// D3.1, D4.2, D4.3, D4.5
#include <stdint.h>

#include "consts.h"
#include "prim.h"
#include "str.h"

#include "test.h"

// The sample values below are the test data: the edges of every integer
// type and the examples of type-system.md 10 and core-language.md 5.9.
// NOLINTBEGIN(readability-magic-numbers)

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

// Whether `v` fits exactly the kinds listed in `mask`, a bit per prim_kind_t
// over the integer and float kinds, `char` and `bool`.
static bool fits_exactly(cval_t v, unsigned mask) {
    for (int k = 0; k < PRIM_COUNT; k++) {
        const bool expect = (mask & (1U << (unsigned)k)) != 0;
        if (cv_fits(v, (prim_kind_t)k) != expect) {
            return false;
        }
    }
    return true;
}

enum {
    F_I8 = 1U << PRIM_I8,
    F_I16 = 1U << PRIM_I16,
    F_I32 = 1U << PRIM_I32,
    F_I64 = 1U << PRIM_I64,
    F_U8 = 1U << PRIM_U8,
    F_U16 = 1U << PRIM_U16,
    F_U32 = 1U << PRIM_U32,
    F_U64 = 1U << PRIM_U64,
    F_FLOATS = (1U << PRIM_F32) | (1U << PRIM_F64),
    F_BOOL = 1U << PRIM_BOOL,
    F_CHAR = 1U << PRIM_CHAR,
    F_SIGNED = F_I8 | F_I16 | F_I32 | F_I64,
    F_UNSIGNED = F_U8 | F_U16 | F_U32 | F_U64,
    F_INTS = F_SIGNED | F_UNSIGNED,
};

// ---- prim.h ----------------------------------------------------------------------

TEST(prim_is_integer_is_i8_to_u64, {
    TEST_ASSERT_TRUE(prim_is_integer(PRIM_I8));
    TEST_ASSERT_TRUE(prim_is_integer(PRIM_I64));
    TEST_ASSERT_TRUE(prim_is_integer(PRIM_U8));
    TEST_ASSERT_TRUE(prim_is_integer(PRIM_U64));
    TEST_ASSERT_FALSE(prim_is_integer(PRIM_F32));
    TEST_ASSERT_FALSE(prim_is_integer(PRIM_F64));
    TEST_ASSERT_FALSE(prim_is_integer(PRIM_BOOL));
    TEST_ASSERT_FALSE(prim_is_integer(PRIM_CHAR));
    TEST_ASSERT_FALSE(prim_is_integer(PRIM_VOID));
})

TEST(prim_is_signed_is_i8_to_i64, {
    TEST_ASSERT_TRUE(prim_is_signed(PRIM_I8));
    TEST_ASSERT_TRUE(prim_is_signed(PRIM_I16));
    TEST_ASSERT_TRUE(prim_is_signed(PRIM_I32));
    TEST_ASSERT_TRUE(prim_is_signed(PRIM_I64));
    TEST_ASSERT_FALSE(prim_is_signed(PRIM_U8));
    TEST_ASSERT_FALSE(prim_is_signed(PRIM_U64));
    TEST_ASSERT_FALSE(prim_is_signed(PRIM_CHAR));
})

TEST(prim_is_float_is_f32_and_f64, {
    TEST_ASSERT_TRUE(prim_is_float(PRIM_F32));
    TEST_ASSERT_TRUE(prim_is_float(PRIM_F64));
    TEST_ASSERT_FALSE(prim_is_float(PRIM_I32));
    TEST_ASSERT_FALSE(prim_is_float(PRIM_VOID));
})

TEST(prim_size_follows_d3_1, {
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_I8), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_U8), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_BOOL), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_CHAR), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_I16), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_U16), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_I32), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_U32), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_F32), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_I64), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_U64), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_F64), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64((uint64_t)prim_size(PRIM_VOID), (uint64_t)0);
})

TEST(prim_name_spells_every_type, {
    TEST_ASSERT_EQ_STR(prim_name(PRIM_I8), "i8");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_I16), "i16");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_I32), "i32");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_I64), "i64");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_U8), "u8");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_U16), "u16");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_U32), "u32");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_U64), "u64");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_F32), "f32");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_F64), "f64");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_BOOL), "bool");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_CHAR), "char");
    TEST_ASSERT_EQ_STR(prim_name(PRIM_VOID), "void");
    TEST_ASSERT_EQ_INT32(PRIM_COUNT, 13);
})

// ---- cv_fits: every integer kind at both edges -------------------------------------
// D4.2

TEST(fits_i8_at_its_edges, {
    TEST_ASSERT_TRUE(fits_exactly(i(-128), F_SIGNED | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(-129), F_I16 | F_I32 | F_I64 | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(127), F_INTS | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(128), (F_INTS & ~F_I8) | F_FLOATS));
})

TEST(fits_u8_at_its_edges, {
    TEST_ASSERT_TRUE(fits_exactly(i(0), F_INTS | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(-1), F_SIGNED | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(255), (F_INTS & ~F_I8) | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(256), (F_INTS & ~(F_I8 | F_U8)) | F_FLOATS));
})

TEST(fits_i16_at_its_edges, {
    TEST_ASSERT_TRUE(fits_exactly(i(-32768), (F_SIGNED & ~F_I8) | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(-32769), F_I32 | F_I64 | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(32767), (F_INTS & ~(F_I8 | F_U8)) | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(32768), (F_INTS & ~(F_I8 | F_U8 | F_I16)) | F_FLOATS));
})

TEST(fits_u16_at_its_edges, {
    TEST_ASSERT_TRUE(fits_exactly(i(65535), (F_I32 | F_I64 | F_U16 | F_U32 | F_U64) | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(65536), (F_I32 | F_I64 | F_U32 | F_U64) | F_FLOATS));
})

TEST(fits_i32_at_its_edges, {
    TEST_ASSERT_TRUE(fits_exactly(i(INT32_MIN), F_I32 | F_I64 | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i((int64_t)INT32_MIN - 1), F_I64 | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(INT32_MAX), (F_I32 | F_I64 | F_U32 | F_U64) | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i((int64_t)INT32_MAX + 1), (F_I64 | F_U32 | F_U64) | F_FLOATS));
})

TEST(fits_u32_at_its_edges, {
    TEST_ASSERT_TRUE(fits_exactly(i(UINT32_MAX), (F_I64 | F_U32 | F_U64) | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i((int64_t)UINT32_MAX + 1), (F_I64 | F_U64) | F_FLOATS));
})

TEST(fits_i64_at_its_edges, {
    TEST_ASSERT_TRUE(fits_exactly(neg_two63(), F_I64 | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(i(INT64_MAX), (F_I64 | F_U64) | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(u(TWO63), F_U64 | F_FLOATS));
})

TEST(fits_u64_at_its_edge, {
    TEST_ASSERT_TRUE(fits_exactly(u(U64_MAX), F_U64 | F_FLOATS));
    TEST_ASSERT_TRUE(fits_exactly(u(U64_MAX - 1U), F_U64 | F_FLOATS));
})

TEST(fits_0x80000000_u32_and_i64_but_not_i32, {
    const cval_t v = u(UINT64_C(0x80000000));
    TEST_ASSERT_TRUE(cv_fits(v, PRIM_U32));
    TEST_ASSERT_TRUE(cv_fits(v, PRIM_I64));
    TEST_ASSERT_TRUE(cv_fits(v, PRIM_U64));
    TEST_ASSERT_FALSE(cv_fits(v, PRIM_I32));
    TEST_ASSERT_FALSE(cv_fits(v, PRIM_U16));
})

TEST(fits_examples_of_type_system_10_1_and_10_2, {
    TEST_ASSERT_TRUE(cv_fits(i(1), PRIM_I64));   // i64 a = 1
    TEST_ASSERT_TRUE(cv_fits(i(1), PRIM_U8));    // u8 b = 1
    TEST_ASSERT_TRUE(cv_fits(i(1), PRIM_F64));   // f64 c = 1
    TEST_ASSERT_TRUE(cv_fits(i(255), PRIM_U8));  // u8 ok = 255
    TEST_ASSERT_FALSE(cv_fits(i(256), PRIM_U8)); // u8 e1 = 256
    TEST_ASSERT_FALSE(cv_fits(i(-1), PRIM_U32)); // u32 e2 = -1
    TEST_ASSERT_TRUE(cv_fits(i(2), PRIM_F32));   // f32 ok2 = 2
})

TEST(fits_examples_of_type_system_10_4, {
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_not(i(0), &r));
    TEST_ASSERT_FALSE(cv_fits(r, PRIM_U32));                      // u32 m = ~0
    TEST_ASSERT_TRUE(cv_fits(u(UINT64_C(0xFFFFFFFF)), PRIM_U32)); // u32 m2
    TEST_ASSERT_TRUE(cv_shl(i(1), i(40), &r));
    TEST_ASSERT_FALSE(cv_fits(r, PRIM_I32)); // i32 n = 1 << 40
    TEST_ASSERT_TRUE(cv_shl(i(1), i(31), &r));
    // D4.4
    TEST_ASSERT_FALSE(cv_fits(r, PRIM_I32)); // i32 x = 1 << 31
    TEST_ASSERT_TRUE(cv_fits(r, PRIM_U32));
    TEST_ASSERT_FALSE(cv_shl(i(1), i(64), &r)); // u64 big = 1 << 64
    TEST_ASSERT_FALSE(cv_div(i(1), i(0), &r));  // i32 z = 1 / 0
})

// ---- cv_fits: chars, bools and the rest ----------------------------------------------
// D4.3

TEST(fits_char_literal_in_char_and_integer_contexts, {
    // u8 b = 'a' is 97; 'a' also fits i8 and every wider integer.
    TEST_ASSERT_TRUE(fits_exactly(cv_from_char('a'), F_INTS | F_CHAR));
    TEST_ASSERT_TRUE(fits_exactly(cv_from_char(0), F_INTS | F_CHAR));
    TEST_ASSERT_TRUE(fits_exactly(cv_from_char(127), F_INTS | F_CHAR));
})

TEST(fits_char_above_127_skips_i8, {
    TEST_ASSERT_TRUE(fits_exactly(cv_from_char(128), (F_INTS & ~F_I8) | F_CHAR));
    TEST_ASSERT_TRUE(fits_exactly(cv_from_char(255), (F_INTS & ~F_I8) | F_CHAR));
})

TEST(fits_integer_never_becomes_char, {
    TEST_ASSERT_FALSE(cv_fits(i(65), PRIM_CHAR)); // char e = 65
    TEST_ASSERT_FALSE(cv_fits(i(0), PRIM_CHAR));
    TEST_ASSERT_FALSE(cv_fits(i(255), PRIM_CHAR));
})

TEST(fits_bool_only_in_bool, {
    TEST_ASSERT_TRUE(fits_exactly(cv_from_bool(true), F_BOOL));
    TEST_ASSERT_TRUE(fits_exactly(cv_from_bool(false), F_BOOL));
    TEST_ASSERT_FALSE(cv_fits(i(1), PRIM_BOOL));
    TEST_ASSERT_FALSE(cv_fits(cv_from_char(1), PRIM_BOOL));
})

TEST(fits_nothing_for_null_strings_none_and_void, {
    TEST_ASSERT_TRUE(fits_exactly(cv_null(), 0));
    TEST_ASSERT_TRUE(fits_exactly(cv_from_str(str_from_cstr("a")), 0));
    TEST_ASSERT_TRUE(fits_exactly(cv_none(), 0));
    TEST_ASSERT_FALSE(cv_fits(i(0), PRIM_VOID));
    TEST_ASSERT_FALSE(cv_fits(cv_from_char('a'), PRIM_VOID));
})

// ---- cv_default_kind --------------------------------------------------------------
// D4.5

TEST(default_kind_of_a_small_integer_is_i32, {
    prim_kind_t k = PRIM_VOID;
    TEST_ASSERT_TRUE(cv_default_kind(i(7), &k)); // print(7)
    TEST_ASSERT_TRUE(k == PRIM_I32);
    TEST_ASSERT_TRUE(cv_default_kind(i(0), &k));
    TEST_ASSERT_TRUE(k == PRIM_I32);
    TEST_ASSERT_TRUE(cv_default_kind(i(-7), &k));
    TEST_ASSERT_TRUE(k == PRIM_I32);
    TEST_ASSERT_TRUE(cv_default_kind(i(INT32_MIN), &k));
    TEST_ASSERT_TRUE(k == PRIM_I32);
    TEST_ASSERT_TRUE(cv_default_kind(i(INT32_MAX), &k));
    TEST_ASSERT_TRUE(k == PRIM_I32);
})

TEST(default_kind_of_a_large_integer_is_i64, {
    prim_kind_t k = PRIM_VOID;
    TEST_ASSERT_TRUE(cv_default_kind(i(INT64_C(3000000000)), &k)); // print(3000000000)
    TEST_ASSERT_TRUE(k == PRIM_I64);
    TEST_ASSERT_TRUE(cv_default_kind(i((int64_t)INT32_MAX + 1), &k));
    TEST_ASSERT_TRUE(k == PRIM_I64);
    TEST_ASSERT_TRUE(cv_default_kind(i((int64_t)INT32_MIN - 1), &k));
    TEST_ASSERT_TRUE(k == PRIM_I64);
    TEST_ASSERT_TRUE(cv_default_kind(u(UINT64_C(0x80000000)), &k));
    TEST_ASSERT_TRUE(k == PRIM_I64);
    TEST_ASSERT_TRUE(cv_default_kind(i(INT64_MAX), &k));
    TEST_ASSERT_TRUE(k == PRIM_I64);
    TEST_ASSERT_TRUE(cv_default_kind(neg_two63(), &k));
    TEST_ASSERT_TRUE(k == PRIM_I64);
})

TEST(default_kind_of_an_integer_above_i64_is_an_error, {
    prim_kind_t k = PRIM_VOID;
    cval_t r = cv_none();
    TEST_ASSERT_TRUE(cv_shl(i(1), i(63), &r));
    TEST_ASSERT_FALSE(cv_default_kind(r, &k)); // print(1 << 63)
    TEST_ASSERT_FALSE(cv_default_kind(u(U64_MAX), &k));
    TEST_ASSERT_TRUE(k == PRIM_VOID);
})

TEST(default_kind_of_a_char_is_char_and_of_a_bool_is_bool, {
    prim_kind_t k = PRIM_VOID;
    TEST_ASSERT_TRUE(cv_default_kind(cv_from_char('a'), &k)); // print('a')
    TEST_ASSERT_TRUE(k == PRIM_CHAR);
    TEST_ASSERT_TRUE(cv_default_kind(cv_from_bool(true), &k));
    TEST_ASSERT_TRUE(k == PRIM_BOOL);
})

TEST(default_kind_of_null_strings_and_none_is_an_error, {
    prim_kind_t k = PRIM_VOID;
    TEST_ASSERT_FALSE(cv_default_kind(cv_null(), &k)); // print(null)
    TEST_ASSERT_FALSE(cv_default_kind(cv_from_str(str_from_cstr("a")), &k));
    TEST_ASSERT_FALSE(cv_default_kind(cv_none(), &k));
    TEST_ASSERT_TRUE(k == PRIM_VOID);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("consts_fits", argc, argv);
    TEST_RUN(prim_is_integer_is_i8_to_u64);
    TEST_RUN(prim_is_signed_is_i8_to_i64);
    TEST_RUN(prim_is_float_is_f32_and_f64);
    TEST_RUN(prim_size_follows_d3_1);
    TEST_RUN(prim_name_spells_every_type);
    TEST_RUN(fits_i8_at_its_edges);
    TEST_RUN(fits_u8_at_its_edges);
    TEST_RUN(fits_i16_at_its_edges);
    TEST_RUN(fits_u16_at_its_edges);
    TEST_RUN(fits_i32_at_its_edges);
    TEST_RUN(fits_u32_at_its_edges);
    TEST_RUN(fits_i64_at_its_edges);
    TEST_RUN(fits_u64_at_its_edge);
    TEST_RUN(fits_0x80000000_u32_and_i64_but_not_i32);
    TEST_RUN(fits_examples_of_type_system_10_1_and_10_2);
    TEST_RUN(fits_examples_of_type_system_10_4);
    TEST_RUN(fits_char_literal_in_char_and_integer_contexts);
    TEST_RUN(fits_char_above_127_skips_i8);
    TEST_RUN(fits_integer_never_becomes_char);
    TEST_RUN(fits_bool_only_in_bool);
    TEST_RUN(fits_nothing_for_null_strings_none_and_void);
    TEST_RUN(default_kind_of_a_small_integer_is_i32);
    TEST_RUN(default_kind_of_a_large_integer_is_i64);
    TEST_RUN(default_kind_of_an_integer_above_i64_is_an_error);
    TEST_RUN(default_kind_of_a_char_is_char_and_of_a_bool_is_bool);
    TEST_RUN(default_kind_of_null_strings_and_none_is_an_error);
    TEST_EXIT();
}
