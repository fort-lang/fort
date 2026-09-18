// Types: nodes, interning, identity, conversions, layout, spelling and the
// builder; see types.h.
#include "types.h"

#include <stddef.h>
#include <stdint.h>

// The sizes the type rules fix.
enum { PTR_SIZE = 8, PTR_ALIGN = 8, SPAN_SIZE = 16, ENUM_SIZE = 4 };

// ---- table and interning ---------------------------------------------------------

static type_t* node_new(type_table_t* tt, type_kind_t kind) {
    type_t* t = mem_alloc(sizeof *t);
    t->kind = kind;
    ptrvec_push(&tt->nodes, t);
    return t;
}

// Structural equality of two nodes by child identity: children are interned,
// so pointer equality of children is enough. Nominal nodes are never
// interned, so they never match.
static bool node_same(const type_t* a, const type_t* b) {
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == TYPE_STRUCT || a->kind == TYPE_ENUM) {
        return false;
    }
    if (a->prim != b->prim || a->mut != b->mut || a->own != b->own || a->noreturn != b->noreturn ||
        a->elem != b->elem || a->len != b->len || a->nparams != b->nparams) {
        return false;
    }
    for (uint32_t i = 0; i < a->nparams; i++) {
        if (a->params[i] != b->params[i]) {
            return false;
        }
    }
    return true;
}

// The interned node equal to `key`, or a fresh copy of it.
static const type_t* intern(type_table_t* tt, const type_t* key) {
    for (uint64_t i = 0; i < tt->nodes.len; i++) {
        const type_t* t = tt->nodes.items[i];
        if (node_same(t, key)) {
            return t;
        }
    }
    type_t* t = node_new(tt, key->kind);
    t->prim = key->prim;
    t->mut = key->mut;
    t->own = key->own;
    t->noreturn = key->noreturn;
    t->elem = key->elem;
    t->len = key->len;
    if (key->nparams > 0) {
        // A parameter list is an array of interned pointers. The slot size is
        // the size of a pointer to an aggregate, which is what
        // bugprone-sizeof-expression warns about; here it is the point.
        // NOLINTNEXTLINE(bugprone-sizeof-expression)
        const type_t** params = (const type_t**)mem_alloc(mem_mul(key->nparams, sizeof *params));
        for (uint32_t i = 0; i < key->nparams; i++) {
            params[i] = key->params[i];
        }
        t->params = params;
        t->nparams = key->nparams;
    }
    return t;
}

// A zeroed key of `kind`: the fields a constructor does not set keep the
// values of a node that never carries them.
static type_t key_of(type_kind_t kind) {
    type_t key;
    key.kind = kind;
    key.prim = PRIM_I8;
    key.mut = false;
    key.own = false;
    key.noreturn = false;
    key.elem = NULL;
    key.len = 0;
    key.params = NULL;
    key.nparams = 0;
    key.name = str_from_cstr(NULL);
    key.decl = NULL;
    key.layout = NULL;
    return key;
}

void type_table_init(type_table_t* tt) {
    ptrvec_init(&tt->nodes);
    tt->void_type = NULL;
    tt->null_type = NULL;
    tt->error_type = NULL;
    for (int i = 0; i < PRIM_COUNT; i++) {
        tt->prims[i] = NULL;
    }
}

void type_table_free(type_table_t* tt) {
    for (uint64_t i = 0; i < tt->nodes.len; i++) {
        type_t* t = tt->nodes.items[i];
        // The parameter list was allocated here, so dropping const gives
        // back exactly what mem_alloc handed out.
        mem_free((void*)t->params);
        mem_free(t->layout);
        mem_free(t);
    }
    ptrvec_free(&tt->nodes);
    type_table_init(tt);
}

// ---- constructors ----------------------------------------------------------------

const type_t* type_void(type_table_t* tt) {
    if (tt->void_type == NULL) {
        tt->void_type = node_new(tt, TYPE_VOID);
    }
    return tt->void_type;
}

const type_t* type_null(type_table_t* tt) {
    if (tt->null_type == NULL) {
        tt->null_type = node_new(tt, TYPE_NULL);
    }
    return tt->null_type;
}

const type_t* type_error(type_table_t* tt) {
    if (tt->error_type == NULL) {
        tt->error_type = node_new(tt, TYPE_ERROR);
    }
    return tt->error_type;
}

const type_t* type_prim(type_table_t* tt, prim_kind_t k) {
    if (k == PRIM_VOID) {
        fatal_internal("PRIM_VOID as a primitive type: use type_void");
    }
    if (tt->prims[k] == NULL) {
        type_t* t = node_new(tt, TYPE_PRIM);
        t->prim = k;
        tt->prims[k] = t;
    }
    return tt->prims[k];
}

const type_t* type_string(type_table_t* tt, bool own) {
    type_t key = key_of(TYPE_STRING);
    key.own = own;
    return intern(tt, &key);
}

