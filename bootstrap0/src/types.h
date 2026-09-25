// Represents and interns compiler types.
// It checks conversions, casts, sizes, layouts, and canonical spellings.
// Type nodes remain valid until `type_table_free`.
#ifndef FORT_TYPES_H
#define FORT_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "containers.h"
#include "prim.h"
#include "str.h"

typedef enum {
    TYPE_VOID,    // `void`: a return type or the base of `void*`
    TYPE_PRIM,    // a primitive (prim.h)
    TYPE_PTR,     // `T*`, `T* own`, `T mut*`
    TYPE_VOIDPTR, // `void*`, `void* own`, `void mut*`
    TYPE_FN,      // `fn (P1, P2) R`
    TYPE_ARRAY,   // `T[N]`
    TYPE_SPAN,    // `T@`, `T@ own`, `T mut@`
    TYPE_STRING,  // `string`, `string own`
    TYPE_STRUCT,  // nominal
    TYPE_ENUM,    // nominal, underlying i32
    TYPE_NULL,    // the type of the literal `null` before it adopts one
    TYPE_ERROR,   // poison: the type of an expression that already failed
} type_kind_t;

// The state of a struct's layout.
typedef enum {
    LAYOUT_UNRESOLVED = 0,
    LAYOUT_RESOLVING, // on the resolution path: reaching it again is a cycle
    LAYOUT_RESOLVED,
    LAYOUT_ERROR, // infinite size; sizeof yields 0 and alignof 1
} layout_state_t;

typedef struct {
    layout_state_t state;
    uint64_t size;
    uint64_t align;
    bool owning; // a field is an `own` reference or an owning aggregate
} type_layout_t;

typedef struct type type_t;
struct type {
    type_kind_t kind;
    prim_kind_t prim;            // TYPE_PRIM
    bool mut;                    // TYPE_PTR, TYPE_VOIDPTR, TYPE_SPAN: the storage reached is
                                 // mutable
    bool own;                    // TYPE_PTR, TYPE_VOIDPTR, TYPE_SPAN, TYPE_STRING
    bool noreturn;               // TYPE_FN: `fn (...) noreturn`, with elem void
    const type_t* elem;          // pointee, element or return type
    uint64_t len;                // TYPE_ARRAY
    const type_t* const* params; // TYPE_FN, owned by the node
    uint32_t nparams;
    str_t name;            // TYPE_STRUCT, TYPE_ENUM: the spelling in diagnostics
    const void* decl;      // TYPE_STRUCT, TYPE_ENUM: the declaring symbol, opaque
    type_layout_t* layout; // TYPE_STRUCT, owned by the node; NULL otherwise
};

// Owns every node it hands out. Zero-initialized storage is a valid empty
// table.
typedef struct {
    ptrvec_t nodes;
    const type_t* void_type;
    const type_t* null_type;
    const type_t* error_type;
    const type_t* prims[PRIM_COUNT];
} type_table_t;

void type_table_init(type_table_t* tt);

// Releases every node; the table is empty and usable afterwards.
void type_table_free(type_table_t* tt);

// ---- constructors ----------------------------------------------------------------
// Every constructor interns: equal arguments give the same node. An elem or
// parameter of kind TYPE_ERROR poisons the result, which is the error type.

const type_t* type_void(type_table_t* tt);
const type_t* type_null(type_table_t* tt);
const type_t* type_error(type_table_t* tt);
// `k` must not be PRIM_VOID: `void` is a type kind of its own (type_void).
const type_t* type_prim(type_table_t* tt, prim_kind_t k);
const type_t* type_string(type_table_t* tt, bool own);
const type_t* type_voidptr(type_table_t* tt, bool own, bool mut);

// `elem` must not be void (use type_voidptr) or the null type.
const type_t* type_ptr(type_table_t* tt, const type_t* elem, bool own, bool mut);
const type_t* type_span(type_table_t* tt, const type_t* elem, bool own, bool mut);

// `len` must be greater than 0; `elem` must not be void or null.
const type_t* type_array(type_table_t* tt, const type_t* elem, uint64_t len);

// `ret` is void for a `noreturn` function. The parameter list is copied.
const type_t* type_fn(type_table_t* tt,
                      const type_t* ret,
                      const type_t* const* params,
                      uint32_t nparams,
                      bool noreturn);

// Nominal types: every call makes a new node with UNRESOLVED layout. `name` is
// borrowed for the life of the table; `decl` is stored and otherwise opaque.
// Two nominal nodes are identical only when their `decl` is the same non-NULL
// pointer, or when they are one node.
const type_t* type_struct(type_table_t* tt, str_t name, const void* decl);
const type_t* type_enum(type_table_t* tt, str_t name, const void* decl);

