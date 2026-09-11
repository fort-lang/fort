// Unit tests of the checker's symbols and declarations (core-language.md 3;
// D3.4, D3.8, D3.9, D5.5, D7.10, D8.6, D9.4, D14.2): the symbol record of
// every kind, the two-phase resolution and its cycles, the marker refusals
// the type table delegates, and the poisoning that keeps one error one error.
// The expressions and the statements are the other halves, in
// check_expr_test.c and check_stmt_test.c.
#include "check.h"

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check_helpers.h"
#include "consts.h"
#include "sym.h"
#include "types.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the line and column numbers of the
// sources below are the test data.

// ---- one record per kind of sym_kind_t (sym.h) --------------------------------------

TEST(a_module_carries_its_own_symbol, {
    TEST_ASSERT_TRUE(check_src("fn i32 main() {\n    return 0;\n}\n"));
    const module_t* m = module_at("main");
    TEST_ASSERT_NONNULL(m);
    const sym_t* s = m->ast->sym;
    TEST_ASSERT_NONNULL(s);
    TEST_ASSERT_EQ_STR(sym_kind_name(s->kind), "module");
    TEST_ASSERT_EQ_STR(s->name.ptr, "main");
    TEST_ASSERT_NULL(s->type);
    TEST_ASSERT_NULL(s->owner);
})

TEST(a_function_declaration_is_its_own_symbol, {
    TEST_ASSERT_TRUE(check_src("fn i32 main() {\n    return 0;\n}\n"));
    const sym_t* s = sym_main("main");
    TEST_ASSERT_NONNULL(s);
    TEST_ASSERT_EQ_STR(sym_kind_name(s->kind), "fn");
    // Every declaration node carries its own symbol (the symbol contract).
    TEST_ASSERT_TRUE(s->node == node_in_main(AST_FN_DECL, "main"));
    TEST_ASSERT_TRUE(s->node->sym == s);
    // The record is found at the name token, not at the `fn` (D20.4).
    TEST_ASSERT_EQ_UINT64((uint64_t)s->decl.line, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)s->decl.col, (uint64_t)8);
    TEST_ASSERT_EQ_STR(type_text(s->type), "fn i32()");
    TEST_ASSERT_TRUE(s->owner == module_at("main")->ast->sym);
})

TEST(an_extern_declaration_has_its_own_kind, {
    TEST_ASSERT_TRUE(check_src("extern fn i32 puts(char* s);\n"
                               "fn i32 main() {\n    return 0;\n}\n"));
    const sym_t* s = sym_main("puts");
    TEST_ASSERT_EQ_STR(sym_kind_name(s->kind), "extern fn");
    TEST_ASSERT_EQ_STR(type_text(s->type), "fn i32(char*)");
})

TEST(a_parameter_is_a_symbol_owned_by_its_function, {
    TEST_ASSERT_TRUE(check_src("fn i32 twice(i32 mut n) {\n    n = n + n;\n    return n;\n}\n"
                               "fn i32 main() {\n    return twice(2);\n}\n"));
    const ast_node_t* p = node_in_main(AST_PARAM, "n");
    TEST_ASSERT_NONNULL(p);
    const sym_t* s = p->sym;
    TEST_ASSERT_EQ_STR(sym_kind_name(s->kind), "parameter");
    TEST_ASSERT_TRUE(s->node == p);
    TEST_ASSERT_TRUE(s->owner == sym_main("twice"));
    // `mut` in the outermost position makes the callee's copy assignable
    // (D5.6).
    TEST_ASSERT_TRUE(s->mut0);
    TEST_ASSERT_EQ_STR(sym_type_text(s), "i32 mut");
})