const type_t* type_voidptr(type_table_t* tt, bool own, bool mut) {
    type_t key = key_of(TYPE_VOIDPTR);
    key.elem = type_void(tt);
    key.own = own;
    // The storage behind a `void*` has no type, and `void mut*` says a caller
    // may write it.
    key.mut = mut;
    return intern(tt, &key);
}

// The element checks shared by pointers, spans and arrays; true when the
// result is the poisoned error type.
static bool elem_poisons(const type_t* elem) {
    if (elem->kind == TYPE_VOID) {
        fatal_internal("void as an element type");
    }
    if (elem->kind == TYPE_NULL) {
        fatal_internal("null as an element type");
    }
    return elem->kind == TYPE_ERROR;
}

const type_t* type_ptr(type_table_t* tt, const type_t* elem, bool own, bool mut) {
    if (elem_poisons(elem)) {
        return elem;
    }
    type_t key = key_of(TYPE_PTR);
    key.elem = elem;
    key.own = own;
    key.mut = mut;
    return intern(tt, &key);
}

const type_t* type_span(type_table_t* tt, const type_t* elem, bool own, bool mut) {
    if (elem_poisons(elem)) {
        return elem;
    }
    type_t key = key_of(TYPE_SPAN);
    key.elem = elem;
    key.own = own;
    key.mut = mut;
    return intern(tt, &key);
}

const type_t* type_array(type_table_t* tt, const type_t* elem, uint64_t len) {
    if (elem_poisons(elem)) {
        return elem;
    }
    if (len == 0) {
        fatal_internal("array length 0");
    }
    type_t key = key_of(TYPE_ARRAY);
    key.elem = elem;
    key.len = len;
    return intern(tt, &key);
}

const type_t* type_fn(type_table_t* tt,
                      const type_t* ret,
                      const type_t* const* params,
                      uint32_t nparams,
                      bool noreturn) {
    if (ret->kind == TYPE_NULL) {
        fatal_internal("null as a return type");
    }
    if (noreturn && ret->kind != TYPE_VOID) {
        fatal_internal("noreturn with a return type");
    }
    if (ret->kind == TYPE_ERROR) {
        return ret;
    }
    for (uint32_t i = 0; i < nparams; i++) {
        if (params[i]->kind == TYPE_ERROR) {
            return params[i];
        }
        if (params[i]->kind == TYPE_VOID || params[i]->kind == TYPE_NULL) {
            fatal_internal("void or null as a parameter type");
        }
    }
    type_t key = key_of(TYPE_FN);
    key.elem = ret;
    key.params = params;
    key.nparams = nparams;
    key.noreturn = noreturn;
    return intern(tt, &key);
}

// A nominal type has one node per declaration because the declaration defines
// identity. Another call with the same `decl` returns the first node. Thus, one
// struct cannot split its derived types across nodes. A NULL
// `decl` is anonymous and always makes a node.
static const type_t* nominal(type_table_t* tt, type_kind_t kind, str_t name, const void* decl) {
    if (decl != NULL) {
        for (uint64_t i = 0; i < tt->nodes.len; i++) {
            const type_t* t = tt->nodes.items[i];
            if (t->decl != decl) {
                continue;
            }
            if (t->kind != kind) {
                fatal_internal("one declaration used for a struct and an enum");
            }
            return t;
        }
    }
    type_t* t = node_new(tt, kind);
    t->name = name;
    t->decl = decl;
    if (kind == TYPE_STRUCT) {
        t->layout = mem_alloc(sizeof *t->layout);
        t->layout->state = LAYOUT_UNRESOLVED;
        t->layout->align = 1;
    }
    return t;
}

const type_t* type_struct(type_table_t* tt, str_t name, const void* decl) {
    return nominal(tt, TYPE_STRUCT, name, decl);
}

const type_t* type_enum(type_table_t* tt, str_t name, const void* decl) {
    return nominal(tt, TYPE_ENUM, name, decl);
}

// ---- queries ---------------------------------------------------------------------

bool type_is_reference(const type_t* t) {
    return t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR || t->kind == TYPE_SPAN ||
           t->kind == TYPE_STRING;
}

bool type_is_int(const type_t* t) {
    return t->kind == TYPE_PRIM && prim_is_integer(t->prim);
}

bool type_is_float(const type_t* t) {
    return t->kind == TYPE_PRIM && prim_is_float(t->prim);
}

bool type_is_scalar(const type_t* t) {
    return t->kind == TYPE_PRIM || t->kind == TYPE_ENUM;
}

// Skips the fixed arrays around a type: the node whose storage the arrays'
// elements share.
static const type_t* behind_arrays(const type_t* t) {
    while (t->kind == TYPE_ARRAY) {
        t = t->elem;
    }
    return t;
}

// Whether a reference reaches a storage level. Pointers, `void*`, and spans do.
// Strings do not because their characters are immutable and have no target level. The
// level a `void*` reaches has no type, and `void mut*` marks it writable.
static bool has_target_level(const type_t* t) {
    return t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR || t->kind == TYPE_SPAN;
}

uint32_t type_levels(const type_t* t) {
    uint32_t n = 0;
    t = behind_arrays(t);
    while (has_target_level(t)) {
        n++;
        t = behind_arrays(t->elem);
    }
    return n;
}

