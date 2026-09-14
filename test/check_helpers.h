// The environment the checker suites share: the module sandbox of
// modules_helpers.h, a checker over the closure it loads, and small queries
// over the symbols and the annotations the checker wrote.
//
// The helpers are static inline and the state is per suite, so a suite that
// uses only some of them still builds under -Werror.
#ifndef FORT_TEST_CHECK_HELPERS_H
#define FORT_TEST_CHECK_HELPERS_H

#include <stdbool.h>
#include <stdint.h>
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
/// Options of the next check, applied and reset by check_entry so that a test
/// states the ones it needs and leaves the next test the defaults.
static bool want_main = true;
static bool want_mute = false;

/// Releases the checker of the previous test. The symbols point into the
/// previous sandbox's tree, which `begin` has already released, and check_free
/// only frees its own memory, so the order is safe.
static inline void check_reset(void) {
    if (checker_live) {
        check_free(&checker);
        checker_live = false;
    }
}

/// Loads the closure of the sandbox module `rel` and checks it; false when the
/// load or the check reported a diagnostic. Checking runs even when the load
/// failed, as the front end does, so that a module whose import did not parse
/// is still checked.
/// D14.2, D20.1
static inline bool check_entry(const char* rel) {
    check_reset();
    const bool loaded = load(rel);
    check_init(&checker);
    checker_live = true;
    checker.require_main = want_main;
    checker.mute = want_mute;
    want_main = true;
    want_mute = false;
    return check_program(&checker, &set) && loaded;
}

/// Writes `text` as the entry module `main.ft` and checks it.
static inline bool check_src(const char* text) {
    begin();
    add("main.ft", text);
    return check_entry("main.ft");
}

// The standard library sources, which CMake names for the suites that ask for
// it. The real `std/libc.ft` is what the closure-wide rule on an extern
// declaration holds a program's own declaration of a libc symbol against, so
// a test of that rule reads the library and not a copy of its text.
// D9.8, D9.10
#ifndef FORT_STD_SOURCE_DIR
#define FORT_STD_SOURCE_DIR "std"
#endif

/// The same as check_src, with the real standard library as the `--std-dir`:
/// the closure then holds `std.rt` and, behind it, `std.libc`, exactly as a
/// build does. Every other helper here leaves the directory unnamed, so the
/// runtime stays out of the closure and the assertions stay about the module
/// the test wrote.
/// D9.10, D13.1
static inline bool check_src_with_library(const char* text) {
    begin();
    add("main.ft", text);
    module_set_std_dir(&set, FORT_STD_SOURCE_DIR);
    return check_entry("main.ft");
}

/// The same for a source that is a function body: the wrapper is
/// `fn i32 main() {` on line 1, so a statement of `body` on its own line n is
/// on line n + 1 of the module.
static inline bool check_body(const char* body) {
    char source[4096];
    TEST_UNUSED(snprintf(source, sizeof source, "fn i32 main() {\n%s\n    return 0;\n}\n", body));
    return check_src(source);
}

/// Checks the entry module even though it did not parse, which the driver
/// never does but an editor mode will: the error nodes stand where statements,
/// fields and declarations were expected, and every pass skips them.
/// D14.2
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

/// `body` inside a main that already has a struct `node`, a mutable `m`, an
/// immutable `k` and a mutable `i32 w`: the shape the tables are written
/// against.
/// D5.3, D17.2
static inline bool check_node_body(const char* body) {
    char source[4096];
    TEST_UNUSED(snprintf(source,
                         sizeof source,
                         "struct node {\n    i32 value;\n}\n"
                         "fn i32 main() {\n    node mut m = {};\n    node k = {};\n"
                         "    i32 mut w = 1;\n%s\n    return 0;\n}\n",
                         body));
    return check_src(source);
}

/// The module of the closure at `path`, or NULL.
static inline const module_t* module_at(const char* path) {
    return module_set_find(&set, str_from_cstr(path));
}

/// The symbol of a top-level declaration of the module at `path`.
static inline const sym_t* sym_of(const char* path, const char* name) {
    const module_t* m = module_at(path);
    const binding_t* b = m != NULL ? scope_find(&m->names, str_from_cstr(name)) : NULL;
    if (b == NULL || b->node == NULL) {
        return NULL;
    }
    return b->node->sym;
}

/// The symbol of a top-level declaration of the entry module `main`.
static inline const sym_t* sym_main(const char* name) {
    return sym_of("main", name);
}

/// The first node of `kind` in the tree below `n`, in source order; `name`
/// selects by the node's own name when it is not NULL.
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

/// The first node of `kind` named `name` in the entry module.
static inline ast_node_t* node_in_main(ast_kind_t kind, const char* name) {
    const module_t* m = module_at("main");
    return m != NULL ? node_find(m->ast, kind, name) : NULL;
}

/// The canonical spelling of a type, valid until the next call.
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

/// The spelling of a declaration's type, level 0 included.
/// D5.3
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

