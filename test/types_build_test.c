// The type builder and the canonical spelling: the reading rules of D3.6,
// the placement rules of D5.3 and D17.2, and every row of the tables those
// decisions carry. Each row is one assertion: the reader of types_helpers.h
// turns the spelling into type_build arguments, and the type is checked by
// the levels it marks mutable (`tenv_muts`, level 0 first), by the `own`
// mark of each of its references (`tenv_owns`, outermost first) and by the
// spelling type_to_str gives it back.
//
// Every marker follows the type element whose storage it marks and the last
// position is the binding, so a declaration's own `mut` is just the last one
// written (D5.3). The tables of core-language.md and type-system.md are
// being re-spelled, so decisions.md is the source here.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "prim.h"
#include "str.h"
#include "types.h"
#include "types_helpers.h"

#include "test.h"

// ---- reading suffixes (D3.6) ----------------------------------------------------

TEST(the_d3_6_shapes_round_trip, {
    tenv_t e;
    tenv_init(&e);
    // Reference suffixes read inside-out; the fixed-array group reads
    // outside-in and sits between the two reference groups.
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node**"), "node**");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node*@"), "node*@");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@*"), "u8@*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@@"), "u8@@");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[3][4]"), "i32[3][4]");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node*[16]"), "node*[16]");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node@[4]"), "node@[4]");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4]*"), "i32[4]*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4]@"), "i32[4]@");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node*@*"), "node*@*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void*[2]"), "void*[2]");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32@@"), "i32@@");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32@[4]"), "i32@[4]");
    tenv_free(&e);
})

TEST(the_d3_6_shapes_are_the_ones_it_names, {
    tenv_t e;
    tenv_init(&e);
    // `node*@` is a span of pointers, `u8@*` a pointer to a span.
    const type_t* span_of_ptr = tenv_type(&e, "node*@");
    TEST_ASSERT_TRUE(span_of_ptr->kind == TYPE_SPAN);
    TEST_ASSERT_TRUE(span_of_ptr->elem->kind == TYPE_PTR);
    const type_t* ptr_to_span = tenv_type(&e, "u8@*");
    TEST_ASSERT_TRUE(ptr_to_span->kind == TYPE_PTR);
    TEST_ASSERT_TRUE(ptr_to_span->elem->kind == TYPE_SPAN);
    // `node*[16]` is sixteen pointers, `node@[4]` four spans.
    const type_t* ptrs = tenv_type(&e, "node*[16]");
    TEST_ASSERT_TRUE(ptrs->kind == TYPE_ARRAY);
    TEST_ASSERT_EQ_UINT64(ptrs->len, (uint64_t)16);
    TEST_ASSERT_TRUE(ptrs->elem->kind == TYPE_PTR);
    const type_t* spans = tenv_type(&e, "node@[4]");
    TEST_ASSERT_TRUE(spans->kind == TYPE_ARRAY);
    TEST_ASSERT_TRUE(spans->elem->kind == TYPE_SPAN);
    // `i32[4]*` points to the whole array, `i32[4]@` spans it.
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[4]*")->elem->kind == TYPE_ARRAY);
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[4]@")->elem->kind == TYPE_ARRAY);
    // `i32[3][4]` is three arrays of four, indexed a[i][j].
    const type_t* m = tenv_type(&e, "i32[3][4]");
    TEST_ASSERT_EQ_UINT64(m->len, (uint64_t)3);
    TEST_ASSERT_EQ_UINT64(m->elem->len, (uint64_t)4);
    // `void*` is its own kind, with no pointee level (D3.11).
    TEST_ASSERT_TRUE(tenv_type(&e, "void*")->kind == TYPE_VOIDPTR);
    TEST_ASSERT_TRUE(tenv_type(&e, "void**")->kind == TYPE_PTR);
    TEST_ASSERT_TRUE(tenv_type(&e, "void**")->elem->kind == TYPE_VOIDPTR);
    tenv_free(&e);
})

TEST(an_array_suffix_never_follows_a_reference_suffix, {
    tenv_t e;
    tenv_init(&e);
    // `i32[4]*[2]` does not parse; wrap the pointer in a struct (D3.6).
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4]*[2]"),
                       "error: no array suffix may follow a trailing reference suffix");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4]@[2]"),
                       "error: no array suffix may follow a trailing reference suffix");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node*[2]*[2]"),
                       "error: no array suffix may follow a trailing reference suffix");
    tenv_free(&e);
})

