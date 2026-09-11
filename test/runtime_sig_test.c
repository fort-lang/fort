// Unit tests of the runtime entry-point table (runtime_sig.h, toolchain.md
// 5.1): the name lookup, the arity and result of every row, the `_Noreturn`
// set, the IR text of every form, the `declare` line each row renders, the
// agreement of every row with the C prototype in `runtime/fort_rt.h`, and the
// form a fort type takes at the C boundary. The table is the one description
// of an entry point the compiler holds, so the emitter's declarations
// (test/gen_decl_test.c) and the checker's comparison against an `extern fn`
// (test/check_conv_test.c) both answer from what is asserted here.
#include "runtime_sig.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "containers.h"
#include "fort_rt.h"
#include "gen.h"
#include "prim.h"
#include "str.h"
#include "types.h"

#include "test.h"

static type_table_t types;
static bool types_live = false;

static void types_begin(void) {
    if (types_live) {
        type_table_free(&types);
    }
    type_table_init(&types);
    types_live = true;
}

static void types_end(void) {
    if (types_live) {
        type_table_free(&types);
        types_live = false;
    }
}

static const type_t* prim(prim_kind_t k) {
    return type_prim(&types, k);
}

// The IR form of a fort type, as its text, so a failing assertion shows the
// spelling rather than a number.
static const char* form_of(const type_t* t) {
    return ir_param_text(ir_form_of_type(t));
}

// The parameter text the emitter would write for `t` at an extern call site
// and in an extern declaration: the value type of item 7 with the extension
// attribute of D9.9 after it. The comparison below holds ir_form_of_type
// against it, since a second mapping from a fort type to an IR type is what
// would let the check of D9.8 pass a call the emitter writes differently.
static const char* emitted_param(const type_t* t) {
    static char text[64];
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    gen_t g;
    gen_init(&g, opts);
    const str_t ty = gen_value_type(&g, t);
    const char* attr = gen_ext_attr(t);
    uint64_t n = 0;
    while (n < ty.len && n + 1 < sizeof text) {
        text[n] = ty.ptr[n];
        n++;
    }
    text[n] = '\0';
    if (attr != NULL) {
        TEST_UNUSED(strncat(text, " ", sizeof text - strlen(text) - 1));
        TEST_UNUSED(strncat(text, attr, sizeof text - strlen(text) - 1));
    }
    gen_free(&g);
    return text;
}

// ---- the table (toolchain.md 5.1) --------------------------------------------------

TEST(a_c_name_finds_its_entry_point_and_nothing_else_does, {
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_rt_new")) == RT_NEW);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_rt_exit")) == RT_EXIT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_rt_print_enum")) == RT_PRINT_ENUM);
    // A name outside the `fort_rt_` space, and a name inside it that no entry
    // point takes: the list of section 5.1 is complete (D11.6).
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("write")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_rt_print")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_rt_news")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_entry")) == RT_COUNT);
})

TEST(every_row_names_the_entry_point_its_enumerator_does, {
    // The enum of runtime_sig.h is positional, so a row inserted in the middle of
    // section 5.1's order moves every later one: each name is held against
    // the enumerator it is indexed by.
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_NEW), "fort_rt_new");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_DEL), "fort_rt_del");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_STR_EQ), "fort_rt_str_eq");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_BOUNDS), "fort_rt_fail_bounds");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_SPAN), "fort_rt_fail_span");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_OVERFLOW), "fort_rt_fail_overflow");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_SHIFT), "fort_rt_fail_shift");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_DIV_ZERO), "fort_rt_fail_div_zero");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_DIV_OVERFLOW), "fort_rt_fail_div_overflow");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_ALLOC_COUNT), "fort_rt_fail_alloc_count");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_OVERWRITE), "fort_rt_fail_overwrite");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_ENUM), "fort_rt_fail_enum");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PANIC), "fort_rt_panic");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ASSERT_FAIL), "fort_rt_assert_fail");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_I64), "fort_rt_print_i64");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_U64), "fort_rt_print_u64");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_F32), "fort_rt_print_f32");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_F64), "fort_rt_print_f64");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_BOOL), "fort_rt_print_bool");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_CHAR), "fort_rt_print_char");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_PTR), "fort_rt_print_ptr");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_STR), "fort_rt_print_str");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_ENUM), "fort_rt_print_enum");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FLUSH), "fort_rt_flush");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FLUSH_ALL), "fort_rt_flush_all");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ARGS_INIT), "fort_rt_args_init");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ARGS_PTR), "fort_rt_args_ptr");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ARGS_LEN), "fort_rt_args_len");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_EXIT), "fort_rt_exit");
})

