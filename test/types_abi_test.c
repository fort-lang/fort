// The one silent failure mode of D3.8: fort computes struct layout itself and
// the C ABI computes it independently, and a disagreement neither fails to
// compile nor fails a language test -- it reads the wrong bytes at the only
// boundary that can see it, which is pointer-based C interop (D9.9, "Struct
// layout stays C-compatible, so pointer-based interop works").
//
// So every struct here is written twice: once as the field list
// `type_layout_struct` lays out and once as the C struct it must equal, and
// the assertions are against `offsetof`, `sizeof` and `alignof` of that
// mirror rather than against numbers written by hand. A number written by
// hand states what the author believed; `offsetof` states what the ABI does.
//
// The suite is compiled for the host (arm64 Linux) and the target is x86-64
// Linux, which is sound for these layouts because the two ABIs are both LP64
// with natural alignment and fort has no type whose C alignment differs
// between them: the widest are 8 bytes (`i64`, `f64`, every pointer, and the
// two words of a span). `long double`, the one scalar C lays out differently
// on the two, has no fort spelling (D3.1). test/lang/run/ffi/006 closes the
// remaining gap on the target itself, where a C helper compiled by the cross
// clang reads fields of a struct fort allocated.
//
// The layout algorithm's own rules -- the order, the ceiling of D3.4, the
// cycles of D3.8 -- are tested in types_layout_test.c; this suite only holds
// it against C.
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prim.h"
#include "str.h"
#include "types.h"
#include "types_helpers.h"

#include "test.h"

// The most fields any mirror below has.
enum { MAX_FIELDS = 4 };

// A span and a `string` are one two-word header and are never split (D9.9),
// so their mirror is the runtime's own `struct fort_span` (D3.5, D3.7).
struct span_mirror {
    void* ptr;
    uint64_t len;
};

// struct rec { u8 tag; i32 n; u16 k; f64 x; } (type-system.md 4.1).
struct rec_mirror {
    uint8_t tag;
    int32_t n;
    uint16_t k;
    double x;
};

// The largest member first, so the struct ends in padding C still counts.
struct tail_mirror {
    int64_t big;
    uint8_t tag;
};

// Three bytes of trailing padding, so an array of it strides by more than the
// bytes its fields occupy.
struct small_mirror {
    int32_t n;
    uint8_t tag;
};

struct nest_mirror {
    uint8_t tag;
    struct rec_mirror inner;
};

struct grid_mirror {
    struct small_mirror cells[3];
    uint8_t tag;
};

// `bool` is `i1` in a value and `i8` in memory (D19.2), so in a struct it is
// C's `_Bool`.
struct flags_mirror {
    bool on;
    int32_t n;
    bool off;
};

struct spanful_mirror {
    uint8_t tag;
    struct span_mirror name;
    struct span_mirror vals;
};

struct plain_mirror {
    uint8_t tag;
    struct span_mirror s;
};

struct refs_mirror {
    void* node;
    int32_t (*fn)(int32_t);
    void* opaque;
};

// An enum's underlying type is `i32` and its size 4 (D3.9), so its mirror is
// `int32_t` and not a C `enum`, whose underlying type C leaves to the
// implementation.
struct tagged_mirror {
    uint8_t tag;
    int32_t colour;
    int64_t big;
};

// `char` is an unsigned byte (D3.2) and `f32` is C's `float` (D3.1).
struct narrow_mirror {
    char first;
    float weight;
    unsigned char last;
};

// A struct of one field: the size is the field's, rounded up to its own
// alignment, so it adds nothing.
struct one_mirror {
    int64_t only;
};

// The classic C divergence: `inner` is five bytes of fields and eight of
// storage, so the field after it stands at 8 and not at 5.
struct inner_mirror {
    int32_t n;
    uint8_t tag;
};

struct after_mirror {
    struct inner_mirror inner;
    uint8_t tag;
};

// Lays out a fresh nominal struct of `n` fields and writes their offsets. A
// layout that refuses leaves the struct ERROR and `offsets` partly
// uninitialised, so every caller asserts `laid_out` before it reads one.
static const type_t* lay_out(
    tenv_t* e, const char* name, const type_t* const* fields, uint32_t n, uint64_t* offsets) {
    const type_t* s = tenv_struct(e, name);
    TEST_UNUSED(type_layout_begin(s));
    TEST_UNUSED(type_layout_struct(s, fields, n, offsets));
    return s;
}

// Whether the layout ran to the end: the state type_layout_struct leaves on a
// true return, and never the ERROR of a refusal.
static bool laid_out(const type_t* s) {
    return type_layout_state(s) == LAYOUT_RESOLVED;
}

