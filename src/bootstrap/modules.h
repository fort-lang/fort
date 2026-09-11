// Module resolution of the bootstrap compiler (module-system.md 1, 2, 3, 6 and
// 10; D9.1 to D9.6, D9.10): the search roots, the two readings of an import
// path, the import closure of the entry file, the cycle rule and the
// dependency order the later passes walk.
//
// One source file is one module (D9.1); its module path is its file path
// relative to a search root with `/` replaced by `::` and `.ft` dropped. The
// roots are the directory containing the entry file, then each `-I`
// directory, then the standard library directory, which is the only place a
// path beginning with `std` is looked up and is never used for any other path
// (D9.2). A module's identity is the real path of its file, so one file
// reached through two module paths is an error (D9.2).
//
// module_set_load walks the closure depth first: it parses a module, collects
// its declarations into its namespace, then resolves its imports, so the
// modules land in module_set_at in dependency order, every imported module
// before its importers (D9.10). A module reached while it is still being
// walked closes a cycle, which is an error at the import that closes it
// (D9.5).
//
// Diagnostics follow D14.2 and the table of module-system.md 13; they are
// reported at the `import` keyword, or at 1:1 for the entry file's own name.
// The loader stops at the first error, so the diagnostics of one module come
// together as D14.2 requires; the caller asks diag_count whether anything was
// reported.
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants, candidate paths enumerated
// by index instead of through a callback.
#ifndef FORT_MODULES_H
#define FORT_MODULES_H

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "containers.h"
#include "scope.h"
#include "str.h"

// Where a module stands in the walk of the import closure (D9.5).
typedef enum {
    MODULE_LOADING, // on the current import path: reaching it again is a cycle
    MODULE_READY,   // its imports are resolved and it is in the dependency order
} module_state_t;

typedef struct {
    str_t path;      // the module path, `std::io` (D9.1)
    str_t file;      // the file as the compiler opened it, the `<file>` of D14.2
    str_t real;      // the real path: the module's identity (D9.2)
    str_t source;    // the file's bytes, which the tree's names point into
    ast_node_t* ast; // the AST_MODULE, complete only when `parsed`
    scope_t names;   // the module namespace (D7.9)
    bool parsed;     // no diagnostic was reported while lexing and parsing it
    bool entry;      // the module named on the command line (D14.1)
    module_state_t state;
} module_t;

typedef struct {
    ptrvec_t modules;  // module_t*, in the order they were read, owned
    ptrvec_t order;    // module_t*, an imported module before its importers (D9.10)
    ptrvec_t stack;    // module_t*, the import path being walked, for cycles (D9.5)
    ptrvec_t roots;    // the `-I` roots, interned, in command-line order (D9.2)
    strmap_t by_path;  // module path -> position in `modules`
    strmap_t by_real;  // real path -> position in `modules`, the identity map (D9.2)
    str_t entry_dir;   // the first root: the directory containing the entry file
    str_t std_dir;     // the standard library directory, the last root
    str_pool_t pool;   // owns every string above and every file name a loc_t holds
    ast_arena_t arena; // owns every module's nodes until module_set_free
    sb_t msg;          // the message builder of diag.h
    bool stopped;      // a module has errors: read no further file (D14.2)
} module_set_t;

void module_set_init(module_set_t* set);

// Releases every module, its namespace and the shared arena; the set is empty
// and usable afterwards.
void module_set_free(module_set_t* set);

// Appends a `-I` search root; the roots are searched in the order they were
// added, after the entry file's directory (D9.2). The string is copied.
void module_set_add_root(module_set_t* set, const char* dir);

// Sets the standard library directory, the only root a path beginning with
// `std` is looked up in (D9.2). The string is copied; without it no such path
// resolves.
void module_set_std_dir(module_set_t* set, const char* dir);

// Reads `entry`, derives its module path from its base name and walks the
// import closure (module-system.md 2, 10). Returns false after reporting a
// diagnostic: an unreadable file, a lexical or syntax error, or an import
// error of module-system.md 13 that a namespace answers -- not every row of
// that table is the loader's, since two `extern` declarations of one C symbol
// are compared as types and so in the checker (D9.8).
bool module_set_load(module_set_t* set, const char* entry);

// The modules of the closure in dependency order, every module after the ones
// it imports (D9.10).
uint64_t module_set_count(const module_set_t* set);
const module_t* module_set_at(const module_set_t* set, uint64_t i);

// The modules a whole-closure pass visits, in the order it must visit them
// (D9.10, D14.2): the dependency order above first, then every other module
// the loader read that parsed -- one whose own import failed, or an importer
// of a file that did not parse -- in the reverse of the read order, which is
// depth first, so an imported module comes before its importer in that group
// too. The checker walks this order and so does the index walk, which is what
// makes the file order of the index the order D20.3 documents; the two may not
// drift, so neither builds an order of its own.
uint64_t module_set_pass_count(const module_set_t* set);
const module_t* module_set_pass_at(const module_set_t* set, uint64_t i);

// Whether the loader put `m` in the dependency order, as opposed to the
// modules the pass order visits after it (D9.5, D14.2).
bool module_set_is_ordered(const module_set_t* set, const module_t* m);

// The files of every module the loader read, in the order it read them,
// whether or not the module parsed and whether or not the closure is
// complete: the "files" array of the check mode's document, which tells a
// client which files it may clear stale diagnostics for (D20.2). A file that
// could not be read is not among them.
uint64_t module_set_file_count(const module_set_t* set);
str_t module_set_file_at(const module_set_t* set, uint64_t i);

// The module of the given path, or NULL when the closure holds none.
const module_t* module_set_find(const module_set_t* set, str_t path);

// The entry module, or NULL before module_set_load has read it.
const module_t* module_set_entry(const module_set_t* set);

#endif
