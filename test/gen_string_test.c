// Unit tests of the `string` half of the emitter (toolchain.md 6 items 8, 16,
// 17 and 19): the header a literal writes, `==` and `!=` through the runtime
// entry point of section 5.1, and the operations a `string` shares with a
// span.
// D3.7
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

// The two programs the comparison tests share.
static const char EQ_LITERAL[] = "fn main() i32 {\n    string s = \"hello\";\n"
                                 "    println(s == \"hello\");\n    return 0;\n}\n";
static const char NE_LITERAL[] = "fn main() i32 {\n    string s = \"hello\";\n"
                                 "    println(s != \"hi\");\n    return 0;\n}\n";

// ---- the header of a literal (item 17) ---------------------------------------------
// D3.7

TEST(a_string_literal_is_its_bytes_and_the_length_the_nul_excludes, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string s = \"hello\";\n"
                          "    println(s.len);\n    return 0;\n}\n"));
    // The bytes carry a trailing NUL that `len` does not count.
    // D3.7
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [6 x i8] c\"hello\\00\""),
                       "@.str.0 = private unnamed_addr constant [6 x i8] c\"hello\\00\"");
    TEST_ASSERT_EQ_STR(found("  %t0 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  store ptr @.str.0, ptr %t0, align 8\n"
                             "  %t1 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                             "  store i64 5, ptr %t1, align 8\n"),
                       "  %t0 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  store ptr @.str.0, ptr %t0, align 8\n"
                       "  %t1 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                       "  store i64 5, ptr %t1, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_empty_string_literal_is_one_nul_byte_and_a_zero_length, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string s = \"\";\n"
                          "    println(s.len);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [1 x i8] c\"\\00\""),
                       "@.str.0 = private unnamed_addr constant [1 x i8] c\"\\00\"");
    TEST_ASSERT_EQ_STR(found("store i64 0, ptr %t1, align 8"), "store i64 0, ptr %t1, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_zero_string_is_a_sixteen_byte_memset, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string s = {};\n"
                          "    println(s.len);\n    return 0;\n}\n"));
    // The zero value of a span or `string` is `{null, 0}`.
    // D3.5, D3.7
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)"),
        "call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- equality ----------------------------------------------------------------------
// D3.7, D9.8

