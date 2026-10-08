// Tests the suffix limit of D2.11 in the checker: a type takes 256 suffixes, and
// the parser refuses the 257th before the checker sees the type. Tests the size
// ceiling of D3.4 behind a reference too.
#include <stdbool.h>
#include <stdint.h>

#include "check.h"
#include "common/check_helpers.h"
#include "str.h"
#include "sym.h"
#include "types.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the suffix counts and the source positions below are
// the test data.

static sb_t deep_src;

// The module `<base> <suffix * inner> ... deep = null;` and a `main`. A
// nonzero `inner` puts `base` and the first `inner` suffixes into a group, and
// `outer` more suffixes follow it. D2.11 counts the suffixes of the group and
// its suffixes together.
static const char* deep_decl(const char* base, const char* suffix, uint32_t inner, uint32_t outer) {
    sb_clear(&deep_src);
    if (inner > 0) {
        sb_push(&deep_src, '(');
    }
    sb_append(&deep_src, base);
    for (uint32_t i = 0; i < inner; i++) {
        sb_append(&deep_src, suffix);
    }
    if (inner > 0) {
        sb_push(&deep_src, ')');
    }
    for (uint32_t i = 0; i < outer; i++) {
        sb_append(&deep_src, suffix);
    }
    sb_append(&deep_src, " deep = null;\nfn main() i32 {\n    return 0;\n}\n");
    return sb_cstr(&deep_src);
}

// The number of pointer levels above the type spelled `base` in the type of
// `deep`.
static uint64_t pointers_of_deep(const char* base) {
    const type_t* t = sym_main("deep")->type;
    uint64_t n = 0;
    while (t->kind == TYPE_PTR) {
        n++;
        t = t->elem;
    }
    TEST_ASSERT_EQ_STR(type_text(t), base);
    return n;
}

// The parser refuses the 257th suffix before the checker sees the type, so
// the checker's own refusal never shows.
// It answers false when a frame of the suffix stack stays open.
static bool refused_at_the_nesting_limit(const char* src) {
    return !check_src(src) && said("nesting deeper than 256") && !said("too many type suffixes") &&
           diag_lines() == 1 && checker.suffix_top == 0;
}

// Checks `src` and answers whether the checker took it and closed each frame of
// its suffix stack.
static bool taken(const char* src) {
    return check_src(src) && checker.suffix_top == 0;
}

