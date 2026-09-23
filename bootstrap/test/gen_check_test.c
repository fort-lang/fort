// Tests checked arithmetic and runtime failure emission.
#include <stdbool.h>
#include <stdint.h>

#include "common/gen_helpers.h"
#include "gen.h"
#include "str.h"

#include "common/test.h"

// The programs the mode tests compile in both build modes.
static const char ADD_I32[] = "fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                              "    return a + b;\n}\n";
static const char ADD_U8[] = "fn main() i32 {\n    u8 a = 7;\n    u8 b = 3;\n"
                             "    return cast(a + b, i32);\n}\n";
static const char SHIFT_I32[] = "fn main() i32 {\n    i32 a = 7;\n    i32 n = 3;\n"
                                "    return a << n;\n}\n";

// ---- the overflow intrinsics ---------------------------------------------

TEST(a_checked_signed_addition_is_the_intrinsic_and_its_two_extractvalues, {
    TEST_ASSERT_TRUE(emit(ADD_I32));
    TEST_ASSERT_EQ_STR(
        found("  %t2 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %t0, i32 %t1)\n"
              "  %t3 = extractvalue { i32, i1 } %t2, 0\n"
              "  %t4 = extractvalue { i32, i1 } %t2, 1\n"
              "  br i1 %t4, label %L1, label %L0\n"),
        "  %t2 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %t0, i32 %t1)\n"
        "  %t3 = extractvalue { i32, i1 } %t2, 0\n"
        "  %t4 = extractvalue { i32, i1 } %t2, 1\n"
        "  br i1 %t4, label %L1, label %L0\n");
})

TEST(the_failure_block_holds_one_call_and_unreachable, {
    TEST_ASSERT_TRUE(emit(ADD_I32));
    // The column of a check is that of its operator token.
    TEST_ASSERT_EQ_STR(
        found("\nL1:\n  call void @\"std.rt.fail_overflow\"(ptr @.file.0, i32 4, i32 14)"
              "\n  unreachable\n}"),
        "\nL1:\n  call void @\"std.rt.fail_overflow\"(ptr @.file.0, i32 4, i32 14)"
        "\n  unreachable\n}");
})

TEST(a_failure_entry_point_is_neither_declared_nor_marked_at_the_call_site, {
    TEST_ASSERT_TRUE(emit(ADD_I32));
    // `std.rt` is in the closure, so the module that holds the call holds the definition and
    // nothing declares it. The `cold noreturn nounwind` attribute group `#2` is not emitted. Its
    // index stays reserved, so `#3` through `#7` keep their numbers. A failure call site also has
    // no attribute.
    TEST_ASSERT_EQ_STR(absent("declare void @\"std.rt.fail_overflow\""), "absent");
    TEST_ASSERT_EQ_STR(absent("attributes #2 ="), "absent");
    TEST_ASSERT_EQ_STR(absent("@\"std.rt.fail_overflow\"(ptr @.file.0, i32 4, i32 14) #"),
                       "absent");
})

TEST(an_unsigned_addition_uses_the_unsigned_intrinsic, {
    TEST_ASSERT_TRUE(emit(ADD_U8));
    TEST_ASSERT_EQ_STR(found("@llvm.uadd.with.overflow.i8(i8 %t0, i8 %t1)"),
                       "@llvm.uadd.with.overflow.i8(i8 %t0, i8 %t1)");
    TEST_ASSERT_EQ_STR(found("declare { i8, i1 } @llvm.uadd.with.overflow.i8(i8, i8) #4"),
                       "declare { i8, i1 } @llvm.uadd.with.overflow.i8(i8, i8) #4");
})

TEST(subtraction_and_multiplication_use_their_own_intrinsics, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                          "    u64 c = 1;\n    u64 d = 2;\n"
                          "    println(a - b, a * b, c - d, c * d);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@llvm.ssub.with.overflow.i32"), "@llvm.ssub.with.overflow.i32");
    TEST_ASSERT_EQ_STR(found("@llvm.smul.with.overflow.i32"), "@llvm.smul.with.overflow.i32");
    TEST_ASSERT_EQ_STR(found("@llvm.usub.with.overflow.i64"), "@llvm.usub.with.overflow.i64");
    TEST_ASSERT_EQ_STR(found("@llvm.umul.with.overflow.i64"), "@llvm.umul.with.overflow.i64");
})

