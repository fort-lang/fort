// The identifier index of the check mode (D20.3); see index.h.
#include "index.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "containers.h"
#include "diag.h"
#include "json.h"
#include "modules.h"
#include "str.h"
#include "sym.h"
#include "types.h"

void index_init(index_t* ix) {
    ptrvec_init(&ix->entries);
    str_pool_init(&ix->pool);
    sb_init(&ix->msg);
}

void index_free(index_t* ix) {
    for (uint64_t i = 0; i < ix->entries.len; i++) {
        mem_free(ix->entries.items[i]);
    }
    ptrvec_free(&ix->entries);
    str_pool_free(&ix->pool);
    sb_free(&ix->msg);
}

uint64_t index_count(const index_t* ix) {
    return ix->entries.len;
}

const index_entry_t* index_at(const index_t* ix, uint64_t i) {
    if (i >= ix->entries.len) {
        fatal_internal("index_at: index out of range");
    }
    return (const index_entry_t*)ix->entries.items[i];
}

// ---- one record (D20.3) -----------------------------------------------------------

// The type of the name as a declaration of it would spell it, level-0
// mutability included (D5.2, D5.3). It is empty for a name that denotes no
// value -- a module, a struct name, an enum name, a builtin -- and for a
// declaration that failed to check, whose type is the poison of D14.2 and
// says nothing a reader wants (D20.3).
static str_t type_spelling(index_t* ix, const sym_t* s) {
    if (s->type == NULL || s->error || s->kind == SYM_MODULE || s->kind == SYM_STRUCT ||
        s->kind == SYM_ENUM) {
        return str_from_cstr("");
    }
    sb_clear(&ix->msg);
    type_to_str_decl(s->type, s->mut0, &ix->msg);
    return str_pool_intern(&ix->pool, sb_view(&ix->msg));
}

// Where the declaration of the name is. A module is declared by a file and
// has no name token, so it is the empty range at 1:1 of that file, which is
// D14.2's position for what has none, and go-to-definition on a module
// qualifier opens the module (D9.1, D20.3).
static loc_t declaration_range(const sym_t* s) {
    if (s->decl.file != NULL) {
        return s->decl;
    }
    return loc_make(s->node->loc.file, 1, 1);
}

// Whether `a` begins before `b`: line, then byte column (D20.4).
static bool starts_before(loc_t a, loc_t b) {
    if (a.line != b.line) {
        return a.line < b.line;
    }
    return a.col < b.col;
}

static void record(index_t* ix, const ast_node_t* n) {
    const sym_t* s = n->sym;
    index_entry_t* e = mem_alloc((uint64_t)sizeof(index_entry_t));
    // The occurrence's own name token, never the construct's first one
    // (D20.4), so an editor underlines the name and nothing else.
    e->loc = n->name_loc;
    e->name = n->name;
    e->kind = s->kind;
    e->type = type_spelling(ix, s);
    // `sym->node` is the declaring node, so the declaration is the one
    // occurrence that stands on it (sym.h).
    e->is_decl = s->node == n;
    // A builtin is declared by no source, so it has no declaration range
    // (D12.2, D20.3).
    e->has_decl = s->node != NULL;
    e->decl = e->has_decl ? declaration_range(s) : loc_make(NULL, 1, 1);
    ptrvec_push(&ix->entries, e);
}

// ---- the walk ---------------------------------------------------------------------

static void walk(index_t* ix, const ast_node_t* n) {
    if (n == NULL) {
        return;
    }
    // A node with a symbol but no name token of its own is not an occurrence
    // of anything: the module node carries the module's record, and the
    // import node the binding's, while the name a reader sees is on the path
    // segment or on the alias beside them (D20.3, sym.h).
    if (n->sym != NULL && n->name_loc.file != NULL) {
        record(ix, n);
    }
    walk(ix, n->a);
    walk(ix, n->b);
    walk(ix, n->c);
    walk(ix, n->d);
    for (uint64_t i = 0; i < ast_len(n); i++) {
        walk(ix, ast_child(n, i));
    }
}

