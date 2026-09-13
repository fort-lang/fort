// Unit tests of the control flow the emitter writes (toolchain.md 6 items 10
// and 11): `if`, `while`, every `for` form, the range `for`, `break` and
// `continue`, all of them explicit blocks and `br` and never a `phi`, with one
// terminator per block.
// D7.4, D7.5, D8.4, D19.4, D19.5
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the literals are the test data: the
// fort programs and the IR text each one must produce.

// A program whose every statement form is a branch, used by the structural
// tests that answer about the whole module rather than about one snippet.
static const char BUSY_SOURCE[] = "struct pair { i32 x; i32 y; }\n"
                                  "fn i32 main() {\n"
                                  "    i32 mut s = 0;\n"
                                  "    if (s < 1) { s = 1; } else if (s < 2) { s = 2; }\n"
                                  "    while (s < 9) { s = s +% 1; if (s == 4) { continue; } }\n"
                                  "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                                  "        if (i == 1) { break; }\n"
                                  "    }\n"
                                  "    for (;;) { break; }\n"
                                  "    i32[3] a = {1, 2, 3};\n"
                                  "    for (i32 v : a) { s = s +% v; }\n"
                                  "    pair[2] ps = {};\n"
                                  "    for (pair p : ps) { s = s +% p.x; }\n"
                                  "    for (char c : \"hi\") { if (c == 'h') { continue; } }\n"
                                  "    return s;\n"
                                  "}\n";

// ---- if, else if, else -------------------------------------------------------------
// D7.4

TEST(an_if_without_an_else_branches_to_its_continuation, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    if (n < 1) {\n        n = 2;\n    }\n    return n;\n}\n"));
    // The false edge of an `if` with no `else` is the continuation itself, and
    // the branch that falls off the end of the body is the only terminator of
    // its block (item 10).
    // D7.4
    const char* want = "  br i1 %t1, label %L0, label %L1\n"
                       "\nL0:\n"
                       "  store i32 2, ptr %n.0, align 4\n"
                       "  br label %L1\n"
                       "\nL1:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
})

TEST(an_if_with_an_else_gives_each_branch_its_own_block, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    if (n < 1) {\n        n = 2;\n    } else {\n        n = 3;\n    }\n"
                          "    return n;\n}\n"));
    // Labels are `%L<N>` in creation order, so the then block, the else block
    // and the continuation are allocated in that order.
    // D19.5
    const char* want = "  br i1 %t1, label %L0, label %L1\n"
                       "\nL0:\n"
                       "  store i32 2, ptr %n.0, align 4\n"
                       "  br label %L2\n"
                       "\nL1:\n"
                       "  store i32 3, ptr %n.0, align 4\n"
                       "  br label %L2\n"
                       "\nL2:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
})

TEST(an_else_if_chain_nests_in_the_else_block, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 n = 0;\n"
                          "    if (n < 1) {\n        return 1;\n"
                          "    } else if (n < 2) {\n        return 2;\n"
                          "    } else {\n        return 3;\n    }\n}\n"));
    // The `else` of an `else if` is the nested `if` itself, so its test stands
    // in the else block.
    // D7.4
    const char* want = "\nL1:\n"
                       "  %t2 = load i32, ptr %n.0, align 4\n"
                       "  %t3 = icmp slt i32 %t2, 2\n"
                       "  br i1 %t3, label %L3, label %L4\n"
                       "\nL3:\n"
                       "  ret i32 2\n"
                       "\nL4:\n"
                       "  ret i32 3\n";
    TEST_ASSERT_EQ_STR(found(want), want);
})

TEST(a_branch_that_terminates_does_not_branch_to_the_continuation, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 n = 0;\n"
                          "    if (n < 1) {\n        return 1;\n"
                          "    } else {\n        return 2;\n    }\n}\n"));
    // An `if` whose branches both terminate is itself terminating, so neither
    // branch branches to the continuation, which is left unreachable.
    // D8.4
    const char* want = "\nL0:\n  ret i32 1\n\nL1:\n  ret i32 2\n\nL2:\n  unreachable\n}";
    TEST_ASSERT_EQ_STR(found(want), want);
})

TEST(the_statements_after_a_terminating_if_stand_in_a_fresh_block, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    if (n < 1) {\n        return 1;\n        n = 2;\n    }\n"
                          "    return 0;\n}\n"));
    // After a terminating statement the emitter opens a fresh `%L<N>` block
    // for the unreachable statements the parser allows (item 10).
    // D14.2
    const char* want = "\nL0:\n  ret i32 1\n\nL2:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
})