TEST(a_marker_that_belongs_to_an_array_is_refused, {
    tenv_t e;
    tenv_init(&e);
    // The elements of a fixed array share its storage, so the marker goes
    // after the length: `i32 mut[4]` is an error (D5.3).
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32 mut[4]"), "error: mark the array after its length");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[3] mut[4]"), "error: mark the array after its length");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* mut[4]"), "error: mark the array after its length");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node** mut[4]"), "error: mark the array after its length");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node@ mut[4]"), "error: mark the array after its length");
    // The array's own position is where it belongs.
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32[4] mut"), "i32[4] mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32[3][4] mut"), "i32[3][4] mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node*[4] mut"), "node*[4] mut");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4] mut*"), "i32[4] mut*");
    // A `mut` after a reference suffix that no array follows is fine.
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* mut*"), "node* mut*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* mut@"), "node* mut@");
    tenv_free(&e);
})

TEST(an_own_marks_a_reference_and_nothing_else, {
    tenv_t e;
    tenv_init(&e);
    // `own` follows a `*` or an `@`, or `string`, the reference with no
    // suffix; it never follows another base type or a fixed-array suffix
    // (D17.2).
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node own*"),
                       "error: 'own' marks a reference: write it after a '*' or an '@'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32 own"),
                       "error: 'own' marks a reference: write it after a '*' or an '@'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "point own"),
                       "error: 'own' marks a reference: write it after a '*' or an '@'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "color own"),
                       "error: 'own' marks a reference: write it after a '*' or an '@'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node*[4] own"),
                       "error: 'own' never follows a fixed-array suffix");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4] own"),
                       "error: 'own' never follows a fixed-array suffix");
    // Where it is legal, it marks exactly the reference it follows.
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* own"), "node* own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@ own"), "u8@ own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "string own"), "string own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void* own"), "void* own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* own[4]"), "node* own[4]");
    tenv_free(&e);
})

TEST(builder_errors_of_lengths_and_void, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[0]"), "error: array length must be greater than 0");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4][0]"), "error: array length must be greater than 0");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void"), "void");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void*"), "void*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void**"), "void**");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void*@"), "void*@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "void* mut"), "void* mut");
    // `void` has no target level, so `void mut*` and `void own` are errors,
    // and a span or an array of `void` does not exist (D3.11, D5.3).
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void mut*"),
                       "error: 'void' is only a return type or the base of 'void*'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void own"),
                       "error: 'void' is only a return type or the base of 'void*'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void@"),
                       "error: 'void' is only a return type or the base of 'void*'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void[4]"),
                       "error: 'void' is only a return type or the base of 'void*'");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void mut"),
                       "error: 'void' is only a return type or the base of 'void*'");
    tenv_free(&e);
})

TEST(an_own_mark_on_a_function_type_is_refused, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* f = tenv_fn(&e, i32, i32, NULL);
    // A function pointer is not a reference that owns anything (D17.1).
    type_build_t r = type_build(&e.tt, true, false, f, NULL, 0);
    TEST_ASSERT_NULL(r.type);
    TEST_ASSERT_EQ_STR(r.error, "'own' marks a reference: write it after a '*' or an '@'");
    // A `mut` after a function type marks the storage holding the function
    // pointer, which in the last position is the binding (D5.3).
    r = type_build(&e.tt, false, true, f, NULL, 0);
    TEST_ASSERT_TRUE(r.type == f);
    TEST_ASSERT_TRUE(r.mut0);
    tenv_free(&e);
})

// ---- the placement rule (D5.3) --------------------------------------------------

TEST(the_d5_3_table_marks_the_levels_it_names, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32 mut"), "y");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "point mut"), "y");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4] mut"), "y");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node*"), "nn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* mut"), "yn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut*"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut* mut"), "yy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32@"), "nn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32@ mut"), "yn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32 mut@"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* mut@"), "nyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut*@"), "nny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* mut*"), "nyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut**"), "nny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8 mut@ mut*"), "nyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8@ mut*"), "nyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node*[4] mut"), "yn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "string mut"), "y");
    tenv_free(&e);
})

TEST(the_d5_3_table_spells_its_declarations_back, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32 mut"), "i32 mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "point mut"), "point mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32[4] mut"), "i32[4] mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node*"), "node*");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node* mut"), "node* mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut*"), "node mut*");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut* mut"), "node mut* mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32@"), "i32@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32@ mut"), "i32@ mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32 mut@"), "i32 mut@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node* mut@"), "node* mut@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut*@"), "node mut*@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node* mut*"), "node* mut*");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut**"), "node mut**");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8 mut@ mut*"), "u8 mut@ mut*");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8@ mut*"), "u8@ mut*");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node*[4] mut"), "node*[4] mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "string mut"), "string mut");
    tenv_free(&e);
})