TEST(the_four_entry_points_that_return_a_value_are_the_only_ones, {
    // fort_rt_new, fort_rt_str_eq, fort_rt_args_ptr and fort_rt_args_len
    // (section 5.1); every other row is `void`.
    uint64_t returning = 0;
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        if (rt_entry_result((rt_entry_t)i) != IR_VOID) {
            returning++;
        }
    }
    TEST_ASSERT_EQ_UINT64(returning, (uint64_t)4);
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_NEW)), "ptr");
    // A narrow result carries its extension attribute before the type, which
    // is what licenses eliding the caller's re-narrowing (D9.9, item 7).
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_STR_EQ)), "zeroext i8");
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_ARGS_PTR)), "ptr");
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_ARGS_LEN)), "i64");
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_DEL)), "void");
})

TEST(the_noreturn_entry_points_are_the_ones_section_5_1_names, {
    // Every `fort_rt_fail_*` function, `fort_rt_panic`, `fort_rt_assert_fail`
    // and `fort_rt_exit` is `_Noreturn`, and nothing else is: that is what
    // puts `cold noreturn nounwind` on the declaration (item 14).
    uint64_t noreturn = 0;
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        const rt_entry_t rt = (rt_entry_t)i;
        const bool named =
            strncmp(rt_entry_name(rt), "fort_rt_fail_", strlen("fort_rt_fail_")) == 0;
        const bool listed = named || rt == RT_PANIC || rt == RT_ASSERT_FAIL || rt == RT_EXIT;
        TEST_ASSERT_TRUE(rt_entry_noreturn(rt) == listed);
        if (rt_entry_noreturn(rt)) {
            noreturn++;
        }
    }
    TEST_ASSERT_EQ_UINT64(noreturn, (uint64_t)12);
})

TEST(the_parameter_list_of_a_row_is_the_c_prototype_of_section_5_1, {
    // `loc` is the three parameters `ptr, i32, i32`, so the arity of a
    // failure entry point counts them.
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_NEW), (uint64_t)5);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_DEL), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_STR_EQ), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_FAIL_SPAN), (uint64_t)6);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_FAIL_OVERFLOW), (uint64_t)3);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_PRINT_ENUM), (uint64_t)4);
    // The rows with no parameter at all: the arity is read off the row, so an
    // empty list is not a miscount of a padded one.
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_FLUSH_ALL), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_ARGS_PTR), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_ARGS_LEN), (uint64_t)0);
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_NEW, 0)), "i64");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_NEW, 2)), "ptr");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_NEW, 4)), "i32");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_BOOL, 1)), "i8 zeroext");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_CHAR, 1)), "i8 zeroext");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_F32, 1)), "float");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_F64, 1)), "double");
})

// Every scalar an extern signature may use (D9.8), as a file-scope table: a
// brace initializer inside a TEST body would split the one macro argument.
static const prim_kind_t SCALARS[] = {PRIM_I8,
                                      PRIM_I16,
                                      PRIM_I32,
                                      PRIM_I64,
                                      PRIM_U8,
                                      PRIM_U16,
                                      PRIM_U32,
                                      PRIM_U64,
                                      PRIM_BOOL,
                                      PRIM_CHAR,
                                      PRIM_F32,
                                      PRIM_F64};

