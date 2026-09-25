// Tests parser positions and complete accepted modules. Every later pass reports at a
// node's `loc`. Each kind of node is pinned to the token a diagnostic about it should point at. The
// module shapes the specification writes are parsed end to end.
#include <stdint.h>

#include "ast.h"
#include "common/parser_helpers.h"
#include "parser.h"

#include "common/test.h"

// The `i`-th statement of the body of the `d`-th declaration.
static const ast_node_t* body_stmt(const ast_node_t* mod, uint64_t d, uint64_t i) {
    const ast_node_t* decl = pt_decl(mod, d);
    if (decl == NULL || decl->b == NULL || ast_len(decl->b) <= i) {
        return NULL;
    }
    return ast_child(decl->b, i);
}

// NOLINTBEGIN(readability-magic-numbers) the sources, the positions and the
// trees they parse to are the test data.

// ---- positions of declarations --------------------------------------------

TEST(a_declaration_starts_at_its_type, {
    const ast_node_t* mod = parse_text("i32 N = 4;\nfn f(i32 a) i32 {\n    return a;\n}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(loc_of(mod), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->a), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->b), "1:9");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)), "2:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)->a), "2:13");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(pt_decl(mod, 1), 0)), "2:6");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)->b), "2:17");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 1, 0)), "3:5");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 1, 0)->a), "3:12");
})

TEST(a_struct_an_enum_and_an_import_start_at_their_keyword, {
    const ast_node_t* mod = parse_text("import std.io;\n"
                                       "struct point {\n"
                                       "    i32 x;\n"
                                       "}\n"
                                       "enum color {\n"
                                       "    red,\n"
                                       "    green = 5,\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->a), "1:8");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)), "2:1");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(pt_decl(mod, 1), 0)), "3:5");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 2)), "5:1");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(pt_decl(mod, 2), 0)), "6:5");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(pt_decl(mod, 2), 1)), "7:5");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(pt_decl(mod, 2), 1)->a), "7:13");
})

// ---- positions of expressions ---------------------------------------------

// An operator node carries its operator, which is where a diagnostic about
// the operation points.
TEST(an_operator_node_is_at_its_operator, {
    const ast_node_t* mod = parse_text("i32 x = a + b * c;\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* sum = pt_decl(mod, 0)->b;
    TEST_ASSERT_EQ_STR(loc_of(sum), "1:11");
    TEST_ASSERT_EQ_STR(loc_of(sum->a), "1:9");
    TEST_ASSERT_EQ_STR(loc_of(sum->b), "1:15");
    TEST_ASSERT_EQ_STR(loc_of(sum->b->a), "1:13");
    TEST_ASSERT_EQ_STR(loc_of(sum->b->b), "1:17");
})

TEST(unary_and_postfix_nodes_are_at_their_own_token, {
    const ast_node_t* mod = parse_text("i32 a = -x;\n"
                                       "i32 b = f(1);\n"
                                       "i32 c = v[0];\n"
                                       "i32 d = p.f;\n"
                                       "i32 e = p->f;\n"
                                       "i32 g = s[1..2];\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->b), "1:9");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)->b), "2:10");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 2)->b), "3:10");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 3)->b), "4:10");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 4)->b), "5:10");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 5)->b), "6:10");
})

TEST(literal_and_keyword_expressions_are_at_their_first_token, {
    const ast_node_t* mod = parse_text("i32 a = cast(x, i32);\n"
                                       "i32 b = sizeof(u8);\n"
                                       "i32 c = new(node);\n"
                                       "point d = point{1};\n"
                                       "i32 e = i32[1]{0};\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->b), "1:9");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->b->b), "1:17");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)->b), "2:9");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 2)->b), "3:9");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 3)->b), "4:11");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 3)->b->b), "4:16");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 4)->b), "5:9");
})

// ---- positions of types ---------------------------------------------------

// A type starts at its base type and every suffix at its own token, so a
// placement diagnostic points at the marker it is about.
TEST(a_type_and_its_suffixes_carry_their_own_positions, {
    const ast_node_t* mod = parse_text("node* mut@ own x = 0;\ni32[4] mut y = 0;\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* first = pt_decl(mod, 0)->a;
    TEST_ASSERT_EQ_STR(loc_of(first), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(first->a), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(first, 0)), "1:5");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(first, 1)), "1:10");
    const ast_node_t* second = pt_decl(mod, 1)->a;
    TEST_ASSERT_EQ_STR(loc_of(ast_child(second, 0)), "2:4");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(second, 0)->a), "2:5");
})