// ---- while -------------------------------------------------------------------------
// D7.5

TEST(a_while_has_a_head_a_body_and_a_continuation, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    while (n < 3) {\n        n = n +% 1;\n    }\n    return n;\n}\n"));
    // The condition is re-evaluated in the head block, which the body
    // branches back to (item 10).
    const char* want = "  br label %L0\n"
                       "\nL0:\n"
                       "  %t0 = load i32, ptr %n.0, align 4\n"
                       "  %t1 = icmp slt i32 %t0, 3\n"
                       "  br i1 %t1, label %L1, label %L2\n"
                       "\nL1:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(found("  store i32 %t3, ptr %n.0, align 4\n  br label %L0\n\nL2:\n"),
                       "  store i32 %t3, ptr %n.0, align 4\n  br label %L0\n\nL2:\n");
})

TEST(a_while_true_with_no_break_leaves_its_continuation_unreachable, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    while (true) {\n        n = n +% 1;\n    }\n}\n"));
    // `while (true)` with no `break` targeting it is a terminating statement,
    // so nothing branches to the continuation and the body's back-edge is the
    // only edge out of it.
    // D8.4
    TEST_ASSERT_EQ_STR(found("  br i1 true, label %L1, label %L2\n"),
                       "  br i1 true, label %L1, label %L2\n");
    TEST_ASSERT_EQ_STR(found("\nL2:\n  unreachable\n}"), "\nL2:\n  unreachable\n}");
})

// ---- for, every form ---------------------------------------------------------------
// D7.5

TEST(a_for_puts_its_step_in_a_block_of_its_own, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                          "        s = s +% i;\n    }\n    return s;\n}\n"));
    // The init runs in the block the loop stands in and the step in a block of
    // its own, which `continue` targets because `continue` in a `for` runs
    // `step`.
    // D7.5
    const char* want = "  store i32 0, ptr %i.1, align 4\n"
                       "  br label %L0\n"
                       "\nL0:\n"
                       "  %t0 = load i32, ptr %i.1, align 4\n"
                       "  %t1 = icmp slt i32 %t0, 3\n"
                       "  br i1 %t1, label %L1, label %L3\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    const char* step = "\nL2:\n"
                       "  %t5 = load i32, ptr %i.1, align 4\n"
                       "  %t6 = add i32 %t5, 1\n"
                       "  store i32 %t6, ptr %i.1, align 4\n"
                       "  br label %L0\n";
    TEST_ASSERT_EQ_STR(found(step), step);
})

TEST(a_for_with_an_empty_condition_branches_straight_into_its_body, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (s = 0; ; s = s +% 1) {\n        break;\n    }\n"
                          "    return s;\n}\n"));
    // An empty condition means `true`, so the head branches straight in.
    // D7.5
    TEST_ASSERT_EQ_STR(found("\nL0:\n  br label %L1\n"), "\nL0:\n  br label %L1\n");
})

TEST(a_for_with_no_init_and_no_step_still_has_all_four_blocks, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (; s < 3;) {\n        s = s +% 1;\n    }\n    return s;\n}\n"));
    // An empty step leaves its block, which is what keeps `continue` one label
    // whatever the header holds.
    // D7.5
    TEST_ASSERT_EQ_STR(found("\nL2:\n  br label %L0\n"), "\nL2:\n  br label %L0\n");
})

TEST(for_ever_is_a_head_that_only_branches_in, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (;;) {\n        s = s +% 1;\n        if (s > 2) { break; }\n"
                          "    }\n    return s;\n}\n"));
    // `for (;;)` is legal and is the loop with no header at all.
    // D7.5
    TEST_ASSERT_EQ_STR(found("  br label %L0\n\nL0:\n  br label %L1\n"),
                       "  br label %L0\n\nL0:\n  br label %L1\n");
    // Its `break` leaves by the continuation, which the loop allocated last.
    TEST_ASSERT_EQ_STR(found("\nL4:\n  br label %L3\n"), "\nL4:\n  br label %L3\n");
})

// ---- break and continue ------------------------------------------------------------
// D7.5

TEST(a_continue_in_a_while_branches_to_the_head, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    while (n < 3) {\n        n = n +% 1;\n"
                          "        if (n == 1) { continue; }\n    }\n    return n;\n}\n"));
    // `continue` in a `while` re-tests the condition, so it targets the head.
    // D7.5
    TEST_ASSERT_EQ_STR(found("\nL3:\n  br label %L0\n"), "\nL3:\n  br label %L0\n");
})