TEST(a_type_takes_256_suffixes_and_not_257, {
    sb_init(&deep_src);
    TEST_ASSERT_TRUE(taken(deep_decl("i32", "*", 0, 256)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32"), (uint64_t)256);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 0, 257)));
    // The array suffix counts as one of the 256.
    TEST_ASSERT_TRUE(taken(deep_decl("i32[1]", "*", 0, 255)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32[1]"), (uint64_t)255);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32[1]", "*", 0, 256)));
    sb_free(&deep_src);
})

// The array length is the size of a type of 200 suffixes. The frame of that type
// grows the suffix stack while the frame of `deep` is open, so the stack moves.
TEST(a_type_in_an_array_length_grows_the_suffix_stack, {
    sb_t base;
    sb_init(&base);
    sb_append(&base, "i32[sizeof(i32");
    for (uint32_t i = 0; i < 200; i++) {
        sb_push(&base, '*');
    }
    sb_append(&base, ")]");
    sb_init(&deep_src);
    TEST_ASSERT_TRUE(taken(deep_decl(sb_cstr(&base), "*", 0, 50)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32[8]"), (uint64_t)50);
    TEST_ASSERT_TRUE(checker.suffix_cap >= (uint64_t)251);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl(sb_cstr(&base), "*", 0, 256)));
    sb_free(&deep_src);
    sb_free(&base);
})

// A frame that fails closes too: an unknown base stops the frame before its
// loop, and a bad array length stops it in the loop.
TEST(a_failed_type_closes_its_suffix_frame, {
    sb_init(&deep_src);
    TEST_ASSERT_FALSE(check_src(deep_decl("nothing", "*", 0, 256)));
    TEST_ASSERT_TRUE(said("unknown type 'nothing'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(checker.suffix_top, (uint64_t)0);
    TEST_ASSERT_FALSE(check_src(deep_decl("i32[-1]", "*", 0, 255)));
    TEST_ASSERT_TRUE(said("an array length must be greater than 0"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(checker.suffix_top, (uint64_t)0);
    sb_free(&deep_src);
})

TEST(a_group_does_not_reset_the_suffix_count, {
    sb_init(&deep_src);
    TEST_ASSERT_TRUE(taken(deep_decl("i32", "*", 128, 128)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32"), (uint64_t)256);
    TEST_ASSERT_TRUE(taken(deep_decl("i32", "*", 255, 1)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32"), (uint64_t)256);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 128, 129)));
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 256, 1)));
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 1, 256)));
    sb_free(&deep_src);
})

// ---- sizes behind a reference (D3.4) ------------------------------------------------

// u64[2^62] needs 2^65 bytes, so it is too large behind any reference too. Each
// test counts the lines, because the corpus cannot see a second report.
TEST(an_array_behind_a_pointer_that_is_too_large_is_refused, {
    TEST_ASSERT_FALSE(check_src("u64[4611686018427387904]* BIG = null;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:1:1: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(an_array_behind_a_pointer_that_fits_is_accepted, {
    // 2^60 - 1 values of 8 bytes need 2^63 - 8 bytes; 2^63 - 1 bytes fit exactly
    TEST_ASSERT_TRUE(check_src("u64[1152921504606846975]* WORDS = null;\n"
                               "u8[9223372036854775807]* BYTES = null;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("u16[4611686018427387904]* HALVES = null;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:1:1: error: type is too large: u16[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_parameter_and_a_result_behind_a_pointer_are_refused, {
    TEST_ASSERT_FALSE(check_src("fn f(u64[4611686018427387904]* p) void {\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:1:6: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("fn f() u64[4611686018427387904]* {\n    return null;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:1:8: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_function_type_that_names_a_type_too_large_is_refused, {
    // no value stores a parameter of a function type, but D3.4 refuses its type
    TEST_ASSERT_FALSE(check_src("fn(u64[4611686018427387904]) void OP = null;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:1:1: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_field_behind_a_pointer_that_is_too_large_is_refused, {
    TEST_ASSERT_FALSE(check_src("struct holder {\n    u64[4611686018427387904]* p;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:2:5: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_struct_behind_a_pointer_is_sized_in_either_declaration_order, {
    // The struct `late` is laid out after `early` resolves, so both orders are
    // one program (D7.10) and each reports once.
    TEST_ASSERT_FALSE(check_src("struct early {\n    late[4611686018427387904]* p;\n}\n"
                                "struct late {\n    u64 a;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:2:5: error: type is too large: late[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("struct late {\n    u64 a;\n}\n"
                                "struct early {\n    late[4611686018427387904]* p;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:5:5: error: type is too large: late[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_struct_behind_its_own_pointer_is_sized_after_its_layout, {
    TEST_ASSERT_FALSE(check_src("struct self {\n    self[4611686018427387904]* next;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:2:5: error: type is too large: self[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(check_src("struct self {\n    self[2]* next;\n}\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
})

TEST(a_value_cycle_behind_a_pointer_is_not_an_infinite_size, {
    // `inner` contains `outer` by value. A forced layout of `inner` from the field
    // of `outer` would read `outer` as resolving and report an infinite size (D3.8).
    TEST_ASSERT_TRUE(check_src("struct outer {\n    inner[2]* p;\n}\n"
                               "struct inner {\n    outer o;\n}\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("struct outer {\n    inner[4611686018427387904]* p;\n}\n"
                                "struct inner {\n    outer o;\n    u64 x;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:2:5: error: type is too large: inner[4611686018427387904]"));
    TEST_ASSERT_FALSE(said("infinite size"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

// Two structs each reach the other through a large array. The field of the second
// struct reports and fails its struct, and the failed struct silences the waiting
// entry of the first, so one error stays one error in both orders.
TEST(two_structs_that_reach_each_other_report_once_in_either_order, {
    TEST_ASSERT_FALSE(check_src("struct a {\n    b[4611686018427387904]* p;\n}\n"
                                "struct b {\n    a[4611686018427387904]* q;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:5:5: error: type is too large: a[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("struct b {\n    a[4611686018427387904]* q;\n}\n"
                                "struct a {\n    b[4611686018427387904]* p;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:5:5: error: type is too large: b[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_local_a_cast_and_sizeof_behind_a_pointer_are_refused, {
    TEST_ASSERT_FALSE(check_body("    u64[4611686018427387904]* p = null;"));
    TEST_ASSERT_TRUE(said("main.ft:2:5: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("    u64[2] w = {};\n"
                                 "    u64 c = (*cast(&w, u64[4611686018427387904]*))[0];\n"
                                 "    println(c);"));
    TEST_ASSERT_TRUE(said("main.ft:3:15: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(
        check_body("    u64 z = sizeof(u64[4611686018427387904]*);\n    println(z);"));
    TEST_ASSERT_TRUE(said("main.ft:2:13: error: type is too large: u64[4611686018427387904]"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_limits", argc, argv);
    TEST_RUN(a_type_takes_256_suffixes_and_not_257);
    TEST_RUN(a_type_in_an_array_length_grows_the_suffix_stack);
    TEST_RUN(a_failed_type_closes_its_suffix_frame);
    TEST_RUN(a_group_does_not_reset_the_suffix_count);
    TEST_RUN(an_array_behind_a_pointer_that_is_too_large_is_refused);
    TEST_RUN(an_array_behind_a_pointer_that_fits_is_accepted);
    TEST_RUN(a_parameter_and_a_result_behind_a_pointer_are_refused);
    TEST_RUN(a_function_type_that_names_a_type_too_large_is_refused);
    TEST_RUN(a_field_behind_a_pointer_that_is_too_large_is_refused);
    TEST_RUN(a_struct_behind_a_pointer_is_sized_in_either_declaration_order);
    TEST_RUN(a_struct_behind_its_own_pointer_is_sized_after_its_layout);
    TEST_RUN(a_value_cycle_behind_a_pointer_is_not_an_infinite_size);
    TEST_RUN(two_structs_that_reach_each_other_report_once_in_either_order);
    TEST_RUN(a_local_a_cast_and_sizeof_behind_a_pointer_are_refused);
    check_reset();
    done();
    TEST_EXIT();
}
