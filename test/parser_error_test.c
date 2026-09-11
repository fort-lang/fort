// Every diagnostic the parser can write, at the position it writes it. The
// parser's messages are the compiler's first contact with a mistyped program,
// so each one is pinned here by its full line, and a source that reports more
// than one after recovering (D14.2) pins them all, so that a cascade shows up
// as a line no one expected.
#include <stdint.h>

#include "ast.h"
#include "parser.h"
#include "parser_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources and the diagnostics they
// produce are the test data.

// ---- the bracket pairs ----------------------------------------------------

TEST(a_missing_open_parenthesis, {
    TEST_ASSERT_EQ_STR(parse_fails("fn i32 f) { }"), "t.ft:1:9: error: expected '(', found ')'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("if c) { }"),
                       "t.ft:2:4: error: expected '(', found identifier "
                       "'c'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("while c { }"),
                       "t.ft:2:7: error: expected '(', found identifier 'c'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("for i = 0;;) { }"),
                       "t.ft:2:5: error: expected '(', found identifier 'i'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("switch c { }"),
                       "t.ft:2:8: error: expected '(', found identifier 'c'\n");
    TEST_ASSERT_EQ_STR(expr_fails("cast x, i32)"),
                       "t.ft:1:14: error: expected '(', found identifier 'x'\n");
    TEST_ASSERT_EQ_STR(expr_fails("sizeof i32)"), "t.ft:1:16: error: expected '(', found 'i32'\n");
    TEST_ASSERT_EQ_STR(expr_fails("new i32)"), "t.ft:1:13: error: expected '(', found 'i32'\n");
    TEST_ASSERT_EQ_STR(type_fails("fn i32 i32)"), "t.ft:1:8: error: expected '(', found 'i32'\n");
})