TEST(the_intrinsic_is_taken_at_the_operands_width, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i8 a = 1;\n    i16 b = 1;\n    i32 c = 1;\n"
                          "    i64 d = 1;\n    println(a + a, b + b, c + c, d + d);\n"
                          "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i8("), "@llvm.sadd.with.overflow.i8(");
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i16("), "@llvm.sadd.with.overflow.i16(");
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i32("), "@llvm.sadd.with.overflow.i32(");
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i64("), "@llvm.sadd.with.overflow.i64(");
    // Overflow intrinsics appear in increasing operand width.
    TEST_ASSERT_TRUE(
        before("@llvm.sadd.with.overflow.i8(i8, i8)", "@llvm.sadd.with.overflow.i16(i16, i16)"));
    TEST_ASSERT_TRUE(
        before("@llvm.sadd.with.overflow.i16(i16, i16)", "@llvm.sadd.with.overflow.i32(i32, i32)"));
    TEST_ASSERT_TRUE(
        before("@llvm.sadd.with.overflow.i32(i32, i32)", "@llvm.sadd.with.overflow.i64(i64, i64)"));
})

TEST(the_overflow_intrinsics_carry_their_attribute_group, {
    TEST_ASSERT_TRUE(emit(ADD_I32));
    TEST_ASSERT_EQ_STR(
        found("attributes #4 = { nocallback nofree nosync nounwind speculatable willreturn "
              "memory(none) }"),
        "attributes #4 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) "
        "}");
})

TEST(release_mode_emits_a_plain_add, {
    TEST_ASSERT_TRUE(emit_release(ADD_I32));
    TEST_ASSERT_EQ_STR(found("  %t2 = add i32 %t0, %t1\n  ret i32 %t2\n"),
                       "  %t2 = add i32 %t0, %t1\n  ret i32 %t2\n");
    TEST_ASSERT_EQ_STR(absent("llvm.sadd"), "absent");
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_overflow"), "absent");
    TEST_ASSERT_EQ_STR(absent("\nL0:"), "absent");
})

TEST(release_mode_emits_plain_sub_and_mul, {
    TEST_ASSERT_TRUE(emit_release("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                                  "    println(a - b, a * b);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("sub i32 %t0, %t1"), "sub i32 %t0, %t1");
    TEST_ASSERT_EQ_STR(found("mul i32 %t"), "mul i32 %t");
    TEST_ASSERT_EQ_STR(absent("with.overflow"), "absent");
})

TEST(neither_mode_emits_nsw_nuw_or_exact, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                          "    println(a + b, a - b, a * b, a / b, a % b, a << b, a >> b);\n"
                          "    return 0;\n}\n"));
    // Wrapping is defined behavior and the check has already proved the
    // absence of overflow, so the flags are never emitted.
    TEST_ASSERT_EQ_STR(absent(" nsw "), "absent");
    TEST_ASSERT_EQ_STR(absent(" nuw "), "absent");
    TEST_ASSERT_EQ_STR(absent(" exact "), "absent");
    TEST_ASSERT_TRUE(emit_release("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                                  "    println(a + b, a - b, a * b, a / b, a % b, a << b);\n"
                                  "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(absent(" nsw "), "absent");
    TEST_ASSERT_EQ_STR(absent(" nuw "), "absent");
    TEST_ASSERT_EQ_STR(absent(" exact "), "absent");
})

TEST(the_wrapping_operators_are_plain_in_the_checked_mode, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u8 a = 255;\n    u8 b = 1;\n"
                          "    println(a +% b, a -% b, a *% b);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("add i8 %t0, %t1"), "add i8 %t0, %t1");
    TEST_ASSERT_EQ_STR(found("sub i8 %t"), "sub i8 %t");
    TEST_ASSERT_EQ_STR(found("mul i8 %t"), "mul i8 %t");
    TEST_ASSERT_EQ_STR(absent("with.overflow"), "absent");
})

TEST(a_compound_assignment_uses_the_intrinsic_of_its_operator, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut c = 11;\n    c += 2;\n    return c;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i32(i32 %t0, i32 2)"),
                       "@llvm.sadd.with.overflow.i32(i32 %t0, i32 2)");
})

TEST(increment_and_decrement_use_the_same_intrinsics, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut d = 5;\n    d++;\n    d--;\n"
                          "    return d;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i32(i32 %t0, i32 1)"),
                       "@llvm.sadd.with.overflow.i32(i32 %t0, i32 1)");
    TEST_ASSERT_EQ_STR(found("@llvm.ssub.with.overflow.i32(i32 %t"),
                       "@llvm.ssub.with.overflow.i32(i32 %t");
})

