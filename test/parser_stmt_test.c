// The statements, declarations and module structure of the parser
// (grammar.md 2, 3, 5), the three speculative points of grammar.md 7 and the
// nesting limit.
// D2.11
#include <stdint.h>
#include <string.h>

#include "ast.h"
#include "parser.h"
#include "parser_helpers.h"
#include "str.h"

#include "test.h"

// The source of the nesting tests, kept alive while its tree is used.
static sb_t deep;

// `prefix`, `n` copies of `open`, `core`, `n` copies of `close`, `suffix`.
static const char* nested(
    const char* prefix, char open, uint64_t n, const char* core, char close, const char* suffix) {
    sb_clear(&deep);
    sb_append(&deep, prefix);
    for (uint64_t i = 0; i < n; i++) {
        sb_push(&deep, open);
    }
    sb_append(&deep, core);
    for (uint64_t i = 0; i < n; i++) {
        sb_push(&deep, close);
    }
    sb_append(&deep, suffix);
    return sb_cstr(&deep);
}

// `i32` followed by `n` pointer suffixes, as a global declaration.
static const char* many_suffixes(uint64_t n) {
    sb_clear(&deep);
    sb_append(&deep, "i32");
    for (uint64_t i = 0; i < n; i++) {
        sb_push(&deep, '*');
    }
    sb_append(&deep, " p = null;");
    return sb_cstr(&deep);
}

// NOLINTBEGIN(readability-magic-numbers) the sources and the trees they parse
// to are the test data.

// ---- statements (grammar.md 5) --------------------------------------------

TEST(declarations_and_assignments, {
    TEST_ASSERT_EQ_STR(dump_stmt("i32 mut x = 1;"), "(var x (type (prim i32) mut) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x = 1;"), "(assign = (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x += 1;"), "(assign += (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x <<= 1;"), "(assign <<= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x *%= 1;"), "(assign *%= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("*p = 1;"), "(assign = (unary * (ident p)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("a[i] = 1;"), "(assign = (index (ident a) (ident i)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("p->f = 1;"), "(assign = (arrow (ident p) f) (int 1))");
})

TEST(increments_and_calls_are_statements, {
    TEST_ASSERT_EQ_STR(dump_stmt("i++;"), "(incdec ++ (ident i))");
    TEST_ASSERT_EQ_STR(dump_stmt("a[i]--;"), "(incdec -- (index (ident a) (ident i)))");
    TEST_ASSERT_EQ_STR(dump_stmt("f(1);"), "(call-stmt (call (ident f) (int 1)))");
    TEST_ASSERT_EQ_STR(dump_stmt("m.f();"), "(call-stmt (call (field (ident m) f)))");
})

TEST(blocks_nest_and_are_statements, {
    TEST_ASSERT_EQ_STR(dump_stmt("{ f(); }"), "(block (call-stmt (call (ident f))))");
    TEST_ASSERT_EQ_STR(dump_stmt("{ { } }"), "(block (block))");
})

TEST(if_chains_keep_their_shape, {
    TEST_ASSERT_EQ_STR(dump_stmt("if (c) { f(); }"),
                       "(if (ident c) (block (call-stmt (call (ident f)))) nil)");
    TEST_ASSERT_EQ_STR(dump_stmt("if (c) { } else { }"), "(if (ident c) (block) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("if (a) { } else if (b) { } else { }"),
                       "(if (ident a) (block) (if (ident b) (block) (block)))");
    TEST_ASSERT_EQ_STR(dump_stmt("if (a) { } else if (b) { }"),
                       "(if (ident a) (block) (if (ident b) (block) nil))");
})

TEST(while_loops, {
    TEST_ASSERT_EQ_STR(dump_stmt("while (c) { }"), "(while (ident c) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("while (i < n) { i++; }"),
                       "(while (binary < (ident i) (ident n)) (block (incdec ++ (ident i))))");
})

// Every `for` form, including the empty one.
// D7.5
TEST(for_forms, {
    TEST_ASSERT_EQ_STR(dump_stmt("for (;;) { }"), "(for nil nil nil (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (i32 mut i = 0; i < n; i++) { }"),
                       "(for (var i (type (prim i32) mut) (int 0))"
                       " (binary < (ident i) (ident n)) (incdec ++ (ident i)) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (i = 0; ; ) { }"),
                       "(for (assign = (ident i) (int 0)) nil nil (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (f(); c; g()) { }"),
                       "(for (call-stmt (call (ident f))) (ident c)"
                       " (call-stmt (call (ident g))) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (; c; ) { }"), "(for nil (ident c) nil (block))");
})

