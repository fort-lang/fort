// Unit tests of untyped constants and constant folding in the checker
// (core-language.md 5.3; D4.1 to D4.6, D3.14, D3.15): the context points at
// which a constant takes its type, the exact folding of D4.4, the typed
// folding of D4.6 and the run-time semantics of a constant `cast`. The
// operators, the lvalues and the calls are in check_expr_test.c.
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"
#include "consts.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the constants below are the test
// data: the values, the widths and the ranges the rules are about.

// ---- a constant takes its type from its context (D4.1, D4.2) ------------------------

TEST(an_integer_literal_takes_the_declared_type, {
    TEST_ASSERT_TRUE(check_body("    u8 b = 255;\n    i64 w = 1;\n    println(b, w);"));
    TEST_ASSERT_EQ_STR(init_type("b"), "u8");
    TEST_ASSERT_EQ_STR(init_type("w"), "i64");
})

TEST(a_constant_that_does_not_fit_its_context_is_refused, {
    TEST_ASSERT_FALSE(check_body("    u8 b = 256;"));
    TEST_ASSERT_TRUE(said("constant 256 does not fit u8"));
    TEST_ASSERT_FALSE(check_body("    u32 x = -1;"));
    TEST_ASSERT_TRUE(said("constant -1 does not fit u32"));
    TEST_ASSERT_FALSE(check_body("    i8 c = 128;"));
    TEST_ASSERT_TRUE(said("constant 128 does not fit i8"));
})

TEST(the_complement_of_zero_is_minus_one, {
    // `~c` on an untyped integer is `-c - 1`, so `u32 m = ~0;` is an error
    // (D4.4).
    TEST_ASSERT_FALSE(check_body("    u32 m = ~0;"));
    TEST_ASSERT_TRUE(said("constant -1 does not fit u32"));
    TEST_ASSERT_TRUE(check_body("    u32 m = 0xFFFFFFFF;\n    println(m);"));
})

TEST(an_integer_constant_does_not_become_a_char, {
    TEST_ASSERT_FALSE(check_body("    char c = 65;"));
    // An integer literal never becomes `char` implicitly (D4.3).
    TEST_ASSERT_TRUE(said("an integer constant does not become char: use cast"));
})

TEST(a_char_constant_becomes_an_integer, {
    TEST_ASSERT_TRUE(check_body("    u8 b = 'a';\n    println(b);"));
    TEST_ASSERT_EQ_STR(init_type("b"), "u8");
    int64_t v = 0;
    // In an integer context a char literal is its code point (D4.3).
    TEST_ASSERT_TRUE(init_int("b", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)97);
})

TEST(a_char_literal_defaults_to_char, {
    TEST_ASSERT_TRUE(check_body("    char c = 'a';\n    println(c);"));
    TEST_ASSERT_EQ_STR(init_type("c"), "char");
})

