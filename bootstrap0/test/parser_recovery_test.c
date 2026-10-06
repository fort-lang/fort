// Tests parser recovery and diagnostic counts.
// Each broken construct reports once, and parsing continues at the next recovery point.
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

#include "ast.h"
#include "common/parser_helpers.h"
#include "diag.h"
#include "parser.h"
#include "str.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources and the diagnostics they
// produce are the test data.

// ---- one diagnostic per mistake -------------------------------------------

TEST(two_broken_statements_report_two_lines, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    i32 a = ;\n"
                                   "    i32 b = ;\n"
                                   "}\n"),
                       "t.ft:2:13: error: expected an expression, found ';'\n"
                       "t.ft:3:13: error: expected an expression, found ';'\n");
})

// The two markers of fail/mutability/004 and fail/ownership/009, which is why
// the corpus needs recovery at all.
TEST(two_broken_declarations_report_two_lines, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    i32 mut mut* p = &x;\n"
                                   "    i32 mut* mut mut q = &x;\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:13: error: a mut appears once in a type position\n"
                       "t.ft:3:18: error: a mut appears once in a type position\n");
})

// The parser skips a statement with a missing `;` at the next boundary.
// It then reads the statements after the mistake.
TEST(a_missing_semicolon_does_not_cascade, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() i32 {\n"
                                   "    i32 a = 1\n"
                                   "    return a;\n"
                                   "}\n"),
                       "t.ft:3:5: error: expected ';', found 'return'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() i32 {\n"
                                  "    i32 a = 1\n"
                                  "    return a;\n"
                                  "}\n"),
                       "(module (fn (type (prim i32)) f (params) (block "
                       "(error) (return (ident a)))))");
})

// The declaration after a broken one is read too, and the broken one is the
// only thing reported.
TEST(a_broken_declaration_does_not_cascade, {
    TEST_ASSERT_EQ_STR(parse_fails("i32 a = ;\n"
                                   "i32 b = 1;\n"),
                       "t.ft:1:9: error: expected an expression, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_dump("i32 a = ;\n"
                                  "i32 b = 1;\n"),
                       "(module (error) (var b (type (prim i32)) (int 1)))");
})

// A field recovers inside the struct body, so the fields after it are read.
TEST(a_broken_field_does_not_cascade, {
    TEST_ASSERT_EQ_STR(parse_fails("struct s {\n"
                                   "    i32 ;\n"
                                   "    i32 y;\n"
                                   "}\n"),
                       "t.ft:2:9: error: expected an identifier, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_dump("struct s {\n"
                                  "    i32 ;\n"
                                  "    i32 y;\n"
                                  "}\n"),
                       "(module (struct s (error) (field-decl (type (prim i32)) y)))");
})

// The statements of a case clause recover like a block's.
TEST(a_broken_statement_in_a_case_does_not_cascade, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    switch (c) {\n"
                                   "    case 1:\n"
                                   "        x = ;\n"
                                   "        g();\n"
                                   "    default:\n"
                                   "        h();\n"
                                   "    }\n"
                                   "}\n"),
                       "t.ft:4:13: error: expected an expression, found ';'\n");
})

// A clause that does not parse is skipped to the next one: the clauses of a switch are a recovery
// point of their own. The clauses after a broken one are read.
TEST(a_broken_case_clause_does_not_cascade, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    switch (c) {\n"
                                   "    case 1\n"
                                   "        return 1;\n"
                                   "    case 2:\n"
                                   "        return 2;\n"
                                   "    }\n"
                                   "}\n"),
                       "t.ft:4:9: error: expected ':', found 'return'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void {\n"
                                  "    switch (c) {\n"
                                  "    case 1 2:\n"
                                  "        g();\n"
                                  "    default:\n"
                                  "        h();\n"
                                  "    }\n"
                                  "}\n"),
                       "(module (fn (type (void)) f (params) (block (switch (ident c) (error) "
                       "(case default (block (call-stmt (call (ident h)))))))))");
})

// A skip inside a switch stops before the next clause. A statement that runs off the end of its
// clause costs that clause and not the one after it.
TEST(a_skip_inside_a_case_stops_before_the_next_clause, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    switch (c) {\n"
                                   "    case 1:\n"
                                   "        x = \n"
                                   "    case 2:\n"
                                   "        g();\n"
                                   "    }\n"
                                   "}\n"),
                       "t.ft:5:5: error: expected an expression, found 'case'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void {\n"
                                  "    switch (c) {\n"
                                  "    case 1:\n"
                                  "        x = \n"
                                  "    default:\n"
                                  "        g();\n"
                                  "    }\n"
                                  "}\n"),
                       "(module (fn (type (void)) f (params) (block (switch (ident c) "
                       "(case (labels (int 1)) (block (error))) "
                       "(case default (block (call-stmt (call (ident g)))))))))");
})