// The range loop, told from the other forms by the `:` after `type identifier`
// (grammar.md 7.3).
// D7.5
TEST(range_for_forms, {
    TEST_ASSERT_EQ_STR(dump_stmt("for (i32 x : xs) { }"),
                       "(range-for (type (prim i32)) x (ident xs) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (i32 mut x : xs) { }"),
                       "(range-for (type (prim i32) mut) x (ident xs) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (node* p : ps) { }"),
                       "(range-for (type (name node) (ptr)) p (ident ps) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (char c : s[1..]) { }"),
                       "(range-for (type (prim char)) c (span (ident s) (int 1) nil) (block))");
})

TEST(switch_clauses_and_bodies, {
    TEST_ASSERT_EQ_STR(dump_stmt("switch (c) { case 1, 2: f(); case color.red: break; default: }"),
                       "(switch (ident c)"
                       " (case (labels (int 1) (int 2)) (block (call-stmt (call (ident f)))))"
                       " (case (labels (field (ident color) red)) (block (break)))"
                       " (case default (block)))");
    TEST_ASSERT_EQ_STR(dump_stmt("switch (e) { }"), "(switch (ident e))");
})

TEST(defer_return_break_and_continue, {
    TEST_ASSERT_EQ_STR(dump_stmt("defer del(p);"),
                       "(defer (call-stmt (call (ident del) (ident p))))");
    TEST_ASSERT_EQ_STR(dump_stmt("defer { f(); }"), "(defer (block (call-stmt (call (ident f)))))");
    TEST_ASSERT_EQ_STR(dump_stmt("defer x = 1;"), "(defer (assign = (ident x) (int 1)))");
    TEST_ASSERT_EQ_STR(dump_stmt("return;"), "(return nil)");
    TEST_ASSERT_EQ_STR(dump_stmt("return 1 + 2;"), "(return (binary + (int 1) (int 2)))");
    TEST_ASSERT_EQ_STR(dump_stmt("break;"), "(break)");
    TEST_ASSERT_EQ_STR(dump_stmt("continue;"), "(continue)");
})

// ---- the declaration-versus-statement choice (grammar.md 7.1) -------------

TEST(the_token_after_the_type_settles_a_declaration, {
    TEST_ASSERT_EQ_STR(dump_stmt("foo[3] = x;"),
                       "(assign = (index (ident foo) (int 3)) (ident x))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo[3] arr = {};"),
                       "(var arr (type (name foo) (array (int 3))) (init))");
})

// Every `mut` and `own` stands after what it qualifies, so these are
// declarations, settled by the same speculative parse (grammar.md 7.1).
// D5.3
TEST(a_marked_type_at_statement_level_is_a_declaration, {
    TEST_ASSERT_EQ_STR(dump_stmt("foo@ mut x = s;"),
                       "(var x (type (name foo) (span mut)) (ident s))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo* own p = q;"),
                       "(var p (type (name foo) (ptr own)) (ident q))");
    TEST_ASSERT_EQ_STR(stmt_fails("foo@ mut x;"), "t.ft:2:11: error: expected '=', found ';'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("foo* own p;"), "t.ft:2:11: error: expected '=', found ';'\n");
})

TEST(a_qualified_type_and_a_function_type_at_statement_level, {
    TEST_ASSERT_EQ_STR(dump_stmt("math.vector w = {};"),
                       "(var w (type (name math vector)) (init))");
    TEST_ASSERT_EQ_STR(dump_stmt("fn (i32) i32 op = add;"),
                       "(var op (type (fn-type (type (prim i32)) (type (prim i32))))"
                       " (ident add))");
    TEST_ASSERT_EQ_STR(dump_stmt("string s = \"x\";"), "(var s (type (string)) (str \"x\"))");
    // A type admits exactly one dot (grammar.md 4: `qualified_name`), so a
    // second one is no longer a type and the statement is read as an
    // expression.
    // D9.4
    TEST_ASSERT_EQ_STR(stmt_fails("math.vec.pair w = {};"),
                       "t.ft:2:15: error: expected an assignment, an increment or a call, "
                       "found identifier 'w'\n");
})