TEST(string_equality_is_one_call_to_the_runtime_entry_point, {
    TEST_ASSERT_TRUE(emit(EQ_LITERAL));
    // The operand's header is read field by field and the literal arrives as
    // its constant and its length, with no header built for it (item 19).
    TEST_ASSERT_EQ_STR(
        found("  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
              "  %t3 = load ptr, ptr %t2, align 8\n"
              "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
              "  %t5 = load i64, ptr %t4, align 8\n"
              "  %t6 = call zeroext i1 @\"std.rt.str_eq\"(ptr %t3, i64 %t5, ptr @.str.1, i64 5)\n"),
        "  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
        "  %t3 = load ptr, ptr %t2, align 8\n"
        "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
        "  %t5 = load i64, ptr %t4, align 8\n"
        "  %t6 = call zeroext i1 @\"std.rt.str_eq\"(ptr %t3, i64 %t5, ptr @.str.1, i64 5)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(inequality_is_the_same_call_negated, {
    TEST_ASSERT_TRUE(emit(NE_LITERAL));
    TEST_ASSERT_EQ_STR(found("  %t7 = xor i1 %t6, true\n"), "  %t7 = xor i1 %t6, true\n");
    // One call either way: the negation is in the module and not in the
    // runtime.
    // D3.7
    TEST_ASSERT_EQ_SIZE(occurrences("call zeroext i1 @\"std.rt.str_eq\"("), (size_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(comparing_two_places_reads_both_headers, {
    TEST_ASSERT_TRUE(emit("fn same(string a, string b) bool {\n    return a == b;\n}\n"
                          "fn main() i32 {\n    println(same(\"a\", \"a\"));\n    return 0;\n}\n"));
    // An aggregate parameter's place is the caller-made copy the incoming
    // pointer designates (item 10), so both operands are header reads.
    TEST_ASSERT_EQ_STR(
        found("  %t0 = getelementptr inbounds %fort.span, ptr %a.in, i32 0, i32 0\n"
              "  %t1 = load ptr, ptr %t0, align 8\n"
              "  %t2 = getelementptr inbounds %fort.span, ptr %a.in, i32 0, i32 1\n"
              "  %t3 = load i64, ptr %t2, align 8\n"
              "  %t4 = getelementptr inbounds %fort.span, ptr %b.in, i32 0, i32 0\n"
              "  %t5 = load ptr, ptr %t4, align 8\n"
              "  %t6 = getelementptr inbounds %fort.span, ptr %b.in, i32 0, i32 1\n"
              "  %t7 = load i64, ptr %t6, align 8\n"
              "  %t8 = call zeroext i1 @\"std.rt.str_eq\"(ptr %t1, i64 %t3, ptr %t5, i64 %t7)\n"),
        "  %t0 = getelementptr inbounds %fort.span, ptr %a.in, i32 0, i32 0\n"
        "  %t1 = load ptr, ptr %t0, align 8\n"
        "  %t2 = getelementptr inbounds %fort.span, ptr %a.in, i32 0, i32 1\n"
        "  %t3 = load i64, ptr %t2, align 8\n"
        "  %t4 = getelementptr inbounds %fort.span, ptr %b.in, i32 0, i32 0\n"
        "  %t5 = load ptr, ptr %t4, align 8\n"
        "  %t6 = getelementptr inbounds %fort.span, ptr %b.in, i32 0, i32 1\n"
        "  %t7 = load i64, ptr %t6, align 8\n"
        "  %t8 = call zeroext i1 @\"std.rt.str_eq\"(ptr %t1, i64 %t3, ptr %t5, i64 %t7)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_left_operand_is_evaluated_before_the_right, {
    TEST_ASSERT_TRUE(emit("fn left() string {\n    println(\"left\");\n    return \"a\";\n}\n"
                          "fn right() string {\n    println(\"right\");\n    return \"b\";\n}\n"
                          "fn main() i32 {\n    println(left() == right());\n    return 0;\n}\n"));
    // Each argument is evaluated in turn, left to right.
    // D6.3
    TEST_ASSERT_TRUE(before("call void @\"main.left\"", "call void @\"main.right\""));
    TEST_ASSERT_TRUE(before("call void @\"main.right\"", "@\"std.rt.str_eq\"("));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_entry_point_is_never_declared, {
    TEST_ASSERT_TRUE(emit("extern fn puts(char* s) i32;\n"
                          "fn main() i32 {\n    string s = \"hi\";\n"
                          "    println(s == \"hi\", s == \"ho\");\n"
                          "    return puts(s.ptr);\n}\n"));
    // `std.rt` is in the closure, so the module that holds the call holds the
    // definition and nothing declares it: a `declare` beside a `define` is a
    // redefinition `opt` rejects (item 8). The declarations section holds the
    // externs and the intrinsics and no third group.
    TEST_ASSERT_EQ_SIZE(occurrences("call zeroext i1 @\"std.rt.str_eq\"("), (size_t)2);
    TEST_ASSERT_EQ_STR(absent("declare zeroext i1 @\"std.rt.str_eq\""), "absent");
    TEST_ASSERT_EQ_STR(absent("declare void @\"std.rt."), "absent");
    TEST_ASSERT_EQ_STR(found("declare i32 @puts(ptr) nobuiltin"),
                       "declare i32 @puts(ptr) nobuiltin");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_program_that_compares_no_string_declares_no_entry_point, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string s = \"hi\";\n"
                          "    println(s.len);\n    return 0;\n}\n"));
    // Only referenced declarations are emitted (item 8).
    // D19.5
    TEST_ASSERT_EQ_STR(absent("std.rt.str_eq"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(release_mode_compares_strings_the_same_way, {
    TEST_ASSERT_TRUE(emit_release(EQ_LITERAL));
    // The comparison is not a check, so neither build mode changes it.
    // D11.1
    TEST_ASSERT_EQ_STR(found("@\"std.rt.str_eq\"(ptr %t3, i64 %t5, ptr @.str.1, i64 5)"),
                       "@\"std.rt.str_eq\"(ptr %t3, i64 %t5, ptr @.str.1, i64 5)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(no_bounds_check_compares_strings_the_same_way, {
    TEST_ASSERT_TRUE(emit_unchecked(EQ_LITERAL));
    TEST_ASSERT_EQ_STR(found("@\"std.rt.str_eq\"(ptr %t3, i64 %t5, ptr @.str.1, i64 5)"),
                       "@\"std.rt.str_eq\"(ptr %t3, i64 %t5, ptr @.str.1, i64 5)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- indexing and the pseudo-fields -------------------------------------------------
// D3.7, D6.8

TEST(indexing_a_string_reaches_its_bytes_through_the_pointer_field, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string s = \"hi\";\n    u64 i = 1;\n"
                          "    println(s[i]);\n    return 0;\n}\n"));
    // A `string` has `char` elements, which are `i8`, reached through the
    // header's pointer (item 3).
    // D3.2, D19.2
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds i8, ptr %t7, i64 %t2"),
                       "getelementptr inbounds i8, ptr %t7, i64 %t2");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_char\""), "@\"std.rt.print_char\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_pseudo_fields_of_a_string_are_its_header_fields, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string s = \"hi\";\n"
                          "    println(s.len, s.ptr != null);\n    return 0;\n}\n"));
    // `.len` and `.ptr` are read-only pseudo-fields of the header.
    // D3.5, D3.7
    TEST_ASSERT_EQ_STR(found("  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                             "  %t3 = load i64, ptr %t2, align 8\n"),
                       "  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                       "  %t3 = load i64, ptr %t2, align 8\n");
    TEST_ASSERT_EQ_STR(found("  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  %t5 = load ptr, ptr %t4, align 8\n"),
                       "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  %t5 = load ptr, ptr %t4, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_string_is_passed_and_returned_through_a_hidden_pointer, {
    TEST_ASSERT_TRUE(emit("fn pick(string a) string {\n    return a;\n}\n"
                          "fn main() i32 {\n    println(pick(\"hi\"));\n    return 0;\n}\n"));
    // A span or `string` stays one hidden pointer and is never split into two
    // scalars (item 7).
    // D9.9
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"main.pick\"(ptr sret(%fort.span) "
                             "%ret.sret, ptr %a.in) #0"),
                       "define dso_local void @\"main.pick\"(ptr sret(%fort.span) "
                       "%ret.sret, ptr %a.in) #0");
    // The result place comes first and the argument copy second (item 7).
    TEST_ASSERT_EQ_STR(found("call void @\"main.pick\"(ptr sret(%fort.span) %tmp0, ptr %tmp1)"),
                       "call void @\"main.pick\"(ptr sret(%fort.span) %tmp0, ptr %tmp1)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_string", argc, argv);
    TEST_RUN(a_string_literal_is_its_bytes_and_the_length_the_nul_excludes);
    TEST_RUN(an_empty_string_literal_is_one_nul_byte_and_a_zero_length);
    TEST_RUN(the_zero_string_is_a_sixteen_byte_memset);
    TEST_RUN(string_equality_is_one_call_to_the_runtime_entry_point);
    TEST_RUN(inequality_is_the_same_call_negated);
    TEST_RUN(comparing_two_places_reads_both_headers);
    TEST_RUN(the_left_operand_is_evaluated_before_the_right);
    TEST_RUN(the_entry_point_is_never_declared);
    TEST_RUN(a_program_that_compares_no_string_declares_no_entry_point);
    TEST_RUN(release_mode_compares_strings_the_same_way);
    TEST_RUN(no_bounds_check_compares_strings_the_same_way);
    TEST_RUN(indexing_a_string_reaches_its_bytes_through_the_pointer_field);
    TEST_RUN(the_pseudo_fields_of_a_string_are_its_header_fields);
    TEST_RUN(a_string_is_passed_and_returned_through_a_hidden_pointer);
    gen_done();
    TEST_EXIT();
}