TEST(the_placement_rule_on_further_shapes, {
    tenv_t e;
    tenv_init(&e);
    // Chains of three, where each position marks one level (D5.3).
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node**"), "nnn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node** mut"), "ynn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut* mut* mut"), "yyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32@ mut@"), "nyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32@@ mut"), "ynn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32 mut@ mut@ mut"), "yyy");
    // A fixed array adds no level, so its elements and the array are one
    // storage (D5.2).
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4]@"), "nn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4] mut@"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4]@ mut"), "yn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut*[4]"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut*[4] mut"), "yy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[3][4]"), "n");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[3][4] mut"), "y");
    // Structs, strings and `void*` behave like any other base type.
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "point*"), "nn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "point mut*"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "point mut* mut"), "yy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "string@ mut"), "yn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "string mut@"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "void* mut"), "y");
    // Out-parameters, the shape D3.6 names.
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8@*"), "nnn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8@* mut"), "ynn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8 mut@ mut* mut"), "yyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4]*"), "nn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4] mut*"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4] mut* mut"), "yy");
    tenv_free(&e);
})

TEST(the_placement_rule_spells_further_shapes_back, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node** mut"), "node** mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut* mut* mut"), "node mut* mut* mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32@ mut@"), "i32@ mut@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32@@ mut"), "i32@@ mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32 mut@ mut@ mut"), "i32 mut@ mut@ mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32[4] mut@"), "i32[4] mut@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32[4]@ mut"), "i32[4]@ mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut*[4] mut"), "node mut*[4] mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32[3][4] mut"), "i32[3][4] mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "point mut* mut"), "point mut* mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "string mut@"), "string mut@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "string@ mut"), "string@ mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8@* mut"), "u8@* mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8 mut@ mut* mut"), "u8 mut@ mut* mut");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "i32[4] mut* mut"), "i32[4] mut* mut");
    tenv_free(&e);
})

TEST(the_last_position_is_the_binding, {
    tenv_t e;
    tenv_init(&e);
    // Level 0 is a position like any other: the marker before the name.
    TEST_ASSERT_TRUE(tenv_mut0(&e, "node* mut"));
    TEST_ASSERT_FALSE(tenv_mut0(&e, "node mut*"));
    TEST_ASSERT_TRUE(tenv_mut0(&e, "i32 mut"));
    TEST_ASSERT_FALSE(tenv_mut0(&e, "i32"));
    TEST_ASSERT_TRUE(tenv_mut0(&e, "point mut"));
    TEST_ASSERT_TRUE(tenv_mut0(&e, "i32[3][4] mut"));
    TEST_ASSERT_TRUE(tenv_mut0(&e, "node*[4] mut"));
    TEST_ASSERT_FALSE(tenv_mut0(&e, "node* mut@"));
    TEST_ASSERT_TRUE(tenv_mut0(&e, "node* mut@ mut"));
    TEST_ASSERT_TRUE(tenv_mut0(&e, "string own mut"));
    // The binding's marker is not part of the type.
    TEST_ASSERT_TRUE(tenv_type(&e, "node* mut") == tenv_type(&e, "node*"));
    TEST_ASSERT_TRUE(tenv_type(&e, "node mut*") != tenv_type(&e, "node*"));
    TEST_ASSERT_TRUE(tenv_type(&e, "point mut") == tenv_type(&e, "point"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[4] mut") == tenv_type(&e, "i32[4]"));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[3][4] mut") == tenv_type(&e, "i32[3][4]"));
    TEST_ASSERT_TRUE(tenv_type(&e, "string mut") == tenv_type(&e, "string"));
    TEST_ASSERT_TRUE(tenv_type(&e, "void* mut") == tenv_type(&e, "void*"));
    // A type with a suffix keeps its own marks whatever the binding says.
    TEST_ASSERT_TRUE(tenv_type(&e, "node mut* mut") == tenv_type(&e, "node mut*"));
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node mut* mut"), "node mut*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4] mut"), "i32[4]");
    tenv_free(&e);
})

// ---- ownership placement (D17.2) ------------------------------------------------