// Whether the struct `s` has the size and alignment of a C type.
static bool matches(const type_t* s, uint64_t size, uint64_t align) {
    return type_sizeof(s) == size && type_alignof(s) == align;
}

TEST(rec_has_the_offsets_sizeof_and_alignof_of_its_c_mirror, {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[MAX_FIELDS];
    uint64_t offsets[MAX_FIELDS];
    fields[0] = tenv_type(&e, "u8");
    fields[1] = tenv_type(&e, "i32");
    fields[2] = tenv_type(&e, "u16");
    fields[3] = tenv_type(&e, "f64");
    const type_t* s = lay_out(&e, "rec_abi", fields, MAX_FIELDS, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct rec_mirror, tag));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct rec_mirror, n));
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)offsetof(struct rec_mirror, k));
    TEST_ASSERT_EQ_UINT64(offsets[3], (uint64_t)offsetof(struct rec_mirror, x));
    TEST_ASSERT_EQ_UINT64(type_sizeof(s), (uint64_t)sizeof(struct rec_mirror));
    TEST_ASSERT_EQ_UINT64(type_alignof(s), (uint64_t)alignof(struct rec_mirror));
    tenv_free(&e);
})

TEST(a_struct_whose_largest_member_is_first_keeps_its_trailing_padding, {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "i64");
    fields[1] = tenv_type(&e, "u8");
    const type_t* s = lay_out(&e, "tail_abi", fields, 2, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct tail_mirror, big));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct tail_mirror, tag));
    // The size passes the last field, because C rounds it up to the alignment
    // (D3.8): a copy that stopped at the last byte written would lose it.
    TEST_ASSERT_EQ_UINT64(type_sizeof(s), (uint64_t)sizeof(struct tail_mirror));
    TEST_ASSERT_EQ_UINT64(type_alignof(s), (uint64_t)alignof(struct tail_mirror));
    tenv_free(&e);
})

TEST(an_array_strides_by_the_padded_size_of_its_element_struct, {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "i32");
    fields[1] = tenv_type(&e, "u8");
    const type_t* small = lay_out(&e, "small_abi", fields, 2, offsets);
    TEST_ASSERT_TRUE(laid_out(small));
    TEST_ASSERT_EQ_UINT64(type_sizeof(small), (uint64_t)sizeof(struct small_mirror));
    // The fields occupy five bytes and the element strides by eight, which is
    // what an array of the mirror does (D3.4, D3.8).
    const type_t* array = type_array(&e.tt, small, 3);
    TEST_ASSERT_EQ_UINT64(type_sizeof(array), (uint64_t)sizeof(struct small_mirror[3]));
    TEST_ASSERT_EQ_UINT64(type_alignof(array), (uint64_t)alignof(struct small_mirror[3]));
    tenv_free(&e);
})

TEST(a_nested_struct_field_lands_where_c_puts_it, {
    tenv_t e;
    tenv_init(&e);
    const type_t* inner_fields[MAX_FIELDS];
    uint64_t inner_offsets[MAX_FIELDS];
    inner_fields[0] = tenv_type(&e, "u8");
    inner_fields[1] = tenv_type(&e, "i32");
    inner_fields[2] = tenv_type(&e, "u16");
    inner_fields[3] = tenv_type(&e, "f64");
    const type_t* inner = lay_out(&e, "rec_abi", inner_fields, MAX_FIELDS, inner_offsets);
    TEST_ASSERT_TRUE(laid_out(inner));
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = tenv_type(&e, "u8");
    fields[1] = inner;
    const type_t* s = lay_out(&e, "nest_abi", fields, 2, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct nest_mirror, tag));
    // The inner struct's alignment is the outer one's, so the field is pushed
    // past seven bytes of padding (D3.8).
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct nest_mirror, inner));
    TEST_ASSERT_TRUE(
        matches(s, (uint64_t)sizeof(struct nest_mirror), (uint64_t)alignof(struct nest_mirror)));
    tenv_free(&e);
})

TEST(a_struct_holding_an_array_of_structs_matches_c, {
    tenv_t e;
    tenv_init(&e);
    const type_t* small_fields[2];
    uint64_t small_offsets[2];
    small_fields[0] = tenv_type(&e, "i32");
    small_fields[1] = tenv_type(&e, "u8");
    const type_t* small = lay_out(&e, "small_abi", small_fields, 2, small_offsets);
    TEST_ASSERT_TRUE(laid_out(small));
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = type_array(&e.tt, small, 3);
    fields[1] = tenv_type(&e, "u8");
    const type_t* s = lay_out(&e, "grid_abi", fields, 2, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct grid_mirror, cells));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct grid_mirror, tag));
    TEST_ASSERT_TRUE(
        matches(s, (uint64_t)sizeof(struct grid_mirror), (uint64_t)alignof(struct grid_mirror)));
    tenv_free(&e);
})

