// Unit tests of the operand of an operator that gives it no context and drops
// it when it is poisoned (core-language.md 5.3): `*e`, `e.f`, `e[i]`, `e()`,
// `&e`, `del(e)`, `move(e)`, an assignment target and a range `for`
// collection. Each of those accepted a constant with no default type in
// silence until 2026-09-14. The rest of the constants are in
// check_const_test.c and the operators in check_expr_test.c.
// D4.1, D4.5, D14.2
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// Every operator that drops a poisoned operand, which is every position that
// is no context at all. A constant with no default type is poisoned and
// unreported, so each of these accepted the program in silence until
// 2026-09-14: the compiler exited 0 and the program printed nothing.
// D4.1, D4.5

// The thirteen positions, each written once. The body goes inside `main`.
static const char* const DROP_SITES[] = {
    "    println(*C);",
    "    println((C).x);",
    "    println((C)->x);",
    "    println((C).len);",
    "    println(C[0]);",
    "    println((C)[0 .. 1]);",
    "    println(C());",
    "    println(&C);",
    "    del(C);",
    "    println(move(C));",
    "    C = 1;",
    "    C++;",
    "    for (i32 x : C) {\n        println(x);\n    }",
};

// `DROP_SITES[i]` with `C` replaced by `constant`. It returns NULL rather than
// a shorter program when the buffer would overflow, because a template that
// silently loses its tail is a test that silently stops testing: `check_body`
// of half a statement is a syntax error, which is a `false` the assertions
// below would read as a pass.
static const char* drop_site(uint64_t i, const char* constant) {
    static char body[256];
    uint64_t w = 0;
    for (uint64_t r = 0; DROP_SITES[i][r] != '\0'; r++) {
        const char* piece = DROP_SITES[i][r] == 'C' ? constant : NULL;
        if (piece == NULL) {
            if (w + 1 >= sizeof body) {
                return NULL;
            }
            body[w++] = DROP_SITES[i][r];
            continue;
        }
        for (uint64_t k = 0; piece[k] != '\0'; k++) {
            if (w + 1 >= sizeof body) {
                return NULL;
            }
            body[w++] = piece[k];
        }
    }
    body[w] = '\0';
    return body;
}

TEST(every_operator_that_drops_its_operand_reports_a_constant_with_no_type, {
    // `2^63` written out folds to a value that neither i32 nor i64 holds, so
    // the rule's own tail reports it. The operand reaches the rule because
    // none of these positions is a context; without that it is dropped with
    // the error type and nothing is ever said.
    // D4.1, D4.5
    for (uint64_t i = 0; i < sizeof DROP_SITES / sizeof DROP_SITES[0]; i++) {
        const char* body = drop_site(i, "9223372036854775808");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("constant expression out of range"));
        // The other end of the range the rule has no type for, `2^64 - 1`.
        // D4.5
        body = drop_site(i, "18446744073709551615");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("constant expression out of range"));
    }
})

TEST(every_operator_that_drops_its_operand_reports_a_constant_it_carries, {
    // The same thirteen with an expression that folded to no value. The
    // constant then meets i64, the widest type the rule offers, and the
    // message names it. `n` is a variable, so the shift folds nothing.
    // D4.5, D6.2
    char source[512];
    for (uint64_t i = 0; i < sizeof DROP_SITES / sizeof DROP_SITES[0]; i++) {
        const char* body = drop_site(i, "(9223372036854775808 << n)");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_UNUSED(snprintf(source, sizeof source, "    i32 n = 1;\n%s", body));
        TEST_ASSERT_FALSE(check_body(source));
        TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    }
})

TEST(a_constant_that_folds_back_into_range_leaves_the_operator_its_own_rule, {
    // The second outcome of the same call, and it is not an error path. The
    // shift leaves `2^63 >> 1` poisoned and carrying the i64 2^62, and the
    // default type accepts that value, so the operand reaches the operator
    // **typed** and the operator answers by its own rule. Without the call the
    // four programs below compiled, exited 0 and printed nothing, which is why
    // they stand with the thirteen and not with the typed operands.
    // D4.4, D4.5
    TEST_ASSERT_FALSE(check_body("    println(*(9223372036854775808 >> 1));"));
    TEST_ASSERT_TRUE(said("cannot dereference i64"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_body("    println((9223372036854775808 >> 1).x);"));
    TEST_ASSERT_TRUE(said("i64 has no field 'x'"));
    TEST_ASSERT_FALSE(check_body("    println((9223372036854775808 >> 1)());"));
    TEST_ASSERT_TRUE(said("cannot call a value of type i64"));
    TEST_ASSERT_FALSE(check_body("    del(9223372036854775808 >> 1);"));
    TEST_ASSERT_TRUE(said("'del' takes a reference, not i64"));
    // The value itself is the one the exact folding gives, and a context takes it.
    // D4.4
    TEST_ASSERT_TRUE(check_body("    i64 w = 9223372036854775808 >> 1;\n    println(w);"));
    TEST_ASSERT_EQ_STR(init_type("w"), "i64");
})

TEST(a_dropped_operand_that_is_no_constant_keeps_its_one_diagnostic, {
    // The guard reads the operand and not the operator, so an operand that was
    // reported where it arose stays reported once. Every message below is the
    // one the site gave before the guard existed, and the count is the
    // assertion: a guard that reported for every poisoned operand would double
    // each of them, and the corpus could not see it, because the harness
    // judges a line and not a count.
    // D14.2
    for (uint64_t i = 0; i < sizeof DROP_SITES / sizeof DROP_SITES[0]; i++) {
        const char* body = drop_site(i, "nosuch");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("unknown name 'nosuch'"));
        // `move` adds "'move' has no value", which it added before this guard
        // too: its early return leaves the result `void`.
        // D12.2
        TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)(i == 9 ? 2 : 1));
    }
})

TEST(a_dropped_operand_with_a_type_keeps_the_message_of_its_operator, {
    // The other half: an operand that is no untyped constant reaches the
    // operator, which answers as it always did. No mutant of the guard turns
    // this red, and that is the point of writing it down: the guard runs only
    // for a poisoned operand, so a typed one never enters it. The test pins
    // the seven messages, so a later change that widens the key is seen.
    // D14.2
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    println(*v);"));
    TEST_ASSERT_TRUE(said("cannot dereference i32"));
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    println(v.x);"));
    TEST_ASSERT_TRUE(said("i32 has no field 'x'"));
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    println(v[0]);"));
    TEST_ASSERT_TRUE(said("i32 cannot be indexed"));
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    println(v());"));
    TEST_ASSERT_TRUE(said("cannot call a value of type i32"));
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    del(v);"));
    TEST_ASSERT_TRUE(said("'del' takes a reference, not i32"));
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    println(move(v));"));
    TEST_ASSERT_TRUE(said("'move' needs an owning operand, not i32"));
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    for (i32 x : v) {\n        println(x);\n"
                                 "    }"));
    TEST_ASSERT_TRUE(said("cannot iterate i32"));
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_operand", argc, argv);
    TEST_RUN(every_operator_that_drops_its_operand_reports_a_constant_with_no_type);
    TEST_RUN(every_operator_that_drops_its_operand_reports_a_constant_it_carries);
    TEST_RUN(a_constant_that_folds_back_into_range_leaves_the_operator_its_own_rule);
    TEST_RUN(a_dropped_operand_that_is_no_constant_keeps_its_one_diagnostic);
    TEST_RUN(a_dropped_operand_with_a_type_keeps_the_message_of_its_operator);
    check_reset();
    done();
    TEST_EXIT();
}