TEST(a_missing_close_parenthesis, {
    TEST_ASSERT_EQ_STR(parse_fails("fn i32 f(i32 a { }"),
                       "t.ft:1:16: error: expected ')', found '{'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("if (c) { "),
                       "t.ft:3:2: error: expected '}', found end of file\n");
    // The step of a `for` is optional, so a missing `)` is reported where the
    // step would start.
    TEST_ASSERT_EQ_STR(stmt_fails("for (;;{ }"),
                       "t.ft:2:8: error: expected an expression, found '{'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("for (;; i++ { }"),
                       "t.ft:2:13: error: expected ')', found '{'\n");
    // An identifier directly followed by `{` is a struct literal wherever an
    // expression is expected (D6.5), so a `)` missing after one is reported
    // at the end of the literal.
    TEST_ASSERT_EQ_STR(stmt_fails("for (i32 v : xs { }"),
                       "t.ft:3:1: error: expected ')', found '}'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("while (c { }"), "t.ft:3:1: error: expected ')', found '}'\n");
    TEST_ASSERT_EQ_STR(expr_fails("f(1"), "t.ft:1:12: error: expected ')', found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("cast(x, i32"), "t.ft:1:20: error: expected ')', found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("sizeof(i32"), "t.ft:1:19: error: expected ')', found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(i32"), "t.ft:1:16: error: expected ')', found ';'\n");
})

TEST(a_missing_bracket_or_brace, {
    TEST_ASSERT_EQ_STR(expr_fails("a[0"), "t.ft:1:12: error: expected ']', found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("s[0..1"), "t.ft:1:15: error: expected ']', found ';'\n");
    TEST_ASSERT_EQ_STR(type_fails("i32[4"),
                       "t.ft:1:7: error: expected ']', found identifier 'x'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn void f() }"), "t.ft:1:13: error: expected '{', found '}'\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s i32 x; }"),
                       "t.ft:1:10: error: expected '{', found 'i32'\n");
    TEST_ASSERT_EQ_STR(parse_fails("enum e red }"),
                       "t.ft:1:8: error: expected '{', found identifier 'red'\n");
    // The `{` the switch never opened leaves its `}` to the function's block,
    // which therefore closes on line 2, so the `}` of line 3 is read where a
    // declaration was due (D14.2).
    TEST_ASSERT_EQ_STR(stmt_fails("switch (c) case 1: }"),
                       "t.ft:2:12: error: expected '{', found 'case'\n"
                       "t.ft:3:1: error: expected a type, found '}'\n");
    TEST_ASSERT_EQ_STR(expr_fails("point{1"), "t.ft:1:16: error: expected '}', found ';'\n");
})

// ---- the separators -------------------------------------------------------

TEST(a_missing_semicolon, {
    TEST_ASSERT_EQ_STR(parse_fails("i32 N = 4"),
                       "t.ft:1:10: error: expected ';', found end of file\n");
    TEST_ASSERT_EQ_STR(stmt_fails("f()"), "t.ft:3:1: error: expected ';', found '}'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("return 1"), "t.ft:3:1: error: expected ';', found '}'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("break"), "t.ft:3:1: error: expected ';', found '}'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("continue"), "t.ft:3:1: error: expected ';', found '}'\n");
    TEST_ASSERT_EQ_STR(parse_fails("extern fn void f()"),
                       "t.ft:1:19: error: expected ';', found end of file\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s { i32 x }"),
                       "t.ft:1:18: error: expected ';', found '}'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("for (i32 mut i = 0 i < 3; i++) { }"),
                       "t.ft:2:20: error: expected ';', found identifier 'i'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("for (; c ) { }"), "t.ft:2:10: error: expected ';', found ')'\n");
})

TEST(a_missing_equals_colon_or_comma, {
    TEST_ASSERT_EQ_STR(stmt_fails("i32 x 1;"),
                       "t.ft:2:7: error: expected '=', found integer literal\n");
    TEST_ASSERT_EQ_STR(expr_fails("point{.x 1}"),
                       "t.ft:1:18: error: expected '=', found integer "
                       "literal\n");
    TEST_ASSERT_EQ_STR(stmt_fails("switch (c) { case 1 f(); }"),
                       "t.ft:2:21: error: expected ':', found identifier 'f'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("switch (c) { default f(); }"),
                       "t.ft:2:22: error: expected ':', found identifier 'f'\n");
    TEST_ASSERT_EQ_STR(expr_fails("cast(x i32)"), "t.ft:1:16: error: expected ',', found 'i32'\n");
})

TEST(a_missing_identifier, {
    TEST_ASSERT_EQ_STR(parse_fails("fn i32 f() { } "),
                       "(module (fn (type (prim i32)) f (params) (block)))");
    TEST_ASSERT_EQ_STR(parse_fails("fn void f(i32) { }"),
                       "t.ft:1:14: error: expected an identifier, found ')'\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct { i32 x; }"),
                       "t.ft:1:8: error: expected an identifier, found '{'\n");
    TEST_ASSERT_EQ_STR(parse_fails("enum { red }"),
                       "t.ft:1:6: error: expected an identifier, found '{'\n");
    TEST_ASSERT_EQ_STR(parse_fails("import ;"),
                       "t.ft:1:8: error: expected an identifier, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_fails("import a as ;"),
                       "t.ft:1:13: error: expected an identifier, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_fails("import a.{b as };"),
                       "t.ft:1:16: error: expected an identifier, found '}'\n");
    TEST_ASSERT_EQ_STR(expr_fails("p->"), "t.ft:1:12: error: expected an identifier, found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("point{.= 1}"),
                       "t.ft:1:16: error: expected an identifier, found '='\n");
    // A `for` whose type is followed by no identifier is not a declaration,
    // so the init is parsed as an assignment or a call (grammar.md 7.3).
    TEST_ASSERT_EQ_STR(stmt_fails("for (i32 : xs) { }"),
                       "t.ft:2:6: error: expected an expression, found 'i32'\n");
})

// ---- the keyword forms ----------------------------------------------------

TEST(the_keyword_forms_that_need_a_keyword, {
    TEST_ASSERT_EQ_STR(parse_fails("extern void f();"),
                       "t.ft:1:8: error: expected 'fn', found 'void'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("do { } (c);"), "t.ft:2:8: error: expected 'while', found '('\n");
    TEST_ASSERT_EQ_STR(stmt_fails("do { } while (c)"),
                       "t.ft:3:1: error: expected ';', found '}'\n");
})

// ---- what is not a type and not an expression -----------------------------

TEST(a_token_that_starts_no_type, {
    TEST_ASSERT_EQ_STR(parse_fails("1 x = 0;"),
                       "t.ft:1:1: error: expected a type, found integer literal\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn void f(1 a) { }"),
                       "t.ft:1:11: error: expected a type, found integer literal\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s { 1 x; }"),
                       "t.ft:1:12: error: expected a type, found integer literal\n");
    TEST_ASSERT_EQ_STR(expr_fails("sizeof(1)"),
                       "t.ft:1:16: error: expected a type, found integer literal\n");
    TEST_ASSERT_EQ_STR(expr_fails("cast(x, 1)"),
                       "t.ft:1:17: error: expected a type, found integer literal\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(1)"),
                       "t.ft:1:13: error: expected a type, found integer literal\n");
    TEST_ASSERT_EQ_STR(type_fails("fn void(1)"),
                       "t.ft:1:9: error: expected a type, found integer literal\n");
})

TEST(a_token_that_starts_no_expression, {
    TEST_ASSERT_EQ_STR(expr_fails(";"), "t.ft:1:9: error: expected an expression, found ';'\n");
    TEST_ASSERT_EQ_STR(expr_fails("i32"), "t.ft:1:9: error: expected an expression, found 'i32'\n");
    TEST_ASSERT_EQ_STR(expr_fails("*"), "t.ft:1:10: error: expected an expression, found ';'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("return {};"),
                       "t.ft:2:8: error: expected an expression, found '{'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("f({});"),
                       "t.ft:2:3: error: expected an expression, found "
                       "'{'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("x = {};"),
                       "t.ft:2:5: error: expected an expression, found "
                       "'{'\n");
    TEST_ASSERT_EQ_STR(type_fails("i32[]"), "t.ft:1:5: error: expected an expression, found ']'\n");
})

// The reserved words of D2.4 are keywords the lexer knows and no production
// takes, so they end whatever was being parsed.
TEST(a_reserved_word_is_not_an_identifier, {
    TEST_ASSERT_EQ_STR(parse_fails("i32 const = 1;"),
                       "t.ft:1:5: error: 'const' is a reserved word\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct type { i32 x; }"),
                       "t.ft:1:8: error: 'type' is a reserved word\n");
})

// ---- one diagnostic per mistake, recovery in between (D14.2) -------------

TEST(later_errors_are_reported_too, {
    TEST_ASSERT_EQ_STR(parse_fails("i32 a = ;\ni32 b = ;\ni32 c = ;\n"),
                       "t.ft:1:9: error: expected an expression, found ';'\n"
                       "t.ft:2:9: error: expected an expression, found ';'\n"
                       "t.ft:3:9: error: expected an expression, found ';'\n");
    // The `}` of line 4 closes the `if`, so the function's block runs out of
    // tokens and says so at the end of the file.
    TEST_ASSERT_EQ_STR(parse_fails("fn void f() {\n    if (c) {\n    x = ;\n}\n"),
                       "t.ft:3:9: error: expected an expression, found ';'\n"
                       "t.ft:5:1: error: expected '}', found end of file\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s { i32 x }\nfn void f( { }\n"),
                       "t.ft:1:18: error: expected ';', found '}'\n"
                       "t.ft:2:12: error: expected a type, found '{'\n");
})