TEST(a_bool_field_occupies_the_byte_c_gives_it, {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_type(&e, "bool");
    fields[1] = tenv_type(&e, "i32");
    fields[2] = tenv_type(&e, "bool");
    const type_t* s = lay_out(&e, "flags_abi", fields, 3, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    // `bool` is `i1` in a value and one byte in memory, as C's `_Bool` is
    // (D19.2), so it pads like a `u8` and never like an `i32`.
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct flags_mirror, on));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct flags_mirror, n));
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)offsetof(struct flags_mirror, off));
    TEST_ASSERT_TRUE(
        matches(s, (uint64_t)sizeof(struct flags_mirror), (uint64_t)alignof(struct flags_mirror)));
    tenv_free(&e);
})

TEST(a_span_and_a_string_field_are_one_two_word_header_each, {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_type(&e, "u8");
    fields[1] = tenv_type(&e, "string");
    fields[2] = tenv_type(&e, "i32@");
    const type_t* s = lay_out(&e, "spanful_abi", fields, 3, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    // A span or `string` is one hidden pointer and is never split into two
    // scalars (D9.9), so each field is one `struct fort_span`.
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct spanful_mirror, tag));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct spanful_mirror, name));
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)offsetof(struct spanful_mirror, vals));
    TEST_ASSERT_TRUE(matches(
        s, (uint64_t)sizeof(struct spanful_mirror), (uint64_t)alignof(struct spanful_mirror)));
    tenv_free(&e);
})

TEST(pointer_and_function_pointer_fields_match_c_pointers, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_type(&e, "node*");
    fields[1] = tenv_fn(&e, i32, i32, NULL);
    fields[2] = tenv_type(&e, "void*");
    const type_t* s = lay_out(&e, "refs_abi", fields, 3, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct refs_mirror, node));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct refs_mirror, fn));
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)offsetof(struct refs_mirror, opaque));
    TEST_ASSERT_TRUE(
        matches(s, (uint64_t)sizeof(struct refs_mirror), (uint64_t)alignof(struct refs_mirror)));
    tenv_free(&e);
})

TEST(an_enum_field_is_four_bytes_wide, {
    tenv_t e;
    tenv_init(&e);
    // An enum is `i32` and 4 bytes (D3.9), so a field of one pads like an
    // `i32`: laid out as anything wider it would move the field after it and
    // the struct's size with it.
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_type(&e, "u8");
    fields[1] = e.color;
    fields[2] = tenv_type(&e, "i64");
    const type_t* s = lay_out(&e, "tagged_abi", fields, 3, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct tagged_mirror, tag));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct tagged_mirror, colour));
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)offsetof(struct tagged_mirror, big));
    TEST_ASSERT_TRUE(matches(
        s, (uint64_t)sizeof(struct tagged_mirror), (uint64_t)alignof(struct tagged_mirror)));
    tenv_free(&e);
})

TEST(char_and_f32_fields_match_their_c_spellings, {
    tenv_t e;
    tenv_init(&e);
    // `char` is an unsigned byte and `f32` is C's `float` (D3.1, D3.2), so
    // the `f32` is 4-aligned and the byte after it is not.
    const type_t* fields[3];
    uint64_t offsets[3];
    fields[0] = tenv_type(&e, "char");
    fields[1] = tenv_type(&e, "f32");
    fields[2] = tenv_type(&e, "char");
    const type_t* s = lay_out(&e, "narrow_abi", fields, 3, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct narrow_mirror, first));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct narrow_mirror, weight));
    TEST_ASSERT_EQ_UINT64(offsets[2], (uint64_t)offsetof(struct narrow_mirror, last));
    TEST_ASSERT_TRUE(matches(
        s, (uint64_t)sizeof(struct narrow_mirror), (uint64_t)alignof(struct narrow_mirror)));
    tenv_free(&e);
})

TEST(a_struct_of_one_field_is_that_field, {
    tenv_t e;
    tenv_init(&e);
    const type_t* fields[1];
    uint64_t offsets[1];
    fields[0] = tenv_type(&e, "i64");
    const type_t* s = lay_out(&e, "one_abi", fields, 1, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct one_mirror, only));
    TEST_ASSERT_TRUE(
        matches(s, (uint64_t)sizeof(struct one_mirror), (uint64_t)alignof(struct one_mirror)));
    tenv_free(&e);
})