// The declaration list of toolchain.md 6 item 8, byte for byte and in its
// order. What it witnesses is the rendering: the spelling of every form, the
// `#2` of a `_Noreturn` row, the order of the group and the text the emitter
// used to hold as a table of its own. What it cannot witness is a wrong row,
// since a hand-written golden agrees with whatever was written beside it --
// the mutation that gave `fort_rt_print_f64` an `i64` parameter passed here
// once the line was edited to match. That question is
// every_row_is_the_c_prototype_of_the_runtime_header's, which reads the C
// header instead. One entry per row of RT_SIG, which is why the array is at
// file scope and not in the TEST body.
static const char* const DECLARATIONS[RT_COUNT] = {
    "declare ptr @fort_rt_new(i64, i64, ptr, i32, i32)\n",
    "declare void @fort_rt_del(ptr)\n",
    "declare zeroext i8 @fort_rt_str_eq(ptr, i64, ptr, i64)\n",
    "declare void @fort_rt_fail_bounds(i64, i64, ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_span(i64, i64, i64, ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_overflow(ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_shift(i64, ptr, ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_div_zero(ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_div_overflow(ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_alloc_count(i64, ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_overwrite(ptr, i32, i32) #2\n",
    "declare void @fort_rt_fail_enum(i64, ptr, ptr, i32, i32) #2\n",
    "declare void @fort_rt_panic(ptr, i64, ptr, i32, i32) #2\n",
    "declare void @fort_rt_assert_fail(ptr, ptr, i32, i32) #2\n",
    "declare void @fort_rt_print_i64(i32, i64)\n",
    "declare void @fort_rt_print_u64(i32, i64)\n",
    "declare void @fort_rt_print_f32(i32, float)\n",
    "declare void @fort_rt_print_f64(i32, double)\n",
    "declare void @fort_rt_print_bool(i32, i8 zeroext)\n",
    "declare void @fort_rt_print_char(i32, i8 zeroext)\n",
    "declare void @fort_rt_print_ptr(i32, ptr)\n",
    "declare void @fort_rt_print_str(i32, ptr, i64)\n",
    "declare void @fort_rt_print_enum(i32, i32, ptr, i64)\n",
    "declare void @fort_rt_flush(i32)\n",
    "declare void @fort_rt_flush_all()\n",
    "declare void @fort_rt_args_init(i32, ptr)\n",
    "declare ptr @fort_rt_args_ptr()\n",
    "declare i64 @fort_rt_args_len()\n",
    "declare void @fort_rt_exit(i32) #2\n",
};

TEST(every_entry_point_renders_the_declaration_of_item_8, {
    sb_t out;
    sb_init(&out);
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        sb_clear(&out);
        rt_declaration(&out, (rt_entry_t)i);
        TEST_ASSERT_EQ_STR(sb_cstr(&out), DECLARATIONS[i]);
    }
    sb_free(&out);
})

// ---- the C prototypes of runtime/fort_rt.h (D13.1) ---------------------------------

