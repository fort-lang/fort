// Unit tests of the checker's statements (core-language.md 6, 7.3): the
// scopes and the shadowing rules, the declarations and the assignment forms,
// the control-flow statements, `switch`, `defer`, `return` and the
// terminating-statement rule. The expressions are in check_expr_test.c and
// check_const_test.c.
// D7.1 to D7.11, D8.4, D8.5
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- scopes and shadowing -----------------------------------------------------------
// D7.9

TEST(a_local_may_not_reuse_a_parameter_name, {
    TEST_ASSERT_FALSE(check_src("fn i32 twice(i32 n) {\n    i32 n = 2;\n    return n;\n}\n"
                                "fn i32 main() {\n    return twice(1);\n}\n"));
    // The diagnostic stands at the inner declaration.
    // D7.9, D14.2
    TEST_ASSERT_TRUE(said("main.ft:2:9: error: 'n' shadows a parameter"));
})

TEST(a_local_may_not_reuse_an_enclosing_local_name, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    {\n        i32 x = 2;\n"
                                 "        println(x);\n    }"));
    TEST_ASSERT_TRUE(said("'x' shadows an enclosing local"));
})

TEST(sibling_scopes_may_reuse_a_name, {
    TEST_ASSERT_TRUE(check_body("    {\n        i32 k = 1;\n        println(k);\n    }\n"
                                "    {\n        i32 k = 2;\n        println(k);\n    }"));
})

TEST(a_local_may_shadow_a_module_level_name, {
    TEST_ASSERT_TRUE(check_src("i32 MAX = 1;\n"
                               "fn i32 main() {\n    i32 MAX = 2;\n    return MAX;\n}\n"));
})

TEST(a_local_is_visible_only_after_its_own_declaration, {
    TEST_ASSERT_FALSE(check_body("    i32 x = x;\n    println(x);"));
    // A local's scope starts after its own declaration.
    // D7.9
    TEST_ASSERT_TRUE(said("unknown name 'x'"));
})

TEST(a_redeclaration_in_one_block_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    i32 x = 2;\n    println(x);"));
    TEST_ASSERT_TRUE(said("'x' is already declared in this block"));
})

TEST(a_duplicate_parameter_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn i32 add(i32 a, i32 a) {\n    return a;\n}\n"
                                "fn i32 main() {\n    return add(1, 2);\n}\n"));
    TEST_ASSERT_TRUE(said("duplicate parameter 'a'"));
})

// ---- declarations and initializers --------------------------------------------------
// D7.1, D6.5

TEST(a_local_takes_its_declared_type, {
    TEST_ASSERT_TRUE(check_body("    i32 mut* mut p = null;\n    println(p);"));
    TEST_ASSERT_EQ_STR(decl_type("p"), "i32 mut* mut");
})

TEST(a_brace_initializer_needs_an_aggregate, {
    TEST_ASSERT_FALSE(check_body("    i32 h = {};\n    println(h);"));
    // `= {}` zero-initializes any aggregate, span, string or enum.
    // D6.5
    TEST_ASSERT_TRUE(said("'{}' initializes an aggregate, span, string or enum, not i32"));
    TEST_ASSERT_FALSE(check_body("    i32* k = {};\n    println(k);"));
    TEST_ASSERT_TRUE(said("not i32*"));
})

TEST(the_zero_initializer_works_for_every_aggregate, {
    TEST_ASSERT_TRUE(
        check_src("enum color {\n    red,\n}\nstruct point {\n    i32 x;\n}\n"
                  "fn i32 main() {\n    point p = {};\n    i32[2] a = {};\n"
                  "    i32@ s = {};\n    string t = {};\n    color c = {};\n"
                  "    println(p.x, a[0], s.len, t, cast(c, i32));\n    return 0;\n}\n"));
})

TEST(a_positional_struct_literal_needs_every_field, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                                "fn i32 main() {\n    point p = point{1};\n    return p.x;\n}\n"));
    TEST_ASSERT_TRUE(said("a positional literal of struct point needs its 2 fields, 1 given"));
})

