// Tests parser setup and common module forms.
#include "parser.h"

#include <stdint.h>

#include "ast.h"
#include "common/parser_helpers.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources and the trees they parse
// to are the test data.

// ---- literals --------------------------------

TEST(int_literals_carry_their_magnitude, {
    TEST_ASSERT_EQ_STR(dump_expr("0"), "(int 0)");
    TEST_ASSERT_EQ_STR(dump_expr("42"), "(int 42)");
    TEST_ASSERT_EQ_STR(dump_expr("0xFF"), "(int 255)");
    TEST_ASSERT_EQ_STR(dump_expr("18446744073709551615"), "(int 18446744073709551615)");
})

TEST(char_string_bool_and_null_literals, {
    TEST_ASSERT_EQ_STR(dump_expr("'a'"), "(char 97)");
    TEST_ASSERT_EQ_STR(dump_expr("'\\0'"), "(char 0)");
    TEST_ASSERT_EQ_STR(dump_expr("\"hi\""), "(str \"hi\")");
    TEST_ASSERT_EQ_STR(dump_expr("\"\""), "(str \"\")");
    TEST_ASSERT_EQ_STR(dump_expr("true"), "(bool true)");
    TEST_ASSERT_EQ_STR(dump_expr("false"), "(bool false)");
    TEST_ASSERT_EQ_STR(dump_expr("null"), "(null)");
    TEST_ASSERT_EQ_STR(dump_expr("count"), "(ident count)");
})

// ---- precedence and associativity -----------------------------------------

TEST(multiplication_binds_tighter_than_addition, {
    TEST_ASSERT_EQ_STR(dump_expr("1 + 2 * 3"), "(binary + (int 1) (binary * (int 2) (int 3)))");
    TEST_ASSERT_EQ_STR(dump_expr("1 * 2 + 3"), "(binary + (binary * (int 1) (int 2)) (int 3))");
    TEST_ASSERT_EQ_STR(dump_expr("1 % 2 - 3"), "(binary - (binary % (int 1) (int 2)) (int 3))");
})

TEST(wrapping_operators_have_their_plain_precedence, {
    TEST_ASSERT_EQ_STR(dump_expr("a +% b *% c"),
                       "(binary +% (ident a) (binary *% (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a -% b + c"),
                       "(binary + (binary -% (ident a) (ident b)) (ident c))");
})

TEST(binary_operators_are_left_associative, {
    TEST_ASSERT_EQ_STR(dump_expr("1 - 2 - 3"), "(binary - (binary - (int 1) (int 2)) (int 3))");
    TEST_ASSERT_EQ_STR(dump_expr("a / b / c"),
                       "(binary / (binary / (ident a) (ident b)) (ident c))");
})

TEST(the_precedence_ladder_of_d6_1, {
    TEST_ASSERT_EQ_STR(dump_expr("a << b + c"),
                       "(binary << (ident a) (binary + (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a < b << c"),
                       "(binary < (ident a) (binary << (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a == b < c"),
                       "(binary == (ident a) (binary < (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a & b == c"),
                       "(binary & (ident a) (binary == (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a ^ b & c"),
                       "(binary ^ (ident a) (binary & (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a | b ^ c"),
                       "(binary | (ident a) (binary ^ (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a && b | c"),
                       "(binary && (ident a) (binary | (ident b) (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("a || b && c"),
                       "(binary || (ident a) (binary && (ident b) (ident c)))");
})

TEST(parentheses_override_precedence, {
    TEST_ASSERT_EQ_STR(dump_expr("(1 + 2) * 3"), "(binary * (binary + (int 1) (int 2)) (int 3))");
    TEST_ASSERT_EQ_STR(dump_expr("((x))"), "(ident x)");
})

// ---- unary and postfix -------------------------------------

