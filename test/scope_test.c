// Unit tests of the namespaces and scopes of module-system.md 5: one
// namespace per module, collisions whatever the kinds, the block scopes a
// local may shadow through, the enclosing locals it may not, and the universe
// scope.
// D7.9, D12.2
#include "scope.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "diag.h"
#include "str.h"

#include "test.h"

// The position every binding of a test that does not care about positions is
// declared at.
static loc_t at(uint32_t line) {
    return loc_make("t.ft", line, 1);
}

static str_t name(const char* text) {
    return str_from_cstr(text);
}

// A module namespace with three declarations, the shape most tests start
// from.
static void module_scope(scope_t* s) {
    scope_init(s, SCOPE_MODULE, NULL);
    TEST_UNUSED(scope_declare(s, name("add"), BIND_FN, at(1), NULL));
    TEST_UNUSED(scope_declare(s, name("node"), BIND_STRUCT, at(2), NULL));
    TEST_UNUSED(scope_declare(s, name("LIMIT"), BIND_VAR, at(3), NULL));
}

// ---- declaring and finding ---------------------------------------------------------

TEST(a_fresh_module_namespace_is_empty, {
    scope_t s;
    scope_init(&s, SCOPE_MODULE, NULL);
    TEST_ASSERT_EQ_UINT64(scope_count(&s), (uint64_t)0);
    TEST_ASSERT_NULL(scope_find(&s, name("add")));
    scope_free(&s);
})

TEST(a_declaration_is_found_by_name, {
    scope_t s;
    module_scope(&s);
    const binding_t* b = scope_find(&s, name("node"));
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_STRUCT);
    TEST_ASSERT_EQ_UINT64((uint64_t)b->loc.line, (uint64_t)2);
    scope_free(&s);
})

TEST(a_name_that_was_never_declared_is_not_found, {
    scope_t s;
    module_scope(&s);
    TEST_ASSERT_NULL(scope_find(&s, name("sub")));
    scope_free(&s);
})

TEST(bindings_are_kept_in_declaration_order, {
    scope_t s;
    module_scope(&s);
    TEST_ASSERT_EQ_UINT64(scope_count(&s), (uint64_t)3);
    TEST_ASSERT_EQ_UINT64((uint64_t)scope_at(&s, 0)->loc.line, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)scope_at(&s, 1)->loc.line, (uint64_t)2);
    TEST_ASSERT_EQ_UINT64((uint64_t)scope_at(&s, 2)->loc.line, (uint64_t)3);
    scope_free(&s);
})

TEST(the_declaring_node_is_kept_on_the_binding, {
    ast_arena_t arena;
    ast_arena_init(&arena);
    ast_node_t* decl = ast_new(&arena, AST_FN_DECL, at(7));
    scope_t s;
    scope_init(&s, SCOPE_MODULE, NULL);
    const binding_t* b = scope_declare(&s, name("add"), BIND_FN, at(7), decl);
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_TRUE(b->node == decl);
    scope_free(&s);
    ast_arena_free(&arena);
})

// ---- collisions ---------------------------------------------------------------------
// D7.9

TEST(a_second_binding_of_one_name_is_refused, {
    scope_t s;
    module_scope(&s);
    TEST_ASSERT_NULL(scope_declare(&s, name("add"), BIND_FN, at(9), NULL));
    TEST_ASSERT_EQ_UINT64(scope_count(&s), (uint64_t)3);
    scope_free(&s);
})

TEST(a_struct_and_a_function_of_one_name_collide, {
    scope_t s;
    module_scope(&s);
    TEST_ASSERT_NULL(scope_declare(&s, name("node"), BIND_FN, at(9), NULL));
    scope_free(&s);
})

TEST(an_import_binding_collides_with_a_declaration, {
    scope_t s;
    module_scope(&s);
    TEST_ASSERT_NULL(scope_declare(&s, name("add"), BIND_SYMBOL, at(9), NULL));
    TEST_ASSERT_NULL(scope_declare(&s, name("node"), BIND_MODULE, at(9), NULL));
    scope_free(&s);
})

TEST(a_refused_binding_leaves_the_first_one_in_place, {
    scope_t s;
    module_scope(&s);
    TEST_ASSERT_NULL(scope_declare(&s, name("add"), BIND_ENUM, at(9), NULL));
    const binding_t* b = scope_find(&s, name("add"));
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_FN);
    TEST_ASSERT_EQ_UINT64((uint64_t)b->loc.line, (uint64_t)1);
    scope_free(&s);
})