TEST(the_d17_2_table_marks_the_references_it_names, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "u8 mut@ own"), "y");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "u8@ own"), "y");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node mut* own"), "y");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node* mut@ own"), "yn");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node mut* own mut@ own"), "yy");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node* own@"), "ny");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "u8 mut@ own mut*"), "ny");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "string own"), "y");
    tenv_free(&e);
})

TEST(the_d17_2_table_marks_the_levels_it_names, {
    tenv_t e;
    tenv_init(&e);
    // `own` never changes which levels are mutable (D17.2).
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8 mut@ own"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8@ own"), "nn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut* own"), "ny");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* mut@ own"), "nyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut* own mut@ own"), "nyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* own@"), "nnn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8 mut@ own mut*"), "nyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "string own"), "n");
    tenv_free(&e);
})

TEST(the_d17_2_table_spells_its_declarations_back, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8 mut@ own"), "u8 mut@ own");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8@ own"), "u8@ own");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut* own"), "node mut* own");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node* mut@ own"), "node* mut@ own");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node mut* own mut@ own"), "node mut* own mut@ own");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node* own@"), "node* own@");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8 mut@ own mut*"), "u8 mut@ own mut*");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "string own"), "string own");
    tenv_free(&e);
})

TEST(the_marks_inside_a_derived_type_survive, {
    tenv_t e;
    tenv_init(&e);
    // `kids[i]` of a `node mut* own mut@ own` is a `node mut* own`, and
    // `*out` of a `u8 mut@ own mut*` a `u8 mut@ own` (D17.2).
    const type_t* kids = tenv_type(&e, "node mut* own mut@ own");
    TEST_ASSERT_TRUE(kids->elem == tenv_type(&e, "node mut* own"));
    const type_t* out = tenv_type(&e, "u8 mut@ own mut*");
    TEST_ASSERT_TRUE(out->elem == tenv_type(&e, "u8 mut@ own"));
    const type_t* view = tenv_type(&e, "node* own@");
    TEST_ASSERT_TRUE(view->elem == tenv_type(&e, "node* own"));
    const type_t* items = tenv_type(&e, "node* mut@ own");
    TEST_ASSERT_TRUE(items->elem == tenv_type(&e, "node*"));
    const type_t* t = tenv_type(&e, "node* own[4]");
    TEST_ASSERT_TRUE(t->elem == tenv_type(&e, "node* own"));
    tenv_free(&e);
})

TEST(each_own_marks_one_reference_only, {
    tenv_t e;
    tenv_init(&e);
    // A reference is owning only where its own position says so (D17.2).
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node** own"), "yn");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node* own* own"), "yy");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node* own*"), "ny");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "u8@* own"), "yn");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "u8@ own*"), "ny");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "string own@ own"), "yy");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "string@ own"), "yn");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node** own"), "node** own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* own* own"), "node* own* own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@ own*"), "u8@ own*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@* own"), "u8@* own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "string own@ own"), "string own@ own");
    tenv_free(&e);
})

TEST(function_types_are_spelled_fn_return_parameters, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* v = type_void(&e.tt);
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, i32, i32, i32)), "fn i32(i32, i32)");
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, i32, i32, NULL)), "fn i32(i32)");
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, v, NULL, NULL)), "fn void()");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_fn(&e.tt, v, NULL, 0, true)), "fn noreturn()");
    // Parameter and return types keep their own marks.
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, v, tenv_type(&e, "node mut* own"), NULL)),
                       "fn void(node mut* own)");
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, tenv_type(&e, "string own"), NULL, NULL)),
                       "fn string own()");
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, v, tenv_type(&e, "u8 mut@ own mut*"), NULL)),
                       "fn void(u8 mut@ own mut*)");
    // A function type may be a parameter type itself.
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, v, tenv_fn(&e, i32, i32, NULL), NULL)),
                       "fn void(fn i32(i32))");
    tenv_free(&e);
})

TEST(suffixes_after_a_function_type_apply_to_it, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* f = tenv_fn(&e, i32, i32, NULL);
    // `fn i32(i32)[4]`: an array of four function pointers (D3.6).
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_array(&e.tt, f, 4)), "fn i32(i32)[4]");
    // `fn i32[4](i32)`: a function returning an `i32[4]`.
    TEST_ASSERT_EQ_STR(tenv_str(&e, tenv_fn(&e, type_array(&e.tt, i32, 4), i32, NULL)),
                       "fn i32[4](i32)");
    // `fn i32(i32)*`: a pointer to a slot holding a function pointer.
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_ptr(&e.tt, f, false, false)), "fn i32(i32)*");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_ptr(&e.tt, f, false, true)), "fn i32(i32) mut*");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_span(&e.tt, f, false, false)), "fn i32(i32)@");
    tenv_free(&e);
})