// A statement never begins with a marker (grammar.md 7.1).
TEST(a_statement_never_begins_with_a_marker, {
    TEST_ASSERT_EQ_STR(stmt_fails("mut i32 x = 1;"),
                       "t.ft:2:1: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("own u8@ b = {};"),
                       "t.ft:2:1: error: an own never precedes the base type: "
                       "write 'node* own p' or 'string own s'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("for (mut i32 i = 0; ; ) { }"),
                       "t.ft:2:6: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
})

// Expression statements are calls only, and `IDENT {` is never a block.
// D7.3, D6.5
TEST(an_expression_statement_that_is_not_a_call, {
    TEST_ASSERT_EQ_STR(stmt_fails("x;"),
                       "t.ft:2:2: error: expected an assignment, an increment or a call, "
                       "found ';'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("point{1, 2};"),
                       "t.ft:2:12: error: expected an assignment, an increment or a call, "
                       "found ';'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("a * b;"), "t.ft:2:6: error: expected '=', found ';'\n");
    TEST_ASSERT_EQ_STR(stmt_fails(";"), "t.ft:2:1: error: an empty statement is not allowed\n");
})

TEST(the_syntax_errors_of_a_declaration_and_a_condition, {
    TEST_ASSERT_EQ_STR(stmt_fails("i32 x;"), "t.ft:2:6: error: expected '=', found ';'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("i32 a = 1, b = 2;"),
                       "t.ft:2:10: error: expected ';', found ','\n");
    TEST_ASSERT_EQ_STR(stmt_fails("if (x = 5) { }"), "t.ft:2:7: error: expected ')', found '='\n");
    TEST_ASSERT_EQ_STR(stmt_fails("if (x > 0) return 1;"),
                       "t.ft:2:12: error: expected '{', found 'return'\n");
})

// `do`-`while` is outside the C bootstrap's subset (toolchain.md 7.3).
TEST(do_while_is_not_supported, {
    TEST_ASSERT_EQ_STR(stmt_fails("do { } while (c);"),
                       "t.ft:2:1: error: not supported by the bootstrap compiler: do-while\n");
})

// ---- a rewind leaves no diagnostics (grammar.md 7) ------------------------

TEST(rewinds_leave_no_diagnostics, {
    const char* src = "fn f() void {\n"
                      "    foo[3] = x;\n"
                      "    a[b] = c[d];\n"
                      "    p->next = null;\n"
                      "    g(1);\n"
                      "    for (i = 0; i < 3; i++) { }\n"
                      "    for (i32 v : vs) { }\n"
                      "    m.f(1);\n"
                      "}\n";
    TEST_ASSERT_NONNULL(parse_text(src));
    TEST_ASSERT_EQ_STR(parse_diags(), "");
})

// An error inside a speculative parse is reported once, by the branch the
// decision committed to.
TEST(an_error_inside_a_speculation_is_reported_once, {
    TEST_ASSERT_EQ_STR(stmt_fails("foo[3 +] = x;"),
                       "t.ft:2:8: error: expected an expression, found ']'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("for (i32 x : ) { }"),
                       "t.ft:2:14: error: expected an expression, found ')'\n");
})

// A speculative parse skips the unsupported-feature checks, so the shape is
// still decided and the committed parse reports the feature
// (toolchain.md 7.3).
TEST(an_unsupported_feature_survives_a_rewind, {
    TEST_ASSERT_EQ_STR(
        stmt_fails("foo[3][4] arr = {};"),
        "t.ft:2:7: error: not supported by the bootstrap compiler: multi-dimensional arrays\n");
    TEST_ASSERT_EQ_STR(
        stmt_fails("foo@@ s = t;"),
        "t.ft:2:5: error: not supported by the bootstrap compiler: spans of spans\n");
    TEST_ASSERT_EQ_STR(
        stmt_fails("x = i32[2][3]{1};"),
        "t.ft:2:11: error: not supported by the bootstrap compiler: multi-dimensional arrays\n");
})

// The struct-literal lookahead of grammar.md 7.2 does not disturb an
// ordinary expression that starts the same way.
TEST(literals_and_expressions_that_start_alike, {
    TEST_ASSERT_EQ_STR(dump_stmt("x = point{1, 2};"),
                       "(assign = (ident x) (struct-lit (name point) (init (int 1) (int 2))))");
    TEST_ASSERT_EQ_STR(dump_stmt("x = i32[2]{1, 2};"),
                       "(assign = (ident x) (array-lit (type (prim i32) (array (int 2)))"
                       " (init (int 1) (int 2))))");
    TEST_ASSERT_EQ_STR(dump_stmt("x = m.f[1];"),
                       "(assign = (ident x) (index (field (ident m) f) (int 1)))");
})

// ---- declarations and modules (grammar.md 2, 3) --------------------------

TEST(function_declarations, {
    TEST_ASSERT_EQ_STR(parse_dump("fn main() i32 { return 0; }"),
                       "(module (fn (type (prim i32)) main (params) (block (return (int 0)))))");
    TEST_ASSERT_EQ_STR(parse_dump("fn f(i32 a, node mut* b) void { }"),
                       "(module (fn (type (void)) f (params (param (type (prim i32)) a)"
                       " (param (type (name node) mut (ptr)) b)) (block)))");
    TEST_ASSERT_EQ_STR(parse_dump("fn die() noreturn { }"),
                       "(module (fn (type (noreturn)) die (params) (block)))");
    TEST_ASSERT_EQ_STR(parse_dump("fn choose(bool p) fn (i32) i32 { }"),
                       "(module (fn (type (fn-type (type (prim i32)) (type (prim i32)))) choose"
                       " (params (param (type (prim bool)) p)) (block)))");
})

TEST(extern_struct_and_enum_declarations, {
    TEST_ASSERT_EQ_STR(parse_dump("extern fn puts(string s) void;"),
                       "(module (extern-fn (type (void)) puts"
                       " (params (param (type (string)) s)) nil))");
    TEST_ASSERT_EQ_STR(parse_dump("struct point { i32 x; i32 y; }"),
                       "(module (struct point (field-decl (type (prim i32)) x)"
                       " (field-decl (type (prim i32)) y)))");
    TEST_ASSERT_EQ_STR(parse_dump("enum color { red, green = 5, blue, }"),
                       "(module (enum color (member red nil) (member green (int 5))"
                       " (member blue nil)))");
})

// A module-level declaration is a var_decl whose initializer is constant; a
// function type is a type like any other there.
// D7.10
TEST(global_declarations, {
    TEST_ASSERT_EQ_STR(parse_dump("i32 N = 4;"), "(module (var N (type (prim i32)) (int 4)))");
    TEST_ASSERT_EQ_STR(parse_dump("point[2] mut cells = {};"),
                       "(module (var cells (type (name point) (array (int 2) mut)) (init)))");
    TEST_ASSERT_EQ_STR(parse_dump("fn (i32) i32 OP = add;"),
                       "(module (var OP (type (fn-type (type (prim i32)) (type (prim i32))))"
                       " (ident add)))");
})

TEST(import_forms, {
    TEST_ASSERT_EQ_STR(parse_dump("import std.io;"), "(module (import (path std io) nil))");
    TEST_ASSERT_EQ_STR(parse_dump("import a;"), "(module (import (path a) nil))");
    TEST_ASSERT_EQ_STR(parse_dump("import a.b as c;"), "(module (import (path a b) (ident c)))");
    TEST_ASSERT_EQ_STR(parse_dump("import a.b.{s1, s2 as t,};"),
                       "(module (import (path a b) nil (item s1 nil) (item s2 (ident t))))");
})

TEST(a_module_is_imports_then_declarations, {
    TEST_ASSERT_EQ_STR(parse_dump(""), "(module)");
    TEST_ASSERT_EQ_STR(parse_dump("import std.io;\ni32 N = 1;\nfn f() void { }"),
                       "(module (import (path std io) nil) (var N (type (prim i32)) (int 1))"
                       " (fn (type (void)) f (params) (block)))");
    TEST_ASSERT_EQ_STR(parse_fails("i32 N = 4;\nimport a;"),
                       "t.ft:2:1: error: an import comes before every declaration\n");
    TEST_ASSERT_EQ_STR(parse_fails("mut i32 N = 4;"),
                       "t.ft:1:1: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
})

TEST(empty_aggregates_are_errors, {
    TEST_ASSERT_EQ_STR(parse_fails("struct s { }"),
                       "t.ft:1:12: error: a struct has at least one field\n");
    TEST_ASSERT_EQ_STR(parse_fails("enum e { }"),
                       "t.ft:1:10: error: an enum has at least one member\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s { i32 x }"),
                       "t.ft:1:18: error: expected ';', found '}'\n");
})

// A parameter list takes no trailing comma.
// D6.5
TEST(parameter_lists_take_no_trailing_comma, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f(i32 a,) void { }"),
                       "t.ft:1:12: error: expected a type, found ')'\n");
})

