// Types of the bootstrap compiler (type-system.md): the type node, interning,
// identity, the implicit conversions, the cast matrix, sizes and C/System V
// layout, the canonical spelling of a type, and a builder that applies the
// suffix-reading rules to a list of suffix descriptors.
// D3, D5, D17, D3.1, D3.6, D3.12, D3.14, D3.15, D5.4, D9.9, D17.1, D17.4
//
// Level model. A type is a chain of storage levels: level 0 is the binding's
// own storage and lives outside the type (the `mut0` of the declaration, which
// the caller holds); every pointer, `void*` or span node carries the mutability
// of the storage it reaches (`mut`, the next level) and whether the reference it
// represents is owned (`own`). Fixed arrays add no level. `string` has no target
// level, so its `mut` is always false. A `void*` reaches a level whose type
// cannot be written, and `void mut*` marks that level writable.
// D3.11, D5.2, D17.2
//
// A written type marks each of those storages once, in the position that
// follows the type element occupying it: after the base type for the values of
// that type, after a `*` or `@` for the reference that suffix introduces,
// after a fixed-array suffix for the array, whose elements share its storage.
// The last position is the binding, so the pointer node of `node mut* p` has
// `mut` set (the node is writable) while `node* mut p` has a plain pointer
// node and `mut0` set.
// D5.3
//
// Nodes are interned per table: two structurally equal types built through one
// table are one node, so pointer equality is type identity for every kind
// except struct and enum, whose identity is their declaration. The table is a
// vector and interning scans it, which is what a compilation of this size
// needs; a map can replace the scan without changing this interface.
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants, layout driven by the caller
// instead of callbacks.
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
    TYPE_VOIDPTR, // D3.11: `void*`, `void* own`, `void mut*`
    TYPE_FN,      // D3.10: `fn R(P1, P2)`
    TYPE_ARRAY,   // D3.4: `T[N]`
    TYPE_SPAN,    // D3.5: `T@`, `T@ own`, `T mut@`
    TYPE_STRING,  // D3.7: `string`, `string own`
    TYPE_STRUCT,  // D3.8: nominal
    TYPE_ENUM,    // D3.9: nominal, underlying i32
    TYPE_NULL,    // D10.5: the type of the literal `null` before it adopts one
    TYPE_ERROR,   // poison: the type of an expression that already failed
} type_kind_t;

/// The state of a struct's layout.
/// D3.8
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
    bool owning; // D17.7: a field is an `own` reference or an owning aggregate
} type_layout_t;

typedef struct type type_t;
struct type {
    type_kind_t kind;
    prim_kind_t prim;            // TYPE_PRIM
    bool mut;                    // TYPE_PTR, TYPE_VOIDPTR, TYPE_SPAN: the storage reached is
                                 // mutable
    bool own;                    // TYPE_PTR, TYPE_VOIDPTR, TYPE_SPAN, TYPE_STRING
    bool noreturn;               // TYPE_FN: `fn noreturn(...)`, with elem void
    const type_t* elem;          // pointee, element or return type
    uint64_t len;                // TYPE_ARRAY
    const type_t* const* params; // TYPE_FN, owned by the node
    uint32_t nparams;
    str_t name;            // TYPE_STRUCT, TYPE_ENUM: the spelling in diagnostics
    const void* decl;      // TYPE_STRUCT, TYPE_ENUM: the declaring symbol, opaque
    type_layout_t* layout; // TYPE_STRUCT, owned by the node; NULL otherwise
};

/// Owns every node it hands out. Zero-initialized storage is a valid empty
/// table.
typedef struct {
    ptrvec_t nodes;
    const type_t* void_type;
    const type_t* null_type;
    const type_t* error_type;
    const type_t* prims[PRIM_COUNT];
} type_table_t;

void type_table_init(type_table_t* tt);

/// Releases every node; the table is empty and usable afterwards.
void type_table_free(type_table_t* tt);

