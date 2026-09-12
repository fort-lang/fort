// Unit tests of spans (toolchain.md 6 items 3, 16 and 17; D3.5, D6.8, D6.9,
// D19.2): the `%fort.span` header and its two pseudo-fields, the index check,
// the four span expressions with their two-bound check, the unchecked escape
// a raw pointer gives, and what `--no-bounds-check` removes.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers)
// The literals below are the test data: the lengths, columns and temporary
// numbers of the module each program emits.

// A span of a fixed array, whose length is a literal, and a span of a span,
// whose length is a header field: the two operand classes of item 16.
static const char ARRAY_SPAN[] = "fn i64 lo() {\n    return 1;\n}\n"
                                 "fn i32 main() {\n    i32[5] a = {};\n    i64 i = lo();\n"
                                 "    i32@ s = a[i..4];\n    println(s.len);\n    return 0;\n}\n";

// ---- the header (item 17, D3.5, D19.2) ---------------------------------------------

TEST(the_span_type_is_a_pointer_and_a_length, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32@ s = {};\n"
                          "    println(s.len);\n    return 0;\n}\n"));
    // One `%fort.span` serves every span and `string`, with the pointer at 0
    // and the length at 8 (D3.5, D3.7, D19.2).
    TEST_ASSERT_EQ_STR(found("%fort.span = type { ptr, i64 }"), "%fort.span = type { ptr, i64 }");
    TEST_ASSERT_EQ_STR(found("%s.0 = alloca %fort.span, align 8"),
                       "%s.0 = alloca %fort.span, align 8");
    // The zero value of a span is `{null, 0}` (D3.5).
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)"),
        "call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_span_is_passed_by_hidden_pointer_and_never_split, {
    TEST_ASSERT_TRUE(emit("fn u64 size(i32@ s) {\n    return s.len;\n}\n"
                          "fn i32 main() {\n    i32@ s = {};\n"
                          "    println(size(s));\n    return 0;\n}\n"));
    // A span stays one hidden pointer and is never split into two scalars
    // (D9.9, item 7).
    TEST_ASSERT_EQ_STR(found("define dso_local i64 @\"main.size\"(ptr %s.in) #0"),
                       "define dso_local i64 @\"main.size\"(ptr %s.in) #0");
    // The caller copies the header into a place of its own and passes that
    // (item 7).
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp0, ptr align 8 %s.0, i64 16, "
              "i1 false)"),
        "call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp0, ptr align 8 %s.0, i64 16, i1 false)");
    TEST_ASSERT_EQ_STR(found("call i64 @\"main.size\"(ptr %tmp0)"),
                       "call i64 @\"main.size\"(ptr %tmp0)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_pseudo_fields_are_read_through_a_pointer_to_a_span, {
    TEST_ASSERT_TRUE(emit("fn u64 size(i32@* p) {\n    return p->len;\n}\n"
                          "fn i32 main() {\n    i32@ s = {};\n"
                          "    println(size(&s));\n    return 0;\n}\n"));
    // Through a pointer to a span, `->` reaches `.len` and `.ptr` too
    // (D6.10).
    TEST_ASSERT_EQ_STR(found("  %t0 = load ptr, ptr %p.0, align 8\n"
                             "  %t1 = getelementptr inbounds %fort.span, ptr %t0, i32 0, i32 1\n"
                             "  %t2 = load i64, ptr %t1, align 8\n"),
                       "  %t0 = load ptr, ptr %p.0, align 8\n"
                       "  %t1 = getelementptr inbounds %fort.span, ptr %t0, i32 0, i32 1\n"
                       "  %t2 = load i64, ptr %t1, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- indexing (item 16, D6.8) ------------------------------------------------------

TEST(indexing_a_span_checks_against_its_header_length, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    u64 n = 3;\n"
                          "    i32 mut@ own s = new(i32, n);\n    u64 i = 1;\n"
                          "    println(s[i]);\n    del(s);\n    return 0;\n}\n"));
    // One `icmp uge i64 %idx, %len` branches to std.rt.fail_bounds, and the
    // element is reached through the header's pointer (item 16, item 3).
    TEST_ASSERT_EQ_STR(found("  %t8 = getelementptr inbounds %fort.span, ptr %s.1, i32 0, i32 1\n"
                             "  %t9 = load i64, ptr %t8, align 8\n"
                             "  %t10 = icmp uge i64 %t7, %t9\n"
                             "  br i1 %t10, label %L3, label %L2\n"),
                       "  %t8 = getelementptr inbounds %fort.span, ptr %s.1, i32 0, i32 1\n"
                       "  %t9 = load i64, ptr %t8, align 8\n"
                       "  %t10 = icmp uge i64 %t7, %t9\n"
                       "  br i1 %t10, label %L3, label %L2\n");
    TEST_ASSERT_EQ_STR(
        found("@\"std.rt.fail_bounds\"(i64 %t7, i64 %t9, ptr @.file.0, i32 5, i32 14)"),
        "@\"std.rt.fail_bounds\"(i64 %t7, i64 %t9, ptr @.file.0, i32 5, i32 14)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_element_of_a_span_of_pointers_is_a_pointer_slot, {
    TEST_ASSERT_TRUE(emit("struct node {\n    i32 value;\n}\n"
                          "fn i32 main() {\n    u64 n = 2;\n"
                          "    node mut* mut@ own s = new(node*, n);\n    u64 i = 0;\n"
                          "    println(s[i] == null);\n    del(s);\n    return 0;\n}\n"));
    // `new(node* own, n)`-shaped spans hold pointer slots, so the element
    // stride is a pointer (D17.3).
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\"(i64 8, i64 %t0, "),
                       "call ptr @\"std.rt.alloc\"(i64 8, i64 %t0, ");
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds ptr, ptr %t12, i64 %t7"),
                       "getelementptr inbounds ptr, ptr %t12, i64 %t7");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- span expressions (item 16, D6.9) ----------------------------------------------

