// Tests a comparison with two untyped constants when one folds to no value.
// A comparison yields `bool`, so no context reaches its operands: the checker fixes their
// default type where the comparison is checked, over both sides together. The rest of the constants
// are in check_const_test.c and the dropped operands in check_operand_test.c.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"
#include "consts.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// Returns the type of one comparison operand in the initializer of `name`.
// The result excludes the comparison's `bool` type.
static const char* operand_type(const char* name, bool left) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    const ast_node_t* cmp = d != NULL ? d->b : NULL;
    if (cmp == NULL || cmp->kind != AST_BINARY) {
        return "<not a comparison>";
    }
    const ast_node_t* operand = left ? cmp->a : cmp->b;
    return type_text(operand != NULL ? operand->type : NULL);
}

// The two constants beyond the default i64 range.
// Each diagnostic names i64, the widest default type.
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
static bool init_is_true(const char* name) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    if (d == NULL || d->b == NULL) {
        return false;
    }
    const cval_t v = check_node_value(&checker, d->b);
    return v.kind == CV_BOOL && v.mag == 1;
}

TEST(a_comparison_reports_the_constant_of_either_operand, {
    // The constant has no default type, so the diagnostic names i64.
    // Either operand position reports it.
    char body[256];
    for (uint64_t e = 0; e < sizeof ENDS / sizeof ENDS[0]; e++) {
        for (uint64_t i = 0; i < sizeof FORMS / sizeof FORMS[0]; i++) {
            TEST_UNUSED(snprintf(body, sizeof body, FORMS[i], ENDS[e]));
            TEST_ASSERT_FALSE(check_body(body));
            TEST_ASSERT_TRUE(said(MESSAGES[e]));
            // The count, because the harness of the language corpus groups
            // diagnostics by line and cannot see one that is added or
            // doubled. One constant is reported once.
            TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
        }
    }
})

TEST(a_comparison_reports_a_bad_constant_on_each_side, {
    // Both sides are retyped, so both are reported. A fix that retyped the left side alone leaves
    // this at one diagnostic, and the corpus cannot see the difference. This is because both stand
    // on one line.
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n"
                                 "    println((9223372036854775808 << n) > "
                                 "(18446744073709551615 << n));"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    TEST_ASSERT_TRUE(said("constant 18446744073709551615 does not fit i64"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)2);
})

TEST(a_comparison_takes_one_default_type_over_both_operands, {
    // One type covers the whole untyped expression. The strongest requirement from either side
    // selects that type.
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = 2147483648 > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i64");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i64");
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = (1 << n) > 2147483648;"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i64");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i64");
    // Neither side asks for more than i32, so the pair keeps i32.
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = 2 > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i32");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = (1 << n) > (2 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i32");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
    // The integer clause wins over the char clause.
    // The character becomes its i32 code point, so both operands match.
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = 'a' > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i32");
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
})

TEST(a_comparison_that_folds_and_one_with_a_typed_operand_are_unchanged, {
    // A folded comparison chooses no default operand type.
    // A typed operand instead supplies the other operand's context.
    TEST_ASSERT_TRUE(check_body("    bool b = 2147483648 > 1;\n    println(b);"));
    TEST_ASSERT_TRUE(init_is_true("b"));
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = n > (1 << n);"));
    TEST_ASSERT_EQ_STR(operand_type("b", false), "i32");
    // A constant that does not fit the typed operand is reported by the
    // conversion, once, exactly as it was.
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(n > 9223372036854775808);"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i32"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_unary_operand_leaves_the_pair_before_the_rule_sees_it, {
    // `check_unary` gives an operand with no value its default type at once and alone. The pair
    // rule never runs: an untyped pair is two untyped sides. So a constant that is legal in `(1 <<
    // n) > 2147483648` is an error one unary minus away from it. It is the ordinary context rule
    // and not a hole.
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(-(1 << n) > 2147483648);"));
    TEST_ASSERT_TRUE(said("constant 2147483648 does not fit i32"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(2147483648 > ~(1 << n));"));
    TEST_ASSERT_TRUE(said("constant 2147483648 does not fit i32"));
    // With no unary the same constant carries the pair to i64 and the program
    // is legal. `run/constants/014` prints the answer.
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    bool b = (1 << n) > 2147483648;"));
    TEST_ASSERT_EQ_STR(operand_type("b", true), "i64");
})

TEST(an_untyped_pair_that_is_no_comparison_still_reaches_its_context, {
    // The other half of the same branch, which is unchanged: an operator that is no comparison
    // leaves the pair untyped. The context walks both operands and reports there. The count is what
    // this asserts, because the fix must not add a second report on the way.
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println(9223372036854775808 + (1 << n));"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    println((1 << n) + 9223372036854775808);"));
    TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    // The right operand needs `i64`, so both operands and the addition use `i64`.
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
