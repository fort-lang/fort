// The environment the checker suites share: the module sandbox of
// modules_helpers.h, a checker over the closure it loads, and small queries
// over the symbols and the annotations the checker wrote.
//
// The helpers are static inline and the state is per suite, so a suite that
// uses only some of them still builds under -Werror.
#ifndef FORT_TEST_CHECK_HELPERS_H
#define FORT_TEST_CHECK_HELPERS_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "check.h"
#include "consts.h"
#include "modules.h"
#include "modules_helpers.h"
#include "scope.h"
#include "str.h"
#include "sym.h"
#include "types.h"

#include "test.h"

static check_t checker;
static bool checker_live = false;
// Options of the next check, applied and reset by check_entry so that a test
// states the ones it needs and leaves the next test the defaults.
static bool want_main = true;
static bool want_mute = false;

// Releases the checker of the previous test. The symbols point into the
// previous sandbox's tree, which `begin` has already released, and check_free
// only frees its own memory, so the order is safe.
static inline void check_reset(void) {
    if (checker_live) {
        check_free(&checker);
        checker_live = false;
    }
}

// Loads the closure of the sandbox module `rel` and checks it; false when the
// load or the check reported a diagnostic.
static inline bool check_entry(const char* rel) {
    check_reset();
    if (!load(rel)) {
        return false;
    }
    check_init(&checker);
    checker_live = true;
    checker.require_main = want_main;
    checker.mute = want_mute;
    want_main = true;
    want_mute = false;
    return check_program(&checker, &set);
}

// Writes `text` as the entry module `main.ft` and checks it.
static inline bool check_src(const char* text) {
    begin();
    add("main.ft", text);
    return check_entry("main.ft");
}

// The same for a source that is a function body: the wrapper is
// `fn i32 main() {` on line 1, so a statement of `body` on its own line n is
// on line n + 1 of the module.
static inline bool check_body(const char* body) {
    char source[4096];
    TEST_UNUSED(snprintf(source, sizeof source, "fn i32 main() {\n%s\n    return 0;\n}\n", body));
    return check_src(source);
}

// Checks the entry module even though it did not parse, which the driver
// never does but an editor mode will: the error nodes of D14.2 stand where
// statements, fields and declarations were expected, and every pass skips
// them.
static inline bool check_broken(const char* text) {
    begin();
    add("main.ft", text);
    TEST_UNUSED(load("main.ft"));
    check_reset();
    check_init(&checker);
    checker_live = true;
    checker.require_main = false;
    const module_t* m = module_set_find(&set, str_from_cstr("main"));
    return m != NULL && check_module(&checker, m);
}

// The module of the closure at `path`, or NULL.
static inline const module_t* module_at(const char* path) {
    return module_set_find(&set, str_from_cstr(path));
}

// The symbol of a top-level declaration of the module at `path`.
static inline const sym_t* sym_of(const char* path, const char* name) {
    const module_t* m = module_at(path);
    const binding_t* b = m != NULL ? scope_find(&m->names, str_from_cstr(name)) : NULL;
    if (b == NULL || b->node == NULL) {
        return NULL;
    }
    return b->node->sym;
}

// The symbol of a top-level declaration of the entry module `main`.
static inline const sym_t* sym_main(const char* name) {
    return sym_of("main", name);
}

// The first node of `kind` in the tree below `n`, in source order; `name`
// selects by the node's own name when it is not NULL.
static inline ast_node_t* node_find(ast_node_t* n, ast_kind_t kind, const char* name) {
    if (n == NULL) {
        return NULL;
    }
    if (n->kind == kind && (name == NULL || str_eq(n->name, str_from_cstr(name)))) {
        return n;
    }
    ast_node_t* const kids[] = {n->a, n->b, n->c, n->d};
    for (uint64_t i = 0; i < sizeof kids / sizeof kids[0]; i++) {
        ast_node_t* hit = node_find(kids[i], kind, name);
        if (hit != NULL) {
            return hit;
        }
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        ast_node_t* hit = node_find(ast_child(n, i), kind, name);
        if (hit != NULL) {
            return hit;
        }
    }
    return NULL;
}

// The first node of `kind` named `name` in the entry module.
static inline ast_node_t* node_in_main(ast_kind_t kind, const char* name) {
    const module_t* m = module_at("main");
    return m != NULL ? node_find(m->ast, kind, name) : NULL;
}

// The canonical spelling of a type, valid until the next call.
static inline const char* type_text(const type_t* t) {
    static sb_t out;
    static bool ready = false;
    if (!ready) {
        sb_init(&out);
        ready = true;
    }
    sb_clear(&out);
    if (t == NULL) {
        sb_append(&out, "<null>");
    } else {
        type_to_str(t, &out);
    }
    return sb_cstr(&out);
}

// The spelling of a declaration's type, level 0 included (D5.3).
static inline const char* sym_type_text(const sym_t* s) {
    static sb_t out;
    static bool ready = false;
    if (!ready) {
        sb_init(&out);
        ready = true;
    }
    sb_clear(&out);
    if (s == NULL || s->type == NULL) {
        sb_append(&out, "<null>");
    } else {
        type_to_str_decl(s->type, s->mut0, &out);
    }
    return sb_cstr(&out);
}

// The first expression node below `n` that the checker left without a type,
// or NULL: every expression of a module that checks clean carries one, which
// is what the emitter reads. The expression kinds are the range of ast.h
// between the literals and the array literal.
static inline ast_node_t* untyped_expr(ast_node_t* n) {
    if (n == NULL || n->kind == AST_IMPORT) {
        // The identifiers of an import path name modules, which have no type
        // of their own; they carry a symbol and nothing else (D9.3).
        return NULL;
    }
    if (n->sym != NULL &&
        (n->sym->kind == SYM_MODULE || n->sym->kind == SYM_STRUCT || n->sym->kind == SYM_ENUM)) {
        // A name that denotes a module or a type is not a value: `color` in
        // `color.red` and `util` in `util.one()` carry a symbol only (D9.4,
        // D3.9).
        return NULL;
    }
    if (n->kind >= AST_INT && n->kind <= AST_ARRAY_LIT && n->type == NULL) {
        return n;
    }
    ast_node_t* const kids[] = {n->a, n->b, n->c, n->d};
    for (uint64_t i = 0; i < sizeof kids / sizeof kids[0]; i++) {
        ast_node_t* hit = untyped_expr(kids[i]);
        if (hit != NULL) {
            return hit;
        }
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        ast_node_t* hit = untyped_expr(ast_child(n, i));
        if (hit != NULL) {
            return hit;
        }
    }
    return NULL;
}

// The folded value of the initializer of the local or module declaration
// `name`, as an int64_t; false when it is not an integer constant.
static inline bool init_int(const char* name, int64_t* out) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    if (d == NULL || d->b == NULL) {
        return false;
    }
    return cv_to_i64(check_node_value(&checker, d->b), out);
}

// The type the checker gave the initializer of the declaration `name`.
static inline const char* init_type(const char* name) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    return type_text(d != NULL && d->b != NULL ? d->b->type : NULL);
}

// The declared type of the local or module declaration `name`, level 0
// included (D5.3).
static inline const char* decl_type(const char* name) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    return sym_type_text(d != NULL ? d->sym : NULL);
}

// The number of diagnostic lines the compilation reported.
static inline uint64_t diag_lines(void) {
    uint64_t lines = 0;
    const char* text = diags();
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

#endif