TEST(the_five_unary_operators, {
    TEST_ASSERT_EQ_STR(dump_expr("-x"), "(unary - (ident x))");
    TEST_ASSERT_EQ_STR(dump_expr("!b"), "(unary ! (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("~m"), "(unary ~ (ident m))");
    TEST_ASSERT_EQ_STR(dump_expr("*p"), "(unary * (ident p))");
    TEST_ASSERT_EQ_STR(dump_expr("&x"), "(unary & (ident x))");
    TEST_ASSERT_EQ_STR(dump_expr("**pp"), "(unary * (unary * (ident pp)))");
})

TEST(unary_binds_tighter_than_binary_and_looser_than_postfix, {
    TEST_ASSERT_EQ_STR(dump_expr("-x * y"), "(binary * (unary - (ident x)) (ident y))");
    TEST_ASSERT_EQ_STR(dump_expr("&a[0]"), "(unary & (index (ident a) (int 0)))");
    TEST_ASSERT_EQ_STR(dump_expr("*p.f"), "(unary * (field (ident p) f))");
})

TEST(calls_take_their_arguments_in_order, {
    TEST_ASSERT_EQ_STR(dump_expr("f()"), "(call (ident f))");
    TEST_ASSERT_EQ_STR(dump_expr("f(1)"), "(call (ident f) (int 1))");
    TEST_ASSERT_EQ_STR(dump_expr("f(1, x + 1)"),
                       "(call (ident f) (int 1) (binary + (ident x) (int 1)))");
    TEST_ASSERT_EQ_STR(dump_expr("f(g(1))(2)"),
                       "(call (call (ident f) (call (ident g) (int 1))) (int 2))");
})

TEST(indexing_and_the_four_span_forms, {
    TEST_ASSERT_EQ_STR(dump_expr("a[i]"), "(index (ident a) (ident i))");
    TEST_ASSERT_EQ_STR(dump_expr("a[i + 1]"), "(index (ident a) (binary + (ident i) (int 1)))");
    TEST_ASSERT_EQ_STR(dump_expr("s[1..2]"), "(span (ident s) (int 1) (int 2))");
    TEST_ASSERT_EQ_STR(dump_expr("s[1..]"), "(span (ident s) (int 1) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("s[..2]"), "(span (ident s) nil (int 2))");
    TEST_ASSERT_EQ_STR(dump_expr("s[..]"), "(span (ident s) nil nil)");
})

TEST(field_and_arrow_access, {
    TEST_ASSERT_EQ_STR(dump_expr("p.x"), "(field (ident p) x)");
    TEST_ASSERT_EQ_STR(dump_expr("p->next"), "(arrow (ident p) next)");
    TEST_ASSERT_EQ_STR(dump_expr("p->next->value"), "(arrow (arrow (ident p) next) value)");
    TEST_ASSERT_EQ_STR(dump_expr("m.f(1)"), "(call (field (ident m) f) (int 1))");
    TEST_ASSERT_EQ_STR(dump_expr("a.b[0].c"), "(field (index (field (ident a) b) (int 0)) c)");
    TEST_ASSERT_EQ_STR(dump_expr("s.len"), "(field (ident s) len)");
    TEST_ASSERT_EQ_STR(dump_expr("out->len"), "(arrow (ident out) len)");
})

// ---- cast, sizeof and new -------------------------------------------------

TEST(cast_takes_an_expression_and_a_type, {
    TEST_ASSERT_EQ_STR(dump_expr("cast(x, i64)"), "(cast (ident x) (type (prim i64)))");
    TEST_ASSERT_EQ_STR(dump_expr("cast(p, u8 mut* own)"),
                       "(cast (ident p) (type (prim u8) mut (ptr own)))");
})

TEST(sizeof_takes_a_type_only, {
    TEST_ASSERT_EQ_STR(dump_expr("sizeof(i32)"), "(sizeof (type (prim i32)))");
    TEST_ASSERT_EQ_STR(dump_expr("sizeof(u8[4])"), "(sizeof (type (prim u8) (array (int 4))))");
    TEST_ASSERT_EQ_STR(dump_expr("sizeof(fn (i32) i32)"),
                       "(sizeof (type (fn-type (type (prim i32)) (type (prim i32)))))");
})

// new(T) allocates one T and new(T, n) a span of n.
TEST(new_with_and_without_a_count, {
    TEST_ASSERT_EQ_STR(dump_expr("new(node)"), "(new (type (name node)) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("new(i32, n)"), "(new (type (prim i32)) (ident n))");
    TEST_ASSERT_EQ_STR(dump_expr("new(i32[4])"), "(new (type (prim i32) (array (int 4))) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("new(u8, n * 2)"),
                       "(new (type (prim u8)) (binary * (ident n) (int 2)))");
})

// Inside new an own follows a `*` of the element type.
TEST(new_of_owning_pointers, {
    TEST_ASSERT_EQ_STR(dump_expr("new(node* own, n)"),
                       "(new (type (name node) (ptr own)) (ident n))");
    TEST_ASSERT_EQ_STR(dump_expr("new(node*)"), "(new (type (name node) (ptr)) nil)");
})

// ---- literals -------------------------------------------------------------

TEST(struct_literals_are_positional_or_designated, {
    TEST_ASSERT_EQ_STR(dump_expr("point{1, 2}"),
                       "(struct-lit (name point) (init (int 1) (int 2)))");
    TEST_ASSERT_EQ_STR(dump_expr("point{}"), "(struct-lit (name point) (init))");
    TEST_ASSERT_EQ_STR(dump_expr("point{.x = 1, .y = 2}"),
                       "(struct-lit (name point) (init (designator x (int 1))"
                       " (designator y (int 2))))");
    TEST_ASSERT_EQ_STR(dump_expr("math.vector{1}"),
                       "(struct-lit (name math vector) (init (int 1)))");
})

TEST(array_literals_carry_their_type, {
    TEST_ASSERT_EQ_STR(dump_expr("i32[3]{1, 2, 3}"),
                       "(array-lit (type (prim i32) (array (int 3)))"
                       " (init (int 1) (int 2) (int 3)))");
    TEST_ASSERT_EQ_STR(dump_expr("node*[2]{&a, &b}"),
                       "(array-lit (type (name node) (ptr) (array (int 2)))"
                       " (init (unary & (ident a)) (unary & (ident b))))");
    TEST_ASSERT_EQ_STR(dump_expr("string[2]{\"a\", \"b\"}"),
                       "(array-lit (type (string) (array (int 2)))"
                       " (init (str \"a\") (str \"b\")))");
})

TEST(brace_lists_allow_a_trailing_comma_and_nest, {
    TEST_ASSERT_EQ_STR(dump_expr("i32[2]{1, 2,}"),
                       "(array-lit (type (prim i32) (array (int 2))) (init (int 1) (int 2)))");
    TEST_ASSERT_EQ_STR(dump_expr("point[2]{{1, 2}, {3, 4}}"),
                       "(array-lit (type (name point) (array (int 2)))"
                       " (init (init (int 1) (int 2)) (init (int 3) (int 4))))");
    TEST_ASSERT_EQ_STR(dump_expr("point{.p = {1}}"),
                       "(struct-lit (name point) (init (designator p (init (int 1)))))");
})

// ---- what an expression is not --------------------------------------------

TEST(an_identifier_before_a_bracket_is_not_a_literal, {
    TEST_ASSERT_EQ_STR(dump_expr("foo[3]"), "(index (ident foo) (int 3))");
    TEST_ASSERT_EQ_STR(dump_expr("foo[3][1]"), "(index (index (ident foo) (int 3)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_expr("m.f[3]"), "(index (field (ident m) f) (int 3))");
})

TEST(a_missing_operand_is_reported_where_it_is_missing, {
    TEST_ASSERT_EQ_STR(expr_fails("1 +"), "t.ft:1:12: error: expected an expression, found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("f(1,)"),
                       "t.ft:1:13: error: expected an expression, found ')'\n");
    TEST_ASSERT_EQ_STR(expr_fails("(1"), "t.ft:1:11: error: expected ')', found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("a[1"), "t.ft:1:12: error: expected ']', found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("p."), "t.ft:1:11: error: expected an identifier, found ';'\n");
})

TEST(as_is_not_a_cast_and_assignment_is_not_an_expression, {
    TEST_ASSERT_EQ_STR(expr_fails("x as i32"), "t.ft:1:11: error: expected ';', found 'as'\n");
    TEST_ASSERT_EQ_STR(expr_fails("a = b"), "t.ft:1:11: error: expected ';', found '='\n");
})

TEST(positional_and_designated_initializers_do_not_mix, {
    TEST_ASSERT_EQ_STR(expr_fails("point{1, .y = 2}"),
                       "t.ft:1:18: error: positional and designated initializers do not mix\n");
    TEST_ASSERT_EQ_STR(expr_fails("point{.x = 1, 2}"),
                       "t.ft:1:23: error: positional and designated initializers do not mix\n");
})

// Features the C bootstrap deliberately lacks.
TEST(float_literals_are_not_supported, {
    TEST_ASSERT_EQ_STR(
        expr_fails("1.5"),
        "t.ft:1:9: error: not supported by the bootstrap compiler: float literals\n");
    TEST_ASSERT_EQ_STR(
        expr_fails("2.0e3 + 1"),
        "t.ft:1:9: error: not supported by the bootstrap compiler: float literals\n");
})

TEST(the_conditional_operator_is_not_supported, {
    TEST_ASSERT_EQ_STR(expr_fails("c ? 1 : 2"),
                       "t.ft:1:11: error: not supported by the bootstrap compiler: ?:\n");
    TEST_ASSERT_EQ_STR(expr_fails("a + (c ? 1 : 2)"),
                       "t.ft:1:16: error: not supported by the bootstrap compiler: ?:\n");
})

// ---- every operator token -------------------------------------------------

// Each binary operator builds its own node with its own spelling.
TEST(every_binary_operator_parses, {
    TEST_ASSERT_EQ_STR(dump_expr("a + b"), "(binary + (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a - b"), "(binary - (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a * b"), "(binary * (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a / b"), "(binary / (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a % b"), "(binary % (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a +% b"), "(binary +% (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a -% b"), "(binary -% (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a *% b"), "(binary *% (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a << b"), "(binary << (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a >> b"), "(binary >> (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a < b"), "(binary < (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a <= b"), "(binary <= (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a > b"), "(binary > (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a >= b"), "(binary >= (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a == b"), "(binary == (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a != b"), "(binary != (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a & b"), "(binary & (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a ^ b"), "(binary ^ (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a | b"), "(binary | (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a && b"), "(binary && (ident a) (ident b))");
    TEST_ASSERT_EQ_STR(dump_expr("a || b"), "(binary || (ident a) (ident b))");
})