// The one place the rows of RT_SIG are held against the C runtime they
// describe. Each row carries the enumerator, the C name, the C result type
// with the macro that checks it, the C parameter list and the macro that
// unpacks it, and it is expanded twice: once into a `_Static_assert` whose
// `_Generic` matches the function's real type in `runtime/fort_rt.h`, which
// pins the row's C column to the header, and once into a runtime check that
// every form of RT_SIG is the form of the C type beside it, which pins the
// table to that column. Neither half alone is a witness: the first compares
// the row with the header, the second the row with the table, and only both
// make a wrong form in RT_SIG fail.
//
// What no construct can witness is the `_Noreturn` column: `_Noreturn` is a
// function specifier and not part of the function's type (C11 6.7.4), so
// `_Generic` cannot see it and neither can anything else. That column is
// checked against `toolchain.md` 5.1 by reading, here and in the table.
//
// The parameter list is written once per row. The unpacking macro names the
// arity, so a list whose length disagrees with it fails to compile with the
// wrong number of macro arguments.
#define RT_ENTRIES(X)                                                                              \
    X(RT_NEW,                                                                                      \
      fort_rt_new,                                                                                 \
      void*,                                                                                       \
      RT_RVAL,                                                                                     \
      (uint64_t, uint64_t, const char*, uint32_t, uint32_t),                                       \
      RT_P5)                                                                                       \
    X(RT_DEL, fort_rt_del, void, RT_RVOID, (void*), RT_P1)                                         \
    X(RT_STR_EQ,                                                                                   \
      fort_rt_str_eq,                                                                              \
      uint8_t,                                                                                     \
      RT_RVAL,                                                                                     \
      (const char*, uint64_t, const char*, uint64_t),                                              \
      RT_P4)                                                                                       \
    X(RT_FAIL_BOUNDS,                                                                              \
      fort_rt_fail_bounds,                                                                         \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (int64_t, uint64_t, const char*, uint32_t, uint32_t),                                        \
      RT_P5)                                                                                       \
    X(RT_FAIL_SPAN,                                                                                \
      fort_rt_fail_span,                                                                           \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (int64_t, int64_t, uint64_t, const char*, uint32_t, uint32_t),                               \
      RT_P6)                                                                                       \
    X(RT_FAIL_OVERFLOW,                                                                            \
      fort_rt_fail_overflow,                                                                       \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (const char*, uint32_t, uint32_t),                                                           \
      RT_P3)                                                                                       \
    X(RT_FAIL_SHIFT,                                                                               \
      fort_rt_fail_shift,                                                                          \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (int64_t, const char*, const char*, uint32_t, uint32_t),                                     \
      RT_P5)                                                                                       \
    X(RT_FAIL_DIV_ZERO,                                                                            \
      fort_rt_fail_div_zero,                                                                       \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (const char*, uint32_t, uint32_t),                                                           \
      RT_P3)                                                                                       \
    X(RT_FAIL_DIV_OVERFLOW,                                                                        \
      fort_rt_fail_div_overflow,                                                                   \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (const char*, uint32_t, uint32_t),                                                           \
      RT_P3)                                                                                       \
    X(RT_FAIL_ALLOC_COUNT,                                                                         \
      fort_rt_fail_alloc_count,                                                                    \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (int64_t, const char*, uint32_t, uint32_t),                                                  \
      RT_P4)                                                                                       \
    X(RT_FAIL_OVERWRITE,                                                                           \
      fort_rt_fail_overwrite,                                                                      \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (const char*, uint32_t, uint32_t),                                                           \
      RT_P3)                                                                                       \
    X(RT_FAIL_ENUM,                                                                                \
      fort_rt_fail_enum,                                                                           \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (int64_t, const char*, const char*, uint32_t, uint32_t),                                     \
      RT_P5)                                                                                       \
    X(RT_PANIC,                                                                                    \
      fort_rt_panic,                                                                               \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (const char*, uint64_t, const char*, uint32_t, uint32_t),                                    \
      RT_P5)                                                                                       \
    X(RT_ASSERT_FAIL,                                                                              \
      fort_rt_assert_fail,                                                                         \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (const char*, const char*, uint32_t, uint32_t),                                              \
      RT_P4)                                                                                       \
    X(RT_PRINT_I64, fort_rt_print_i64, void, RT_RVOID, (int32_t, int64_t), RT_P2)                  \
    X(RT_PRINT_U64, fort_rt_print_u64, void, RT_RVOID, (int32_t, uint64_t), RT_P2)                 \
    X(RT_PRINT_F32, fort_rt_print_f32, void, RT_RVOID, (int32_t, float), RT_P2)                    \
    X(RT_PRINT_F64, fort_rt_print_f64, void, RT_RVOID, (int32_t, double), RT_P2)                   \
    X(RT_PRINT_BOOL, fort_rt_print_bool, void, RT_RVOID, (int32_t, uint8_t), RT_P2)                \
    X(RT_PRINT_CHAR, fort_rt_print_char, void, RT_RVOID, (int32_t, uint8_t), RT_P2)                \
    X(RT_PRINT_PTR, fort_rt_print_ptr, void, RT_RVOID, (int32_t, const void*), RT_P2)              \
    X(RT_PRINT_STR, fort_rt_print_str, void, RT_RVOID, (int32_t, const char*, uint64_t), RT_P3)    \
    X(RT_PRINT_ENUM,                                                                               \
      fort_rt_print_enum,                                                                          \
      void,                                                                                        \
      RT_RVOID,                                                                                    \
      (int32_t, int32_t, const struct fort_rt_enum_member*, uint64_t),                             \
      RT_P4)                                                                                       \
    X(RT_FLUSH, fort_rt_flush, void, RT_RVOID, (int32_t), RT_P1)                                   \
    X(RT_FLUSH_ALL, fort_rt_flush_all, void, RT_RVOID, (void), RT_P0)                              \
    X(RT_ARGS_INIT, fort_rt_args_init, void, RT_RVOID, (int, char**), RT_P2)                       \
    X(RT_ARGS_PTR, fort_rt_args_ptr, const struct fort_string*, RT_RVAL, (void), RT_P0)            \
    X(RT_ARGS_LEN, fort_rt_args_len, uint64_t, RT_RVAL, (void), RT_P0)                             \
    X(RT_EXIT, fort_rt_exit, void, RT_RVOID, (int32_t), RT_P1)