TEST(sibling_scopes_may_reuse_a_name, {
    scope_t module;
    module_scope(&module);
    scope_t first;
    scope_init(&first, SCOPE_BLOCK, &module);
    TEST_ASSERT_NONNULL(scope_declare(&first, name("i"), BIND_LOCAL, at(4), NULL));
    scope_free(&first);
    scope_t second;
    scope_init(&second, SCOPE_BLOCK, &module);
    TEST_ASSERT_NONNULL(scope_declare(&second, name("i"), BIND_LOCAL, at(8), NULL));
    scope_free(&second);
    scope_free(&module);
})

// ---- lookup and shadowing -----------------------------------------------------------
// D7.9

TEST(lookup_finds_a_module_level_name_from_a_block, {
    scope_t module;
    module_scope(&module);
    scope_t body;
    scope_init(&body, SCOPE_BLOCK, &module);
    scope_t inner;
    scope_init(&inner, SCOPE_BLOCK, &body);
    const binding_t* b = scope_lookup(&inner, name("add"));
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_FN);
    scope_free(&inner);
    scope_free(&body);
    scope_free(&module);
})

TEST(lookup_takes_the_innermost_binding_of_a_name, {
    scope_t module;
    module_scope(&module);
    scope_t body;
    scope_init(&body, SCOPE_BLOCK, &module);
    TEST_UNUSED(scope_declare(&body, name("add"), BIND_PARAM, at(5), NULL));
    const binding_t* b = scope_lookup(&body, name("add"));
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_PARAM);
    scope_free(&body);
    scope_free(&module);
})

TEST(a_local_may_shadow_an_import_binding, {
    scope_t module;
    scope_init(&module, SCOPE_MODULE, NULL);
    TEST_UNUSED(scope_declare(&module, name("io"), BIND_MODULE, at(1), NULL));
    scope_t body;
    scope_init(&body, SCOPE_BLOCK, &module);
    TEST_ASSERT_NONNULL(scope_declare(&body, name("io"), BIND_PARAM, at(3), NULL));
    TEST_ASSERT_EQ_INT32((int32_t)scope_lookup(&body, name("io"))->kind, (int32_t)BIND_PARAM);
    scope_free(&body);
    scope_free(&module);
})

TEST(lookup_of_an_unknown_name_reaches_no_binding, {
    scope_t module;
    module_scope(&module);
    scope_t body;
    scope_init(&body, SCOPE_BLOCK, &module);
    TEST_ASSERT_NULL(scope_lookup(&body, name("println")));
    scope_free(&body);
    scope_free(&module);
})

TEST(an_enclosing_local_is_found_across_block_scopes, {
    scope_t module;
    module_scope(&module);
    scope_t body;
    scope_init(&body, SCOPE_BLOCK, &module);
    TEST_UNUSED(scope_declare(&body, name("n"), BIND_LOCAL, at(4), NULL));
    scope_t inner;
    scope_init(&inner, SCOPE_BLOCK, &body);
    const binding_t* b = scope_find_in_blocks(&inner, name("n"));
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_UINT64((uint64_t)b->loc.line, (uint64_t)4);
    scope_free(&inner);
    scope_free(&body);
    scope_free(&module);
})

TEST(a_module_level_name_is_not_an_enclosing_local, {
    scope_t module;
    module_scope(&module);
    scope_t body;
    scope_init(&body, SCOPE_BLOCK, &module);
    TEST_ASSERT_NULL(scope_find_in_blocks(&body, name("add")));
    scope_free(&body);
    scope_free(&module);
})

TEST(a_module_namespace_searched_for_blocks_alone_yields_nothing, {
    scope_t module;
    module_scope(&module);
    TEST_ASSERT_NULL(scope_find_in_blocks(&module, name("add")));
    scope_free(&module);
})

// ---- import bindings ----------------------------------------------------------------
// D9.3

TEST(a_module_binding_carries_the_module_it_denotes, {
    scope_t s;
    scope_init(&s, SCOPE_MODULE, NULL);
    const char* target = "std.io";
    binding_t* b = scope_declare(&s, name("io"), BIND_MODULE, at(1), NULL);
    TEST_ASSERT_NONNULL(b);
    b->module = target;
    TEST_ASSERT_TRUE(scope_find(&s, name("io"))->module == target);
    scope_free(&s);
})

TEST(a_symbol_binding_points_at_the_other_modules_declaration, {
    scope_t other;
    module_scope(&other);
    scope_t s;
    scope_init(&s, SCOPE_MODULE, NULL);
    binding_t* b = scope_declare(&s, name("plus"), BIND_SYMBOL, at(1), NULL);
    TEST_ASSERT_NONNULL(b);
    b->to = scope_find(&other, name("add"));
    TEST_ASSERT_TRUE(scope_find(&s, name("plus"))->to == scope_find(&other, name("add")));
    scope_free(&s);
    scope_free(&other);
})