// Comparisons do not chain into one node: `a < b < c` parses and is a type error later.
TEST(comparisons_parse_but_do_not_chain, {
    TEST_ASSERT_EQ_STR(dump_expr("a < b < c"),
                       "(binary < (binary < (ident a) (ident b)) (ident c))");
    TEST_ASSERT_EQ_STR(dump_expr("a == b == c"),
                       "(binary == (binary == (ident a) (ident b)) (ident c))");
})

// Unary operators stack and bind looser than every postfix form.
TEST(unary_operators_stack, {
    TEST_ASSERT_EQ_STR(dump_expr("!!b"), "(unary ! (unary ! (ident b)))");
    TEST_ASSERT_EQ_STR(dump_expr("- -x"), "(unary - (unary - (ident x)))");
    TEST_ASSERT_EQ_STR(dump_expr("~-x"), "(unary ~ (unary - (ident x)))");
    TEST_ASSERT_EQ_STR(dump_expr("*&x"), "(unary * (unary & (ident x)))");
    TEST_ASSERT_EQ_STR(dump_expr("&*p"), "(unary & (unary * (ident p)))");
    TEST_ASSERT_EQ_STR(dump_expr("!p->ok"), "(unary ! (arrow (ident p) ok))");
    TEST_ASSERT_EQ_STR(dump_expr("-f(x)"), "(unary - (call (ident f) (ident x)))");
})