// The IR form of a C type of those prototypes, as the emitter would give it
// (toolchain.md 6 items 7 and 8): `int32_t` and `uint32_t` are `i32`, the
// 64-bit integers are `i64`, `uint8_t` is `i8 zeroext` and every pointer is
// `ptr`. The default is IR_NONE and never IR_PTR: C's `char` is a type of its
// own beside `signed char` and `unsigned char`, so a pointer default would
// swallow a scalar this list does not name instead of failing.
#define IR_OF(v)                                                                                   \
    _Generic((v),                                                                                  \
        void*: IR_PTR,                                                                             \
        const void*: IR_PTR,                                                                       \
        char**: IR_PTR,                                                                            \
        const char*: IR_PTR,                                                                       \
        const struct fort_string*: IR_PTR,                                                         \
        const struct fort_rt_enum_member*: IR_PTR,                                                 \
        uint8_t: IR_U8,                                                                            \
        int32_t: IR_I32,                                                                           \
        uint32_t: IR_I32,                                                                          \
        int64_t: IR_I64,                                                                           \
        uint64_t: IR_I64,                                                                          \
        float: IR_F32,                                                                             \
        double: IR_F64,                                                                            \
        default: IR_NONE)

// The result column: `void` cannot be a `_Generic` association, so a void
// result is checked as IR_VOID and the four that return a value through
// IR_OF. The function-pointer assertion below covers the result type itself.
#define RT_RVOID(t) IR_VOID
#define RT_RVAL(t) IR_OF((t)0)

// The parameter columns, as the elements after the leading IR_NONE of the
// array the check reads: one macro per arity, so the count is part of the row.
#define RT_P0(a)
#define RT_P1(a) , IR_OF((a)0)
#define RT_P2(a, b) , IR_OF((a)0), IR_OF((b)0)
#define RT_P3(a, b, c) , IR_OF((a)0), IR_OF((b)0), IR_OF((c)0)
#define RT_P4(a, b, c, d) , IR_OF((a)0), IR_OF((b)0), IR_OF((c)0), IR_OF((d)0)
#define RT_P5(a, b, c, d, e) , IR_OF((a)0), IR_OF((b)0), IR_OF((c)0), IR_OF((d)0), IR_OF((e)0)
#define RT_P6(a, b, c, d, e, f)                                                                    \
    , IR_OF((a)0), IR_OF((b)0), IR_OF((c)0), IR_OF((d)0), IR_OF((e)0), IR_OF((f)0)

