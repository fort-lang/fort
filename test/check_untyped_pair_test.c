// Unit tests of a comparison whose two operands are untyped constants and one
// of which folded to no value (core-language.md 5.3). A comparison yields
// `bool`, so no context reaches its operands: the checker fixes their default
// type where the comparison is checked, over both sides together. Until
// 2026-09-14 it took the left operand's type for the pair, which dropped the
// other operand's poison and its constant. The rest of the constants are in
// check_const_test.c and the dropped operands in check_operand_test.c.
// D4.1, D4.5, D6.2
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"
#include "consts.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// The spelling of the type the checker gave one operand of the comparison that
// initializes the declaration `name`. The operands are what the rule types, so
// the test reads them and not the `bool` above them.
static const char* operand_type(const char* name, bool left) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    const ast_node_t* cmp = d != NULL ? d->b : NULL;
    if (cmp == NULL || cmp->kind != AST_BINARY) {
        return "<not a comparison>";
    }
    const ast_node_t* operand = left ? cmp->a : cmp->b;
    return type_text(operand != NULL ? operand->type : NULL);
}

// The two ends of the range the rule has no type for, `[2^63, 2^64 - 1]`, and
// the message each draws against i64, the widest type the rule offers.
// D4.5
static const char* const ENDS[] = {"9223372036854775808", "18446744073709551615"};
static const char* const MESSAGES[] = {"constant 9223372036854775808 does not fit i64",
                                       "constant 18446744073709551615 does not fit i64"};

// The four comparisons, two orderings and two equalities, with the constant on
// each side of the operator. `%s` takes one of `ENDS`.
static const char* const FORMS[] = {
    "    i32 n = 1;\n    println(%s > (1 << n));",
    "    i32 n = 1;\n    println((1 << n) > %s);",
    "    i32 n = 1;\n    println(%s == (1 << n));",
    "    i32 n = 1;\n    println((1 << n) != %s);",
};

// True when the initializer of `name` folded to the constant `true`. A
// comparison of two constants is a constant expression, and the value is a
// bool, which `init_int` of check_helpers.h cannot read.
// D4.6
static bool init_is_true(const char* name) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    if (d == NULL || d->b == NULL) {
        return false;
    }
    const cval_t v = check_node_value(&checker, d->b);
    return v.kind == CV_BOOL && v.mag == 1;
}

TEST(a_comparison_reports_the_constant_of_either_operand, {
    // The constant has no default type, so the rule's own tail reports it
    // against i64. It is reported on whichever side it stands: the pair took
    // the left operand's type before 2026-09-14, so the right side printed a
    // wrong answer instead, and the left side compiled two widths into one
    // comparison.
    // D4.5, D6.2
    char body[256];
    for (uint64_t e = 0; e < sizeof ENDS / sizeof ENDS[0]; e++) {
        for (uint64_t i = 0; i < sizeof FORMS / sizeof FORMS[0]; i++) {
            TEST_UNUSED(snprintf(body, sizeof body, FORMS[i], ENDS[e]));
            TEST_ASSERT_FALSE(check_body(body));
            TEST_ASSERT_TRUE(said(MESSAGES[e]));
            // The count, because the harness of the language corpus groups
            // diagnostics by line and cannot see one that is added or
            // doubled. One constant is reported once.
            // D14.2
            TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
        }
    }
})

TEST(a_comparison_reports_a_bad_constant_on_each_side, {
    // Both sides are retyped, so both are reported. A fix that retyped the
    // left side alone leaves this at one diagnostic, and the corpus cannot
    // see the difference, because both stand on one line.
    // D4.5
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n"
                                 "    println((9223372036854775808 << n) > "
                                 "(18446744073709551615 << n));"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    TEST_ASSERT_TRUE(said("constant 18446744073709551615 does not fit i64"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)2);
})

TEST(a_comparison_takes_one_default_type_over_both_operands, {
    // One type covers the whole untyped expression, and the clause that wins
    // is the strongest either side asks for. The left operand alone decided
    // it before 2026-09-14: `2147483648 > (1 << n)` gave the constant i64 and
    // the shift i32, which clang refused as two widths in one comparison.
    // D4.5, D6.2
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = 2147483648 > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i64");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i64");
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = (1 << n) > 2147483648;"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i64");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i64");
    // Neither side asks for more than i32, so the pair keeps i32.
    // D4.5
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = 2 > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i32");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = (1 << n) > (2 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i32");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
    // The integer clause wins over the char clause, so the char literal is its
    // code point and both operands are i32. It was i8 beside i32 before.
    // D4.3, D4.5
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = 'a' > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i32");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
})

TEST(a_comparison_that_folds_and_one_with_a_typed_operand_are_unchanged, {
    // The two neighbours of the rule, so that a change to it is seen here
    // first. Both operands carry a value, so the comparison folds and no
    // default type is chosen for it; and a typed operand is a context, which
    // the untyped one takes its type from.
    // D4.1, D4.4
    TEST_ASSERT_TRUE(check_body("    bool b = 2147483648 > 1;\n    println(b);"));
    TEST_ASSERT_TRUE(init_is_true("b"));
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = n > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
    // A constant that does not fit the typed operand is reported by the
    // conversion, once, exactly as it was.
    // D4.1
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(n > 9223372036854775808);"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i32"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_unary_operand_leaves_the_pair_before_the_rule_sees_it, {
    // `check_unary` gives an operand with no value its default type at once and
    // alone, so `-(1 << n)` is a **typed** i32 and the constant beside it takes
    // i32 from a typed operand, as it takes a type from any typed operand. The
    // pair rule never runs: an untyped pair is two untyped sides. So a constant
    // that is legal in `(1 << n) > 2147483648` is an error one unary minus away
    // from it, which is the ordinary context rule and not a hole.
    // D4.1, D4.5
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(-(1 << n) > 2147483648);"));
    TEST_ASSERT_TRUE(said("constant 2147483648 does not fit i32"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(2147483648 > ~(1 << n));"));
    TEST_ASSERT_TRUE(said("constant 2147483648 does not fit i32"));
    // With no unary the same constant carries the pair to i64 and the program
    // is legal. run/constants/014 prints the answer.
    // D4.5
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = (1 << n) > 2147483648;"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i64");
})

TEST(an_untyped_pair_that_is_no_comparison_still_reaches_its_context, {
    // The other half of the same branch, which is unchanged: an operator that
    // is no comparison leaves the pair untyped, so the context walks both
    // operands and reports there. The count is what this asserts, because the
    // fix must not add a second report on the way.
    // D4.1, D4.5
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(9223372036854775808 + (1 << n));"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println((1 << n) + 9223372036854775808);"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    // A sibling that needs i64 carries the whole expression to i64, which is
    // the same rule reached through the context.
    // D4.5, D6.2
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    i64 w = (1 << n) + 4294967296;"));
    TEST_ASSERT_EQ_STR(init_type("w"), "i64");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_untyped_pair", argc, argv);
    TEST_RUN(a_comparison_reports_the_constant_of_either_operand);
    TEST_RUN(a_comparison_reports_a_bad_constant_on_each_side);
    TEST_RUN(a_comparison_takes_one_default_type_over_both_operands);
    TEST_RUN(a_comparison_that_folds_and_one_with_a_typed_operand_are_unchanged);
    TEST_RUN(a_unary_operand_leaves_the_pair_before_the_rule_sees_it);
    TEST_RUN(an_untyped_pair_that_is_no_comparison_still_reaches_its_context);
    check_reset();
    done();
    TEST_EXIT();
}