// ---- positions of statements ----------------------------------------------

TEST(a_statement_starts_at_its_keyword_or_target, {
    const ast_node_t* mod = parse_text("fn f() void {\n"
                                       "    if (c) { }\n"
                                       "    while (c) { }\n"
                                       "    for (;;) { }\n"
                                       "    switch (c) { case 1: }\n"
                                       "    defer g();\n"
                                       "    x = 1;\n"
                                       "    x++;\n"
                                       "    break;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 0)), "2:5");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 1)), "3:5");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 2)), "4:5");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 3)), "5:5");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(body_stmt(mod, 0, 3), 0)), "5:18");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 4)), "6:5");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 5)), "7:7");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 6)), "8:6");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 0, 7)), "9:5");
})

// ---- ranges: the end of a node's range ------------------------------------

// A node's range runs to one past the last byte of the construct's last
// token, which the helpers show as the source text it covers.
TEST(a_declaration_covers_its_terminator, {
    const ast_node_t* mod = parse_text("i32 n = 4 + 1;\n"
                                       "fn f(i32 a) i32 {\n"
                                       "    return a;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 0)), "i32 n = 4 + 1;");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 0)->a), "i32");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 1)), "fn f(i32 a) i32 {\n    return a;\n}");
    TEST_ASSERT_EQ_STR(text_of(ast_child(pt_decl(mod, 1), 0)), "i32 a");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 1)->b), "{\n    return a;\n}");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 1, 0)), "return a;");
})

// The whole module runs from 1:1 to one past the last byte of its last token.
TEST(the_module_covers_the_whole_file, {
    const ast_node_t* mod = parse_text("i32 n = 4;\ni32 m = 5;\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(range_of(mod), "1:1-2:11");
    TEST_ASSERT_EQ_STR(text_of(mod), "i32 n = 4;\ni32 m = 5;");
})

TEST(an_empty_file_is_the_empty_range_at_one_one, {
    const ast_node_t* mod = parse_text("");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(range_of(mod), "1:1-1:1");
})

// A node named after an operator starts at that operator and ends with its last operand, so its
// range is not the whole operation.
TEST(an_operator_node_runs_from_its_operator_to_its_last_operand, {
    const ast_node_t* mod = parse_text("i32 x = a + b * c;\n"
                                       "fn f(i32 mut y) void {\n"
                                       "    y += g(1, 2);\n"
                                       "    y++;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* sum = pt_decl(mod, 0)->b;
    TEST_ASSERT_EQ_STR(text_of(sum), "+ b * c");
    TEST_ASSERT_EQ_STR(text_of(sum->a), "a");
    TEST_ASSERT_EQ_STR(text_of(sum->b), "* c");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 1, 0)), "+= g(1, 2);");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 1, 0)->b), "(1, 2)");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 1, 1)), "++;");
})

TEST(postfix_and_unary_nodes_cover_their_own_tokens, {
    const ast_node_t* mod = parse_text("i32 a = -x;\n"
                                       "i32 b = f(1);\n"
                                       "i32 c = v[0];\n"
                                       "i32 d = p.f;\n"
                                       "i32 e = p->f;\n"
                                       "i32 g = s[1..2];\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 0)->b), "-x");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 1)->b), "(1)");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 2)->b), "[0]");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 3)->b), ".f");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 4)->b), "->f");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 5)->b), "[1..2]");
})

TEST(literal_and_keyword_expressions_cover_their_whole_form, {
    const ast_node_t* mod = parse_text("i32 a = cast(x, i32);\n"
                                       "i32 b = sizeof(u8);\n"
                                       "i32 c = new(node);\n"
                                       "point d = point{1};\n"
                                       "i32 e = i32[1]{0};\n"
                                       "string f = \"hi\";\n"
                                       "bool g = true;\n"
                                       "node* h = null;\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 0)->b), "cast(x, i32)");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 1)->b), "sizeof(u8)");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 2)->b), "new(node)");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 3)->b), "point{1}");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 3)->b->b), "{1}");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 4)->b), "i32[1]{0}");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 5)->b), "\"hi\"");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 6)->b), "true");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 7)->b), "null");
})