TEST(a_two_bound_span_checks_both_bounds_in_one_branch, {
    TEST_ASSERT_TRUE(emit(ARRAY_SPAN));
    // `icmp ugt %hi, %len`, `icmp ugt %lo, %hi` and one `or` (item 16); a
    // fixed array's length is an `i64` literal.
    TEST_ASSERT_EQ_STR(found("  %t2 = sext i32 4 to i64\n"
                             "  %t3 = icmp ugt i64 %t2, 5\n"
                             "  %t4 = icmp ugt i64 %t1, %t2\n"
                             "  %t5 = or i1 %t3, %t4\n"
                             "  br i1 %t5, label %L1, label %L0\n"),
                       "  %t2 = sext i32 4 to i64\n"
                       "  %t3 = icmp ugt i64 %t2, 5\n"
                       "  %t4 = icmp ugt i64 %t1, %t2\n"
                       "  %t5 = or i1 %t3, %t4\n"
                       "  br i1 %t5, label %L1, label %L0\n");
    // The failure takes the two bounds and the length, and is reported at the
    // `[` of the span expression (D11.4, toolchain.md 4).
    TEST_ASSERT_EQ_STR(
        found("\nL1:\n  call void @\"std.rt.fail_span\"(i64 %t1, i64 %t2, i64 5, ptr @.file.0, "
              "i32 7, i32 15)\n  unreachable\n"),
        "\nL1:\n  call void @\"std.rt.fail_span\"(i64 %t1, i64 %t2, i64 5, ptr @.file.0, "
        "i32 7, i32 15)\n  unreachable\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_result_is_the_element_at_lo_and_the_difference_of_the_bounds, {
    TEST_ASSERT_TRUE(emit(ARRAY_SPAN));
    // The view's pointer is the address of element `lo` by the array shape of
    // item 3, and its length is `hi - lo` (D6.9).
    TEST_ASSERT_EQ_STR(found("  %t6 = getelementptr inbounds [5 x i32], ptr %a.0, i64 0, i64 %t1\n"
                             "  %t7 = sub i64 %t2, %t1\n"
                             "  %t8 = getelementptr inbounds %fort.span, ptr %s.2, i32 0, i32 0\n"
                             "  store ptr %t6, ptr %t8, align 8\n"
                             "  %t9 = getelementptr inbounds %fort.span, ptr %s.2, i32 0, i32 1\n"
                             "  store i64 %t7, ptr %t9, align 8\n"),
                       "  %t6 = getelementptr inbounds [5 x i32], ptr %a.0, i64 0, i64 %t1\n"
                       "  %t7 = sub i64 %t2, %t1\n"
                       "  %t8 = getelementptr inbounds %fort.span, ptr %s.2, i32 0, i32 0\n"
                       "  store ptr %t6, ptr %t8, align 8\n"
                       "  %t9 = getelementptr inbounds %fort.span, ptr %s.2, i32 0, i32 1\n"
                       "  store i64 %t7, ptr %t9, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_absent_low_bound_is_zero, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[5] a = {};\n"
                          "    i32@ s = a[..2];\n    println(s.len);\n    return 0;\n}\n"));
    // `e[..hi]` is `e[0..hi]` (D6.9).
    TEST_ASSERT_EQ_STR(found("  %t1 = icmp ugt i64 %t0, 5\n  %t2 = icmp ugt i64 0, %t0\n"),
                       "  %t1 = icmp ugt i64 %t0, 5\n  %t2 = icmp ugt i64 0, %t0\n");
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds [5 x i32], ptr %a.0, i64 0, i64 0"),
                       "getelementptr inbounds [5 x i32], ptr %a.0, i64 0, i64 0");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_span\"(i64 0, i64 %t0, i64 5, "),
                       "@\"std.rt.fail_span\"(i64 0, i64 %t0, i64 5, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_absent_high_bound_is_the_operands_length, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    u64 n = 4;\n"
                          "    i32 mut@ own s = new(i32, n);\n"
                          "    i32@ t = s[2..];\n    println(t.len);\n    del(s);\n"
                          "    return 0;\n}\n"));
    // `e[lo..]` is `e[lo..len]`, and the length is read once and used by both
    // the check and the result (D6.9).
    TEST_ASSERT_EQ_STR(found("  %t8 = getelementptr inbounds %fort.span, ptr %s.1, i32 0, i32 1\n"
                             "  %t9 = load i64, ptr %t8, align 8\n"
                             "  %t10 = icmp ugt i64 %t9, %t9\n"
                             "  %t11 = icmp ugt i64 %t7, %t9\n"),
                       "  %t8 = getelementptr inbounds %fort.span, ptr %s.1, i32 0, i32 1\n"
                       "  %t9 = load i64, ptr %t8, align 8\n"
                       "  %t10 = icmp ugt i64 %t9, %t9\n"
                       "  %t11 = icmp ugt i64 %t7, %t9\n");
    TEST_ASSERT_EQ_STR(found("sub i64 %t9, %t7"), "sub i64 %t9, %t7");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_whole_span_form_still_branches, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[3] a = {};\n"
                          "    i32@ s = a[..];\n    println(s.len);\n    return 0;\n}\n"));
    // Bounds checks are performed on every span operation, in every build
    // mode (D10.6); the optimizer folds the one `e[..]` cannot fail.
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_span\"(i64 0, i64 3, i64 3, "),
                       "@\"std.rt.fail_span\"(i64 0, i64 3, i64 3, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_negative_bound_fails_the_same_unsigned_compare, {
    TEST_ASSERT_TRUE(emit("fn i32 lo() {\n    return -1;\n}\n"
                          "fn i32 main() {\n    i32[3] a = {};\n    i32 i = lo();\n"
                          "    i32@ s = a[i..2];\n    println(s.len);\n    return 0;\n}\n"));
    // A signed bound is sign-extended, so a negative one is a huge unsigned
    // value and fails `icmp ugt` (item 16).
    TEST_ASSERT_EQ_STR(found("  %t2 = sext i32 %t1 to i64\n"), "  %t2 = sext i32 %t1 to i64\n");
    TEST_ASSERT_EQ_STR(found("  %t5 = icmp ugt i64 %t2, %t3\n"), "  %t5 = icmp ugt i64 %t2, %t3\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_span_of_a_string_walks_its_bytes, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    string s = \"hello\";\n"
                          "    string t = s[1..4];\n    println(t);\n    return 0;\n}\n"));
    // A span of a `string` is a `string` whose elements are `char`, which is
    // `i8` (D3.7, D19.2).
    TEST_ASSERT_EQ_STR(found("  %t9 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  %t10 = load ptr, ptr %t9, align 8\n"
                             "  %t11 = getelementptr inbounds i8, ptr %t10, i64 %t2\n"),
                       "  %t9 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  %t10 = load ptr, ptr %t9, align 8\n"
                       "  %t11 = getelementptr inbounds i8, ptr %t10, i64 %t2\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_span_of_a_pointer_is_unchecked, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[4] mut a = {};\n    i32 mut* p = &a[0];\n"
                          "    i32 mut@ s = p[1..3];\n    println(s.len);\n    return 0;\n}\n"));
    // `p[lo..hi]` on a raw pointer is the explicit unsafe escape for foreign
    // memory and checks nothing (D6.9, D10.4).
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_span"), "absent");
    TEST_ASSERT_EQ_STR(found("  %t3 = load ptr, ptr %p.1, align 8\n"
                             "  %t4 = sext i32 1 to i64\n"
                             "  %t5 = sext i32 3 to i64\n"
                             "  %t6 = getelementptr inbounds i32, ptr %t3, i64 %t4\n"
                             "  %t7 = sub i64 %t5, %t4\n"),
                       "  %t3 = load ptr, ptr %p.1, align 8\n"
                       "  %t4 = sext i32 1 to i64\n"
                       "  %t5 = sext i32 3 to i64\n"
                       "  %t6 = getelementptr inbounds i32, ptr %t3, i64 %t4\n"
                       "  %t7 = sub i64 %t5, %t4\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_span_of_a_span_expression_lands_in_a_temporary, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[5] a = {};\n"
                          "    println(a[1..4][1..2].len);\n    return 0;\n}\n"));
    // A span expression yields a view, which is a value, so the operand of
    // the second one is a temporary of its own (D6.7, D6.9, D19.3).
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %fort.span, align 8"),
                       "%tmp0 = alloca %fort.span, align 8");
    TEST_ASSERT_EQ_STR(found("%tmp1 = alloca %fort.span, align 8"),
                       "%tmp1 = alloca %fort.span, align 8");
    TEST_ASSERT_EQ_SIZE(occurrences("call void @\"std.rt.fail_span\"("), (size_t)2);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_operand_is_evaluated_before_the_bounds, {
    TEST_ASSERT_TRUE(emit("fn i32 lo() {\n    println(\"lo\");\n    return 1;\n}\n"
                          "fn i32 hi() {\n    println(\"hi\");\n    return 2;\n}\n"
                          "fn i32 main() {\n    i32[5] a = {};\n"
                          "    println(a[lo()..hi()].len);\n    return 0;\n}\n"));
    // Left to right (D6.3), and the operand's length is read after both
    // bounds have run.
    TEST_ASSERT_TRUE(before("call i32 @\"main.lo\"", "call i32 @\"main.hi\""));
    TEST_ASSERT_TRUE(before("call i32 @\"main.hi\"", "call void @\"std.rt.fail_span\""));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the two switches (D10.6, D11.1) -----------------------------------------------

TEST(no_bounds_check_removes_the_span_branch_and_keeps_the_inbounds, {
    TEST_ASSERT_TRUE(emit_unchecked(ARRAY_SPAN));
    // `--no-bounds-check` removes exactly the index and span branches (D10.6).
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_span"), "absent");
    TEST_ASSERT_EQ_STR(absent("icmp ugt"), "absent");
    TEST_ASSERT_EQ_STR(found("  %t3 = getelementptr inbounds [5 x i32], ptr %a.0, i64 0, i64 %t1\n"
                             "  %t4 = sub i64 %t2, %t1\n"),
                       "  %t3 = getelementptr inbounds [5 x i32], ptr %a.0, i64 0, i64 %t1\n"
                       "  %t4 = sub i64 %t2, %t1\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(no_bounds_check_reads_no_length_when_both_bounds_are_written, {
    static const char PROGRAM[] = "fn i32 main() {\n    string s = \"hello\";\n"
                                  "    string t = s[1..4];\n    println(t);\n"
                                  "    return 0;\n}\n";
    TEST_ASSERT_TRUE(emit_unchecked(PROGRAM));
    // The branch was the only reader of the operand's length, so nothing
    // loads it: the bounds are followed straight by the element shape
    // (D10.6). A fixed array cannot show this, its length being a literal.
    TEST_ASSERT_EQ_STR(found("  %t2 = sext i32 1 to i64\n"
                             "  %t3 = sext i32 4 to i64\n"
                             "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  %t5 = load ptr, ptr %t4, align 8\n"
                             "  %t6 = getelementptr inbounds i8, ptr %t5, i64 %t2\n"
                             "  %t7 = sub i64 %t3, %t2\n"),
                       "  %t2 = sext i32 1 to i64\n"
                       "  %t3 = sext i32 4 to i64\n"
                       "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  %t5 = load ptr, ptr %t4, align 8\n"
                       "  %t6 = getelementptr inbounds i8, ptr %t5, i64 %t2\n"
                       "  %t7 = sub i64 %t3, %t2\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
    // Its twin keeps the branch, and with it the one load of the length.
    TEST_ASSERT_TRUE(emit(PROGRAM));
    TEST_ASSERT_EQ_STR(found("  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                             "  %t5 = load i64, ptr %t4, align 8\n"
                             "  %t6 = icmp ugt i64 %t3, %t5\n"),
                       "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                       "  %t5 = load i64, ptr %t4, align 8\n"
                       "  %t6 = icmp ugt i64 %t3, %t5\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(no_bounds_check_still_reads_the_length_an_absent_bound_needs, {
    TEST_ASSERT_TRUE(emit_unchecked("fn i32 main() {\n    string s = \"hello\";\n"
                                    "    string t = s[2..];\n    println(t);\n"
                                    "    return 0;\n}\n"));
    // `e[lo..]` is `e[lo..len]`, so the length is the bound itself and is
    // read whether or not the branch is emitted (D6.9, D10.6).
    TEST_ASSERT_EQ_STR(found("  %t3 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                             "  %t4 = load i64, ptr %t3, align 8\n"),
                       "  %t3 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                       "  %t4 = load i64, ptr %t3, align 8\n");
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_span"), "absent");
    TEST_ASSERT_EQ_STR(found("sub i64 %t4, %t2"), "sub i64 %t4, %t2");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(release_mode_keeps_the_span_branch, {
    TEST_ASSERT_TRUE(emit_release(ARRAY_SPAN));
    // Bounds checks are performed in every build mode; only
    // `--no-bounds-check` removes them (D10.6).
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_span\"(i64 %t1, i64 %t2, i64 5, "),
                       "@\"std.rt.fail_span\"(i64 %t1, i64 %t2, i64 5, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(no_bounds_check_leaves_the_allocation_and_enum_checks_alone, {
    TEST_ASSERT_TRUE(emit_unchecked("enum level {\n    low = 1,\n}\n"
                                    "fn level pick() {\n    return level.low;\n}\n"
                                    "fn i32 main() {\n    i32 n = 2;\n"
                                    "    i32 mut@ own s = new(i32, n);\n"
                                    "    switch (pick()) {\n    case level.low:\n"
                                    "        println(s.len);\n    }\n"
                                    "    del(s);\n    return 0;\n}\n"));
    // Neither the negative-count check of D10.2 nor the enum default of D7.7
    // is a bounds check (D10.6).
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_alloc_count\""), "@\"std.rt.fail_alloc_count\"");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_enum\""), "@\"std.rt.fail_enum\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- casts and lending (D3.14, D17.4) ----------------------------------------------

TEST(a_cast_between_span_shapes_emits_nothing_of_its_own, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    string s = \"hi\";\n"
                          "    u8@ b = cast(s, u8@);\n"
                          "    println(b.len, b[0]);\n    return 0;\n}\n"));
    // A cast between aggregates only drops marks, so the value is the copy of
    // the source's storage and nothing else (item 12, D3.14, D19.3).
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memcpy.p0.p0.i64(ptr align 8 %b.1, ptr align 8 %s.0, i64 16, "
              "i1 false)"),
        "call void @llvm.memcpy.p0.p0.i64(ptr align 8 %b.1, ptr align 8 %s.0, i64 16, i1 false)");
    TEST_ASSERT_EQ_STR(absent("ptrtoint"), "absent");
    TEST_ASSERT_EQ_STR(absent("inttoptr"), "absent");
    TEST_ASSERT_EQ_STR(absent("bitcast"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(lending_an_owned_span_is_a_copy_of_its_header, {
    TEST_ASSERT_TRUE(emit("fn u64 size(i32@ s) {\n    return s.len;\n}\n"
                          "fn i32 main() {\n    u64 n = 2;\n"
                          "    i32 mut@ own s = new(i32, n);\n"
                          "    i32@ view = s;\n"
                          "    println(view.len, size(s));\n    del(s);\n    return 0;\n}\n"));
    // `own X` converts implicitly to `X`, and `own` is erased: the drop is
    // the ordinary copy of an aggregate and no mark survives it (D17.4, item
    // 18).
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memcpy.p0.p0.i64(ptr align 8 %view.2, ptr align 8 %s.1, i64 16, "
              "i1 false)"),
        "call void @llvm.memcpy.p0.p0.i64(ptr align 8 %view.2, ptr align 8 %s.1, i64 16, "
        "i1 false)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- range for (D7.5, D17.10) ------------------------------------------------------

TEST(a_range_for_over_a_span_walks_it_by_index_with_no_check, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    u64 n = 3;\n"
                          "    i32 mut@ own s = new(i32, n);\n    i32 mut total = 0;\n"
                          "    for (i32 v : s) {\n        total += v;\n    }\n"
                          "    println(total);\n    del(s);\n    return 0;\n}\n"));
    // The counter never passes the length, so the element address needs no
    // bounds check (item 16), and an owning collection is iterated in place
    // (D17.10): the loop reads the length and the pointer out of the
    // collection's own slot, and the one memcpy of the module is the
    // declaration's checked store (D17.11).
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_bounds"), "absent");
    TEST_ASSERT_EQ_STR(found("icmp ult i64 "), "icmp ult i64 ");
    TEST_ASSERT_EQ_SIZE(occurrences("call void @llvm.memcpy"), (size_t)1);
    TEST_ASSERT_EQ_STR(found("  %t7 = load i64, ptr %tmp1, align 8\n"
                             "  %t8 = getelementptr inbounds %fort.span, ptr %s.1, "
                             "i32 0, i32 1\n"),
                       "  %t7 = load i64, ptr %tmp1, align 8\n"
                       "  %t8 = getelementptr inbounds %fort.span, ptr %s.1, "
                       "i32 0, i32 1\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_range_for_over_a_span_expression_copies_it_once, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[4] a = {};\n    i32 mut total = 0;\n"
                          "    for (i32 v : a[1..3]) {\n        total += v;\n    }\n"
                          "    println(total);\n    return 0;\n}\n"));
    // A collection that owns nothing is evaluated once into a temporary
    // before the first iteration (D7.5).
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %fort.span, align 8"),
                       "%tmp0 = alloca %fort.span, align 8");
    TEST_ASSERT_EQ_SIZE(occurrences("call void @\"std.rt.fail_span\"("), (size_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_span", argc, argv);
    TEST_RUN(the_span_type_is_a_pointer_and_a_length);
    TEST_RUN(a_span_is_passed_by_hidden_pointer_and_never_split);
    TEST_RUN(the_pseudo_fields_are_read_through_a_pointer_to_a_span);
    TEST_RUN(indexing_a_span_checks_against_its_header_length);
    TEST_RUN(an_element_of_a_span_of_pointers_is_a_pointer_slot);
    TEST_RUN(a_two_bound_span_checks_both_bounds_in_one_branch);
    TEST_RUN(the_result_is_the_element_at_lo_and_the_difference_of_the_bounds);
    TEST_RUN(an_absent_low_bound_is_zero);
    TEST_RUN(an_absent_high_bound_is_the_operands_length);
    TEST_RUN(the_whole_span_form_still_branches);
    TEST_RUN(a_negative_bound_fails_the_same_unsigned_compare);
    TEST_RUN(a_span_of_a_string_walks_its_bytes);
    TEST_RUN(a_span_of_a_pointer_is_unchecked);
    TEST_RUN(a_span_of_a_span_expression_lands_in_a_temporary);
    TEST_RUN(the_operand_is_evaluated_before_the_bounds);
    TEST_RUN(no_bounds_check_removes_the_span_branch_and_keeps_the_inbounds);
    TEST_RUN(no_bounds_check_reads_no_length_when_both_bounds_are_written);
    TEST_RUN(no_bounds_check_still_reads_the_length_an_absent_bound_needs);
    TEST_RUN(release_mode_keeps_the_span_branch);
    TEST_RUN(no_bounds_check_leaves_the_allocation_and_enum_checks_alone);
    TEST_RUN(a_cast_between_span_shapes_emits_nothing_of_its_own);
    TEST_RUN(lending_an_owned_span_is_a_copy_of_its_header);
    TEST_RUN(a_range_for_over_a_span_walks_it_by_index_with_no_check);
    TEST_RUN(a_range_for_over_a_span_expression_copies_it_once);
    gen_done();
    TEST_EXIT();
}
