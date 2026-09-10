// The positions the parser records and the whole modules it accepts: every
// later pass reports at a node's `loc` (D14.2), so each kind of node is
// pinned to the token a diagnostic about it should point at, and the module
// shapes the specification writes are parsed end to end.
#include <stdint.h>

#include "ast.h"
#include "parser.h"
#include "parser_helpers.h"

#include "test.h"

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
    const ast_node_t* mod = parse_text("i32 N = 4;\nfn i32 f(i32 a) {\n    return a;\n}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(loc_of(mod), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->a), "1:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 0)->b), "1:9");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)), "2:1");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)->a), "2:4");
    TEST_ASSERT_EQ_STR(loc_of(ast_child(pt_decl(mod, 1), 0)), "2:10");
    TEST_ASSERT_EQ_STR(loc_of(pt_decl(mod, 1)->b), "2:17");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 1, 0)), "3:5");
    TEST_ASSERT_EQ_STR(loc_of(body_stmt(mod, 1, 0)->a), "3:12");
})

TEST(a_struct_an_enum_and_an_import_start_at_their_keyword, {
    const ast_node_t* mod = parse_text("import std::io;\n"
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
// placement diagnostic points at the marker it is about (D5.3).
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
    const ast_node_t* mod = parse_text("fn void f() {\n"
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

// ---- whole modules --------------------------------------------------------

// The out-parameter signature of D3.6 and D17.2, with every marker position
// in one declaration.
TEST(the_out_parameter_signature_of_d17_2, {
    const ast_node_t* mod = parse_text("fn bool read_file(string path, u8 mut@ own mut* out) {\n"
                                       "    u8 mut@ own buf = new(u8, 16);\n"
                                       "    *out = move(buf);\n"
                                       "    return true;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_STR(dumped(ast_child(pt_decl(mod, 0), 1)),
                       "(param (type (prim u8) mut (slice own mut) (ptr)) out)");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 0)),
                       "(var buf (type (prim u8) mut (slice own)) "
                       "(new (type (prim u8)) (int 16)))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 1)),
                       "(assign = (unary * (ident out)) (call (ident move) (ident buf)))");
})

// A module with imports, a struct, an enum, a global and a function that
// uses most of the statement forms.
TEST(a_whole_module_parses_end_to_end, {
    const ast_node_t* mod = parse_text("import std::io;\n"
                                       "import util::strings as s;\n"
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
                                       "extern fn void abort();\n"
                                       "\n"
                                       "fn i32 sum(i32@ xs) {\n"
                                       "    mut i32 total = 0;\n"
                                       "    for (i32 v : xs) {\n"
                                       "        total += v;\n"
                                       "    }\n"
                                       "    return total;\n"
                                       "}\n");
    TEST_ASSERT_NULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(),
                       "t.ft:17:5: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
})

TEST(a_whole_module_in_the_east_marker_spelling, {
    const ast_node_t* mod = parse_text("import std::io;\n"
                                       "import util::strings as s;\n"
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
                                       "extern fn void abort();\n"
                                       "\n"
                                       "fn i32 sum(i32@ xs) {\n"
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

// A linked-list walk with ownership markers, `defer`, `del` and `->`, the
// shape memory-model.md writes.
TEST(an_ownership_program_parses, {
    const ast_node_t* mod = parse_text("fn void drop_list(node mut* own head) {\n"
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

// A switch over an enum with a nested loop and a `defer`, the shape
// core-language.md writes.
TEST(a_control_flow_program_parses, {
    const ast_node_t* mod = parse_text("fn i32 run(color c, i32@ xs) {\n"
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
    TEST_RUN(the_out_parameter_signature_of_d17_2);
    TEST_RUN(a_whole_module_parses_end_to_end);
    TEST_RUN(a_whole_module_in_the_east_marker_spelling);
    TEST_RUN(an_ownership_program_parses);
    TEST_RUN(a_control_flow_program_parses);
    parse_done();
    TEST_EXIT();
}