TEST(a_designated_literal_refuses_duplicates_and_unknown_fields, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                                "fn i32 main() {\n    point p = point{.x = 1, .x = 2};\n"
                                "    return p.x;\n}\n"));
    TEST_ASSERT_TRUE(said("duplicate field 'x'"));
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                                "fn i32 main() {\n    point p = point{.z = 1};\n"
                                "    return p.x;\n}\n"));
    TEST_ASSERT_TRUE(said("struct point has no field 'z'"));
})

TEST(an_array_literal_needs_exactly_its_length, {
    TEST_ASSERT_FALSE(check_body("    i32[2] a = {1, 2, 3};\n    println(a[0]);"));
    TEST_ASSERT_TRUE(said("an array literal for i32[2] needs 2 elements, 3 given"));
})

TEST(a_nested_literal_checks_its_leaves, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "struct line {\n    point a;\n    point b;\n}\n"
                               "fn i32 main() {\n    line l = {{0, 0}, {1, 1}};\n"
                               "    return l.b.x;\n}\n"));
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                                "struct line {\n    point a;\n    point b;\n}\n"
                                "fn i32 main() {\n    line l = {{0, 0}, {1, \"s\"}};\n"
                                "    return l.b.x;\n}\n"));
    TEST_ASSERT_TRUE(said("the field expects i32, not string"));
})

// ---- assignment ---------------------------------------------------------------------
// D7.2

TEST(assignment_needs_a_mutable_lvalue, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    x = 2;\n    println(x);"));
    TEST_ASSERT_TRUE(said("cannot assign to immutable 'x'"));
    TEST_ASSERT_TRUE(check_body("    i32 mut y = 1;\n    y = 2;\n    println(y);"));
})

TEST(assignment_converts_the_value, {
    TEST_ASSERT_FALSE(
        check_body("    i32 mut x = 1;\n    i64 w = 2;\n    x = w;\n    println(x);"));
    TEST_ASSERT_TRUE(said("the assignment expects i32, not i64"));
})

TEST(assignment_may_drop_mutability, {
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32 mut* p = &v;\n    i32* mut q = null;\n"
                                "    q = p;\n    println(q);"));
})

TEST(a_compound_assignment_has_the_rules_of_its_operator, {
    TEST_ASSERT_TRUE(check_body("    i32 mut x = 1;\n    x += 2;\n    x <<= 3;\n    println(x);"));
    TEST_ASSERT_FALSE(
        check_body("    i32 mut x = 1;\n    i64 w = 2;\n    x += w;\n    println(x);"));
    TEST_ASSERT_TRUE(said("'+' takes two operands of the same type, not i32 and i64"));
    TEST_ASSERT_FALSE(check_body("    string mut s = \"a\";\n    s += \"b\";\n    println(s);"));
    TEST_ASSERT_TRUE(said("'+' takes numeric operands, not string"));
    // A target that failed says nothing about the operator as well: one
    // construct, one diagnostic.
    // D14.2
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    x += \"s\";\n    println(x);"));
    TEST_ASSERT_TRUE(said("cannot assign to immutable 'x'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(incdec_needs_an_integer_lvalue, {
    TEST_ASSERT_TRUE(check_body("    i32 mut x = 1;\n    x++;\n    x--;\n    println(x);"));
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    x++;\n    println(x);"));
    TEST_ASSERT_TRUE(said("cannot modify immutable 'x'"));
    TEST_ASSERT_FALSE(check_body("    char mut c = 'a';\n    c++;\n    println(c);"));
    // `++` and `--` are allowed on integer types only.
    // D7.2
    TEST_ASSERT_TRUE(said("'++' takes an integer operand, not char"));
})

TEST(a_call_statement_may_discard_its_result, {
    TEST_ASSERT_TRUE(check_src("fn i32 compute() {\n    return 1;\n}\n"
                               "fn i32 main() {\n    compute();\n    return 0;\n}\n"));
})

TEST(an_owning_result_may_not_be_discarded, {
    TEST_ASSERT_FALSE(check_body("    i32 mut* own p = new(i32);\n    move(p);\n    del(p);"));
    // The value of `move` must land somewhere.
    // D12.2, D17.8
    TEST_ASSERT_TRUE(said("owning temporary would leak"));
})

// ---- control flow -------------------------------------------------------------------
// D7.4, D7.5

