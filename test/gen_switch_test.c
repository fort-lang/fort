// Tests switch control-flow emission.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the literals are the test data: the
// fort programs and the IR text each one must produce.

// The shape tests use a switch with three clauses.
// It has a middle `default`, two labels, and one empty body.
static const char THREE_CLAUSES[] = "fn main() i32 {\n"
                                    "    i32 n = 0;\n"
                                    "    i32 mut r = 0;\n"
                                    "    switch (n) {\n"
                                    "    case 1, 2:\n        r = 1;\n"
                                    "    default:\n        r = 2;\n"
                                    "    case 3:\n"
                                    "    }\n"
                                    "    return r;\n}\n";

// A `switch` in a `for`, with `break`, `continue`, and a `default` body.
// The program distinguishes the two jump targets.
static const char SWITCH_IN_LOOP[] = "fn main() i32 {\n"
                                     "    i32 mut s = 0;\n"
                                     "    for (i32 mut i = 0; i < 4; i = i +% 1) {\n"
                                     "        switch (i) {\n"
                                     "        case 1:\n            break;\n"
                                     "        case 2:\n            continue;\n"
                                     "        default:\n            s = s +% 1;\n"
                                     "        }\n"
                                     "        s = s +% 10;\n"
                                     "    }\n"
                                     "    return s;\n}\n";

// A `while` inside a case body: its `break` is the loop's, not the switch's.
static const char LOOP_IN_SWITCH[] = "fn main() i32 {\n"
                                     "    i32 n = 0;\n"
                                     "    i32 mut s = 0;\n"
                                     "    switch (n) {\n"
                                     "    case 1:\n"
                                     "        while (s < 3) {\n"
                                     "            if (s == 1) { break; }\n"
                                     "            s = s +% 1;\n"
                                     "        }\n"
                                     "    default:\n        s = 9;\n"
                                     "    }\n"
                                     "    return s;\n}\n";

// An enum switch with no `default` clause, which the exhaustive-switch rule
// gives one of its own.
static const char EXHAUSTIVE_ENUM[] = "enum color { red, green }\n"
                                      "fn main() i32 {\n    color c = color.red;\n"
                                      "    i32 mut r = 0;\n"
                                      "    switch (c) {\n"
                                      "    case color.red:\n        r = 1;\n"
                                      "    case color.green:\n        r = 2;\n"
                                      "    }\n    return r;\n}\n";

// Each clause returns. The epilogue closes the continuation with `unreachable`, and `{}` supplies a
// value that no clause names.
static const char ENUM_NAME_FN[] = "enum level { low = 1, high = 2 }\n"
                                   "fn name(level l) string {\n"
                                   "    switch (l) {\n"
                                   "    case level.low:\n        return \"low\";\n"
                                   "    case level.high:\n        return \"high\";\n"
                                   "    }\n}\n"
                                   "fn main() i32 {\n    level z = {};\n"
                                   "    println(name(z));\n    return 0;\n}\n";

// A `switch` inside a case body of another: each `break` names its own
// switch's continuation.
static const char SWITCH_IN_SWITCH[] = "fn main() i32 {\n"
                                       "    i32 n = 0;\n"
                                       "    i32 mut s = 0;\n"
                                       "    switch (n) {\n"
                                       "    case 1:\n"
                                       "        switch (s) {\n"
                                       "        case 2:\n            break;\n"
                                       "        default:\n            s = 1;\n"
                                       "        }\n"
                                       "        break;\n"
                                       "    default:\n        s = 2;\n"
                                       "    }\n"
                                       "    return s;\n}\n";

// ---- the shape of one switch ---------------------------------------------

