// Loads modules and resolves import closures.
// Modules appear in dependency order, with each import before its importer.
// Search roots include the entry directory, `-I` roots, and the standard library.
#ifndef FORT_MODULES_H
#define FORT_MODULES_H

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "containers.h"
#include "scope.h"
#include "str.h"

// Where a module stands in the walk of the import closure.
typedef enum {
    MODULE_LOADING, // on the current import path: reaching it again is a cycle
    MODULE_READY,   // its imports are resolved and it is in the dependency order
} module_state_t;

typedef struct {
    str_t path;      // the module path, `std.io`
    str_t file;      // the file as the compiler opened it, its `<file>`
    str_t real;      // the real path: the module's identity
    str_t source;    // the file's bytes, which the tree's names point into
    ast_node_t* ast; // the AST_MODULE, complete only when `parsed`
    scope_t names;   // the module namespace
    bool parsed;     // no diagnostic was reported while lexing and parsing it
    bool entry;      // the module named on the command line
    module_state_t state;
} module_t;

typedef struct {
    ptrvec_t modules;  // module_t*, in the order they were read, owned
    ptrvec_t order;    // module_t*, an imported module before its importers
    ptrvec_t stack;    // module_t*, the import path being walked, for cycles
    ptrvec_t roots;    // the `-I` roots, interned, in command-line order
    strmap_t by_path;  // module path -> position in `modules`
    strmap_t by_real;  // real path -> position in `modules`, the identity map
    str_t entry_dir;   // the first root: the directory containing the entry file
    str_t std_dir;     // the standard library directory, the last root
    str_pool_t pool;   // owns every string above and every file name a loc_t holds
    ast_arena_t arena; // owns every module's nodes until module_set_free
    sb_t msg;          // the message builder of diag.h
    bool stopped;      // a module has errors: read no further file
} module_set_t;

void module_set_init(module_set_t* set);

// Releases every module, its namespace and the shared arena; the set is empty
// and usable afterwards.
void module_set_free(module_set_t* set);

// Appends a `-I` search root; the roots are searched in the order they were
// added, after the entry file's directory. The string is copied.
void module_set_add_root(module_set_t* set, const char* dir);

// Sets the standard library directory, the only root a path beginning with
// `std` is looked up in. The string is copied; without it no such path
// resolves.
void module_set_std_dir(module_set_t* set, const char* dir);

// Reads `entry` and its import closure.
// Returns false after a read, parse, selection, or import diagnostic.
bool module_set_load(module_set_t* set, const char* entry);

// The modules of the closure in dependency order, every module after the ones
// it imports.
uint64_t module_set_count(const module_set_t* set);
const module_t* module_set_at(const module_set_t* set, uint64_t i);

// Counts modules in whole-closure pass order. Dependency-ordered modules come
// first. Other parsed modules follow in reverse read order. This order puts
// imports before importers in both groups. The checker and index walk use it.
uint64_t module_set_pass_count(const module_set_t* set);
const module_t* module_set_pass_at(const module_set_t* set, uint64_t i);

// Whether the loader put `m` in the dependency order, as opposed to the
// modules the pass order visits after it.
bool module_set_is_ordered(const module_set_t* set, const module_t* m);

// Counts readable module files in loader read order. The count includes files
// that did not parse or enter the completed closure. These files form the
// check-mode document's "files" array. A client can clear stale diagnostics for
// them. The count excludes files that could not be read.
uint64_t module_set_file_count(const module_set_t* set);
str_t module_set_file_at(const module_set_t* set, uint64_t i);

// Returns the read module of `path`, or NULL when no read module has that path.
// The module can have failed parsing and can be outside the completed closure.
const module_t* module_set_find(const module_set_t* set, str_t path);

// The entry module, or NULL before module_set_load has read it.
const module_t* module_set_entry(const module_set_t* set);

#endif