TEST(every_condition_must_be_bool, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    if (x) {\n        println(1);\n    }"));
    TEST_ASSERT_TRUE(said("a condition must be bool, not i32"));
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    while (p) {\n"
                                 "        println(1);\n    }"));
    TEST_ASSERT_TRUE(said("a condition must be bool, not i32*"));
    TEST_ASSERT_FALSE(check_body("    for (i32 mut i = 0; i; i++) {\n        println(i);\n    }"));
    TEST_ASSERT_TRUE(said("a condition must be bool, not i32"));
})

TEST(an_else_if_chain_is_checked, {
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    if (n < 0) {\n        println(0);\n"
                                "    } else if (n > 0) {\n        println(1);\n    } else {\n"
                                "        println(2);\n    }"));
})

TEST(a_for_init_variable_is_scoped_to_the_loop, {
    TEST_ASSERT_TRUE(
        check_body("    for (i32 mut i = 0; i < 3; i++) {\n        println(i);\n    }\n"
                   "    i32 i = 9;\n    println(i);"));
    TEST_ASSERT_FALSE(
        check_body("    for (i32 mut i = 0; i < 3; i++) {\n        println(i);\n    }\n"
                   "    println(i);"));
    TEST_ASSERT_TRUE(said("unknown name 'i'"));
})

TEST(a_for_induction_variable_must_be_mut, {
    TEST_ASSERT_FALSE(check_body("    for (i32 i = 0; i < 3; i++) {\n        println(i);\n    }"));
    // There is no exception for induction variables.
    // D7.5
    TEST_ASSERT_TRUE(said("cannot modify immutable 'i'"));
})

TEST(an_empty_for_header_is_legal,
     { TEST_ASSERT_TRUE(check_body("    for (;;) {\n        break;\n    }")); })

TEST(a_range_for_binds_the_element_type, {
    TEST_ASSERT_TRUE(check_body("    i32[3] a = {1, 2, 3};\n    for (i32 v : a) {\n"
                                "        println(v);\n    }"));
    const ast_node_t* loop = node_in_main(AST_RANGE_FOR, "v");
    TEST_ASSERT_NONNULL(loop);
    TEST_ASSERT_EQ_STR(sym_kind_name(loop->sym->kind), "local");
    TEST_ASSERT_EQ_STR(type_text(loop->sym->type), "i32");
})

TEST(a_range_for_refuses_a_wrong_element_type, {
    TEST_ASSERT_FALSE(check_body("    i32[3] a = {1, 2, 3};\n    for (i64 v : a) {\n"
                                 "        println(v);\n    }"));
    TEST_ASSERT_TRUE(said("the element type is i32, not i64"));
})

TEST(a_range_for_needs_a_collection, {
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    for (i32 v : p) {\n"
                                 "        println(v);\n    }"));
    TEST_ASSERT_TRUE(said("cannot iterate i32*"));
})

TEST(a_range_for_over_a_string_yields_char,
     { TEST_ASSERT_TRUE(check_body("    for (char c : \"hi\") {\n        println(c);\n    }")); })

TEST(a_range_variable_cannot_be_own, {
    TEST_ASSERT_FALSE(check_body("    i32 mut* own mut@ own k = new(i32* own, 2);\n"
                                 "    for (i32 mut* own c : k) {\n        println(c);\n    }\n"
                                 "    del(k);"));
    // The loop lends its collection.
    // D17.10
    TEST_ASSERT_TRUE(said("a range variable cannot be own"));
})

TEST(a_range_for_lends_an_owning_element, {
    TEST_ASSERT_TRUE(check_body("    i32 mut* own mut@ own k = new(i32* own, 2);\n"
                                "    for (i32 mut* c : k) {\n        println(c);\n    }\n"
                                "    del(k);"));
})

// ---- switch -------------------------------------------------------------------------
// D7.6, D7.7

TEST(a_switch_operand_is_an_integer_a_char_or_an_enum, {
    TEST_ASSERT_FALSE(check_body("    string s = \"a\";\n    switch (s) {\n    default:\n    }"));
    TEST_ASSERT_TRUE(said("cannot switch on string"));
    TEST_ASSERT_FALSE(
        check_body("    bool flag = true;\n    switch (flag) {\n    default:\n    }"));
    TEST_ASSERT_TRUE(said("cannot switch on bool"));
    TEST_ASSERT_TRUE(check_body("    char c = 'a';\n    switch (c) {\n    case 'a':\n"
                                "        println(1);\n    default:\n    }"));
})

