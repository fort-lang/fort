// The representation of a type: the table and its interning, identity
// (D3.12, D17.1) and the level model (D5.2).
//
// The conversions are tested in types_convert_test.c, the sizes, the layout
// and the internal errors in types_layout_test.c, the builder and the
// canonical spelling in types_build_test.c.
#include "types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "prim.h"
#include "str.h"
#include "types_helpers.h"

#include "test.h"

// ---- interning ---------------------------------------------------------------

TEST(intern_gives_one_node_per_type, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(tenv_type(&e, "node*") == tenv_type(&e, "node*"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32@") == tenv_type(&e, "i32@"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[4]") == tenv_type(&e, "i32[4]"));
    TEST_ASSERT_TRUE(tenv_type(&e, "string") == tenv_type(&e, "string"));
    TEST_ASSERT_TRUE(tenv_type(&e, "void*") == tenv_type(&e, "void*"));
    TEST_ASSERT_TRUE(tenv_type(&e, "node mut* own mut@ own") ==
                     tenv_type(&e, "node mut* own mut@ own"));
    tenv_free(&e);
})

TEST(intern_separates_marks_and_shapes, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(tenv_type(&e, "node*") != tenv_type(&e, "node mut*"));
    TEST_ASSERT_TRUE(tenv_type(&e, "node*") != tenv_type(&e, "node* own"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32@") != tenv_type(&e, "i32 mut@"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32@") != tenv_type(&e, "i32@ own"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[4]") != tenv_type(&e, "i32[8]"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32*") != tenv_type(&e, "u32*"));
    TEST_ASSERT_TRUE(tenv_type(&e, "string") != tenv_type(&e, "string own"));
    TEST_ASSERT_TRUE(tenv_type(&e, "void*") != tenv_type(&e, "void* own"));
    TEST_ASSERT_TRUE(tenv_type(&e, "node**") != tenv_type(&e, "node* mut*"));
    tenv_free(&e);
})

TEST(intern_covers_function_types, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* i64 = type_prim(&e.tt, PRIM_I64);
    TEST_ASSERT_TRUE(tenv_fn(&e, i32, i32, NULL) == tenv_fn(&e, i32, i32, NULL));
    TEST_ASSERT_TRUE(tenv_fn(&e, i32, i32, i32) == tenv_fn(&e, i32, i32, i32));
    TEST_ASSERT_TRUE(tenv_fn(&e, i32, i32, NULL) != tenv_fn(&e, i64, i32, NULL));
    TEST_ASSERT_TRUE(tenv_fn(&e, i32, i32, NULL) != tenv_fn(&e, i32, i64, NULL));
    TEST_ASSERT_TRUE(tenv_fn(&e, i32, i32, NULL) != tenv_fn(&e, i32, i32, i32));
    tenv_free(&e);
})

TEST(singletons_are_one_node_each, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_void(&e.tt) == type_void(&e.tt));
    TEST_ASSERT_TRUE(type_null(&e.tt) == type_null(&e.tt));
    TEST_ASSERT_TRUE(type_error(&e.tt) == type_error(&e.tt));
    TEST_ASSERT_TRUE(type_prim(&e.tt, PRIM_I32) == type_prim(&e.tt, PRIM_I32));
    TEST_ASSERT_TRUE(type_prim(&e.tt, PRIM_I32) != type_prim(&e.tt, PRIM_U32));
    TEST_ASSERT_TRUE(type_void(&e.tt) != type_null(&e.tt));
    tenv_free(&e);
})