const type_t* type_ref_at(const type_t* t, uint32_t k) {
    if (k == 0) {
        fatal_internal("level 0 is not a reference");
    }
    t = behind_arrays(t);
    while (type_is_reference(t)) {
        if (k == 1) {
            return t;
        }
        if (t->elem == NULL) {
            return NULL;
        }
        t = behind_arrays(t->elem);
        k--;
    }
    return NULL;
}

bool type_level_mut(const type_t* t, uint32_t k) {
    const type_t* r = type_ref_at(t, k);
    return r != NULL && r->mut;
}

bool type_is_owning_aggregate(const type_t* t) {
    if (t->kind == TYPE_ARRAY) {
        const type_t* e = t->elem;
        return (type_is_reference(e) && e->own) || type_is_owning_aggregate(e);
    }
    if (t->kind != TYPE_STRUCT) {
        return false;
    }
    if (t->layout->state == LAYOUT_ERROR) {
        return false;
    }
    if (t->layout->state != LAYOUT_RESOLVED) {
        fatal_internal("owning query on a struct without layout");
    }
    return t->layout->owning;
}

// ---- identity --------------------------------------------------------------------

// The identity: with `bits` false the `mut` and `own` marks of the reference
// chain are ignored and only its shape is compared. A function type is always
// compared whole. Its parameter and result marks are part of its identity, not
// the chain that reaches it.
static bool same(const type_t* a, const type_t* b, bool bits) {
    if (a == b) {
        return true;
    }
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
    case TYPE_VOID:
    case TYPE_NULL:
    case TYPE_ERROR:
        return true;
    case TYPE_PRIM:
        return a->prim == b->prim;
    case TYPE_STRING:
        return !bits || a->own == b->own;
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_SPAN:
        // The elem of a `void*` is always `void`, so the recursion ends there,
        // and only its marks decide.
        return (!bits || (a->own == b->own && a->mut == b->mut)) && same(a->elem, b->elem, bits);
    case TYPE_ARRAY:
        return a->len == b->len && same(a->elem, b->elem, bits);
    case TYPE_FN:
        // `fn (node mut*) void` and `fn (node*) void` are two types
        if (a->noreturn != b->noreturn || a->nparams != b->nparams ||
            !same(a->elem, b->elem, true)) {
            return false;
        }
        for (uint32_t i = 0; i < a->nparams; i++) {
            if (!same(a->params[i], b->params[i], true)) {
                return false;
            }
        }
        return true;
    case TYPE_STRUCT:
    case TYPE_ENUM:
        return a->decl != NULL && a->decl == b->decl;
    }
    fatal_internal("unknown type kind");
}

bool type_equal(const type_t* a, const type_t* b) {
    return same(a, b, true);
}

bool type_same_shape(const type_t* a, const type_t* b) {
    return same(a, b, false);
}

// ---- implicit conversions --------------------------------------------------------

// Whether `src` converts to `dst` at one chain position. `prefix_immutable`
// means each earlier destination level is immutable. `outer_own` means an outer
// destination reference owns its value.
static bool convertible(const type_t* dst,
                        const type_t* src,
                        bool prefix_immutable,
                        bool outer_own) {
    if (dst == src) {
        return true;
    }
    if (dst->kind != src->kind) {
        return false;
    }
    switch (dst->kind) {
    case TYPE_VOID:
    case TYPE_NULL:
    case TYPE_ERROR:
        return true;
    case TYPE_PRIM:
        // the only implicit conversions drop marks
        return dst->prim == src->prim;
    case TYPE_STRUCT:
    case TYPE_ENUM:
    case TYPE_FN:
        // nominal, and no variance for function types
        return type_equal(dst, src);
    case TYPE_ARRAY:
        return dst->len == src->len &&
               convertible(dst->elem, src->elem, prefix_immutable, outer_own);
    case TYPE_STRING:
    case TYPE_VOIDPTR:
    case TYPE_PTR:
    case TYPE_SPAN:
        break;
    }
    if (dst->mut && !src->mut) {
        return false;
    }
    if (src->mut && !dst->mut && !prefix_immutable) {
        // the C `T** -> const T**` hole
        return false;
    }
    if (dst->own && !src->own) {
        return false;
    }
    if (src->own && !dst->own && (!prefix_immutable || outer_own)) {
        // else the inner objects would be owned by nobody
        return false;
    }
    if (dst->elem == NULL) {
        // a `string` has no level behind it
        return true;
    }
    return convertible(dst->elem, src->elem, prefix_immutable && !dst->mut, outer_own || dst->own);
}

bool type_assignable(const type_t* dst, const type_t* src) {
    if (dst->kind == TYPE_ERROR || src->kind == TYPE_ERROR) {
        // A poisoned type converts either way, so one error is reported once.
        return true;
    }
    if (src->kind == TYPE_NULL) {
        // The null literal never converts to a span or string. Their zero is `{}`.
        return dst->kind == TYPE_PTR || dst->kind == TYPE_VOIDPTR || dst->kind == TYPE_FN;
    }
    if (dst->kind == TYPE_NULL || dst->kind == TYPE_VOID || src->kind == TYPE_VOID) {
        return false;
    }
    return convertible(dst, src, true, false);
}