TEST(duplicate_case_values_are_refused, {
    TEST_ASSERT_FALSE(check_src("i32 TWO = 2;\n"
                                "fn i32 main() {\n    i32 x = 1;\n    switch (x) {\n"
                                "    case 1:\n        println(1);\n    case 2, 3:\n"
                                "        println(2);\n    case TWO:\n        println(3);\n"
                                "    }\n    return 0;\n}\n"));
    // No duplicates after evaluation.
    // D7.6
    TEST_ASSERT_TRUE(said("duplicate case value 2"));
})

TEST(a_switch_has_at_most_one_default, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    switch (x) {\n    default:\n"
                                 "        println(1);\n    default:\n        println(2);\n    }"));
    TEST_ASSERT_TRUE(said("a switch has at most one 'default'"));
})

TEST(a_case_label_must_be_a_constant_expression, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    i32 k = 2;\n    switch (x) {\n"
                                 "    case k:\n        println(1);\n    }"));
    TEST_ASSERT_TRUE(said("a case label must be a constant expression"));
})

TEST(a_case_label_takes_the_operand_type, {
    TEST_ASSERT_FALSE(check_body("    char c = 'a';\n    switch (c) {\n    case 65:\n"
                                 "        println(1);\n    }"));
    // An integer constant in a char context is an error.
    // D4.3
    TEST_ASSERT_TRUE(said("an integer constant does not become char"));
})

TEST(a_non_exhaustive_enum_switch_is_reported_at_the_closing_brace, {
    TEST_ASSERT_FALSE(check_src("enum color {\n    red,\n    green,\n    blue,\n}\n"
                                "fn i32 main() {\n    color c = color.red;\n    switch (c) {\n"
                                "    case color.red:\n        println(1);\n    }\n"
                                "    return 0;\n}\n"));
    // Reported at the closing brace of the switch.
    // D7.7, D14.2
    TEST_ASSERT_TRUE(said("main.ft:11:5: error: switch over color does not handle green, blue"));
})

TEST(a_default_completes_an_enum_switch, {
    TEST_ASSERT_TRUE(check_src("enum color {\n    red,\n    green,\n}\n"
                               "fn i32 main() {\n    color c = color.red;\n    switch (c) {\n"
                               "    case color.red:\n        println(1);\n    default:\n    }\n"
                               "    return 0;\n}\n"));
})

TEST(a_case_body_is_a_scope, {
    TEST_ASSERT_TRUE(check_body("    i32 x = 1;\n    switch (x) {\n    case 1:\n"
                                "        i32 k = 2;\n        println(k);\n    case 2:\n"
                                "        i32 k = 3;\n        println(k);\n    default:\n    }"));
})

// ---- break, continue and defer ------------------------------------------------------
// D7.6, D7.8

TEST(break_outside_a_loop_or_switch_is_refused, {
    TEST_ASSERT_FALSE(check_body("    break;"));
    TEST_ASSERT_TRUE(said("'break' outside a loop or switch"));
    TEST_ASSERT_TRUE(
        check_body("    i32 x = 1;\n    switch (x) {\n    default:\n        break;\n    }"));
})

TEST(continue_outside_a_loop_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    switch (x) {\n    default:\n"
                                 "        continue;\n    }"));
    TEST_ASSERT_TRUE(said("'continue' outside a loop"));
    TEST_ASSERT_TRUE(check_body("    while (true) {\n        continue;\n    }"));
})

TEST(return_break_and_continue_are_refused_inside_deferred_code, {
    TEST_ASSERT_FALSE(check_body("    defer {\n        return 1;\n    }"));
    TEST_ASSERT_TRUE(said("'return' inside deferred code"));
    TEST_ASSERT_FALSE(check_body("    while (true) {\n        defer {\n            break;\n"
                                 "        }\n    }"));
    TEST_ASSERT_TRUE(said("'break' inside deferred code"));
})