TEST(a_nominal_type_is_one_node_per_declaration, {
    tenv_t e;
    tenv_init(&e);
    // Two structs with the same name and different declarations are two
    // types; one declaration is one node, however often it is asked for, so
    // no two nodes of a struct can split the types built from it (D3.12).
    const type_t* a = type_struct(&e.tt, str_from_cstr("p1"), "p1");
    const type_t* b = type_struct(&e.tt, str_from_cstr("p1"), "p2");
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_FALSE(type_equal(a, b));
    TEST_ASSERT_TRUE(type_equal(a, a));
    TEST_ASSERT_TRUE(type_struct(&e.tt, str_from_cstr("p1"), "p1") == a);
    TEST_ASSERT_TRUE(type_struct(&e.tt, str_from_cstr("other name"), "p1") == a);
    TEST_ASSERT_TRUE(type_enum(&e.tt, str_from_cstr("c1"), "c1") ==
                     type_enum(&e.tt, str_from_cstr("c1"), "c1"));
    TEST_ASSERT_TRUE(type_struct(&e.tt, str_from_cstr("node"), "node") == e.node);
    // Every type built from one declaration is therefore one node.
    TEST_ASSERT_TRUE(type_ptr(&e.tt, a, false, false) ==
                     type_ptr(&e.tt, type_struct(&e.tt, str_from_cstr("p1"), "p1"), false, false));
    // An anonymous node (no declaration) is its own type.
    const type_t* anon1 = type_struct(&e.tt, str_from_cstr("p1"), NULL);
    const type_t* anon2 = type_struct(&e.tt, str_from_cstr("p1"), NULL);
    TEST_ASSERT_TRUE(anon1 != anon2);
    TEST_ASSERT_FALSE(type_equal(anon1, anon2));
    TEST_ASSERT_TRUE(type_equal(anon1, anon1));
    tenv_free(&e);
})

TEST(error_type_poisons_every_constructor, {
    tenv_t e;
    tenv_init(&e);
    const type_t* err = type_error(&e.tt);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    TEST_ASSERT_TRUE(type_ptr(&e.tt, err, false, false) == err);
    TEST_ASSERT_TRUE(type_slice(&e.tt, err, true, true) == err);
    TEST_ASSERT_TRUE(type_array(&e.tt, err, 4) == err);
    TEST_ASSERT_TRUE(tenv_fn(&e, err, i32, NULL) == err);
    TEST_ASSERT_TRUE(tenv_fn(&e, i32, err, NULL) == err);
    tenv_free(&e);
})

TEST(table_free_leaves_an_empty_usable_table, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_NONNULL(tenv_type(&e, "node mut* own mut@ own"));
    TEST_UNUSED(tenv_fn(&e, type_prim(&e.tt, PRIM_I32), e.node, e.point));
    tenv_free(&e);
    tenv_init(&e);
    TEST_ASSERT_NONNULL(tenv_type(&e, "u8 mut@ own mut*"));
    tenv_free(&e);
})

// ---- type classes ---------------------------------------------------------------

TEST(type_classes_answer_for_whole_types, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_is_int(tenv_type(&e, "i32")));
    TEST_ASSERT_FALSE(type_is_int(tenv_type(&e, "f64")));
    TEST_ASSERT_TRUE(type_is_float(tenv_type(&e, "f32")));
    TEST_ASSERT_TRUE(type_is_scalar(tenv_type(&e, "bool")));
    TEST_ASSERT_TRUE(type_is_scalar(tenv_type(&e, "char")));
    TEST_ASSERT_TRUE(type_is_scalar(e.color));
    TEST_ASSERT_FALSE(type_is_scalar(e.point));
    TEST_ASSERT_FALSE(type_is_scalar(tenv_type(&e, "node*")));
    TEST_ASSERT_TRUE(type_is_reference(tenv_type(&e, "node*")));
    TEST_ASSERT_TRUE(type_is_reference(tenv_type(&e, "void*")));
    TEST_ASSERT_TRUE(type_is_reference(tenv_type(&e, "i32@")));
    TEST_ASSERT_TRUE(type_is_reference(tenv_type(&e, "string")));
    TEST_ASSERT_FALSE(type_is_reference(tenv_type(&e, "i32[4]")));
    TEST_ASSERT_FALSE(type_is_reference(e.point));
    tenv_free(&e);
})

// ---- identity (D3.12, D17.1) -----------------------------------------------------