// The `}` of a brace initializer that a `;` follows is consumed with it: no block is followed by a
// `;`. The pair ends the statement the skip is dropping and the statements after it are read.
TEST(a_skip_ends_at_the_brace_and_semicolon_of_an_initializer, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    point p = {.x = 1, 2};\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:24: error: positional and designated initializers do not mix\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn main() i32 {\n"
                                  "    point p = {.x = 1, 2};\n"
                                  "    return 0;\n"
                                  "}\n"),
                       "(module (fn (type (prim i32)) main (params) (block "
                       "(error) (return (int 0)))))");
})

// ---- the missing brace every edit passes through --------------------------

// The `{` of a function body may be missing without the body being read as declarations: the header
// before it is complete. It is reported once and the statements are read as statements.
TEST(a_body_without_its_opening_brace_is_read_as_a_body, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32\n"
                                   "    i32 mut n = 1;\n"
                                   "    n = n + 1;\n"
                                   "    return n;\n"
                                   "}\n"),
                       "t.ft:2:5: error: expected '{', found 'i32'\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s\n"
                                   "    i32 x;\n"
                                   "}\n"),
                       "t.ft:2:5: error: expected '{', found 'i32'\n");
    TEST_ASSERT_EQ_STR(parse_fails("enum e\n"
                                   "    red, green\n"
                                   "}\n"),
                       "t.ft:2:5: error: expected '{', found identifier 'red'\n");
})

// A block that runs to a top-level declaration is reported once, there, and
// the declarations after it are parsed as declarations.
TEST(an_unclosed_block_reports_at_the_next_declaration, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    g();\n"
                                   "\n"
                                   "fn h() void {\n"
                                   "    i();\n"
                                   "}\n"),
                       "t.ft:4:1: error: expected '}', found 'fn'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void {\n"
                                  "    g();\n"
                                  "\n"
                                  "fn h() void {\n"
                                  "    i();\n"
                                  "}\n"),
                       "(module (fn (type (void)) f (params) (block (call-stmt (call (ident g))))) "
                       "(fn (type (void)) h (params) (block (call-stmt (call (ident i))))))");
})

// A `fn` at statement level is the base of a function type. It ends no block: the two are told
// apart by the speculative parse of the grammar.
TEST(a_function_type_at_statement_level_is_not_a_declaration, {
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void {\n"
                                  "    fn (i32) i32 op = add;\n"
                                  "}\n"),
                       "(module (fn (type (void)) f (params) (block "
                       "(var op (type (fn-type (type (prim i32)) (type (prim i32)))) "
                       "(ident add)))))");
})

// An unclosed struct body and an unclosed enum body end at the declaration
// that follows them, like a block.
TEST(an_unclosed_struct_or_enum_reports_at_the_next_declaration, {
    TEST_ASSERT_EQ_STR(parse_fails("struct s {\n"
                                   "    i32 x;\n"
                                   "\n"
                                   "fn main() i32 {\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:4:1: error: expected '}', found 'fn'\n");
    TEST_ASSERT_EQ_STR(parse_fails("enum e { red, green\n"
                                   "\n"
                                   "fn main() i32 {\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:3:1: error: expected '}', found 'fn'\n");
})

// Nested unclosed blocks at file end report one missing `}`.
// Each enclosing construct reaches the same position.
TEST(an_unclosed_block_at_the_end_of_the_file_reports_once, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    if (c) {\n"
                                   "        while (d) {\n"),
                       "t.ft:4:1: error: expected '}', found end of file\n");
})

// It answers about the tokens ahead and not about the unwind. The tree is what says the declaration
// survived.
TEST(a_declaration_that_follows_a_failed_one_is_still_a_declaration, {
    TEST_ASSERT_EQ_STR(parse_fails("i32 x =\n"
                                   "fn h() void { g(); }\n"),
                       "t.ft:2:1: error: expected an expression, found 'fn'\n");
    TEST_ASSERT_EQ_STR(parse_dump("i32 x =\n"
                                  "fn h() void { g(); }\n"),
                       "(module (error) (fn (type (void)) h (params) "
                       "(block (call-stmt (call (ident g))))))");
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void\n"
                                   "fn h() void { }\n"),
                       "t.ft:2:1: error: expected '{', found 'fn'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void\n"
                                  "fn h() void { }\n"),
                       "(module (fn (type (void)) f (params) (block)) "
                       "(fn (type (void)) h (params) (block)))");
    TEST_ASSERT_EQ_STR(parse_fails("enum e { red, green\n"
                                   "fn main() i32 { return 0; }\n"),
                       "t.ft:2:1: error: expected '}', found 'fn'\n");
    TEST_ASSERT_EQ_STR(parse_dump("enum e { red, green\n"
                                  "fn main() i32 { return 0; }\n"),
                       "(module (enum e (member red nil) (member green nil)) "
                       "(fn (type (prim i32)) main (params) (block (return (int 0)))))");
})

