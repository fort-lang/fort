// The type-table environment the type tests share: the table, the nominal
// types the specification's examples use, a lookup by name, and a reader that
// turns a written type into the arguments of type_build (grammar 4), so that
// a table row of decisions.md is one assertion.
// D3.6
//
// The reader accepts `base [own] [mut] { suffix [own] [mut] }` with a suffix
// `*`, `@` or `[N]`, which is the grammar's type rule without function types;
// a suite that needs `fn R(P)` builds it with tenv_fn. The binding's own
// position is the last one, which the builder returns as `mut0`, so every
// helper reports it beside the type.
// D5.3
#ifndef FORT_TEST_TYPES_HELPERS_H
#define FORT_TEST_TYPES_HELPERS_H

#include <stdbool.h>
#include <stdint.h>

#include "prim.h"
#include "str.h"
#include "types.h"

#include "common.h"

enum { TENV_MAX_NAMED = 32, TENV_MAX_SUFFIX = 8 };

/// The nominal types the examples use: `node` and `point`, `rec`
/// (type-system.md 4.1), `vec`, the enums `color` and `shape`. `node`, `point`
/// and `vec` are laid out; `rec` is left UNRESOLVED for the layout suite to
/// lay out itself.
/// D5.3, D17.7, D3.9
typedef struct {
    type_table_t tt;
    sb_t buf;
    const char* names[TENV_MAX_NAMED];
    const type_t* types[TENV_MAX_NAMED];
    uint32_t nnamed;
    const type_t* node;  // struct node { i32 v; node* next; }
    const type_t* point; // struct point { i32 x; i32 y; }
    const type_t* rec;   // struct rec { u8 tag; i32 n; u16 k; f64 x; }
    const type_t* vec;   // struct vec { i32 mut@ own data; u64 len; }
    const type_t* color;
    const type_t* shape;
} tenv_t;

static inline void tenv_bind(tenv_t* e, const char* name, const type_t* t) {
    if (e->nnamed >= TENV_MAX_NAMED) {
        fatal_internal("too many named types in a test environment");
    }
    e->names[e->nnamed] = name;
    e->types[e->nnamed] = t;
    e->nnamed++;
}

/// A nominal struct whose declaration pointer is its own name, so that two
/// structs are never confused.
/// D3.12
static inline const type_t* tenv_struct(tenv_t* e, const char* name) {
    const type_t* t = type_struct(&e->tt, str_from_cstr(name), name);
    tenv_bind(e, name, t);
    return t;
}

static inline const type_t* tenv_enum(tenv_t* e, const char* name) {
    const type_t* t = type_enum(&e->tt, str_from_cstr(name), name);
    tenv_bind(e, name, t);
    return t;
}

/// The type of `name`, or NULL.
static inline const type_t* tenv_lookup(const tenv_t* e, str_t name) {
    for (uint32_t i = 0; i < e->nnamed; i++) {
        if (str_eq(name, str_from_cstr(e->names[i]))) {
            return e->types[i];
        }
    }
    return NULL;
}

static inline void tenv_init(tenv_t* e) {
    type_table_init(&e->tt);
    sb_init(&e->buf);
    e->nnamed = 0;
    for (int i = 0; i < PRIM_COUNT; i++) {
        const prim_kind_t k = (prim_kind_t)i;
        if (k == PRIM_VOID) {
            // D3.1
            continue; // `void` is a type kind of its own (types.h)
        }
        tenv_bind(e, prim_name(k), type_prim(&e->tt, k));
    }
    tenv_bind(e, "string", type_string(&e->tt, false));
    tenv_bind(e, "void", type_void(&e->tt));
    e->node = tenv_struct(e, "node");
    e->point = tenv_struct(e, "point");
    e->rec = tenv_struct(e, "rec");
    e->vec = tenv_struct(e, "vec");
    e->color = tenv_enum(e, "color");
    e->shape = tenv_enum(e, "shape");

    const type_t* i32 = type_prim(&e->tt, PRIM_I32);
    const type_t* u64 = type_prim(&e->tt, PRIM_U64);
    uint64_t offsets[2];

    const type_t* point_fields[2] = {i32, i32};
    TEST_UNUSED(type_layout_begin(e->point));
    TEST_UNUSED(type_layout_struct(e->point, point_fields, 2, offsets));

    const type_t* node_fields[2] = {i32, type_ptr(&e->tt, e->node, false, false)};
    TEST_UNUSED(type_layout_begin(e->node));
    TEST_UNUSED(type_layout_struct(e->node, node_fields, 2, offsets));

    const type_t* vec_fields[2] = {type_span(&e->tt, i32, true, true), u64};
    TEST_UNUSED(type_layout_begin(e->vec));
    TEST_UNUSED(type_layout_struct(e->vec, vec_fields, 2, offsets));
}