TEST(identity_of_primitives_is_the_name, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_equal(tenv_type(&e, "i32"), tenv_type(&e, "i32")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "i32"), tenv_type(&e, "u32")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "i32"), tenv_type(&e, "i64")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "char"), tenv_type(&e, "u8")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "bool"), tenv_type(&e, "u8")));
    tenv_free(&e);
})

TEST(identity_of_struct_and_enum_is_the_declaration, {
    tenv_t e;
    tenv_init(&e);
    const type_t* p1 = type_struct(&e.tt, str_from_cstr("p1"), "d1");
    const type_t* p1_again = type_struct(&e.tt, str_from_cstr("p1"), "d1");
    TEST_ASSERT_TRUE(type_equal(p1, p1_again));
    TEST_ASSERT_TRUE(p1 == p1_again);
    TEST_ASSERT_FALSE(type_equal(e.node, e.point));
    TEST_ASSERT_FALSE(type_equal(e.color, e.shape));
    TEST_ASSERT_FALSE(type_equal(e.color, e.node));
    TEST_ASSERT_TRUE(type_equal(tenv_type(&e, "node*"), tenv_type(&e, "node*")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "node*"), tenv_type(&e, "point*")));
    tenv_free(&e);
})

TEST(identity_of_arrays_is_element_and_length, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_equal(tenv_type(&e, "i32[4]"), tenv_type(&e, "i32[4]")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "i32[4]"), tenv_type(&e, "i32[5]")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "i32[4]"), tenv_type(&e, "u32[4]")));
    TEST_ASSERT_TRUE(type_equal(tenv_type(&e, "i32[3][4]"), tenv_type(&e, "i32[3][4]")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "i32[3][4]"), tenv_type(&e, "i32[4][3]")));
    tenv_free(&e);
})

TEST(identity_of_pointers_and_slices_includes_every_mark, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "node*"), tenv_type(&e, "node mut*")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "node*"), tenv_type(&e, "node* own")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "node**"), tenv_type(&e, "node* mut*")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "i32@"), tenv_type(&e, "i32 mut@")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "node*@"), tenv_type(&e, "node* own@")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "node*@"), tenv_type(&e, "node* mut@ own")));
    // Level 0 is not part of the type: `node* mut p` and `node* q` agree.
    TEST_ASSERT_TRUE(type_equal(tenv_type(&e, "node* mut"), tenv_type(&e, "node*")));
    TEST_ASSERT_TRUE(type_equal(tenv_type(&e, "i32@ mut"), tenv_type(&e, "i32@")));
    tenv_free(&e);
})

TEST(identity_of_voidptr_and_string_is_the_own_mark, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_equal(tenv_type(&e, "void*"), tenv_type(&e, "void*")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "void*"), tenv_type(&e, "void* own")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "string"), tenv_type(&e, "string own")));
    // `string` is distinct from `char@` and `u8@` (D3.7).
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "string"), tenv_type(&e, "char@")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "string"), tenv_type(&e, "u8@")));
    TEST_ASSERT_FALSE(type_equal(tenv_type(&e, "void*"), tenv_type(&e, "i32*")));
    tenv_free(&e);
})

