// Sizes, alignment and C/System V struct layout: the sizes of D3.1 and
// D3.15, the layout of type-system.md 4.1, the ceiling on the size of a type
// (D3.4) and the value-containment cycles of D3.8. The internal errors the module reports when a
// caller breaks one of its preconditions are at the end, each in a forked child.
//
// The representation and identity of a type are tested in types_test.c, its
// conversions in types_convert_test.c, the builder and the spelling in
// types_build_test.c.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fork.h"
#include "prim.h"
#include "str.h"
#include "types.h"
#include "types_helpers.h"

#include "test.h"

// The bytes of stderr a forked fatal path may write.
enum { ERR_MAX = 256 };

static uint64_t size_of(tenv_t* e, const char* text) {
    return type_sizeof(tenv_type(e, text));
}

static uint64_t align_of(tenv_t* e, const char* text) {
    return type_alignof(tenv_type(e, text));
}

TEST(sizeof_and_alignof_of_every_type_form, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i8"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "u8"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "bool"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "char"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i16"), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "u16"), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32"), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "f32"), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i64"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "f64"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(type_sizeof(e.color), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(type_alignof(e.color), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node*"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "void*"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32@"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "string"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "i32@"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "string"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "node*"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "i8"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "i16"), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "i32"), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "f64"), (uint64_t)8);
    // A function pointer is 8, and `own` changes no size (D17.1).
    TEST_ASSERT_EQ_UINT64(type_sizeof(tenv_fn(&e, type_prim(&e.tt, PRIM_I32), e.point, NULL)),
                          (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(type_alignof(tenv_fn(&e, type_void(&e.tt), NULL, NULL)), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "u8 mut@ own"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "string own"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node mut* own"), (uint64_t)8);
    tenv_free(&e);
})

TEST(sizeof_of_the_suffix_shapes_of_d3_6, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node*[16]"), (uint64_t)128);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node**"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32[3][4]"), (uint64_t)48);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32[4]@"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32@[4]"), (uint64_t)64);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32@@"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node* mut@"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "u8@*"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32[4]*"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node*@*"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "void*[2]"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node mut* own mut@ own"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node* own@"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "u8 mut@ own mut*"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "node* own[4]"), (uint64_t)32);
    // Alignment of an array is its element's (D3.1).
    TEST_ASSERT_EQ_UINT64(align_of(&e, "i32[3][4]"), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "node*[16]"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "u8[3]"), (uint64_t)1);
    tenv_free(&e);
})

TEST(sizeof_of_function_types_with_suffixes, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* f = tenv_fn(&e, i32, i32, NULL);
    // `fn i32(i32)[4]` is an array of four function pointers (D3.6).
    TEST_ASSERT_EQ_UINT64(type_sizeof(type_array(&e.tt, f, 4)), (uint64_t)32);
    // `fn i32[4](i32)` is one function pointer returning an `i32[4]`.
    TEST_ASSERT_EQ_UINT64(type_sizeof(tenv_fn(&e, type_array(&e.tt, i32, 4), i32, NULL)),
                          (uint64_t)8);
    // `fn i32(i32)*` points to a slot holding a function pointer.
    TEST_ASSERT_EQ_UINT64(type_sizeof(type_ptr(&e.tt, f, false, false)), (uint64_t)8);
    tenv_free(&e);
})

TEST(sizeof_of_the_error_type_is_zero, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_UINT64(type_sizeof(type_error(&e.tt)), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(type_alignof(type_error(&e.tt)), (uint64_t)1);
    tenv_free(&e);
})

// ---- struct layout (type-system.md 4.1, D3.8, D9.9) ------------------------------