TEST(a_continue_in_a_for_branches_to_the_step_block, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                          "        if (i == 1) { continue; }\n        s = s +% i;\n    }\n"
                          "    return s;\n}\n"));
    // `continue` in a `for` runs `step`, so it targets the step block and not
    // the head.
    // D7.5
    TEST_ASSERT_EQ_STR(found("\nL4:\n  br label %L2\n"), "\nL4:\n  br label %L2\n");
})

TEST(a_break_branches_to_the_continuation_of_its_loop, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    while (n < 3) {\n        if (n == 1) { break; }\n"
                          "        n = n +% 1;\n    }\n    return n;\n}\n"));
    TEST_ASSERT_EQ_STR(found("\nL3:\n  br label %L2\n"), "\nL3:\n  br label %L2\n");
})

TEST(break_and_continue_target_the_innermost_enclosing_loop, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (i32 mut i = 0; i < 2; i = i +% 1) {\n"
                          "        for (i32 mut j = 0; j < 2; j = j +% 1) {\n"
                          "            if (j == 1) { break; }\n"
                          "            if (i == 1) { continue; }\n"
                          "            s = s +% 1;\n"
                          "        }\n    }\n    return s;\n}\n"));
    // The inner loop's head, body, step and continuation are L4 to L7, and
    // both jumps name the inner loop's labels.
    // D7.5
    TEST_ASSERT_EQ_STR(found("  br label %L4\n\nL4:\n"), "  br label %L4\n\nL4:\n");
    TEST_ASSERT_EQ_STR(found("  br i1 %t3, label %L5, label %L7\n"),
                       "  br i1 %t3, label %L5, label %L7\n");
    // The `break` leaves by the inner continuation L7 and the `continue` by
    // the inner step block L6, never by the outer loop's L3 and L2.
    // D7.5
    TEST_ASSERT_EQ_STR(found("\nL8:\n  br label %L7\n"), "\nL8:\n  br label %L7\n");
    TEST_ASSERT_EQ_STR(found("\nL10:\n  br label %L6\n"), "\nL10:\n  br label %L6\n");
    // The inner continuation falls through to the outer step block.
    TEST_ASSERT_EQ_STR(found("\nL7:\n  br label %L2\n"), "\nL7:\n  br label %L2\n");
})

// ---- range for ---------------------------------------------------------------------
// D7.5, D17.10

TEST(a_range_for_over_a_fixed_array_walks_an_invented_counter, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[3] a = {1, 2, 3};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n        s = s +% v;\n    }\n    return s;\n}\n"));
    // The counter is a place the compiler invents, `%tmp<K>` from its own
    // counter, and an entry-block alloca like every other.
    // D19.4, D19.5
    TEST_ASSERT_EQ_STR(found("  %tmp1 = alloca i64, align 8\n"), "  %tmp1 = alloca i64, align 8\n");
    const char* want = "  store i64 0, ptr %tmp1, align 8\n"
                       "  br label %L0\n"
                       "\nL0:\n"
                       "  %t3 = load i64, ptr %tmp1, align 8\n"
                       "  %t4 = icmp ult i64 %t3, 3\n"
                       "  br i1 %t4, label %L1, label %L3\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    // A fixed array's length is an `i64` literal and its elements are reached
    // with the array shape of item 3.
    TEST_ASSERT_EQ_STR(
        found("  %t5 = getelementptr inbounds [3 x i32], ptr %tmp0, i64 0, i64 %t3\n"),
        "  %t5 = getelementptr inbounds [3 x i32], ptr %tmp0, i64 0, i64 %t3\n");
    // The counter is bounded by the length, so its increment is a plain
    // `add` and not a checked one (item 15).
    const char* step = "\nL2:\n"
                       "  %t10 = load i64, ptr %tmp1, align 8\n"
                       "  %t11 = add i64 %t10, 1\n"
                       "  store i64 %t11, ptr %tmp1, align 8\n"
                       "  br label %L0\n";
    TEST_ASSERT_EQ_STR(found(step), step);
})

TEST(a_fixed_array_that_owns_nothing_is_iterated_over_a_copy, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[3] a = {1, 2, 3};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n        s = s +% v;\n    }\n    return s;\n}\n"));
    // The collection is evaluated once before the first iteration, and a fixed
    // array that owns nothing is evaluated as a value, so the loop iterates
    // over the copy.
    // D7.5
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %a.0,"
              " i64 12, i1 false)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %a.0,"
        " i64 12, i1 false)\n");
})