// A type covers its base and every marker and suffix written after it, and
// each suffix covers its own marker.
TEST(a_type_covers_its_suffixes_and_markers, {
    const ast_node_t* mod = parse_text("node* mut@ own x = 0;\n"
                                       "i32[4] mut y = 0;\n"
                                       "string own s = \"\";\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* first = pt_decl(mod, 0)->a;
    TEST_ASSERT_EQ_STR(text_of(first), "node* mut@ own");
    TEST_ASSERT_EQ_STR(text_of(first->a), "node");
    TEST_ASSERT_EQ_STR(text_of(ast_child(first, 0)), "* mut");
    TEST_ASSERT_EQ_STR(text_of(ast_child(first, 1)), "@ own");
    const ast_node_t* second = pt_decl(mod, 1)->a;
    TEST_ASSERT_EQ_STR(text_of(second), "i32[4] mut");
    TEST_ASSERT_EQ_STR(text_of(ast_child(second, 0)), "[4] mut");
    TEST_ASSERT_EQ_STR(text_of(ast_child(second, 0)->a), "4");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 2)->a), "string own");
})

TEST(a_statement_covers_its_block_or_semicolon, {
    const ast_node_t* mod = parse_text("fn f() void {\n"
                                       "    if (c) { g(); } else { h(); }\n"
                                       "    while (c) { }\n"
                                       "    for (i32 mut i = 0; i < 4; i++) { }\n"
                                       "    for (i32 v : xs) { }\n"
                                       "    switch (c) { case 1: g(); default: }\n"
                                       "    defer g();\n"
                                       "    x = 1;\n"
                                       "    g();\n"
                                       "    return;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 0)), "if (c) { g(); } else { h(); }");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 1)), "while (c) { }");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 2)), "for (i32 mut i = 0; i < 4; i++) { }");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 2)->a), "i32 mut i = 0");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 3)), "for (i32 v : xs) { }");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 4)), "switch (c) { case 1: g(); default: }");
    TEST_ASSERT_EQ_STR(text_of(ast_child(body_stmt(mod, 0, 4), 0)), "case 1: g();");
    TEST_ASSERT_EQ_STR(text_of(ast_child(body_stmt(mod, 0, 4), 0)->a), "g();");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 5)), "defer g();");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 6)), "= 1;");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 7)), "g();");
    TEST_ASSERT_EQ_STR(text_of(body_stmt(mod, 0, 8)), "return;");
})

// An if-else-if chain nests at its tail, so every `if` of the chain ends at
// the end of the chain.
TEST(every_if_of_a_chain_ends_at_the_end_of_the_chain, {
    const ast_node_t* mod = parse_text("fn f() void {\n"
                                       "    if (a) { } else if (b) { } else { }\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* outer = body_stmt(mod, 0, 0);
    TEST_ASSERT_EQ_STR(text_of(outer), "if (a) { } else if (b) { } else { }");
    TEST_ASSERT_EQ_STR(text_of(outer->c), "if (b) { } else { }");
    TEST_ASSERT_EQ_STR(text_of(outer->c->c), "{ }");
})

// An empty case body has no token of its own. It is the empty range just after the ':' and stays
// inside its clause.
TEST(an_empty_case_body_is_an_empty_range_after_the_colon, {
    const ast_node_t* mod = parse_text("fn f() void {\n"
                                       "    switch (c) { case 1: }\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* clause = ast_child(body_stmt(mod, 0, 0), 0);
    TEST_ASSERT_EQ_STR(range_of(clause), "2:18-2:25");
    TEST_ASSERT_EQ_STR(range_of(clause->a), "2:25-2:25");
    TEST_ASSERT_EQ_STR(text_of(clause->a), "");
})

TEST(an_import_covers_its_semicolon_and_its_items, {
    const ast_node_t* mod = parse_text("import std.io;\n"
                                       "import util.strings as s;\n"
                                       "import a.b.{c, d as e};\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 0)), "import std.io;");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 0)->a), "std.io");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 1)), "import util.strings as s;");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 1)->b), "as s");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 2)), "import a.b.{c, d as e};");
    // The path ends at its last segment; the item list is not part of it.
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 2)->a), "a.b");
    TEST_ASSERT_EQ_STR(text_of(ast_child(pt_decl(mod, 2), 0)), "c");
    TEST_ASSERT_EQ_STR(text_of(ast_child(pt_decl(mod, 2), 1)), "d as e");
})

