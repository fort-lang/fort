// Tests that the error type silences later diagnostics.
// They reject messages that expose the internal `<error>` name.
// Other constant tests are in check_const_test.c, check_operand_test.c, and
// check_untyped_pair_test.c.
//
// The count is the instrument here, and it is the only one. The language harness groups diagnostics
// by `(file, line)`. It cannot see a diagnostic removed from a line that keeps another. The stderr
// directive asserts the presence of a substring and has no negative form, so it cannot see a
// removal either. Every test below asserts diag_lines().
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "check.h"
#include "common/check_helpers.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// The 8 contexts that fix the type of an untyped constant. Each must report only the error that
// poisons the type.
static const char* const CONTEXTS[] = {
    "    println(C);",
    "    println(C + 1);",
    "    println(cast(C, i32));",
    "    if (C) {\n        println(1);\n    }",
    "    while (C) {\n        println(1);\n    }",
    "    switch (C) {\n        case 1: {\n            println(1);\n        }\n    }",
    "    println(new(i32, C));",
    "    println(1, C);",
};

// The 13 no-context positions share the table in `check_helpers.h`.
// `check_operand_test.c` checks their messages. This suite checks their counts.

TEST(a_context_that_meets_a_poisoned_constant_says_nothing_more, {
    // `2^63` has no default type, so the checker reports it and gives the
    // expression the error type. The context then retypes the constant
    // against that error type, which is the second visit, and the error type
    // silences it.
    for (uint64_t i = 0; i < sizeof CONTEXTS / sizeof CONTEXTS[0]; i++) {
        const char* body = site_body(CONTEXTS[i], "9223372036854775808");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("constant expression out of range"));
        TEST_ASSERT_FALSE(said("not a constant"));
        TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    }
})

TEST(every_position_that_drops_its_operand_reports_once, {
    // Counts the 13 positions from check_operand_test.c.
    // `move` keeps two diagnostics because its early return leaves a `void` result.
    for (uint64_t i = 0; i < (uint64_t)DROP_SITE_COUNT; i++) {
        const char* body = drop_site(i, "9223372036854775808");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("constant expression out of range"));
        TEST_ASSERT_FALSE(said("not a constant"));
        TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)(i == DROP_SITE_MOVE ? 2 : 1));
    }
})

TEST(the_other_end_of_the_range_is_silenced_as_well, {
    // `2^64 - 1` also has no default integer type and takes the same path.
    for (uint64_t i = 0; i < sizeof CONTEXTS / sizeof CONTEXTS[0]; i++) {
        const char* body = site_body(CONTEXTS[i], "18446744073709551615");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_FALSE(said("not a constant"));
        TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    }
})