// A postfix chain is a left-leaning spine, whatever the forms in it.
TEST(postfix_chains_lean_left, {
    TEST_ASSERT_EQ_STR(dump_expr("a.b.c"), "(field (field (ident a) b) c)");
    TEST_ASSERT_EQ_STR(dump_expr("f()()"), "(call (call (ident f)))");
    TEST_ASSERT_EQ_STR(dump_expr("a[0][1]"), "(index (index (ident a) (int 0)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_expr("p->a->b"), "(arrow (arrow (ident p) a) b)");
    TEST_ASSERT_EQ_STR(dump_expr("f(1).x[0]"),
                       "(index (field (call (ident f) (int 1)) x) (int 0))");
    TEST_ASSERT_EQ_STR(dump_expr("xs[0..2][1]"),
                       "(index (span (ident xs) (int 0) (int 2)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_expr("(*p).f"), "(field (unary * (ident p)) f)");
    TEST_ASSERT_EQ_STR(dump_expr("(*p)[0]"), "(index (unary * (ident p)) (int 0))");
})

// A whole expression of mixed precedence, as a program writes it.
TEST(a_mixed_expression_keeps_the_ladder, {
    TEST_ASSERT_EQ_STR(dump_expr("a + b * c - d / e"),
                       "(binary - (binary + (ident a) (binary * (ident b) (ident c)))"
                       " (binary / (ident d) (ident e)))");
    TEST_ASSERT_EQ_STR(dump_expr("i < n && xs[i] != 0"),
                       "(binary && (binary < (ident i) (ident n))"
                       " (binary != (index (ident xs) (ident i)) (int 0)))");
    TEST_ASSERT_EQ_STR(dump_expr("(a | b) & ~c"),
                       "(binary & (binary | (ident a) (ident b)) (unary ~ (ident c)))");
    TEST_ASSERT_EQ_STR(dump_expr("p != null && p->n > 0"),
                       "(binary && (binary != (ident p) (null))"
                       " (binary > (arrow (ident p) n) (int 0)))");
})

// The arguments and the members of a literal are expressions of their own.
TEST(arguments_and_members_are_full_expressions, {
    TEST_ASSERT_EQ_STR(dump_expr("f(a + 1, g(b), c[0])"),
                       "(call (ident f) (binary + (ident a) (int 1))"
                       " (call (ident g) (ident b)) (index (ident c) (int 0)))");
    TEST_ASSERT_EQ_STR(dump_expr("point{a + 1, -b}"),
                       "(struct-lit (name point) (init (binary + (ident a) (int 1))"
                       " (unary - (ident b))))");
    TEST_ASSERT_EQ_STR(dump_expr("i32[2]{f(1), x.y}"),
                       "(array-lit (type (prim i32) (array (int 2)))"
                       " (init (call (ident f) (int 1)) (field (ident x) y)))");
    TEST_ASSERT_EQ_STR(dump_expr("s[a + 1 .. b - 1]"),
                       "(span (ident s) (binary + (ident a) (int 1))"
                       " (binary - (ident b) (int 1)))");
})

// Casts and sizeof take full types, including function and marked ones.
TEST(cast_and_sizeof_take_any_type, {
    TEST_ASSERT_EQ_STR(dump_expr("cast(p, void*)"), "(cast (ident p) (type (void) (ptr)))");
    TEST_ASSERT_EQ_STR(dump_expr("cast(s, u8@)"), "(cast (ident s) (type (prim u8) (span)))");
    TEST_ASSERT_EQ_STR(dump_expr("cast(f, fn (i32) i32)"),
                       "(cast (ident f) (type (fn-type (type (prim i32)) (type (prim i32)))))");
    TEST_ASSERT_EQ_STR(dump_expr("cast(x + 1, i64)"),
                       "(cast (binary + (ident x) (int 1)) (type (prim i64)))");
    TEST_ASSERT_EQ_STR(dump_expr("sizeof(string)"), "(sizeof (type (string)))");
    TEST_ASSERT_EQ_STR(dump_expr("sizeof(node mut* own)"),
                       "(sizeof (type (name node) mut (ptr own)))");
})
// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser", argc, argv);
    TEST_RUN(int_literals_carry_their_magnitude);
    TEST_RUN(char_string_bool_and_null_literals);
    TEST_RUN(multiplication_binds_tighter_than_addition);
    TEST_RUN(wrapping_operators_have_their_plain_precedence);
    TEST_RUN(binary_operators_are_left_associative);
    TEST_RUN(the_precedence_ladder_of_d6_1);
    TEST_RUN(parentheses_override_precedence);
    TEST_RUN(the_five_unary_operators);
    TEST_RUN(unary_binds_tighter_than_binary_and_looser_than_postfix);
    TEST_RUN(calls_take_their_arguments_in_order);
    TEST_RUN(indexing_and_the_four_span_forms);
    TEST_RUN(field_and_arrow_access);
    TEST_RUN(cast_takes_an_expression_and_a_type);
    TEST_RUN(sizeof_takes_a_type_only);
    TEST_RUN(new_with_and_without_a_count);
    TEST_RUN(new_of_owning_pointers);
    TEST_RUN(struct_literals_are_positional_or_designated);
    TEST_RUN(array_literals_carry_their_type);
    TEST_RUN(brace_lists_allow_a_trailing_comma_and_nest);
    TEST_RUN(an_identifier_before_a_bracket_is_not_a_literal);
    TEST_RUN(a_missing_operand_is_reported_where_it_is_missing);
    TEST_RUN(as_is_not_a_cast_and_assignment_is_not_an_expression);
    TEST_RUN(positional_and_designated_initializers_do_not_mix);
    TEST_RUN(float_literals_are_not_supported);
    TEST_RUN(the_conditional_operator_is_not_supported);
    TEST_RUN(every_binary_operator_parses);
    TEST_RUN(comparisons_parse_but_do_not_chain);
    TEST_RUN(unary_operators_stack);
    TEST_RUN(postfix_chains_lean_left);
    TEST_RUN(a_mixed_expression_keeps_the_ladder);
    TEST_RUN(arguments_and_members_are_full_expressions);
    TEST_RUN(cast_and_sizeof_take_any_type);
    parse_done();
    TEST_EXIT();
}