// ---- casts -----------------------------------------------------------------------

// Whether a type belongs to the string family: `string`, `char@` or `u8@` with
// any marks. These spellings, and only these, cast among themselves: the header
// is reinterpreted, `own` may be added or dropped, and `mut` may be dropped.
// Adding `mut` is refused here as everywhere, so `cast("abc", u8 mut@)` no
// longer aliases read-only bytes as writable ones.
static bool string_family(const type_t* t) {
    if (t->kind == TYPE_STRING) {
        return true;
    }
    return t->kind == TYPE_SPAN && t->elem->kind == TYPE_PRIM &&
           (t->elem->prim == PRIM_CHAR || t->elem->prim == PRIM_U8);
}

static bool is_u64(const type_t* t) {
    return t->kind == TYPE_PRIM && t->prim == PRIM_U64;
}

static bool is_ptr_like(const type_t* t) {
    return t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR;
}

static bool is_prim(const type_t* t, prim_kind_t k) {
    return t->kind == TYPE_PRIM && t->prim == k;
}

// The scalar rows of the matrix: integers, floats, bool, char and enums.
static bool scalar_cast_allowed(const type_t* dst, const type_t* src) {
    bool src_int = type_is_int(src);
    bool dst_int = type_is_int(dst);
    bool src_num = src_int || type_is_float(src);
    bool dst_num = dst_int || type_is_float(dst);
    if (src_num && dst_num) {
        // widening, narrowing, sign change, rounding, saturation
        return true;
    }
    if (dst_int) {
        // `bool` gives 0 or 1. `char` gives its byte.
        return is_prim(src, PRIM_BOOL) || is_prim(src, PRIM_CHAR) || src->kind == TYPE_ENUM;
    }
    if (src_int) {
        return is_prim(dst, PRIM_CHAR) || dst->kind == TYPE_ENUM;
    }
    // go through an integer
    return false;
}

// Whether `src` loses `own` below a reference that `dst` still owns. Such a cast
// would leave inner objects without an owner and is not allowed.
static bool own_orphaned(const type_t* dst, const type_t* src, bool outer_own) {
    while (dst->kind == TYPE_ARRAY || type_is_reference(dst)) {
        if (dst->kind == TYPE_ARRAY) {
            dst = dst->elem;
            src = src->elem;
            continue;
        }
        if (src->own && !dst->own && outer_own) {
            return true;
        }
        outer_own = outer_own || dst->own;
        if (dst->elem == NULL) {
            return false; // `string`: no reference behind it
        }
        dst = dst->elem;
        src = src->elem;
    }
    return false;
}

// Applies the span row of the cast matrix. Spans must have the same element
// type. Their marks can differ only by dropped mutability at any level.
// Ownership marks can change at any reference. Function parameter and result
// marks are part of element identity. Thus,
// A span of `fn (node mut*) void` does not cast to a span of `fn (node*) void`.
// The drop alone is this row's work. `type_cast_adds_mut` already refused every
// span whose marks add one.
static bool span_cast_allowed(const type_t* dst, const type_t* src) {
    return type_same_shape(dst, src) && !own_orphaned(dst, src, false);
}

// Whether `t` marks any level `mut`, at this position or below it. A fixed
// array shares the storage of its elements and adds no level of its own.
static bool marks_mut(const type_t* t) {
    if (t->kind == TYPE_ARRAY) {
        return marks_mut(t->elem);
    }
    if (!type_is_reference(t)) {
        return false;
    }
    if (t->mut) {
        return true;
    }
    if (t->elem == NULL) {
        // `string`: no level behind it, and none of its own
        return false;
    }
    return marks_mut(t->elem);
}

bool type_cast_adds_mut(const type_t* dst, const type_t* src) {
    if (dst->kind == TYPE_ARRAY) {
        // the array adds no level, so the two chains stay in step
        if (src->kind == TYPE_ARRAY) {
            return type_cast_adds_mut(dst->elem, src->elem);
        }
        return marks_mut(dst);
    }
    if (!type_is_reference(dst)) {
        // No level of the target stands here, so nothing can be added to it.
        return false;
    }
    if (!type_is_reference(src)) {
        // The source has no level at this depth: an integer, a `void`, a
        // struct or a function pointer promises nothing about this storage.
        return marks_mut(dst);
    }
    if (dst->mut && !src->mut) {
        return true;
    }
    if (dst->elem == NULL) {
        // The target is a `string`: no level behind it.
        return false;
    }
    if (src->elem == NULL) {
        // The source is a `string`, whose bytes are immutable and behind which
        // no level stands.
        return marks_mut(dst->elem);
    }
    return type_cast_adds_mut(dst->elem, src->elem);
}