TEST(a_deferred_statement_is_checked, {
    TEST_ASSERT_TRUE(check_body("    i32 mut* own p = new(i32);\n    defer del(p);\n"
                                "    println(p);"));
    TEST_ASSERT_FALSE(check_body("    defer println(nope);"));
    TEST_ASSERT_TRUE(said("unknown name 'nope'"));
})

// ---- return and terminating statements ----------------------------------------------
// D7.11, D8.4, D8.5

TEST(a_return_with_a_value_in_a_void_function_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn void nothing() {\n    return 1;\n}\n"
                                "fn i32 main() {\n    nothing();\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'return' with a value in a void function"));
})

TEST(a_return_without_a_value_in_a_non_void_function_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn i32 bare() {\n    return;\n}\n"
                                "fn i32 main() {\n    return bare();\n}\n"));
    TEST_ASSERT_TRUE(said("'return' without a value in a function returning i32"));
})

TEST(a_return_in_a_noreturn_function_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn noreturn quit(i32 code) {\n    if (code == 0) {\n"
                                "        return;\n    }\n    panic(\"quit\");\n}\n"
                                "fn i32 main() {\n    quit(1);\n}\n"));
    // A `noreturn` function may not contain `return`.
    // D8.5
    TEST_ASSERT_TRUE(said("'return' in a noreturn function"));
})

TEST(a_missing_return_is_reported_at_the_closing_brace, {
    TEST_ASSERT_FALSE(check_src("fn i32 pick(bool b) {\n    if (b) {\n        return 1;\n    }\n}\n"
                                "fn i32 main() {\n    return pick(true);\n}\n"));
    // Reported at the body's closing brace.
    // D8.4, D14.2
    TEST_ASSERT_TRUE(said("main.ft:5:1: error: missing return"));
})

TEST(an_if_with_an_else_terminates, {
    TEST_ASSERT_TRUE(check_src("fn i32 sign(i32 x) {\n    if (x < 0) {\n        return -1;\n"
                               "    } else {\n        return 1;\n    }\n}\n"
                               "fn i32 main() {\n    return sign(1);\n}\n"));
    TEST_ASSERT_FALSE(check_src("fn i32 sign(i32 x) {\n    if (x < 0) {\n        return -1;\n"
                                "    } else if (x > 0) {\n        return 1;\n    }\n}\n"
                                "fn i32 main() {\n    return sign(1);\n}\n"));
    TEST_ASSERT_TRUE(said("missing return"));
})

TEST(a_while_true_with_no_break_terminates, {
    TEST_ASSERT_TRUE(
        check_src("fn i32 spin() {\n    while (true) {\n        println(1);\n    }\n}\n"
                  "fn i32 main() {\n    return spin();\n}\n"));
    TEST_ASSERT_FALSE(check_src("fn i32 spin() {\n    while (true) {\n        break;\n    }\n}\n"
                                "fn i32 main() {\n    return spin();\n}\n"));
    // A `break` targeting the loop makes it fall through.
    // D8.4
    TEST_ASSERT_TRUE(said("missing return"));
})

TEST(a_constant_true_condition_does_not_terminate, {
    // The rule names `while (true)`, the literal: a condition that folds to
    // true is not one, and the conservative reading reports the missing
    // return rather than accepting a body that may fall through.
    // D8.4
    TEST_ASSERT_FALSE(check_src("bool ALWAYS = true;\n"
                                "fn i32 spin() {\n    while (ALWAYS) {\n        println(1);\n"
                                "    }\n}\n"
                                "fn i32 main() {\n    return spin();\n}\n"));
    TEST_ASSERT_TRUE(said("missing return"));
})

TEST(a_call_through_a_noreturn_pointer_terminates, {
    // A call statement to a `noreturn` function is terminating, through a
    // function pointer as through a name.
    // D8.4, D6.11
    TEST_ASSERT_TRUE(check_src("fn i32 pick(bool b, fn noreturn(string) quit) {\n"
                               "    if (b) {\n        return 1;\n    }\n"
                               "    quit(\"no\");\n}\n"
                               "fn noreturn die(string msg) {\n    panic(msg);\n}\n"
                               "fn i32 main() {\n    return pick(true, die);\n}\n"));
    TEST_ASSERT_FALSE(check_src("fn i32 pick(bool b, fn void(string) quit) {\n"
                                "    if (b) {\n        return 1;\n    }\n"
                                "    quit(\"no\");\n}\n"
                                "fn void say(string msg) {\n    println(msg);\n}\n"
                                "fn i32 main() {\n    return pick(true, say);\n}\n"));
    TEST_ASSERT_TRUE(said("missing return"));
})

