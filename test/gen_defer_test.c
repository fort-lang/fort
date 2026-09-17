// Unit tests of the deferred statements the emitter expands
// (toolchain.md 6 item 10): a `defer` emits nothing where it stands and its
// statement is copied into every exit that leaves its block -- falling off
// the end, `return`, `break` and `continue` -- innermost block first and in
// reverse textual order within a block, with the returned value read before
// any of it runs.
// D7.8
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the literals are the test data: the
// fort programs and the IR text each one must produce.

// A program holding a deferred statement in every kind of block, used by the
// structural tests that answer about the whole module rather than about one
// snippet.
static const char BUSY_SOURCE[] = "fn note(i32 n) void {\n}\n"
                                  "fn work(i32 n) i32 {\n"
                                  "    defer note(0);\n"
                                  "    {\n"
                                  "        defer note(1);\n"
                                  "        if (n == 1) { return 1; }\n"
                                  "    }\n"
                                  "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                                  "        defer note(2);\n"
                                  "        if (i == 1) { continue; }\n"
                                  "        if (i == 2) { break; }\n"
                                  "        switch (i) {\n"
                                  "        case 0:\n"
                                  "            defer note(3);\n"
                                  "            break;\n"
                                  "        default:\n"
                                  "            defer note(4);\n"
                                  "        }\n"
                                  "    }\n"
                                  "    {\n"
                                  "        defer {\n"
                                  "            for (i32 mut k = 0; k < 2; k = k +% 1) {\n"
                                  "                defer note(6);\n"
                                  "                if (k == 1) { break; }\n"
                                  "                if (k == 0) { continue; }\n"
                                  "            }\n"
                                  "        }\n"
                                  "        if (n == 3) { return 4; }\n"
                                  "    }\n"
                                  "    while (n < 0) {\n"
                                  "        defer note(5);\n"
                                  "        return 2;\n"
                                  "    }\n"
                                  "    return 3;\n"
                                  "}\n"
                                  "fn main() i32 {\n"
                                  "    return work(1);\n"
                                  "}\n";

// ---- a defer emits nothing where it stands -----------------------------------------
// D7.8