TEST(unary_minus_is_a_checked_subtraction_from_zero, {
    TEST_ASSERT_TRUE(
        emit("fn main() i32 {\n    i8 m = -128;\n    println(-m);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@llvm.ssub.with.overflow.i8(i8 0, i8 %t0)"),
                       "@llvm.ssub.with.overflow.i8(i8 0, i8 %t0)");
})

TEST(a_bitwise_operator_never_checks, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u8 a = 240;\n    u8 b = 60;\n"
                          "    println(a & b, a | b, a ^ b, ~a);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("and i8 %t0, %t1"), "and i8 %t0, %t1");
    TEST_ASSERT_EQ_STR(found("or i8 %t"), "or i8 %t");
    TEST_ASSERT_EQ_STR(found("xor i8 %t"), "xor i8 %t");
    TEST_ASSERT_EQ_STR(absent("with.overflow"), "absent");
})

// ---- shifts --------------------------------------------------------------

TEST(a_shift_count_is_materialized_at_64_bits_and_checked, {
    TEST_ASSERT_TRUE(emit(SHIFT_I32));
    TEST_ASSERT_EQ_STR(found("  %t2 = sext i32 %t1 to i64\n"
                             "  %t3 = icmp uge i64 %t2, 32\n"
                             "  br i1 %t3, label %L1, label %L0\n"),
                       "  %t2 = sext i32 %t1 to i64\n"
                       "  %t3 = icmp uge i64 %t2, 32\n"
                       "  br i1 %t3, label %L1, label %L0\n");
    // The type name is the shifted operand's.
    TEST_ASSERT_EQ_STR(
        found("@\"std.rt.fail_shift\"(i64 %t2, ptr @.str.0, ptr @.file.0, i32 4, i32 14)"),
        "@\"std.rt.fail_shift\"(i64 %t2, ptr @.str.0, ptr @.file.0, i32 4, i32 14)");
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [4 x i8] c\"i32\\00\""),
                       "@.str.0 = private unnamed_addr constant [4 x i8] c\"i32\\00\"");
})

TEST(the_checked_count_is_truncated_to_the_operands_type, {
    TEST_ASSERT_TRUE(emit(SHIFT_I32));
    TEST_ASSERT_EQ_STR(found("  %t4 = trunc i64 %t2 to i32\n  %t5 = shl i32 %t0, %t4\n"),
                       "  %t4 = trunc i64 %t2 to i32\n  %t5 = shl i32 %t0, %t4\n");
})

TEST(an_unsigned_count_is_zero_extended, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 7;\n    u8 n = 3;\n"
                          "    return a << n;\n}\n"));
    TEST_ASSERT_EQ_STR(found("zext i8 %t1 to i64"), "zext i8 %t1 to i64");
})

TEST(a_64_bit_count_needs_no_resize, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i64 a = 1;\n    u64 n = 3;\n"
                          "    println(a << n);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("icmp uge i64 %t1, 64"), "icmp uge i64 %t1, 64");
    TEST_ASSERT_EQ_STR(found("shl i64 %t0, %t1"), "shl i64 %t0, %t1");
})

TEST(a_right_shift_is_arithmetic_for_a_signed_operand, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = -8;\n    i32 n = 1;\n"
                          "    return a >> n;\n}\n"));
    TEST_ASSERT_EQ_STR(found("ashr i32 %t0, %t4"), "ashr i32 %t0, %t4");
})