TEST(identity_of_function_types_is_structural, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* i64 = type_prim(&e.tt, PRIM_I64);
    const type_t* v = type_void(&e.tt);
    TEST_ASSERT_TRUE(type_equal(tenv_fn(&e, i32, i32, i32), tenv_fn(&e, i32, i32, i32)));
    TEST_ASSERT_TRUE(type_equal(tenv_fn(&e, v, NULL, NULL), tenv_fn(&e, v, NULL, NULL)));
    // Return type, parameter types and arity.
    TEST_ASSERT_FALSE(type_equal(tenv_fn(&e, i32, i32, NULL), tenv_fn(&e, i64, i32, NULL)));
    TEST_ASSERT_FALSE(type_equal(tenv_fn(&e, i32, i32, NULL), tenv_fn(&e, i32, i64, NULL)));
    TEST_ASSERT_FALSE(type_equal(tenv_fn(&e, i32, i32, NULL), tenv_fn(&e, i32, i32, i32)));
    TEST_ASSERT_FALSE(type_equal(tenv_fn(&e, v, NULL, NULL), tenv_fn(&e, i32, NULL, NULL)));
    // Pointee mutability and `own` of a parameter (D3.10, D17.1).
    TEST_ASSERT_FALSE(type_equal(tenv_fn(&e, v, tenv_type(&e, "node*"), NULL),
                                 tenv_fn(&e, v, tenv_type(&e, "node mut*"), NULL)));
    TEST_ASSERT_FALSE(type_equal(tenv_fn(&e, v, tenv_type(&e, "node*"), NULL),
                                 tenv_fn(&e, v, tenv_type(&e, "node* own"), NULL)));
    // `noreturn` is part of identity.
    const type_t* nore = type_fn(&e.tt, v, NULL, 0, true);
    TEST_ASSERT_FALSE(type_equal(nore, tenv_fn(&e, v, NULL, NULL)));
    TEST_ASSERT_TRUE(type_equal(nore, type_fn(&e.tt, v, NULL, 0, true)));
    tenv_free(&e);
})

TEST(same_shape_ignores_mut_and_own, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_same_shape(tenv_type(&e, "node*"), tenv_type(&e, "node mut* own")));
    TEST_ASSERT_TRUE(
        type_same_shape(tenv_type(&e, "node*@"), tenv_type(&e, "node mut* own mut@ own")));
    TEST_ASSERT_TRUE(type_same_shape(tenv_type(&e, "string"), tenv_type(&e, "string own")));
    TEST_ASSERT_TRUE(type_same_shape(tenv_type(&e, "void*"), tenv_type(&e, "void* own")));
    TEST_ASSERT_FALSE(type_same_shape(tenv_type(&e, "node*"), tenv_type(&e, "point*")));
    TEST_ASSERT_FALSE(type_same_shape(tenv_type(&e, "i32[4]"), tenv_type(&e, "i32@")));
    TEST_ASSERT_FALSE(type_same_shape(tenv_type(&e, "string"), tenv_type(&e, "char@")));
    // A function type is compared whole: the marks of its parameters and
    // result are part of its identity, not of the chain that reaches it
    // (D3.10), so they survive `type_same_shape`.
    const type_t* v = type_void(&e.tt);
    const type_t* takes_node = tenv_fn(&e, v, tenv_type(&e, "node*"), NULL);
    const type_t* takes_mut = tenv_fn(&e, v, tenv_type(&e, "node mut*"), NULL);
    const type_t* takes_own = tenv_fn(&e, v, tenv_type(&e, "node* own"), NULL);
    TEST_ASSERT_FALSE(type_same_shape(takes_node, takes_mut));
    TEST_ASSERT_FALSE(type_same_shape(takes_node, takes_own));
    TEST_ASSERT_TRUE(type_same_shape(takes_node, takes_node));
    TEST_ASSERT_FALSE(type_same_shape(type_slice(&e.tt, takes_node, false, false),
                                      type_slice(&e.tt, takes_mut, false, false)));
    TEST_ASSERT_TRUE(type_same_shape(type_slice(&e.tt, takes_node, true, true),
                                     type_slice(&e.tt, takes_node, false, false)));
    TEST_ASSERT_FALSE(type_same_shape(tenv_fn(&e, tenv_type(&e, "node*"), NULL, NULL),
                                      tenv_fn(&e, tenv_type(&e, "node mut*"), NULL, NULL)));
    tenv_free(&e);
})

// ---- the level model (D5.2) ------------------------------------------------------

TEST(the_levels_of_a_chain_of_suffixes, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "i32")), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "node*")), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "node**")), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "i32@")), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "i32@@")), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "node*@")), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "u8@*")), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "i32[4]*")), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "i32[3][4]")), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "point")), (uint64_t)0);
    // `string` and `void*` have no level behind the binding (D5.2, D3.11).
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "string")), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "void*")), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "i32[4]@")), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(tenv_type(&e, "i32@[4]")), (uint64_t)1);
    tenv_free(&e);
})