TEST(a_defer_emits_nothing_where_it_stands, {
    TEST_ASSERT_TRUE(emit("fn note() void {\n}\n"
                          "fn f() void {\n    defer note();\n    note();\n}\n"
                          "fn main() i32 {\n    f();\n    return 0;\n}\n"));
    // The body holds the statement that stands below the `defer` first and
    // the deferred copy after it, so nothing was emitted at the `defer`
    // itself.
    // D7.8
    const char* want = "define dso_local void @\"main.f\"() #0 {\n"
                       "entry:\n"
                       "  call void @\"main.note\"()\n"
                       "  call void @\"main.note\"()\n"
                       "  ret void\n"
                       "}\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_deferred_statement_of_a_block_that_is_never_left_early_is_emitted_once, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn f() void {\n    defer note(1);\n}\n"
                          "fn main() i32 {\n    f();\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 1)"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the order: innermost block first, reverse within a block ----------------------

TEST(the_deferred_statements_of_one_block_run_in_reverse_textual_order, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn f() void {\n    defer note(1);\n    defer note(2);\n}\n"
                          "fn main() i32 {\n    f();\n    return 0;\n}\n"));
    const char* want = "  call void @\"main.note\"(i32 2)\n"
                       "  call void @\"main.note\"(i32 1)\n"
                       "  ret void\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_return_runs_the_innermost_block_first, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn f() i32 {\n"
                          "    defer note(1);\n"
                          "    defer note(2);\n"
                          "    {\n        defer note(3);\n        return 7;\n    }\n}\n"
                          "fn main() i32 {\n    return f();\n}\n"));
    // The inner block's statement, then the outer block's two in reverse
    // order, then the `ret`.
    // D7.8
    const char* want = "  call void @\"main.note\"(i32 3)\n"
                       "  call void @\"main.note\"(i32 2)\n"
                       "  call void @\"main.note\"(i32 1)\n"
                       "  ret i32 7\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_deferred_statement_of_an_inner_block_is_not_run_by_an_outer_exit, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn f() void {\n"
                          "    {\n        defer note(3);\n    }\n"
                          "    note(9);\n}\n"
                          "fn main() i32 {\n    f();\n    return 0;\n}\n"));
    // The inner block's statement runs where that block ends and nowhere
    // else: one copy, and it precedes the call below the block.
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 3)"), (uint64_t)1);
    TEST_ASSERT_TRUE(before("call void @\"main.note\"(i32 3)", "call void @\"main.note\"(i32 9)"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(each_exit_of_a_function_carries_its_own_copy, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn f(bool b) i32 {\n"
                          "    defer note(1);\n"
                          "    if (b) { return 2; }\n"
                          "    return 3;\n}\n"
                          "fn main() i32 {\n    return f(true);\n}\n"));
    // The set of deferred statements at an exit is static, so the code is
    // copied into each of the two `return`s.
    // D7.8
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 1)"), (uint64_t)2);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the returned value is read first ---------------------------------------------
// D7.8, D17.5

TEST(the_returned_value_is_read_before_the_deferred_code_runs, {
    TEST_ASSERT_TRUE(emit("fn f() i32 {\n"
                          "    i32 mut x = 5;\n"
                          "    defer x = 6;\n"
                          "    return x;\n}\n"
                          "fn main() i32 {\n    return f();\n}\n"));
    // The load of `x` stands above the deferred store, and the `ret` hands
    // out the value that load produced, so deferred code cannot change it.
    // D7.8
    const char* want = "  %t0 = load i32, ptr %x.0, align 4\n"
                       "  store i32 6, ptr %x.0, align 4\n"
                       "  ret i32 %t0\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_aggregate_result_is_fixed_in_a_temporary_before_the_deferred_code, {
    TEST_ASSERT_TRUE(emit("struct pair { i32 x; i32 y; }\n"
                          "fn note() void {\n}\n"
                          "fn f() pair {\n"
                          "    pair p = {1, 2};\n"
                          "    defer note();\n"
                          "    return p;\n}\n"
                          "fn main() i32 {\n    pair q = f();\n    return q.x;\n}\n"));
    // The `sret` block is the caller's and the caller may hold a pointer to
    // it as well, so the result lands in a temporary of the callee's own
    // first and is copied out after the deferred code has run: `return e`
    // evaluates `e` before deferred code runs (item 7).
    // D7.8, D8.2
    const char* want = "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, "
                       "ptr align 4 %p.0, i64 8, i1 false)\n"
                       "  call void @\"main.note\"()\n"
                       "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ret.sret, "
                       "ptr align 4 %tmp0, i64 8, i1 false)\n"
                       "  ret void\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_aggregate_result_of_a_function_with_no_defer_is_written_straight_out, {
    TEST_ASSERT_TRUE(emit("struct pair { i32 x; i32 y; }\n"
                          "fn f() pair {\n    pair p = {1, 2};\n    return p;\n}\n"
                          "fn main() i32 {\n    pair q = f();\n    return q.x;\n}\n"));
    // The temporary is the price of a deferred statement and of nothing else,
    // so a function with none writes the caller's block directly (item 7).
    const char* want = "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ret.sret, "
                       "ptr align 4 %p.0, i64 8, i1 false)\n"
                       "  ret void\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(absent("%tmp0"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_deferred_store_through_a_parameter_cannot_reach_the_result, {
    TEST_ASSERT_TRUE(emit("struct pair { i32 x; i32 y; }\n"
                          "fn copy_of(pair mut* p) pair {\n"
                          "    defer p->x = 9;\n    return *p;\n}\n"
                          "fn main() i32 {\n"
                          "    pair mut s = {1, 2};\n    s = copy_of(&s);\n"
                          "    return s.x;\n}\n"));
    // The caller passes one block as the destination and as the argument, so
    // the deferred store reaches the `sret` block; the copy out of the
    // callee's temporary happens after it and overwrites what it wrote, which
    // is what keeps the returned value the one `return` evaluated.
    // D7.8
    const char* call = "call void @\"main.copy_of\"(ptr sret(%struct.main.pair) %s.0, ptr %s.0)";
    TEST_ASSERT_EQ_STR(found(call), call);
    const char* want = "  store i32 9, ptr %t2, align 4\n"
                       "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ret.sret, "
                       "ptr align 4 %tmp0, i64 8, i1 false)\n"
                       "  ret void\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_bare_return_runs_the_deferred_code_before_its_terminator, {
    TEST_ASSERT_TRUE(emit("fn note() void {\n}\n"
                          "fn f(bool b) void {\n"
                          "    defer note();\n"
                          "    if (b) { return; }\n}\n"
                          "fn main() i32 {\n    f(true);\n    return 0;\n}\n"));
    const char* want = "  call void @\"main.note\"()\n"
                       "  ret void\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"()"), (uint64_t)2);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(returning_an_own_local_empties_it_before_the_deferred_del_reads_it, {
    TEST_ASSERT_TRUE(emit("fn make() i32 mut* own {\n"
                          "    i32 mut* own p = new(i32);\n"
                          "    defer del(p);\n"
                          "    return p;\n}\n"
                          "fn main() i32 {\n"
                          "    i32 mut* own r = make();\n    del(r);\n    return 0;\n}\n"));
    // The implicit move of `return p` reads the pointer, stores null over the
    // local and only then runs `defer del(p)`, which therefore frees nothing
    // on the returning path while the caller keeps the block.
    // D7.8, D17.5
    const char* want = "  %t3 = load ptr, ptr %p.0, align 8\n"
                       "  store ptr null, ptr %p.0, align 8\n"
                       "  %t4 = load ptr, ptr %p.0, align 8\n"
                       "  call void @\"std.rt.free\"(ptr %t4)\n"
                       "  store ptr null, ptr %p.0, align 8\n"
                       "  ret ptr %t3\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- break and continue -----------------------------------------------------------
// D7.5, D7.6, D7.8

TEST(a_break_runs_the_deferred_code_of_the_loop_body_before_its_branch, {
    TEST_ASSERT_TRUE(emit("fn note() void {\n}\n"
                          "fn main() i32 {\n"
                          "    while (true) {\n        defer note();\n        break;\n    }\n"
                          "    return 0;\n}\n"));
    const char* want = "  call void @\"main.note\"()\n"
                       "  br label %L2\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_continue_in_a_for_runs_the_deferred_code_before_the_step_block, {
    TEST_ASSERT_TRUE(emit("fn note() void {\n}\n"
                          "fn main() i32 {\n"
                          "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                          "        defer note();\n        continue;\n    }\n"
                          "    return 0;\n}\n"));
    // `continue` branches to the step block, so the deferred statement stands
    // before that branch and the step runs after it.
    // D7.5, D7.8
    const char* want = "  call void @\"main.note\"()\n"
                       "  br label %L2\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_loop_body_with_three_exits_carries_three_copies, {
    TEST_ASSERT_TRUE(emit("fn note() void {\n}\n"
                          "fn main() i32 {\n"
                          "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                          "        defer note();\n"
                          "        if (i == 1) { continue; }\n"
                          "        if (i == 2) { break; }\n    }\n"
                          "    return 0;\n}\n"));
    // The `continue`, the `break` and the end of the body each leave the body
    // block, so each carries a copy.
    // D7.8
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"()"), (uint64_t)3);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_break_leaves_only_the_blocks_between_it_and_its_loop, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    defer note(1);\n"
                          "    while (true) {\n"
                          "        defer note(2);\n"
                          "        { defer note(3); break; }\n    }\n"
                          "    return 0;\n}\n"));
    // The inner block and the loop body are left, the function block is not:
    // the exit stops at the loop.
    // D7.8
    const char* want = "  call void @\"main.note\"(i32 3)\n"
                       "  call void @\"main.note\"(i32 2)\n"
                       "  br label %L2\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 1)"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_return_out_of_a_loop_leaves_the_loop_body_and_the_function_block, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    defer note(1);\n"
                          "    while (true) {\n"
                          "        defer note(2);\n        return 0;\n    }\n}\n"));
    const char* want = "  call void @\"main.note\"(i32 2)\n"
                       "  call void @\"main.note\"(i32 1)\n"
                       "  ret i32 0\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_break_in_a_switch_in_a_loop_leaves_the_case_body_alone, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    for (i32 mut i = 0; i < 2; i = i +% 1) {\n"
                          "        defer note(2);\n"
                          "        switch (i) {\n"
                          "        case 0:\n            defer note(3);\n            break;\n"
                          "        default:\n            note(4);\n        }\n    }\n"
                          "    return 0;\n}\n"));
    // `break` inside a `switch` exits the switch, so the case body's
    // statement runs and the loop body's does not.
    // D7.6, D7.8
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 3)"), (uint64_t)1);
    TEST_ASSERT_TRUE(before("call void @\"main.note\"(i32 3)", "call void @\"main.note\"(i32 2)"));
    // The loop body has one exit of its own, the end of the iteration.
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 2)"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_continue_in_a_case_body_leaves_the_case_body_and_the_loop_body, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    for (i32 mut i = 0; i < 2; i = i +% 1) {\n"
                          "        defer note(2);\n"
                          "        switch (i) {\n"
                          "        case 0:\n            defer note(3);\n            continue;\n"
                          "        default:\n            note(4);\n        }\n    }\n"
                          "    return 0;\n}\n"));
    // `continue` passes through the case body and stops at the loop body, so
    // both statements run, innermost first.
    // D7.6, D7.8
    const char* want = "  call void @\"main.note\"(i32 3)\n"
                       "  call void @\"main.note\"(i32 2)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_return_in_a_case_body_unwinds_through_it_to_the_function_block, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn f(i32 n) i32 {\n"
                          "    defer note(1);\n"
                          "    while (true) {\n"
                          "        defer note(2);\n"
                          "        switch (n) {\n"
                          "        case 0:\n            defer note(3);\n            return 5;\n"
                          "        default:\n            return 6;\n        }\n    }\n}\n"
                          "fn main() i32 {\n    return f(0);\n}\n"));
    // `return` stops at the function body alone, so it passes through the
    // case body and the loop body it leaves on the way.
    // D7.6, D7.8
    const char* want = "  call void @\"main.note\"(i32 3)\n"
                       "  call void @\"main.note\"(i32 2)\n"
                       "  call void @\"main.note\"(i32 1)\n"
                       "  ret i32 5\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    // The `default` clause leaves the same two blocks, its own holding
    // nothing deferred.
    const char* other = "  call void @\"main.note\"(i32 2)\n"
                        "  call void @\"main.note\"(i32 1)\n"
                        "  ret i32 6\n";
    TEST_ASSERT_EQ_STR(found(other), other);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_case_body_that_falls_off_its_end_runs_its_deferred_code_there, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    switch (1) {\n"
                          "    case 1:\n        defer note(3);\n        note(9);\n"
                          "    default:\n        note(4);\n    }\n"
                          "    return 0;\n}\n"));
    // A case body is a block with an implicit `break` at its end, so the
    // deferred statement runs there and once only.
    // D7.6
    const char* want = "  call void @\"main.note\"(i32 9)\n"
                       "  call void @\"main.note\"(i32 3)\n"
                       "  br label %L2\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 3)"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- what runs nothing ------------------------------------------------------------