TEST(a_struct_and_its_fields_are_symbols, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "fn i32 main() {\n    point p = {1, 2};\n    return p.x;\n}\n"));
    const sym_t* s = sym_main("point");
    TEST_ASSERT_EQ_STR(sym_kind_name(s->kind), "struct");
    TEST_ASSERT_EQ_STR(type_text(s->type), "point");
    const ast_node_t* f = node_in_main(AST_FIELD_DECL, "y");
    TEST_ASSERT_NONNULL(f);
    TEST_ASSERT_EQ_STR(sym_kind_name(f->sym->kind), "field");
    TEST_ASSERT_TRUE(f->sym->owner == s);
    // The C layout of D3.8 puts the second i32 at offset 4.
    TEST_ASSERT_EQ_UINT64(f->aux, (uint64_t)4);
})

TEST(an_enum_and_its_members_are_symbols, {
    TEST_ASSERT_TRUE(check_src("enum color {\n    red,\n    green = 5,\n    blue,\n}\n"
                               "fn i32 main() {\n    color c = color.blue;\n"
                               "    return cast(c, i32);\n}\n"));
    const sym_t* s = sym_main("color");
    TEST_ASSERT_EQ_STR(sym_kind_name(s->kind), "enum");
    const ast_node_t* m = node_in_main(AST_ENUM_MEMBER, "blue");
    TEST_ASSERT_EQ_STR(sym_kind_name(m->sym->kind), "enum member");
    TEST_ASSERT_TRUE(m->sym->owner == s);
    TEST_ASSERT_TRUE(m->sym->type == s->type);
    // Values start at 0 and increment from the last explicit one (D3.9).
    int64_t v = 0;
    TEST_ASSERT_TRUE(cv_to_i64(check_node_value(&checker, m), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)6);
})

TEST(a_module_declaration_is_a_constant_or_a_global, {
    TEST_ASSERT_TRUE(check_src("i32 MAX = 64;\ni32 mut counter = 0;\n"
                               "fn i32 main() {\n    counter = MAX;\n    return counter;\n}\n"));
    // `Type NAME = init;` is a compile-time constant, `mut Type g` a global
    // (D7.10).
    TEST_ASSERT_EQ_STR(sym_kind_name(sym_main("MAX")->kind), "constant");
    TEST_ASSERT_FALSE(sym_main("MAX")->mut0);
    TEST_ASSERT_EQ_STR(sym_kind_name(sym_main("counter")->kind), "global");
    TEST_ASSERT_TRUE(sym_main("counter")->mut0);
})

TEST(a_local_is_a_symbol_owned_by_its_function, {
    TEST_ASSERT_TRUE(check_body("    i32 mut x = 1;\n    x = 2;"));
    const ast_node_t* d = node_in_main(AST_VAR_DECL, "x");
    TEST_ASSERT_NONNULL(d);
    TEST_ASSERT_EQ_STR(sym_kind_name(d->sym->kind), "local");
    TEST_ASSERT_TRUE(d->sym->node == d);
    TEST_ASSERT_TRUE(d->sym->owner == sym_main("main"));
    TEST_ASSERT_EQ_STR(sym_type_text(d->sym), "i32 mut");
})

TEST(a_builtin_is_a_symbol_with_no_node, {
    TEST_ASSERT_TRUE(check_body("    println(1);"));
    const ast_node_t* callee = node_in_main(AST_IDENT, "println");
    TEST_ASSERT_NONNULL(callee);
    TEST_ASSERT_NONNULL(callee->sym);
    TEST_ASSERT_EQ_STR(sym_kind_name(callee->sym->kind), "builtin");
    // A builtin is declared by no source, so it has no node and no type
    // (D12.2).
    TEST_ASSERT_NULL(callee->sym->node);
    TEST_ASSERT_NULL(callee->sym->type);
})