// ---- constructors ----------------------------------------------------------------
// Every constructor interns: equal arguments give the same node. An elem or
// parameter of kind TYPE_ERROR poisons the result, which is the error type.

const type_t* type_void(type_table_t* tt);
const type_t* type_null(type_table_t* tt);
const type_t* type_error(type_table_t* tt);
/// `k` must not be PRIM_VOID: `void` is a type kind of its own (type_void).
const type_t* type_prim(type_table_t* tt, prim_kind_t k);
const type_t* type_string(type_table_t* tt, bool own);
const type_t* type_voidptr(type_table_t* tt, bool own, bool mut);

/// `elem` must not be void (use type_voidptr) or the null type.
const type_t* type_ptr(type_table_t* tt, const type_t* elem, bool own, bool mut);
const type_t* type_span(type_table_t* tt, const type_t* elem, bool own, bool mut);

/// `len` must be greater than 0; `elem` must not be void or null.
/// D3.4
const type_t* type_array(type_table_t* tt, const type_t* elem, uint64_t len);

/// `ret` is void for a `noreturn` function. The parameter list is copied.
const type_t* type_fn(type_table_t* tt,
                      const type_t* ret,
                      const type_t* const* params,
                      uint32_t nparams,
                      bool noreturn);

/// Nominal types: every call makes a new node with UNRESOLVED layout. `name` is
/// borrowed for the life of the table; `decl` is stored and otherwise opaque.
/// Two nominal nodes are identical only when their `decl` is the same non-NULL
/// pointer, or when they are one node.
const type_t* type_struct(type_table_t* tt, str_t name, const void* decl);
const type_t* type_enum(type_table_t* tt, str_t name, const void* decl);

// ---- queries ---------------------------------------------------------------------

/// A pointer, `void*`, span or string: the kinds that may be `own`.
/// D17.1
bool type_is_reference(const type_t* t);

/// A primitive of the class prim.h names, on a whole type.
bool type_is_int(const type_t* t);
bool type_is_float(const type_t* t);
bool type_is_scalar(const type_t* t); // integer, float, bool, char, enum

/// The storage levels behind level 0: one per pointer, `void*` or span node on
/// the chain, fixed arrays adding none; `string` adds none.
/// D3.11, D5.2
uint32_t type_levels(const type_t* t);

/// The reference stored at level k - 1 and reaching level k, for k >= 1: the
/// outermost pointer, `void*`, span or string of `t` (behind any fixed arrays) for
/// k == 1, then inward; NULL beyond the chain. Its `mut` is the mutability of
/// level k and its `own` the ownership mark of that reference. A `string` node is
/// reported, since it carries an `own` mark, although it adds no level.
/// D17.2
const type_t* type_ref_at(const type_t* t, uint32_t k);

/// The mutability of level k >= 1: false beyond the chain and for the
/// characters of a string. Level 1 of a `void*` is the storage it reaches,
/// which `void mut*` marks writable.
/// D3.11
bool type_level_mut(const type_t* t, uint32_t k);

/// Whether a struct or fixed array contains an `own` reference by value, directly
/// or through nested aggregates. A struct answers from its layout, so it must be
/// RESOLVED; an ERROR struct answers false.
/// D17.7
bool type_is_owning_aggregate(const type_t* t);

// ---- identity and conversions ----------------------------------------------------

/// Identity: nominal for struct and enum, structural otherwise, with the `mut`
/// and `own` bits of every level.
/// D3.12, D17.1
bool type_equal(const type_t* a, const type_t* b);

/// Whether the equality of `type_equal` holds when the `mut` and `own` bits of
/// the reference chain are ignored: the shape of the two types is the same. A
/// function type is compared whole, since the marks of its parameters and result
/// are part of its identity, so `fn void(node mut*)` and `fn void(node*)` are
/// different shapes.
/// D3.10
bool type_same_shape(const type_t* a, const type_t* b);