TEST(a_right_shift_is_logical_for_an_unsigned_operand, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u32 a = 8;\n    u32 n = 1;\n"
                          "    println(a >> n);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("lshr i32 %t0, %t4"), "lshr i32 %t0, %t4");
})

TEST(release_mode_masks_the_shift_count_instead_of_checking_it, {
    TEST_ASSERT_TRUE(emit_release(SHIFT_I32));
    TEST_ASSERT_EQ_STR(found("  %t3 = and i64 %t2, 31\n  %t4 = trunc i64 %t3 to i32\n"
                             "  %t5 = shl i32 %t0, %t4\n"),
                       "  %t3 = and i64 %t2, 31\n  %t4 = trunc i64 %t3 to i32\n"
                       "  %t5 = shl i32 %t0, %t4\n");
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_shift"), "absent");
})

// ---- division and remainder ----------------------------------------------

TEST(a_signed_division_checks_zero_then_the_overflow_of_min_by_minus_one, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                          "    return a / b;\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %t2 = icmp eq i32 %t1, 0\n  br i1 %t2, label %L1, label %L0\n"),
                       "  %t2 = icmp eq i32 %t1, 0\n  br i1 %t2, label %L1, label %L0\n");
    TEST_ASSERT_EQ_STR(found("  %t3 = icmp eq i32 %t1, -1\n"
                             "  %t4 = icmp eq i32 %t0, -2147483648\n"
                             "  %t5 = and i1 %t3, %t4\n"
                             "  br i1 %t5, label %L3, label %L2\n"),
                       "  %t3 = icmp eq i32 %t1, -1\n"
                       "  %t4 = icmp eq i32 %t0, -2147483648\n"
                       "  %t5 = and i1 %t3, %t4\n"
                       "  br i1 %t5, label %L3, label %L2\n");
    TEST_ASSERT_EQ_STR(found("  %t6 = sdiv i32 %t0, %t1\n"), "  %t6 = sdiv i32 %t0, %t1\n");
})

TEST(the_minimum_of_each_width_is_printed_as_its_signed_value, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i8 a = 1;\n    i8 b = 1;\n    i64 c = 1;\n"
                          "    i64 d = 1;\n    println(a / b, c / d);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("icmp eq i8 %t0, -128"), "icmp eq i8 %t0, -128");
    TEST_ASSERT_EQ_STR(found("icmp eq i64 %t8, -9223372036854775808"),
                       "icmp eq i64 %t8, -9223372036854775808");
})

TEST(an_unsigned_division_checks_zero_alone, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u32 a = 7;\n    u32 b = 3;\n"
                          "    println(a / b, a % b);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("udiv i32 %t"), "udiv i32 %t");
    TEST_ASSERT_EQ_STR(found("urem i32 %t"), "urem i32 %t");
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_div_overflow"), "absent");
})

TEST(a_remainder_takes_the_same_two_checks, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                          "    return a % b;\n}\n"));
    TEST_ASSERT_EQ_STR(found("srem i32 %t0, %t1"), "srem i32 %t0, %t1");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_div_zero\"(ptr @.file.0, i32 4, i32 14)"),
                       "@\"std.rt.fail_div_zero\"(ptr @.file.0, i32 4, i32 14)");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_div_overflow\"(ptr @.file.0, i32 4, i32 14)"),
                       "@\"std.rt.fail_div_overflow\"(ptr @.file.0, i32 4, i32 14)");
})

TEST(release_mode_keeps_both_division_checks, {
    TEST_ASSERT_TRUE(emit_release("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                                  "    return a / b;\n}\n"));
    // Division by zero and MIN / -1 are runtime errors in every build mode.
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_div_zero\""), "@\"std.rt.fail_div_zero\"");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_div_overflow\""), "@\"std.rt.fail_div_overflow\"");
})

// ---- bounds checks -------------------------------------------------------