TEST(an_owning_collection_is_iterated_in_place, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut* own[2] mut a = {};\n"
                          "    for (i32 mut* p : a) {\n        if (p != null) { return 1; }\n"
                          "    }\n    return 0;\n}\n"));
    // The loop lends its collection: an owning fixed array is iterated in
    // place and is never moved or copied, so the only copy is the element's.
    // D17.10
    TEST_ASSERT_EQ_STR(absent("@llvm.memcpy"), "absent");
    TEST_ASSERT_EQ_STR(
        found("  %t2 = getelementptr inbounds [2 x ptr], ptr %a.0, i64 0, i64 %t0\n"),
        "  %t2 = getelementptr inbounds [2 x ptr], ptr %a.0, i64 0, i64 %t0\n");
    // The loop variable's type is the element type with its outermost `own`
    // removed, so its slot holds a plain pointer.
    // D7.6, D17.10
    TEST_ASSERT_EQ_STR(found("  %p.1 = alloca ptr, align 8\n"), "  %p.1 = alloca ptr, align 8\n");
})

TEST(a_range_for_over_a_string_uses_the_element_gep_shape, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    for (char c : \"hi\") {\n        n = n +% 1;\n    }\n"
                          "    return n;\n}\n"));
    // A span's length is its header field and its elements are reached through
    // its `.ptr`; a `string` has `char` elements (item 3).
    // D3.7
    const char* head = "  %t2 = load i64, ptr %tmp1, align 8\n"
                       "  %t3 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 1\n"
                       "  %t4 = load i64, ptr %t3, align 8\n"
                       "  %t5 = icmp ult i64 %t2, %t4\n";
    TEST_ASSERT_EQ_STR(found(head), head);
    const char* body = "  %t6 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
                       "  %t7 = load ptr, ptr %t6, align 8\n"
                       "  %t8 = getelementptr inbounds i8, ptr %t7, i64 %t2\n"
                       "  %t9 = load i8, ptr %t8, align 1\n"
                       "  store i8 %t9, ptr %c.1, align 1\n";
    TEST_ASSERT_EQ_STR(found(body), body);
})

TEST(an_aggregate_element_is_copied_into_the_loop_variable_with_a_memcpy, {
    TEST_ASSERT_TRUE(emit("struct pair { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    pair[2] a = {};\n    i32 mut s = 0;\n"
                          "    for (pair p : a) {\n        s = s +% p.x;\n    }\n"
                          "    return s;\n}\n"));
    // `x` is a fresh copy of each element taken at the start of its iteration,
    // and an aggregate is copied with `llvm.memcpy`.
    // D7.5, D19.3
    const char* want = "  %t2 = getelementptr inbounds [2 x %struct.main.pair], ptr %tmp0,"
                       " i64 0, i64 %t0\n"
                       "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %p.2, ptr align 4 %t2,"
                       " i64 8, i1 false)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
})

TEST(the_loop_variable_of_a_range_for_is_an_entry_block_alloca, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[2] a = {1, 2};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n        s = s +% v;\n    }\n    return s;\n}\n"));
    // A range `for` declares its loop variable on the loop itself, and that
    // variable is a local like any other: `%<ident>.<slot>` by its index in
    // the function, allocated in the entry block.
    // D19.4, D19.5
    const char* want = "entry:\n"
                       "  %a.0 = alloca [2 x i32], align 4\n"
                       "  %s.1 = alloca i32, align 4\n"
                       "  %v.2 = alloca i32, align 4\n";
    TEST_ASSERT_EQ_STR(found(want), want);
})

TEST(a_body_that_terminates_does_not_branch_to_the_step_block, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n"
                          "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                          "        return i;\n    }\n    return 0;\n}\n"));
    // The body already ended in a terminator, so no branch to the step block
    // is added and the step block keeps its back-edge alone (item 10).
    TEST_ASSERT_EQ_STR(found("\nL1:\n  %t2 = load i32, ptr %i.0, align 4\n  ret i32 %t2\n"),
                       "\nL1:\n  %t2 = load i32, ptr %i.0, align 4\n  ret i32 %t2\n");
    TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
})