TEST(a_switch_is_one_llvm_switch_on_its_operand, {
    TEST_ASSERT_TRUE(emit(THREE_CLAUSES));
    // One case per label, a block per clause in clause order and the
    // continuation last, which is the order the labels are allocated in. Each
    // body ends in the implicit `break`.
    const char* want = "  %t0 = load i32, ptr %n.0, align 4\n"
                       "  switch i32 %t0, label %L1 [\n"
                       "    i32 1, label %L0\n"
                       "    i32 2, label %L0\n"
                       "    i32 3, label %L2\n"
                       "  ]\n"
                       "\nL0:\n"
                       "  store i32 1, ptr %r.1, align 4\n"
                       "  br label %L3\n"
                       "\nL1:\n"
                       "  store i32 2, ptr %r.1, align 4\n"
                       "  br label %L3\n"
                       "\nL2:\n"
                       "  br label %L3\n"
                       "\nL3:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_default_between_two_cases_is_still_the_default_label, {
    TEST_ASSERT_TRUE(emit(THREE_CLAUSES));
    // The `default` is the second of three clauses. Its block is L1 and the switch names it without
    // listing a value: `default` stands in any position.
    TEST_ASSERT_EQ_STR(found("switch i32 %t0, label %L1 ["), "switch i32 %t0, label %L1 [");
    TEST_ASSERT_EQ_STR(absent(", label %L1\n"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_labels_of_one_clause_name_one_block, {
    TEST_ASSERT_TRUE(emit(THREE_CLAUSES));
    // `case a, b:` shares one body, so both values name the same block.
    TEST_ASSERT_EQ_STR(found("    i32 1, label %L0\n    i32 2, label %L0\n"),
                       "    i32 1, label %L0\n    i32 2, label %L0\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_empty_case_body_only_branches_to_the_continuation, {
    TEST_ASSERT_TRUE(emit(THREE_CLAUSES));
    // An empty case body does nothing and still ends in the implicit `break`.
    TEST_ASSERT_EQ_STR(found("\nL2:\n  br label %L3\n"), "\nL2:\n  br label %L3\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_switch_without_a_default_falls_to_its_continuation, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 n = 0;\n    i32 mut r = 0;\n"
                          "    switch (n) {\n    case 1:\n        r = 1;\n    }\n"
                          "    return r;\n}\n"));
    // Without `default`, the continuation is the default block. An operand that matches no label
    // skips the switch.
    const char* want = "  switch i32 %t0, label %L1 [\n"
                       "    i32 1, label %L0\n"
                       "  ]\n"
                       "\nL0:\n"
                       "  store i32 1, ptr %r.1, align 4\n"
                       "  br label %L1\n"
                       "\nL1:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_switch_with_no_clause_has_an_empty_case_list, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 n = 0;\n    switch (n) {\n    }\n"
                          "    return n;\n}\n"));
    const char* want = "  switch i32 %t0, label %L0 [\n  ]\n\nL0:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_case_body_that_terminates_does_not_branch_to_the_continuation, {
    TEST_ASSERT_TRUE(emit("fn bucket(i32 n) i32 {\n"
                          "    switch (n) {\n"
                          "    case 1:\n        return 10;\n"
                          "    default:\n        return 20;\n"
                          "    }\n}\n"
                          "fn main() i32 { return bucket(1); }\n"));
    // A `return` already ended the block.
    const char* want = "\nL0:\n"
                       "  ret i32 10\n"
                       "\nL1:\n"
                       "  ret i32 20\n"
                       "\nL2:\n"
                       "  unreachable\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_statement_after_a_switch_stands_in_its_continuation, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut n = 0;\n"
                          "    switch (n) {\n    default:\n        n = 1;\n    }\n"
                          "    n = n +% 2;\n    return n;\n}\n"));
    const char* want = "\nL1:\n"
                       "  %t1 = load i32, ptr %n.0, align 4\n"
                       "  %t2 = add i32 %t1, 2\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- break and continue ------------------------------------------------------------

TEST(a_break_inside_a_case_branches_to_the_switchs_continuation, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 n = 0;\n    i32 mut s = 0;\n"
                          "    switch (n) {\n"
                          "    default:\n        if (n == 0) { break; }\n        s = 1;\n"
                          "    }\n    return s;\n}\n"));
    // The `break` of a case with no enclosing loop is lowered like any other:
    // a switch carries a break target of its own.
    TEST_ASSERT_EQ_STR(found("\nL2:\n  br label %L1\n"), "\nL2:\n  br label %L1\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_break_in_a_switch_in_a_loop_exits_the_switch_and_not_the_loop, {
    TEST_ASSERT_TRUE(emit(SWITCH_IN_LOOP));
    // The loop's blocks are L0 to L3 and the switch's are L4 to L7. The `break` naming L7 leaves
    // the switch and runs the statement after it, which is the trap the switch rule documents.
    TEST_ASSERT_EQ_STR(found("\nL4:\n  br label %L7\n"), "\nL4:\n  br label %L7\n");
    TEST_ASSERT_EQ_STR(found("\nL7:\n  %t5 = load i32, ptr %s.0, align 4\n"),
                       "\nL7:\n  %t5 = load i32, ptr %s.0, align 4\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_continue_in_a_switch_targets_the_loop_around_it, {
    TEST_ASSERT_TRUE(emit(SWITCH_IN_LOOP));
    // `continue` targets the innermost enclosing loop, which the switch
    // between does not interrupt: L2 is the step block of the `for`.
    TEST_ASSERT_EQ_STR(found("\nL5:\n  br label %L2\n"), "\nL5:\n  br label %L2\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_case_body_never_falls_into_the_next_clause, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 n = 0;\n    i32 mut r = 0;\n"
                          "    switch (n) {\n"
                          "    case 1:\n        r = 1;\n"
                          "    case 2:\n        r = 2;\n        break;\n"
                          "    }\n    return r;\n}\n"));
    // The first body falls off its end and the second ends in an explicit `break`. Both branch to
    // the continuation and neither reaches the clause below it, because there is no fallthrough.
    const char* want = "\nL0:\n"
                       "  store i32 1, ptr %r.1, align 4\n"
                       "  br label %L2\n"
                       "\nL1:\n"
                       "  store i32 2, ptr %r.1, align 4\n"
                       "  br label %L2\n"
                       "\nL2:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_break_in_a_loop_inside_a_case_targets_the_loop, {
    TEST_ASSERT_TRUE(emit(LOOP_IN_SWITCH));
    // The `while` inside the case owns both targets while its body is emitted,
    // so the `break` names the loop's continuation L5.
    TEST_ASSERT_EQ_STR(found("\nL6:\n  br label %L5\n"), "\nL6:\n  br label %L5\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_switchs_break_target_is_restored_after_a_loop_in_a_case, {
    TEST_ASSERT_TRUE(emit(LOOP_IN_SWITCH));
    // The loop's continuation falls through to the switch's continuation L2,
    // which is the implicit `break` of the case body.
    TEST_ASSERT_EQ_STR(found("\nL5:\n  br label %L2\n"), "\nL5:\n  br label %L2\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_break_in_a_switch_in_a_switch_names_the_inner_one, {
    TEST_ASSERT_TRUE(emit(SWITCH_IN_SWITCH));
    // The inner switch's clauses are L3 and L4 and its continuation L5, so the
    // `break` of its first case leaves by L5.
    TEST_ASSERT_EQ_STR(found("\nL3:\n  br label %L5\n"), "\nL3:\n  br label %L5\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_enclosing_switch_is_the_target_again_after_a_nested_one, {
    TEST_ASSERT_TRUE(emit(SWITCH_IN_SWITCH));
    // The `break` written after the inner switch belongs to the outer one,
    // whose continuation is L2, so the saved target was restored.
    TEST_ASSERT_EQ_STR(found("\nL5:\n  br label %L2\n"), "\nL5:\n  br label %L2\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_switch_inside_a_range_for_keeps_the_loops_continue_target, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32[3] a = {4, 5, 6};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n"
                          "        switch (v) {\n"
                          "        case 5:\n            continue;\n"
                          "        default:\n            s = s +% v;\n"
                          "        }\n    }\n    return s;\n}\n"));
    // A range `for` invents its own step block, L2, and the `continue` inside
    // the switch names it.
    TEST_ASSERT_EQ_STR(found("\nL4:\n  br label %L2\n"), "\nL4:\n  br label %L2\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_loop_in_a_switch_in_a_loop_names_the_innermost_of_each, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut s = 0;\n"
                          "    while (s < 9) {\n"
                          "        switch (s) {\n"
                          "        default:\n"
                          "            for (i32 mut i = 0; i < 2; i = i +% 1) {\n"
                          "                if (i == 0) { continue; }\n"
                          "                break;\n"
                          "            }\n"
                          "            s = s +% 1;\n"
                          "        }\n    }\n    return s;\n}\n"));
    // The outer `while` uses L0 to L2, and the switch uses L3 and L4.
    // The inner `for` uses L5 to L8, and both jumps target it.
    TEST_ASSERT_EQ_STR(found("\nL9:\n  br label %L7\n"), "\nL9:\n  br label %L7\n");
    TEST_ASSERT_EQ_STR(found("\nL10:\n  br label %L8\n"), "\nL10:\n  br label %L8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the operand and the labels ----------------------------------------------------

TEST(a_char_operand_switches_on_i8_with_byte_values, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    char c = 'a';\n    i32 mut r = 0;\n"
                          "    switch (c) {\n"
                          "    case 'a', '\\n':\n        r = 1;\n"
                          "    case '\\xff':\n        r = 2;\n"
                          "    }\n    return r;\n}\n"));
    // `char` is `i8` compared as an unsigned byte, so its labels print as
    // their code points.
    const char* want = "  switch i8 %t0, label %L2 [\n"
                       "    i8 97, label %L0\n"
                       "    i8 10, label %L0\n"
                       "    i8 255, label %L1\n"
                       "  ]\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_enum_operand_switches_on_i32_and_a_negative_member_prints_signed, {
    TEST_ASSERT_TRUE(emit("enum sign { neg = -1, zero, pos }\n"
                          "fn main() i32 {\n    sign s = sign.zero;\n    i32 mut r = 0;\n"
                          "    switch (s) {\n"
                          "    case sign.neg:\n        r = 1;\n"
                          "    case sign.zero, sign.pos:\n        r = 2;\n"
                          "    }\n    return r;\n}\n"));
    // An enum is `i32` and a member's value may be negative, so its label
    // prints as a negative literal, the way its table does. The switch lists
    // every member and has no `default` clause, so L3 is the failure block the
    // exhaustive-switch rule gives it and not the continuation.
    const char* want = "  switch i32 %t0, label %L3 [\n"
                       "    i32 -1, label %L0\n"
                       "    i32 0, label %L1\n"
                       "    i32 1, label %L1\n"
                       "  ]\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_enum_switch_with_no_default_clause_gets_the_default_of_d7_7, {
    TEST_ASSERT_TRUE(emit(EXHAUSTIVE_ENUM));
    // Listing every member is not covering every value. Its label follows the continuation's.
    const char* want = "  %t0 = load i32, ptr %c.0, align 4\n"
                       "  %t1 = sext i32 %t0 to i64\n"
                       "  switch i32 %t0, label %L3 [\n"
                       "    i32 0, label %L0\n"
                       "    i32 1, label %L1\n"
                       "  ]\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    const char* block = "\nL3:\n"
                        "  call void @\"std.rt.fail_enum\"(i64 %t1, ptr @.str.0, ptr @.file.0, "
                        "i32 5, i32 5)\n"
                        "  unreachable\n";
    TEST_ASSERT_EQ_STR(found(block), block);
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [6 x i8] c\"color\\00\""),
                       "@.str.0 = private unnamed_addr constant [6 x i8] c\"color\\00\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_continuation_of_an_enum_switch_is_never_the_default_target, {
    // Each clause of this exhaustive enum switch returns.
    // The default targets the failure block, while the continuation has no incoming edge.
    TEST_ASSERT_TRUE(emit(ENUM_NAME_FN));
    const char* want = "  switch i32 %t0, label %L3 [\n"
                       "    i32 1, label %L0\n"
                       "    i32 2, label %L1\n"
                       "  ]\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(found("\nL2:\n  unreachable\n"), "\nL2:\n  unreachable\n");
    TEST_ASSERT_EQ_STR(absent("label %L2"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_generated_default_is_kept_in_both_build_modes, {
    // It is not a bounds check, so neither `--release` nor `--no-bounds-check`
    // removes it.
    TEST_ASSERT_TRUE(emit_release(ENUM_NAME_FN));
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"std.rt.fail_enum\""), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
    TEST_ASSERT_TRUE(emit_unchecked(ENUM_NAME_FN));
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"std.rt.fail_enum\""), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_enum_switch_with_a_default_clause_gets_no_failure_block, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn main() i32 {\n    color c = color.red;\n    i32 mut r = 0;\n"
                          "    switch (c) {\n"
                          "    case color.red:\n        r = 1;\n"
                          "    default:\n        r = 2;\n"
                          "    }\n    return r;\n}\n"));
    // The clause covers every other value, so there is nothing to report, and
    // no `sext` of the operand is emitted either.
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_enum"), "absent");
    TEST_ASSERT_EQ_STR(absent("sext i32"), "absent");
    TEST_ASSERT_EQ_STR(found("  switch i32 %t0, label %L1 [\n"), "  switch i32 %t0, label %L1 [\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_switch_on_an_integer_with_no_default_gets_no_failure_block, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 n = 0;\n    i32 mut r = 0;\n"
                          "    switch (n) {\n    case 1:\n        r = 1;\n    }\n"
                          "    return r;\n}\n"));
    // Every value of an integer type is a legal value of it. A switch no label matches simply falls
    // to the continuation; the default belongs to enums alone.
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_enum"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_generated_default_precedes_the_failure_blocks_of_the_bodies, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn main(string@ args) i32 {\n    color c = color.red;\n"
                          "    i32 mut r = 0;\n"
                          "    switch (c) {\n"
                          "    case color.red:\n        r = cast(args.len, i32) + 1;\n"
                          "    case color.green:\n        r = 2;\n"
                          "    }\n    return r;\n}\n"));
    // Failure blocks are emitted after every normal block, in ascending label order. The switch's
    // own block, whose label was taken before the bodies were walked, stands before the overflow
    // check's.
    TEST_ASSERT_TRUE(
        before("call void @\"std.rt.fail_enum\"", "call void @\"std.rt.fail_overflow\""));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_signed_label_of_a_narrow_type_prints_as_a_negative, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i8 v = -128;\n"
                          "    switch (v) {\n    case -128:\n        return 1;\n"
                          "    default:\n        return 0;\n    }\n}\n"));
    TEST_ASSERT_EQ_STR(found("    i8 -128, label %L0\n"), "    i8 -128, label %L0\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_unsigned_label_prints_its_whole_range, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u64 v = 18446744073709551615;\n"
                          "    switch (v) {\n    case 18446744073709551615:\n        return 1;\n"
                          "    default:\n        return 0;\n    }\n}\n"));
    // An unsigned label prints unsigned, so the greatest `u64` is not the
    // `i64` -1 its bits would read as.
    TEST_ASSERT_EQ_STR(found("    i64 18446744073709551615, label %L0\n"),
                       "    i64 18446744073709551615, label %L0\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_untyped_constant_operand_takes_its_default_type, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut r = 0;\n"
                          "    switch (200) {\n    case 200:\n        r = 1;\n    }\n"
                          "    return r;\n}\n"));
    // The operand is `i32`, the default type of an untyped integer constant,
    // and the folded operand is a literal of that type.
    TEST_ASSERT_EQ_STR(found("  switch i32 200, label %L1 [\n    i32 200, label %L0\n"),
                       "  switch i32 200, label %L1 [\n    i32 200, label %L0\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_label_that_is_a_constant_expression_prints_its_value, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 n = 5;\n"
                          "    switch (n) {\n    case 2 + 3:\n        return 1;\n"
                          "    case sizeof(i64):\n        return 2;\n"
                          "    default:\n        return 0;\n    }\n}\n"));
    // A label is a constant expression, which the checker folded.
    TEST_ASSERT_EQ_STR(found("    i32 5, label %L0\n    i32 8, label %L1\n"),
                       "    i32 5, label %L0\n    i32 8, label %L1\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_operand_is_evaluated_once, {
    TEST_ASSERT_TRUE(emit("fn count() i32 { return 3; }\n"
                          "fn main() i32 {\n    i32 mut r = 0;\n"
                          "    switch (count()) {\n"
                          "    case 3:\n        r = 1;\n"
                          "    default:\n        r = 2;\n    }\n"
                          "    return r;\n}\n"));
    // One LLVM `switch` on one value: the call is emitted once, whatever the number of labels.
    TEST_ASSERT_EQ_UINT64(occurrences("call i32 @\"main.count\"()"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(found("  %t0 = call i32 @\"main.count\"()\n  switch i32 %t0, label %L1 [\n"),
                       "  %t0 = call i32 @\"main.count\"()\n  switch i32 %t0, label %L1 [\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(each_case_body_is_a_block_scope_with_slots_of_its_own, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut n = 0;\n"
                          "    switch (n) {\n"
                          "    case 1: {\n        i32 x = 1;\n        n = x;\n    }\n"
                          "    default: {\n        i64 x = 2;\n        n = cast(x, i32);\n    }\n"
                          "    }\n    return n;\n}\n"));
    // Each case body is an implicit block scope, so the two locals named `x`
    // are two locals with two entry-block allocas.
    const char* want = "  %x.1 = alloca i32, align 4\n  %x.2 = alloca i64, align 8\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the invariants of the rules ---------------------------------------------

// One program per switch shape the emitter must lower, for the checks that
// answer about a whole module.
static const char* const SWITCH_CORPUS[] = {
    THREE_CLAUSES,
    SWITCH_IN_LOOP,
    LOOP_IN_SWITCH,
    SWITCH_IN_SWITCH,
    "fn main() i32 {\n    i32 n = 0;\n    switch (n) {\n    }\n    return n;\n}\n",
    "enum color { red, green = 5, blue }\n"
    "fn main() i32 {\n    color c = color.blue;\n"
    "    switch (c) {\n"
    "    case color.red, color.green:\n        println(c);\n"
    "    case color.blue:\n        println(\"blue\");\n"
    "    }\n    return 0;\n}\n",
    ENUM_NAME_FN,
    "fn kind(char c) i32 {\n"
    "    switch (c) {\n"
    "    case 'a', 'e':\n        return 1;\n"
    "    default:\n        return 0;\n    }\n}\n"
    "fn main() i32 { return kind('e'); }\n",
    "fn main() i32 {\n    i32[4] a = {1, 2, 3, 4};\n    i32 mut s = 0;\n"
    "    for (i32 v : a) {\n"
    "        switch (v) {\n"
    "        case 1:\n            continue;\n"
    "        case 2:\n            break;\n"
    "        default:\n            s = s +% v;\n"
    "        }\n        s = s +% 1;\n    }\n    return s;\n}\n",
};

enum { SWITCH_CORPUS_LEN = sizeof SWITCH_CORPUS / sizeof SWITCH_CORPUS[0] };

TEST(every_block_of_every_switch_program_ends_in_exactly_one_terminator, {
    for (uint64_t i = 0; i < (uint64_t)SWITCH_CORPUS_LEN; i++) {
        TEST_ASSERT_TRUE(emit(SWITCH_CORPUS[i]));
        // The `switch` is the one terminator the emitter spells over several lines, so the scan
        // must still count it once.
        TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
        TEST_ASSERT_EQ_STR(verified(), "verified");
    }
})

TEST(every_switch_program_verifies_in_release_mode, {
    for (uint64_t i = 0; i < (uint64_t)SWITCH_CORPUS_LEN; i++) {
        TEST_ASSERT_TRUE(emit_release(SWITCH_CORPUS[i]));
        TEST_ASSERT_EQ_STR(verified(), "verified");
    }
})

TEST(every_switch_program_verifies_without_bounds_checks, {
    for (uint64_t i = 0; i < (uint64_t)SWITCH_CORPUS_LEN; i++) {
        TEST_ASSERT_TRUE(emit_unchecked(SWITCH_CORPUS[i]));
        TEST_ASSERT_EQ_STR(verified(), "verified");
    }
})

TEST(two_runs_over_a_switch_program_produce_byte_identical_text, {
    static sb_t first;
    for (uint64_t i = 0; i < (uint64_t)SWITCH_CORPUS_LEN; i++) {
        TEST_ASSERT_TRUE(emit(SWITCH_CORPUS[i]));
        sb_clear(&first);
        sb_append(&first, ir());
        TEST_ASSERT_TRUE(emit(SWITCH_CORPUS[i]));
        TEST_ASSERT_EQ_STR(ir(), sb_cstr(&first));
    }
    sb_free(&first);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_block_counter_is_reset_at_each_switch_bearing_definition, {
    TEST_ASSERT_TRUE(emit("fn first(i32 n) i32 {\n"
                          "    switch (n) {\n    case 1:\n        return 1;\n"
                          "    default:\n        return 0;\n    }\n}\n"
                          "fn second(i32 n) i32 {\n"
                          "    switch (n) {\n    case 2:\n        return 2;\n"
                          "    default:\n        return 0;\n    }\n}\n"
                          "fn main() i32 { return first(1) +% second(2); }\n"));
    // Every counter is per function and reset at each definition, so both
    // switches name L0, L1 and L2.
    TEST_ASSERT_EQ_UINT64(occurrences("  switch i32 %t0, label %L1 [\n"), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(occurrences("\nL2:\n  unreachable\n"), (uint64_t)2);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_switch_builds_no_phi, {
    TEST_ASSERT_TRUE(emit(SWITCH_IN_SWITCH));
    // The emitter carries no value across a merge point.
    TEST_ASSERT_EQ_STR(absent(" phi "), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_switch", argc, argv);
    TEST_RUN(a_switch_is_one_llvm_switch_on_its_operand);
    TEST_RUN(a_default_between_two_cases_is_still_the_default_label);
    TEST_RUN(two_labels_of_one_clause_name_one_block);
    TEST_RUN(an_empty_case_body_only_branches_to_the_continuation);
    TEST_RUN(a_switch_without_a_default_falls_to_its_continuation);
    TEST_RUN(a_switch_with_no_clause_has_an_empty_case_list);
    TEST_RUN(a_case_body_that_terminates_does_not_branch_to_the_continuation);
    TEST_RUN(a_statement_after_a_switch_stands_in_its_continuation);
    TEST_RUN(a_break_inside_a_case_branches_to_the_switchs_continuation);
    TEST_RUN(a_break_in_a_switch_in_a_loop_exits_the_switch_and_not_the_loop);
    TEST_RUN(a_continue_in_a_switch_targets_the_loop_around_it);
    TEST_RUN(a_case_body_never_falls_into_the_next_clause);
    TEST_RUN(a_break_in_a_loop_inside_a_case_targets_the_loop);
    TEST_RUN(the_switchs_break_target_is_restored_after_a_loop_in_a_case);
    TEST_RUN(a_break_in_a_switch_in_a_switch_names_the_inner_one);
    TEST_RUN(the_enclosing_switch_is_the_target_again_after_a_nested_one);
    TEST_RUN(a_switch_inside_a_range_for_keeps_the_loops_continue_target);
    TEST_RUN(a_loop_in_a_switch_in_a_loop_names_the_innermost_of_each);
    TEST_RUN(a_char_operand_switches_on_i8_with_byte_values);
    TEST_RUN(an_enum_operand_switches_on_i32_and_a_negative_member_prints_signed);
    TEST_RUN(an_enum_switch_with_no_default_clause_gets_the_default_of_d7_7);
    TEST_RUN(the_continuation_of_an_enum_switch_is_never_the_default_target);
    TEST_RUN(the_generated_default_is_kept_in_both_build_modes);
    TEST_RUN(an_enum_switch_with_a_default_clause_gets_no_failure_block);
    TEST_RUN(a_switch_on_an_integer_with_no_default_gets_no_failure_block);
    TEST_RUN(the_generated_default_precedes_the_failure_blocks_of_the_bodies);
    TEST_RUN(a_signed_label_of_a_narrow_type_prints_as_a_negative);
    TEST_RUN(an_unsigned_label_prints_its_whole_range);
    TEST_RUN(an_untyped_constant_operand_takes_its_default_type);
    TEST_RUN(a_label_that_is_a_constant_expression_prints_its_value);
    TEST_RUN(the_operand_is_evaluated_once);
    TEST_RUN(each_case_body_is_a_block_scope_with_slots_of_its_own);
    TEST_RUN(every_block_of_every_switch_program_ends_in_exactly_one_terminator);
    TEST_RUN(every_switch_program_verifies_in_release_mode);
    TEST_RUN(every_switch_program_verifies_without_bounds_checks);
    TEST_RUN(two_runs_over_a_switch_program_produce_byte_identical_text);
    TEST_RUN(the_block_counter_is_reset_at_each_switch_bearing_definition);
    TEST_RUN(a_switch_builds_no_phi);
    gen_done();
    TEST_EXIT();
}