// The row's C prototype is the one `runtime/fort_rt.h` declares. `_Generic`
// selects on the type of `&name`, so the association is the function pointer
// the C column spells; a header that changed makes this fail to compile. The
// arguments cannot be parenthesized: `res` and `params` spell a type, where a
// parenthesis is a different type or none at all.
// NOLINTBEGIN(bugprone-macro-parentheses) a type argument cannot be parenthesized
#define WITNESS_C_TYPE(e, name, res, rescheck, params, pmacro)                                     \
    _Static_assert(_Generic(&name, res(*) params: 1, default: 0), #name);
// NOLINTEND(bugprone-macro-parentheses)

// The forms of one row, with a leading IR_NONE so that a prototype with no
// parameter still initializes an array.
#define RT_FORMS(params, pmacro) ((const ir_form_t[]){IR_NONE pmacro params})

#define WITNESS_ROW(e, name, res, rescheck, params, pmacro)                                        \
    TEST_ASSERT_EQ_STR(row_mismatch(e,                                                             \
                                    #name,                                                         \
                                    rescheck(res),                                                 \
                                    sizeof RT_FORMS(params, pmacro) / sizeof(ir_form_t) - 1,       \
                                    RT_FORMS(params, pmacro)),                                     \
                       "");

RT_ENTRIES(WITNESS_C_TYPE)

// The first disagreement between the row `e` of RT_SIG and the C prototype
// beside it, or "": the name, the result form, the arity, and each parameter
// form. A form of IR_NONE is a disagreement of its own, since no row may hold
// one and IR_OF answers IR_NONE for a C type it does not know.
static const char* row_mismatch(
    rt_entry_t e, const char* name, ir_form_t result, uint64_t nparams, const ir_form_t* forms) {
    static char text[128];
    if (strcmp(rt_entry_name(e), name) != 0) {
        TEST_UNUSED(snprintf(text, sizeof text, "%s: the row names %s", name, rt_entry_name(e)));
        return text;
    }
    if (result == IR_NONE || rt_entry_result(e) != result) {
        TEST_UNUSED(snprintf(text,
                             sizeof text,
                             "%s: the result is %s, the C prototype %s",
                             name,
                             ir_result_text(rt_entry_result(e)),
                             ir_result_text(result)));
        return text;
    }
    if ((uint64_t)rt_entry_param_count(e) != nparams) {
        TEST_UNUSED(snprintf(text,
                             sizeof text,
                             "%s: the row takes %u parameters, the C prototype %llu",
                             name,
                             rt_entry_param_count(e),
                             (unsigned long long)nparams));
        return text;
    }
    for (uint64_t i = 0; i < nparams; i++) {
        if (forms[i + 1] != IR_NONE && rt_entry_param(e, (uint32_t)i) == forms[i + 1]) {
            continue;
        }
        TEST_UNUSED(snprintf(text,
                             sizeof text,
                             "%s: parameter %llu is %s, the C prototype %s",
                             name,
                             (unsigned long long)(i + 1),
                             ir_param_text(rt_entry_param(e, (uint32_t)i)),
                             ir_param_text(forms[i + 1])));
        return text;
    }
    return "";
}

TEST(every_row_is_the_c_prototype_of_the_runtime_header,
     {// The must-fix of T-072's review: before this, a row could name a form no
      // C prototype had and the whole gate stayed green -- `fort_rt_print_f64`
      // taking an `i64` and `fort_rt_fail_div_zero` a 64-bit line number both
      // passed every test, the second emitting a call `opt -passes=verify`
      // accepted against a declaration it disagreed with.
      RT_ENTRIES(WITNESS_ROW)})

// ---- the form of a fort type at the boundary (D9.8, D9.9) --------------------------

TEST(a_fort_type_takes_the_ir_form_of_its_c_counterpart, {
    types_begin();
    // The mapping of D9.8: `int` is `i32`, `long` and `size_t` are 64-bit,
    // and a narrow value carries an extension attribute on both sides.
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I32)), "i32");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U32)), "i32");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I64)), "i64");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U64)), "i64");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I8)), "i8 signext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U8)), "i8 zeroext");
    // Fort `char` is C's `unsigned char` at the boundary (D3.2), so it is the
    // form a `uint8_t` parameter takes.
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_CHAR)), "i8 zeroext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I16)), "i16 signext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U16)), "i16 zeroext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_F32)), "float");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_F64)), "double");
    // `bool` is `i1` as a value and `i8` in memory (D19.2), so it is a form
    // of its own and not the form of a `uint8_t`.
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_BOOL)), "i1 zeroext");
    TEST_ASSERT_EQ_STR(form_of(type_void(&types)), "void");
    types_end();
})