/// The first expression node below `n` that the checker left without a type,
/// or NULL: every expression of a module that checks clean carries one, which
/// is what the emitter reads. The expression kinds are the range of ast.h
/// between the literals and the array literal.
static inline ast_node_t* untyped_expr(ast_node_t* n) {
    if (n == NULL || n->kind == AST_IMPORT) {
        // The identifiers of an import path name modules, which have no type
        // of their own; they carry a symbol and nothing else.
        // D9.3
        return NULL;
    }
    if (n->sym != NULL &&
        (n->sym->kind == SYM_MODULE || n->sym->kind == SYM_STRUCT || n->sym->kind == SYM_ENUM)) {
        // A name that denotes a module or a type is not a value: `color` in
        // `color.red` and `util` in `util.one()` carry a symbol only.
        // D9.4, D3.9
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

/// The first node below `n` whose own name token denotes something and that
/// the checker left without a symbol, or NULL. Literals carry their bytes in
/// `name` and denote nothing, and so do the pseudo-fields `.len` and `.ptr`,
/// which no declaration introduces.
static inline ast_node_t* unresolved_name(ast_node_t* n) {
    if (n == NULL) {
        return NULL;
    }
    if (n->kind == AST_PATH) {
        // Every segment of an import path but the last names a search
        // directory rather than a module, so only the last one denotes
        // something.
        // D9.2, D9.3
        return unresolved_name(ast_child(n, ast_len(n) - 1));
    }
    const bool literal = n->kind == AST_STRING || n->kind == AST_FLOAT;
    const bool pseudo =
        (n->kind == AST_FIELD || n->kind == AST_ARROW) &&
        (str_eq(n->name, str_from_cstr("len")) || str_eq(n->name, str_from_cstr("ptr")));
    if (n->name.len > 0 && n->sym == NULL && !literal && !pseudo) {
        return n;
    }
    ast_node_t* const kids[] = {n->a, n->b, n->c, n->d};
    for (uint64_t i = 0; i < sizeof kids / sizeof kids[0]; i++) {
        ast_node_t* hit = unresolved_name(kids[i]);
        if (hit != NULL) {
            return hit;
        }
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        ast_node_t* hit = unresolved_name(ast_child(n, i));
        if (hit != NULL) {
            return hit;
        }
    }
    return NULL;
}

/// The folded value of the initializer of the local or module declaration
/// `name`, as an int64_t; false when it is not an integer constant.
static inline bool init_int(const char* name, int64_t* out) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    if (d == NULL || d->b == NULL) {
        return false;
    }
    return cv_to_i64(check_node_value(&checker, d->b), out);
}

/// The type the checker gave the initializer of the declaration `name`.
static inline const char* init_type(const char* name) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    return type_text(d != NULL && d->b != NULL ? d->b->type : NULL);
}

/// The declared type of the local or module declaration `name`, level 0
/// included.
/// D5.3
static inline const char* decl_type(const char* name) {
    const ast_node_t* d = node_in_main(AST_VAR_DECL, name);
    return sym_type_text(d != NULL ? d->sym : NULL);
}

/// The thirteen positions that give their operand no context at all, and so
/// drop it when it is poisoned: `*e`, `e.f`, `e->f`, `e.len`, `e[i]`,
/// `e[a .. b]`, `e()`, `&e`, `del(e)`, `move(e)`, an assignment target, `++`
/// and a range `for` collection. Each writes `C` where the operand goes. Two
/// suites read the table -- check_operand_test.c for what each position
/// reports and check_poison_test.c for how many -- so it stands here rather
/// than in either of them. Index 9 is `move`, which reports twice.
/// D4.1, D4.5
enum { DROP_SITE_COUNT = 13, DROP_SITE_MOVE = 9 };

static inline const char* drop_site_form(uint64_t i) {
    static const char* const SITES[DROP_SITE_COUNT] = {
        "    println(*C);",
        "    println((C).x);",
        "    println((C)->x);",
        "    println((C).len);",
        "    println(C[0]);",
        "    println((C)[0 .. 1]);",
        "    println(C());",
        "    println(&C);",
        "    del(C);",
        "    println(move(C));",
        "    C = 1;",
        "    C++;",
        "    for (i32 x : C) {\n        println(x);\n    }",
    };
    return i < DROP_SITE_COUNT ? SITES[i] : NULL;
}

/// `form` with every `C` replaced by `constant`, as the body of a main. It
/// returns NULL rather than a shorter program when the buffer would overflow,
/// because a template that silently loses its tail is a test that silently
/// stops testing: check_body of half a statement is a syntax error, which is a
/// `false` the assertions would read as a pass. The buffer is shared, so one
/// call is live at a time.
static inline const char* site_body(const char* form, const char* constant) {
    static char body[256];
    uint64_t w = 0;
    if (form == NULL) {
        return NULL;
    }
    for (uint64_t r = 0; form[r] != '\0'; r++) {
        if (form[r] != 'C') {
            if (w + 1 >= sizeof body) {
                return NULL;
            }
            body[w++] = form[r];
            continue;
        }
        for (uint64_t k = 0; constant[k] != '\0'; k++) {
            if (w + 1 >= sizeof body) {
                return NULL;
            }
            body[w++] = constant[k];
        }
    }
    body[w] = '\0';
    return body;
}

/// `drop_site_form(i)` with `C` replaced by `constant`.
static inline const char* drop_site(uint64_t i, const char* constant) {
    return site_body(drop_site_form(i), constant);
}

/// The number of diagnostic lines the compilation reported.
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