TEST(a_discarded_owning_aggregate_is_refused, {
    // An owning result is an `own` reference or an owning aggregate, and
    // neither may be dropped.
    // D17.7, D17.8
    TEST_ASSERT_FALSE(check_src("struct vec {\n    i32 mut@ own data;\n}\n"
                                "fn vec make() {\n    return vec{.data = new(i32, 2)};\n}\n"
                                "fn i32 main() {\n    make();\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak"));
    TEST_ASSERT_FALSE(check_src("struct vec {\n    i32 mut@ own data;\n}\n"
                                "fn i32 main() {\n    vec mut b = {};\n    move(b);\n"
                                "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak"));
    // A result that owns nothing is discarded freely.
    // D7.3
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n}\n"
                               "fn point make() {\n    return point{1};\n}\n"
                               "fn i32 main() {\n    make();\n    return 0;\n}\n"));
})

TEST(a_break_in_an_inner_switch_does_not_target_the_loop, {
    TEST_ASSERT_TRUE(check_src("fn i32 spin(i32 n) {\n    while (true) {\n        switch (n) {\n"
                               "        default:\n            break;\n        }\n    }\n}\n"
                               "fn i32 main() {\n    return spin(1);\n}\n"));
})

TEST(a_for_with_no_condition_terminates, {
    TEST_ASSERT_TRUE(check_src("fn i32 spin() {\n    for (;;) {\n        println(1);\n    }\n}\n"
                               "fn i32 main() {\n    return spin();\n}\n"));
})

TEST(an_exhaustive_enum_switch_terminates, {
    TEST_ASSERT_TRUE(check_src("enum color {\n    red,\n    green,\n}\n"
                               "fn i32 code(color c) {\n    switch (c) {\n    case color.red:\n"
                               "        return 1;\n    case color.green:\n        return 2;\n"
                               "    }\n}\n"
                               "fn i32 main() {\n    return code(color.red);\n}\n"));
    TEST_ASSERT_FALSE(check_src("enum color {\n    red,\n    green,\n}\n"
                                "fn i32 code(color c) {\n    switch (c) {\n    case color.red:\n"
                                "        return 1;\n    case color.green:\n        println(2);\n"
                                "    }\n}\n"
                                "fn i32 main() {\n    return code(color.red);\n}\n"));
    TEST_ASSERT_TRUE(said("missing return"));
})

TEST(a_panic_and_a_noreturn_call_terminate, {
    TEST_ASSERT_TRUE(check_src("fn i32 checked(i32 x) {\n    if (x < 0) {\n"
                               "        panic(\"negative\");\n    } else {\n        return x;\n"
                               "    }\n}\n"
                               "fn i32 main() {\n    return checked(1);\n}\n"));
    TEST_ASSERT_TRUE(check_src("fn noreturn die() {\n    panic(\"stop\");\n}\n"
                               "fn i32 pick(bool b) {\n    if (b) {\n        return 1;\n    }\n"
                               "    die();\n}\n"
                               "fn i32 main() {\n    return pick(true);\n}\n"));
})

TEST(a_noreturn_function_must_end_in_a_terminating_statement, {
    TEST_ASSERT_FALSE(check_src("fn noreturn die() {\n    println(1);\n}\n"
                                "fn i32 main() {\n    die();\n}\n"));
    TEST_ASSERT_TRUE(said("a noreturn function must end in a terminating statement"));
})