// ---- spellings for diagnostics --------------------------------------------------

TEST(every_combination_of_markers_has_a_spelling, {
    tenv_t e;
    tenv_init(&e);
    // A mutable level behind an immutable one, which `&f` on an immutable
    // struct with a `node mut*` field yields, is written by marking each
    // position on its own (D5.3).
    const type_t* mut_node_ptr = tenv_type(&e, "node mut*");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_ptr(&e.tt, mut_node_ptr, false, false)), "node mut**");
    const type_t* mut_span = tenv_type(&e, "i32 mut@");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_span(&e.tt, mut_span, false, false)), "i32 mut@@");
    // An owned string inside a type has its own position too (D3.7, D17.2).
    const type_t* own_string = tenv_type(&e, "string own");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_ptr(&e.tt, own_string, false, false)), "string own*");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_span(&e.tt, own_string, false, false)), "string own@");
    // Each of those spellings reads back as the type it came from.
    TEST_ASSERT_TRUE(tenv_type(&e, "node mut**") == type_ptr(&e.tt, mut_node_ptr, false, false));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32 mut@@") == type_span(&e.tt, mut_span, false, false));
    TEST_ASSERT_TRUE(tenv_type(&e, "string own*") == type_ptr(&e.tt, own_string, false, false));
    TEST_ASSERT_TRUE(tenv_type(&e, "string own@") == type_span(&e.tt, own_string, false, false));
    tenv_free(&e);
})

TEST(only_an_array_behind_a_reference_needs_parentheses, {
    tenv_t e;
    tenv_init(&e);
    // No array suffix may follow a trailing reference suffix, so a pointer
    // to an array inside an array has no spelling (D3.6); diagnostics print
    // the inner type in parentheses.
    const type_t* arr_ptr = type_ptr(&e.tt, tenv_type(&e, "i32[4]"), false, false);
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_array(&e.tt, arr_ptr, 2)), "(i32[4])*[2]");
    const type_t* arr_span = type_span(&e.tt, tenv_type(&e, "i32[4]"), false, true);
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_array(&e.tt, arr_span, 2)), "(i32[4] mut)@[2]");
    // The marker of the storage behind the parentheses is written inside
    // them, since the position after `)` belongs to the suffix that follows.
    TEST_ASSERT_EQ_STR(
        tenv_str(&e, type_array(&e.tt, type_ptr(&e.tt, tenv_type(&e, "i32[4]"), false, true), 2)),
        "(i32[4] mut)*[2]");
    tenv_free(&e);
})

TEST(a_chain_deeper_than_one_group_continues_in_parentheses, {
    tenv_t e;
    tenv_init(&e);
    // The speller walks 64 nodes in one group; a deeper chain, which no
    // source can write, is cut and continued in parentheses rather than
    // truncated.
    const uint32_t depth = 70;
    const type_t* t = e.node;
    for (uint32_t i = 0; i < depth; i++) {
        t = type_ptr(&e.tt, t, false, false);
    }
    sb_t out;
    sb_init(&out);
    type_to_str(t, &out);
    const str_t spelled = sb_view(&out);
    // One `(`, one `)`, the base once and 70 stars.
    uint64_t stars = 0;
    uint64_t parens = 0;
    for (uint64_t i = 0; i < spelled.len; i++) {
        if (spelled.ptr[i] == '*') {
            stars++;
        }
        if (spelled.ptr[i] == '(') {
            parens++;
        }
    }
    TEST_ASSERT_EQ_UINT64(stars, (uint64_t)depth);
    TEST_ASSERT_EQ_UINT64(parens, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(t), (uint64_t)depth);
    sb_free(&out);
    tenv_free(&e);
})

TEST(the_null_and_error_types_have_a_spelling, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_null(&e.tt)), "null");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_error(&e.tt)), "<error>");
    TEST_ASSERT_EQ_STR(tenv_str(&e, type_void(&e.tt)), "void");
    TEST_ASSERT_EQ_STR(tenv_str(&e, e.point), "point");
    TEST_ASSERT_EQ_STR(tenv_str(&e, e.color), "color");
    tenv_free(&e);
})

