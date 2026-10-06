// Tests the checker's indexing and span expressions: the element type and the lvalue an
// index yields, the constant bounds, and what a span may view. The other postfix forms
// are in check_expr_test.c.
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "common/check_helpers.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- indexing and span expressions --------------------------------------------------

TEST(an_element_of_a_string_is_a_char, {
    TEST_ASSERT_TRUE(check_body("    string s = \"ab\";\n    char c = s[0];\n    println(c);"));
    TEST_ASSERT_EQ_STR(init_type("c"), "char");
    // A string holds `char` and not `u8`, and a `char` does not convert to `u8`.
    TEST_ASSERT_FALSE(check_body("    string s = \"ab\";\n    u8 b = s[0];\n    println(b);"));
    TEST_ASSERT_TRUE(said("the initializer expects u8, not char"));
})

TEST(a_constant_index_out_of_range_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32 x = a[4];\n    println(x);"));
    TEST_ASSERT_TRUE(said("index 4 out of range for i32[4]"));
})

TEST(an_index_of_an_rvalue_array_is_not_an_lvalue, {
    // `e[i]` is an lvalue where `e` is an lvalue fixed array: the elements of
    // a returned array live in a temporary.
    TEST_ASSERT_FALSE(check_src("fn make() i32[3] {\n    return i32[3]{1, 2, 3};\n}\n"
                                "fn main() i32 {\n    i32* p = &make()[0];\n"
                                "    return *p;\n}\n"));
    TEST_ASSERT_TRUE(said("'&' requires an lvalue"));
    // Reading one is fine, and a span or string expression is an lvalue
    // whatever its operand.
    TEST_ASSERT_TRUE(check_src("fn make() i32[3] {\n    return i32[3]{1, 2, 3};\n}\n"
                               "fn main() i32 {\n    return make()[0];\n}\n"));
    TEST_ASSERT_TRUE(check_body("    string s = \"ab\";\n    println(&s[0]);"));
})

TEST(a_span_of_an_rvalue_array_is_refused, {
    // A span views storage, and the elements of a returned array live in a temporary.
    TEST_ASSERT_FALSE(check_src("fn make() i32[3] {\n    return i32[3]{1, 2, 3};\n}\n"
                                "fn main() i32 {\n    i32@ s = make()[0..2];\n"
                                "    return s[0];\n}\n"));
    TEST_ASSERT_TRUE(said("a span of a fixed array needs an lvalue"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    // The rule is about a fixed array only. A returned span is an rvalue, and a span of it is
    // fine, because it views the storage the returned span views.
    TEST_ASSERT_TRUE(check_src("fn view(i32@ s) i32@ {\n    return s;\n}\n"
                               "fn main() i32 {\n    i32[3] a = {1, 2, 3};\n"
                               "    i32@ t = view(a[0..3])[0..2];\n    return t[0];\n}\n"));
    // A span of an lvalue array is fine.
    TEST_ASSERT_TRUE(
        check_body("    i32[3] a = {1, 2, 3};\n    i32@ s = a[0..2];\n    println(s[0]);"));
})

TEST(a_negative_constant_index_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32 x = a[-1];\n    println(x);"));
    TEST_ASSERT_TRUE(said("a negative index: -1"));
})

TEST(a_negative_span_bound_or_count_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32@ s = a[-1..];\n    println(s.len);"));
    TEST_ASSERT_TRUE(said("a negative span bound: -1"));
    TEST_ASSERT_FALSE(check_body("    del(new(i32, -1));"));
    TEST_ASSERT_TRUE(said("a negative count: -1"));
})

TEST(a_pointer_cannot_be_indexed, {
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    i32 v = p[0];\n"
                                 "    println(v);"));
    // Pointers cannot be indexed, not even pointers to arrays.
    TEST_ASSERT_TRUE(said("i32* cannot be indexed: write '(*p)[i]'"));
})

TEST(a_span_of_a_string_is_a_string, {
    TEST_ASSERT_TRUE(check_body("    string s = \"hello\";\n    string t = s[1..3];\n"
                                "    println(t);"));
    TEST_ASSERT_EQ_STR(init_type("t"), "string");
})

TEST(a_span_of_an_array_takes_its_element_mutability, {
    TEST_ASSERT_TRUE(check_body("    i32[4] mut a = {};\n    i32 mut@ s = a[1..3];\n"
                                "    i32@ t = a[..];\n    println(s.len, t.len);"));
    TEST_ASSERT_EQ_STR(init_type("s"), "i32 mut@");
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32 mut@ s = a[1..3];\n"
                                 "    println(s.len);"));
    // A span of an immutable array cannot add mutability.
    TEST_ASSERT_TRUE(said("expects i32 mut@, not i32@"));
})

TEST(a_span_of_a_pointer_needs_both_bounds, {
    TEST_ASSERT_TRUE(
        check_body("    i32 mut x = 1;\n    i32 mut* p = &x;\n    i32 mut@ s = p[0..1];\n"
                   "    println(s.len);"));
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    i32@ s = p[0..];\n"
                                 "    println(s.len);"));
    // A pointer has no length, so only the two-bound form exists.
    TEST_ASSERT_TRUE(said("a pointer has no length"));
})

TEST(a_span_of_a_void_pointer_is_refused, {
    TEST_ASSERT_FALSE(check_body("    void* v = null;\n    i32 n = 2;\n    i32@ s = v[0..n];\n"
                                 "    println(s.len);"));
    TEST_ASSERT_TRUE(said("cannot take a span of void*"));
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_index", argc, argv);
    TEST_RUN(an_element_of_a_string_is_a_char);
    TEST_RUN(a_constant_index_out_of_range_is_refused);
    TEST_RUN(an_index_of_an_rvalue_array_is_not_an_lvalue);
    TEST_RUN(a_span_of_an_rvalue_array_is_refused);
    TEST_RUN(a_negative_constant_index_is_refused);
    TEST_RUN(a_negative_span_bound_or_count_is_refused);
    TEST_RUN(a_pointer_cannot_be_indexed);
    TEST_RUN(a_span_of_a_string_is_a_string);
    TEST_RUN(a_span_of_an_array_takes_its_element_mutability);
    TEST_RUN(a_span_of_a_pointer_needs_both_bounds);
    TEST_RUN(a_span_of_a_void_pointer_is_refused);
    check_reset();
    done();
    TEST_EXIT();
}