TEST(every_symbol_kind_is_recorded, {
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft",
        "import util;\n"
        "struct point {\n    i32 x;\n}\n"
        "enum color {\n    red,\n}\n"
        "i32 MAX = 2;\n"
        "i32 mut hits = 0;\n"
        "extern fn i32 puts(char* s);\n"
        "fn i32 add(i32 a) {\n    i32 b = a;\n    println(b);\n"
        "    return b + MAX;\n}\n"
        "fn i32 main() {\n    point p = {1};\n"
        "    color c = color.red;\n    hits = util.one();\n"
        "    return add(p.x) + cast(c, i32);\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    bool seen[SYM_KIND_COUNT];
    for (uint64_t k = 0; k < (uint64_t)SYM_KIND_COUNT; k++) {
        seen[k] = false;
    }
    for (uint64_t i = 0; i < check_sym_count(&checker); i++) {
        seen[check_sym_at(&checker, i)->kind] = true;
    }
    for (uint64_t k = 0; k < (uint64_t)SYM_KIND_COUNT; k++) {
        TEST_LOG_("kind %s", sym_kind_name((sym_kind_t)k));
        TEST_ASSERT_TRUE(seen[k]);
    }
})

TEST(every_expression_of_a_clean_module_carries_a_type, {
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft",
        "import util;\n"
        "struct point {\n    i32 x;\n    i32 y;\n}\n"
        "enum color {\n    red,\n    green = 4,\n}\n"
        "i32 MAX = 2 + 3;\n"
        "i32[MAX] mut table = {};\n"
        "fn i32 sum(point p, color c, string s) {\n"
        "    i32 mut total = p.x + p.y;\n"
        "    u8 mut@ own bytes = new(u8, 4);\n"
        "    for (u64 mut i = 0; i < bytes.len; i++) {\n"
        "        bytes[i] = cast(s.len, u8);\n"
        "    }\n"
        "    for (char ch : s) {\n        total = total + cast(ch, i32);\n    }\n"
        "    switch (c) {\n    case color.red:\n        total = total + 1;\n"
        "    default:\n    }\n"
        "    defer del(bytes);\n"
        "    if (total > 0 && !(total < 0)) {\n        println(total, ' ', s, true, null);\n"
        "    }\n"
        "    table[0] = sizeof(point) > 0 ? 1 : 1;\n"
        "    return total + util.one() + MAX;\n}\n"
        "fn i32 main() {\n    point p = point{.x = 1, .y = 2};\n"
        "    return sum(p, color.green, \"ab\");\n}\n");
    // The ternary is outside the bootstrap's subset, so the source above uses
    // it nowhere else; drop it before checking.
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft",
        "import util;\n"
        "struct point {\n    i32 x;\n    i32 y;\n}\n"
        "enum color {\n    red,\n    green = 4,\n}\n"
        "i32 MAX = 2 + 3;\n"
        "i32[MAX] mut table = {};\n"
        "fn i32 sum(point p, color c, string s) {\n"
        "    i32 mut total = p.x + p.y;\n"
        "    u8 mut@ own bytes = new(u8, 4);\n"
        "    defer del(bytes);\n"
        "    for (u64 mut i = 0; i < bytes.len; i++) {\n"
        "        bytes[i] = cast(s.len, u8);\n"
        "    }\n"
        "    for (char ch : s) {\n        total = total + cast(ch, i32);\n    }\n"
        "    switch (c) {\n    case color.red:\n        total = total + 1;\n"
        "    default:\n    }\n"
        "    if (total > 0 && !(total < 0)) {\n        println(total, ' ', s, true);\n"
        "    }\n"
        "    table[0] = cast(sizeof(point), i32);\n"
        "    return total + util.one() + MAX;\n}\n"
        "fn i32 main() {\n    point p = point{.x = 1, .y = 2};\n"
        "    return sum(p, color.green, \"ab\");\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    const ast_node_t* missing = untyped_expr(module_at("main")->ast);
    if (missing != NULL) {
        TEST_LOG_("untyped %s at %u:%u",
                  ast_kind_name(missing->kind),
                  missing->loc.line,
                  missing->loc.col);
    }
    TEST_ASSERT_NULL(missing);
    TEST_ASSERT_NULL(untyped_expr(module_at("util")->ast));
})

// ---- what a name token denotes (the symbol contract) --------------------------------

