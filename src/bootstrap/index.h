// The identifier index of the check mode (D20.3, toolchain.md 9.1): one
// record per identifier occurrence the checker resolved, which is what an
// editor answers hover and go-to-definition from.
//
// The walk reads the `sym` annotation of every node of every module that was
// checked, so it runs after the front end and before the analysis is freed: a
// record lives as long as the checker that made it (sym.h). What it produces
// borrows from the trees too -- a name is a view into a module's source and
// every file name is the module set's -- and owns only the type spellings it
// builds, so the whole index dies with the run that made it.
//
// Records come out ordered by file, an imported module before its importers
// (D9.10), and within a file by the start of the occurrence, which is the
// order of D20.3.
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants, a plain record struct and an
// explicit vector of them.
#ifndef FORT_INDEX_H
#define FORT_INDEX_H

#include <stdbool.h>
#include <stdint.h>

#include "containers.h"
#include "diag.h"
#include "json.h"
#include "modules.h"
#include "str.h"
#include "sym.h"

// One occurrence of one name (D20.3). `loc` is that name token's own range
// and never the construct's first token (D20.4); `name` is the identifier as
// it is spelled here, so an `as` alias reads as the alias (D9.3); `type` is
// what a declaration of it would spell (D5.2), empty for a name that denotes
// no value type and absent, which `has_type` says, when the declaration
// failed to check; `is_decl` marks an occurrence that declares the name in
// this module, the `as` alias of an import included; `decl` is the declaring
// name's range, which `has_decl` denies to a builtin alone, since no source
// declares one (D12.2).
typedef struct {
    loc_t loc;
    str_t name;
    sym_kind_t kind;
    str_t type;
    bool has_type;
    bool is_decl;
    bool has_decl;
    loc_t decl;
} index_entry_t;

// The records of one run. Zero-initialized storage is not one: index_init
// prepares it.
typedef struct {
    ptrvec_t entries; // index_entry_t*, owned, in the order of D20.3
    str_pool_t pool;  // owns every type spelling
    sb_t msg;         // the buffer a spelling is built in
} index_t;

void index_init(index_t* ix);

// Releases every record and every spelling; the index is empty and usable
// afterwards.
void index_free(index_t* ix);

// Walks every module of `set` the checker checked and records what it
// resolved (D20.3). The set and the checker that annotated it must both still
// be alive, since every record is read out of an annotation (sym.h); the
// records are appended to whatever the index already holds.
void index_build(index_t* ix, const module_set_t* set);

// The records, in the order of D20.3. `i` past the end is an internal error.
uint64_t index_count(const index_t* ix);
const index_entry_t* index_at(const index_t* ix, uint64_t i);

// Writes the records as the `"symbols"` array of the document, the key
// already written by the caller (D20.2, D20.3).
void index_write_json(const index_t* ix, json_t* j);

#endif