TEST(an_increment_step_uses_the_overflow_intrinsic_of_its_type, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (i32 mut i = 0; i < 3; i++) {\n        s = s +% 1;\n    }\n"
                          "    return s;\n}\n"));
    // `++` is a step form of its own and uses the same intrinsics as `+` (item
    // 15), which puts a check and its failure block inside the step block.
    // D7.5
    TEST_ASSERT_EQ_STR(found("@llvm.sadd.with.overflow.i32"), "@llvm.sadd.with.overflow.i32");
    TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_call_is_a_legal_init_and_step, {
    TEST_ASSERT_TRUE(emit("fn void bump(i32 mut* mut c) { *c = *c +% 1; }\n"
                          "fn i32 main() {\n    i32 mut c = 0;\n"
                          "    for (bump(&c); c < 3; bump(&c)) {\n    }\n    return c;\n}\n"));
    // `init` and `step` may each be a call: the init's call stands before the
    // head and the step's inside the step block.
    // D7.5
    TEST_ASSERT_EQ_STR(found("  call void @\"main.bump\"(ptr %c.0)\n  br label %L0\n"),
                       "  call void @\"main.bump\"(ptr %c.0)\n  br label %L0\n");
    const char* step = "\nL2:\n  call void @\"main.bump\"(ptr %c.0)\n  br label %L0\n";
    TEST_ASSERT_EQ_STR(found(step), step);
})

TEST(break_and_continue_in_a_range_for_target_its_own_blocks, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[3] a = {1, 2, 3};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n        if (v == 1) { continue; }\n"
                          "        if (v == 3) { break; }\n        s = s +% v;\n    }\n"
                          "    return s;\n}\n"));
    // A range `for` is a loop like any other: `continue` runs its step, which
    // advances the counter, and `break` leaves by its continuation.
    // D7.5
    TEST_ASSERT_EQ_STR(found("\nL4:\n  br label %L2\n"), "\nL4:\n  br label %L2\n");
    TEST_ASSERT_EQ_STR(found("\nL6:\n  br label %L3\n"), "\nL6:\n  br label %L3\n");
})

TEST(a_range_for_emits_no_bounds_check_of_its_own, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[3] a = {1, 2, 3};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n        s = s +% v;\n    }\n    return s;\n}\n"));
    // The counter is bounded by the length the head compares against, so the
    // element address needs no `std.rt.fail_bounds` branch of its own (item
    // 16); a program with no check has no `@.file.N` either (item 5).
    TEST_ASSERT_EQ_STR(absent("@\"std.rt.fail_bounds\""), "absent");
    TEST_ASSERT_EQ_STR(absent("@.file."), "absent");
})

TEST(the_no_bounds_check_mode_leaves_the_blocks_of_a_loop_alone, {
    static const char SOURCE[] = "fn i32 main() {\n    i32[3] a = {1, 2, 3};\n    i32 mut s = 0;\n"
                                 "    for (i32 v : a) {\n        s = s +% v;\n    }\n"
                                 "    return s;\n}\n";
    TEST_ASSERT_TRUE(emit(SOURCE));
    static sb_t checked;
    sb_clear(&checked);
    sb_append(&checked, ir());
    TEST_ASSERT_TRUE(emit_unchecked(SOURCE));
    // `--no-bounds-check` removes exactly the index and span branches, and a
    // range `for` has none, so the two texts agree.
    // D10.6
    TEST_ASSERT_EQ_STR(ir(), sb_cstr(&checked));
})

TEST(a_nested_if_inside_a_loop_rejoins_before_the_back_edge, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    while (n < 3) {\n        if (n == 1) { n = n +% 2; }\n"
                          "        n = n +% 1;\n    }\n    return n;\n}\n"));
    // The `if`'s continuation is where the body resumes, so the loop's
    // back-edge leaves from it and not from the branch (item 10).
    const char* want = "\nL4:\n"
                       "  %t6 = load i32, ptr %n.0, align 4\n"
                       "  %t7 = add i32 %t6, 1\n"
                       "  store i32 %t7, ptr %n.0, align 4\n"
                       "  br label %L0\n";
    TEST_ASSERT_EQ_STR(found(want), want);
})