TEST(an_identifier_carries_the_declaration_it_denotes, {
    TEST_ASSERT_TRUE(check_body("    i32 x = 1;\n    println(x);"));
    const ast_node_t* use = node_find(node_in_main(AST_CALL, NULL), AST_IDENT, "x");
    TEST_ASSERT_NONNULL(use);
    TEST_ASSERT_TRUE(use->sym == node_in_main(AST_VAR_DECL, "x")->sym);
    TEST_ASSERT_EQ_STR(type_text(use->type), "i32");
})

TEST(an_unresolved_name_has_no_symbol_and_one_diagnostic, {
    TEST_ASSERT_FALSE(check_body("    println(nope);"));
    TEST_ASSERT_TRUE(said("unknown name 'nope'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_NULL(node_in_main(AST_IDENT, "nope")->sym);
})

TEST(an_unqualified_enum_member_says_how_it_is_written, {
    TEST_ASSERT_FALSE(check_src("enum color {\n    red,\n}\n"
                                "fn i32 main() {\n    color c = red;\n    return 0;\n}\n"));
    // An enum member is scoped to its enum (D3.9).
    TEST_ASSERT_TRUE(said("color.red"));
})

TEST(a_field_access_carries_the_field, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "fn i32 main() {\n    point p = {1, 2};\n    return p.y;\n}\n"));
    const ast_node_t* access = node_in_main(AST_FIELD, "y");
    TEST_ASSERT_NONNULL(access);
    TEST_ASSERT_TRUE(access->sym == node_in_main(AST_FIELD_DECL, "y")->sym);
})

TEST(an_enum_member_access_carries_the_member, {
    TEST_ASSERT_TRUE(check_src("enum color {\n    red,\n}\n"
                               "fn i32 main() {\n    color c = color.red;\n    return 0;\n}\n"));
    const ast_node_t* access = node_in_main(AST_FIELD, "red");
    TEST_ASSERT_TRUE(access->sym == node_in_main(AST_ENUM_MEMBER, "red")->sym);
    // The operand of the access denotes the enum itself.
    TEST_ASSERT_TRUE(access->a->sym == sym_main("color"));
})

TEST(a_designator_carries_the_field, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "fn i32 main() {\n    point p = point{.y = 2};\n"
                               "    return p.y;\n}\n"));
    const ast_node_t* d = node_in_main(AST_DESIGNATOR, "y");
    TEST_ASSERT_NONNULL(d);
    TEST_ASSERT_TRUE(d->sym == node_in_main(AST_FIELD_DECL, "y")->sym);
})

TEST(a_type_name_carries_the_declaration, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n}\n"
                               "fn i32 main() {\n    point p = {1};\n    return p.x;\n}\n"));
    const ast_node_t* t = node_in_main(AST_TYPE_NAME, "point");
    TEST_ASSERT_NONNULL(t);
    TEST_ASSERT_TRUE(t->sym == sym_main("point"));
})

// ---- imports (D9.3, D9.4) -----------------------------------------------------------