// ---- the nesting limit ---------------------------------------------------
// D2.11

TEST(nesting_of_256_is_accepted, {
    TEST_ASSERT_NONNULL(parse_text(nested("i32 x = ", '(', 256, "1", ')', ";")));
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_NONNULL(parse_text(nested("fn f() void ", '{', 256, "", '}', "")));
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_NONNULL(parse_text(many_suffixes(256)));
    TEST_ASSERT_EQ_STR(parse_diags(), "");
})

TEST(nesting_deeper_than_256_is_an_error, {
    TEST_ASSERT_NONNULL(strstr(parse_fails(nested("i32 x = ", '(', 257, "1", ')', ";")),
                               "error: nesting deeper than 256\n"));
    TEST_ASSERT_NONNULL(strstr(parse_fails(nested("fn f() void ", '{', 257, "", '}', "")),
                               "error: nesting deeper than 256\n"));
    TEST_ASSERT_NONNULL(strstr(parse_fails(nested("point x = ", '{', 257, "", '}', ";")),
                               "error: nesting deeper than 256\n"));
    TEST_ASSERT_NONNULL(
        strstr(parse_fails(many_suffixes(257)), "error: nesting deeper than 256\n"));
})

// The end of the file closes nothing: every unterminated construct is
// reported where it runs out of tokens, and no loop spins there.
TEST(unterminated_constructs_end_at_the_end_of_the_file, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() i32 {"),
                       "t.ft:1:13: error: expected '}', found end of file\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s {"),
                       "t.ft:1:11: error: expected '}', found end of file\n");
    TEST_ASSERT_EQ_STR(parse_fails("enum e {"),
                       "t.ft:1:9: error: expected an identifier, found end of file\n");
    TEST_ASSERT_EQ_STR(parse_fails("point x = {"),
                       "t.ft:1:12: error: expected an expression, found end of file\n");
    TEST_ASSERT_EQ_STR(parse_fails("import a."),
                       "t.ft:1:10: error: expected an identifier, found end of file\n");
    TEST_ASSERT_EQ_STR(parse_fails("i32"),
                       "t.ft:1:4: error: expected an identifier, found end of file\n");
})