TEST(an_index_is_extended_to_64_bits_and_compared_unsigned, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32[3] a = {};\n    i64 mut i = 5;\n"
                          "    return a[i];\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %t0 = load i64, ptr %i.1, align 8\n"
                             "  %t1 = icmp uge i64 %t0, 3\n"
                             "  br i1 %t1, label %L1, label %L0\n"),
                       "  %t0 = load i64, ptr %i.1, align 8\n"
                       "  %t1 = icmp uge i64 %t0, 3\n"
                       "  br i1 %t1, label %L1, label %L0\n");
    TEST_ASSERT_EQ_STR(
        found("@\"std.rt.fail_bounds\"(i64 %t0, i64 3, ptr @.file.0, i32 4, i32 13)"),
        "@\"std.rt.fail_bounds\"(i64 %t0, i64 3, ptr @.file.0, i32 4, i32 13)");
})

TEST(a_signed_index_is_sign_extended_so_a_negative_one_fails_the_same_compare, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32[3] a = {};\n    i32 i = 1;\n"
                          "    return a[i];\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %t1 = sext i32 %t0 to i64\n  %t2 = icmp uge i64 %t1, 3\n"),
                       "  %t1 = sext i32 %t0 to i64\n  %t2 = icmp uge i64 %t1, 3\n");
})

TEST(an_unsigned_index_is_zero_extended, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32[3] a = {};\n    u8 i = 1;\n"
                          "    return a[i];\n}\n"));
    TEST_ASSERT_EQ_STR(found("zext i8 %t0 to i64"), "zext i8 %t0 to i64");
})

TEST(a_span_length_is_read_from_its_header, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string s = \"hi\";\n    u64 i = 0;\n"
                          "    println(s[i]);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1"),
                       "getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1");
    TEST_ASSERT_EQ_STR(found("  %t4 = load i64, ptr %t3, align 8\n"
                             "  %t5 = icmp uge i64 %t2, %t4\n"),
                       "  %t4 = load i64, ptr %t3, align 8\n"
                       "  %t5 = icmp uge i64 %t2, %t4\n");
})

TEST(no_bounds_check_removes_the_branch_and_keeps_the_inbounds, {
    TEST_ASSERT_TRUE(emit_unchecked("fn main() i32 {\n    i32[3] a = {};\n    i64 mut i = 5;\n"
                                    "    return a[i];\n}\n"));
    // --no-bounds-check removes exactly the index branch, which is what makes
    // it unsafe.
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_bounds"), "absent");
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0"),
                       "getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0");
})

TEST(no_bounds_check_leaves_the_arithmetic_checks_alone, {
    TEST_ASSERT_TRUE(emit_unchecked(ADD_I32));
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i32"), "@llvm.sadd.with.overflow.i32");
})

// ---- failure blocks ------------------------------------------------------

TEST(failure_blocks_are_emitted_after_every_normal_block_in_ascending_order, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                          "    i32 c = a + b;\n    i32 d = a / b;\n    return c + d;\n}\n"));
    TEST_ASSERT_TRUE(before("\nL0:", "\nL1:"));
    TEST_ASSERT_TRUE(before("\nL2:", "\nL4:"));
    // Every failure block stands after the last normal one.
    TEST_ASSERT_TRUE(before("ret i32 ", "\nL1:\n  call void @\"std.rt.fail_overflow\""));
    TEST_ASSERT_TRUE(before("\nL1:\n  call void @\"std.rt.fail_overflow\"",
                            "\nL3:\n  call void @\"std.rt.fail_div_zero\""));
    TEST_ASSERT_TRUE(before("\nL3:\n  call void @\"std.rt.fail_div_zero\"",
                            "\nL5:\n  call void @\"std.rt.fail_div_overflow\""));
})

TEST(the_continuation_label_is_allocated_before_the_failure_label, {
    TEST_ASSERT_TRUE(emit(ADD_I32));
    // The branch names the failure block first and the continuation second.
    TEST_ASSERT_EQ_STR(found("br i1 %t4, label %L1, label %L0"), "br i1 %t4, label %L1, label %L0");
})

TEST(the_emitter_builds_no_phi, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 7;\n    i32 b = 3;\n"
                          "    bool t = a > 0 && b > 0;\n    println(t, a + b);\n"
                          "    return 0;\n}\n"));
    // A temporary is used only in the block that defines it or in one it
    // dominates; the optimizer builds the phis.
    TEST_ASSERT_EQ_STR(absent("phi "), "absent");
})