const type_t* type_without_mut(type_table_t* tt, const type_t* t) {
    switch (t->kind) {
    case TYPE_PTR:
        return type_ptr(tt, type_without_mut(tt, t->elem), t->own, false);
    case TYPE_VOIDPTR:
        return type_voidptr(tt, t->own, false);
    case TYPE_SPAN:
        return type_span(tt, type_without_mut(tt, t->elem), t->own, false);
    case TYPE_ARRAY:
        // the array holds no mark of its own
        return type_array(tt, type_without_mut(tt, t->elem), t->len);
    default:
        return t;
    }
}

bool type_cast_allowed(const type_t* dst, const type_t* src) {
    if (dst->kind == TYPE_ERROR || src->kind == TYPE_ERROR) {
        return true;
    }
    if (src->kind == TYPE_NULL || dst->kind == TYPE_NULL || dst->kind == TYPE_VOID ||
        src->kind == TYPE_VOID) {
        return false;
    }
    if (type_equal(dst, src)) {
        return true;
    }
    // Any conversion that only drops mutability or ownership is a cast too, a no-op
    // the implicit rules already cover. This is the only way a fixed array or a
    // struct converts at all: every other row that names them is an error.
    if (type_assignable(dst, src)) {
        return true;
    }
    // One rule over every row below: a cast never adds `mut`, from any source.
    // `void*`, `u64` and `string` are sources like the rest, so
    // `cast(cast(p, void*), u8 mut*)` fails here at the second cast.
    if (type_cast_adds_mut(dst, src)) {
        return false;
    }
    if (type_is_scalar(src) && type_is_scalar(dst)) {
        return scalar_cast_allowed(dst, src);
    }
    if (is_ptr_like(src)) {
        // A pointer converts to another pointer or to `u64`.
        // A function pointer converts only through `void*`.
        return is_ptr_like(dst) || is_u64(dst) ||
               (src->kind == TYPE_VOIDPTR && dst->kind == TYPE_FN);
    }
    if (is_u64(src)) {
        // an address comes back from `u64` alone
        return is_ptr_like(dst);
    }
    if (src->kind == TYPE_FN) {
        // not even another function type
        return dst->kind == TYPE_VOIDPTR;
    }
    if (string_family(src) && string_family(dst)) {
        return true;
    }
    if (src->kind == TYPE_SPAN && dst->kind == TYPE_SPAN) {
        return span_cast_allowed(dst, src);
    }
    return false;
}

// ---- sizes and layout ------------------------------------------------------------

// The largest permitted object is 2^63 - 1 bytes. A declaration reports
// "type is too large" when its type would exceed this limit.
static const uint64_t TYPE_MAX_SIZE = (uint64_t)INT64_MAX;

// `v` rounded up to a multiple of `align` in `*out`; false when the result
// would exceed TYPE_MAX_SIZE.
static bool round_up(uint64_t v, uint64_t align, uint64_t* out) {
    if (align == 0) {
        fatal_internal("alignment 0");
    }
    uint64_t rem = v % align;
    if (rem != 0 && v > TYPE_MAX_SIZE - (align - rem)) {
        return false;
    }
    *out = rem == 0 ? v : v + (align - rem);
    return true;
}

static type_layout_t* layout_of(const type_t* s) {
    if (s->kind != TYPE_STRUCT) {
        fatal_internal("layout of a non-struct");
    }
    return s->layout;
}

// The layout of a struct for sizeof and alignof: RESOLVED or ERROR.
static const type_layout_t* laid_out(const type_t* s) {
    const type_layout_t* l = layout_of(s);
    if (l->state != LAYOUT_RESOLVED && l->state != LAYOUT_ERROR) {
        fatal_internal("sizeof of a struct without layout");
    }
    return l;
}

static bool size_checked(const type_t* t, uint64_t* out);

// `len * sizeof(elem)` in `*out`; false when the product would exceed
// TYPE_MAX_SIZE.
static bool array_size(const type_t* t, uint64_t* out) {
    uint64_t elem = 0;
    if (!size_checked(t->elem, &elem)) {
        return false;
    }
    if (elem != 0 && t->len > TYPE_MAX_SIZE / elem) {
        return false;
    }
    *out = t->len * elem;
    return true;
}

// The size of `t` in `*out`; false when it would exceed TYPE_MAX_SIZE. Every
// size the compiler computes goes through here, so no size arithmetic can wrap.
static bool size_checked(const type_t* t, uint64_t* out) {
    switch (t->kind) {
    case TYPE_VOID:
    case TYPE_NULL:
        fatal_internal("sizeof of void or null");
    case TYPE_ERROR:
        *out = 0;
        return true;
    case TYPE_PRIM:
        *out = (uint64_t)prim_size(t->prim);
        return true;
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_FN:
        *out = PTR_SIZE;
        return true;
    case TYPE_SPAN:
    case TYPE_STRING:
        // the fat pointer `{ptr, len}`
        *out = SPAN_SIZE;
        return true;
    case TYPE_ARRAY:
        // elements are contiguous
        return array_size(t, out);
    case TYPE_ENUM:
        *out = ENUM_SIZE;
        return true;
    case TYPE_STRUCT:
        // The layout enforced the ceiling when it laid the struct out.
        *out = laid_out(t)->size;
        return true;
    }
    fatal_internal("unknown type kind");
}