// Orders the records added since `start` by the start of the occurrence
// (D20.3). The walk visits a parent before its children, so a record is
// almost always in place already and only a `.` or an import path moves one
// back; insertion sort pays for the moves it makes and nothing more, and
// keeps the walk's order between two records at one position.
static void sort_from(index_t* ix, uint64_t start) {
    for (uint64_t i = start + 1; i < ix->entries.len; i++) {
        index_entry_t* e = (index_entry_t*)ix->entries.items[i];
        uint64_t j = i;
        while (j > start &&
               starts_before(e->loc, ((index_entry_t*)ix->entries.items[j - 1])->loc)) {
            ix->entries.items[j] = ix->entries.items[j - 1];
            j--;
        }
        ix->entries.items[j] = e;
    }
}

// One file's records, grouped and ordered (D20.3). A module that did not
// parse was not checked either, so it has no annotation to read (D14.2).
static void index_module(index_t* ix, const module_t* m) {
    if (!m->parsed || m->ast == NULL) {
        return;
    }
    const uint64_t start = ix->entries.len;
    walk(ix, m->ast);
    sort_from(ix, start);
}

// Whether the loader put the module in the dependency order, which is where
// the walk starts; check_program asks the same question the same way before
// it checks the modules the walk never ordered.
static bool in_dependency_order(const module_set_t* set, const module_t* m) {
    for (uint64_t i = 0; i < module_set_count(set); i++) {
        if (module_set_at(set, i) == m) {
            return true;
        }
    }
    return false;
}

void index_build(index_t* ix, const module_set_t* set) {
    // The dependency order first, an imported module before its importers
    // (D9.10, D20.3).
    for (uint64_t i = 0; i < module_set_count(set); i++) {
        index_module(ix, module_set_at(set, i));
    }
    // Then the modules the loader read but never ordered -- one whose own
    // import failed, or an importer of a file that did not parse -- which the
    // checker checked all the same, so that a file being edited is indexed
    // whatever its imports do (D14.2, D20.1). They were read depth first, so
    // the reverse of the read order puts an imported module first here too.
    for (uint64_t i = set->modules.len; i > 0; i--) {
        const module_t* m = (const module_t*)set->modules.items[i - 1];
        if (!in_dependency_order(set, m)) {
            index_module(ix, m);
        }
    }
}

// ---- the document (D20.2, D20.3) --------------------------------------------------

// A range as a diagnostic's is written: the file and the 1-based byte columns
// of both ends, the end exclusive (D20.2, D20.4).
static void write_range(json_t* j, loc_t loc) {
    json_key(j, "file");
    json_cstr(j, loc.file != NULL ? loc.file : "");
    json_key(j, "line");
    json_uint(j, loc.line);
    json_key(j, "col");
    json_uint(j, loc.col);
    json_key(j, "end_line");
    json_uint(j, loc.end_line);
    json_key(j, "end_col");
    json_uint(j, loc.end_col);
}

void index_write_json(const index_t* ix, json_t* j) {
    json_array_begin(j);
    for (uint64_t i = 0; i < ix->entries.len; i++) {
        const index_entry_t* e = index_at(ix, i);
        json_object_begin(j);
        write_range(j, e->loc);
        json_key(j, "name");
        json_str(j, e->name);
        // The kind as a diagnostic spells it: "enum member", "parameter"
        // (D20.3).
        json_key(j, "kind");
        json_cstr(j, sym_kind_name(e->kind));
        json_key(j, "type");
        json_str(j, e->type);
        json_key(j, "is_decl");
        json_bool(j, e->is_decl);
        json_key(j, "decl");
        if (e->has_decl) {
            json_object_begin(j);
            write_range(j, e->decl);
            json_object_end(j);
        } else {
            // A builtin has no declaration to jump to (D12.2, D20.3).
            json_null(j);
        }
        json_object_end(j);
    }
    json_array_end(j);
}