TEST(an_import_carries_the_module_it_binds, {
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft", "import util as u;\nfn i32 main() {\n    return u.one();\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    const sym_t* util = module_at("util")->ast->sym;
    const ast_node_t* imp = node_find(module_at("main")->ast, AST_IMPORT, NULL);
    TEST_ASSERT_NONNULL(imp);
    // The import, its `as` alias and the last path segment all denote the
    // module (D9.3).
    TEST_ASSERT_TRUE(imp->sym == util);
    TEST_ASSERT_TRUE(imp->b->sym == util);
    TEST_ASSERT_TRUE(ast_child(imp->a, 0)->sym == util);
    // So does the qualified name in the call.
    TEST_ASSERT_TRUE(node_find(module_at("main")->ast, AST_IDENT, "u")->sym == util);
})

TEST(an_import_item_carries_the_declaration, {
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft", "import util::{one as first};\nfn i32 main() {\n    return first();\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    const sym_t* one = sym_of("util", "one");
    const ast_node_t* item = node_find(module_at("main")->ast, AST_IMPORT_ITEM, "one");
    TEST_ASSERT_NONNULL(item);
    TEST_ASSERT_TRUE(item->sym == one);
    TEST_ASSERT_TRUE(item->a->sym == one);
})

TEST(a_qualified_type_name_carries_the_module_and_the_type, {
    begin();
    add("geom.ft", "struct point {\n    i32 x;\n}\n");
    add("main.ft", "import geom;\nfn i32 main() {\n    geom.point p = {1};\n    return p.x;\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    const ast_node_t* t = node_find(module_at("main")->ast, AST_TYPE_NAME, "geom");
    TEST_ASSERT_NONNULL(t);
    // The type name denotes the module and its child holds the type (the
    // symbol contract, D9.4).
    TEST_ASSERT_TRUE(t->sym == module_at("geom")->ast->sym);
    TEST_ASSERT_TRUE(t->a->sym == sym_of("geom", "point"));
})

// ---- two-phase resolution (D7.10) ---------------------------------------------------

TEST(declarations_are_order_independent, {
    TEST_ASSERT_TRUE(check_src("fn i32 main() {\n    point p = {SIZE};\n    return p.x;\n}\n"
                               "i32 SIZE = 7;\n"
                               "struct point {\n    i32 x;\n}\n"));
    TEST_ASSERT_EQ_STR(type_text(sym_main("main")->type), "fn i32()");
    TEST_ASSERT_EQ_UINT64(type_sizeof(sym_main("point")->type), (uint64_t)4);
})

TEST(a_constant_sizes_an_array_before_its_own_declaration, {
    TEST_ASSERT_TRUE(check_src("i32[MAX] table = {};\ni32 MAX = 4;\n"
                               "fn i32 main() {\n    return table[3];\n}\n"));
    TEST_ASSERT_EQ_STR(sym_type_text(sym_main("table")), "i32[4]");
})

TEST(a_constant_of_another_module_sizes_an_array, {
    begin();
    add("limits.ft", "i32 MAX = 3;\n");
    add("main.ft",
        "import limits;\ni32[limits.MAX] table = {};\n"
        "fn i32 main() {\n    return table[2];\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    TEST_ASSERT_EQ_STR(sym_type_text(sym_main("table")), "i32[3]");
})

TEST(a_struct_may_contain_itself_through_a_pointer, {
    TEST_ASSERT_TRUE(
        check_src("struct node {\n    i32 value;\n    node mut* next;\n}\n"
                  "fn i32 main() {\n    node n = {1, null};\n    return n.value;\n}\n"));
    // A pointer field is one word, so the struct is 16 bytes with padding
    // (D3.8).
    TEST_ASSERT_EQ_UINT64(type_sizeof(sym_main("node")->type), (uint64_t)16);
})

TEST(a_value_containment_cycle_is_an_infinite_size_error, {
    TEST_ASSERT_FALSE(check_src("struct loop {\n    i32 v;\n    loop next;\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("struct loop has infinite size"));
    // Reported at the `struct` keyword, which is line 1 column 1 (D14.2).
    TEST_ASSERT_TRUE(said("main.ft:1:1: error:"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(sym_main("loop")->error);
})

TEST(a_cycle_of_two_structs_is_reported_once, {
    TEST_ASSERT_FALSE(check_src("struct a {\n    b x;\n}\nstruct b {\n    a y;\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(sym_main("a")->error);
    TEST_ASSERT_TRUE(sym_main("b")->error);
})

TEST(a_constant_initializer_cycle_is_an_error, {
    TEST_ASSERT_FALSE(check_src("i32 A = B;\ni32 B = A;\n"
                                "fn i32 main() {\n    return A;\n}\n"));
    // Constant references are evaluated lazily with cycle detection (D4.6).
    TEST_ASSERT_TRUE(said("is defined in terms of itself"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(an_enum_value_may_not_refer_to_its_own_enum, {
    TEST_ASSERT_FALSE(check_src("enum color {\n    red = 1,\n    green = cast(color.red, i32),\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'color' is defined in terms of itself"));
})

// ---- poisoning (D14.2) --------------------------------------------------------------

TEST(a_failed_declaration_gets_the_error_type, {
    TEST_ASSERT_FALSE(check_src("i32 A = nope;\nfn i32 main() {\n    return 0;\n}\n"));
    const sym_t* s = sym_main("A");
    TEST_ASSERT_TRUE(s->error);
    TEST_ASSERT_EQ_STR(type_text(s->type), "<error>");
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_failed_declaration_silences_its_uses, {
    TEST_ASSERT_FALSE(check_src("i32 A = nope;\nfn i32 main() {\n    return A + 1;\n}\n"));
    // One unknown name is one diagnostic: the error type silences the uses
    // (D14.2).
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(an_error_typed_declaration_silences_its_uses_in_an_importing_module, {
    begin();
    add("util.ft", "i32 A = nope;\n");
    add("main.ft", "import util::A;\nfn i32 main() {\n    return A + 1;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("util.ft:1:9: error: unknown name 'nope'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(an_importer_is_checked_in_full_after_its_import_failed, {
    begin();
    add("util.ft", "i32 A = nope;\n");
    add("main.ft", "import util::A;\nfn i32 main() {\n    i32 x = \"s\";\n    return x + A;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // Every module of the closure is checked in dependency order, and the
    // importer sees its own errors and no cascade of the import's (D14.2 as
    // amended).
    TEST_ASSERT_TRUE(said("util.ft:1:9: error: unknown name 'nope'"));
    TEST_ASSERT_TRUE(said("main.ft:3:13: error: the initializer expects i32, not string"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)2);
})

// ---- the marker refusals the type table delegates ------------------------------------

TEST(a_mut_on_a_field_is_refused, {
    TEST_ASSERT_FALSE(check_src("struct counter {\n    i32 mut hits;\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    // A field's own storage follows its struct (D5.5).
    TEST_ASSERT_TRUE(said("a field's own storage follows its struct"));
    TEST_ASSERT_TRUE(said("main.ft:2:5:"));
})

TEST(a_mut_on_a_return_type_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn i32 mut f() {\n    return 1;\n}\n"
                                "fn i32 main() {\n    return f();\n}\n"));
    // A return type has no binding (D5.5).
    TEST_ASSERT_TRUE(said("a return type has no binding"));
})

TEST(a_mut_on_a_cast_target_is_refused, {
    TEST_ASSERT_FALSE(check_body("    string s = \"ab\";\n    u8@ b = cast(s, u8@ mut);\n"
                                 "    println(b.len);"));
    // A cast result has no binding (D3.14).
    TEST_ASSERT_TRUE(said("a cast result has no binding"));
})

TEST(a_mut_on_a_pointee_or_a_binding_is_kept, {
    TEST_ASSERT_TRUE(check_src("struct node {\n    node mut* next;\n}\n"
                               "fn node mut* find(node mut* n) {\n    return n;\n}\n"
                               "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(type_text(sym_main("find")->type), "fn node mut*(node mut*)");
})

// ---- sizes (D3.4) -------------------------------------------------------------------

TEST(a_type_that_is_too_large_is_refused, {
    TEST_ASSERT_FALSE(check_src("i32[4611686018427387904] BIG = {};\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("type is too large"));
})

TEST(a_struct_that_is_too_large_is_reported_at_its_keyword, {
    TEST_ASSERT_FALSE(check_src("struct huge {\n    i8[4611686018427387904] a;\n"
                                "    i8[4611686018427387904] b;\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:1:1: error: type is too large: struct huge"));
})

TEST(an_array_length_must_be_a_constant_expression, {
    TEST_ASSERT_FALSE(check_body("    i32 n = 3;\n    i32[n] a = {};\n    println(a[0]);"));
    TEST_ASSERT_TRUE(said("an array length must be a constant expression"));
})

TEST(an_array_length_must_be_greater_than_zero, {
    TEST_ASSERT_FALSE(check_body("    i32[0] z = {};\n    println(z[0]);"));
    TEST_ASSERT_TRUE(said("an array length must be greater than 0"));
})

TEST(sizeof_lays_out_a_struct_on_demand, {
    TEST_ASSERT_TRUE(check_src("fn i32 main() {\n    return cast(sizeof(point), i32);\n}\n"
                               "struct point {\n    i32 x;\n    i64 y;\n}\n"));
    const ast_node_t* n = node_in_main(AST_SIZEOF, NULL);
    int64_t v = 0;
    TEST_ASSERT_TRUE(cv_to_i64(check_node_value(&checker, n), &v));
    // C layout: an i32, four bytes of padding and an i64 (D3.8).
    TEST_ASSERT_EQ_INT64(v, (int64_t)16);
})

// ---- enums (D3.9) and extern signatures (D9.8) --------------------------------------

TEST(duplicate_enum_values_are_refused, {
    TEST_ASSERT_FALSE(check_src("enum color {\n    red = 1,\n    green = 1,\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("duplicate enum value 1 for 'green'"));
})

TEST(an_enum_value_must_fit_i32, {
    TEST_ASSERT_FALSE(check_src("enum big {\n    x = 2147483648,\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("does not fit i32"));
})

TEST(an_extern_signature_cannot_use_a_string, {
    TEST_ASSERT_FALSE(check_src("extern fn i32 puts(string s);\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    // An extern signature may use only scalars, pointers and function
    // pointers (D9.8).
    // The diagnostic of module-system.md 13.
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'string'"));
})

// ---- the entry point (D8.6) ---------------------------------------------------------

TEST(an_entry_module_without_main_is_reported_at_one_one, {
    TEST_ASSERT_FALSE(check_src("i32 A = 1;\n"));
    // The diagnostic of module-system.md 13, at 1:1 since it has no position
    // in the file (D8.6, D14.2).
    TEST_ASSERT_TRUE(said("main.ft:1:1: error: entry module 'main' must define 'fn i32 main()' "
                          "or 'fn i32 main(string@ args)'"));
})

TEST(a_main_returning_void_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn void main() {\n}\n"));
    TEST_ASSERT_TRUE(said("must define"));
    // The diagnostic stands at the declaration when one is there (D14.2).
    TEST_ASSERT_TRUE(said("main.ft:1:9:"));
})

TEST(a_main_with_a_wrong_signature_is_refused_in_both_modes, {
    // The entry rule stands down for a missing `main` under a check of one
    // module, never for one that is there and wrong (D8.6, D20.1).
    begin();
    add("main.ft", "fn i32 main(i32 n) {\n    return n;\n}\n");
    want_main = false;
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("must define"));
    begin();
    add("main.ft", "fn i32 main(i32 n) {\n    return n;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("must define"));
})

TEST(main_may_take_the_argument_span, {
    TEST_ASSERT_TRUE(
        check_src("fn i32 main(string@ args) {\n    return cast(args.len, i32);\n}\n"));
    TEST_ASSERT_EQ_STR(type_text(sym_main("main")->type), "fn i32(string@)");
})

TEST(a_main_in_another_module_is_ordinary, {
    begin();
    add("util.ft", "fn void main() {\n}\n");
    add("main.ft", "import util;\nfn i32 main() {\n    util.main();\n    return 0;\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(a_check_without_the_main_rule_accepts_a_module_without_one, {
    begin();
    add("main.ft", "i32 A = 1;\n");
    want_main = false;
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(the_mute_flag_counts_without_reporting, {
    begin();
    add("main.ft", "fn i32 main() {\n    return nope;\n}\n");
    want_mute = true;
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // A muted checker annotates the file without emitting semantic noise
    // (D20.2).
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(checker.errors, (uint64_t)1);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check", argc, argv);
    TEST_RUN(a_module_carries_its_own_symbol);
    TEST_RUN(a_function_declaration_is_its_own_symbol);
    TEST_RUN(an_extern_declaration_has_its_own_kind);
    TEST_RUN(a_parameter_is_a_symbol_owned_by_its_function);
    TEST_RUN(a_struct_and_its_fields_are_symbols);
    TEST_RUN(an_enum_and_its_members_are_symbols);
    TEST_RUN(a_module_declaration_is_a_constant_or_a_global);
    TEST_RUN(a_local_is_a_symbol_owned_by_its_function);
    TEST_RUN(a_builtin_is_a_symbol_with_no_node);
    TEST_RUN(every_symbol_kind_is_recorded);
    TEST_RUN(every_expression_of_a_clean_module_carries_a_type);
    TEST_RUN(an_identifier_carries_the_declaration_it_denotes);
    TEST_RUN(an_unresolved_name_has_no_symbol_and_one_diagnostic);
    TEST_RUN(an_unqualified_enum_member_says_how_it_is_written);
    TEST_RUN(a_field_access_carries_the_field);
    TEST_RUN(an_enum_member_access_carries_the_member);
    TEST_RUN(a_designator_carries_the_field);
    TEST_RUN(a_type_name_carries_the_declaration);
    TEST_RUN(an_import_carries_the_module_it_binds);
    TEST_RUN(an_import_item_carries_the_declaration);
    TEST_RUN(a_qualified_type_name_carries_the_module_and_the_type);
    TEST_RUN(declarations_are_order_independent);
    TEST_RUN(a_constant_sizes_an_array_before_its_own_declaration);
    TEST_RUN(a_constant_of_another_module_sizes_an_array);
    TEST_RUN(a_struct_may_contain_itself_through_a_pointer);
    TEST_RUN(a_value_containment_cycle_is_an_infinite_size_error);
    TEST_RUN(a_cycle_of_two_structs_is_reported_once);
    TEST_RUN(a_constant_initializer_cycle_is_an_error);
    TEST_RUN(an_enum_value_may_not_refer_to_its_own_enum);
    TEST_RUN(a_failed_declaration_gets_the_error_type);
    TEST_RUN(a_failed_declaration_silences_its_uses);
    TEST_RUN(an_error_typed_declaration_silences_its_uses_in_an_importing_module);
    TEST_RUN(an_importer_is_checked_in_full_after_its_import_failed);
    TEST_RUN(a_mut_on_a_field_is_refused);
    TEST_RUN(a_mut_on_a_return_type_is_refused);
    TEST_RUN(a_mut_on_a_cast_target_is_refused);
    TEST_RUN(a_mut_on_a_pointee_or_a_binding_is_kept);
    TEST_RUN(a_type_that_is_too_large_is_refused);
    TEST_RUN(a_struct_that_is_too_large_is_reported_at_its_keyword);
    TEST_RUN(an_array_length_must_be_a_constant_expression);
    TEST_RUN(an_array_length_must_be_greater_than_zero);
    TEST_RUN(sizeof_lays_out_a_struct_on_demand);
    TEST_RUN(duplicate_enum_values_are_refused);
    TEST_RUN(an_enum_value_must_fit_i32);
    TEST_RUN(an_extern_signature_cannot_use_a_string);
    TEST_RUN(an_entry_module_without_main_is_reported_at_one_one);
    TEST_RUN(a_main_returning_void_is_refused);
    TEST_RUN(a_main_with_a_wrong_signature_is_refused_in_both_modes);
    TEST_RUN(main_may_take_the_argument_span);
    TEST_RUN(a_main_in_another_module_is_ordinary);
    TEST_RUN(a_check_without_the_main_rule_accepts_a_module_without_one);
    TEST_RUN(the_mute_flag_counts_without_reporting);
    check_reset();
    done();
    TEST_EXIT();
}
