// Stores module namespaces, block scopes, and built-in names.
// Lookup searches blocks, the module, and then the universe.
#ifndef FORT_SCOPE_H
#define FORT_SCOPE_H

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "containers.h"
#include "diag.h"
#include "str.h"

// Identifies what a bound name denotes.
// Import bindings do not become exports of the importing module.
typedef enum {
    BIND_NONE = 0,   // never stored; the kind of a binding that was not found
    BIND_FN,         // `fn`
    BIND_EXTERN_FN,  // `extern fn`
    BIND_STRUCT,     // `struct`
    BIND_ENUM,       // `enum`
    BIND_VAR,        // a module-level constant or `mut` global
    BIND_MODULE,     // `import a.b;`: the name denotes a module
    BIND_SYMBOL,     // `import a.b.c;`: the name denotes a declaration of a.b
    BIND_LOCAL,      // a local of a block
    BIND_PARAM,      // a parameter
    BIND_KIND_COUNT, // one past the last kind, for tables
} bind_kind_t;

typedef struct binding binding_t;
struct binding {
    str_t name;
    bind_kind_t kind;
    loc_t loc;              // where the name is declared or bound
    const ast_node_t* node; // the declaring or importing node
    const void* module;     // BIND_MODULE: the module the name denotes, opaque here
    const binding_t* to;    // BIND_SYMBOL: the declaration in the other module
};

// A namespace or a block: the module namespace has no parent, every block
// scope has one.
typedef enum {
    SCOPE_MODULE,
    SCOPE_BLOCK,
} scope_kind_t;

typedef struct scope scope_t;
struct scope {
    scope_kind_t kind;
    scope_t* parent; // NULL for a module namespace
    strmap_t index;  // name -> position in `items`
    ptrvec_t items;  // binding_t*, in declaration order, owned by the scope
};

// Zero-initialized storage is not a valid scope: scope_init sets the kind and
// the parent, which NULL makes a module namespace.
void scope_init(scope_t* s, scope_kind_t kind, scope_t* parent);

// Releases every binding and the index; the scope is empty and usable
// afterwards.
void scope_free(scope_t* s);

// Binds `name` in `s` and returns the new binding. Returns NULL when `s`
// already binds the name. Entries in one namespace collide regardless of kind.
// The caller reports the collision against scope_find.
binding_t* scope_declare(
    scope_t* s, str_t name, bind_kind_t kind, loc_t loc, const ast_node_t* node);

// The binding of `name` in `s` alone, or NULL.
const binding_t* scope_find(const scope_t* s, str_t name);

// The binding of `name` in `s`, then in every enclosing scope, or NULL.
// scope_is_universe identifies the universe.
const binding_t* scope_lookup(const scope_t* s, str_t name);

// Finds `name` in `s` and its enclosing block scopes. The search stops before
// the module namespace. A new local cannot reuse these local and parameter
// names. It can shadow module names.
//
// A scope holds each binding declared in it so far. The caller enforces
// visibility after declaration. It declares a local after checking its
// initializer. Thus, `i32 x = x;` is an error.
const binding_t* scope_find_in_blocks(const scope_t* s, str_t name);

// The bindings of `s` in declaration order.
uint64_t scope_count(const scope_t* s);
const binding_t* scope_at(const scope_t* s, uint64_t i);

// Whether the binding denotes a declaration instead of an import binding.
// `import a.b.c;` requires `c` to name a declaration. Import bindings are not
// re-exported.
bool bind_is_declaration(const binding_t* b);

// ---- the universe -----------------------------------------------------------------

// The builtins, looked up after every scope: `del`, `move`, `assert`, `panic`,
// `print`, `println`, `eprint`, `eprintln`, `fprint`, and `fprintln`.
enum { UNIVERSE_COUNT = 10 };

// Whether `name` is a universe name. A module declaration or local can shadow
// one. The universe name is then inaccessible in that scope. This query does
// not report an error.
bool scope_is_universe(str_t name);

// The `i`-th universe name. `i` must be less than UNIVERSE_COUNT.
str_t scope_universe_at(uint64_t i);

#endif