TEST(spelling_a_type_appends_to_the_buffer, {
    tenv_t e;
    tenv_init(&e);
    sb_t out;
    sb_init(&out);
    sb_append(&out, "expected ");
    type_to_str(tenv_type(&e, "u8 mut@ own"), &out);
    sb_append(&out, ", found ");
    type_to_str_decl(tenv_type(&e, "node* mut"), true, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "expected u8 mut@ own, found node* mut");
    sb_free(&out);
    tenv_free(&e);
})

// ---- the builder on descriptors -------------------------------------------------

TEST(the_builder_reads_the_three_suffix_groups, {
    tenv_t e;
    tenv_init(&e);
    // `node*@*` is a pointer to a span of pointers: reference suffixes read
    // inside-out (D3.6), and each position carries its own markers (D5.3).
    type_suffix_t suffixes[3];
    suffixes[0].kind = SUFFIX_PTR;
    suffixes[0].len = 0;
    suffixes[0].own = false;
    suffixes[0].mut = false;
    suffixes[1].kind = SUFFIX_SPAN;
    suffixes[1].len = 0;
    suffixes[1].own = false;
    suffixes[1].mut = false;
    suffixes[2].kind = SUFFIX_PTR;
    suffixes[2].len = 0;
    suffixes[2].own = true;
    suffixes[2].mut = true;
    const type_build_t r = type_build(&e.tt, false, false, e.node, suffixes, 3);
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_TRUE(r.mut0);
    TEST_ASSERT_TRUE(r.type->kind == TYPE_PTR);
    TEST_ASSERT_TRUE(r.type->own);
    TEST_ASSERT_TRUE(r.type->elem->kind == TYPE_SPAN);
    TEST_ASSERT_FALSE(r.type->elem->own);
    TEST_ASSERT_TRUE(r.type->elem->elem->kind == TYPE_PTR);
    TEST_ASSERT_TRUE(r.type->elem->elem->elem == e.node);
    TEST_ASSERT_EQ_STR(tenv_str(&e, r.type), "node*@* own");
    TEST_ASSERT_TRUE(r.type == tenv_type(&e, "node*@* own"));
    // The array group sits between the two reference groups.
    type_suffix_t mixed[3];
    mixed[0].kind = SUFFIX_PTR;
    mixed[0].len = 0;
    mixed[0].own = false;
    mixed[0].mut = false;
    mixed[1].kind = SUFFIX_ARRAY;
    mixed[1].len = 4;
    mixed[1].own = false;
    mixed[1].mut = true;
    mixed[2].kind = SUFFIX_SPAN;
    mixed[2].len = 0;
    mixed[2].own = true;
    mixed[2].mut = false;
    const type_build_t m = type_build(&e.tt, true, false, e.node, mixed, 3);
    TEST_ASSERT_NULL(m.type);
    TEST_ASSERT_EQ_STR(m.error, "'own' marks a reference: write it after a '*' or an '@'");
    const type_build_t ok = type_build(&e.tt, false, false, e.node, mixed, 3);
    TEST_ASSERT_NULL(ok.error);
    TEST_ASSERT_FALSE(ok.mut0);
    TEST_ASSERT_EQ_STR(tenv_str(&e, ok.type), "node*[4] mut@ own");
    tenv_free(&e);
})

TEST(a_base_position_own_marks_a_string, {
    tenv_t e;
    tenv_init(&e);
    // `string` is the one base type that takes `own`, being a reference with
    // no suffix (D3.7, D17.2).
    type_build_t r = type_build(&e.tt, true, false, tenv_type(&e, "string"), NULL, 0);
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_TRUE(r.type == tenv_type(&e, "string own"));
    TEST_ASSERT_FALSE(r.mut0);
    // With the binding's marker as well.
    r = type_build(&e.tt, true, true, tenv_type(&e, "string"), NULL, 0);
    TEST_ASSERT_TRUE(r.type == tenv_type(&e, "string own"));
    TEST_ASSERT_TRUE(r.mut0);
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "string own mut"), "string own mut");
    // No other base type takes it, whatever the caller passes.
    r = type_build(&e.tt, true, false, tenv_type(&e, "node*"), NULL, 0);
    TEST_ASSERT_EQ_STR(r.error, "'own' marks a reference: write it after a '*' or an '@'");
    r = type_build(&e.tt, true, false, tenv_type(&e, "u8@"), NULL, 0);
    TEST_ASSERT_EQ_STR(r.error, "'own' marks a reference: write it after a '*' or an '@'");
    r = type_build(&e.tt, true, false, e.point, NULL, 0);
    TEST_ASSERT_EQ_STR(r.error, "'own' marks a reference: write it after a '*' or an '@'");
    tenv_free(&e);
})