// ---- queries ---------------------------------------------------------------------

// A pointer, `void*`, span or string: the kinds that may be `own`.
bool type_is_reference(const type_t* t);

// A primitive of the class prim.h names, on a whole type.
bool type_is_int(const type_t* t);
bool type_is_float(const type_t* t);
bool type_is_scalar(const type_t* t); // integer, float, bool, char, enum

// The storage levels behind level 0: one per pointer, `void*` or span node on
// the chain, fixed arrays adding none; `string` adds none.
uint32_t type_levels(const type_t* t);

// Returns the reference stored at level k - 1 that reaches level k. `k` must be
// at least 1. Level 1 is the outermost reference behind any fixed arrays. Later
// levels move inward. Returns NULL beyond the chain. `mut` describes level k.
// `own` describes the reference. A string node can report `own`, but it adds no
// level.
const type_t* type_ref_at(const type_t* t, uint32_t k);

// The mutability of level k >= 1: false beyond the chain and for the
// characters of a string. Level 1 of a `void*` is the storage it reaches,
// which `void mut*` marks writable.
bool type_level_mut(const type_t* t, uint32_t k);

// Whether a struct or fixed array contains an `own` reference by value, directly
// or through nested aggregates. A struct answers from its layout, so it must be
// RESOLVED; an ERROR struct answers false.
bool type_is_owning_aggregate(const type_t* t);

// ---- identity and conversions ----------------------------------------------------

// Identity: nominal for struct and enum, structural otherwise, with the `mut`
// and `own` bits of every level.
bool type_equal(const type_t* a, const type_t* b);

// Whether the types are equal after ignoring `mut` and `own` in the reference
// chain. A function type is compared whole. Its parameter and result marks are
// part of its identity. Thus, `fn (node mut*) void` and `fn (node*) void` have
// different shapes.
bool type_same_shape(const type_t* a, const type_t* b);

// Whether a value of type `src` may be used where `dst` is expected. This allows
// identity, a null literal for a pointer, `void*`, or function type, and two
// monotone drops. Mutability can drop at level k only when each earlier level is
// immutable in `dst`. Ownership has this condition and one more condition. No
// outer reference can be `own` in `dst`. Adding either mark is refused. Level 0
// is not part of the types and is unconstrained. An error type on either side is
// accepted. A `void` or `null` destination is refused. A `void` source is also
// refused because no value has that type.
bool type_assignable(const type_t* dst, const type_t* src);

// Applies the cast matrix to types alone. The checker handles rvalues, lvalues,
// `move`, and binding-level `mut` in a cast target. Each conversion accepted by
// `type_assignable` is also a cast. Thus, an array or struct can convert when
// only marks drop.
bool type_cast_allowed(const type_t* dst, const type_t* src);

// Whether a cast from `src` to `dst` would mark a level `mut` that `src` leaves
// immutable. A cast never adds `mut`. `dst` can mark a level `mut` only when
// `src` marks the same level `mut`. `dst` cannot add `mut` below a missing source
// level. Integers, strings, reinterpreted pointees, and the `void` behind a
// `void*` have such missing levels. `type_cast_allowed` rejects each cast when
// this function returns true. The checker calls this function again to select
// the diagnostic.
bool type_cast_adds_mut(const type_t* dst, const type_t* src);

// `t` with every `mut` cleared, at every level a mark stands on. A function
// type comes back as it is, because the marks of its parameters and of its
// result are part of its identity. The checker retries a refused cast with this
// type. This distinguishes a `mut` failure from an element-type failure.
const type_t* type_without_mut(type_table_t* tt, const type_t* t);

// ---- sizes and layout ------------------------------------------------------------

// Whether the size of `t` fits the 2^63 - 1 byte object limit. An array fails
// when its element count times its element size exceeds the limit. The checker
// asks at the declaration and reports "type is too large". A struct uses its
// layout, which enforces the same limit.
bool type_size_fits(const type_t* t);

// Size and alignment in bytes. A struct must be RESOLVED or ERROR (0 and 1);
// void and the null type are internal errors; the error type is 0 and 1.
// `type_sizeof` of a type `type_size_fits` refuses is an internal error, so the
// caller asks first.
uint64_t type_sizeof(const type_t* t);
uint64_t type_alignof(const type_t* t);