// ---- assert and panic ----------------------------------------------------

TEST(assert_branches_to_the_continuation_first_and_quotes_its_argument, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 x = 0;\n    assert(x > 0);\n"
                          "    return 0;\n}\n"));
    // Its operand is already the success condition, which is the one
    // exception to the shape a check takes.
    TEST_ASSERT_EQ_STR(found("  %t1 = icmp sgt i32 %t0, 0\n  br i1 %t1, label %L0, label %L1\n"),
                       "  %t1 = icmp sgt i32 %t0, 0\n  br i1 %t1, label %L0, label %L1\n");
    // The text is the source text of the expression, verbatim, and the column
    // is the builtin's name.
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [6 x i8] c\"x > 0\\00\""),
                       "@.str.0 = private unnamed_addr constant [6 x i8] c\"x > 0\\00\"");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.assert_fail\"(ptr @.str.0, ptr @.file.0, i32 3, i32 5)"),
                       "@\"std.rt.assert_fail\"(ptr @.str.0, ptr @.file.0, i32 3, i32 5)");
})

TEST(assert_is_active_in_release_mode_too, {
    TEST_ASSERT_TRUE(emit_release("fn main() i32 {\n    i32 x = 0;\n    assert(x > 0);\n"
                                  "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"std.rt.assert_fail\""), "@\"std.rt.assert_fail\"");
})

TEST(assert_of_a_constant_still_branches, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    assert(1 + 1 == 2);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("br i1 true, label %L0, label %L1"),
                       "br i1 true, label %L0, label %L1");
    TEST_ASSERT_EQ_STR(found("c\"1 + 1 == 2\\00\""), "c\"1 + 1 == 2\\00\"");
})

TEST(panic_calls_the_runtime_and_is_followed_by_unreachable, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    println(\"before\");\n    panic(\"boom\");\n}\n"));
    TEST_ASSERT_EQ_STR(
        found("  call void @\"std.rt.panic\"(ptr @.str.1, i64 4, ptr @.file.0, i32 3, i32 5)\n"
              "  unreachable\n"),
        "  call void @\"std.rt.panic\"(ptr @.str.1, i64 4, ptr @.file.0, i32 3, i32 5)\n"
        "  unreachable\n");
})

// ---- comparisons -------------------------------------------------------------------

TEST(a_signed_comparison_uses_the_signed_predicates, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 3;\n    i32 b = 5;\n"
                          "    println(a < b, a <= b, a > b, a >= b, a == b, a != b);\n"
                          "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("icmp slt i32"), "icmp slt i32");
    TEST_ASSERT_EQ_STR(found("icmp sle i32"), "icmp sle i32");
    TEST_ASSERT_EQ_STR(found("icmp sgt i32"), "icmp sgt i32");
    TEST_ASSERT_EQ_STR(found("icmp sge i32"), "icmp sge i32");
    TEST_ASSERT_EQ_STR(found("icmp eq i32"), "icmp eq i32");
    TEST_ASSERT_EQ_STR(found("icmp ne i32"), "icmp ne i32");
})

TEST(an_unsigned_comparison_uses_the_unsigned_predicates, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u32 a = 3;\n    u32 b = 5;\n"
                          "    println(a < b, a >= b);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("icmp ult i32"), "icmp ult i32");
    TEST_ASSERT_EQ_STR(found("icmp uge i32"), "icmp uge i32");
})

TEST(a_char_compares_as_an_unsigned_byte, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    char c = 'a';\n    println(c < 'b');\n"
                          "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("icmp ult i8 %t0, 98"), "icmp ult i8 %t0, 98");
})

TEST(a_pointer_compares_as_a_pointer, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 v = 1;\n    i32* p = &v;\n    i32* q = p;\n"
                          "    println(p == q, p != null);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("icmp eq ptr %t1, %t2"), "icmp eq ptr %t1, %t2");
    TEST_ASSERT_EQ_STR(found("icmp ne ptr %t"), "icmp ne ptr %t");
})