TEST(layout_of_rec_matches_the_c_abi, {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[4];
    uint64_t offsets[4];
    fields[0] = tenv_type(&e, "u8");
    fields[1] = tenv_type(&e, "i32");
    fields[2] = tenv_type(&e, "u16");
    fields[3] = tenv_type(&e, "f64");
    TEST_ASSERT_TRUE(type_layout_begin(e.rec));
    TEST_ASSERT_TRUE(type_layout_struct(e.rec, fields, 4, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(offsets[3], (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(type_sizeof(e.rec), (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(type_alignof(e.rec), (uint64_t)8);
    TEST_ASSERT_TRUE(type_layout_state(e.rec) == LAYOUT_RESOLVED);
    TEST_ASSERT_FALSE(type_is_owning_aggregate(e.rec));
    tenv_free(&e);
})

TEST(layout_of_a_map_entry_is_40_bytes, {
    tenv_t e;
    tenv_init(&e);
    // struct str_map_entry { string key; i64 val; u64 hash; u8 state; }
    const type_t* entry = type_struct(&e.tt, str_from_cstr("str_map_entry"), "entry");
    const type_t* fields[4];
    uint64_t offsets[4];
    fields[0] = tenv_type(&e, "string");
    fields[1] = tenv_type(&e, "i64");
    fields[2] = tenv_type(&e, "u64");
    fields[3] = tenv_type(&e, "u8");
    TEST_ASSERT_TRUE(type_layout_begin(entry));
    TEST_ASSERT_TRUE(type_layout_struct(entry, fields, 4, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(offsets[3], (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(type_sizeof(entry), (uint64_t)40);
    TEST_ASSERT_EQ_UINT64(type_alignof(entry), (uint64_t)8);
    tenv_free(&e);
})

TEST(layout_of_a_struct_with_a_struct_field, {
    tenv_t e;
    tenv_init(&e);
    // struct outer { u8 a; point p; u8 b; }: point is 8 bytes, aligned 4.
    TEST_ASSERT_EQ_UINT64(type_sizeof(e.point), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(type_alignof(e.point), (uint64_t)4);
    const type_t* outer = type_struct(&e.tt, str_from_cstr("outer"), "outer");
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_type(&e, "u8");
    fields[1] = e.point;
    fields[2] = tenv_type(&e, "u8");
    TEST_ASSERT_TRUE(type_layout_begin(outer));
    TEST_ASSERT_TRUE(type_layout_struct(outer, fields, 3, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)12);
    TEST_ASSERT_EQ_UINT64(type_sizeof(outer), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(type_alignof(outer), (uint64_t)4);
    tenv_free(&e);
})

TEST(layout_of_arrays_slices_and_pointers_in_a_struct, {
    tenv_t e;
    tenv_init(&e);
    // struct box { u8 a; i32[3] xs; }
    const type_t* box = type_struct(&e.tt, str_from_cstr("box"), "box");
    const type_t* box_fields[2];
    uint64_t box_offsets[2];
    box_fields[0] = tenv_type(&e, "u8");
    box_fields[1] = tenv_type(&e, "i32[3]");
    TEST_ASSERT_TRUE(type_layout_begin(box));
    TEST_ASSERT_TRUE(type_layout_struct(box, box_fields, 2, box_offsets));
    TEST_ASSERT_EQ_UINT64(box_offsets[1], (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(type_sizeof(box), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(type_alignof(box), (uint64_t)4);
    // struct span { u8 a; i32@ s; node* p; }: a slice is 16 aligned 8.
    const type_t* span = type_struct(&e.tt, str_from_cstr("span"), "span");
    const type_t* span_fields[3];
    uint64_t span_offsets[3];
    span_fields[0] = tenv_type(&e, "u8");
    span_fields[1] = tenv_type(&e, "i32@");
    span_fields[2] = tenv_type(&e, "node*");
    TEST_ASSERT_TRUE(type_layout_begin(span));
    TEST_ASSERT_TRUE(type_layout_struct(span, span_fields, 3, span_offsets));
    TEST_ASSERT_EQ_UINT64(span_offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(span_offsets[1], (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(span_offsets[2], (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(type_sizeof(span), (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(type_alignof(span), (uint64_t)8);
    tenv_free(&e);
})

TEST(layout_of_a_struct_holding_a_struct_array, {
    tenv_t e;
    tenv_init(&e);
    // struct grid { point[3] ps; u8 tag; }: the element must be laid out
    // first, which type_layout_pending reports.
    const type_t* grid = type_struct(&e.tt, str_from_cstr("grid"), "grid");
    const type_t* points = type_array(&e.tt, e.point, 3);
    TEST_ASSERT_NULL(type_layout_pending(points));
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = points;
    fields[1] = tenv_type(&e, "u8");
    TEST_ASSERT_TRUE(type_layout_begin(grid));
    TEST_ASSERT_TRUE(type_layout_struct(grid, fields, 2, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(type_sizeof(grid), (uint64_t)28);
    TEST_ASSERT_EQ_UINT64(type_alignof(grid), (uint64_t)4);
    tenv_free(&e);
})

TEST(owning_aggregates_are_recognised_through_fields_and_elements, {
    tenv_t e;
    tenv_init(&e);
    // struct vec { i32 mut@ own data; u64 len; } is owning (D17.7).
    TEST_ASSERT_TRUE(type_is_owning_aggregate(e.vec));
    TEST_ASSERT_EQ_UINT64(type_sizeof(e.vec), (uint64_t)24);
    TEST_ASSERT_FALSE(type_is_owning_aggregate(e.point));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(e.node));
    // struct pair { vec a; vec b; }: owning through nested aggregates.
    const type_t* pair = type_struct(&e.tt, str_from_cstr("pair"), "pair");
    const type_t* pair_fields[2];
    uint64_t pair_offsets[2];
    pair_fields[0] = e.vec;
    pair_fields[1] = e.vec;
    TEST_ASSERT_TRUE(type_layout_begin(pair));
    TEST_ASSERT_TRUE(type_layout_struct(pair, pair_fields, 2, pair_offsets));
    TEST_ASSERT_TRUE(type_is_owning_aggregate(pair));
    TEST_ASSERT_EQ_UINT64(type_sizeof(pair), (uint64_t)48);
    // A fixed array of `own` pointers is owning; a slice of them is not,
    // since its elements are not held by value (D17.7).
    TEST_ASSERT_TRUE(type_is_owning_aggregate(tenv_type(&e, "node* own[4]")));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(tenv_type(&e, "node*[4]")));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(tenv_type(&e, "node* own@")));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(tenv_type(&e, "node mut* own mut@ own")));
    // An array of owning arrays is owning too.
    TEST_ASSERT_TRUE(type_is_owning_aggregate(type_array(&e.tt, tenv_type(&e, "node* own[4]"), 2)));
    // A struct holding an `own string` is owning (D17.12).
    const type_t* named = type_struct(&e.tt, str_from_cstr("named"), "named");
    const type_t* named_fields[1];
    uint64_t named_offsets[1];
    named_fields[0] = tenv_type(&e, "string own");
    TEST_ASSERT_TRUE(type_layout_begin(named));
    TEST_ASSERT_TRUE(type_layout_struct(named, named_fields, 1, named_offsets));
    TEST_ASSERT_TRUE(type_is_owning_aggregate(named));
    tenv_free(&e);
})

TEST(owning_reaches_through_arrays_of_structs_and_struct_fields, {
    tenv_t e;
    tenv_init(&e);
    // An array of owning structs is owning, and so is a struct whose field
    // is an owning array (D17.7).
    TEST_ASSERT_TRUE(type_is_owning_aggregate(type_array(&e.tt, e.vec, 2)));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(type_array(&e.tt, e.point, 2)));
    const type_t* slots = type_struct(&e.tt, str_from_cstr("slots"), "slots");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "node* own[4]");
    fields[1] = tenv_type(&e, "u64");
    TEST_ASSERT_TRUE(type_layout_begin(slots));
    TEST_ASSERT_TRUE(type_layout_struct(slots, fields, 2, offsets));
    TEST_ASSERT_TRUE(type_is_owning_aggregate(slots));
    TEST_ASSERT_EQ_UINT64(type_sizeof(slots), (uint64_t)40);
    // A struct whose field is an array of that struct is owning too.
    const type_t* nest = type_struct(&e.tt, str_from_cstr("nest"), "nest");
    const type_t* nest_fields[1];
    uint64_t nest_offsets[1];
    nest_fields[0] = type_array(&e.tt, slots, 2);
    TEST_ASSERT_TRUE(type_layout_begin(nest));
    TEST_ASSERT_TRUE(type_layout_struct(nest, nest_fields, 1, nest_offsets));
    TEST_ASSERT_TRUE(type_is_owning_aggregate(nest));
    TEST_ASSERT_EQ_UINT64(type_sizeof(nest), (uint64_t)80);
    // A borrowed slice of owned pointers is not owning: its elements are not
    // held by value (D17.7).
    const type_t* view = type_struct(&e.tt, str_from_cstr("view"), "view");
    const type_t* view_fields[1];
    uint64_t view_offsets[1];
    view_fields[0] = tenv_type(&e, "node* own@");
    TEST_ASSERT_TRUE(type_layout_begin(view));
    TEST_ASSERT_TRUE(type_layout_struct(view, view_fields, 1, view_offsets));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(view));
    tenv_free(&e);
})

TEST(layout_pending_names_the_struct_a_size_needs, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_layout_pending(e.rec) == e.rec);
    TEST_ASSERT_TRUE(type_layout_pending(type_array(&e.tt, e.rec, 2)) == e.rec);
    TEST_ASSERT_TRUE(type_layout_pending(type_array(&e.tt, type_array(&e.tt, e.rec, 2), 3)) ==
                     e.rec);
    // A reference to a struct needs no layout: it is 8 or 16 bytes.
    TEST_ASSERT_NULL(type_layout_pending(type_ptr(&e.tt, e.rec, false, false)));
    TEST_ASSERT_NULL(type_layout_pending(type_slice(&e.tt, e.rec, false, false)));
    TEST_ASSERT_NULL(type_layout_pending(tenv_type(&e, "i32[4]")));
    TEST_ASSERT_NULL(type_layout_pending(e.point)); // already resolved
    TEST_ASSERT_TRUE(type_layout_state(e.rec) == LAYOUT_UNRESOLVED);
    TEST_ASSERT_TRUE(type_layout_state(e.point) == LAYOUT_RESOLVED);
    // A struct being laid out is still pending.
    TEST_ASSERT_TRUE(type_layout_begin(e.rec));
    TEST_ASSERT_TRUE(type_layout_state(e.rec) == LAYOUT_RESOLVING);
    TEST_ASSERT_TRUE(type_layout_pending(e.rec) == e.rec);
    tenv_free(&e);
})

TEST(a_value_containment_cycle_is_reported_once, {
    tenv_t e;
    tenv_init(&e);
    // struct a { b x; } and struct b { a y; }: laying out `a` reaches `b`,
    // which reaches `a` again (D3.8, "infinite size").
    const type_t* a = type_struct(&e.tt, str_from_cstr("a"), "a");
    const type_t* b = type_struct(&e.tt, str_from_cstr("b"), "b");
    TEST_ASSERT_TRUE(type_layout_begin(a));
    TEST_ASSERT_TRUE(type_layout_pending(b) == b);
    TEST_ASSERT_TRUE(type_layout_begin(b));
    TEST_ASSERT_TRUE(type_layout_pending(a) == a);
    TEST_ASSERT_FALSE(type_layout_begin(a)); // the cycle
    type_layout_fail(a);
    type_layout_fail(b);
    TEST_ASSERT_TRUE(type_layout_state(a) == LAYOUT_ERROR);
    TEST_ASSERT_TRUE(type_layout_state(b) == LAYOUT_ERROR);
    // A failed struct has a size, so the compiler can carry on.
    TEST_ASSERT_EQ_UINT64(type_sizeof(a), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(type_alignof(a), (uint64_t)1);
    TEST_ASSERT_FALSE(type_is_owning_aggregate(a));
    TEST_ASSERT_NULL(type_layout_pending(a));
    tenv_free(&e);
})

TEST(a_struct_containing_itself_by_value_is_a_cycle, {
    tenv_t e;
    tenv_init(&e);
    // struct loop { i32 v; loop next; }
    const type_t* loop = type_struct(&e.tt, str_from_cstr("loop"), "loop");
    TEST_ASSERT_TRUE(type_layout_begin(loop));
    TEST_ASSERT_TRUE(type_layout_pending(loop) == loop);
    TEST_ASSERT_FALSE(type_layout_begin(loop));
    type_layout_fail(loop);
    TEST_ASSERT_EQ_UINT64(type_sizeof(loop), (uint64_t)0);
    // A self-reference through a pointer or a slice is not a cycle: `node`
    // and `tree` lay out (D3.8).
    const type_t* tree = type_struct(&e.tt, str_from_cstr("tree"), "tree");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "i32");
    fields[1] = type_slice(&e.tt, tree, false, false);
    TEST_ASSERT_TRUE(type_layout_begin(tree));
    TEST_ASSERT_NULL(type_layout_pending(fields[1]));
    TEST_ASSERT_TRUE(type_layout_struct(tree, fields, 2, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(type_sizeof(tree), (uint64_t)24);
    tenv_free(&e);
})

TEST(a_cycle_through_an_array_field_is_found_too, {
    tenv_t e;
    tenv_init(&e);
    // struct a { b[2] x; } and struct b { a y; }: an array adds no level, so
    // the containment is by value and the size is infinite (D3.4, D3.8).
    const type_t* a = type_struct(&e.tt, str_from_cstr("a"), "a");
    const type_t* b = type_struct(&e.tt, str_from_cstr("b"), "b");
    const type_t* pair = type_array(&e.tt, b, 2);
    TEST_ASSERT_TRUE(type_layout_begin(a));
    // The field is an array, and the struct behind it is what needs a layout.
    TEST_ASSERT_TRUE(type_layout_pending(pair) == b);
    TEST_ASSERT_TRUE(type_layout_begin(b));
    TEST_ASSERT_TRUE(type_layout_pending(a) == a);
    TEST_ASSERT_FALSE(type_layout_begin(a));
    type_layout_fail(a);
    type_layout_fail(b);
    TEST_ASSERT_TRUE(type_layout_state(a) == LAYOUT_ERROR);
    TEST_ASSERT_EQ_UINT64(type_sizeof(pair), (uint64_t)0);
    // A struct that holds the failed array still lays out.
    const type_t* holder = type_struct(&e.tt, str_from_cstr("holder"), "holder");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "i32");
    fields[1] = pair;
    TEST_ASSERT_TRUE(type_layout_begin(holder));
    TEST_ASSERT_TRUE(type_layout_struct(holder, fields, 2, offsets));
    TEST_ASSERT_EQ_UINT64(type_sizeof(holder), (uint64_t)4);
    tenv_free(&e);
})

TEST(a_failed_struct_can_be_a_field_of_another, {
    tenv_t e;
    tenv_init(&e);
    const type_t* bad = type_struct(&e.tt, str_from_cstr("bad"), "bad");
    type_layout_fail(bad);
    const type_t* holder = type_struct(&e.tt, str_from_cstr("holder"), "holder");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "i32");
    fields[1] = bad;
    TEST_ASSERT_NULL(type_layout_pending(bad));
    TEST_ASSERT_TRUE(type_layout_begin(holder));
    TEST_ASSERT_TRUE(type_layout_struct(holder, fields, 2, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(type_sizeof(holder), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(type_alignof(holder), (uint64_t)4);
    tenv_free(&e);
})

// ---- the size ceiling (D3.4) ----------------------------------------------------

TEST(a_type_at_the_ceiling_still_fits, {
    tenv_t e;
    tenv_init(&e);
    // 2^63 - 1 bytes is the largest object the compiler admits (D3.4).
    const uint64_t max = (uint64_t)INT64_MAX;
    const type_t* u8 = tenv_type(&e, "u8");
    const type_t* biggest = type_array(&e.tt, u8, max);
    TEST_ASSERT_TRUE(type_size_fits(biggest));
    TEST_ASSERT_EQ_UINT64(type_sizeof(biggest), max);
    TEST_ASSERT_EQ_UINT64(type_alignof(biggest), (uint64_t)1);
    // The largest `i64` array holds an eighth as many elements.
    const type_t* eighth = type_array(&e.tt, tenv_type(&e, "i64"), max >> 3U);
    TEST_ASSERT_TRUE(type_size_fits(eighth));
    TEST_ASSERT_EQ_UINT64(type_sizeof(eighth), (max >> 3U) << 3U);
    TEST_ASSERT_FALSE(type_size_fits(type_array(&e.tt, tenv_type(&e, "i64"), (max >> 3U) + 1U)));
    tenv_free(&e);
})

TEST(a_type_past_the_ceiling_does_not_fit, {
    tenv_t e;
    tenv_init(&e);
    const uint64_t max = (uint64_t)INT64_MAX;
    const type_t* u8 = tenv_type(&e, "u8");
    // One byte past the ceiling, and a product that would wrap.
    TEST_ASSERT_FALSE(type_size_fits(type_array(&e.tt, u8, max + 2U)));
    TEST_ASSERT_FALSE(type_size_fits(type_array(&e.tt, tenv_type(&e, "i32"), max)));
    TEST_ASSERT_FALSE(type_size_fits(type_array(&e.tt, tenv_type(&e, "i64"), max)));
    TEST_ASSERT_FALSE(type_size_fits(type_array(&e.tt, e.point, max)));
    // Nested arrays multiply, and the ceiling holds through them.
    const type_t* row = type_array(&e.tt, u8, (uint64_t)1U << 33U);
    TEST_ASSERT_TRUE(type_size_fits(row));
    TEST_ASSERT_FALSE(type_size_fits(type_array(&e.tt, row, (uint64_t)1U << 31U)));
    TEST_ASSERT_TRUE(type_size_fits(type_array(&e.tt, row, (uint64_t)1U << 29U)));
    // A reference to an object that could never exist is 8 or 16 bytes and
    // fits: only the object itself is too large (D3.4, D3.5).
    TEST_ASSERT_TRUE(type_size_fits(type_ptr(&e.tt, type_array(&e.tt, u8, max), false, false)));
    TEST_ASSERT_TRUE(type_size_fits(type_slice(&e.tt, u8, false, false)));
    tenv_free(&e);
})

TEST(a_struct_past_the_ceiling_is_reported_like_a_cycle, {
    tenv_t e;
    tenv_init(&e);
    const uint64_t max = (uint64_t)INT64_MAX;
    const type_t* half = type_array(&e.tt, tenv_type(&e, "u8"), (max >> 1U) + 1U);
    TEST_ASSERT_TRUE(type_size_fits(half));
    // struct twice { u8[2^62] a; u8[2^62] b; }: each field fits, the two
    // together do not (D3.4).
    const type_t* twice = type_struct(&e.tt, str_from_cstr("twice"), "twice");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = half;
    fields[1] = half;
    TEST_ASSERT_TRUE(type_layout_begin(twice));
    TEST_ASSERT_FALSE(type_layout_struct(twice, fields, 2, offsets));
    // The struct is left as the infinite-size error leaves one.
    TEST_ASSERT_TRUE(type_layout_state(twice) == LAYOUT_ERROR);
    TEST_ASSERT_EQ_UINT64(type_sizeof(twice), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(type_alignof(twice), (uint64_t)1);
    TEST_ASSERT_NULL(type_layout_pending(twice));
    tenv_free(&e);
})

TEST(a_struct_with_a_field_past_the_ceiling_fails, {
    tenv_t e;
    tenv_init(&e);
    const uint64_t max = (uint64_t)INT64_MAX;
    const type_t* big = type_struct(&e.tt, str_from_cstr("big"), "big");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "u8");
    fields[1] = type_array(&e.tt, tenv_type(&e, "i32"), max);
    TEST_ASSERT_FALSE(type_size_fits(fields[1]));
    TEST_ASSERT_TRUE(type_layout_begin(big));
    TEST_ASSERT_FALSE(type_layout_struct(big, fields, 2, offsets));
    TEST_ASSERT_TRUE(type_layout_state(big) == LAYOUT_ERROR);
    TEST_ASSERT_EQ_UINT64(type_sizeof(big), (uint64_t)0);
    // A struct that failed is a field like any other failed struct.
    TEST_ASSERT_FALSE(type_is_owning_aggregate(big));
    TEST_ASSERT_TRUE(type_size_fits(big));
    tenv_free(&e);
})

TEST(rounding_the_size_up_cannot_push_a_struct_past_the_ceiling, {
    tenv_t e;
    tenv_init(&e);
    const uint64_t max = (uint64_t)INT64_MAX;
    // struct edge { i64 a; u8[2^63 - 9] b; }: every field fits and so does
    // their sum, which is the ceiling exactly, but rounding the size up to
    // the alignment of `i64` passes it (D3.4, D3.8).
    const type_t* edge = type_struct(&e.tt, str_from_cstr("edge"), "edge");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "i64");
    fields[1] = type_array(&e.tt, tenv_type(&e, "u8"), max - 8U);
    TEST_ASSERT_TRUE(type_size_fits(fields[1]));
    TEST_ASSERT_TRUE(type_layout_begin(edge));
    TEST_ASSERT_FALSE(type_layout_struct(edge, fields, 2, offsets));
    TEST_ASSERT_TRUE(type_layout_state(edge) == LAYOUT_ERROR);
    TEST_ASSERT_EQ_UINT64(type_sizeof(edge), (uint64_t)0);
    // One byte less and the same struct lays out, its size rounded to 2^63
    // minus 8.
    const type_t* fits = type_struct(&e.tt, str_from_cstr("fits"), "fits");
    const type_t* small[2];
    uint64_t small_offsets[2];
    small[0] = tenv_type(&e, "i64");
    small[1] = type_array(&e.tt, tenv_type(&e, "u8"), max - 15U);
    TEST_ASSERT_TRUE(type_layout_begin(fits));
    TEST_ASSERT_TRUE(type_layout_struct(fits, small, 2, small_offsets));
    TEST_ASSERT_EQ_UINT64(small_offsets[1], (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(type_sizeof(fits), max - 7U);
    TEST_ASSERT_EQ_UINT64(type_alignof(fits), (uint64_t)8);
    tenv_free(&e);
})

TEST(alignment_padding_cannot_push_a_struct_past_the_ceiling, {
    tenv_t e;
    tenv_init(&e);
    const uint64_t max = (uint64_t)INT64_MAX;
    // struct edge { u8[2^63 - 1] a; u8 b; }: the second field is one byte
    // past the ceiling.
    const type_t* edge = type_struct(&e.tt, str_from_cstr("edge"), "edge");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = type_array(&e.tt, tenv_type(&e, "u8"), max);
    fields[1] = tenv_type(&e, "u8");
    TEST_ASSERT_TRUE(type_layout_begin(edge));
    TEST_ASSERT_FALSE(type_layout_struct(edge, fields, 2, offsets));
    TEST_ASSERT_TRUE(type_layout_state(edge) == LAYOUT_ERROR);
    // The same struct with the big field alone lays out at the ceiling.
    const type_t* just = type_struct(&e.tt, str_from_cstr("just"), "just");
    uint64_t just_offsets[1];
    TEST_ASSERT_TRUE(type_layout_begin(just));
    TEST_ASSERT_TRUE(type_layout_struct(just, fields, 1, just_offsets));
    TEST_ASSERT_EQ_UINT64(type_sizeof(just), max);
    TEST_ASSERT_EQ_UINT64(type_alignof(just), (uint64_t)1);
    tenv_free(&e);
})

// ---- internal errors ------------------------------------------------------------

static void sizeof_unresolved_struct(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_sizeof(e.rec));
    tenv_free(&e);
}

static void ref_at_level_zero(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_ref_at(tenv_type(&e, "node*"), 0));
    tenv_free(&e);
}

static void array_of_length_zero(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_array(&e.tt, tenv_type(&e, "i32"), 0));
    tenv_free(&e);
}

static void struct_without_fields(void) {
    tenv_t e;
    tenv_init(&e);
    uint64_t offsets[1];
    TEST_UNUSED(type_layout_begin(e.rec));
    TEST_UNUSED(type_layout_struct(e.rec, NULL, 0, offsets));
    tenv_free(&e);
}

static void pointer_to_void(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_ptr(&e.tt, type_void(&e.tt), false, false));
    tenv_free(&e);
}

static void primitive_void(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_prim(&e.tt, PRIM_VOID));
    tenv_free(&e);
}

static void slice_of_null(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_slice(&e.tt, type_null(&e.tt), false, false));
    tenv_free(&e);
}

static void function_returning_null(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_fn(&e.tt, type_null(&e.tt), NULL, 0, false));
    tenv_free(&e);
}

static void noreturn_with_a_result(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_fn(&e.tt, tenv_type(&e, "i32"), NULL, 0, true));
    tenv_free(&e);
}

static void parameter_of_type_void(void) {
    tenv_t e;
    tenv_init(&e);
    const type_t* params[1];
    params[0] = type_void(&e.tt);
    TEST_UNUSED(type_fn(&e.tt, type_void(&e.tt), params, 1, false));
    tenv_free(&e);
}

static void owning_query_without_layout(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_is_owning_aggregate(e.rec));
    tenv_free(&e);
}

static void layout_of_a_pointer(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_layout_state(tenv_type(&e, "node*")));
    tenv_free(&e);
}

static void sizeof_of_void(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_sizeof(type_void(&e.tt)));
    tenv_free(&e);
}

static void alignof_of_null(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_alignof(type_null(&e.tt)));
    tenv_free(&e);
}

static void layout_begun_twice(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_layout_begin(e.point)); // point is already RESOLVED
    tenv_free(&e);
}

static void layout_of_a_struct_twice(void) {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[1];
    uint64_t offsets[1];
    fields[0] = tenv_type(&e, "i32");
    TEST_UNUSED(type_layout_struct(e.point, fields, 1, offsets));
    tenv_free(&e);
}

static void layout_with_a_pending_field(void) {
    tenv_t e;
    tenv_init(&e);
    const type_t* holder = type_struct(&e.tt, str_from_cstr("holder"), "holder");
    const type_t* fields[1];
    uint64_t offsets[1];
    fields[0] = e.rec; // still UNRESOLVED
    TEST_UNUSED(type_layout_begin(holder));
    TEST_UNUSED(type_layout_struct(holder, fields, 1, offsets));
    tenv_free(&e);
}

static void sizeof_of_a_type_too_large(void) {
    tenv_t e;
    tenv_init(&e);
    const type_t* huge = type_array(&e.tt, tenv_type(&e, "i32"), (uint64_t)INT64_MAX);
    TEST_UNUSED(type_sizeof(huge));
    tenv_free(&e);
}

static void own_on_an_owned_base(void) {
    tenv_t e;
    tenv_init(&e);
    const type_t* owned = type_string(&e.tt, true);
    TEST_UNUSED(type_build(&e.tt, true, false, owned, NULL, 0).type);
    tenv_free(&e);
}

static void one_declaration_two_nominal_kinds(void) {
    tenv_t e;
    tenv_init(&e);
    TEST_UNUSED(type_enum(&e.tt, str_from_cstr("node"), "node"));
    tenv_free(&e);
}

TEST(the_size_of_a_struct_without_layout_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(sizeof_unresolved_struct, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: sizeof of a struct without layout\n");
})

TEST(level_zero_is_not_a_reference, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(ref_at_level_zero, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: level 0 is not a reference\n");
})

TEST(an_array_length_of_zero_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(array_of_length_zero, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: array length 0\n");
})

TEST(an_empty_struct_never_reaches_the_layout, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(struct_without_fields, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: layout of an empty struct\n");
})

TEST(void_is_never_an_element_type, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(pointer_to_void, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: void as an element type\n");
})

TEST(prim_void_is_not_a_primitive_type, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(primitive_void, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err,
                       "fort: error: internal error: PRIM_VOID as a primitive type: "
                       "use type_void\n");
})

TEST(layout_packs_the_primitives_by_alignment, {
    tenv_t e;
    tenv_init(&e);
    // struct all { i8 a; i16 b; i32 c; i64 d; }
    const type_t* all = type_struct(&e.tt, str_from_cstr("all"), "all");
    const type_t* fields[4];
    uint64_t offsets[4];
    fields[0] = tenv_type(&e, "i8");
    fields[1] = tenv_type(&e, "i16");
    fields[2] = tenv_type(&e, "i32");
    fields[3] = tenv_type(&e, "i64");
    TEST_ASSERT_TRUE(type_layout_begin(all));
    TEST_ASSERT_TRUE(type_layout_struct(all, fields, 4, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(offsets[3], (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(type_sizeof(all), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(type_alignof(all), (uint64_t)8);
    tenv_free(&e);
})

TEST(layout_rounds_the_size_up_to_the_alignment, {
    tenv_t e;
    tenv_init(&e);
    // struct pad { u16 a; u8 b; }: size 4, alignment 2.
    const type_t* pad = type_struct(&e.tt, str_from_cstr("pad"), "pad");
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "u16");
    fields[1] = tenv_type(&e, "u8");
    TEST_ASSERT_TRUE(type_layout_begin(pad));
    TEST_ASSERT_TRUE(type_layout_struct(pad, fields, 2, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(type_sizeof(pad), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(type_alignof(pad), (uint64_t)2);
    // struct one { bool b; }: size 1, alignment 1.
    const type_t* one = type_struct(&e.tt, str_from_cstr("one"), "one");
    const type_t* one_fields[1];
    uint64_t one_offsets[1];
    one_fields[0] = tenv_type(&e, "bool");
    TEST_ASSERT_TRUE(type_layout_begin(one));
    TEST_ASSERT_TRUE(type_layout_struct(one, one_fields, 1, one_offsets));
    TEST_ASSERT_EQ_UINT64(type_sizeof(one), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(type_alignof(one), (uint64_t)1);
    tenv_free(&e);
})

TEST(layout_of_reference_and_function_pointer_fields, {
    tenv_t e;
    tenv_init(&e);
    // struct hooks { fn i32(i32) f; void* ctx; u8 tag; }
    const type_t* hooks = type_struct(&e.tt, str_from_cstr("hooks"), "hooks");
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_fn(&e, type_prim(&e.tt, PRIM_I32), type_prim(&e.tt, PRIM_I32), NULL);
    fields[1] = tenv_type(&e, "void*");
    fields[2] = tenv_type(&e, "u8");
    TEST_ASSERT_TRUE(type_layout_begin(hooks));
    TEST_ASSERT_TRUE(type_layout_struct(hooks, fields, 3, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(type_sizeof(hooks), (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(type_alignof(hooks), (uint64_t)8);
    // A function pointer is not an owning field, whatever it returns.
    TEST_ASSERT_FALSE(type_is_owning_aggregate(hooks));
    tenv_free(&e);
})

TEST(layout_of_a_struct_with_an_owned_pointer_field, {
    tenv_t e;
    tenv_init(&e);
    // struct arena { u8 mut@ own bytes; void* own raw; u64 used; }
    const type_t* arena = type_struct(&e.tt, str_from_cstr("arena"), "arena");
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_type(&e, "u8 mut@ own");
    fields[1] = tenv_type(&e, "void* own");
    fields[2] = tenv_type(&e, "u64");
    TEST_ASSERT_TRUE(type_layout_begin(arena));
    TEST_ASSERT_TRUE(type_layout_struct(arena, fields, 3, offsets));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(type_sizeof(arena), (uint64_t)32);
    TEST_ASSERT_TRUE(type_is_owning_aggregate(arena));
    // `own` changes no size: the same struct without the marks is as big.
    const type_t* plain = type_struct(&e.tt, str_from_cstr("plain"), "plain");
    const type_t* plain_fields[3];
    uint64_t plain_offsets[3];
    plain_fields[0] = tenv_type(&e, "u8 mut@");
    plain_fields[1] = tenv_type(&e, "void*");
    plain_fields[2] = tenv_type(&e, "u64");
    TEST_ASSERT_TRUE(type_layout_begin(plain));
    TEST_ASSERT_TRUE(type_layout_struct(plain, plain_fields, 3, plain_offsets));
    TEST_ASSERT_EQ_UINT64(type_sizeof(plain), type_sizeof(arena));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(plain));
    tenv_free(&e);
})

TEST(sizeof_of_arrays_of_structs_and_slices, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_EQ_UINT64(type_sizeof(type_array(&e.tt, e.point, 3)), (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(type_alignof(type_array(&e.tt, e.point, 3)), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(type_sizeof(type_array(&e.tt, e.node, 2)), (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(type_sizeof(type_array(&e.tt, e.vec, 2)), (uint64_t)48);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "string[3]"), (uint64_t)48);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "i32[2][3][4]"), (uint64_t)96);
    TEST_ASSERT_EQ_UINT64(size_of(&e, "bool[7]"), (uint64_t)7);
    TEST_ASSERT_EQ_UINT64(align_of(&e, "bool[7]"), (uint64_t)1);
    tenv_free(&e);
})

TEST(the_null_type_is_never_an_element_or_a_signature_part, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(slice_of_null, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: null as an element type\n");
    TEST_ASSERT_EQ_INT32(run_forked(function_returning_null, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: null as a return type\n");
    TEST_ASSERT_EQ_INT32(run_forked(parameter_of_type_void, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: void or null as a parameter type\n");
})

TEST(a_noreturn_function_has_no_result_type, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(noreturn_with_a_result, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: noreturn with a return type\n");
})

TEST(the_owning_query_needs_a_layout, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(owning_query_without_layout, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err,
                       "fort: error: internal error: owning query on a struct without layout\n");
})

TEST(only_a_struct_has_a_layout, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(layout_of_a_pointer, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: layout of a non-struct\n");
})

TEST(void_and_null_have_no_size, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(sizeof_of_void, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: sizeof of void or null\n");
    TEST_ASSERT_EQ_INT32(run_forked(alignof_of_null, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: alignof of void or null\n");
})

TEST(a_struct_is_laid_out_once, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(layout_begun_twice, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: layout begun twice\n");
    TEST_ASSERT_EQ_INT32(run_forked(layout_of_a_struct_twice, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: layout of a struct twice\n");
})

TEST(a_field_is_laid_out_before_the_struct_that_holds_it, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(layout_with_a_pending_field, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: layout with a pending field\n");
})

TEST(the_size_of_a_type_too_large_is_an_internal_error, {
    char err[ERR_MAX];
    // The checker asks type_size_fits at the declaration and reports "type
    // is too large" there (D3.4); reaching sizeof anyway is a compiler bug.
    TEST_ASSERT_EQ_INT32(run_forked(sizeof_of_a_type_too_large, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: sizeof of a type that is too large\n");
})

TEST(a_base_type_is_never_an_owned_one, {
    char err[ERR_MAX];
    // No source can write `own` on a base type that already owns: the base
    // of a written type is never an owned type (D17.2), so the builder
    // treats it as a compiler bug rather than a diagnostic.
    TEST_ASSERT_EQ_INT32(run_forked(own_on_an_owned_base, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err,
                       "fort: error: internal error: own on a base type that is already owned\n");
})

TEST(a_declaration_names_one_nominal_kind, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(one_declaration_two_nominal_kinds, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(
        err, "fort: error: internal error: one declaration used for a struct and an enum\n");
})

int main(int argc, char** argv) {
    TEST_INIT("types_layout", argc, argv);
    TEST_RUN(sizeof_and_alignof_of_every_type_form);
    TEST_RUN(sizeof_of_the_suffix_shapes_of_d3_6);
    TEST_RUN(sizeof_of_function_types_with_suffixes);
    TEST_RUN(sizeof_of_the_error_type_is_zero);
    TEST_RUN(layout_of_rec_matches_the_c_abi);
    TEST_RUN(layout_of_a_map_entry_is_40_bytes);
    TEST_RUN(layout_of_a_struct_with_a_struct_field);
    TEST_RUN(layout_of_arrays_slices_and_pointers_in_a_struct);
    TEST_RUN(layout_of_a_struct_holding_a_struct_array);
    TEST_RUN(owning_aggregates_are_recognised_through_fields_and_elements);
    TEST_RUN(owning_reaches_through_arrays_of_structs_and_struct_fields);
    TEST_RUN(layout_pending_names_the_struct_a_size_needs);
    TEST_RUN(a_value_containment_cycle_is_reported_once);
    TEST_RUN(a_struct_containing_itself_by_value_is_a_cycle);
    TEST_RUN(a_cycle_through_an_array_field_is_found_too);
    TEST_RUN(a_failed_struct_can_be_a_field_of_another);
    TEST_RUN(the_size_of_a_struct_without_layout_is_an_internal_error);
    TEST_RUN(level_zero_is_not_a_reference);
    TEST_RUN(an_array_length_of_zero_is_an_internal_error);
    TEST_RUN(an_empty_struct_never_reaches_the_layout);
    TEST_RUN(void_is_never_an_element_type);
    TEST_RUN(prim_void_is_not_a_primitive_type);
    TEST_RUN(layout_packs_the_primitives_by_alignment);
    TEST_RUN(layout_rounds_the_size_up_to_the_alignment);
    TEST_RUN(layout_of_reference_and_function_pointer_fields);
    TEST_RUN(layout_of_a_struct_with_an_owned_pointer_field);
    TEST_RUN(sizeof_of_arrays_of_structs_and_slices);
    TEST_RUN(a_type_at_the_ceiling_still_fits);
    TEST_RUN(a_type_past_the_ceiling_does_not_fit);
    TEST_RUN(a_struct_past_the_ceiling_is_reported_like_a_cycle);
    TEST_RUN(a_struct_with_a_field_past_the_ceiling_fails);
    TEST_RUN(rounding_the_size_up_cannot_push_a_struct_past_the_ceiling);
    TEST_RUN(alignment_padding_cannot_push_a_struct_past_the_ceiling);
    TEST_RUN(the_null_type_is_never_an_element_or_a_signature_part);
    TEST_RUN(a_noreturn_function_has_no_result_type);
    TEST_RUN(the_owning_query_needs_a_layout);
    TEST_RUN(only_a_struct_has_a_layout);
    TEST_RUN(void_and_null_have_no_size);
    TEST_RUN(a_struct_is_laid_out_once);
    TEST_RUN(a_field_is_laid_out_before_the_struct_that_holds_it);
    TEST_RUN(the_size_of_a_type_too_large_is_an_internal_error);
    TEST_RUN(a_base_type_is_never_an_owned_one);
    TEST_RUN(a_declaration_names_one_nominal_kind);
    TEST_EXIT();
}