bool type_size_fits(const type_t* t) {
    uint64_t size = 0;
    return size_checked(t, &size);
}

uint64_t type_sizeof(const type_t* t) {
    uint64_t size = 0;
    if (!size_checked(t, &size)) {
        fatal_internal("sizeof of a type that is too large");
    }
    return size;
}

uint64_t type_alignof(const type_t* t) {
    switch (t->kind) {
    case TYPE_VOID:
    case TYPE_NULL:
        fatal_internal("alignof of void or null");
    case TYPE_ERROR:
        return 1;
    case TYPE_PRIM:
        return (uint64_t)prim_size(t->prim);
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_FN:
    case TYPE_SPAN:
    case TYPE_STRING:
        return PTR_ALIGN;
    case TYPE_ARRAY:
        return type_alignof(t->elem);
    case TYPE_ENUM:
        return ENUM_SIZE;
    case TYPE_STRUCT:
        return laid_out(t)->align;
    }
    fatal_internal("unknown type kind");
}

layout_state_t type_layout_state(const type_t* s) {
    return layout_of(s)->state;
}

bool type_layout_begin(const type_t* s) {
    type_layout_t* l = layout_of(s);
    if (l->state == LAYOUT_UNRESOLVED) {
        l->state = LAYOUT_RESOLVING;
        return true;
    }
    if (l->state == LAYOUT_RESOLVING) {
        return false;
    }
    fatal_internal("layout begun twice");
}

void type_layout_fail(const type_t* s) {
    type_layout_t* l = layout_of(s);
    l->state = LAYOUT_ERROR;
    l->size = 0;
    l->align = 1;
    l->owning = false;
}

const type_t* type_layout_pending(const type_t* t) {
    t = behind_arrays(t);
    if (t->kind != TYPE_STRUCT) {
        return NULL;
    }
    layout_state_t st = t->layout->state;
    return (st == LAYOUT_RESOLVED || st == LAYOUT_ERROR) ? NULL : t;
}

bool type_layout_struct(const type_t* s,
                        const type_t* const* fields,
                        uint32_t n,
                        uint64_t* offsets) {
    type_layout_t* l = layout_of(s);
    if (l->state == LAYOUT_RESOLVED || l->state == LAYOUT_ERROR) {
        fatal_internal("layout of a struct twice");
    }
    if (n == 0) {
        fatal_internal("layout of an empty struct");
    }
    uint64_t off = 0;
    uint64_t align = 1;
    bool owning = false;
    for (uint32_t i = 0; i < n; i++) {
        const type_t* f = fields[i];
        if (type_layout_pending(f) != NULL) {
            fatal_internal("layout with a pending field");
        }
        // each field at the next multiple of its alignment
        // a field or a total past the ceiling is too large
        uint64_t size = 0;
        uint64_t a = type_alignof(f);
        if (!size_checked(f, &size) || !round_up(off, a, &off) || off > TYPE_MAX_SIZE - size) {
            type_layout_fail(s);
            return false;
        }
        offsets[i] = off;
        off += size;
        if (a > align) {
            align = a;
        }
        // an `own` field makes the struct an owning aggregate
        if ((type_is_reference(f) && f->own) || type_is_owning_aggregate(f)) {
            owning = true;
        }
    }
    // rounded up to the alignment, as C lays the same struct out
    if (!round_up(off, align, &l->size)) {
        type_layout_fail(s);
        return false;
    }
    l->align = align;
    l->owning = owning;
    l->state = LAYOUT_RESOLVED;
    return true;
}

// ---- spelling --------------------------------------------------------------------

// The most nodes one spelled chain walks; a deeper chain continues in
// parentheses.
enum { SPINE_MAX = 64 };

// Holds suffixes from the outermost inward, followed by the base. The groups are
// references, fixed arrays, and trailing references. Each group can be empty.
// A chain with an array after a trailing reference cannot be written directly.
// Collection stops there and puts the remaining type in parentheses.
typedef struct {
    const type_t* nodes[SPINE_MAX];
    uint32_t n;
    uint32_t first_array; // the array group is [first_array, end_array)
    uint32_t end_array;
    const type_t* base;
    bool paren;
} spine_t;

static bool is_array_node(const type_t* t) {
    return t->kind == TYPE_ARRAY;
}

// A `*` or `@` suffix: the two that introduce a reference.
static bool is_ref_node(const type_t* t) {
    return t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR || t->kind == TYPE_SPAN;
}