/// Whether a value of type `src` may be used where `dst` is expected: identity,
/// the null literal for a pointer, `void*` or function type, and the two monotone
/// drops. Mutability may be dropped at level k only if every level from 1 to k - 1
/// is immutable in `dst`; `own` may be dropped at a reference only under the same
/// condition and only if no reference outside it is `own` in `dst`. Adding either
/// is refused. Level 0 is not part of the types and is unconstrained. An error
/// type on either side is accepted; a `void` or `null` type as the destination,
/// and `void` as the source, are refused (no value has those types).
/// D5.4, D17.4
bool type_assignable(const type_t* dst, const type_t* src);

/// The cast matrix (type-system.md 9.2) on types alone: the rvalue, lvalue and
/// `move` rules are the checker's, and so is the refusal of a binding-level `mut`
/// in a cast target. Every conversion `type_assignable` accepts is a cast as well,
/// which is what lets a fixed array or a struct convert when only marks are
/// dropped.
/// D3.14, D17.5, D17.8
bool type_cast_allowed(const type_t* dst, const type_t* src);

/// Whether a cast from `src` to `dst` would mark a level `mut` that `src` leaves
/// immutable. A cast never adds `mut`, from any source: `dst` may mark a level
/// `mut` only where `src` marks the level at the same depth `mut` too. Where
/// `src` has no such level -- an integer, the `void` behind a `void*`, a
/// `string`, or a pointee type the cast reinterprets -- `dst` may mark no level
/// below that point. `type_cast_allowed` refuses every cast this accepts; the
/// checker asks it again to name the reason in the diagnostic.
/// D3.14
bool type_cast_adds_mut(const type_t* dst, const type_t* src);

/// `t` with every `mut` cleared, at every level a mark stands on. A function
/// type comes back as it is, because the marks of its parameters and of its
/// result are part of its identity. The checker asks whether a refused cast
/// would be allowed over this type, which is how it tells a cast refused for
/// the mark from one refused for the element type as well.
/// D3.10, D3.14
const type_t* type_without_mut(type_table_t* tt, const type_t* t);

// ---- sizes and layout ------------------------------------------------------------

/// Whether the size of `t` fits the largest object the compiler admits, 2^63 - 1
/// bytes: false for an array whose element count times element size passes it. The
/// checker asks this at the declaration that introduces the type and reports "type
/// is too large"; a struct answers from its layout, which enforced the same
/// ceiling.
/// D3.4
bool type_size_fits(const type_t* t);

/// Size and alignment in bytes. A struct must be RESOLVED or ERROR (0 and 1);
/// void and the null type are internal errors; the error type is 0 and 1.
/// `type_sizeof` of a type `type_size_fits` refuses is an internal error, so the
/// caller asks first.
/// D3.1, D3.15
uint64_t type_sizeof(const type_t* t);
uint64_t type_alignof(const type_t* t);

/// Struct layout is driven by the caller, since fields are not stored in the
/// type: for a struct S, `type_layout_begin(S)`; for each field, while
/// `type_layout_pending(field)` is a struct, lay that struct out first (a
/// `type_layout_begin` that returns false is a value-containment cycle: report it
/// and `type_layout_fail` the structs on the path); then `type_layout_struct(S,
/// ...)`.
/// D3.8
layout_state_t type_layout_state(const type_t* s);

/// UNRESOLVED to RESOLVING: true. RESOLVING: false, a cycle. RESOLVED and
/// ERROR are internal errors.
bool type_layout_begin(const type_t* s);

/// Marks a struct ERROR from any state.
void type_layout_fail(const type_t* s);

/// The struct whose layout `sizeof(t)` needs and that is not RESOLVED or
/// ERROR yet: `t` itself, the element's for a fixed array, or NULL.
const type_t* type_layout_pending(const type_t* t);