TEST(every_pointer_takes_the_opaque_ptr_form, {
    types_begin();
    const type_t* i32t = prim(PRIM_I32);
    TEST_ASSERT_EQ_STR(form_of(type_ptr(&types, i32t, false, false)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_ptr(&types, i32t, true, true)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false)), "ptr");
    // A function pointer is a pointer (D3.10) and an enum crosses as `i32`
    // (D9.8, D3.9).
    TEST_ASSERT_EQ_STR(form_of(type_fn(&types, i32t, &i32t, 1, false)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_enum(&types, str_from_cstr("color"), &types)), "i32");
    types_end();
})

TEST(a_type_no_extern_signature_may_use_has_no_form, {
    types_begin();
    const type_t* i32t = prim(PRIM_I32);
    // Spans, strings, structs and fixed arrays are out of extern signatures
    // (D9.8), and the checker refuses them before this is asked; a form of
    // IR_NONE never matches a row, so a type that slipped through cannot
    // silently agree with one.
    TEST_ASSERT_EQ_STR(form_of(type_span(&types, i32t, false, false)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(type_string(&types, false)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(type_array(&types, i32t, 2)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(type_struct(&types, str_from_cstr("point"), &types)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(NULL), "<none>");
    types_end();
})

TEST(the_form_of_a_type_is_the_text_the_emitter_writes_for_it, {
    types_begin();
    // One map from a fort type to an IR type, or the check of D9.8 would
    // compare a signature against a call the emitter writes differently.
    for (uint64_t i = 0; i < sizeof SCALARS / sizeof SCALARS[0]; i++) {
        const type_t* t = prim(SCALARS[i]);
        TEST_ASSERT_EQ_STR(form_of(t), emitted_param(t));
    }
    const type_t* i32t = prim(PRIM_I32);
    TEST_ASSERT_EQ_STR(form_of(type_ptr(&types, i32t, false, false)),
                       emitted_param(type_ptr(&types, i32t, false, false)));
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false)),
                       emitted_param(type_voidptr(&types, false)));
    TEST_ASSERT_EQ_STR(form_of(type_enum(&types, str_from_cstr("color"), &types)),
                       emitted_param(type_enum(&types, str_from_cstr("color"), &types)));
    types_end();
})

int main(int argc, char** argv) {
    TEST_INIT("runtime_sig", argc, argv);
    TEST_RUN(a_c_name_finds_its_entry_point_and_nothing_else_does);
    TEST_RUN(every_row_names_the_entry_point_its_enumerator_does);
    TEST_RUN(the_four_entry_points_that_return_a_value_are_the_only_ones);
    TEST_RUN(the_noreturn_entry_points_are_the_ones_section_5_1_names);
    TEST_RUN(the_parameter_list_of_a_row_is_the_c_prototype_of_section_5_1);
    TEST_RUN(every_entry_point_renders_the_declaration_of_item_8);
    TEST_RUN(every_row_is_the_c_prototype_of_the_runtime_header);
    TEST_RUN(a_fort_type_takes_the_ir_form_of_its_c_counterpart);
    TEST_RUN(every_pointer_takes_the_opaque_ptr_form);
    TEST_RUN(a_type_no_extern_signature_may_use_has_no_form);
    TEST_RUN(the_form_of_a_type_is_the_text_the_emitter_writes_for_it);
    types_end();
    TEST_EXIT();
}