// D7.8, D8.5, D11.4

// ---- a loop inside deferred code ----------------------------------------------------
// D7.8

TEST(a_break_of_a_loop_inside_deferred_code_branches_to_that_loop, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    while (true) {\n"
                          "        defer {\n"
                          "            while (true) {\n                note(2);\n"
                          "                break;\n            }\n"
                          "            note(3);\n        }\n"
                          "        break;\n    }\n"
                          "    return 0;\n}\n"));
    // L2 is the enclosing loop's exit and L5 the exit of the loop inside the
    // deferred code. The `break` of that inner loop branches to L5, so what the
    // deferred code writes after it still runs and the enclosing loop is left
    // once, at the end of the expansion. A branch to L2 here would put
    // `note(3)` after a terminator, which `verified()` reports and `opt` alone
    // does not (notes/compiler.md 6).
    // D7.8
    const char* want = "L4:\n"
                       "  call void @\"main.note\"(i32 2)\n"
                       "  br label %L5\n"
                       "\n"
                       "L5:\n"
                       "  call void @\"main.note\"(i32 3)\n"
                       "  br label %L2\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_loop_inside_deferred_code_is_expanded_at_every_exit, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    for (i32 mut i = 0; i < 3; i = i +% 1) {\n"
                          "        defer {\n"
                          "            for (i32 mut k = 0; k < 2; k = k +% 1) {\n"
                          "                defer note(7);\n"
                          "                if (k == 1) { break; }\n            }\n"
                          "        }\n"
                          "        if (i == 1) { continue; }\n"
                          "        if (i == 2) { break; }\n    }\n"
                          "    return 0;\n}\n"));
    // Three exits leave the body block, and each carries the whole deferred
    // block: the inner loop and the deferred statement of its own body, which
    // the inner loop leaves at two exits of its own.
    // D7.8
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 7)"), (uint64_t)6);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_break_in_a_switch_inside_deferred_code_leaves_that_case_body, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn main() i32 {\n"
                          "    i32 x = 1;\n"
                          "    while (true) {\n"
                          "        defer {\n"
                          "            switch (x) {\n"
                          "            default:\n                defer note(8);\n"
                          "                break;\n            }\n"
                          "            note(9);\n        }\n"
                          "        break;\n    }\n"
                          "    return 0;\n}\n"));
    // The `break` leaves the case body of the `switch` written inside the
    // deferred code, runs that case body's own deferred statement, and the
    // deferred code goes on at `note(9)`.
    // D7.6, D7.8
    const char* want = "  call void @\"main.note\"(i32 8)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 8)"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 9)"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_noreturn_call_runs_no_deferred_code_at_all, {
    TEST_ASSERT_TRUE(emit("fn stop() noreturn {\n    panic(\"x\");\n}\n"
                          "fn f() void {\n    defer note();\n    stop();\n}\n"
                          "fn note() void {\n}\n"
                          "fn main() i32 {\n    f();\n    return 0;\n}\n"));
    // A `noreturn` call never exits the block, so nothing deferred runs: the
    // block ends at the call and its trap.
    // D7.8, D8.5
    const char* want = "define dso_local void @\"main.f\"() #0 {\n"
                       "entry:\n"
                       "  call void @\"main.stop\"()\n"
                       "  call void @llvm.trap()\n"
                       "  unreachable\n"
                       "}\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_deferred_noreturn_call_stops_the_expansion_at_itself, {
    TEST_ASSERT_TRUE(emit("fn stop() noreturn {\n    panic(\"x\");\n}\n"
                          "fn note() void {\n}\n"
                          "fn f() void {\n"
                          "    defer note();\n    defer stop();\n}\n"
                          "fn main() i32 {\n    f();\n    return 0;\n}\n"));
    // The deferred `stop()` ends the block, so the statement below it in the
    // expansion is not reached and the block still holds one terminator (item
    // 10).
    // D7.8, D8.5
    const char* want = "define dso_local void @\"main.f\"() #0 {\n"
                       "entry:\n"
                       "  call void @\"main.stop\"()\n"
                       "  call void @llvm.trap()\n"
                       "  unreachable\n"
                       "}\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(absent("call void @\"main.note\"()"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_runtime_check_branches_to_its_failure_block_without_deferred_code, {
    TEST_ASSERT_TRUE(emit("fn note() void {\n}\n"
                          "fn main() i32 {\n"
                          "    i32[3] a = {1, 2, 3};\n    i32 mut i = 5;\n"
                          "    defer note();\n    return a[i];\n}\n"));
    // A runtime error aborts without running deferred code, so the failure
    // block holds the report and the trap alone.
    // D7.8, D11.4
    const char* want = "  call void @\"std.rt.fail_bounds\"(";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"()"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the scope stack is per function ----------------------------------------------
// D19.5

TEST(the_deferred_statements_of_one_function_do_not_reach_the_next, {
    TEST_ASSERT_TRUE(emit("fn note(i32 n) void {\n}\n"
                          "fn f() void {\n    defer note(1);\n}\n"
                          "fn g() void {\n    note(2);\n}\n"
                          "fn main() i32 {\n    f();\n    g();\n    return 0;\n}\n"));
    const char* want = "define dso_local void @\"main.g\"() #0 {\n"
                       "entry:\n"
                       "  call void @\"main.note\"(i32 2)\n"
                       "  ret void\n"
                       "}\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @\"main.note\"(i32 1)"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the module as a whole --------------------------------------------------------

TEST(every_block_of_a_defer_heavy_module_ends_in_exactly_one_terminator, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    // Code duplicated at several exits is exactly the shape that produces a
    // block with two terminators or an instruction after one (item 10).
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_defer_heavy_module_verifies_in_release_mode_too, {
    TEST_ASSERT_TRUE(emit_release(BUSY_SOURCE));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_runs_over_a_defer_heavy_program_produce_byte_identical_text, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    sb_t first;
    sb_init(&first);
    sb_append(&first, ir());
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    const char* again = ir();
    TEST_ASSERT_EQ_STR(again, sb_cstr(&first));
    TEST_ASSERT_EQ_STR(verified(), "verified");
    sb_free(&first);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_defer", argc, argv);
    TEST_RUN(a_defer_emits_nothing_where_it_stands);
    TEST_RUN(a_deferred_statement_of_a_block_that_is_never_left_early_is_emitted_once);
    TEST_RUN(the_deferred_statements_of_one_block_run_in_reverse_textual_order);
    TEST_RUN(a_return_runs_the_innermost_block_first);
    TEST_RUN(a_deferred_statement_of_an_inner_block_is_not_run_by_an_outer_exit);
    TEST_RUN(each_exit_of_a_function_carries_its_own_copy);
    TEST_RUN(the_returned_value_is_read_before_the_deferred_code_runs);
    TEST_RUN(an_aggregate_result_is_fixed_in_a_temporary_before_the_deferred_code);
    TEST_RUN(an_aggregate_result_of_a_function_with_no_defer_is_written_straight_out);
    TEST_RUN(a_deferred_store_through_a_parameter_cannot_reach_the_result);
    TEST_RUN(a_bare_return_runs_the_deferred_code_before_its_terminator);
    TEST_RUN(returning_an_own_local_empties_it_before_the_deferred_del_reads_it);
    TEST_RUN(a_break_runs_the_deferred_code_of_the_loop_body_before_its_branch);
    TEST_RUN(a_continue_in_a_for_runs_the_deferred_code_before_the_step_block);
    TEST_RUN(a_loop_body_with_three_exits_carries_three_copies);
    TEST_RUN(a_break_leaves_only_the_blocks_between_it_and_its_loop);
    TEST_RUN(a_return_out_of_a_loop_leaves_the_loop_body_and_the_function_block);
    TEST_RUN(a_break_in_a_switch_in_a_loop_leaves_the_case_body_alone);
    TEST_RUN(a_continue_in_a_case_body_leaves_the_case_body_and_the_loop_body);
    TEST_RUN(a_case_body_that_falls_off_its_end_runs_its_deferred_code_there);
    TEST_RUN(a_return_in_a_case_body_unwinds_through_it_to_the_function_block);
    TEST_RUN(a_break_of_a_loop_inside_deferred_code_branches_to_that_loop);
    TEST_RUN(a_loop_inside_deferred_code_is_expanded_at_every_exit);
    TEST_RUN(a_break_in_a_switch_inside_deferred_code_leaves_that_case_body);
    TEST_RUN(a_noreturn_call_runs_no_deferred_code_at_all);
    TEST_RUN(a_deferred_noreturn_call_stops_the_expansion_at_itself);
    TEST_RUN(a_runtime_check_branches_to_its_failure_block_without_deferred_code);
    TEST_RUN(the_deferred_statements_of_one_function_do_not_reach_the_next);
    TEST_RUN(every_block_of_a_defer_heavy_module_ends_in_exactly_one_terminator);
    TEST_RUN(a_defer_heavy_module_verifies_in_release_mode_too);
    TEST_RUN(two_runs_over_a_defer_heavy_program_produce_byte_identical_text);
    gen_done();
    TEST_EXIT();
}