TEST(a_loop_inside_a_loop_restores_the_enclosing_targets, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    while (s < 9) {\n"
                          "        for (i32 mut i = 0; i < 2; i = i +% 1) { break; }\n"
                          "        if (s == 4) { break; }\n        s = s +% 1;\n    }\n"
                          "    return s;\n}\n"));
    // The inner loop saved and restored the enclosing loop's labels, so the
    // outer `break` after it still names the outer continuation L2.
    // D7.5
    TEST_ASSERT_EQ_STR(found("\nL7:\n  br label %L2\n"), "\nL7:\n  br label %L2\n");
    TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_noreturn_init_leaves_the_entry_edge_out, {
    TEST_ASSERT_TRUE(emit("fn noreturn die() { panic(\"gone\"); }\n"
                          "fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (die(); s < 3; s = s +% 1) {\n    }\n    return s;\n}\n"));
    // `init` may be a call and a call to a `noreturn` function is a
    // terminating statement that already ended the block with a trap (item
    // 20), so no branch to the head is added after it (item 10).
    // D7.5, D8.4
    const char* want = "  call void @\"main.die\"()\n"
                       "  call void @llvm.trap()\n"
                       "  unreachable\n"
                       "\nL0:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_noreturn_step_leaves_the_back_edge_out, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut s = 0;\n"
                          "    for (i32 mut i = 0; i < 3; panic(\"boom\")) {\n"
                          "        s = s +% 1;\n    }\n    return s;\n}\n"));
    // `step` may be a call too, and `panic` is `noreturn` (item 19), so the
    // step block ends at its `unreachable` and not at a second terminator.
    // D8.4
    TEST_ASSERT_EQ_STR(found("\nL2:\n  call void @\"std.rt.panic\"("),
                       "\nL2:\n  call void @\"std.rt.panic\"(");
    TEST_ASSERT_EQ_STR(found("  unreachable\n\nL3:\n"), "  unreachable\n\nL3:\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_check_inside_a_loop_puts_its_failure_block_after_every_normal_one, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[3] a = {1, 2, 3};\n    i32 mut s = 0;\n"
                          "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                          "        s = s +% a[i];\n    }\n    return s;\n}\n"));
    // Failure blocks are emitted after every normal block of the function, so
    // a check inside a loop leaves its block below the loop's, while its `%tN`
    // operands are defined inside the loop that dominates it (item 11).
    // D19.6
    TEST_ASSERT_TRUE(before("\nL3:\n", "\nL5:\n  call void @\"std.rt.fail_bounds\""));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_range_for_over_an_empty_collection_never_enters_its_body, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 mut n = 0;\n"
                          "    for (char c : \"\") {\n        n = n +% 1;\n    }\n"
                          "    return n;\n}\n"));
    // Nothing special is emitted for an empty collection: the head's `icmp
    // ult` is false at the first iteration and the body is skipped.
    // D7.5
    TEST_ASSERT_EQ_STR(found("  %t5 = icmp ult i64 %t2, %t4\n"), "  %t5 = icmp ult i64 %t2, %t4\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_range_for_over_a_single_element_array_compares_against_one, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[1] a = {7};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n        s = s +% v;\n    }\n    return s;\n}\n"));
    // A fixed array's length is an `i64` literal, whatever it is (item 16).
    TEST_ASSERT_EQ_STR(found("  %t2 = icmp ult i64 %t1, 1\n"), "  %t2 = icmp ult i64 %t1, 1\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_range_for_inside_a_range_for_keeps_two_counters, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32[2] a = {1, 2};\n    i32 mut s = 0;\n"
                          "    for (i32 v : a) {\n"
                          "        for (i32 w : a) {\n"
                          "            if (w == 1) { continue; }\n"
                          "            if (v == 2) { break; }\n"
                          "            s = s +% w;\n        }\n    }\n    return s;\n}\n"));
    // Each loop invents its own counter and its own copy of the collection,
    // from the one per-function `%tmp<K>` counter, and the inner loop's
    // `break` and `continue` name the inner loop's blocks.
    // D19.5, D7.5
    TEST_ASSERT_EQ_STR(found("  %tmp3 = alloca i64, align 8\n"), "  %tmp3 = alloca i64, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the invariants of items 10 and 11 ---------------------------------------------

TEST(every_block_of_a_control_flow_module_ends_in_exactly_one_terminator, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    // Every block ends in exactly one terminator and holds no instruction
    // after it, which is the half of item 10 that `opt -passes=verify` also
    // confirms.
    TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// The two shapes the terminator scan must reject, so that the tests above
// answer about the emitter and not about a scan that always agrees.
static const char NO_TERMINATOR[] = "define dso_local void @\"main.f\"() #0 {\n"
                                    "entry:\n"
                                    "  br label %L0\n"
                                    "\nL0:\n"
                                    "  store i32 0, ptr %n.0, align 4\n"
                                    "}\n";
static const char AFTER_TERMINATOR[] = "define dso_local void @\"main.f\"() #0 {\n"
                                       "entry:\n"
                                       "  ret void\n"
                                       "  store i32 0, ptr %n.0, align 4\n"
                                       "}\n";

// An LLVM `switch` is the one terminator printed over several lines; its
// continuation lines are indented past an instruction's two spaces and its
// last line holds the closing bracket.
// D7.7, T-020: the ticket that made the emitter write a switch
static const char MULTI_LINE_SWITCH[] = "define dso_local void @\"main.f\"() #0 {\n"
                                        "entry:\n"
                                        "  switch i32 %t0, label %L0 [\n"
                                        "    i32 1, label %L1\n"
                                        "    i32 2, label %L2\n"
                                        "  ]\n"
                                        "\nL0:\n"
                                        "  ret void\n"
                                        "}\n";

TEST(the_terminator_scan_rejects_a_block_with_none_and_one_with_two, {
    TEST_ASSERT_EQ_STR(gen_block_terminators_of(NO_TERMINATOR), "L0: has 0 terminators");
    TEST_ASSERT_EQ_STR(gen_block_terminators_of(AFTER_TERMINATOR),
                       "entry: has an instruction after its terminator");
})

TEST(the_terminator_scan_counts_a_multi_line_switch_once, {
    TEST_ASSERT_EQ_STR(gen_block_terminators_of(MULTI_LINE_SWITCH), "one terminator per block");
})

TEST(the_terminator_scan_refuses_to_pass_a_module_with_no_definition, {
    // A truncated module, or one the emitter never wrote a function into,
    // would otherwise report success with no block looked at.
    TEST_ASSERT_EQ_STR(gen_block_terminators_of("target triple = \"x86_64\"\n"),
                       "the module holds no closed definition");
    TEST_ASSERT_EQ_STR(gen_block_terminators_of("define dso_local void @\"main.f\"() #0 {\n"
                                                "entry:\n  ret void\n"),
                       "the module holds no closed definition");
})

TEST(control_flow_builds_no_phi_and_carries_no_value_across_a_merge, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    // The emitter builds no `phi` and carries no value across a merge point:
    // every user-visible value lives in an alloca (item 11).
    // D19.4
    TEST_ASSERT_EQ_STR(absent("phi "), "absent");
})

TEST(a_short_circuit_in_a_condition_keeps_its_own_blocks, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 a = 1;\n    i32 b = 2;\n"
                          "    if (a > 0 && b > 0) {\n        return 1;\n    }\n    return 0;\n}\n"
                          ""));
    // `&&` short-circuits through a stack slot rather than a `phi`, so the
    // tree walk never has to know its predecessors (item 10); the `if` then
    // branches on the slot's value.
    // D19.4
    TEST_ASSERT_EQ_STR(found("  %tmp0 = alloca i8, align 1\n"), "  %tmp0 = alloca i8, align 1\n");
    TEST_ASSERT_EQ_STR(absent("phi "), "absent");
    TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// The determinism of the emitted text is guarded by three tests, each owning a