// An `else if` chain is a chain, not nesting: it costs no depth and no
// recursion, so a long one parses.
// D7.4
TEST(a_long_else_if_chain_is_not_nesting, {
    sb_clear(&deep);
    sb_append(&deep, "fn f() void {\n    if (c) { }");
    for (uint64_t i = 0; i < 300; i++) {
        sb_append(&deep, " else if (c) { }");
    }
    sb_append(&deep, " else { }\n}\n");
    const ast_node_t* mod = parse_text(sb_cstr(&deep));
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    const ast_node_t* chain = ast_child(ast_child(mod, 0)->b, 0);
    uint64_t links = 0;
    while (chain != NULL && chain->kind == AST_IF) {
        links++;
        chain = chain->c;
    }
    TEST_ASSERT_EQ_UINT64(links, (uint64_t)301);
})

// One diagnostic per broken statement, and the statements and declarations
// after it are parsed and reported too.
// D14.2
TEST(every_broken_statement_is_reported, {
    TEST_ASSERT_EQ_STR(parse_fails("fn f() void {\n    x;\n    y;\n}\ni32 = 1;\n"),
                       "t.ft:2:6: error: expected an assignment, an increment or a call, "
                       "found ';'\n"
                       "t.ft:3:6: error: expected an assignment, an increment or a call, "
                       "found ';'\n"
                       "t.ft:5:5: error: expected an identifier, found '='\n");
})

// ---- statements in every context ------------------------------------------

// Every compound assignment is a statement of its own.
// D7.2
TEST(every_assignment_operator_is_a_statement, {
    TEST_ASSERT_EQ_STR(dump_stmt("x -= 1;"), "(assign -= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x *= 1;"), "(assign *= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x /= 1;"), "(assign /= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x %= 1;"), "(assign %= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x +%= 1;"), "(assign +%= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x -%= 1;"), "(assign -%= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x &= 1;"), "(assign &= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x |= 1;"), "(assign |= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x ^= 1;"), "(assign ^= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("x >>= 1;"), "(assign >>= (ident x) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("i--;"), "(incdec -- (ident i))");
})

// An assignment target is any postfix form or a unary `*` (grammar.md 5).
TEST(every_assignment_target_shape, {
    TEST_ASSERT_EQ_STR(dump_stmt("**pp = 1;"), "(assign = (unary * (unary * (ident pp))) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("(*p).f = 1;"),
                       "(assign = (field (unary * (ident p)) f) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("a[i][j] = 1;"),
                       "(assign = (index (index (ident a) (ident i)) (ident j)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("p->next->value = 1;"),
                       "(assign = (arrow (arrow (ident p) next) value) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("m.g.h = 1;"), "(assign = (field (field (ident m) g) h) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("f(1).x = 1;"),
                       "(assign = (field (call (ident f) (int 1)) x) (int 1))");
})

// A declaration is a statement wherever a statement is, and its initializer is
// an expression or a brace list.
// D6.5, D7.1
TEST(declarations_in_every_body, {
    TEST_ASSERT_EQ_STR(dump_stmt("if (c) { i32 x = 1; }"),
                       "(if (ident c) (block (var x (type (prim i32)) (int 1))) nil)");
    TEST_ASSERT_EQ_STR(dump_stmt("while (c) { point p = {}; }"),
                       "(while (ident c) (block (var p (type (name point)) (init))))");
    TEST_ASSERT_EQ_STR(dump_stmt("{ i32[2] a = {1, 2}; }"),
                       "(block (var a (type (prim i32) (array (int 2)))"
                       " (init (int 1) (int 2))))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (;;) { string s = \"x\"; }"),
                       "(for nil nil nil (block (var s (type (string)) (str \"x\"))))");
})

