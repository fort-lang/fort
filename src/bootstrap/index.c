// The identifier index of the check mode; see index.h.
// D20.3
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

// ---- one record -------------------------------------------------------------------
// D20.3

// Whether the name denotes something with a type to show. A module, a struct
// name, an enum name and a builtin denote no value type, so their records
// carry the empty spelling.
// D20.3
static bool has_value_type(const sym_t* s) {
    return s->type != NULL && s->kind != SYM_MODULE && s->kind != SYM_STRUCT && s->kind != SYM_ENUM;
}

// The type of the name as a declaration of it would spell it, level-0 mutability
// included. A declaration that failed to check has a poisoned type, which says
// nothing a reader wants, so its record carries no type at all rather than the
// empty spelling of a name that has none: the two cases are told apart by the
// client.
// D5.2, D5.3, D14.2, D20.3
static str_t type_spelling(index_t* ix, const sym_t* s) {
    if (!has_value_type(s)) {
        return str_from_cstr("");
    }
    sb_clear(&ix->msg);
    type_to_str_decl(s->type, s->mut0, &ix->msg);
    return str_pool_intern(&ix->pool, sb_view(&ix->msg));
}

// Where the declaration of the name is. A module is declared by a file and has
// no name token, so it is the empty range at 1:1 of that file, which is the
// position for what has none, and go-to-definition on a module qualifier opens
// the module.
// D9.1, D14.2, D20.3
static loc_t declaration_range(const sym_t* s) {
    if (s->decl.file != NULL) {
        return s->decl;
    }
    return loc_make(s->node->loc.file, 1, 1);
}

// Whether `a` begins before `b`: line, then byte column.
// D20.4
static bool starts_before(loc_t a, loc_t b) {
    if (a.line != b.line) {
        return a.line < b.line;
    }
    return a.col < b.col;
}

// Whether the occurrence is the `as` alias of an import, which declares that
// name in this module while the declaration it binds stands elsewhere: the alias
// of a whole-module import hangs on the import node and the alias of an item on
// the item. An import without an alias introduces the name its declaration
// already has, so it is a use.
// D9.3, D20.3
static bool is_import_alias(const ast_node_t* parent, const ast_node_t* n) {
    if (parent == NULL) {
        return false;
    }
    if (parent->kind == AST_IMPORT) {
        return n == parent->b;
    }
    if (parent->kind == AST_IMPORT_ITEM) {
        return n == parent->a;
    }
    return false;
}

static void record(index_t* ix, const ast_node_t* n, const ast_node_t* parent) {
    const sym_t* s = n->sym;
    index_entry_t* e = mem_alloc((uint64_t)sizeof(index_entry_t));
    // D20.4: the name token, never the construct's first one
    e->loc = n->name_loc;
    e->name = n->name;
    e->kind = s->kind;
    e->type = type_spelling(ix, s);
    // D20.3
    e->has_type = !s->error;
    // `sym->node` is the declaring node, so the declaration is the occurrence
    // that stands on it (sym.h), and an alias declares its own name here.
    e->is_decl = s->node == n || is_import_alias(parent, n);
    // D12.2, D20.3: a builtin is declared by no source
    e->has_decl = s->node != NULL;
    e->decl = e->has_decl ? declaration_range(s) : loc_make(NULL, 1, 1);
    ptrvec_push(&ix->entries, e);
}

// ---- the walk ---------------------------------------------------------------------

// `parent` is the node this one hangs on, which is what tells an `as` alias
// from an ordinary occurrence; it is NULL at the module node.
// D9.3
static void walk(index_t* ix, const ast_node_t* n, const ast_node_t* parent) {
    if (n == NULL) {
        return;
    }
    // A node with a symbol but no name token of its own is not an occurrence of
    // anything: the module node carries the module's record, and the import node the
    // binding's, while the name a reader sees is on the path segment or on the alias
    // beside them (sym.h).
    // D20.3
    if (n->sym != NULL && n->name_loc.file != NULL) {
        record(ix, n, parent);
    }
    walk(ix, n->a, n);
    walk(ix, n->b, n);
    walk(ix, n->c, n);
    walk(ix, n->d, n);
    for (uint64_t i = 0; i < ast_len(n); i++) {
        walk(ix, ast_child(n, i), n);
    }
}

// Orders the records added since `start` by the start of the occurrence. The
// walk visits a parent before its children, so a record is almost always in place
// already and only a `.` or an import path moves one back; insertion sort pays
// for the moves it makes and nothing more, and keeps the walk's order between two
// records at one position.
// D20.3
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

// One file's records, grouped and ordered. A module that did not parse was not
// checked either, so it has no annotation to read.
// D20.3, D14.2
static void index_module(index_t* ix, const module_t* m) {
    if (!m->parsed || m->ast == NULL) {
        return;
    }
    const uint64_t start = ix->entries.len;
    walk(ix, m->ast, NULL);
    sort_from(ix, start);
}

void index_build(index_t* ix, const module_set_t* set) {
    // The pass order of modules.h, which is the order the checker used: an imported
    // module before its importers, then the modules the loader read but never
    // ordered, so a file being edited is indexed whatever its imports do. The index's
    // file order is that order, so it is read from one place and not rebuilt here.
    // D9.10, D14.2, D20.1, D20.3
    for (uint64_t i = 0; i < module_set_pass_count(set); i++) {
        index_module(ix, module_set_pass_at(set, i));
    }
}

// ---- the document -----------------------------------------------------------------
// D20.2, D20.3

// A range as a diagnostic's is written: the file and the 1-based byte columns
// of both ends, the end exclusive.
// D20.2, D20.4
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
        // D20.3: the kind as a diagnostic spells it, "enum member"
        json_key(j, "kind");
        json_cstr(j, sym_kind_name(e->kind));
        json_key(j, "type");
        if (e->has_type) {
            json_str(j, e->type);
        } else {
            // D14.2, D20.3: no type to show, so the client renders it unknown
            json_null(j);
        }
        json_key(j, "is_decl");
        json_bool(j, e->is_decl);
        json_key(j, "decl");
        if (e->has_decl) {
            json_object_begin(j);
            write_range(j, e->decl);
            json_object_end(j);
        } else {
            // D12.2, D20.3
            json_null(j);
        }
        json_object_end(j);
    }
    json_array_end(j);
}