TEST(a_struct_an_enum_and_an_extern_cover_their_whole_declaration, {
    const ast_node_t* mod = parse_text("struct point {\n"
                                       "    i32 x;\n"
                                       "}\n"
                                       "enum color {\n"
                                       "    red,\n"
                                       "    green = 5,\n"
                                       "}\n"
                                       "extern fn abort() void;\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 0)), "struct point {\n    i32 x;\n}");
    TEST_ASSERT_EQ_STR(text_of(ast_child(pt_decl(mod, 0), 0)), "i32 x;");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 1)), "enum color {\n    red,\n    green = 5,\n}");
    TEST_ASSERT_EQ_STR(text_of(ast_child(pt_decl(mod, 1), 0)), "red");
    TEST_ASSERT_EQ_STR(text_of(ast_child(pt_decl(mod, 1), 1)), "green = 5");
    TEST_ASSERT_EQ_STR(text_of(pt_decl(mod, 2)), "extern fn abort() void;");
})

TEST(break_and_continue_cover_their_semicolon, {
    const ast_node_t* mod = parse_text("fn f() void {\n"
                                       "    while (c) {\n"
                                       "        break;\n"
                                       "        continue;\n"
                                       "    }\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* body = body_stmt(mod, 0, 0)->b;
    TEST_ASSERT_EQ_STR(text_of(ast_child(body, 0)), "break;");
    TEST_ASSERT_EQ_STR(text_of(ast_child(body, 1)), "continue;");
})

// ---- name ranges ----------------------------------------------------------

// A declaration carries the range of the name it introduces, so an editor
// jumps to the name and not to the first token.
TEST(declarations_carry_the_range_of_their_name, {
    const ast_node_t* mod = parse_text("struct point {\n"
                                       "    i32 x;\n"
                                       "}\n"
                                       "enum color { red, green }\n"
                                       "i32 total = 0;\n"
                                       "fn sum(i32 a) i32 {\n"
                                       "    i32 local = a;\n"
                                       "    return local;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 0)), "point");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 0), 0)), "x");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 1)), "color");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 1), 0)), "red");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 1), 1)), "green");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 2)), "total");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 3)), "sum");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 3), 0)), "a");
    TEST_ASSERT_EQ_STR(name_text_of(body_stmt(mod, 3, 0)), "local");
})

// The name range is the name token alone, not the declaration.
TEST(a_name_range_is_the_name_token, {
    const ast_node_t* mod = parse_text("i32 total = 0;\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(range_of(pt_decl(mod, 0)), "1:1-1:15");
    const ast_node_t* decl = pt_decl(mod, 0);
    sb_clear(&pt_out);
    msg_uint(&pt_out, decl->name_loc.line);
    sb_push(&pt_out, ':');
    msg_uint(&pt_out, decl->name_loc.col);
    sb_push(&pt_out, '-');
    msg_uint(&pt_out, decl->name_loc.end_line);
    sb_push(&pt_out, ':');
    msg_uint(&pt_out, decl->name_loc.end_col);
    TEST_ASSERT_EQ_STR(sb_cstr(&pt_out), "1:5-1:10");
})

TEST(mentions_of_a_name_carry_its_range, {
    const ast_node_t* mod = parse_text("import std.io;\n"
                                       "import a.{b as c};\n"
                                       "point d = point{.x = 1};\n"
                                       "i32 e = f.g;\n"
                                       "i32 h = p->q;\n"
                                       "io.writer w = 0;\n"
                                       "fn k(i32@ xs) void {\n"
                                       "    for (i32 v : xs) { }\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 0)->a, 0)), "std");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 0)->a, 1)), "io");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 1), 0)), "b");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 1), 0)->a), "c");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 2)->b->a), "point");
    TEST_ASSERT_EQ_STR(name_text_of(ast_child(pt_decl(mod, 2)->b->b, 0)), "x");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 3)->b), "g");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 3)->b->a), "f");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 4)->b), "q");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 5)->a->a), "io");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 5)->a->a->a), "writer");
    TEST_ASSERT_EQ_STR(name_text_of(body_stmt(mod, 6, 0)), "v");
})

