// Namespaces, scopes and the universe; see scope.h.
#include "scope.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "containers.h"
#include "diag.h"
#include "str.h"

// The universe names, in lookup order.
static const char* const UNIVERSE_NAMES[UNIVERSE_COUNT] = {
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

void scope_init(scope_t* s, scope_kind_t kind, scope_t* parent) {
    s->kind = kind;
    s->parent = parent;
    strmap_init(&s->index);
    ptrvec_init(&s->items);
}

void scope_free(scope_t* s) {
    for (uint64_t i = 0; i < s->items.len; i++) {
        mem_free(s->items.items[i]);
    }
    ptrvec_free(&s->items);
    strmap_free(&s->index);
}

binding_t* scope_declare(
    scope_t* s, str_t name, bind_kind_t kind, loc_t loc, const ast_node_t* node) {
    // a struct `node` and a function `node` cannot coexist
    if (strmap_has(&s->index, name)) {
        return NULL;
    }
    binding_t* b = mem_alloc((uint64_t)sizeof(binding_t));
    b->name = name;
    b->kind = kind;
    b->loc = loc;
    b->node = node;
    b->module = NULL;
    b->to = NULL;
    (void)strmap_put(&s->index, b->name, (int64_t)s->items.len);
    ptrvec_push(&s->items, b);
    return b;
}

const binding_t* scope_find(const scope_t* s, str_t name) {
    int64_t at = 0;
    if (!strmap_get(&s->index, name, &at)) {
        return NULL;
    }
    return (const binding_t*)s->items.items[at];
}

// Lookup goes from the innermost block outward, then through the module namespace.
// The caller handles the universe.
const binding_t* scope_lookup(const scope_t* s, str_t name) {
    const scope_t* cur = s;
    while (cur != NULL) {
        const binding_t* found = scope_find(cur, name);
        if (found != NULL) {
            return found;
        }
        cur = cur->parent;
    }
    return NULL;
}

// A local or parameter cannot reuse an enclosing local or parameter name. It can
// shadow a module name, so the walk stops before the module namespace.
const binding_t* scope_find_in_blocks(const scope_t* s, str_t name) {
    const scope_t* cur = s;
    while (cur != NULL && cur->kind == SCOPE_BLOCK) {
        const binding_t* found = scope_find(cur, name);
        if (found != NULL) {
            return found;
        }
        cur = cur->parent;
    }
    return NULL;
}

uint64_t scope_count(const scope_t* s) {
    return s->items.len;
}

const binding_t* scope_at(const scope_t* s, uint64_t i) {
    if (i >= s->items.len) {
        fatal_internal("scope_at: index out of range");
    }
    return (const binding_t*)s->items.items[i];
}

// The import bindings of another module are not importable; there is no
// re-export.
bool bind_is_declaration(const binding_t* b) {
    switch (b->kind) {
    case BIND_FN:
    case BIND_EXTERN_FN:
    case BIND_STRUCT:
    case BIND_ENUM:
    case BIND_VAR:
        return true;
    case BIND_NONE:
    case BIND_MODULE:
    case BIND_SYMBOL:
    case BIND_LOCAL:
    case BIND_PARAM:
    case BIND_KIND_COUNT:
        break;
    }
    return false;
}

bool scope_is_universe(str_t name) {
    for (uint64_t i = 0; i < (uint64_t)UNIVERSE_COUNT; i++) {
        if (str_eq(name, str_from_cstr(UNIVERSE_NAMES[i]))) {
            return true;
        }
    }
    return false;
}

str_t scope_universe_at(uint64_t i) {
    if (i >= (uint64_t)UNIVERSE_COUNT) {
        fatal_internal("scope_universe_at: index out of range");
    }
    return str_from_cstr(UNIVERSE_NAMES[i]);
}