// third of it, and no one of them can be dropped without losing its own: the
// golden-IR tests above own the label order (a loop that allocated its blocks
// in another order fails them),
// `the_block_counter_is_reset_at_each_definition` owns the per-function reset,
// and the test below owns the absence of hash order (it goes red when
// `collect_locals` is ordered by symbol address).
// D19.5
TEST(two_runs_over_a_control_flow_program_produce_byte_identical_text, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    static sb_t first;
    sb_clear(&first);
    sb_append(&first, ir());
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    // Blocks are `%L<N>` in creation order with a per-function counter, so two
    // runs over one program emit the same text, which is what lets stage2 and
    // stage3 reach a fixpoint.
    // D19.5
    TEST_ASSERT_EQ_STR(ir(), sb_cstr(&first));
})

TEST(the_block_counter_is_reset_at_each_definition, {
    TEST_ASSERT_TRUE(emit("fn i32 first(i32 n) {\n    if (n < 1) { return 1; }\n    return 0;\n}\n"
                          "fn i32 main() {\n    if (first(1) < 1) { return 1; }\n"
                          "    return 0;\n}\n"));
    // Every counter is per function and reset at each definition, so both
    // definitions start their blocks at L0.
    // D19.5
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"main.first\"(i32 %n.in) #0 {\n"),
                       "define dso_local i32 @\"main.first\"(i32 %n.in) #0 {\n");
    TEST_ASSERT_TRUE(before("@\"main.first\"", "\nL0:\n"));
    const char* second = "define dso_local i32 @\"main.main\"() #0 {\n"
                         "entry:\n"
                         "  %t0 = call i32 @\"main.first\"(i32 1)\n"
                         "  %t1 = icmp slt i32 %t0, 1\n"
                         "  br i1 %t1, label %L0, label %L1\n";
    TEST_ASSERT_EQ_STR(found(second), second);
})