// Losing one of them would be silent, so the tree is what this test reads.
TEST(a_body_without_its_brace_keeps_every_statement, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32\n"
                                   "    point p = {};\n"
                                   "    q = new(i32);\n"
                                   "    x = i32[2]{1, 2};\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:5: error: expected '{', found identifier 'point'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn main() i32\n"
                                  "    point p = {};\n"
                                  "    q = new(i32);\n"
                                  "    x = i32[2]{1, 2};\n"
                                  "    return 0;\n"
                                  "}\n"),
                       "(module (fn (type (prim i32)) main (params) (block "
                       "(var p (type (name point)) (init)) "
                       "(assign = (ident q) (new (type (prim i32)) nil)) "
                       "(assign = (ident x) (array-lit (type (prim i32) (array (int 2))) "
                       "(init (int 1) (int 2)))) "
                       "(return (int 0)))))");
})

// ---- the mid-edit shapes --------------------------------------------------

// The shapes a buffer passes through while it is being typed: each costs one
// diagnostic, at the token the parser stopped on.
TEST(a_half_typed_construct_reports_once, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    if (\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:3:5: error: expected an expression, found 'return'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    g(1,\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:3:5: error: expected an expression, found 'return'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    f());\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:8: error: expected ';', found ')'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    for (i32 mut i = 0 i < 3; i++) {\n"
                                   "        f();\n"
                                   "    }\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:24: error: expected ';', found identifier 'i'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    switch (c) {\n"
                                   "    case 1 f();\n"
                                   "    }\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:3:12: error: expected ':', found identifier 'f'\n");
})

// A `}` inside a bracket the construct left open closes nothing. It is skipped with the statement
// and the block keeps its own `}`. A `}` that closes the block is left to it, even though the
// construct has a bracket open. This is because a half-typed call followed by the block's real
// brace is what an edit in progress looks like.
TEST(a_brace_inside_an_unclosed_call_goes_with_the_statement, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    g(});\n"
                                   "    h();\n"
                                   "    i();\n"
                                   "}\n"),
                       "t.ft:2:7: error: expected an expression, found '}'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void {\n"
                                  "    g(});\n"
                                  "    h();\n"
                                  "    i();\n"
                                  "}\n"),
                       "(module (fn (type (void)) f (params) (block (error) "
                       "(call-stmt (call (ident h))) (call-stmt (call (ident i))))))");
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    g(1,\n"
                                   "}\n"
                                   "fn h() void { }\n"),
                       "t.ft:3:1: error: expected an expression, found '}'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void {\n"
                                  "    g(1,\n"
                                  "}\n"
                                  "fn h() void { }\n"),
                       "(module (fn (type (void)) f (params) (block (error))) "
                       "(fn (type (void)) h (params) (block)))");
})

// A switch body holds clauses and nothing else. A token that is no clause is reported and skipped
// there rather than handed to the block around it. It would read the clauses after it as
// statements.
TEST(a_switch_body_holds_clauses_only, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    switch (n) {\n"
                                   "    cse 1:\n"
                                   "        return 1;\n"
                                   "    case 2:\n"
                                   "        return 2;\n"
                                   "    default:\n"
                                   "        return 0;\n"
                                   "    }\n"
                                   "}\n"),
                       "t.ft:3:5: error: expected 'case' or 'default', "
                       "found identifier 'cse'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() void {\n"
                                  "    switch (n) {\n"
                                  "    x = 1;\n"
                                  "    case 2:\n"
                                  "        g();\n"
                                  "    }\n"
                                  "}\n"),
                       "(module (fn (type (void)) f (params) (block (switch (ident n) (error) "
                       "(case (labels (int 2)) (block (call-stmt (call (ident g)))))))))");
})

// ---- speculation ----------------------------------------------------------

TEST(an_error_under_a_speculation_is_reported_once, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    foo* p 1;\n"
                                   "}\n"),
                       "t.ft:2:12: error: expected '=', found integer literal\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    node* mut own p = null;\n"
                                   "}\n"),
                       "t.ft:2:15: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    for (node* mut own p = x; c; i++) { }\n"
                                   "}\n"),
                       "t.ft:2:20: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
})

// The statement after one that a speculation gave up on is parsed normally:
// the rewind leaves no failure behind.
TEST(a_speculation_during_an_unwind_leaves_no_trace, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    i32 a = ;\n"
                                   "    foo* p = null;\n"
                                   "}\n"),
                       "t.ft:2:13: error: expected an expression, found ';'\n");
})