TEST(an_enum_compares_as_i32, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn main() i32 {\n    color g = color.green;\n"
                          "    println(g == color.red);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("icmp eq i32 %t0, 0"), "icmp eq i32 %t0, 0");
})

TEST(a_short_circuit_goes_through_a_compiler_made_slot, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 1;\n    i32 b = 2;\n"
                          "    bool t = a > 0 && b > 0;\n    println(t);\n    return 0;\n}\n"));
    // The slot is an entry-block alloca whose name contains no dot, so it
    // cannot collide with a local.
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca i8, align 1"), "%tmp0 = alloca i8, align 1");
    TEST_ASSERT_EQ_STR(found("br i1 %t1, label %L0, label %L1"), "br i1 %t1, label %L0, label %L1");
    TEST_ASSERT_EQ_STR(found("  br label %L1\n"), "  br label %L1\n");
})

TEST(an_or_short_circuit_branches_the_other_way, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 a = 1;\n    i32 b = 2;\n"
                          "    bool t = a > 0 || b > 0;\n    println(t);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("br i1 %t1, label %L1, label %L0"), "br i1 %t1, label %L1, label %L0");
})

// ---- traps and constants ------------------------------------------

TEST(a_call_to_a_noreturn_extern_still_traps, {
    TEST_ASSERT_TRUE(emit("extern fn exit(i32 code) noreturn;\n"
                          "fn main() i32 { println(\"bye\"); exit(0); }\n"));
    // The declaration carries no noreturn, so the optimizer cannot delete the trap; an extern that
    // returns anyway must still hit it.
    TEST_ASSERT_EQ_STR(found("declare void @exit(i32) nobuiltin\n"),
                       "declare void @exit(i32) nobuiltin\n");
    TEST_ASSERT_EQ_STR(absent("noreturn void @exit"), "absent");
    TEST_ASSERT_EQ_STR(found("  call void @exit(i32 0) #3\n"
                             "  call void @llvm.trap()\n  unreachable\n"),
                       "  call void @exit(i32 0) #3\n"
                       "  call void @llvm.trap()\n  unreachable\n");
})

TEST(an_integer_constant_is_printed_with_the_signedness_of_its_type, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i8 a = -1;\n    u8 b = 255;\n"
                          "    i64 c = -9223372036854775808;\n"
                          "    u64 d = 18446744073709551615;\n    u16 e = 65535;\n"
                          "    i16 f = -32768;\n    println(a, b, c, d, e, f);\n"
                          "    return 0;\n}\n"));
    // The decimal is printed without padding and with the signedness of the
    // fort type.
    TEST_ASSERT_EQ_STR(found("store i8 -1, ptr %a.0"), "store i8 -1, ptr %a.0");
    TEST_ASSERT_EQ_STR(found("store i8 255, ptr %b.1"), "store i8 255, ptr %b.1");
    TEST_ASSERT_EQ_STR(found("store i64 -9223372036854775808, ptr %c.2"),
                       "store i64 -9223372036854775808, ptr %c.2");
    TEST_ASSERT_EQ_STR(found("store i64 18446744073709551615, ptr %d.3"),
                       "store i64 18446744073709551615, ptr %d.3");
    TEST_ASSERT_EQ_STR(found("store i16 65535, ptr %e.4"), "store i16 65535, ptr %e.4");
    TEST_ASSERT_EQ_STR(found("store i16 -32768, ptr %f.5"), "store i16 -32768, ptr %f.5");
})

TEST(every_failure_block_and_continuation_ends_in_one_terminator, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32[3] a = {1, 2, 3};\n    i32 mut s = 0;\n"
                          "    i32 n = 3;\n"
                          "    for (i32 mut i = 0; i < n; i++) {\n"
                          "        s += a[i] / n;\n        s <<= 1;\n"
                          "        assert(s >= 0);\n    }\n    return s;\n}\n"));
    // `verified` checks that before it runs `opt`, which exits 0 on a block that holds two.
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