// Loops and conditionals nest, and `break` and `continue` sit in them.
// D7.5, D7.6
TEST(nested_control_flow, {
    TEST_ASSERT_EQ_STR(dump_stmt("while (a) { while (b) { break; } continue; }"),
                       "(while (ident a) (block (while (ident b) (block (break))) (continue)))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (;;) { if (c) { break; } else { continue; } }"),
                       "(for nil nil nil (block (if (ident c) (block (break))"
                       " (block (continue)))))");
    TEST_ASSERT_EQ_STR(dump_stmt("switch (c) { case 1: while (b) { break; } }"),
                       "(switch (ident c) (case (labels (int 1))"
                       " (block (while (ident b) (block (break))))))");
    TEST_ASSERT_EQ_STR(dump_stmt("switch (a) { case 1: switch (b) { default: } }"),
                       "(switch (ident a) (case (labels (int 1))"
                       " (block (switch (ident b) (case default (block))))))");
})

// A case body is an implicit block that holds any statement.
// D7.6
TEST(case_bodies_hold_statements, {
    TEST_ASSERT_EQ_STR(dump_stmt("switch (c) { case 1: i32 x = 1; f(x); }"),
                       "(switch (ident c) (case (labels (int 1))"
                       " (block (var x (type (prim i32)) (int 1))"
                       " (call-stmt (call (ident f) (ident x))))))");
    TEST_ASSERT_EQ_STR(dump_stmt("switch (c) { case 1: case 2: f(); }"),
                       "(switch (ident c) (case (labels (int 1)) (block))"
                       " (case (labels (int 2)) (block (call-stmt (call (ident f))))))");
    TEST_ASSERT_EQ_STR(dump_stmt("switch (c) { default: return; }"),
                       "(switch (ident c) (case default (block (return nil))))");
})

// `defer` takes an assignment, an increment, a call or a block.
// D7.8
TEST(every_defer_form, {
    TEST_ASSERT_EQ_STR(dump_stmt("defer i++;"), "(defer (incdec ++ (ident i)))");
    TEST_ASSERT_EQ_STR(dump_stmt("defer *p = 0;"),
                       "(defer (assign = (unary * (ident p)) (int 0)))");
    TEST_ASSERT_EQ_STR(dump_stmt("defer { del(a); del(b); }"),
                       "(defer (block (call-stmt (call (ident del) (ident a)))"
                       " (call-stmt (call (ident del) (ident b)))))");
    TEST_ASSERT_EQ_STR(dump_stmt("defer io.flush();"),
                       "(defer (call-stmt (call (field (ident io) flush))))");
    TEST_ASSERT_EQ_STR(stmt_fails("defer return;"),
                       "t.ft:2:7: error: expected an expression, found 'return'\n");
    TEST_ASSERT_EQ_STR(stmt_fails("defer i32 x = 1;"),
                       "t.ft:2:7: error: expected an expression, found 'i32'\n");
})

// The `for` parts are a declaration, an assignment, an increment or a call,
// and any of them may be missing.
// D7.5
TEST(every_for_part_shape, {
    TEST_ASSERT_EQ_STR(dump_stmt("for (f(); ; g()) { }"),
                       "(for (call-stmt (call (ident f))) nil"
                       " (call-stmt (call (ident g))) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (i = 0; i < n; i += 2) { }"),
                       "(for (assign = (ident i) (int 0))"
                       " (binary < (ident i) (ident n))"
                       " (assign += (ident i) (int 2)) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (node* mut p = head; p != null; p = p->next) { }"),
                       "(for (var p (type (name node) (ptr mut)) (ident head))"
                       " (binary != (ident p) (null))"
                       " (assign = (ident p) (arrow (ident p) next)) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (; ; i--) { }"),
                       "(for nil nil (incdec -- (ident i)) (block))");
})