// ---- the cap and the dedupe -----------------------------------------------

// The number of diagnostic lines in `src`.
static uint64_t diag_lines(const char* src) {
    const char* text = parse_fails(src);
    uint64_t lines = 0;
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

// Twenty errors are reported per file and the rest are silent, but the parse
// goes on, so the tree still holds every declaration that parsed.
TEST(the_twenty_first_error_is_not_reported, {
    sb_t src;
    sb_init(&src);
    for (uint64_t i = 0; i < 25; i++) {
        sb_append(&src, "i32 a = ;\n");
    }
    sb_append(&src, "i32 last = 1;\n");
    TEST_ASSERT_EQ_UINT64(diag_lines(sb_cstr(&src)), (uint64_t)20);
    const ast_node_t* mod = parse_text(sb_cstr(&src));
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_UINT64(ast_len(mod), (uint64_t)26);
    const ast_node_t* last = ast_child(mod, 25);
    TEST_ASSERT_EQ_STR(dumped(last), "(var last (type (prim i32)) (int 1))");
    sb_free(&src);
})

// Two errors that start at the same token are one mistake, so the second is
// dropped; two that start at different tokens are both reported.
TEST(two_errors_at_one_position_are_reported_once, {
    TEST_ASSERT_EQ_UINT64(diag_lines("fn f() i32 {\n    switch (c) {\n    case 1:\n"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(diag_lines("i32 a = ;\ni32 b = ;\n"), (uint64_t)2);
})

// ---- the tree a recovered parse hands back --------------------------------

// The tree is never NULL, whatever was reported: a caller asks whether the
// file parsed by comparing diag_count().
TEST(a_file_with_errors_still_yields_a_tree, {
    const ast_node_t* mod = parse_text("fn void f( {\n"
                                       "}\n"
                                       "fn g() void {\n"
                                       "    h();\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    TEST_ASSERT_EQ_STR(dumped(ast_child(mod, 1)),
                       "(fn (type (void)) g (params) (block (call-stmt (call (ident h)))))");
})

// A later pass reads it as "nothing was understood here".
TEST(an_error_node_covers_the_skipped_region_and_has_no_children, {
    const ast_node_t* mod = parse_text("fn f() void {\n"
                                       "    i32 a = ;\n"
                                       "    g();\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* err = ast_child(ast_child(mod, 0)->b, 0);
    TEST_ASSERT_EQ_INT32((int32_t)err->kind, (int32_t)AST_ERROR);
    TEST_ASSERT_EQ_UINT64(ast_len(err), (uint64_t)0);
    TEST_ASSERT_NULL(err->a);
    TEST_ASSERT_NULL(err->b);
    TEST_ASSERT_NULL(err->c);
    TEST_ASSERT_NULL(err->d);
    TEST_ASSERT_EQ_STR(range_of(err), "2:5-2:14");
    TEST_ASSERT_EQ_STR(text_of(err), "i32 a = ;");
    TEST_ASSERT_EQ_STR(dumped(err), "(error)");
})

// ---- the boundaries ------------------------------------------------------

// A lexical error costs its line and lexing resumes at the next one. The parser runs on the rest of
// the file and reports the mistake after the bad literal. The line the lexer dropped yields no
// syntax error, since none of its tokens reached the parser.
TEST(a_lexical_error_costs_its_line_and_the_parser_sees_the_rest, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    i32 a = 0xZ;\n"
                                   "    i32 b = ;\n"
                                   "}\n"),
                       "t.ft:2:13: error: hex literal needs at least one digit\n"
                       "t.ft:3:13: error: expected an expression, found ';'\n");
})

// An unclosed string literal on line 3 leaves the declarations after it
// parsed: what an editor needs while a literal is half typed.
TEST(a_file_with_an_unclosed_string_still_parses_the_lines_after_it, {
    const ast_node_t* mod = parse_text("fn one() i32 {\n"
                                       "    string s = \"abc;\n"
                                       "    return 1;\n"
                                       "}\n"
                                       "fn two() i32 {\n"
                                       "    return 2;\n"
                                       "}\n");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    TEST_ASSERT_EQ_STR(parse_diags(), "t.ft:2:16: error: unterminated string literal\n");
    TEST_ASSERT_EQ_STR(dumped(mod),
                       "(module (fn (type (prim i32)) one (params) (block (return (int 1)))) "
                       "(fn (type (prim i32)) two (params) (block (return (int 2)))))");
})

// An empty file reports nothing, while one stray token reports once.
// Recovery must consume the stray token before file end.
TEST(an_empty_file_and_a_one_token_file, {
    TEST_ASSERT_EQ_STR(parse_dump(""), "(module)");
    TEST_ASSERT_EQ_STR(parse_fails("}"), "t.ft:1:1: error: expected a type, found '}'\n");
    TEST_ASSERT_EQ_STR(parse_fails(";"), "t.ft:1:1: error: expected a type, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_fails("i32"),
                       "t.ft:1:4: error: expected an identifier, found end of file\n");
})

// A nesting-limit error unwinds to the nearest recovery point.
// Recovery skips the complete nest and reports once.
TEST(nesting_past_the_limit_is_reported_once, {
    sb_t src;
    sb_init(&src);
    sb_append(&src, "fn f() void ");
    for (uint64_t i = 0; i < 257; i++) {
        sb_push(&src, '{');
    }
    for (uint64_t i = 0; i < 257; i++) {
        sb_push(&src, '}');
    }
    sb_push(&src, '\n');
    TEST_ASSERT_EQ_UINT64(diag_lines(sb_cstr(&src)), (uint64_t)1);
    TEST_ASSERT_EQ_STR(parse_fails(sb_cstr(&src)), "t.ft:1:270: error: nesting deeper than 256\n");
    sb_free(&src);
})

// Exactly twenty errors are all reported; the twenty-first is not.
TEST(the_cap_is_twenty_errors, {
    sb_t src;
    sb_init(&src);
    for (uint64_t i = 0; i < 20; i++) {
        sb_append(&src, "i32 a = ;\n");
    }
    TEST_ASSERT_EQ_UINT64(diag_lines(sb_cstr(&src)), (uint64_t)20);
    sb_append(&src, "i32 a = ;\n");
    TEST_ASSERT_EQ_UINT64(diag_lines(sb_cstr(&src)), (uint64_t)20);
    sb_free(&src);
})

// The budget of twenty is the file's, not the parser's: what the lexer
// reported is already spent when the parse begins.
TEST(the_cap_is_shared_with_the_lexer, {
    sb_t src;
    sb_init(&src);
    for (uint64_t i = 0; i < 15; i++) {
        sb_append(&src, "#\n");         // a lexical error, one per line
        sb_append(&src, "i32 a = ;\n"); // a syntax error
    }
    TEST_ASSERT_EQ_UINT64(diag_lines(sb_cstr(&src)), (uint64_t)20);
    // The lexer runs first and spends fifteen of the twenty, so the parser
    // reports five of its fifteen.
    const char* text = parse_fails(sb_cstr(&src));
    uint64_t lexical = 0;
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '#') {
            lexical++;
        }
    }
    TEST_ASSERT_EQ_UINT64(lexical, (uint64_t)15);
    sb_free(&src);
})