// The caller drives struct layout because the type does not store fields. First,
// call `type_layout_begin(S)`. Resolve each struct returned by
// `type_layout_pending(field)` before S. A false result from type_layout_begin
// identifies a value-containment cycle. Report it and fail each struct on the
// path. Then call `type_layout_struct(S, ...)`.
layout_state_t type_layout_state(const type_t* s);

// UNRESOLVED to RESOLVING: true. RESOLVING: false, a cycle. RESOLVED and
// ERROR are internal errors.
bool type_layout_begin(const type_t* s);

// Marks a struct ERROR from any state.
void type_layout_fail(const type_t* s);

// The struct whose layout `sizeof(t)` needs and that is not RESOLVED or
// ERROR yet: `t` itself, the element's for a fixed array, or NULL.
const type_t* type_layout_pending(const type_t* t);

// Lays out `n` fields in C/System V order. It places each field at the next
// multiple of its alignment. It rounds the size to the largest alignment. It
// writes offsets, size, alignment, and aggregate ownership. It then marks `s`
// RESOLVED. `s` must be an UNRESOLVED or RESOLVING struct. `n` must exceed 0.
// No field can have a pending layout.
//
// Returns false when one field or the complete struct exceeds 2^63 - 1 bytes.
// It leaves `s` in ERROR, as type_layout_fail does. The offsets are then
// unspecified. The caller reports "type is too large", as it reports an
// infinite-size cycle.
bool type_layout_struct(const type_t* s,
                        const type_t* const* fields,
                        uint32_t n,
                        uint64_t* offsets);

// ---- spelling --------------------------------------------------------------------

// Appends the canonical spelling without the binding position. Examples are
// `node mut* own mut@ own`, `u8 mut@ own mut*`, `fn (i32, i32) i32`, and
// `fn () noreturn`. Each combination of marks has a spelling. Only a chain with an
// array after a trailing reference needs parentheses. That chain does not
// parse: `(i32[4])*[2]`. The null type is `null`. The error type is `<error>`.
void type_to_str(const type_t* t, sb_t* out);

// Appends the spelling of a declaration's type with the binding's position, the
// last one: `node* mut`, `u8@* mut`, `i32[4] mut`, `string own mut`. Level 0 is a
// position like any other, so this is `type_to_str` with the last marker written.
void type_to_str_decl(const type_t* t, bool mut0, sb_t* out);

// ---- builder ---------------------------------------------------------------------

typedef enum {
    SUFFIX_PTR,   // `*`, a reference suffix
    SUFFIX_ARRAY, // `[N]`
    SUFFIX_SPAN,  // `@`, a reference suffix
} suffix_kind_t;

// One suffix of `base [own] [mut] { suffix [own] [mut] }` in source order,
// with the `own` and `mut` written right after it. An array suffix carries no
// `own`.
typedef struct {
    suffix_kind_t kind;
    uint64_t len; // SUFFIX_ARRAY
    bool own;
    bool mut;
} type_suffix_t;

typedef struct {
    const type_t* type; // NULL when the descriptors are invalid
    bool mut0;          // level 0 of the declaration
    const char* error;  // the reason when `type` is NULL; a static string
} type_build_t;

// Builds the type `base [own] [mut] suffixes...` by the reading rules of the
// suffix grammar. Reference suffixes (`*`, `@`) read inside-out. Each reference
// group applies in source order. The fixed-array group between them reads
// outside-in. Each position marks the storage of the item that it follows.
// `base_mut` marks values of the base type. `mut` after a reference marks
// storage that holds the reference. `mut` after the last array length marks the
// array. The final marker marks the binding and returns as `mut0`. `base_own`
// marks a string or the reference with no suffix. `own` after `*` or `@` marks
// that reference.
//
// The builder refuses several forms and sets `error`. These forms include an
// array after a trailing reference and `own` after an array. It also rejects
// `own` after a base other than `string`. `mut` cannot follow the position before
// an array suffix. Array elements share the array storage, so `mut` follows the
// length. An array length cannot be 0. `void` is valid only alone or as the base
// of `void*`. Bare `void` builds the return type. An error base poisons the
// result without another diagnostic.
//
// The builder sees one type but does not know its position. The checker enforces
// two placement rules. A struct field or return type cannot have outermost
// `mut`. A cast target also cannot have outermost `mut`. The builder accepts
// these types and returns their mark in `mut0`.
type_build_t type_build(type_table_t* tt,
                        bool base_own,
                        bool base_mut,
                        const type_t* base,
                        const type_suffix_t* suffixes,
                        uint32_t n);

#endif