/// Lays out `n` fields per C/System V: fields in order, each at the next multiple
/// of its alignment, size rounded up to the largest alignment. Writes the offsets,
/// records size, alignment and whether the struct is an owning aggregate on `s`,
/// and marks it RESOLVED. `s` must be a struct in state UNRESOLVED or RESOLVING,
/// `n` greater than 0, and no field may be pending.
///
/// Returns false when the struct would pass the ceiling of 2^63 - 1 bytes, because
/// a field does or because the fields together do: `s` is left ERROR, as after
/// `type_layout_fail`, the offsets are unspecified, and the caller reports "type
/// is too large" the way it reports the infinite-size error of a cycle.
/// D3.4, D3.8, D9.9
bool type_layout_struct(const type_t* s,
                        const type_t* const* fields,
                        uint32_t n,
                        uint64_t* offsets);

// ---- spelling --------------------------------------------------------------------

/// Appends the canonical spelling without the binding's position:
/// `node mut* own mut@ own`, `u8 mut@ own mut*`, `fn i32(i32, i32)`,
/// `fn noreturn()`. Every combination of marks has a spelling, so the only chain
/// that needs parentheses is one with an array suffix after a trailing reference
/// suffix, which does not parse: `(i32[4])*[2]`. The null type is `null`, the
/// error type `<error>`.
/// D3.6, D5.3
void type_to_str(const type_t* t, sb_t* out);

/// Appends the spelling of a declaration's type with the binding's position, the
/// last one: `node* mut`, `u8@* mut`, `i32[4] mut`, `string own mut`. Level 0 is a
/// position like any other, so this is `type_to_str` with the last marker written.
/// D5.3
void type_to_str_decl(const type_t* t, bool mut0, sb_t* out);

// ---- builder ---------------------------------------------------------------------
// D3.6

typedef enum {
    SUFFIX_PTR,   // D3.6: `*`, a reference suffix
    SUFFIX_ARRAY, // D3.4: `[N]`
    SUFFIX_SPAN,  // D3.5: `@`, a reference suffix
} suffix_kind_t;

/// One suffix of `base [own] [mut] { suffix [own] [mut] }` in source order,
/// with the `own` and `mut` written right after it. An array suffix carries no
/// `own`.
/// D17.2
typedef struct {
    suffix_kind_t kind;
    uint64_t len; // SUFFIX_ARRAY
    bool own;
    bool mut;
} type_suffix_t;

typedef struct {
    const type_t* type; // NULL when the descriptors are invalid
    bool mut0;          // D5.2: level 0 of the declaration
    const char* error;  // the reason when `type` is NULL; a static string
} type_build_t;

/// Builds the type `base [own] [mut] suffixes...` by the reading rules of the
/// suffix grammar. Reference suffixes (`*`, `@`) read inside-out, so each of the
/// two reference groups applies in source order; the fixed-array group between
/// them reads outside-in. Each position marks the storage of what it follows:
/// `base_mut` the values of the base type, a `mut` after a reference suffix the
/// storage holding that reference, a `mut` after the last length of the array
/// group the array; the marker of the last position is the binding, returned as
/// `mut0`. `base_own` marks a `string`, the reference with no suffix, and an `own`
/// after `*` or `@` the reference that suffix introduces.
///
/// Refused, with the message in `error`: an array suffix after a trailing
/// reference suffix, `own` after a fixed-array suffix or after any base type but
/// `string`, a `mut` on the position a fixed-array suffix follows (its elements
/// share the array's storage, so the marker goes after the length), an array
/// length of 0, and `void` other than as the base of `void*`. A base of `void`
/// with no suffix builds `void` itself, for return types, and a base of the error
/// type poisons the result without a second diagnostic.
///
/// The builder sees one type and knows nothing of where it stands, so two
/// placement rules stay with the checker: the outermost position of a struct field
/// or of a return type never carries `mut`, and neither does the outermost
/// position of a cast target. Both are refusals of a type this builder accepts, on
/// the `mut0` it returns.
/// D3.6, D3.14, D5.3, D5.5, D17.2
type_build_t type_build(type_table_t* tt,
                        bool base_own,
                        bool base_mut,
                        const type_t* base,
                        const type_suffix_t* suffixes,
                        uint32_t n);

#endif