TEST(ref_at_walks_the_chain_outermost_first, {
    tenv_t e;
    tenv_init(&e);
    const type_t* t = tenv_type(&e, "node mut* own mut@ own");
    TEST_ASSERT_TRUE(type_ref_at(t, 1) == t);
    TEST_ASSERT_TRUE(type_ref_at(t, 2) == t->elem);
    TEST_ASSERT_NULL(type_ref_at(t, 3));
    // A fixed array is transparent: the reference behind it is level 1.
    const type_t* arr = tenv_type(&e, "node* own[4]");
    TEST_ASSERT_TRUE(type_ref_at(arr, 1) == arr->elem);
    TEST_ASSERT_TRUE(type_ref_at(arr, 1)->own);
    TEST_ASSERT_NULL(type_ref_at(arr, 2));
    // `string` and `void*` carry an `own` mark but add no level.
    TEST_ASSERT_TRUE(type_ref_at(tenv_type(&e, "string own"), 1)->own);
    TEST_ASSERT_TRUE(type_ref_at(tenv_type(&e, "void* own"), 1)->own);
    TEST_ASSERT_NULL(type_ref_at(tenv_type(&e, "string own"), 2));
    TEST_ASSERT_NULL(type_ref_at(tenv_type(&e, "i32"), 1));
    TEST_ASSERT_NULL(type_ref_at(tenv_type(&e, "i32[3][4]"), 1));
    tenv_free(&e);
})

TEST(level_mut_reports_the_level_of_each_reference, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_FALSE(type_level_mut(tenv_type(&e, "node*"), 1));
    TEST_ASSERT_TRUE(type_level_mut(tenv_type(&e, "node mut*"), 1));
    TEST_ASSERT_TRUE(type_level_mut(tenv_type(&e, "node* mut*"), 1));
    TEST_ASSERT_FALSE(type_level_mut(tenv_type(&e, "node* mut*"), 2));
    TEST_ASSERT_TRUE(type_level_mut(tenv_type(&e, "node mut* mut*"), 2));
    // Beyond the chain, and for the characters of a string, always false.
    TEST_ASSERT_FALSE(type_level_mut(tenv_type(&e, "node*"), 2));
    TEST_ASSERT_FALSE(type_level_mut(tenv_type(&e, "string"), 1));
    TEST_ASSERT_FALSE(type_level_mut(tenv_type(&e, "string own"), 1));
    TEST_ASSERT_FALSE(type_level_mut(tenv_type(&e, "void* mut"), 1));
    tenv_free(&e);
})

int main(int argc, char** argv) {
    TEST_INIT("types", argc, argv);
    TEST_RUN(intern_gives_one_node_per_type);
    TEST_RUN(intern_separates_marks_and_shapes);
    TEST_RUN(intern_covers_function_types);
    TEST_RUN(singletons_are_one_node_each);
    TEST_RUN(a_nominal_type_is_one_node_per_declaration);
    TEST_RUN(error_type_poisons_every_constructor);
    TEST_RUN(table_free_leaves_an_empty_usable_table);
    TEST_RUN(type_classes_answer_for_whole_types);
    TEST_RUN(identity_of_primitives_is_the_name);
    TEST_RUN(identity_of_struct_and_enum_is_the_declaration);
    TEST_RUN(identity_of_arrays_is_element_and_length);
    TEST_RUN(identity_of_pointers_and_slices_includes_every_mark);
    TEST_RUN(identity_of_voidptr_and_string_is_the_own_mark);
    TEST_RUN(identity_of_function_types_is_structural);
    TEST_RUN(same_shape_ignores_mut_and_own);
    TEST_RUN(the_levels_of_a_chain_of_suffixes);
    TEST_RUN(ref_at_walks_the_chain_outermost_first);
    TEST_RUN(level_mut_reports_the_level_of_each_reference);
    TEST_EXIT();
}