TEST(an_integer_constant_does_not_become_an_enum, {
    TEST_ASSERT_FALSE(check_src("enum color {\n    red,\n}\n"
                                "fn i32 main() {\n    color c = 0;\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("an integer constant does not become an enum: use cast"));
})

TEST(an_integer_constant_does_not_become_a_bool, {
    TEST_ASSERT_FALSE(check_body("    bool b = 1;"));
    // There is no truthiness (D3.3).
    TEST_ASSERT_TRUE(said("an integer constant does not become bool"));
})

TEST(with_no_context_a_constant_is_i32_then_i64, {
    TEST_ASSERT_TRUE(check_body("    println(1, 4294967296);"));
    const ast_node_t* call = node_in_main(AST_CALL, NULL);
    // With no context an untyped integer becomes i32 if it fits, else i64
    // (D4.5).
    TEST_ASSERT_EQ_STR(type_text(ast_child(call, 0)->type), "i32");
    TEST_ASSERT_EQ_STR(type_text(ast_child(call, 1)->type), "i64");
})

TEST(a_constant_beyond_i64_needs_a_context, {
    TEST_ASSERT_TRUE(check_body("    u64 m = 18446744073709551615;\n    println(m);"));
    TEST_ASSERT_EQ_STR(init_type("m"), "u64");
    // With no context at all it is an error (D4.5).
    TEST_ASSERT_FALSE(check_body("    println(18446744073709551615);"));
    TEST_ASSERT_TRUE(said("constant expression out of range"));
})

TEST(null_takes_a_pointer_type_from_its_context, {
    TEST_ASSERT_TRUE(check_body("    i32* p = null;\n    println(p);"));
    TEST_ASSERT_EQ_STR(init_type("p"), "i32*");
    TEST_ASSERT_TRUE(check_body("    void* v = null;\n    println(v);"));
    TEST_ASSERT_EQ_STR(init_type("v"), "void*");
})

TEST(null_outside_a_pointer_context_is_refused, {
    TEST_ASSERT_FALSE(check_body("    println(null);"));
    // `null` has no type of its own (D10.5).
    TEST_ASSERT_TRUE(said("'null' needs a pointer-typed context"));
    // A span does not compare with `null`: it has no equality at all (D3.13,
    // 5.5 of core-language.md).
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    bool b = s == null;\n    println(b);"));
    TEST_ASSERT_TRUE(said("'==' takes comparable operands, not i32@"));
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn i32 main() {\n    point p = null;\n    return p.x;\n}\n"));
    TEST_ASSERT_TRUE(said("'null' needs a pointer type, not point"));
})

TEST(a_shift_count_is_not_a_context_for_the_left_operand, {
    TEST_ASSERT_TRUE(check_body("    i32 n = 3;\n    u64 m = 1 << n;\n    println(m);"));
    // The untyped `1` takes u64 from the declaration, whatever the type of
    // `n` (D4.1).
    TEST_ASSERT_EQ_STR(init_type("m"), "u64");
    const ast_node_t* shift = node_in_main(AST_BINARY, NULL);
    TEST_ASSERT_EQ_STR(type_text(shift->a->type), "u64");
    TEST_ASSERT_EQ_STR(type_text(shift->b->type), "i32");
})

TEST(a_constant_argument_takes_the_parameter_type, {
    TEST_ASSERT_TRUE(check_src("fn i32 take(u8 b) {\n    return cast(b, i32);\n}\n"
                               "fn i32 main() {\n    return take(7);\n}\n"));
    const ast_node_t* call = node_find(node_in_main(AST_FN_DECL, "main"), AST_CALL, NULL);
    TEST_ASSERT_EQ_STR(type_text(ast_child(call, 0)->type), "u8");
    TEST_ASSERT_FALSE(check_src("fn i32 take(u8 b) {\n    return cast(b, i32);\n}\n"
                                "fn i32 main() {\n    return take(300);\n}\n"));
    TEST_ASSERT_TRUE(said("constant 300 does not fit u8"));
})

TEST(a_returned_constant_takes_the_return_type, {
    TEST_ASSERT_TRUE(check_src("fn u8 byte() {\n    return 7;\n}\n"
                               "fn i32 main() {\n    return cast(byte(), i32);\n}\n"));
    TEST_ASSERT_FALSE(check_src("fn u8 byte() {\n    return 300;\n}\n"
                                "fn i32 main() {\n    return cast(byte(), i32);\n}\n"));
    TEST_ASSERT_TRUE(said("constant 300 does not fit u8"));
})

TEST(every_integer_width_accepts_exactly_its_range, {
    // The boundary of each type of D3.1: the extreme value fits and the next
    // one does not (D4.2).
    TEST_ASSERT_TRUE(check_body("    i8 a = 127;\n    i8 b = -128;\n    println(a, b);"));
    TEST_ASSERT_FALSE(check_body("    i8 a = 128;"));
    TEST_ASSERT_FALSE(check_body("    i8 a = -129;"));
    TEST_ASSERT_TRUE(check_body("    u8 a = 255;\n    u8 b = 0;\n    println(a, b);"));
    TEST_ASSERT_FALSE(check_body("    u8 a = 256;"));
    TEST_ASSERT_FALSE(check_body("    u8 a = -1;"));
    TEST_ASSERT_TRUE(check_body("    i16 a = 32767;\n    i16 b = -32768;\n    println(a, b);"));
    TEST_ASSERT_FALSE(check_body("    i16 a = 32768;"));
    TEST_ASSERT_TRUE(check_body("    u16 a = 65535;\n    println(a);"));
    TEST_ASSERT_FALSE(check_body("    u16 a = 65536;"));
    TEST_ASSERT_TRUE(check_body("    i32 a = 2147483647;\n    i32 b = -2147483648;\n"
                                "    println(a, b);"));
    TEST_ASSERT_FALSE(check_body("    i32 a = 2147483648;"));
    TEST_ASSERT_FALSE(check_body("    i32 a = -2147483649;"));
    TEST_ASSERT_TRUE(check_body("    u32 a = 4294967295;\n    println(a);"));
    TEST_ASSERT_FALSE(check_body("    u32 a = 4294967296;"));
    TEST_ASSERT_TRUE(check_body("    i64 a = 9223372036854775807;\n"
                                "    i64 b = -9223372036854775808;\n    println(a, b);"));
    TEST_ASSERT_FALSE(check_body("    i64 a = 9223372036854775808;"));
    TEST_ASSERT_TRUE(check_body("    u64 a = 18446744073709551615;\n    u64 b = 0;\n"
                                "    println(a, b);"));
    // 2^64 leaves the constant range itself, before any target is met (D4.4).
    TEST_ASSERT_FALSE(check_body("    u64 a = 18446744073709551616;"));
})

TEST(a_char_constant_fits_every_width_that_holds_its_code_point, {
    TEST_ASSERT_TRUE(check_body("    u8 a = '\\xFF';\n    i32 b = 'a';\n    println(a, b);"));
    // A char whose code point does not fit the target is refused (D4.3).
    TEST_ASSERT_FALSE(check_body("    i8 a = '\\xFF';"));
})

// ---- exact folding among untyped constants (D4.4) -----------------------------------

TEST(untyped_arithmetic_folds_exactly, {
    TEST_ASSERT_TRUE(check_body("    i32 a = 2 + 3 * 4;\n    i32 b = 7 / 2;\n"
                                "    i32 c = -7 % 2;\n    println(a, b, c);"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("a", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)14);
    // `/` truncates toward zero and `%` takes the dividend's sign (D6.13).
    TEST_ASSERT_TRUE(init_int("b", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)3);
    TEST_ASSERT_TRUE(init_int("c", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)-1);
})

TEST(an_intermediate_may_leave_the_target_range, {
    // Only the result meets the context: untyped integers are evaluated
    // exactly (D4.4).
    TEST_ASSERT_TRUE(check_body("    i8 x = 200 - 100;\n    println(x);"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("x", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)100);
})

TEST(an_intermediate_outside_the_constant_range_is_refused, {
    TEST_ASSERT_FALSE(check_body("    u64 m = 18446744073709551615 + 1;"));
    // Untyped integers are evaluated exactly in [-2^63, 2^64 - 1] (D4.4).
    TEST_ASSERT_TRUE(said("constant expression out of range"));
})

TEST(constant_division_by_zero_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1 / 0;"));
    TEST_ASSERT_TRUE(said("constant division by zero"));
    TEST_ASSERT_FALSE(check_body("    i32 y = 7 % 0;"));
    TEST_ASSERT_TRUE(said("constant division by zero"));
})

TEST(bitwise_folding_uses_the_infinite_extension, {
    TEST_ASSERT_TRUE(check_body("    u64 m = -1 & 0xFFFFFFFFFFFFFFFF;\n    println(m);"));
    // Go's rule: the operands are infinite two's-complement patterns (D4.4).
    const ast_node_t* d = node_in_main(AST_VAR_DECL, "m");
    uint64_t u = 0;
    TEST_ASSERT_TRUE(cv_to_u64(check_node_value(&checker, d->b), &u));
    TEST_ASSERT_EQ_UINT64(u, (uint64_t)18446744073709551615ULL);
})

TEST(untyped_shifts_fold_exactly, {
    TEST_ASSERT_TRUE(check_body("    i64 w = 1 << 40;\n    i32 x = -3 >> 1;\n    println(w, x);"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("w", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)1099511627776);
    // `>>` is a floor division, so `-3 >> 1` is -2 (D4.4).
    TEST_ASSERT_TRUE(init_int("x", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)-2);
    TEST_ASSERT_FALSE(check_body("    i32 n = 1 << 40;"));
    TEST_ASSERT_TRUE(said("constant 1099511627776 does not fit i32"));
})

TEST(an_untyped_shift_count_is_at_most_63, {
    TEST_ASSERT_FALSE(check_body("    i64 w = 1 << 64;"));
    TEST_ASSERT_TRUE(said("constant shift count must be in 0..63"));
})

TEST(comparisons_of_constants_fold_to_a_bool, {
    TEST_ASSERT_TRUE(
        check_body("    bool a = 1 < 2;\n    bool b = 'a' == 'b';\n    println(a, b);"));
    TEST_ASSERT_EQ_STR(init_type("a"), "bool");
    const ast_node_t* d = node_in_main(AST_VAR_DECL, "a");
    TEST_ASSERT_TRUE(cv_eq(check_node_value(&checker, d->b), cv_from_bool(true)));
})

TEST(a_shift_count_is_checked_without_a_folded_left_operand, {
    // The left operand of `1 << n` has no value and no type until its context
    // fixes one, and the count is still in 0..63 (D4.4, D6.2).
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    i32 x = (1 << n) << 64;\n    println(x);"));
    TEST_ASSERT_TRUE(said("constant shift count must be in 0..63"));
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    u64 y = (1 << n) << 99;\n    println(y);"));
    TEST_ASSERT_TRUE(said("constant shift count must be in 0..63"));
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    i32 z = (1 << n) << -1;\n    println(z);"));
    TEST_ASSERT_TRUE(said("constant shift count must be in 0..63"));
    TEST_ASSERT_TRUE(check_body("    i32 n = 1;\n    u64 m = (1 << n) << 63;\n    println(m);"));
})

TEST(untyped_wrapping_operators_fold_exactly, {
    // A wrapping operator among untyped constants has no width to wrap at, so
    // it folds exactly like its checked form and a result outside the
    // constant range is an error (D4.4, D4.6, D11.2).
    TEST_ASSERT_TRUE(check_body("    i32 a = 1 +% 2;\n    i32 b = 2 -% 5;\n"
                                "    i32 c = 3 *% 4;\n    println(a, b, c);"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("a", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)3);
    TEST_ASSERT_TRUE(init_int("b", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)-3);
    TEST_ASSERT_TRUE(init_int("c", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)12);
    TEST_ASSERT_FALSE(check_body("    u64 m = 18446744073709551615 +% 1;\n    println(m);"));
    TEST_ASSERT_TRUE(said("constant expression out of range"));
})

// ---- typed folding (D4.6) -----------------------------------------------------------

TEST(a_typed_constant_expression_is_checked_at_compile_time, {
    TEST_ASSERT_FALSE(check_src("i32 A = 2147483647;\ni32 B = A + 1;\n"
                                "fn i32 main() {\n    return B;\n}\n"));
    // `A + 1` is a compile error, not a runtime trap (D4.6).
    TEST_ASSERT_TRUE(said("constant expression overflows i32"));
})

TEST(a_typed_constant_folds_with_the_declared_type, {
    TEST_ASSERT_TRUE(check_src("u8 A = 200;\nu8 B = A / 3;\n"
                               "fn i32 main() {\n    return cast(B, i32);\n}\n"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("B", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)66);
})

TEST(the_wrapping_operators_fold_at_the_width, {
    TEST_ASSERT_TRUE(check_src("u8 A = 200;\nu8 B = A +% 100;\n"
                               "fn i32 main() {\n    return cast(B, i32);\n}\n"));
    // `+%` wraps in two's complement in both build modes (D11.2).
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("B", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)44);
})

TEST(the_typed_division_traps_of_d6_13_are_compile_errors, {
    // Division by zero, `MIN / -1` and `MIN % -1` are runtime errors in every
    // build mode, so a typed constant that would trap is a compile error
    // (D4.6, D6.13, D11.3).
    TEST_ASSERT_FALSE(check_src("i32 A = -2147483648;\ni32 B = A / -1;\n"
                                "fn i32 main() {\n    return B;\n}\n"));
    TEST_ASSERT_TRUE(said("constant expression overflows i32"));
    TEST_ASSERT_FALSE(check_src("i32 A = -2147483648;\ni32 B = A % -1;\n"
                                "fn i32 main() {\n    return B;\n}\n"));
    TEST_ASSERT_TRUE(said("constant expression overflows i32"));
    TEST_ASSERT_FALSE(check_src("i32 A = 1;\ni32 ZERO = 0;\ni32 B = A / ZERO;\n"
                                "fn i32 main() {\n    return B;\n}\n"));
    TEST_ASSERT_TRUE(said("constant division by zero"));
    // The same operands one width up are ordinary arithmetic.
    TEST_ASSERT_TRUE(check_src("i64 A = -2147483648;\ni64 B = A / -1;\n"
                               "fn i32 main() {\n    return cast(B, i32);\n}\n"));
})

TEST(a_typed_shift_count_is_below_the_width, {
    TEST_ASSERT_FALSE(check_src("i32 A = 1;\ni32 B = A << 32;\n"
                                "fn i32 main() {\n    return B;\n}\n"));
    // A constant count at least the width of the left operand is a compile
    // error (D6.2).
    TEST_ASSERT_TRUE(said("shift count must be in 0..31 for i32"));
})

TEST(a_shift_discards_the_bits_it_pushes_out, {
    TEST_ASSERT_TRUE(check_src("i32 A = 1;\ni32 B = A << 31;\n"
                               "fn i32 main() {\n    return B;\n}\n"));
    // `1 << 31` on i32 is -2147483648 in both build modes (D6.2).
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("B", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)-2147483648);
})

TEST(a_local_immutable_variable_is_not_a_constant, {
    // Local immutable variables are not constants (D7.10), so they may not
    // size an array.
    TEST_ASSERT_FALSE(check_body("    i32 n = 3;\n    i32[n] a = {};\n    println(a[0]);"));
    TEST_ASSERT_TRUE(said("an array length must be a constant expression"));
})

TEST(a_mut_global_is_not_a_constant, {
    TEST_ASSERT_FALSE(check_src("i32 mut counter = 0;\ni32 START = counter;\n"
                                "fn i32 main() {\n    return START;\n}\n"));
    // No reads of `mut` globals in a module-level initializer (D7.10).
    TEST_ASSERT_TRUE(said("a module-level initializer must be a constant expression"));
})

TEST(a_call_is_not_a_constant_initializer, {
    TEST_ASSERT_FALSE(check_src("fn i32 four() {\n    return 4;\n}\ni32 SIZE = four();\n"
                                "fn i32 main() {\n    return SIZE;\n}\n"));
    TEST_ASSERT_TRUE(said("a module-level initializer must be a constant expression"));
})

TEST(a_module_level_initializer_takes_null_a_function_and_an_address, {
    // The extensions of D7.10: `null`, function names and `&` of a
    // module-level declaration.
    TEST_ASSERT_TRUE(check_src("i32 MAX = 64;\ni32* PTR = &MAX;\nnode* ROOT = null;\n"
                               "fn i32 id(i32 n) {\n    return n;\n}\n"
                               "fn i32(i32) F = id;\n"
                               "struct node {\n    i32 v;\n}\n"
                               "fn i32 main() {\n    return F(MAX);\n}\n"));
    TEST_ASSERT_EQ_STR(decl_type("PTR"), "i32*");
    TEST_ASSERT_EQ_STR(decl_type("F"), "fn i32(i32)");
})

// ---- sizeof, .len and cast (D3.15, D3.4, D3.14) -------------------------------------

TEST(sizeof_is_an_untyped_constant, {
    TEST_ASSERT_TRUE(check_body("    u8 b = sizeof(i32);\n    println(b);"));
    // `sizeof` yields an untyped integer constant, so it takes u8 here
    // (D3.15, D4.1).
    TEST_ASSERT_EQ_STR(init_type("b"), "u8");
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("b", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)4);
})

TEST(sizeof_needs_a_type, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    println(sizeof(x));"));
    // `sizeof(expr)` is an error (D3.15).
    TEST_ASSERT_TRUE(said("'x' is a local, not a type"));
    TEST_ASSERT_FALSE(check_body("    println(sizeof(void));"));
    TEST_ASSERT_TRUE(said("'sizeof' needs a sized type, not void"));
})

TEST(len_of_a_fixed_array_is_a_constant, {
    TEST_ASSERT_TRUE(check_body("    i32[3] a = {1, 2, 3};\n    i32[a.len] b = {4, 5, 6};\n"
                                "    println(a[0], b[2]);"));
    // `.len` of any expression of fixed-array type is a constant expression
    // (D3.4, D4.6).
    TEST_ASSERT_EQ_STR(decl_type("b"), "i32[3]");
})

TEST(len_of_a_span_is_not_a_constant, {
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    i32[s.len] b = {};\n    println(b[0]);"));
    TEST_ASSERT_TRUE(said("an array length must be a constant expression"));
})

TEST(a_constant_cast_has_run_time_semantics, {
    TEST_ASSERT_TRUE(check_body("    u8 t = cast(300, u8);\n    u32 u = cast(-1, u32);\n"
                                "    i32 o = cast(0x80000000, i32);\n    println(t, u, o);"));
    int64_t v = 0;
    // `cast(300, u8)` is 44 and `cast(0x80000000, i32)` is -2147483648
    // (D3.14, D4.4).
    TEST_ASSERT_TRUE(init_int("t", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)44);
    TEST_ASSERT_TRUE(init_int("o", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)-2147483648);
    const ast_node_t* d = node_in_main(AST_VAR_DECL, "u");
    uint64_t u = 0;
    TEST_ASSERT_TRUE(cv_to_u64(check_node_value(&checker, d->b), &u));
    TEST_ASSERT_EQ_UINT64(u, (uint64_t)4294967295ULL);
})

TEST(a_cast_operand_takes_its_default_type_first, {
    // `cast` is not a context: the operand takes its default type and is then
    // converted (D4.1).
    TEST_ASSERT_FALSE(check_body("    i32 x = cast(1 << 63, i32);"));
    TEST_ASSERT_TRUE(said("constant expression out of range"));
})

TEST(a_cast_to_bool_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n    bool b = cast(n, bool);\n    println(b);"));
    // Integer to `bool` is forbidden (D3.14).
    TEST_ASSERT_TRUE(said("cannot cast i32 to bool"));
})

TEST(a_cast_of_an_enum_to_an_integer_folds, {
    TEST_ASSERT_TRUE(check_src("enum color {\n    red,\n    green,\n    blue,\n}\n"
                               "i32[cast(color.blue, i32) + 1] table = {};\n"
                               "fn i32 main() {\n    return table[2];\n}\n"));
    // `cast(color.blue, i32) + 1` may size an array (D4.6).
    TEST_ASSERT_EQ_STR(decl_type("table"), "i32[3]");
})

TEST(null_is_not_a_cast_operand, {
    TEST_ASSERT_FALSE(check_body("    void* v = cast(null, void*);\n    println(v);"));
    TEST_ASSERT_TRUE(said("'null' is not a cast operand"));
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_const", argc, argv);
    TEST_RUN(an_integer_literal_takes_the_declared_type);
    TEST_RUN(a_constant_that_does_not_fit_its_context_is_refused);
    TEST_RUN(the_complement_of_zero_is_minus_one);
    TEST_RUN(an_integer_constant_does_not_become_a_char);
    TEST_RUN(a_char_constant_becomes_an_integer);
    TEST_RUN(a_char_literal_defaults_to_char);
    TEST_RUN(an_integer_constant_does_not_become_an_enum);
    TEST_RUN(an_integer_constant_does_not_become_a_bool);
    TEST_RUN(with_no_context_a_constant_is_i32_then_i64);
    TEST_RUN(a_constant_beyond_i64_needs_a_context);
    TEST_RUN(null_takes_a_pointer_type_from_its_context);
    TEST_RUN(null_outside_a_pointer_context_is_refused);
    TEST_RUN(a_shift_count_is_not_a_context_for_the_left_operand);
    TEST_RUN(a_constant_argument_takes_the_parameter_type);
    TEST_RUN(a_returned_constant_takes_the_return_type);
    TEST_RUN(every_integer_width_accepts_exactly_its_range);
    TEST_RUN(a_char_constant_fits_every_width_that_holds_its_code_point);
    TEST_RUN(untyped_arithmetic_folds_exactly);
    TEST_RUN(an_intermediate_may_leave_the_target_range);
    TEST_RUN(an_intermediate_outside_the_constant_range_is_refused);
    TEST_RUN(constant_division_by_zero_is_refused);
    TEST_RUN(bitwise_folding_uses_the_infinite_extension);
    TEST_RUN(untyped_shifts_fold_exactly);
    TEST_RUN(an_untyped_shift_count_is_at_most_63);
    TEST_RUN(comparisons_of_constants_fold_to_a_bool);
    TEST_RUN(a_shift_count_is_checked_without_a_folded_left_operand);
    TEST_RUN(untyped_wrapping_operators_fold_exactly);
    TEST_RUN(the_typed_division_traps_of_d6_13_are_compile_errors);
    TEST_RUN(a_typed_constant_expression_is_checked_at_compile_time);
    TEST_RUN(a_typed_constant_folds_with_the_declared_type);
    TEST_RUN(the_wrapping_operators_fold_at_the_width);
    TEST_RUN(a_typed_shift_count_is_below_the_width);
    TEST_RUN(a_shift_discards_the_bits_it_pushes_out);
    TEST_RUN(a_local_immutable_variable_is_not_a_constant);
    TEST_RUN(a_mut_global_is_not_a_constant);
    TEST_RUN(a_call_is_not_a_constant_initializer);
    TEST_RUN(a_module_level_initializer_takes_null_a_function_and_an_address);
    TEST_RUN(sizeof_is_an_untyped_constant);
    TEST_RUN(sizeof_needs_a_type);
    TEST_RUN(len_of_a_fixed_array_is_a_constant);
    TEST_RUN(len_of_a_span_is_not_a_constant);
    TEST_RUN(a_constant_cast_has_run_time_semantics);
    TEST_RUN(a_cast_operand_takes_its_default_type_first);
    TEST_RUN(a_cast_to_bool_is_refused);
    TEST_RUN(a_cast_of_an_enum_to_an_integer_folds);
    TEST_RUN(null_is_not_a_cast_operand);
    check_reset();
    done();
    TEST_EXIT();
}
