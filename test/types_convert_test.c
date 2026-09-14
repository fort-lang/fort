// The conversions of a type: the implicit drops of mutability and of ownership,
// and the cast matrix. Every example those rules give is one assertion, with the
// further rows the rules imply, and the last four tests re-derive the matrix and
// the invariants that tie the two together.
// D5.4, D17.4, D3.14
//
// The representation, identity and levels of a type are tested in
// types_test.c, its sizes and layout in types_layout_test.c, the builder and
// the spelling in types_build_test.c.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "prim.h"
#include "str.h"
#include "types.h"
#include "types_helpers.h"

#include "test.h"

// Whether a value of the type spelled `src` may be used where the type spelled
// `dst` is expected.
// D5.4, D17.4
static bool assignable(tenv_t* e, const char* dst, const char* src) {
    const type_t* d = tenv_type(e, dst);
    const type_t* s = tenv_type(e, src);
    return type_assignable(d, s);
}

// Whether `cast(expr, dst)` is allowed on an expression of type `src`.
// D3.14
static bool castable(tenv_t* e, const char* dst, const char* src) {
    const type_t* d = tenv_type(e, dst);
    const type_t* s = tenv_type(e, src);
    return type_cast_allowed(d, s);
}

// The class of a scalar type in the cast matrix.
// D3.14
typedef enum {
    CLASS_INT,
    CLASS_FLOAT,
    CLASS_BOOL,
    CLASS_CHAR,
    CLASS_ENUM,
} scalar_class_t;

// The matrix rows for scalars, re-derived from the table rather than from the
// implementation: identity always; integers and floats convert both ways; an
// integer also becomes a `char` or an enum; `bool`, `char` and an enum become
// an integer and nothing else.
static bool matrix_allows(scalar_class_t dst, scalar_class_t src, bool identical) {
    if (identical) {
        return true;
    }
    if (src == CLASS_INT || src == CLASS_FLOAT) {
        if (dst == CLASS_INT || dst == CLASS_FLOAT) {
            return true;
        }
        return src == CLASS_INT && (dst == CLASS_CHAR || dst == CLASS_ENUM);
    }
    return dst == CLASS_INT;
}

// The scalar types of the matrix, with their class.
enum { SCALARS_MAX = 14 };

static uint32_t scalar_types(tenv_t* e, const type_t** types, scalar_class_t* classes) {
    uint32_t n = 0;
    for (int i = 0; i < PRIM_COUNT; i++) {
        const prim_kind_t k = (prim_kind_t)i;
        if (k == PRIM_VOID) {
            continue;
        }
        types[n] = type_prim(&e->tt, k);
        if (prim_is_integer(k)) {
            classes[n] = CLASS_INT;
        } else if (prim_is_float(k)) {
            classes[n] = CLASS_FLOAT;
        } else {
            classes[n] = k == PRIM_BOOL ? CLASS_BOOL : CLASS_CHAR;
        }
        n++;
    }
    types[n] = e->color;
    classes[n] = CLASS_ENUM;
    n++;
    types[n] = e->shape;
    classes[n] = CLASS_ENUM;
    n++;
    return n;
}

// The reference and value types the invariant tests walk, function types and
// references to them included: the marks inside a function type are part of its
// identity, so a property that never sees one cannot tell whether a conversion
// respects them.
// D3.10
enum { SAMPLES_MAX = 32 };

static uint32_t sample_types(tenv_t* e, const type_t** types) {
    static const char* const SPELLINGS[] = {
        "i32",
        "u8",
        "bool",
        "char",
        "f64",
        "node*",
        "node mut*",
        "node mut* own",
        "node**",
        "node* mut*",
        "node*@",
        "node mut* mut@",
        "node mut* own mut@ own",
        "node* own@",
        "node* own[4]",
        "i32@",
        "i32 mut@",
        "i32[4]",
        "point",
        "string",
        "string own",
        "void*",
        "void* own",
        "void mut*",
        "void mut* own",
        "u8 mut@ own mut*",
    };
    uint32_t n = (uint32_t)(sizeof SPELLINGS / sizeof SPELLINGS[0]);
    for (uint32_t i = 0; i < n; i++) {
        types[i] = tenv_type(e, SPELLINGS[i]);
    }
    const type_t* v = type_void(&e->tt);
    const type_t* takes_node = tenv_fn(e, v, tenv_type(e, "node*"), NULL);
    const type_t* takes_mut = tenv_fn(e, v, tenv_type(e, "node mut*"), NULL);
    types[n++] = takes_node;
    types[n++] = takes_mut;
    types[n++] = tenv_fn(e, tenv_type(e, "i32"), tenv_type(e, "i32"), NULL);
    types[n++] = type_span(&e->tt, takes_node, false, false);
    types[n++] = type_span(&e->tt, takes_mut, false, false);
    types[n++] = type_ptr(&e->tt, takes_node, false, false);
    return n;
}

// ---- dropping mutability ---------------------------------------------------------
// D5.4