TEST(a_body_with_an_error_node_never_reports_missing_return, {
    // An error node stands where a statement was expected, and a block that
    // holds one is not judged (rule 4 of the symbol contract). The checker
    // adds nothing to the parser's diagnostic.
    // D14.2
    TEST_ASSERT_TRUE(check_broken("fn i32 pick(bool b) {\n    i32 x = ;\n}\n"));
    TEST_ASSERT_FALSE(said("missing return"));
    TEST_ASSERT_TRUE(said("expected an expression"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_stmt", argc, argv);
    TEST_RUN(a_local_may_not_reuse_a_parameter_name);
    TEST_RUN(a_local_may_not_reuse_an_enclosing_local_name);
    TEST_RUN(sibling_scopes_may_reuse_a_name);
    TEST_RUN(a_local_may_shadow_a_module_level_name);
    TEST_RUN(a_local_is_visible_only_after_its_own_declaration);
    TEST_RUN(a_redeclaration_in_one_block_is_refused);
    TEST_RUN(a_duplicate_parameter_is_refused);
    TEST_RUN(a_local_takes_its_declared_type);
    TEST_RUN(a_brace_initializer_needs_an_aggregate);
    TEST_RUN(the_zero_initializer_works_for_every_aggregate);
    TEST_RUN(a_positional_struct_literal_needs_every_field);
    TEST_RUN(a_designated_literal_refuses_duplicates_and_unknown_fields);
    TEST_RUN(an_array_literal_needs_exactly_its_length);
    TEST_RUN(a_nested_literal_checks_its_leaves);
    TEST_RUN(assignment_needs_a_mutable_lvalue);
    TEST_RUN(assignment_converts_the_value);
    TEST_RUN(assignment_may_drop_mutability);
    TEST_RUN(a_compound_assignment_has_the_rules_of_its_operator);
    TEST_RUN(incdec_needs_an_integer_lvalue);
    TEST_RUN(a_call_statement_may_discard_its_result);
    TEST_RUN(an_owning_result_may_not_be_discarded);
    TEST_RUN(every_condition_must_be_bool);
    TEST_RUN(an_else_if_chain_is_checked);
    TEST_RUN(a_for_init_variable_is_scoped_to_the_loop);
    TEST_RUN(a_for_induction_variable_must_be_mut);
    TEST_RUN(an_empty_for_header_is_legal);
    TEST_RUN(a_range_for_binds_the_element_type);
    TEST_RUN(a_range_for_refuses_a_wrong_element_type);
    TEST_RUN(a_range_for_needs_a_collection);
    TEST_RUN(a_range_for_over_a_string_yields_char);
    TEST_RUN(a_range_variable_cannot_be_own);
    TEST_RUN(a_range_for_lends_an_owning_element);
    TEST_RUN(a_switch_operand_is_an_integer_a_char_or_an_enum);
    TEST_RUN(duplicate_case_values_are_refused);
    TEST_RUN(a_switch_has_at_most_one_default);
    TEST_RUN(a_case_label_must_be_a_constant_expression);
    TEST_RUN(a_case_label_takes_the_operand_type);
    TEST_RUN(a_non_exhaustive_enum_switch_is_reported_at_the_closing_brace);
    TEST_RUN(a_default_completes_an_enum_switch);
    TEST_RUN(a_case_body_is_a_scope);
    TEST_RUN(break_outside_a_loop_or_switch_is_refused);
    TEST_RUN(continue_outside_a_loop_is_refused);
    TEST_RUN(return_break_and_continue_are_refused_inside_deferred_code);
    TEST_RUN(a_deferred_statement_is_checked);
    TEST_RUN(a_return_with_a_value_in_a_void_function_is_refused);
    TEST_RUN(a_return_without_a_value_in_a_non_void_function_is_refused);
    TEST_RUN(a_return_in_a_noreturn_function_is_refused);
    TEST_RUN(a_missing_return_is_reported_at_the_closing_brace);
    TEST_RUN(an_if_with_an_else_terminates);
    TEST_RUN(a_while_true_with_no_break_terminates);
    TEST_RUN(a_constant_true_condition_does_not_terminate);
    TEST_RUN(a_call_through_a_noreturn_pointer_terminates);
    TEST_RUN(a_discarded_owning_aggregate_is_refused);
    TEST_RUN(a_break_in_an_inner_switch_does_not_target_the_loop);
    TEST_RUN(a_for_with_no_condition_terminates);
    TEST_RUN(an_exhaustive_enum_switch_terminates);
    TEST_RUN(a_panic_and_a_noreturn_call_terminate);
    TEST_RUN(a_noreturn_function_must_end_in_a_terminating_statement);
    TEST_RUN(a_body_with_an_error_node_never_reports_missing_return);
    check_reset();
    done();
    TEST_EXIT();
}
