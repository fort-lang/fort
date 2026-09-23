// Tests operands that give a constant no type context.
// Each form must report a constant that has no default type.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "check.h"
#include "common/check_helpers.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// These 13 positions give no context and drop a poisoned operand. The table comes from
// check_helpers.h, which check_poison_test.c also reads.

TEST(every_operator_that_drops_its_operand_reports_a_constant_with_no_type, {
    // `2^63` fits neither default integer type, so the checker reports it.
    // None of these operand positions provides a type context.
    // Without that it is dropped with the error type and nothing is ever said.
    for (uint64_t i = 0; i < (uint64_t)DROP_SITE_COUNT; i++) {
        const char* body = drop_site(i, "9223372036854775808");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("constant expression out of range"));
        // `2^64 - 1` also fits neither default integer type.
        body = drop_site(i, "18446744073709551615");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("constant expression out of range"));
    }
})

TEST(every_operator_that_drops_its_operand_reports_a_constant_it_carries, {
    // The same thirteen with an expression that folded to no value. The
    // The constant meets i64, the widest default type, and the message names it.
    // `n` is a variable, so the shift does not fold.
    char source[512];
    for (uint64_t i = 0; i < (uint64_t)DROP_SITE_COUNT; i++) {
        const char* body = drop_site(i, "(9223372036854775808 << n)");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_UNUSED(snprintf(source, sizeof source, "    i32 n = 1;\n%s", body));
        TEST_ASSERT_FALSE(check_body(source));
        TEST_ASSERT_TRUE(said("constant 9223372036854775808 does not fit i64"));
    }
})

TEST(a_constant_that_folds_back_into_range_leaves_the_operator_its_own_rule, {
    // The second outcome of the same call, and it is not an error path. The shift leaves `2^63 >>
    // 1` poisoned and carrying the i64 2^62. The default type accepts that value, so the operand
    // reaches the operator **typed** and the operator answers by its own rule. Without the call the
    // four programs below compiled, exited 0 and printed nothing. It is why they stand with the
    // thirteen and not with the typed operands.
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
    TEST_ASSERT_TRUE(check_body("    i64 w = 9223372036854775808 >> 1;\n    println(w);"));
    TEST_ASSERT_EQ_STR(init_type("w"), "i64");
})

TEST(a_dropped_operand_that_is_no_constant_keeps_its_one_diagnostic, {
    // The guard reads the operand, not the operator. An operand reported at its
    // source stays one diagnostic. The count detects duplicate reports. A guard
    // that reports each poisoned operand would double the count.
    for (uint64_t i = 0; i < (uint64_t)DROP_SITE_COUNT; i++) {
        const char* body = drop_site(i, "nosuch");
        TEST_ASSERT_TRUE(body != NULL);
        TEST_ASSERT_FALSE(check_body(body));
        TEST_ASSERT_TRUE(said("unknown name 'nosuch'"));
        // `move` also reports "'move' has no value" because its early return
        // leaves the result `void`.
        TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)(i == DROP_SITE_MOVE ? 2 : 1));
    }
})

TEST(a_dropped_operand_with_a_type_keeps_the_message_of_its_operator, {
    // An operand that is not an untyped constant reaches the operator. The guard
    // runs only for poisoned operands, so typed operands keep these seven messages.
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