int main(int argc, char** argv) {
    TEST_INIT("gen_check", argc, argv);
    TEST_RUN(a_checked_signed_addition_is_the_intrinsic_and_its_two_extractvalues);
    TEST_RUN(the_failure_block_holds_one_call_and_unreachable);
    TEST_RUN(a_failure_entry_point_is_neither_declared_nor_marked_at_the_call_site);
    TEST_RUN(an_unsigned_addition_uses_the_unsigned_intrinsic);
    TEST_RUN(subtraction_and_multiplication_use_their_own_intrinsics);
    TEST_RUN(the_intrinsic_is_taken_at_the_operands_width);
    TEST_RUN(the_overflow_intrinsics_carry_their_attribute_group);
    TEST_RUN(release_mode_emits_a_plain_add);
    TEST_RUN(release_mode_emits_plain_sub_and_mul);
    TEST_RUN(neither_mode_emits_nsw_nuw_or_exact);
    TEST_RUN(the_wrapping_operators_are_plain_in_the_checked_mode);
    TEST_RUN(a_compound_assignment_uses_the_intrinsic_of_its_operator);
    TEST_RUN(increment_and_decrement_use_the_same_intrinsics);
    TEST_RUN(unary_minus_is_a_checked_subtraction_from_zero);
    TEST_RUN(a_bitwise_operator_never_checks);
    TEST_RUN(a_shift_count_is_materialized_at_64_bits_and_checked);
    TEST_RUN(the_checked_count_is_truncated_to_the_operands_type);
    TEST_RUN(an_unsigned_count_is_zero_extended);
    TEST_RUN(a_64_bit_count_needs_no_resize);
    TEST_RUN(a_right_shift_is_arithmetic_for_a_signed_operand);
    TEST_RUN(a_right_shift_is_logical_for_an_unsigned_operand);
    TEST_RUN(release_mode_masks_the_shift_count_instead_of_checking_it);
    TEST_RUN(a_signed_division_checks_zero_then_the_overflow_of_min_by_minus_one);
    TEST_RUN(the_minimum_of_each_width_is_printed_as_its_signed_value);
    TEST_RUN(an_unsigned_division_checks_zero_alone);
    TEST_RUN(a_remainder_takes_the_same_two_checks);
    TEST_RUN(release_mode_keeps_both_division_checks);
    TEST_RUN(an_index_is_extended_to_64_bits_and_compared_unsigned);
    TEST_RUN(a_signed_index_is_sign_extended_so_a_negative_one_fails_the_same_compare);
    TEST_RUN(an_unsigned_index_is_zero_extended);
    TEST_RUN(a_span_length_is_read_from_its_header);
    TEST_RUN(no_bounds_check_removes_the_branch_and_keeps_the_inbounds);
    TEST_RUN(no_bounds_check_leaves_the_arithmetic_checks_alone);
    TEST_RUN(failure_blocks_are_emitted_after_every_normal_block_in_ascending_order);
    TEST_RUN(the_continuation_label_is_allocated_before_the_failure_label);
    TEST_RUN(the_emitter_builds_no_phi);
    TEST_RUN(assert_branches_to_the_continuation_first_and_quotes_its_argument);
    TEST_RUN(assert_is_active_in_release_mode_too);
    TEST_RUN(assert_of_a_constant_still_branches);
    TEST_RUN(panic_calls_the_runtime_and_is_followed_by_unreachable);
    TEST_RUN(a_signed_comparison_uses_the_signed_predicates);
    TEST_RUN(an_unsigned_comparison_uses_the_unsigned_predicates);
    TEST_RUN(a_char_compares_as_an_unsigned_byte);
    TEST_RUN(a_pointer_compares_as_a_pointer);
    TEST_RUN(an_enum_compares_as_i32);
    TEST_RUN(a_short_circuit_goes_through_a_compiler_made_slot);
    TEST_RUN(an_or_short_circuit_branches_the_other_way);
    TEST_RUN(every_failure_block_and_continuation_ends_in_one_terminator);
    TEST_RUN(a_call_to_a_noreturn_extern_still_traps);
    TEST_RUN(an_integer_constant_is_printed_with_the_signedness_of_its_type);
    gen_done();
    TEST_EXIT();
}