TEST(a_field_after_a_nested_struct_stands_past_its_trailing_padding, {
    tenv_t e;
    tenv_init(&e);
    // The C divergence a layout written by hand falls into: the inner struct
    // occupies five bytes of fields and eight of storage, and the field after
    // it stands at 8, because a struct's size carries its padding with it
    // (D3.8).
    const type_t* inner_fields[2];
    uint64_t inner_offsets[2];
    inner_fields[0] = tenv_type(&e, "i32");
    inner_fields[1] = tenv_type(&e, "u8");
    const type_t* inner = lay_out(&e, "inner_abi", inner_fields, 2, inner_offsets);
    TEST_ASSERT_TRUE(laid_out(inner));
    const type_t* fields[2];
    uint64_t offsets[2];
    fields[0] = inner;
    fields[1] = tenv_type(&e, "u8");
    const type_t* s = lay_out(&e, "after_abi", fields, 2, offsets);
    TEST_ASSERT_TRUE(laid_out(s));
    TEST_ASSERT_EQ_UINT64(offsets[0], (uint64_t)offsetof(struct after_mirror, inner));
    TEST_ASSERT_EQ_UINT64(offsets[1], (uint64_t)offsetof(struct after_mirror, tag));
    TEST_ASSERT_TRUE(
        matches(s, (uint64_t)sizeof(struct after_mirror), (uint64_t)alignof(struct after_mirror)));
    tenv_free(&e);
})

TEST(an_owning_field_changes_no_offset, {
    tenv_t e;
    tenv_init(&e);
    // `own` is a compile-time mark and no part of the representation (D17.1),
    // so the owning form of a struct has the layout of the plain one.
    const type_t* plain_fields[2];
    uint64_t plain_offsets[2];
    plain_fields[0] = tenv_type(&e, "u8");
    plain_fields[1] = tenv_type(&e, "string");
    const type_t* plain = lay_out(&e, "plain_abi", plain_fields, 2, plain_offsets);
    TEST_ASSERT_TRUE(laid_out(plain));
    const type_t* owning_fields[2];
    uint64_t owning_offsets[2];
    owning_fields[0] = tenv_type(&e, "u8");
    owning_fields[1] = tenv_type(&e, "string own");
    const type_t* owning = lay_out(&e, "owning_abi", owning_fields, 2, owning_offsets);
    TEST_ASSERT_TRUE(laid_out(owning));
    TEST_ASSERT_EQ_UINT64(plain_offsets[1], (uint64_t)offsetof(struct plain_mirror, s));
    TEST_ASSERT_EQ_UINT64(owning_offsets[1], plain_offsets[1]);
    TEST_ASSERT_TRUE(matches(
        plain, (uint64_t)sizeof(struct plain_mirror), (uint64_t)alignof(struct plain_mirror)));
    TEST_ASSERT_TRUE(matches(
        owning, (uint64_t)sizeof(struct plain_mirror), (uint64_t)alignof(struct plain_mirror)));
    TEST_ASSERT_TRUE(type_is_owning_aggregate(owning));
    TEST_ASSERT_FALSE(type_is_owning_aggregate(plain));
    tenv_free(&e);
})

int main(int argc, char** argv) {
    TEST_INIT("types_abi", argc, argv);
    TEST_RUN(rec_has_the_offsets_sizeof_and_alignof_of_its_c_mirror);
    TEST_RUN(a_struct_whose_largest_member_is_first_keeps_its_trailing_padding);
    TEST_RUN(an_array_strides_by_the_padded_size_of_its_element_struct);
    TEST_RUN(a_nested_struct_field_lands_where_c_puts_it);
    TEST_RUN(a_struct_holding_an_array_of_structs_matches_c);
    TEST_RUN(a_bool_field_occupies_the_byte_c_gives_it);
    TEST_RUN(a_span_and_a_string_field_are_one_two_word_header_each);
    TEST_RUN(pointer_and_function_pointer_fields_match_c_pointers);
    TEST_RUN(an_enum_field_is_four_bytes_wide);
    TEST_RUN(char_and_f32_fields_match_their_c_spellings);
    TEST_RUN(a_struct_of_one_field_is_that_field);
    TEST_RUN(a_field_after_a_nested_struct_stands_past_its_trailing_padding);
    TEST_RUN(an_owning_field_changes_no_offset);
    TEST_EXIT();
}