static void spine_collect(spine_t* sp, const type_t* t) {
    bool seen_array = false;
    bool seen_ref_after_array = false;
    sp->n = 0;
    // an array behind a reference behind an array has no spelling
    while (sp->n < SPINE_MAX) {
        if (is_array_node(t)) {
            if (seen_ref_after_array) {
                break;
            }
            seen_array = true;
        } else if (is_ref_node(t)) {
            seen_ref_after_array = seen_ref_after_array || seen_array;
        } else {
            break;
        }
        sp->nodes[sp->n++] = t;
        t = t->elem;
    }
    sp->base = t;
    sp->paren = is_array_node(t) || is_ref_node(t);
    sp->first_array = sp->n;
    sp->end_array = 0;
    for (uint32_t i = 0; i < sp->n; i++) {
        if (is_array_node(sp->nodes[i])) {
            if (sp->first_array == sp->n) {
                sp->first_array = i;
            }
            sp->end_array = i + 1;
        }
    }
    if (sp->first_array == sp->n) {
        sp->first_array = 0;
        sp->end_array = 0;
    }
}

static void spell(const type_t* t, bool mut0, sb_t* out);

static void spell_base(const type_t* t, sb_t* out) {
    switch (t->kind) {
    case TYPE_VOID:
        sb_append(out, "void");
        return;
    case TYPE_PRIM:
        sb_append(out, prim_name(t->prim));
        return;
    case TYPE_STRING:
        sb_append(out, "string");
        return;
    case TYPE_STRUCT:
    case TYPE_ENUM:
        sb_append_str(out, t->name);
        return;
    case TYPE_NULL:
        sb_append(out, "null");
        return;
    case TYPE_ERROR:
        sb_append(out, "<error>");
        return;
    case TYPE_FN:
        // the result comes last, as it does in a declaration
        sb_append(out, "fn (");
        for (uint32_t i = 0; i < t->nparams; i++) {
            if (i > 0) {
                sb_append(out, ", ");
            }
            spell(t->params[i], false, out);
        }
        sb_append(out, ") ");
        if (t->noreturn) {
            sb_append(out, "noreturn");
        } else {
            spell(t->elem, false, out);
        }
        return;
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_ARRAY:
    case TYPE_SPAN:
        break;
    }
    fatal_internal("spelling a reference as a base");
}

// Returns the mutability of storage for the node at `i`. The first outer
// reference reaches that storage. Fixed arrays between them share storage with
// their elements. Without an outer reference, level 0 holds it.
static bool holder_mut(const spine_t* sp, uint32_t i, bool mut0) {
    for (uint32_t j = i; j > 0; j--) {
        if (is_ref_node(sp->nodes[j - 1])) {
            return sp->nodes[j - 1]->mut;
        }
    }
    return mut0;
}

// One suffix with the markers of its position: `own` for a reference that owns
// its target, `mut` for the storage the suffix's own value occupies. A reference
// held by a fixed array prints no `mut`. A non-final array in its group also
// prints no `mut`. They share storage, whose marker follows the final length.
static void spell_suffix(const spine_t* sp, uint32_t i, bool mut0, sb_t* out) {
    const type_t* t = sp->nodes[i];
    if (is_array_node(t)) {
        sb_push(out, '[');
        sb_append_u64(out, t->len);
        sb_push(out, ']');
        if (i + 1 == sp->end_array && holder_mut(sp, i, mut0)) {
            sb_append(out, " mut");
        }
        return;
    }
    sb_append(out, t->kind == TYPE_SPAN ? "@" : "*");
    if (t->own) {
        sb_append(out, " own");
    }
    const bool held_by_array = i > 0 && is_array_node(sp->nodes[i - 1]);
    if (!held_by_array && holder_mut(sp, i, mut0)) {
        sb_append(out, " mut");
    }
}

// Writes suffixes in source order. It writes the base reference group
// innermost first. It writes arrays outermost first. It then writes trailing
// references innermost first.
static void spell_suffixes(const spine_t* sp, bool mut0, sb_t* out) {
    for (uint32_t i = sp->n; i > sp->end_array; i--) {
        spell_suffix(sp, i - 1, mut0, out);
    }
    for (uint32_t i = sp->first_array; i < sp->end_array; i++) {
        spell_suffix(sp, i, mut0, out);
    }
    for (uint32_t i = sp->first_array; i > 0; i--) {
        spell_suffix(sp, i - 1, mut0, out);
    }
}

static void spell(const type_t* t, bool mut0, sb_t* out) {
    spine_t sp;
    spine_collect(&sp, t);
    // The base position stores base-type values. The innermost reference reaches
    // it. The binding stores it when the type has no suffix.
    // An array between them takes the marker.
    const bool base_mut =
        sp.n == 0 ? mut0 : (is_ref_node(sp.nodes[sp.n - 1]) && sp.nodes[sp.n - 1]->mut);
    if (sp.paren) {
        sb_push(out, '(');
        spell(sp.base, base_mut, out);
        sb_push(out, ')');
    } else {
        spell_base(sp.base, out);
        // `string` takes `own` after the base type
        if (sp.base->kind == TYPE_STRING && sp.base->own) {
            sb_append(out, " own");
        }
        if (base_mut) {
            sb_append(out, " mut");
        }
    }
    spell_suffixes(&sp, mut0, out);
}

void type_to_str(const type_t* t, sb_t* out) {
    spell(t, false, out);
}

void type_to_str_decl(const type_t* t, bool mut0, sb_t* out) {
    spell(t, mut0, out);
}

