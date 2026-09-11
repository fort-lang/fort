// Namespaces and scopes of the bootstrap compiler (module-system.md 5, D7.9):
// the module namespace holding a file's declarations and import bindings, the
// block scopes nested inside it, and the universe of D12.2.
//
// One namespace per module holds functions, extern functions, structs, enums,
// constants, globals and import bindings, and any two of them with the same
// name collide, whatever their kinds (D7.9). Lookup of an unqualified name
// goes from the innermost block outward, then the module namespace, then the
// universe; a local may shadow a module-level or universe name but never an
// enclosing local or parameter, which is why the block scopes are searched
// apart from the module namespace (scope_find_in_blocks).
//
// Enum members are not in a namespace (D3.9): `color.red` is the only
// spelling, so a scope never holds one.
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants, a strmap from the name to
// the position of the binding in a vector the scope owns.
#ifndef FORT_SCOPE_H
#define FORT_SCOPE_H

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "containers.h"
#include "diag.h"
#include "str.h"

// What a bound name denotes. The module-level kinds are the importable
// declarations of module-system.md 3; BIND_MODULE and BIND_SYMBOL are the two
// import bindings, which are not importable in turn since there is no
// re-export (D9.3).
typedef enum {
    BIND_NONE = 0,   // never stored; the kind of a binding that was not found
    BIND_FN,         // `fn` (D8.1)
    BIND_EXTERN_FN,  // `extern fn` (D9.8)
    BIND_STRUCT,     // `struct` (D3.8)
    BIND_ENUM,       // `enum` (D3.9)
    BIND_VAR,        // a module-level constant or `mut` global (D7.10)
    BIND_MODULE,     // `import a::b;`: the name denotes a module (D9.3)
    BIND_SYMBOL,     // `import a::b::c;`: the name denotes a declaration of a::b
    BIND_LOCAL,      // a local of a block (D7.1)
    BIND_PARAM,      // a parameter (D8.1)
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
// scope has one (D7.9).
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

// Binds `name` in `s` and returns the new binding, or NULL when `s` already
// binds the name: any two entries of one namespace collide whatever their
// kinds (D7.9), and the caller reports the collision against scope_find.
binding_t* scope_declare(
    scope_t* s, str_t name, bind_kind_t kind, loc_t loc, const ast_node_t* node);

// The binding of `name` in `s` alone, or NULL.
const binding_t* scope_find(const scope_t* s, str_t name);

// The binding of `name` in `s`, then in every enclosing scope (D7.9 steps 1
// and 2), or NULL. The universe of step 3 is scope_is_universe.
const binding_t* scope_lookup(const scope_t* s, str_t name);

// The binding of `name` in `s` and its enclosing block scopes, stopping
// before the module namespace: the locals and parameters a new local may not
// reuse the name of (D7.9), as opposed to the module-level names it may
// shadow.
//
// A scope holds every binding declared in it so far, so D7.9's other rule,
// that a local is visible only after its own declaration (`i32 x = x;` is an
// error), is the caller's: declare the local after checking its initializer.
const binding_t* scope_find_in_blocks(const scope_t* s, str_t name);

// The bindings of `s` in declaration order.
uint64_t scope_count(const scope_t* s);
const binding_t* scope_at(const scope_t* s, uint64_t i);

// Whether the binding denotes a declaration, as opposed to one of the two
// import bindings: what `import a::b::c;` needs of `c` for its symbol
// reading, since import bindings are not re-exported (D9.3).
bool bind_is_declaration(const binding_t* b);

// ---- the universe (D12.2) ---------------------------------------------------------

// The builtins of D12.2, looked up after every scope: `del`, `move`,
// `assert`, `panic`, `print`, `println`, `eprint`, `eprintln`, `fprint` and
// `fprintln`.
enum { UNIVERSE_COUNT = 10 };

// Whether `name` is a universe name. A module-level declaration or a local
// may shadow one, which is then inaccessible in that scope (D7.9), so this is
// a question and never an error on its own.
bool scope_is_universe(str_t name);

// The `i`-th universe name, in the order of D12.2; `i` must be less than
// UNIVERSE_COUNT.
str_t scope_universe_at(uint64_t i);

#endif