// A range loop takes any expression as its collection.
// D7.5
TEST(range_for_collections, {
    TEST_ASSERT_EQ_STR(dump_stmt("for (u8 b : buf[..]) { }"),
                       "(range-for (type (prim u8)) b (span (ident buf) nil nil) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (i32 v : f(1)) { }"),
                       "(range-for (type (prim i32)) v (call (ident f) (int 1)) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (node* n : m.nodes) { }"),
                       "(range-for (type (name node) (ptr)) n (field (ident m) nodes) (block))");
    TEST_ASSERT_EQ_STR(dump_stmt("for (i32[4] row : rows) { }"),
                       "(range-for (type (prim i32) (array (int 4))) row (ident rows) (block))");
})

// ---- the disambiguation matrix (grammar.md 7.1) ---------------------------

// A statement that starts with a type-looking prefix is a declaration
// exactly when an identifier follows the type.
TEST(a_type_prefix_followed_by_an_identifier_is_a_declaration, {
    TEST_ASSERT_EQ_STR(dump_stmt("foo x = 1;"), "(var x (type (name foo)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo* x = null;"), "(var x (type (name foo) (ptr)) (null))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo@ x = s;"), "(var x (type (name foo) (span)) (ident s))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo[2] x = {};"),
                       "(var x (type (name foo) (array (int 2))) (init))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo*[2] x = {};"),
                       "(var x (type (name foo) (ptr) (array (int 2))) (init))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo[2]* x = null;"),
                       "(var x (type (name foo) (array (int 2)) (ptr)) (null))");
    TEST_ASSERT_EQ_STR(dump_stmt("m.foo* mut x = null;"),
                       "(var x (type (name m foo) (ptr mut)) (null))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo mut* own mut x = null;"),
                       "(var x (type (name foo) mut (ptr own mut)) (null))");
})

// The same prefixes followed by anything else are expression statements.
TEST(a_type_prefix_followed_by_anything_else_is_a_statement, {
    TEST_ASSERT_EQ_STR(dump_stmt("foo();"), "(call-stmt (call (ident foo)))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo = 1;"), "(assign = (ident foo) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo++;"), "(incdec ++ (ident foo))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo[2] = 1;"), "(assign = (index (ident foo) (int 2)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo[2][3] = 1;"),
                       "(assign = (index (index (ident foo) (int 2)) (int 3)) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("m.foo = 1;"), "(assign = (field (ident m) foo) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("m.foo(1);"), "(call-stmt (call (field (ident m) foo) (int 1)))");
    TEST_ASSERT_EQ_STR(dump_stmt("foo.bar[2].baz = 1;"),
                       "(assign = (field (index (field (ident foo) bar) (int 2)) baz) (int 1))");
})

// A type embeds in an expression, in `cast`, `sizeof`, `new` and the length of
// an array literal (grammar.md 6), so a `mut` or an `own` inside a bracket
// belongs to that type and says nothing about the statement: only a marker
// outside every bracket makes the statement a declaration (grammar.md 7.1).
TEST(a_marker_inside_a_bracket_leaves_the_statement_alone, {
    TEST_ASSERT_EQ_STR(dump_stmt("a[sizeof(i32 mut*)..2] = b;"),
                       "(assign = (span (ident a) "
                       "(sizeof (type (prim i32) mut (ptr))) (int 2)) (ident b))");
    TEST_ASSERT_EQ_STR(dump_stmt("a[sizeof(node* own)] = 1;"),
                       "(assign = (index (ident a) "
                       "(sizeof (type (name node) (ptr own)))) (int 1))");
    TEST_ASSERT_EQ_STR(dump_stmt("p = cast(q, node* mut);"),
                       "(assign = (ident p) "
                       "(cast (ident q) (type (name node) (ptr mut))))");
    TEST_ASSERT_EQ_STR(dump_stmt("f(new(node* own, 4));"),
                       "(call-stmt (call (ident f) "
                       "(new (type (name node) (ptr own)) (int 4))))");
})