// A declaration that fails after the speculative parse chose it reports the
// declaration's error, not the expression's (grammar.md 7.1).
TEST(the_committed_branch_reports_its_own_error, {
    TEST_ASSERT_EQ_STR(stmt_fails("foo* p 1;"),
                       "t.ft:2:8: error: expected '=', found integer literal\n");
    TEST_ASSERT_EQ_STR(stmt_fails("foo[3] arr;"), "t.ft:2:11: error: expected '=', found ';'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("foo x = ;"),
                       "t.ft:2:9: error: expected an expression, found ';'\n");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser_error", argc, argv);
    TEST_RUN(a_missing_open_parenthesis);
    TEST_RUN(a_missing_close_parenthesis);
    TEST_RUN(a_missing_bracket_or_brace);
    TEST_RUN(a_missing_semicolon);
    TEST_RUN(a_missing_equals_colon_or_comma);
    TEST_RUN(a_missing_identifier);
    TEST_RUN(the_keyword_forms_that_need_a_keyword);
    TEST_RUN(a_token_that_starts_no_type);
    TEST_RUN(a_token_that_starts_no_expression);
    TEST_RUN(a_reserved_word_is_not_an_identifier);
    TEST_RUN(later_errors_are_reported_too);
    TEST_RUN(the_committed_branch_reports_its_own_error);
    parse_done();
    TEST_EXIT();
}