// Past the cap the file is still lexed and parsed whole, so the tree covers
// the declarations after the twentieth diagnostic.
TEST(a_declaration_after_the_cap_is_still_in_the_tree, {
    sb_t src;
    sb_init(&src);
    for (uint64_t i = 0; i < 25; i++) {
        sb_append(&src, "#\n");
    }
    sb_append(&src, "i32 last = 1;\n");
    TEST_ASSERT_EQ_UINT64(diag_lines(sb_cstr(&src)), (uint64_t)20);
    const ast_node_t* mod = parse_text(sb_cstr(&src));
    TEST_ASSERT_EQ_UINT64(ast_len(mod), (uint64_t)1);
    TEST_ASSERT_EQ_STR(dumped(ast_child(mod, 0)), "(var last (type (prim i32)) (int 1))");
    sb_free(&src);
})

// ---- where a skip stops ---------------------------------------------------

// The `}` of a block the failed construct opened is consumed with it. The statement after that
// block is read as a statement and not as a leftover.
TEST(a_skip_consumes_the_block_of_the_failed_statement, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() i32 {\n"
                                   "    if c) { g(); }\n"
                                   "    h();\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:8: error: expected '(', found identifier 'c'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn f() i32 {\n"
                                  "    if c) { g(); }\n"
                                  "    h();\n"
                                  "    return 0;\n"
                                  "}\n"),
                       "(module (fn (type (prim i32)) f (params) (block (error) "
                       "(call-stmt (call (ident h))) (return (int 0)))))");
})

// An import that does not parse is skipped like any declaration, and the
// imports and declarations after it are read.
TEST(a_broken_import_does_not_stop_the_imports, {
    TEST_ASSERT_EQ_STR(parse_fails("import ;\n"
                                   "import std.io;\n"
                                   "fn main() i32 { return 0; }\n"),
                       "t.ft:1:8: error: expected an identifier, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_dump("import ;\n"
                                  "import std.io;\n"
                                  "fn main() i32 { return 0; }\n"),
                       "(module (error) (import (path std io) nil) "
                       "(fn (type (prim i32)) main (params) (block (return (int 0)))))");
})