TEST(a_context_with_a_real_type_still_names_itself, {
    // A valid context still reports its expected type.
    // `fail/operators/018` annotates these two messages.
    // `fail/arrays/009` annotates a third, which bootstrap-0 never reaches because it
    // refuses a second array level.
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    i32* p = 1 << n;\n    println(p);"));
    TEST_ASSERT_TRUE(said("the initializer expects i32*, not a constant"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("struct s {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    i32 n = 1;\n    s q = 1 << n;\n"
                                "    println(q.x);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("the initializer expects s, not a constant"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_poisoned_context_of_its_own_stays_silent, {
    // The type comes from a declaration that failed rather than from the
    // constant, so the constant meets the error type with nothing wrong with
    // it. One diagnostic, the one that named the unknown type.
    TEST_ASSERT_FALSE(check_body("    nosuch v = 1;\n    println(v);"));
    TEST_ASSERT_TRUE(said("unknown type 'nosuch'"));
    TEST_ASSERT_FALSE(said("not a constant"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

// A group of a bare `void` is a complete return type and nothing else. `void` needs its `*` inside
// the same group, so `(void)*` is an error and `(void*)` is the opaque pointer (D3.11). The error
// is reported once, at the innermost group that holds the `void`.
TEST(a_group_of_a_bare_void_is_only_a_return_type, {
    TEST_ASSERT_TRUE(check_src("fn f() (void) { }\n"
                               "fn g() ((void)) { }\n"
                               "fn main() i32 {\n"
                               "    fn () (void) h = f;\n"
                               "    h();\n"
                               "    g();\n"
                               "    (void*) p = null;\n"
                               "    (void mut*) q = null;\n"
                               "    ((void*)) mut r = p;\n"
                               "    r = cast(q, (void*));\n"
                               "    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_body("(void)* p = null;"));
    TEST_ASSERT_NONNULL(
        strstr(diags(), "main.ft:2:2: error: 'void' is only a return type or the base of 'void*'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("((void))* p = null;"));
    TEST_ASSERT_NONNULL(strstr(diags(), "main.ft:2:3: error: 'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("(void mut)* p = null;"));
    TEST_ASSERT_NONNULL(strstr(diags(), "main.ft:2:2: error: 'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("u64 n = sizeof((void)*);"));
    TEST_ASSERT_TRUE(said("'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("void mut* q = null;\n    void* p = cast(q, (void)*);"));
    TEST_ASSERT_TRUE(said("'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("fn f() (void)* {\n    panic(\"x\");\n}\nfn main() i32 {\n"
                                "    return 0;\n}\n"));
    TEST_ASSERT_NONNULL(strstr(diags(), "main.ft:1:9: error: 'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("fn f() (void) mut { }\nfn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_NONNULL(strstr(diags(), "main.ft:1:8: error: 'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("fn f((void)* p) void { }\nfn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_NONNULL(strstr(diags(), "main.ft:1:7: error: 'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src("struct s { (void)* f; }\nfn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_NONNULL(strstr(diags(), "main.ft:1:13: error: 'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("(void) v = {};"));
    TEST_ASSERT_TRUE(said("'void' is only a return type"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

// The return operands whose diagnostic needs the return type. Under a failed
// return type each one reports nothing more (D14.2).
static const char* const RETURNS[] = {
    "fn f() nosuch* {\n    return null;\n}\n",
    "fn f() nosuch {\n    return g();\n}\n",
    "fn f() nosuch* own {\n    return new(i32);\n}\n",
    "fn f() nosuch {\n    return 9223372036854775808;\n}\n",
};

TEST(a_return_under_a_failed_return_type_says_nothing_more, {
    for (uint64_t i = 0; i < sizeof RETURNS / sizeof RETURNS[0]; i++) {
        sb_t src;
        sb_init(&src);
        sb_append(&src, "fn g() void {\n}\n");
        sb_append(&src, RETURNS[i]);
        sb_append(&src, "fn main() i32 {\n    return 0;\n}\n");
        TEST_ASSERT_FALSE(check_src(sb_cstr(&src)));
        TEST_ASSERT_TRUE(said("unknown type 'nosuch'"));
        TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
        sb_free(&src);
    }
})

TEST(a_return_operand_under_a_failed_return_type_reports_its_own_errors, {
    // The error type silences the context and not the operand: an unknown
    // name in the operand is an error of its own.
    TEST_ASSERT_FALSE(check_src("fn f() nosuch* {\n    return missing;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("unknown type 'nosuch'"));
    TEST_ASSERT_TRUE(said("unknown name 'missing'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)2);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_poison", argc, argv);
    TEST_RUN(a_group_of_a_bare_void_is_only_a_return_type);
    TEST_RUN(a_context_that_meets_a_poisoned_constant_says_nothing_more);
    TEST_RUN(every_position_that_drops_its_operand_reports_once);
    TEST_RUN(the_other_end_of_the_range_is_silenced_as_well);
    TEST_RUN(a_context_with_a_real_type_still_names_itself);
    TEST_RUN(a_poisoned_context_of_its_own_stays_silent);
    TEST_RUN(a_return_under_a_failed_return_type_says_nothing_more);
    TEST_RUN(a_return_operand_under_a_failed_return_type_reports_its_own_errors);
    check_reset();
    done();
    TEST_EXIT();
}
