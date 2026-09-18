// Builds ordered identifier records from checked syntax trees.
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

// One occurrence of one name. `loc` is the name token's range, not the construct's first token.
// `name` is the identifier spelling at this occurrence. Thus, an `as` alias uses the alias
// spelling. `type` is the declaration type spelling. It is empty when the name has no value type.
// `has_type` is false when the declaration failed to check. `is_decl` marks a declaration in this
// module, including an import alias. `decl` is the declaring name's range. `has_decl` is false only
// for builtins, which have no source declaration.
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
    ptrvec_t entries; // index_entry_t*, owned, in that order
    str_pool_t pool;  // owns every type spelling
    sb_t msg;         // the buffer a spelling is built in
} index_t;

void index_init(index_t* ix);

// Releases every record and every spelling. The index is empty and usable
// afterwards.
void index_free(index_t* ix);

// Walks every module of `set` the checker checked and records what it resolved.
// The set and the checker that annotated it must both still be alive, since every
// record is read out of an annotation (sym.h). The records are appended to
// whatever the index already holds.
void index_build(index_t* ix, const module_set_t* set);

// The records, in the index's own order. `i` past the end is an internal
// error.
uint64_t index_count(const index_t* ix);
const index_entry_t* index_at(const index_t* ix, uint64_t i);

// Writes the records as the `"symbols"` array of the document, the key already
// written by the caller.
void index_write_json(const index_t* ix, json_t* j);

#endif