// A node no name declares or mentions carries the empty range.
TEST(nodes_without_a_name_carry_an_empty_name_range, {
    const ast_node_t* mod = parse_text("i32 a = 1 + 2;\n"
                                       "string b = \"hi\";\n");
    TEST_ASSERT_NONNULL(mod);
    const ast_node_t* sum = pt_decl(mod, 0)->b;
    TEST_ASSERT_EQ_UINT64((uint64_t)sum->name_loc.line, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64((uint64_t)sum->name_loc.col, (uint64_t)0);
    TEST_ASSERT_EQ_STR(name_text_of(sum), "");
    TEST_ASSERT_EQ_STR(name_text_of(sum->a), "");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 0)->a), "");
    TEST_ASSERT_EQ_STR(name_text_of(pt_decl(mod, 1)->b), "");
})

// ---- whole modules --------------------------------------------------------

// The out-parameter signature, with every marker position in one declaration.
TEST(the_out_parameter_signature_of_d17_2, {
    const ast_node_t* mod = parse_text("fn read_file(string path, u8 mut@ own mut* out) bool {\n"
                                       "    u8 mut@ own buf = new(u8, 16);\n"
                                       "    *out = move(buf);\n"
                                       "    return true;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_STR(dumped(ast_child(pt_decl(mod, 0), 1)),
                       "(param (type (prim u8) mut (span own mut) (ptr)) out)");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 0)),
                       "(var buf (type (prim u8) mut (span own)) "
                       "(new (type (prim u8)) (int 16)))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 1)),
                       "(assign = (unary * (ident out)) (call (ident move) (ident buf)))");
})

// A module with imports, a struct, an enum, a global and a function that
// uses most of the statement forms.
TEST(a_whole_module_parses_end_to_end, {
    const ast_node_t* mod = parse_text("import std.io;\n"
                                       "import util.strings as s;\n"
                                       "\n"
                                       "struct node {\n"
                                       "    i32 value;\n"
                                       "    node mut* next;\n"
                                       "}\n"
                                       "\n"
                                       "enum color { red, green, blue }\n"
                                       "\n"
                                       "i32 LIMIT = 16;\n"
                                       "point[2] mut cells = {};\n"
                                       "\n"
                                       "extern fn abort() void;\n"
                                       "\n"
                                       "fn sum(i32@ xs) i32 {\n"
                                       "    mut i32 total = 0;\n"
                                       "    for (i32 v : xs) {\n"
                                       "        total += v;\n"
                                       "    }\n"
                                       "    return total;\n"
                                       "}\n");
    // The parser recovers and returns the tree of what parsed; the marker is
    // what the module reports, once.
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(),
                       "t.ft:17:5: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
})

TEST(a_whole_module_in_the_east_marker_spelling, {
    const ast_node_t* mod = parse_text("import std.io;\n"
                                       "import util.strings as s;\n"
                                       "\n"
                                       "struct node {\n"
                                       "    i32 value;\n"
                                       "    node mut* next;\n"
                                       "}\n"
                                       "\n"
                                       "enum color { red, green, blue }\n"
                                       "\n"
                                       "i32 LIMIT = 16;\n"
                                       "point[2] mut cells = {};\n"
                                       "\n"
                                       "extern fn abort() void;\n"
                                       "\n"
                                       "fn sum(i32@ xs) i32 {\n"
                                       "    i32 mut total = 0;\n"
                                       "    for (i32 v : xs) {\n"
                                       "        total += v;\n"
                                       "    }\n"
                                       "    return total;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_UINT64(ast_len(mod), (uint64_t)8);
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 3)),
                       "(enum color (member red nil) (member green nil) (member blue nil))");
    TEST_ASSERT_EQ_STR(dumped(ast_child(pt_decl(mod, 2), 1)),
                       "(field-decl (type (name node) mut (ptr)) next)");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 7, 1)),
                       "(range-for (type (prim i32)) v (ident xs)"
                       " (block (assign += (ident total) (ident v))))");
})