static inline void tenv_free(tenv_t* e) {
    sb_free(&e->buf);
    type_table_free(&e->tt);
    e->nnamed = 0;
}

/// `fn ret(p0, p1)`, which the reader cannot spell: a NULL parameter ends the
/// list, so tenv_fn(e, r, NULL, NULL) is `fn r()`.
static inline const type_t* tenv_fn(tenv_t* e,
                                    const type_t* ret,
                                    const type_t* p0,
                                    const type_t* p1) {
    const type_t* params[2];
    params[0] = p0;
    params[1] = p1;
    uint32_t n = 0;
    if (p0 != NULL) {
        n = p1 == NULL ? 1 : 2;
    }
    return type_fn(&e->tt, ret, params, n, false);
}

// ---- the spelling reader ---------------------------------------------------------

static inline bool tenv_is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static inline void tenv_skip_spaces(const char* text, uint32_t* i) {
    while (text[*i] == ' ') {
        (*i)++;
    }
}

/// The word at `*i` after any spaces, `*i` past it; the zero view when the
/// next character starts no word.
static inline str_t tenv_take_word(const char* text, uint32_t* i) {
    tenv_skip_spaces(text, i);
    const uint32_t start = *i;
    while (tenv_is_word_char(text[*i])) {
        (*i)++;
    }
    if (*i == start) {
        return str_from_cstr(NULL);
    }
    return str_from_range(text + start, *i - start);
}

/// Takes the word `want` when it is next; `*i` is untouched otherwise.
static inline bool tenv_take_keyword(const char* text, uint32_t* i, const char* want) {
    const uint32_t save = *i;
    const str_t word = tenv_take_word(text, i);
    if (str_eq(word, str_from_cstr(want))) {
        return true;
    }
    *i = save;
    return false;
}

static inline type_build_t tenv_parse_error(const char* msg) {
    type_build_t r;
    r.type = NULL;
    r.mut0 = false;
    r.error = msg;
    return r;
}

/// Reads one suffix into `s`; NULL on success, the message otherwise.
static inline const char* tenv_take_suffix(const char* text, uint32_t* i, type_suffix_t* s) {
    s->len = 0;
    s->own = false;
    s->mut = false;
    if (text[*i] == '*') {
        (*i)++;
        s->kind = SUFFIX_PTR;
    } else if (text[*i] == '@') {
        (*i)++;
        s->kind = SUFFIX_SPAN;
    } else if (text[*i] == '[') {
        (*i)++;
        const uint64_t base = 10;
        uint64_t len = 0;
        if (text[*i] < '0' || text[*i] > '9') {
            return "parse: expected an array length";
        }
        while (text[*i] >= '0' && text[*i] <= '9') {
            len = (len * base) + (uint64_t)(text[*i] - '0');
            (*i)++;
        }
        if (text[*i] != ']') {
            return "parse: expected ']'";
        }
        (*i)++;
        s->kind = SUFFIX_ARRAY;
        s->len = len;
    } else {
        return "parse: expected a suffix";
    }
    s->own = tenv_take_keyword(text, i, "own");
    s->mut = tenv_take_keyword(text, i, "mut");
    return NULL;
}