TEST(only_declarations_are_importable, {
    scope_t s;
    module_scope(&s);
    TEST_UNUSED(scope_declare(&s, name("io"), BIND_MODULE, at(4), NULL));
    TEST_UNUSED(scope_declare(&s, name("cmp"), BIND_SYMBOL, at(5), NULL));
    TEST_UNUSED(scope_declare(&s, name("write"), BIND_EXTERN_FN, at(6), NULL));
    TEST_ASSERT_TRUE(bind_is_declaration(scope_find(&s, name("add"))));
    TEST_ASSERT_TRUE(bind_is_declaration(scope_find(&s, name("node"))));
    TEST_ASSERT_TRUE(bind_is_declaration(scope_find(&s, name("LIMIT"))));
    TEST_ASSERT_TRUE(bind_is_declaration(scope_find(&s, name("write"))));
    TEST_ASSERT_FALSE(bind_is_declaration(scope_find(&s, name("io"))));
    TEST_ASSERT_FALSE(bind_is_declaration(scope_find(&s, name("cmp"))));
    scope_free(&s);
})

// ---- the universe -------------------------------------------------------------------
// D12.2

// The builtins the universe scope lists, in its order: a brace initializer
// cannot sit in a TEST body, since a comma outside parentheses splits the
// macro argument.
// D12.2
static const char* const EXPECTED_UNIVERSE[] = {
    "del",
    "move",
    "assert",
    "panic",
    "print",
    "println",
    "eprint",
    "eprintln",
    "fprint",
    "fprintln",
};

TEST(the_universe_holds_the_builtins_of_d12_2, {
    TEST_ASSERT_EQ_UINT64((uint64_t)UNIVERSE_COUNT,
                          (uint64_t)(sizeof EXPECTED_UNIVERSE / sizeof EXPECTED_UNIVERSE[0]));
    for (uint64_t i = 0; i < (uint64_t)UNIVERSE_COUNT; i++) {
        TEST_ASSERT_TRUE(str_eq(scope_universe_at(i), str_from_cstr(EXPECTED_UNIVERSE[i])));
        TEST_ASSERT_TRUE(scope_is_universe(str_from_cstr(EXPECTED_UNIVERSE[i])));
    }
})

TEST(an_ordinary_name_is_not_a_universe_name, {
    TEST_ASSERT_FALSE(scope_is_universe(name("printf")));
    TEST_ASSERT_FALSE(scope_is_universe(name("prin")));
    TEST_ASSERT_FALSE(scope_is_universe(name("")));
})

TEST(a_module_level_declaration_may_shadow_a_universe_name, {
    scope_t s;
    scope_init(&s, SCOPE_MODULE, NULL);
    TEST_ASSERT_NONNULL(scope_declare(&s, name("print"), BIND_FN, at(1), NULL));
    TEST_ASSERT_TRUE(scope_is_universe(name("print")));
    scope_free(&s);
})

int main(int argc, char** argv) {
    TEST_INIT("scope", argc, argv);
    TEST_RUN(a_fresh_module_namespace_is_empty);
    TEST_RUN(a_declaration_is_found_by_name);
    TEST_RUN(a_name_that_was_never_declared_is_not_found);
    TEST_RUN(bindings_are_kept_in_declaration_order);
    TEST_RUN(the_declaring_node_is_kept_on_the_binding);
    TEST_RUN(a_second_binding_of_one_name_is_refused);
    TEST_RUN(a_struct_and_a_function_of_one_name_collide);
    TEST_RUN(an_import_binding_collides_with_a_declaration);
    TEST_RUN(a_refused_binding_leaves_the_first_one_in_place);
    TEST_RUN(sibling_scopes_may_reuse_a_name);
    TEST_RUN(lookup_finds_a_module_level_name_from_a_block);
    TEST_RUN(lookup_takes_the_innermost_binding_of_a_name);
    TEST_RUN(a_local_may_shadow_an_import_binding);
    TEST_RUN(lookup_of_an_unknown_name_reaches_no_binding);
    TEST_RUN(an_enclosing_local_is_found_across_block_scopes);
    TEST_RUN(a_module_level_name_is_not_an_enclosing_local);
    TEST_RUN(a_module_namespace_searched_for_blocks_alone_yields_nothing);
    TEST_RUN(a_module_binding_carries_the_module_it_denotes);
    TEST_RUN(a_symbol_binding_points_at_the_other_modules_declaration);
    TEST_RUN(only_declarations_are_importable);
    TEST_RUN(the_universe_holds_the_builtins_of_d12_2);
    TEST_RUN(an_ordinary_name_is_not_a_universe_name);
    TEST_RUN(a_module_level_declaration_may_shadow_a_universe_name);
    TEST_EXIT();
}