// A linked-list walk with ownership markers, `defer`, `del`, and `->`.
TEST(an_ownership_program_parses, {
    const ast_node_t* mod = parse_text("fn drop_list(node mut* own head) void {\n"
                                       "    node mut* own mut cur = move(head);\n"
                                       "    while (cur != null) {\n"
                                       "        node mut* own next = move(cur->next);\n"
                                       "        del(cur);\n"
                                       "        cur = move(next);\n"
                                       "    }\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_STR(dumped(ast_child(pt_decl(mod, 0), 0)),
                       "(param (type (name node) mut (ptr own)) head)");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 0)),
                       "(var cur (type (name node) mut (ptr own mut))"
                       " (call (ident move) (ident head)))");
    const ast_node_t* loop = body_stmt(mod, 0, 1);
    TEST_ASSERT_EQ_STR(dumped(loop->a), "(binary != (ident cur) (null))");
    TEST_ASSERT_EQ_STR(dumped(ast_child(loop->b, 0)),
                       "(var next (type (name node) mut (ptr own))"
                       " (call (ident move) (arrow (ident cur) next)))");
})

// A switch over an enum with a nested loop and a `defer`.
TEST(a_control_flow_program_parses, {
    const ast_node_t* mod = parse_text("fn run(color c, i32@ xs) i32 {\n"
                                       "    i32 mut total = 0;\n"
                                       "    defer io.flush();\n"
                                       "    switch (c) {\n"
                                       "    case color.red, color.green:\n"
                                       "        for (i32 mut i = 0; i < xs.len; i++) {\n"
                                       "            if (xs[i] > 0) {\n"
                                       "                total += xs[i];\n"
                                       "            } else if (xs[i] < 0) {\n"
                                       "                continue;\n"
                                       "            } else {\n"
                                       "                break;\n"
                                       "            }\n"
                                       "        }\n"
                                       "    default:\n"
                                       "        total = -1;\n"
                                       "    }\n"
                                       "    return total;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    const ast_node_t* sw = body_stmt(mod, 0, 2);
    TEST_ASSERT_EQ_UINT64(ast_len(sw), (uint64_t)2);
    TEST_ASSERT_EQ_STR(dumped(ast_child(ast_child(sw, 0), 0)), "(field (ident color) red)");
    TEST_ASSERT_EQ_STR(dumped(ast_child(sw, 1)),
                       "(case default (block (assign = (ident total) (unary - (int 1)))))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 1)),
                       "(defer (call-stmt (call (field (ident io) flush))))");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser_loc", argc, argv);
    TEST_RUN(a_declaration_starts_at_its_type);
    TEST_RUN(a_struct_an_enum_and_an_import_start_at_their_keyword);
    TEST_RUN(an_operator_node_is_at_its_operator);
    TEST_RUN(unary_and_postfix_nodes_are_at_their_own_token);
    TEST_RUN(literal_and_keyword_expressions_are_at_their_first_token);
    TEST_RUN(a_type_and_its_suffixes_carry_their_own_positions);
    TEST_RUN(a_statement_starts_at_its_keyword_or_target);
    TEST_RUN(a_declaration_covers_its_terminator);
    TEST_RUN(the_module_covers_the_whole_file);
    TEST_RUN(an_empty_file_is_the_empty_range_at_one_one);
    TEST_RUN(an_operator_node_runs_from_its_operator_to_its_last_operand);
    TEST_RUN(postfix_and_unary_nodes_cover_their_own_tokens);
    TEST_RUN(literal_and_keyword_expressions_cover_their_whole_form);
    TEST_RUN(a_type_covers_its_suffixes_and_markers);
    TEST_RUN(a_statement_covers_its_block_or_semicolon);
    TEST_RUN(every_if_of_a_chain_ends_at_the_end_of_the_chain);
    TEST_RUN(an_empty_case_body_is_an_empty_range_after_the_colon);
    TEST_RUN(an_import_covers_its_semicolon_and_its_items);
    TEST_RUN(a_struct_an_enum_and_an_extern_cover_their_whole_declaration);
    TEST_RUN(break_and_continue_cover_their_semicolon);
    TEST_RUN(declarations_carry_the_range_of_their_name);
    TEST_RUN(a_name_range_is_the_name_token);
    TEST_RUN(mentions_of_a_name_carry_its_range);
    TEST_RUN(nodes_without_a_name_carry_an_empty_name_range);
    TEST_RUN(the_out_parameter_signature_of_d17_2);
    TEST_RUN(a_whole_module_parses_end_to_end);
    TEST_RUN(a_whole_module_in_the_east_marker_spelling);
    TEST_RUN(an_ownership_program_parses);
    TEST_RUN(a_control_flow_program_parses);
    parse_done();
    TEST_EXIT();
}