// ---- builder ---------------------------------------------------------------------

static type_build_t build_error(const char* msg) {
    type_build_t r;
    r.type = NULL;
    r.mut0 = false;
    r.error = msg;
    return r;
}

// A `*` or `@` suffix, the two that introduce a reference.
static bool suffix_is_ref(const type_suffix_t* s) {
    return s->kind == SUFFIX_PTR || s->kind == SUFFIX_SPAN;
}

// Splits a suffix list into three groups. [0, e) holds base references. [e, a)
// holds fixed arrays. [a, n) holds whole-array references.
typedef struct {
    uint32_t e;
    uint32_t a;
    uint32_t n;
} groups_t;

static bool split_groups(const type_suffix_t* s, uint32_t n, groups_t* g) {
    uint32_t i = 0;
    while (i < n && suffix_is_ref(&s[i])) {
        i++;
    }
    g->e = i;
    while (i < n && s[i].kind == SUFFIX_ARRAY) {
        i++;
    }
    g->a = i;
    while (i < n && suffix_is_ref(&s[i])) {
        i++;
    }
    g->n = n;
    return i == n;
}

// Returns the index of applied suffix k, innermost first. Reference groups apply
// in source order because they read inside-out. The array group applies in
// reverse because it reads outside-in.
static uint32_t apply_order(const groups_t* g, uint32_t k) {
    if (k >= g->e && k < g->a) {
        return g->a - 1 - (k - g->e);
    }
    return k;
}

// Checks each position marker before building. A position marks storage for the
// item it follows. A fixed array shares storage with its elements. Thus, `mut`
// for that storage follows the array length.
static const char* check_positions(
    const type_t* base, bool base_own, bool base_mut, const type_suffix_t* s, uint32_t n) {
    // `void` is a return type or pointer base. It accepts only the `mut` in
    // `void mut*`, which marks reached storage. `void mut`, `void own`, `void@` and `void[4]` name
    // no storage at all.
    if (base->kind == TYPE_VOID) {
        const bool base_of_ptr = n > 0 && s[0].kind == SUFFIX_PTR;
        if (base_own || ((base_mut || n > 0) && !base_of_ptr)) {
            return "'void' is only a return type or the base of 'void*'";
        }
    }
    if (base_own && base->kind != TYPE_STRING) {
        return "'own' marks a reference: write it after a '*' or an '@'";
    }
    if (base_own && base->own) {
        // No source can write it: a base type is never an owned one.
        fatal_internal("own on a base type that is already owned");
    }
    if (base_mut && n > 0 && s[0].kind == SUFFIX_ARRAY) {
        return "mark the array after its length";
    }
    for (uint32_t i = 0; i < n; i++) {
        if (s[i].kind == SUFFIX_ARRAY) {
            if (s[i].len == 0) {
                return "array length must be greater than 0";
            }
            if (s[i].own) {
                return "'own' never follows a fixed-array suffix";
            }
        }
        if (s[i].mut && i + 1 < n && s[i + 1].kind == SUFFIX_ARRAY) {
            return "mark the array after its length";
        }
    }
    return NULL;
}

type_build_t type_build(type_table_t* tt,
                        bool base_own,
                        bool base_mut,
                        const type_t* base,
                        const type_suffix_t* suffixes,
                        uint32_t n) {
    if (base->kind == TYPE_ERROR) {
        // The base already failed: poison the result rather than report a
        // second diagnostic about its markers.
        type_build_t poisoned;
        poisoned.type = base;
        poisoned.mut0 = n == 0 ? base_mut : suffixes[n - 1].mut;
        poisoned.error = NULL;
        return poisoned;
    }
    groups_t g;
    if (!split_groups(suffixes, n, &g)) {
        return build_error("no array suffix may follow a trailing reference suffix");
    }
    const char* bad = check_positions(base, base_own, base_mut, suffixes, n);
    if (bad != NULL) {
        return build_error(bad);
    }
    // Build innermost first. A position's `mut` marks storage for the item it
    // follows. The next outer reference reaches that storage. Without an outer
    // reference, the binding holds it. A fixed array shares the storage with its
    // elements and adds no level.
    const type_t* t = base_own ? type_string(tt, true) : base;
    bool pending_mut = base_mut;
    for (uint32_t k = 0; k < n; k++) {
        const type_suffix_t* s = &suffixes[apply_order(&g, k)];
        if (s->kind == SUFFIX_ARRAY) {
            t = type_array(tt, t, s->len);
            pending_mut = pending_mut || s->mut;
            continue;
        }
        if (s->kind == SUFFIX_SPAN) {
            t = type_span(tt, t, s->own, pending_mut);
        } else if (t->kind == TYPE_VOID) {
            t = type_voidptr(tt, s->own, pending_mut);
        } else {
            t = type_ptr(tt, t, s->own, pending_mut);
        }
        pending_mut = s->mut;
    }
    type_build_t r;
    r.type = t;
    r.mut0 = pending_mut;
    r.error = NULL;
    return r;
}