TEST(a_control_flow_module_passes_the_verifier_in_release_mode_too, {
    TEST_ASSERT_TRUE(emit_release(BUSY_SOURCE));
    // Release mode changes the arithmetic, never the blocks.
    // D11.1
    TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_control", argc, argv);
    TEST_RUN(an_if_without_an_else_branches_to_its_continuation);
    TEST_RUN(an_if_with_an_else_gives_each_branch_its_own_block);
    TEST_RUN(an_else_if_chain_nests_in_the_else_block);
    TEST_RUN(a_branch_that_terminates_does_not_branch_to_the_continuation);
    TEST_RUN(the_statements_after_a_terminating_if_stand_in_a_fresh_block);
    TEST_RUN(a_while_has_a_head_a_body_and_a_continuation);
    TEST_RUN(a_while_true_with_no_break_leaves_its_continuation_unreachable);
    TEST_RUN(a_for_puts_its_step_in_a_block_of_its_own);
    TEST_RUN(a_for_with_an_empty_condition_branches_straight_into_its_body);
    TEST_RUN(a_for_with_no_init_and_no_step_still_has_all_four_blocks);
    TEST_RUN(for_ever_is_a_head_that_only_branches_in);
    TEST_RUN(a_continue_in_a_while_branches_to_the_head);
    TEST_RUN(a_continue_in_a_for_branches_to_the_step_block);
    TEST_RUN(a_break_branches_to_the_continuation_of_its_loop);
    TEST_RUN(break_and_continue_target_the_innermost_enclosing_loop);
    TEST_RUN(a_range_for_over_a_fixed_array_walks_an_invented_counter);
    TEST_RUN(a_fixed_array_that_owns_nothing_is_iterated_over_a_copy);
    TEST_RUN(an_owning_collection_is_iterated_in_place);
    TEST_RUN(a_range_for_over_a_string_uses_the_element_gep_shape);
    TEST_RUN(an_aggregate_element_is_copied_into_the_loop_variable_with_a_memcpy);
    TEST_RUN(the_loop_variable_of_a_range_for_is_an_entry_block_alloca);
    TEST_RUN(a_body_that_terminates_does_not_branch_to_the_step_block);
    TEST_RUN(a_noreturn_init_leaves_the_entry_edge_out);
    TEST_RUN(a_noreturn_step_leaves_the_back_edge_out);
    TEST_RUN(a_check_inside_a_loop_puts_its_failure_block_after_every_normal_one);
    TEST_RUN(a_range_for_over_an_empty_collection_never_enters_its_body);
    TEST_RUN(a_range_for_over_a_single_element_array_compares_against_one);
    TEST_RUN(a_range_for_inside_a_range_for_keeps_two_counters);
    TEST_RUN(an_increment_step_uses_the_overflow_intrinsic_of_its_type);
    TEST_RUN(a_call_is_a_legal_init_and_step);
    TEST_RUN(break_and_continue_in_a_range_for_target_its_own_blocks);
    TEST_RUN(a_range_for_emits_no_bounds_check_of_its_own);
    TEST_RUN(the_no_bounds_check_mode_leaves_the_blocks_of_a_loop_alone);
    TEST_RUN(a_nested_if_inside_a_loop_rejoins_before_the_back_edge);
    TEST_RUN(a_loop_inside_a_loop_restores_the_enclosing_targets);
    TEST_RUN(every_block_of_a_control_flow_module_ends_in_exactly_one_terminator);
    TEST_RUN(the_terminator_scan_rejects_a_block_with_none_and_one_with_two);
    TEST_RUN(the_terminator_scan_counts_a_multi_line_switch_once);
    TEST_RUN(the_terminator_scan_refuses_to_pass_a_module_with_no_definition);
    TEST_RUN(control_flow_builds_no_phi_and_carries_no_value_across_a_merge);
    TEST_RUN(a_short_circuit_in_a_condition_keeps_its_own_blocks);
    TEST_RUN(two_runs_over_a_control_flow_program_produce_byte_identical_text);
    TEST_RUN(the_block_counter_is_reset_at_each_definition);
    TEST_RUN(a_control_flow_module_passes_the_verifier_in_release_mode_too);
    gen_done();
    TEST_EXIT();
}