TEST(the_d5_4_conversions_and_their_shape, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(assignable(&e, "node*", "node mut*"));
    TEST_ASSERT_TRUE(assignable(&e, "node* mut", "node mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "node mut*", "node*"));
    TEST_ASSERT_TRUE(assignable(&e, "node**", "node mut* mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "node* mut*", "node mut* mut*"));
    TEST_ASSERT_TRUE(assignable(&e, "node*@", "node mut* mut@"));
    TEST_ASSERT_TRUE(assignable(&e, "node mut*@", "node mut* mut@"));
    TEST_ASSERT_FALSE(assignable(&e, "node* mut@", "node mut* mut@"));
    TEST_ASSERT_TRUE(assignable(&e, "i32@@ mut", "i32 mut@ mut@"));
    TEST_ASSERT_FALSE(assignable(&e, "i32@ mut@", "i32 mut@ mut@"));
    TEST_ASSERT_TRUE(assignable(&e, "i32@", "i32 mut@"));
    TEST_ASSERT_FALSE(assignable(&e, "i32 mut@", "i32@"));
    // The storage a `void*` reaches is a level like any other: dropping its
    // `mut` is the one implicit conversion, and adding it needs a cast.
    // D3.11, D5.4
    TEST_ASSERT_TRUE(assignable(&e, "void*", "void mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "void mut*", "void*"));
    TEST_ASSERT_TRUE(assignable(&e, "void* mut", "void mut*"));
    TEST_ASSERT_TRUE(assignable(&e, "void mut* mut", "void mut*"));
    // The C `T** -> const T**` hole is closed here too: level 1 of the target
    // must be immutable before level 2 may be dropped.
    // D5.4
    TEST_ASSERT_TRUE(assignable(&e, "void**", "void mut* mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "void* mut*", "void mut* mut*"));
    TEST_ASSERT_TRUE(assignable(&e, "void*@", "void mut* mut@"));
    TEST_ASSERT_TRUE(assignable(&e, "void mut*@", "void mut* mut@"));
    TEST_ASSERT_FALSE(assignable(&e, "void* mut@", "void mut* mut@"));
    tenv_free(&e);
})

TEST(mutability_drops_through_further_shapes, {
    tenv_t e;
    tenv_init(&e);
    // Level 0 of the receiving binding is unconstrained in both directions.
    TEST_ASSERT_TRUE(assignable(&e, "node*", "node* mut"));
    TEST_ASSERT_TRUE(assignable(&e, "node* mut", "node*"));
    // A fixed array adds no level, so its elements follow the same rule.
    TEST_ASSERT_TRUE(assignable(&e, "node*[4]", "node mut*[4]"));
    TEST_ASSERT_FALSE(assignable(&e, "node mut*[4]", "node*[4]"));
    TEST_ASSERT_FALSE(assignable(&e, "node*[4]", "node*[8]"));
    TEST_ASSERT_TRUE(assignable(&e, "i32[4]*", "i32[4] mut*"));
    // Identity always converts.
    TEST_ASSERT_TRUE(assignable(&e, "u8 mut@ own mut*", "u8 mut@ own mut*"));
    TEST_ASSERT_TRUE(assignable(&e, "string", "string"));
    TEST_ASSERT_TRUE(assignable(&e, "point", "point"));
    TEST_ASSERT_TRUE(assignable(&e, "color", "color"));
    TEST_ASSERT_FALSE(assignable(&e, "point", "node"));
    TEST_ASSERT_FALSE(assignable(&e, "color", "shape"));
    TEST_ASSERT_FALSE(assignable(&e, "i64", "i32"));
    TEST_ASSERT_FALSE(assignable(&e, "f64", "i32"));
    TEST_ASSERT_FALSE(assignable(&e, "i32@", "i32[4]"));
    tenv_free(&e);
})

// ---- lending ---------------------------------------------------------------------
// D17.4

TEST(the_d17_4_conversions_and_their_shape, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(assignable(&e, "u8 mut@", "u8 mut@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "u8@", "u8 mut@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "node*", "node* own"));
    TEST_ASSERT_FALSE(assignable(&e, "node* own", "node*"));
    TEST_ASSERT_TRUE(assignable(&e, "node mut* own mut@", "node mut* own mut@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "node*@", "node mut* own mut@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "node* own@ own", "node mut* own mut@ own"));
    TEST_ASSERT_FALSE(assignable(&e, "node mut* mut@ own", "node mut* own mut@ own"));
    TEST_ASSERT_FALSE(assignable(&e, "node mut* mut@", "node mut* own mut@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "u8@*", "u8 mut@ own mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "u8 mut@ mut*", "u8 mut@ own mut*"));
    TEST_ASSERT_TRUE(assignable(&e, "string", "string own"));
    tenv_free(&e);
})

TEST(ownership_is_never_added_implicitly, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_FALSE(assignable(&e, "string own", "string"));
    TEST_ASSERT_FALSE(assignable(&e, "void* own", "void*"));
    TEST_ASSERT_FALSE(assignable(&e, "void mut* own", "void mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "u8@ own", "u8@"));
    TEST_ASSERT_FALSE(assignable(&e, "node* own@ own", "node mut* mut@ own"));
    TEST_ASSERT_FALSE(assignable(&e, "node* own@", "node*@"));
    // Dropping `own` and mutability together is one conversion.
    TEST_ASSERT_TRUE(assignable(&e, "void*", "void* own"));
    // An owned writable allocation lends as a plain view; the reverse adds a
    // mark in each direction.
    TEST_ASSERT_TRUE(assignable(&e, "void*", "void mut* own"));
    TEST_ASSERT_FALSE(assignable(&e, "void mut*", "void* own"));
    TEST_ASSERT_TRUE(assignable(&e, "node* own@", "node* own@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "u8@", "u8@ own"));
    tenv_free(&e);
})

TEST(assignability_of_null_void_and_the_error_type, {
    tenv_t e;
    tenv_init(&e);
    const type_t* null = type_null(&e.tt);
    const type_t* err = type_error(&e.tt);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    // `null` adopts a pointer, `void*` or function type.
    // D10.5
    TEST_ASSERT_TRUE(type_assignable(tenv_type(&e, "node*"), null));
    TEST_ASSERT_TRUE(type_assignable(tenv_type(&e, "node mut* own"), null));
    TEST_ASSERT_TRUE(type_assignable(tenv_type(&e, "void*"), null));
    TEST_ASSERT_TRUE(type_assignable(tenv_fn(&e, i32, i32, NULL), null));
    // Not a span, a string or a value type: the zero value is `{}`.
    // D3.5
    TEST_ASSERT_FALSE(type_assignable(tenv_type(&e, "i32@"), null));
    TEST_ASSERT_FALSE(type_assignable(tenv_type(&e, "string"), null));
    TEST_ASSERT_FALSE(type_assignable(tenv_type(&e, "i32"), null));
    TEST_ASSERT_FALSE(type_assignable(null, tenv_type(&e, "node*")));
    // A poisoned type converts both ways, so one error is reported once.
    TEST_ASSERT_TRUE(type_assignable(err, tenv_type(&e, "i32")));
    TEST_ASSERT_TRUE(type_assignable(tenv_type(&e, "i32"), err));
    // No value has type `void`.
    TEST_ASSERT_FALSE(type_assignable(type_void(&e.tt), type_void(&e.tt)));
    TEST_ASSERT_FALSE(type_assignable(tenv_type(&e, "i32"), type_void(&e.tt)));
    tenv_free(&e);
})

TEST(function_types_convert_only_to_themselves, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* v = type_void(&e.tt);
    TEST_ASSERT_TRUE(type_assignable(tenv_fn(&e, i32, i32, NULL), tenv_fn(&e, i32, i32, NULL)));
    TEST_ASSERT_FALSE(type_assignable(tenv_fn(&e, i32, i32, NULL), tenv_fn(&e, v, i32, NULL)));
    // No variance on a parameter's pointee mutability or `own`.
    // D3.10
    const type_t* takes_node = tenv_fn(&e, v, tenv_type(&e, "node*"), NULL);
    const type_t* takes_mut = tenv_fn(&e, v, tenv_type(&e, "node mut*"), NULL);
    const type_t* takes_own = tenv_fn(&e, v, tenv_type(&e, "node* own"), NULL);
    TEST_ASSERT_FALSE(type_assignable(takes_node, takes_mut));
    TEST_ASSERT_FALSE(type_assignable(takes_mut, takes_node));
    TEST_ASSERT_FALSE(type_assignable(takes_node, takes_own));
    TEST_ASSERT_FALSE(type_assignable(takes_own, takes_node));
    tenv_free(&e);
})

// ---- casts -----------------------------------------------------------------------
// D3.14

TEST(casts_among_numbers_bool_char_and_enums, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(castable(&e, "i64", "i32")); // widening
    TEST_ASSERT_TRUE(castable(&e, "i32", "i64")); // narrowing
    TEST_ASSERT_TRUE(castable(&e, "u32", "i32")); // same width, other sign
    TEST_ASSERT_TRUE(castable(&e, "f64", "i32")); // integer to float
    TEST_ASSERT_TRUE(castable(&e, "i32", "f64")); // float to integer
    TEST_ASSERT_TRUE(castable(&e, "f64", "f32")); // float to float
    TEST_ASSERT_TRUE(castable(&e, "f32", "f64"));
    TEST_ASSERT_TRUE(castable(&e, "i32", "bool")); // bool to integer
    TEST_ASSERT_TRUE(castable(&e, "i32", "char")); // char to and from integer
    TEST_ASSERT_TRUE(castable(&e, "char", "i32"));
    TEST_ASSERT_TRUE(castable(&e, "u8", "char"));
    TEST_ASSERT_TRUE(type_cast_allowed(type_prim(&e.tt, PRIM_I32), e.color));
    TEST_ASSERT_TRUE(type_cast_allowed(e.color, type_prim(&e.tt, PRIM_U8)));
    TEST_ASSERT_TRUE(type_cast_allowed(e.color, e.color));
    tenv_free(&e);
})

TEST(casts_refused_among_scalars, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_FALSE(castable(&e, "bool", "i32"));         // integer to bool
    TEST_ASSERT_FALSE(castable(&e, "bool", "char"));        // char to bool
    TEST_ASSERT_FALSE(castable(&e, "f64", "char"));         // char to float
    TEST_ASSERT_FALSE(castable(&e, "char", "f64"));         // float to char
    TEST_ASSERT_FALSE(castable(&e, "bool", "f64"));         // float to bool
    TEST_ASSERT_FALSE(castable(&e, "f64", "bool"));         // bool to float
    TEST_ASSERT_FALSE(castable(&e, "char", "bool"));        // bool to char
    TEST_ASSERT_FALSE(type_cast_allowed(e.color, e.shape)); // enum to enum
    TEST_ASSERT_FALSE(type_cast_allowed(e.color, type_prim(&e.tt, PRIM_BOOL)));
    TEST_ASSERT_FALSE(type_cast_allowed(e.color, type_prim(&e.tt, PRIM_F64)));
    TEST_ASSERT_FALSE(type_cast_allowed(e.color, type_prim(&e.tt, PRIM_CHAR)));
    TEST_ASSERT_FALSE(type_cast_allowed(type_prim(&e.tt, PRIM_BOOL), e.color));
    TEST_ASSERT_FALSE(type_cast_allowed(type_prim(&e.tt, PRIM_F64), e.color));
    tenv_free(&e);
})

TEST(casts_among_pointers_and_u64, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(castable(&e, "point*", "node*")); // any pointer to any pointer
    // D17.3: the source already carries the `mut` the target keeps
    TEST_ASSERT_TRUE(castable(&e, "node mut* own", "node mut*")); // adopts
    TEST_ASSERT_TRUE(castable(&e, "node*", "node mut* own"));     // lends
    TEST_ASSERT_TRUE(castable(&e, "void*", "node*"));
    TEST_ASSERT_TRUE(castable(&e, "node*", "void*"));
    TEST_ASSERT_TRUE(castable(&e, "void* own", "void*"));
    // A `mut` the source carries reaches the level a `void*` names, in both
    // directions, and a cast that only drops it is allowed as well.
    // D3.14
    TEST_ASSERT_TRUE(castable(&e, "void mut*", "node mut*"));
    TEST_ASSERT_TRUE(castable(&e, "node mut*", "void mut*"));
    TEST_ASSERT_TRUE(castable(&e, "void*", "void mut*"));
    TEST_ASSERT_TRUE(castable(&e, "node*", "node mut*"));
    TEST_ASSERT_TRUE(castable(&e, "u64", "node*"));
    TEST_ASSERT_TRUE(castable(&e, "node*", "u64"));
    TEST_ASSERT_TRUE(castable(&e, "u64", "void*"));
    TEST_ASSERT_TRUE(castable(&e, "void*", "u64"));
    TEST_ASSERT_TRUE(castable(&e, "void* own", "u64"));
    TEST_ASSERT_TRUE(castable(&e, "node**", "node* mut*"));
    // Pointers cast to no other integer type, and never to a span.
    TEST_ASSERT_FALSE(castable(&e, "i64", "node*"));
    TEST_ASSERT_FALSE(castable(&e, "i32", "node*"));
    TEST_ASSERT_FALSE(castable(&e, "u32", "void*"));
    TEST_ASSERT_FALSE(castable(&e, "i32@", "i32*"));
    TEST_ASSERT_FALSE(castable(&e, "i32*", "i32@"));
    TEST_ASSERT_FALSE(castable(&e, "i64", "i32@"));
    // No pointer cast adds the `mut`, from a typed pointer, from a `void*` or
    // from the address in a `u64`.
    // D3.14
    TEST_ASSERT_FALSE(castable(&e, "node mut*", "node*"));
    TEST_ASSERT_FALSE(castable(&e, "node mut* own", "node*"));
    TEST_ASSERT_FALSE(castable(&e, "void mut*", "void*"));
    TEST_ASSERT_FALSE(castable(&e, "void mut*", "node*"));
    TEST_ASSERT_FALSE(castable(&e, "node mut*", "void*"));
    TEST_ASSERT_FALSE(castable(&e, "void mut* own", "u64"));
    TEST_ASSERT_FALSE(castable(&e, "node mut*", "u64"));
    tenv_free(&e);
})

TEST(casts_of_function_pointers_go_through_voidptr, {
    tenv_t e;
    tenv_init(&e);
    const type_t* i32 = type_prim(&e.tt, PRIM_I32);
    const type_t* i64 = type_prim(&e.tt, PRIM_I64);
    const type_t* f = tenv_fn(&e, i32, i32, NULL);
    const type_t* g = tenv_fn(&e, i64, i32, NULL);
    const type_t* vp = tenv_type(&e, "void*");
    TEST_ASSERT_TRUE(type_cast_allowed(vp, f));
    TEST_ASSERT_TRUE(type_cast_allowed(f, vp));
    TEST_ASSERT_TRUE(type_cast_allowed(f, f));
    TEST_ASSERT_FALSE(type_cast_allowed(g, f));
    TEST_ASSERT_FALSE(type_cast_allowed(type_prim(&e.tt, PRIM_U64), f));
    TEST_ASSERT_FALSE(type_cast_allowed(f, type_prim(&e.tt, PRIM_U64)));
    TEST_ASSERT_FALSE(type_cast_allowed(f, tenv_type(&e, "node*")));
    TEST_ASSERT_FALSE(type_cast_allowed(tenv_type(&e, "node*"), f));
    tenv_free(&e);
})

TEST(casts_within_the_string_family, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(castable(&e, "char@", "string"));
    TEST_ASSERT_TRUE(castable(&e, "u8@", "string"));
    TEST_ASSERT_TRUE(castable(&e, "string", "char@"));
    TEST_ASSERT_TRUE(castable(&e, "string", "u8@"));
    TEST_ASSERT_TRUE(castable(&e, "u8@", "char@"));
    TEST_ASSERT_TRUE(castable(&e, "char@", "u8@"));
    TEST_ASSERT_TRUE(castable(&e, "string", "u8 mut@"));
    TEST_ASSERT_TRUE(castable(&e, "u8@", "u8 mut@"));
    TEST_ASSERT_TRUE(castable(&e, "char mut@", "u8 mut@"));
    // Adoption and lending are both casts.
    // D17.3, D17.12
    TEST_ASSERT_TRUE(castable(&e, "string own", "u8 mut@ own"));
    TEST_ASSERT_TRUE(castable(&e, "string", "u8 mut@ own"));
    TEST_ASSERT_TRUE(castable(&e, "string own", "string"));
    TEST_ASSERT_TRUE(castable(&e, "string", "string own"));
    // The element type of any other span never changes.
    TEST_ASSERT_FALSE(castable(&e, "i8@", "u8@"));
    TEST_ASSERT_FALSE(castable(&e, "string", "i8@"));
    TEST_ASSERT_FALSE(castable(&e, "i32@", "u32@"));
    // The bytes of a `string` are immutable, and no member of the family adds
    // the `mut` back, so the two casts of `cast(cast(b, string), u8 mut@)`
    // launder nothing.
    // D3.7, D3.14
    TEST_ASSERT_FALSE(castable(&e, "char mut@", "string"));
    TEST_ASSERT_FALSE(castable(&e, "u8 mut@", "string"));
    TEST_ASSERT_FALSE(castable(&e, "u8 mut@", "u8@"));
    TEST_ASSERT_FALSE(castable(&e, "char mut@", "u8@"));
    TEST_ASSERT_FALSE(castable(&e, "u8 mut@ own", "string"));
    tenv_free(&e);
})

TEST(a_span_cast_never_launders_a_signature_or_an_owner, {
    tenv_t e;
    tenv_init(&e);
    const type_t* v = type_void(&e.tt);
    const type_t* takes_node = tenv_fn(&e, v, tenv_type(&e, "node*"), NULL);
    const type_t* takes_mut = tenv_fn(&e, v, tenv_type(&e, "node mut*"), NULL);
    const type_t* of_node = type_span(&e.tt, takes_node, false, false);
    const type_t* of_mut = type_span(&e.tt, takes_mut, false, false);
    // The marks of a parameter are part of the function type's identity, so those
    // spans have different element types and the row for marks does not reach
    // them, exactly as the cast on one element does not.
    // D3.10, D3.14
    TEST_ASSERT_FALSE(type_cast_allowed(of_node, of_mut));
    TEST_ASSERT_FALSE(type_cast_allowed(of_mut, of_node));
    TEST_ASSERT_FALSE(type_cast_allowed(takes_node, takes_mut));
    TEST_ASSERT_FALSE(type_same_shape(of_node, of_mut));
    TEST_ASSERT_TRUE(type_cast_allowed(of_node, of_node));
    // A span of function pointers still casts where its own marks differ, so
    // long as the marks are not added: `own` may be, `mut` may not.
    // D3.14
    TEST_ASSERT_TRUE(type_cast_allowed(type_span(&e.tt, takes_node, true, false), of_node));
    TEST_ASSERT_FALSE(type_cast_allowed(type_span(&e.tt, takes_node, true, true), of_node));
    TEST_ASSERT_TRUE(type_cast_allowed(of_node, type_span(&e.tt, takes_node, false, true)));
    // Dropping an `own` under a reference the target still owns would leave its
    // objects owned by nobody, which no cast licenses.
    // D17.4
    TEST_ASSERT_FALSE(castable(&e, "node mut* mut@ own", "node mut* own mut@ own"));
    TEST_ASSERT_FALSE(castable(&e, "node* mut@ own", "node* own@ own"));
    TEST_ASSERT_FALSE(castable(&e, "u8@ mut@ own", "u8@ own@ own"));
    // Dropping it with no owner outside is lending, which a cast may do even
    // where the implicit rule refuses.
    // D3.14
    TEST_ASSERT_TRUE(castable(&e, "node mut* mut@", "node mut* own mut@ own"));
    TEST_ASSERT_TRUE(castable(&e, "node*@", "node mut* own mut@ own"));
    TEST_ASSERT_TRUE(castable(&e, "node mut* own mut@", "node mut* own mut@ own"));
    tenv_free(&e);
})

TEST(casts_of_spans_change_only_the_marks, {
    tenv_t e;
    tenv_init(&e);
    // The element type never changes; mutability is dropped at any level, and
    // `own` is added or dropped at any reference.
    // D3.14
    TEST_ASSERT_TRUE(castable(&e, "node*@", "node* mut@"));
    TEST_ASSERT_TRUE(castable(&e, "i32@@ mut", "i32 mut@ mut@"));
    TEST_ASSERT_TRUE(castable(&e, "u8@", "u8@ own"));
    TEST_ASSERT_TRUE(castable(&e, "i32@", "i32 mut@"));
    TEST_ASSERT_TRUE(castable(&e, "node mut* mut@", "node mut* own mut@ own"));
    // The route a caller of `std.sort_ptr` takes: the drop at level 2 behind a
    // mutable level 1, which the implicit conversion refuses and the cast makes.
    // D5.4, D3.14
    TEST_ASSERT_TRUE(castable(&e, "node* mut@", "node mut* mut@"));
    TEST_ASSERT_FALSE(assignable(&e, "node* mut@", "node mut* mut@"));
    TEST_ASSERT_FALSE(castable(&e, "node*@", "point*@"));
    TEST_ASSERT_FALSE(castable(&e, "i32@@", "i32@"));
    // A span adds no `mut`, at the slots or at any level below them.
    // D3.14
    TEST_ASSERT_FALSE(castable(&e, "node* mut@", "node*@"));
    TEST_ASSERT_FALSE(castable(&e, "node mut* mut@", "node* mut@"));
    TEST_ASSERT_FALSE(castable(&e, "i32 mut@ mut@", "i32@ mut@"));
    TEST_ASSERT_FALSE(castable(&e, "i32 mut@", "i32@"));
    TEST_ASSERT_FALSE(castable(&e, "i32 mut@ own", "i32@"));
    TEST_ASSERT_FALSE(castable(&e, "node mut* own mut@", "node*@"));
    tenv_free(&e);
})

TEST(a_cast_never_adds_mut, {
    tenv_t e;
    tenv_init(&e);
    // One rule over every row of the matrix: the target marks a level `mut`
    // only where the source marks the level at the same depth `mut` too.
    // D3.14
    TEST_ASSERT_TRUE(type_cast_adds_mut(tenv_type(&e, "node mut*"), tenv_type(&e, "node*")));
    TEST_ASSERT_FALSE(type_cast_adds_mut(tenv_type(&e, "node*"), tenv_type(&e, "node mut*")));
    TEST_ASSERT_FALSE(type_cast_adds_mut(tenv_type(&e, "node mut*"), tenv_type(&e, "node mut*")));
    // A level below the outermost is a level like any other, and the drop at
    // that level stays legal.
    TEST_ASSERT_FALSE(castable(&e, "node mut* mut*", "node* mut*"));
    TEST_ASSERT_TRUE(castable(&e, "node* mut*", "node mut* mut*"));
    TEST_ASSERT_FALSE(castable(&e, "node mut* mut@", "node* mut@"));
    TEST_ASSERT_TRUE(castable(&e, "node* mut@", "node mut* mut@"));
    // `void*` is a source like any other, so the two casts of
    // `cast(cast(p, void*), u8 mut*)` refuse the second one. A `void mut*`
    // carries the mark and the same cast compiles.
    // D3.11, D3.14
    TEST_ASSERT_FALSE(castable(&e, "u8 mut*", "void*"));
    TEST_ASSERT_TRUE(castable(&e, "u8 mut*", "void mut*"));
    TEST_ASSERT_TRUE(castable(&e, "u8*", "void*"));
    // An integer, a `string` and a function pointer name no level at all, so a
    // target that marks one adds it.
    TEST_ASSERT_FALSE(castable(&e, "u8 mut*", "u64"));
    TEST_ASSERT_TRUE(castable(&e, "u8*", "u64"));
    TEST_ASSERT_FALSE(castable(&e, "u8 mut@", "string"));
    TEST_ASSERT_TRUE(castable(&e, "u8@", "string"));
    TEST_ASSERT_TRUE(
        type_cast_adds_mut(tenv_type(&e, "void mut*"), tenv_fn(&e, type_void(&e.tt), NULL, NULL)));
    // Where the pointee type diverges, the source says nothing about the levels
    // below the divergence, so the target marks none of them.
    TEST_ASSERT_FALSE(castable(&e, "node mut* mut*", "u8 mut*"));
    TEST_ASSERT_TRUE(castable(&e, "node* mut*", "u8 mut*"));
    TEST_ASSERT_TRUE(castable(&e, "u8 mut*", "node mut* mut*"));
    // A fixed array adds no level of its own: the elements are compared.
    // D5.2
    TEST_ASSERT_FALSE(castable(&e, "node mut*[2]", "node*[2]"));
    TEST_ASSERT_TRUE(castable(&e, "node*[2]", "node mut*[2]"));
    tenv_free(&e);
})

TEST(casts_refused_for_structs_and_fixed_arrays, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(type_cast_allowed(e.point, e.point)); // identity
    TEST_ASSERT_FALSE(type_cast_allowed(e.point, e.node));
    TEST_ASSERT_FALSE(type_cast_allowed(type_prim(&e.tt, PRIM_U64), e.point));
    TEST_ASSERT_FALSE(type_cast_allowed(e.point, type_prim(&e.tt, PRIM_U64)));
    TEST_ASSERT_TRUE(castable(&e, "i32[4]", "i32[4]"));
    TEST_ASSERT_FALSE(castable(&e, "i32@", "i32[4]"));
    TEST_ASSERT_FALSE(castable(&e, "i32[4]", "i32@"));
    TEST_ASSERT_FALSE(castable(&e, "i32[4]", "i32[8]"));
    TEST_ASSERT_FALSE(castable(&e, "u64", "i32[4]"));
    tenv_free(&e);
})

TEST(casts_between_a_reference_and_a_fat_pointer_are_refused, {
    tenv_t e;
    tenv_init(&e);
    // A span is `{ptr, len}` and a pointer is an address: neither reaches the
    // other, and a span expression or `.ptr` is the way across.
    // D3.5, D3.14
    TEST_ASSERT_FALSE(castable(&e, "u8@", "u8*"));
    TEST_ASSERT_FALSE(castable(&e, "u8*", "u8@"));
    TEST_ASSERT_FALSE(castable(&e, "u8 mut@", "u8 mut*"));
    TEST_ASSERT_FALSE(castable(&e, "string", "char*"));
    TEST_ASSERT_FALSE(castable(&e, "char*", "string"));
    TEST_ASSERT_FALSE(castable(&e, "string", "void*"));
    TEST_ASSERT_FALSE(castable(&e, "void*", "string"));
    TEST_ASSERT_FALSE(castable(&e, "u8@", "void*"));
    TEST_ASSERT_FALSE(castable(&e, "void*", "u8@"));
    TEST_ASSERT_FALSE(castable(&e, "string", "u64"));
    TEST_ASSERT_FALSE(castable(&e, "u64", "string"));
    TEST_ASSERT_FALSE(castable(&e, "u64", "u8@"));
    TEST_ASSERT_FALSE(castable(&e, "u8@", "u64"));
    // A pointer to a span is a pointer, and casts like one.
    TEST_ASSERT_TRUE(castable(&e, "u64", "u8@*"));
    TEST_ASSERT_TRUE(castable(&e, "void*", "u8@*"));
    tenv_free(&e);
})

TEST(casts_of_null_void_and_the_error_type, {
    tenv_t e;
    tenv_init(&e);
    const type_t* err = type_error(&e.tt);
    const type_t* null = type_null(&e.tt);
    const type_t* v = type_void(&e.tt);
    const type_t* p = tenv_type(&e, "node*");
    // `null` has no type of its own, so it is not a cast operand.
    // D10.5
    TEST_ASSERT_FALSE(type_cast_allowed(p, null));
    TEST_ASSERT_FALSE(type_cast_allowed(null, p));
    // `void` is not a value type.
    TEST_ASSERT_FALSE(type_cast_allowed(v, p));
    TEST_ASSERT_FALSE(type_cast_allowed(p, v));
    // A poisoned operand casts anywhere: the error was reported already.
    TEST_ASSERT_TRUE(type_cast_allowed(err, p));
    TEST_ASSERT_TRUE(type_cast_allowed(p, err));
    tenv_free(&e);
})

// ---- further conversions --------------------------------------------------------

TEST(assignability_is_reflexive_for_every_kind, {
    tenv_t e;
    tenv_init(&e);
    TEST_ASSERT_TRUE(assignable(&e, "i32", "i32"));
    TEST_ASSERT_TRUE(assignable(&e, "bool", "bool"));
    TEST_ASSERT_TRUE(assignable(&e, "char", "char"));
    TEST_ASSERT_TRUE(assignable(&e, "f64", "f64"));
    TEST_ASSERT_TRUE(assignable(&e, "node*", "node*"));
    TEST_ASSERT_TRUE(assignable(&e, "node mut* own", "node mut* own"));
    TEST_ASSERT_TRUE(assignable(&e, "void*", "void*"));
    TEST_ASSERT_TRUE(assignable(&e, "void* own", "void* own"));
    TEST_ASSERT_TRUE(assignable(&e, "void mut*", "void mut*"));
    TEST_ASSERT_TRUE(assignable(&e, "void mut* own", "void mut* own"));
    TEST_ASSERT_TRUE(assignable(&e, "i32[4]", "i32[4]"));
    TEST_ASSERT_TRUE(assignable(&e, "i32[3][4]", "i32[3][4]"));
    TEST_ASSERT_TRUE(assignable(&e, "string own", "string own"));
    TEST_ASSERT_TRUE(assignable(&e, "node* own[4]", "node* own[4]"));
    TEST_ASSERT_TRUE(assignable(&e, "u8 mut@ own mut*", "u8 mut@ own mut*"));
    TEST_ASSERT_TRUE(type_assignable(e.vec, e.vec));
    TEST_ASSERT_TRUE(type_assignable(e.color, e.color));
    tenv_free(&e);
})

TEST(mutability_drops_along_a_three_level_chain, {
    tenv_t e;
    tenv_init(&e);
    // `mut node***` has every level mutable; a target may drop a suffix of them,
    // starting at the outermost.
    // D5.4
    TEST_ASSERT_TRUE(assignable(&e, "node***", "node mut* mut* mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "node* mut**", "node mut* mut* mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "node** mut*", "node mut* mut* mut*"));
    // Dropping nothing is always fine.
    TEST_ASSERT_TRUE(assignable(&e, "node mut* mut* mut*", "node mut* mut* mut*"));
    // A span of spans behaves like a chain of pointers.
    TEST_ASSERT_TRUE(assignable(&e, "i32@@", "i32 mut@ mut@"));
    TEST_ASSERT_FALSE(assignable(&e, "i32 mut@ mut@", "i32@@"));
    TEST_ASSERT_TRUE(assignable(&e, "u8@*", "u8 mut@ mut*"));
    TEST_ASSERT_FALSE(assignable(&e, "u8@ mut*", "u8 mut@ mut*"));
    tenv_free(&e);
})

TEST(conversions_through_a_fixed_array_follow_the_element, {
    tenv_t e;
    tenv_init(&e);
    // A fixed array adds no level, so its elements convert under the rule that
    // applies to the array itself.
    // D5.2, D5.4
    TEST_ASSERT_TRUE(assignable(&e, "node*[4]", "node mut*[4]"));
    TEST_ASSERT_TRUE(assignable(&e, "node*[4]", "node* own[4]"));
    TEST_ASSERT_FALSE(assignable(&e, "node* own[4]", "node*[4]"));
    TEST_ASSERT_TRUE(assignable(&e, "node*[3][4]", "node mut*[3][4]"));
    TEST_ASSERT_FALSE(assignable(&e, "node mut*[3][4]", "node*[3][4]"));
    // A pointer to an array keeps the array transparent.
    TEST_ASSERT_TRUE(assignable(&e, "node*[4]*", "node mut*[4] mut*"));
    // The `mut` of level 1 sits on the trailing `*`, which the inner `*`
    // carries: `node* mut[4]*` is mutable at level 1 only.
    TEST_ASSERT_FALSE(assignable(&e, "node*[4] mut*", "node mut*[4] mut*"));
    tenv_free(&e);
})

TEST(lending_combines_with_dropping_mutability, {
    tenv_t e;
    tenv_init(&e);
    // The two drops are one conversion, in every order of marks.
    // D17.4
    TEST_ASSERT_TRUE(assignable(&e, "u8@", "u8 mut@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "u8 mut@", "u8 mut@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "u8@ own", "u8 mut@ own"));
    TEST_ASSERT_FALSE(assignable(&e, "u8 mut@ own", "u8@ own"));
    TEST_ASSERT_TRUE(assignable(&e, "node*", "node mut* own"));
    TEST_ASSERT_TRUE(assignable(&e, "node mut*", "node mut* own"));
    TEST_ASSERT_TRUE(assignable(&e, "node* own", "node mut* own"));
    TEST_ASSERT_TRUE(assignable(&e, "void*", "void* own"));
    TEST_ASSERT_TRUE(assignable(&e, "void mut*", "void mut* own"));
    TEST_ASSERT_TRUE(assignable(&e, "string", "string own"));
    // A span of owned strings lends its own mark under the same rule.
    TEST_ASSERT_TRUE(assignable(&e, "string@", "string@ own"));
    TEST_ASSERT_FALSE(assignable(&e, "string@ own", "string@"));
    tenv_free(&e);
})

TEST(casts_may_add_own_at_any_reference, {
    tenv_t e;
    tenv_init(&e);
    // Adoption is a cast, at any level, where the implicit drop refuses. It
    // adds the `own` alone: every `mut` of the target stands in the source.
    // D3.14, D17.3
    TEST_ASSERT_TRUE(castable(&e, "u8 mut@ own", "u8 mut@"));
    TEST_ASSERT_TRUE(castable(&e, "u8 mut@ own mut*", "u8 mut@ mut*"));
    TEST_ASSERT_TRUE(castable(&e, "node mut* own mut@ own", "node mut* mut@"));
    TEST_ASSERT_TRUE(castable(&e, "node mut* mut@", "node mut* own mut@ own"));
    TEST_ASSERT_TRUE(castable(&e, "void* own", "u64"));
    TEST_ASSERT_TRUE(castable(&e, "u8 mut* own", "void mut*"));
    TEST_ASSERT_TRUE(castable(&e, "u8* own", "void*"));
    // Adoption adds no `mut` of its own. `cast(libc.malloc(n), u8 mut* own)`
    // compiles because `malloc` answers `void mut* own`.
    // D3.14, D17.13
    TEST_ASSERT_FALSE(castable(&e, "u8 mut@ own", "u8@"));
    TEST_ASSERT_FALSE(castable(&e, "u8 mut* own", "void*"));
    tenv_free(&e);
})

// ---- the matrix and the invariants ----------------------------------------------

TEST(every_scalar_pair_follows_the_d3_14_matrix, {
    tenv_t e;
    tenv_init(&e);
    const type_t* types[SCALARS_MAX];
    scalar_class_t classes[SCALARS_MAX];
    const uint32_t n = scalar_types(&e, types, classes);
    TEST_ASSERT_EQ_UINT64((uint64_t)n, (uint64_t)SCALARS_MAX);
    for (uint32_t d = 0; d < n; d++) {
        for (uint32_t s = 0; s < n; s++) {
            const bool identical = type_equal(types[d], types[s]);
            const bool want = matrix_allows(classes[d], classes[s], identical);
            const bool got = type_cast_allowed(types[d], types[s]);
            if (got != want) {
                TEST_LOG_("cast(%s -> %s): %s, expected %s",
                          tenv_str(&e, types[s]),
                          tenv_str(&e, types[d]),
                          got ? "allowed" : "refused",
                          want ? "allowed" : "refused");
                TEST_FAIL();
            }
        }
    }
    tenv_free(&e);
})

TEST(every_implicit_conversion_is_also_a_cast, {
    tenv_t e;
    tenv_init(&e);
    // A cast that only drops mutability or ownership is a no-op the implicit
    // conversions already cover, so `cast` is never the stricter of the two.
    // D3.14
    const type_t* types[SAMPLES_MAX];
    const uint32_t n = sample_types(&e, types);
    TEST_ASSERT_EQ_UINT64((uint64_t)n, (uint64_t)SAMPLES_MAX);
    for (uint32_t d = 0; d < n; d++) {
        for (uint32_t s = 0; s < n; s++) {
            if (!type_assignable(types[d], types[s])) {
                continue;
            }
            if (!type_cast_allowed(types[d], types[s])) {
                TEST_LOG_("%s converts to %s but does not cast to it",
                          tenv_str(&e, types[s]),
                          tenv_str(&e, types[d]));
                TEST_FAIL();
            }
        }
    }
    tenv_free(&e);
})

TEST(conversion_and_identity_agree_with_themselves, {
    tenv_t e;
    tenv_init(&e);
    // Identity converts and casts; two types that convert both ways are
    // identical, since every drop is strict.
    const type_t* types[SAMPLES_MAX];
    const uint32_t n = sample_types(&e, types);
    for (uint32_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(type_equal(types[i], types[i]));
        TEST_ASSERT_TRUE(type_assignable(types[i], types[i]));
        TEST_ASSERT_TRUE(type_cast_allowed(types[i], types[i]));
        TEST_ASSERT_TRUE(type_same_shape(types[i], types[i]));
        for (uint32_t j = 0; j < n; j++) {
            if (i == j) {
                continue;
            }
            if (type_assignable(types[i], types[j]) && type_assignable(types[j], types[i])) {
                TEST_LOG_("%s and %s convert both ways but are not one type",
                          tenv_str(&e, types[i]),
                          tenv_str(&e, types[j]));
                TEST_FAIL();
            }
            // Distinct spellings are distinct nodes (interning).
            TEST_ASSERT_FALSE(type_equal(types[i], types[j]));
        }
    }
    tenv_free(&e);
})

TEST(dropping_marks_never_changes_the_shape_or_the_size, {
    tenv_t e;
    tenv_init(&e);
    const type_t* types[SAMPLES_MAX];
    const uint32_t n = sample_types(&e, types);
    for (uint32_t d = 0; d < n; d++) {
        for (uint32_t s = 0; s < n; s++) {
            if (d == s || !type_assignable(types[d], types[s])) {
                continue;
            }
            // An implicit conversion is a no-op at run time.
            // D5.4, D17.1
            TEST_ASSERT_TRUE(type_same_shape(types[d], types[s]));
            TEST_ASSERT_EQ_UINT64(type_sizeof(types[d]), type_sizeof(types[s]));
            TEST_ASSERT_EQ_UINT64(type_alignof(types[d]), type_alignof(types[s]));
            TEST_ASSERT_EQ_UINT64((uint64_t)type_levels(types[d]), (uint64_t)type_levels(types[s]));
        }
    }
    tenv_free(&e);
})

int main(int argc, char** argv) {
    TEST_INIT("types_convert", argc, argv);
    TEST_RUN(the_d5_4_conversions_and_their_shape);
    TEST_RUN(mutability_drops_through_further_shapes);
    TEST_RUN(the_d17_4_conversions_and_their_shape);
    TEST_RUN(ownership_is_never_added_implicitly);
    TEST_RUN(assignability_of_null_void_and_the_error_type);
    TEST_RUN(function_types_convert_only_to_themselves);
    TEST_RUN(casts_among_numbers_bool_char_and_enums);
    TEST_RUN(casts_refused_among_scalars);
    TEST_RUN(casts_among_pointers_and_u64);
    TEST_RUN(casts_of_function_pointers_go_through_voidptr);
    TEST_RUN(casts_within_the_string_family);
    TEST_RUN(a_span_cast_never_launders_a_signature_or_an_owner);
    TEST_RUN(casts_of_spans_change_only_the_marks);
    TEST_RUN(a_cast_never_adds_mut);
    TEST_RUN(casts_refused_for_structs_and_fixed_arrays);
    TEST_RUN(casts_between_a_reference_and_a_fat_pointer_are_refused);
    TEST_RUN(casts_of_null_void_and_the_error_type);
    TEST_RUN(assignability_is_reflexive_for_every_kind);
    TEST_RUN(mutability_drops_along_a_three_level_chain);
    TEST_RUN(conversions_through_a_fixed_array_follow_the_element);
    TEST_RUN(lending_combines_with_dropping_mutability);
    TEST_RUN(casts_may_add_own_at_any_reference);
    TEST_RUN(every_scalar_pair_follows_the_d3_14_matrix);
    TEST_RUN(every_implicit_conversion_is_also_a_cast);
    TEST_RUN(conversion_and_identity_agree_with_themselves);
    TEST_RUN(dropping_marks_never_changes_the_shape_or_the_size);
    TEST_EXIT();
}