/// The build of the type spelled by `text`. On failure `error` is the
/// builder's message, or one starting with "parse: " when the spelling is not
/// one the reader (or the grammar) accepts.
static inline type_build_t tenv_build(tenv_t* e, const char* text) {
    uint32_t i = 0;
    const str_t name = tenv_take_word(text, &i);
    if (name.len == 0) {
        return tenv_parse_error("parse: expected a base type");
    }
    const type_t* base = tenv_lookup(e, name);
    if (base == NULL) {
        return tenv_parse_error("parse: unknown base type");
    }
    // The base position, the only marker that precedes a suffix.
    // D5.3
    const bool base_own = tenv_take_keyword(text, &i, "own");
    const bool base_mut = tenv_take_keyword(text, &i, "mut");
    type_suffix_t suffixes[TENV_MAX_SUFFIX];
    uint32_t n = 0;
    tenv_skip_spaces(text, &i);
    while (text[i] != '\0') {
        if (n >= TENV_MAX_SUFFIX) {
            return tenv_parse_error("parse: too many suffixes");
        }
        const char* bad = tenv_take_suffix(text, &i, &suffixes[n]);
        if (bad != NULL) {
            return tenv_parse_error(bad);
        }
        n++;
        tenv_skip_spaces(text, &i);
    }
    return type_build(&e->tt, base_own, base_mut, base, suffixes, n);
}

/// The type `text` spells; a spelling the builder refuses is a test error and
/// ends the process, since every caller writes the spelling itself.
static inline const type_t* tenv_type(tenv_t* e, const char* text) {
    const type_build_t r = tenv_build(e, text);
    if (r.type == NULL) {
        fatal_internal(r.error);
    }
    return r.type;
}

/// Level 0 of the declaration `text`.
/// D5.2
static inline bool tenv_mut0(tenv_t* e, const char* text) {
    const type_build_t r = tenv_build(e, text);
    if (r.type == NULL) {
        fatal_internal(r.error);
    }
    return r.mut0;
}

/// The canonical spelling of the type `text` builds, without level 0, or
/// "error: <message>" when the builder refuses it. Valid until the next call
/// on `e`.
static inline const char* tenv_spell(tenv_t* e, const char* text) {
    const type_build_t r = tenv_build(e, text);
    sb_clear(&e->buf);
    if (r.type == NULL) {
        sb_append(&e->buf, "error: ");
        sb_append(&e->buf, r.error);
    } else {
        type_to_str(r.type, &e->buf);
    }
    return sb_cstr(&e->buf);
}

/// The same with level 0, as a declaration is written.
static inline const char* tenv_spell_decl(tenv_t* e, const char* text) {
    const type_build_t r = tenv_build(e, text);
    sb_clear(&e->buf);
    if (r.type == NULL) {
        sb_append(&e->buf, "error: ");
        sb_append(&e->buf, r.error);
    } else {
        type_to_str_decl(r.type, r.mut0, &e->buf);
    }
    return sb_cstr(&e->buf);
}

/// The spelling of any type, without level 0. Valid until the next call.
static inline const char* tenv_str(tenv_t* e, const type_t* t) {
    sb_clear(&e->buf);
    type_to_str(t, &e->buf);
    return sb_cstr(&e->buf);
}

/// The mutability of level 0 and of every further level of `text` as `y` and
/// `n`, level 0 first: `node* mut p` gives "yn" and `u8 mut@ mut* mut out`
/// "yyy".
/// D5.2, D5.3
static inline const char* tenv_muts(tenv_t* e, const char* text) {
    const type_build_t r = tenv_build(e, text);
    sb_clear(&e->buf);
    if (r.type == NULL) {
        sb_append(&e->buf, "error: ");
        sb_append(&e->buf, r.error);
        return sb_cstr(&e->buf);
    }
    sb_push(&e->buf, r.mut0 ? 'y' : 'n');
    const uint32_t levels = type_levels(r.type);
    for (uint32_t k = 1; k <= levels; k++) {
        sb_push(&e->buf, type_level_mut(r.type, k) ? 'y' : 'n');
    }
    return sb_cstr(&e->buf);
}

/// The `own` mark of every reference of `text`, the outermost first: `node
/// mut* own mut@ own kids` gives "yy" and `node* mut@ own items` "yn". A type
/// with no reference gives "".
/// D17.2
static inline const char* tenv_owns(tenv_t* e, const char* text) {
    const type_build_t r = tenv_build(e, text);
    sb_clear(&e->buf);
    if (r.type == NULL) {
        sb_append(&e->buf, "error: ");
        sb_append(&e->buf, r.error);
        return sb_cstr(&e->buf);
    }
    for (uint32_t k = 1; k <= TENV_MAX_SUFFIX; k++) {
        const type_t* ref = type_ref_at(r.type, k);
        if (ref == NULL) {
            break;
        }
        sb_push(&e->buf, ref->own ? 'y' : 'n');
    }
    return sb_cstr(&e->buf);
}

#endif
