// Unit tests of the rule that the error type silences every later diagnostic
// involving it, measured on the one message that disobeyed it: "the
// expression expects <error>, not a constant". `constant_fits` drew
// it whenever a constant with no default type met a context, after the
// diagnostic that poisoned the type had already reported, and it showed the
// reader the internal name `<error>`. The rest of the constants are in
// check_const_test.c, the dropped operands in check_operand_test.c and the
// untyped pairs in check_untyped_pair_test.c.
//
// The count is the instrument here, and it is the only one. The language
// harness groups diagnostics by `(file, line)`, so it cannot see a diagnostic
// removed from a line that keeps another; the stderr directive asserts the
// presence of a substring and has no negative form, so it cannot see a removal
// either (notes/testing.md 3). Every test below asserts diag_lines().
// D4.5, D14.2
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "check.h"
#include "check_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// The eight contexts that fix the type of an untyped constant. Each drew two
// diagnostics until 2026-09-14: "constant expression out of range" from the
// rule of the default type, and the noise this suite holds down.
// D4.1, D4.5
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

// The thirteen positions that are no context at all reach the same rule
// through check_operand. Their table is in check_helpers.h, which
// check_operand_test.c reads as well: that suite holds what each of them
// reports and this one holds how many.
// D4.1, D4.5

TEST(a_context_that_meets_a_poisoned_constant_says_nothing_more, {
    // `2^63` has no default type, so the rule reports it and gives the
    // expression the error type. The context then retypes the constant
    // against that error type, which is the second visit, and the error type
    // silences it.
    // D4.5, D14.2
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
    // The thirteen of check_operand_test.c, counted. Each of them reached the
    // same rule and drew the same second diagnostic; `move` keeps two,
    // because its early return leaves the result `void`, and the rule of an
    // expression with no value answers that.
    // D4.1, D12.2, D14.2
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
    // `2^64 - 1` is the other constant the rule of the default type has no
    // type for, and it takes the same path.
    // D4.5, D14.2
    for (uint64_t i = 0; i < sizeof CONTEXTS / sizeof CONTEXTS[0]; i++) {
        const char* body = site_body(CONTEXTS[i], "18446744073709551615");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_FALSE(said("not a constant"));
        TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    }
})

TEST(a_context_with_a_real_type_still_names_itself, {
    // The other half, and the reason the guard reads the type and not the
    // message: a context whose type is no error still tells the reader what it
    // expected. fail/operators/018 annotates these two messages, and
    // fail/arrays/009 annotates a third, which stage1 never reaches because it
    // refuses a second array level.
    // D4.1, D4.2
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
    // D14.2
    TEST_ASSERT_FALSE(check_body("    nosuch v = 1;\n    println(v);"));
    TEST_ASSERT_TRUE(said("unknown type 'nosuch'"));
    TEST_ASSERT_FALSE(said("not a constant"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_poison", argc, argv);
    TEST_RUN(a_context_that_meets_a_poisoned_constant_says_nothing_more);
    TEST_RUN(every_position_that_drops_its_operand_reports_once);
    TEST_RUN(the_other_end_of_the_range_is_silenced_as_well);
    TEST_RUN(a_context_with_a_real_type_still_names_itself);
    TEST_RUN(a_poisoned_context_of_its_own_stays_silent);
    check_reset();
    done();
    TEST_EXIT();
}