TEST(a_poisoned_base_poisons_the_type_and_says_nothing, {
    tenv_t e;
    tenv_init(&e);
    const type_t* err = type_error(&e.tt);
    // The base already failed, so its markers are not diagnosed a second
    // time, whatever they are.
    type_build_t r = type_build(&e.tt, false, true, err, NULL, 0);
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_TRUE(r.type == err);
    TEST_ASSERT_TRUE(r.mut0);
    type_suffix_t suffixes[2];
    suffixes[0].kind = SUFFIX_PTR;
    suffixes[0].len = 0;
    suffixes[0].own = true;
    suffixes[0].mut = false;
    suffixes[1].kind = SUFFIX_ARRAY;
    suffixes[1].len = 4;
    suffixes[1].own = false;
    suffixes[1].mut = true;
    r = type_build(&e.tt, true, true, err, suffixes, 2);
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_TRUE(r.type == err);
    TEST_ASSERT_TRUE(r.mut0);
    // A poisoned element does the same through the constructors.
    TEST_ASSERT_TRUE(type_ptr(&e.tt, err, true, true) == err);
    tenv_free(&e);
})

TEST(the_builder_interns_what_the_constructors_intern, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(tenv_type(&e, "node*") == type_ptr(&e.tt, e.node, false, false));
    TEST_ASSERT_TRUE(tenv_type(&e, "node mut* own") == type_ptr(&e.tt, e.node, true, true));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32 mut@") ==
                     type_span(&e.tt, type_prim(&e.tt, PRIM_I32), false, true));
    TEST_ASSERT_TRUE(tenv_type(&e, "string own") == type_string(&e.tt, true));
    TEST_ASSERT_TRUE(tenv_type(&e, "void* own") == type_voidptr(&e.tt, true));
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[4]") == type_array(&e.tt, type_prim(&e.tt, PRIM_I32), 4));
    tenv_free(&e);
})

// ---- deeper chains --------------------------------------------------------------

TEST(deep_chains_round_trip, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node***"), "node***");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[2][3][4]"), "i32[2][3][4]");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@**"), "u8@**");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node*[4] mut*"), "node*[4] mut*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* own[4]*"), "node* own[4]*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@* own"), "u8@* own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "string@"), "string@");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "string@ own"), "string@ own");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "void**"), "void**");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[4][2]@"), "i32[4][2]@");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* mut* mut*"), "node* mut* mut*");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "point*[2]*"), "point*[2]*");
    tenv_free(&e);
})

TEST(deep_chains_mark_the_levels_the_suffixes_name, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node***"), "nnnn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut* mut* mut* mut"), "yyyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* mut**"), "nnyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node** mut*"), "nynn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node*** mut"), "ynnn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* mut* mut*"), "nyyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node*[4] mut*"), "nyn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node mut*[4] mut* mut"), "yyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8@**"), "nnnn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4][2]@"), "nn");
    tenv_free(&e);
})

TEST(the_binding_takes_the_last_position_of_every_shape, {
    tenv_t e;
    tenv_init(&e);
    // Whatever the type ends with, the marker before the name is the
    // binding's (D5.3).
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node* mut@ mut"), "yyn");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node* mut@ mut"), "node* mut@ mut");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* mut@ mut"), "node* mut@");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8@ mut* mut"), "yyn");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8@ mut* mut"), "u8@ mut* mut");
    // A fixed array holds its elements, so the array's own position is the
    // last one and it is the binding's (D5.2, D5.3).
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node*[4] mut"), "yn");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "node*[4][2] mut"), "yn");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "node*[4][2] mut"), "node*[4][2] mut");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "i32[4] mut"), "y");
    tenv_free(&e);
})

TEST(own_and_mut_combine_in_both_positions, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8 mut@ own mut* own"), "u8 mut@ own mut* own");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8 mut@ own mut* own"), "nyy");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8 mut@ own mut* own mut"), "yyy");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8 mut@ own mut* own mut"), "u8 mut@ own mut* own mut");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "u8 mut@ own mut* own"), "yy");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "u8@ own* mut"), "u8@ own*");
    TEST_ASSERT_EQ_STR(tenv_spell_decl(&e, "u8@ own* mut"), "u8@ own* mut");
    TEST_ASSERT_EQ_STR(tenv_muts(&e, "u8@ own* mut"), "ynn");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "u8@ own* mut"), "ny");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "node* own* own* own"), "node* own* own* own");
    TEST_ASSERT_EQ_STR(tenv_owns(&e, "node* own* own* own"), "yyy");
    tenv_free(&e);
})