// A primitive, `string`, `void` or `fn` starts a declaration with no
// speculation at all (grammar.md 7.1).
TEST(a_keyword_type_starts_a_declaration_at_once, {
    TEST_ASSERT_EQ_STR(dump_stmt("u8 b = 0;"), "(var b (type (prim u8)) (int 0))");
    TEST_ASSERT_EQ_STR(dump_stmt("bool ok = true;"), "(var ok (type (prim bool)) (bool true))");
    TEST_ASSERT_EQ_STR(dump_stmt("char c = 'x';"), "(var c (type (prim char)) (char 120))");
    TEST_ASSERT_EQ_STR(dump_stmt("string s = t;"), "(var s (type (string)) (ident t))");
    TEST_ASSERT_EQ_STR(dump_stmt("void* p = null;"), "(var p (type (void) (ptr)) (null))");
    TEST_ASSERT_EQ_STR(dump_stmt("fn () void f = g;"),
                       "(var f (type (fn-type (type (void)))) (ident g))");
    TEST_ASSERT_EQ_STR(dump_stmt("i32[2] mut a = {};"),
                       "(var a (type (prim i32) (array (int 2) mut)) (init))");
})

// The speculative parse looks past a whole type, however long it is, and
// still rewinds cleanly when no identifier follows.
TEST(a_long_type_prefix_rewinds_cleanly, {
    TEST_ASSERT_EQ_STR(dump_stmt("a[b + c * d] = e;"),
                       "(assign = (index (ident a)"
                       " (binary + (ident b) (binary * (ident c) (ident d)))) (ident e))");
    TEST_ASSERT_EQ_STR(dump_stmt("a[f(1)] = e;"),
                       "(assign = (index (ident a) (call (ident f) (int 1))) (ident e))");
    TEST_ASSERT_EQ_STR(dump_stmt("a[sizeof(i32)] = e;"),
                       "(assign = (index (ident a) (sizeof (type (prim i32)))) (ident e))");
    TEST_ASSERT_EQ_STR(dump_stmt("a[m.N] = e;"),
                       "(assign = (index (ident a) (field (ident m) N)) (ident e))");
    TEST_ASSERT_EQ_STR(parse_diags(), "");
})
// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser_stmt", argc, argv);
    sb_init(&deep);
    TEST_RUN(declarations_and_assignments);
    TEST_RUN(increments_and_calls_are_statements);
    TEST_RUN(blocks_nest_and_are_statements);
    TEST_RUN(if_chains_keep_their_shape);
    TEST_RUN(while_loops);
    TEST_RUN(for_forms);
    TEST_RUN(range_for_forms);
    TEST_RUN(switch_clauses_and_bodies);
    TEST_RUN(defer_return_break_and_continue);
    TEST_RUN(the_token_after_the_type_settles_a_declaration);
    TEST_RUN(a_marked_type_at_statement_level_is_a_declaration);
    TEST_RUN(a_qualified_type_and_a_function_type_at_statement_level);
    TEST_RUN(a_statement_never_begins_with_a_marker);
    TEST_RUN(an_expression_statement_that_is_not_a_call);
    TEST_RUN(the_syntax_errors_of_a_declaration_and_a_condition);
    TEST_RUN(do_while_is_not_supported);
    TEST_RUN(rewinds_leave_no_diagnostics);
    TEST_RUN(an_error_inside_a_speculation_is_reported_once);
    TEST_RUN(an_unsupported_feature_survives_a_rewind);
    TEST_RUN(literals_and_expressions_that_start_alike);
    TEST_RUN(function_declarations);
    TEST_RUN(extern_struct_and_enum_declarations);
    TEST_RUN(global_declarations);
    TEST_RUN(import_forms);
    TEST_RUN(a_module_is_imports_then_declarations);
    TEST_RUN(empty_aggregates_are_errors);
    TEST_RUN(parameter_lists_take_no_trailing_comma);
    TEST_RUN(nesting_of_256_is_accepted);
    TEST_RUN(nesting_deeper_than_256_is_an_error);
    TEST_RUN(unterminated_constructs_end_at_the_end_of_the_file);
    TEST_RUN(a_long_else_if_chain_is_not_nesting);
    TEST_RUN(every_broken_statement_is_reported);
    TEST_RUN(every_assignment_operator_is_a_statement);
    TEST_RUN(every_assignment_target_shape);
    TEST_RUN(declarations_in_every_body);
    TEST_RUN(nested_control_flow);
    TEST_RUN(case_bodies_hold_statements);
    TEST_RUN(every_defer_form);
    TEST_RUN(every_for_part_shape);
    TEST_RUN(range_for_collections);
    TEST_RUN(a_type_prefix_followed_by_an_identifier_is_a_declaration);
    TEST_RUN(a_type_prefix_followed_by_anything_else_is_a_statement);
    TEST_RUN(a_marker_inside_a_bracket_leaves_the_statement_alone);
    TEST_RUN(a_keyword_type_starts_a_declaration_at_once);
    TEST_RUN(a_long_type_prefix_rewinds_cleanly);
    parse_done();
    sb_free(&deep);
    TEST_EXIT();
}