// A late import reports before the parser consumes a token.
// Recovery then consumes one token and continues with the next declaration.
TEST(a_late_import_is_reported_once_and_skipped, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 { return 0; }\n"
                                   "import std.io;\n"
                                   "fn g() i32 { return 1; }\n"),
                       "t.ft:2:1: error: an import comes before every declaration\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn main() i32 { return 0; }\n"
                                  "import std.io;\n"
                                  "fn g() i32 { return 1; }\n"),
                       "(module (fn (type (prim i32)) main (params) (block (return (int 0)))) "
                       "(error) (fn (type (prim i32)) g (params) (block (return (int 1)))))");
})

// The `}` of the enclosing block stays that block's and the statements after the broken one are
// read.
TEST(an_unclosed_brace_initializer_costs_one_statement, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    point p = {1, ;\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:19: error: expected an expression, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn main() i32 {\n"
                                  "    point p = {1, ;\n"
                                  "    return 0;\n"
                                  "}\n"),
                       "(module (fn (type (prim i32)) main (params) (block "
                       "(error) (return (int 0)))))");
})

// The error node of each recovery point covers the region its skip dropped.
TEST(every_recovery_point_records_what_it_skipped, {
    const ast_node_t* mod = parse_text("i32 a = ;\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(ast_child(mod, 0)), "i32 a = ;");
    mod = parse_text("struct s {\n    i32 ;\n    i32 y;\n}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(ast_child(ast_child(mod, 0), 0)), "i32 ;");
    mod = parse_text("fn f() void {\n    switch (c) {\n    case 1:\n        x = ;\n    }\n}\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* clause = ast_child(ast_child(ast_child(mod, 0)->b, 0), 0);
    TEST_ASSERT_EQ_STR(text_of(ast_child(clause->a, 0)), "x = ;");
})

// A bracket a construct leaves open costs that construct and the statements up to the next
// boundary.
TEST(an_unclosed_bracket_costs_its_construct_and_no_more, {
    TEST_ASSERT_EQ_STR(parse_fails("fn main() i32 {\n"
                                   "    g(1, 2;\n"
                                   "    h();\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:2:11: error: expected ')', found ';'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn main() i32 {\n"
                                  "    g(1, 2;\n"
                                  "    h();\n"
                                  "    return 0;\n"
                                  "}\n"),
                       "(module (fn (type (prim i32)) main (params) (block "
                       "(error) (return (int 0)))))");
    TEST_ASSERT_EQ_STR(parse_fails("i32 a = {1, 2;\n"
                                   "fn main() i32 {\n"
                                   "    return 0;\n"
                                   "}\n"),
                       "t.ft:1:14: error: expected '}', found ';'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n"
                                   "    x = 1;\n"
                                   "}\n"
                                   "}\n"
                                   "fn g() void { }\n"),
                       "t.ft:4:1: error: expected a type, found '}'\n");
})

// ---- the fail corpus ------------------------------------------------------

enum { CORPUS_PATH_CAP = 512, CORPUS_CHUNK = 4096, CORPUS_FILES = 311 };

// The files walked, the source of the one being read, and the lines that were
// reported on without an annotation, one per line.
static uint64_t corpus_files;
static sb_t corpus_src;
static sb_t corpus_report;

// Whether line `line` of `src`, the first being 1, holds `needle`.
static bool line_has(const char* src, uint32_t line, const char* needle) {
    uint32_t at = 1;
    uint64_t i = 0;
    while (src[i] != '\0' && at < line) {
        if (src[i] == '\n') {
            at++;
        }
        i++;
    }
    const uint64_t len = strlen(needle);
    while (src[i] != '\0' && src[i] != '\n') {
        if (strncmp(&src[i], needle, len) == 0) {
            return true;
        }
        i++;
    }
    return false;
}

// Returns a diagnostic line number, or 0 when no position exists.
// Writes the next diagnostic offset to `*next`.
static uint32_t diag_line_of(const char* text, uint64_t* next) {
    uint64_t i = 0;
    while (text[i] != '\0' && text[i] != ':' && text[i] != '\n') {
        i++;
    }
    uint32_t line = 0;
    if (text[i] == ':') {
        i++;
        while (text[i] >= '0' && text[i] <= '9') {
            line = (line * 10) + (uint32_t)(text[i] - '0');
            i++;
        }
    }
    while (text[i] != '\0' && text[i] != '\n') {
        i++;
    }
    *next = text[i] == '\n' ? i + 1 : i;
    return line;
}

// Reads `path` into `out`, whose contents are then the NUL-terminated source;
// false when the file cannot be read.
static bool read_source(const char* path, sb_t* out) {
    sb_clear(out);
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    char chunk[CORPUS_CHUNK];
    size_t got = fread(chunk, 1, sizeof chunk, file);
    while (got > 0) {
        sb_append_str(out, str_from_range(chunk, (uint64_t)got));
        got = fread(chunk, 1, sizeof chunk, file);
    }
    TEST_UNUSED(fclose(file));
    return true;
}

// Parses one corpus file and records diagnostics without `//! error:` annotations.
// An `//! error-any:` annotation accepts any source position.
static void check_corpus_file(const char* path) {
    if (!read_source(path, &corpus_src)) {
        sb_append(&corpus_report, path);
        sb_append(&corpus_report, ": cannot be read\n");
        return;
    }
    corpus_files++;
    const char* src = sb_cstr(&corpus_src);
    TEST_UNUSED(parse_text(src));
    const bool any = strstr(src, "//! error-any:") != NULL;
    const char* diags = parse_diags();
    uint64_t i = 0;
    while (diags[i] != '\0') {
        uint64_t next = 0;
        const uint32_t line = diag_line_of(&diags[i], &next);
        // A feature the bootstrap lacks is reported wherever the test writes it and is not a
        // cascade. It is how the harness judges those tests too.
        const char* hit = strstr(&diags[i], "not supported by the bootstrap compiler");
        const bool unsupported = hit != NULL && hit < &diags[i + next];
        if (!any && !unsupported && !line_has(src, line, "//! error:")) {
            sb_append(&corpus_report, path);
            sb_push(&corpus_report, ':');
            msg_uint(&corpus_report, line);
            sb_append(&corpus_report, ": unannotated ");
            sb_append_str(&corpus_report, str_from_range(&diags[i], next - 1));
            sb_push(&corpus_report, '\n');
        }
        i += next;
    }
}

// Walks `dir` and checks every `.ft` file below it, directory tests included.
static void walk_corpus(const char* dir) {
    DIR* open = opendir(dir);
    if (open == NULL) {
        sb_append(&corpus_report, dir);
        sb_append(&corpus_report, ": cannot be opened\n");
        return;
    }
    const struct dirent* entry = readdir(open);
    while (entry != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char child[CORPUS_PATH_CAP];
            TEST_UNUSED(snprintf(child, sizeof child, "%s/%s", dir, entry->d_name));
            struct stat info;
            const size_t len = strlen(entry->d_name);
            if (stat(child, &info) != 0) {
                sb_append(&corpus_report, child);
                sb_append(&corpus_report, ": cannot be stat'd\n");
            } else if (S_ISDIR(info.st_mode)) {
                walk_corpus(child);
            } else if (len > 3 && strcmp(&entry->d_name[len - 3], ".ft") == 0) {
                check_corpus_file(child);
            }
        }
        entry = readdir(open);
    }
    TEST_UNUSED(closedir(open));
}

// Every file of test/lang/fail reports only on the lines its annotations name,
// so a recovery that cascades over the corpus fails here. The converse does
// not hold yet: most annotations name semantic errors, which wait for the
// checker.
TEST(the_fail_corpus_reports_only_on_annotated_lines, {
    corpus_files = 0;
    sb_clear(&corpus_report);
    walk_corpus(FORT_LANG_DIR "/fail");
    TEST_ASSERT_EQ_STR(sb_cstr(&corpus_report), "");
    // The exact count, so that a file that stops being walked is noticed. It is a literal on
    // purpose: deriving it with this walker would be circular. The name of the operand carries what
    // to do about it, since `#val` is what the failing assertion prints.
    const uint64_t raise_corpus_files_when_you_add_a_fail_test = corpus_files;
    TEST_ASSERT_EQ_UINT64(raise_corpus_files_when_you_add_a_fail_test, (uint64_t)CORPUS_FILES);
})

// The corpus files whose syntax errors are all reported, with the number of diagnostics each must
// produce. The walk above says only that nothing unannotated was reported, which a parser that gave
// up after the first error would also satisfy. These counts say that recovery happens.
static const char* const MULTI_ERROR_FILES[] = {
    "/fail/mutability/004_doubled_mut_marker.ft",
    "/fail/mutability/006_marker_before_base_type.ft",
    "/fail/ownership/009_doubled_own_marker.ft",
    "/fail/ownership/010_own_non_reference.ft",
    "/fail/ownership/022_own_before_base_type.ft",
    "/fail/declarations/007_two_missing_initializers.ft",
    "/fail/structs/004_two_bad_fields.ft",
    "/fail/defer/004_defer_of_a_declaration.ft"};
static const uint64_t MULTI_ERROR_COUNTS[] = {2, 2, 2, 3, 2, 2, 2, 2};

// Records the files of the table above whose diagnostic count is not the one
// expected, as `<path>: <got> diagnostics, expected <want>`.
static void check_corpus_counts(void) {
    for (uint64_t i = 0; i < sizeof(MULTI_ERROR_FILES) / sizeof(MULTI_ERROR_FILES[0]); i++) {
        char path[CORPUS_PATH_CAP];
        TEST_UNUSED(snprintf(path, sizeof path, "%s%s", FORT_LANG_DIR, MULTI_ERROR_FILES[i]));
        uint64_t count = 0;
        if (read_source(path, &corpus_src)) {
            TEST_UNUSED(parse_text(sb_cstr(&corpus_src)));
            count = diag_count();
        }
        if (count != MULTI_ERROR_COUNTS[i]) {
            sb_append(&corpus_report, MULTI_ERROR_FILES[i]);
            sb_append(&corpus_report, ": ");
            msg_uint(&corpus_report, count);
            sb_append(&corpus_report, " diagnostics, expected ");
            msg_uint(&corpus_report, MULTI_ERROR_COUNTS[i]);
            sb_push(&corpus_report, '\n');
        }
    }
}

TEST(the_corpus_files_with_two_syntax_errors_report_both, {
    sb_clear(&corpus_report);
    check_corpus_counts();
    TEST_ASSERT_EQ_STR(sb_cstr(&corpus_report), "");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser_recovery", argc, argv);
    sb_init(&corpus_src);
    sb_init(&corpus_report);
    TEST_RUN(two_broken_statements_report_two_lines);
    TEST_RUN(two_broken_declarations_report_two_lines);
    TEST_RUN(a_missing_semicolon_does_not_cascade);
    TEST_RUN(a_broken_declaration_does_not_cascade);
    TEST_RUN(a_broken_field_does_not_cascade);
    TEST_RUN(a_broken_statement_in_a_case_does_not_cascade);
    TEST_RUN(a_broken_case_clause_does_not_cascade);
    TEST_RUN(a_skip_inside_a_case_stops_before_the_next_clause);
    TEST_RUN(a_skip_ends_at_the_brace_and_semicolon_of_an_initializer);
    TEST_RUN(a_body_without_its_opening_brace_is_read_as_a_body);
    TEST_RUN(an_unclosed_block_reports_at_the_next_declaration);
    TEST_RUN(a_function_type_at_statement_level_is_not_a_declaration);
    TEST_RUN(an_unclosed_struct_or_enum_reports_at_the_next_declaration);
    TEST_RUN(an_unclosed_block_at_the_end_of_the_file_reports_once);
    TEST_RUN(a_declaration_that_follows_a_failed_one_is_still_a_declaration);
    TEST_RUN(a_body_without_its_brace_keeps_every_statement);
    TEST_RUN(a_half_typed_construct_reports_once);
    TEST_RUN(a_brace_inside_an_unclosed_call_goes_with_the_statement);
    TEST_RUN(a_switch_body_holds_clauses_only);
    TEST_RUN(an_error_under_a_speculation_is_reported_once);
    TEST_RUN(a_speculation_during_an_unwind_leaves_no_trace);
    TEST_RUN(the_twenty_first_error_is_not_reported);
    TEST_RUN(the_cap_is_shared_with_the_lexer);
    TEST_RUN(a_declaration_after_the_cap_is_still_in_the_tree);
    TEST_RUN(two_errors_at_one_position_are_reported_once);
    TEST_RUN(a_file_with_errors_still_yields_a_tree);
    TEST_RUN(an_error_node_covers_the_skipped_region_and_has_no_children);
    TEST_RUN(a_lexical_error_costs_its_line_and_the_parser_sees_the_rest);
    TEST_RUN(a_file_with_an_unclosed_string_still_parses_the_lines_after_it);
    TEST_RUN(an_empty_file_and_a_one_token_file);
    TEST_RUN(nesting_past_the_limit_is_reported_once);
    TEST_RUN(the_cap_is_twenty_errors);
    TEST_RUN(a_skip_consumes_the_block_of_the_failed_statement);
    TEST_RUN(a_broken_import_does_not_stop_the_imports);
    TEST_RUN(a_late_import_is_reported_once_and_skipped);
    TEST_RUN(an_unclosed_brace_initializer_costs_one_statement);
    TEST_RUN(every_recovery_point_records_what_it_skipped);
    TEST_RUN(an_unclosed_bracket_costs_its_construct_and_no_more);
    TEST_RUN(the_fail_corpus_reports_only_on_annotated_lines);
    TEST_RUN(the_corpus_files_with_two_syntax_errors_report_both);
    sb_free(&corpus_report);
    sb_free(&corpus_src);
    parse_done();
    TEST_EXIT();
}