TEST(the_declaration_spelling_writes_the_last_marker_only, {
    tenv_t e;
    tenv_init(&e);
    // `type_to_str` leaves the binding's position empty and
    // `type_to_str_decl` writes it; nothing else moves (D5.3).
    const type_t* t = tenv_type(&e, "node mut*");
    sb_t out;
    sb_init(&out);
    type_to_str_decl(t, false, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "node mut*");
    sb_clear(&out);
    type_to_str_decl(t, true, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "node mut* mut");
    sb_clear(&out);
    type_to_str_decl(e.point, true, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "point mut");
    sb_clear(&out);
    type_to_str_decl(e.point, false, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "point");
    sb_clear(&out);
    type_to_str_decl(tenv_type(&e, "i32[3][4]"), true, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "i32[3][4] mut");
    sb_clear(&out);
    type_to_str_decl(tenv_type(&e, "string own"), true, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "string own mut");
    sb_clear(&out);
    type_to_str_decl(tenv_type(&e, "node* mut@ own"), true, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "node* mut@ own mut");
    sb_free(&out);
    tenv_free(&e);
})

TEST(array_lengths_are_kept_exactly, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[1]"), "i32[1]");
    TEST_ASSERT_EQ_STR(tenv_spell(&e, "i32[65536]"), "i32[65536]");
    TEST_ASSERT_EQ_UINT64(tenv_type(&e, "i32[65536]")->len, (uint64_t)65536);
    TEST_ASSERT_EQ_UINT64(type_sizeof(tenv_type(&e, "u8[65536]")), (uint64_t)65536);
    TEST_ASSERT_TRUE(tenv_type(&e, "i32[1]") != tenv_type(&e, "i32[2]"));
    tenv_free(&e);
})

int main(int argc, char** argv) {
    TEST_INIT("types_build", argc, argv);
    TEST_RUN(the_d3_6_shapes_round_trip);
    TEST_RUN(the_d3_6_shapes_are_the_ones_it_names);
    TEST_RUN(an_array_suffix_never_follows_a_reference_suffix);
    TEST_RUN(a_marker_that_belongs_to_an_array_is_refused);
    TEST_RUN(an_own_marks_a_reference_and_nothing_else);
    TEST_RUN(builder_errors_of_lengths_and_void);
    TEST_RUN(an_own_mark_on_a_function_type_is_refused);
    TEST_RUN(the_d5_3_table_marks_the_levels_it_names);
    TEST_RUN(the_d5_3_table_spells_its_declarations_back);
    TEST_RUN(the_placement_rule_on_further_shapes);
    TEST_RUN(the_placement_rule_spells_further_shapes_back);
    TEST_RUN(the_last_position_is_the_binding);
    TEST_RUN(the_d17_2_table_marks_the_references_it_names);
    TEST_RUN(the_d17_2_table_marks_the_levels_it_names);
    TEST_RUN(the_d17_2_table_spells_its_declarations_back);
    TEST_RUN(the_marks_inside_a_derived_type_survive);
    TEST_RUN(each_own_marks_one_reference_only);
    TEST_RUN(function_types_are_spelled_fn_return_parameters);
    TEST_RUN(suffixes_after_a_function_type_apply_to_it);
    TEST_RUN(every_combination_of_markers_has_a_spelling);
    TEST_RUN(only_an_array_behind_a_reference_needs_parentheses);
    TEST_RUN(a_chain_deeper_than_one_group_continues_in_parentheses);
    TEST_RUN(the_null_and_error_types_have_a_spelling);
    TEST_RUN(spelling_a_type_appends_to_the_buffer);
    TEST_RUN(the_builder_reads_the_three_suffix_groups);
    TEST_RUN(a_base_position_own_marks_a_string);
    TEST_RUN(a_poisoned_base_poisons_the_type_and_says_nothing);
    TEST_RUN(the_builder_interns_what_the_constructors_intern);
    TEST_RUN(deep_chains_round_trip);
    TEST_RUN(deep_chains_mark_the_levels_the_suffixes_name);
    TEST_RUN(the_binding_takes_the_last_position_of_every_shape);
    TEST_RUN(own_and_mut_combine_in_both_positions);
    TEST_RUN(the_declaration_spelling_writes_the_last_marker_only);
    TEST_RUN(array_lengths_are_kept_exactly);
    TEST_EXIT();
}
