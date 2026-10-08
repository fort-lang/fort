// The checker of the bootstrap compiler; see check.h.
#include "check.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "consts.h"
#include "containers.h"
#include "diag.h"
#include "lexer.h"
#include "modules.h"
#include "prim.h"
#include "scope.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// The limits of one declaration. A type has at most 256 suffixes, which is the
// nesting limit of D2.11. The checker also refuses a function or struct with more
// than 128 members. It does not size the member lists dynamically.
enum { CHECK_MAX_SUFFIXES = 256, CHECK_MAX_MEMBERS = 128 };

// The width of the `struct` keyword, which is where an infinite-size struct is
// reported.
enum { STRUCT_KEYWORD_LEN = 6 };

// The underlying type of an enum is i32.
enum { ENUM_UNDERLYING = PRIM_I32 };

// ---- the context -------------------------------------------------------------------

void check_init(check_t* ck) {
    type_table_init(&ck->types);
    ptrvec_init(&ck->syms);
    ptrvec_init(&ck->values);
    sb_init(&ck->msg);
    strmap_init(&ck->bad_imports);
    strmap_init(&ck->extern_first);
    ptrvec_init(&ck->externs);
    // one symbol per name, so every use denotes one record
    for (uint64_t i = 0; i < UNIVERSE_COUNT; i++) {
        ck->builtins[i] = check_sym_new(ck, SYM_BUILTIN, scope_universe_at(i), NULL, NULL);
    }
    ck->mute = false;
    ck->require_main = true;
    ck->errors = 0;
    ck->module = NULL;
    ck->module_sym = NULL;
    ck->fn_sym = NULL;
    ck->callee = NULL;
    ck->addr_only = false;
    ck->scope = NULL;
    ck->ret = NULL;
    ck->ret_void = true;
    ck->ret_noreturn = false;
    ck->in_function = false;
    ck->loops = 0;
    ck->switches = 0;
    ck->defers = 0;
    ck->block_errs = 0;
    ck->suffixes = NULL;
    ck->suffix_cap = 0;
    ck->suffix_top = 0;
    ck->resolve_depth = 0;
    ck->sized = NULL;
    ck->sized_cap = 0;
    ck->sized_len = 0;
    ck->sized_draining = false;
}

void check_free(check_t* ck) {
    for (uint64_t i = 0; i < ck->syms.len; i++) {
        mem_free(ck->syms.items[i]);
    }
    ptrvec_free(&ck->syms);
    for (uint64_t i = 0; i < ck->values.len; i++) {
        mem_free(ck->values.items[i]);
    }
    ptrvec_free(&ck->values);
    strmap_free(&ck->bad_imports);
    strmap_free(&ck->extern_first);
    ptrvec_free(&ck->externs);
    sb_free(&ck->msg);
    type_table_free(&ck->types);
    mem_free(ck->suffixes);
    ck->suffixes = NULL;
    ck->suffix_cap = 0;
    ck->suffix_top = 0;
    mem_free(ck->sized);
    ck->sized = NULL;
    ck->sized_cap = 0;
    ck->sized_len = 0;
}

uint64_t check_sym_count(const check_t* ck) {
    return ck->syms.len;
}

const sym_t* check_sym_at(const check_t* ck, uint64_t i) {
    if (i >= ck->syms.len) {
        fatal_internal("check_sym_at: index out of range");
    }
    return (const sym_t*)ck->syms.items[i];
}

cval_t check_node_value(const check_t* ck, const ast_node_t* n) {
    if ((n->ann & CHECK_ANN_CONST) == 0) {
        return cv_none();
    }
    if (n->aux >= ck->values.len) {
        fatal_internal("check_node_value: value index out of range");
    }
    return *(const cval_t*)ck->values.items[n->aux];
}

// Records the folded value of a node: `aux` indexes it and CHECK_ANN_CONST
// says so.
static void set_value(check_t* ck, ast_node_t* n, cval_t v) {
    if (v.kind == CV_NONE) {
        n->ann &= ~(uint32_t)CHECK_ANN_CONST;
        return;
    }
    cval_t* slot = mem_alloc((uint64_t)sizeof(cval_t));
    *slot = v;
    n->aux = ck->values.len;
    ptrvec_push(&ck->values, slot);
    n->ann |= CHECK_ANN_CONST;
}

// ---- diagnostics -------------------------------------------------------------------

void check_error(check_t* ck, loc_t loc, const char* msg) {
    ck->errors++;
    // a muted checker annotates but does not report
    if (!ck->mute) {
        diag_error(loc, msg);
    }
}

void check_msg_begin(check_t* ck) {
    msg_begin(&ck->msg);
}

void check_msg_type(check_t* ck, const type_t* t) {
    type_to_str(t, &ck->msg);
}

void check_msg_end(check_t* ck, loc_t loc) {
    check_error(ck, loc, msg_end(&ck->msg));
}

bool check_poisoned(const type_t* t) {
    return t == NULL || t->kind == TYPE_ERROR;
}

loc_t check_close_brace(loc_t loc) {
    // a range ends one past its last byte; the `}` is one before
    if (loc.end_col <= 1) {
        return loc;
    }
    return loc_range(loc.file, loc.end_line, loc.end_col - 1, loc.end_line, loc.end_col);
}

// The range of the keyword a declaration starts at, where an infinite-size
// struct is reported.
static loc_t keyword_range(loc_t loc, uint32_t len) {
    return loc_range(loc.file, loc.line, loc.col, loc.line, loc.col + len);
}

// ---- symbols -----------------------------------------------------------------------

sym_t* check_sym_new(
    check_t* ck, sym_kind_t kind, str_t name, const ast_node_t* node, const sym_t* owner) {
    sym_t* s = mem_alloc((uint64_t)sizeof(sym_t));
    s->kind = kind;
    s->name = name;
    // at the name, never at the construct's first token
    s->decl = node != NULL ? node->name_loc : loc_make(NULL, 1, 1);
    s->type = NULL;
    s->mut0 = false;
    s->node = node;
    s->owner = owner;
    s->error = false;
    ptrvec_push(&ck->syms, s);
    // only a top-level declaration resolves lazily
    if (node != NULL &&
        (kind == SYM_FIELD || kind == SYM_ENUM_MEMBER || kind == SYM_LOCAL || kind == SYM_PARAM)) {
        ((ast_node_t*)node)->ann |= CHECK_ANN_RESOLVED;
    }
    return s;
}

// Marks a symbol as failed: the error type silences every later diagnostic
// that involves the declaration.
static void sym_fail(check_t* ck, sym_t* s) {
    s->error = true;
    s->type = type_error(&ck->types);
}

// The declaration a binding denotes: an import binding stands for the module
// or for the declaration it names, which is what its own name denotes.
static const sym_t* sym_of_binding(const binding_t* b) {
    if (b == NULL) {
        return NULL;
    }
    if (b->kind == BIND_MODULE) {
        const module_t* m = (const module_t*)b->module;
        return m != NULL && m->ast != NULL ? m->ast->sym : NULL;
    }
    if (b->kind == BIND_SYMBOL) {
        return b->to != NULL && b->to->node != NULL ? b->to->node->sym : NULL;
    }
    return b->node != NULL ? b->node->sym : NULL;
}

// ---- names -------------------------------------------------------------------------

// The lookup reads the innermost block outward, then the module namespace.
// check_builtin reads names from the universe.
static const binding_t* lookup(const check_t* ck, str_t name) {
    if (ck->scope != NULL) {
        const binding_t* b = scope_lookup(ck->scope, name);
        if (b != NULL) {
            return b;
        }
    }
    if (ck->module != NULL) {
        return scope_find(&ck->module->names, name);
    }
    return NULL;
}

// The builtin of `name`, or NULL: the universe is searched after every scope,
// so a declaration of the same name shadows it.
static const sym_t* check_builtin(const check_t* ck, str_t name) {
    for (uint64_t i = 0; i < UNIVERSE_COUNT; i++) {
        if (str_eq(ck->builtins[i]->name, name)) {
            return ck->builtins[i];
        }
    }
    return NULL;
}

// Whether an import failed to bind the name in this module. The loader reported
// the import, so later uses do not report more errors.
static bool from_a_failed_import(const check_t* ck, str_t name) {
    return strmap_has(&ck->bad_imports, name);
}

// "unknown name 'x'", with the hint for a name that is an enum member, which
// is written `color.red` and lives in no namespace.
static void error_unknown_name(check_t* ck, str_t name, loc_t loc) {
    if (from_a_failed_import(ck, name)) {
        return;
    }
    check_msg_begin(ck);
    msg_str(&ck->msg, "unknown name ");
    msg_quote(&ck->msg, name);
    const module_t* m = ck->module;
    for (uint64_t i = 0; m != NULL && m->ast != NULL && i < ast_len(m->ast); i++) {
        const ast_node_t* decl = ast_child(m->ast, i);
        if (decl->kind != AST_ENUM_DECL) {
            continue;
        }
        for (uint64_t k = 0; k < ast_len(decl); k++) {
            if (str_eq(ast_child(decl, k)->name, name)) {
                msg_str(&ck->msg, ": an enum member is written ");
                msg_view(&ck->msg, decl->name);
                msg_str(&ck->msg, ".");
                msg_view(&ck->msg, name);
                check_msg_end(ck, loc);
                return;
            }
        }
    }
    check_msg_end(ck, loc);
}

// ---- types -------------------------------------------------------------------------

// The declaring symbol of a struct or enum type, which types.h keeps opaque.
static sym_t* nominal_sym(const type_t* t) {
    return (sym_t*)t->decl;
}

static void resolve_sym(check_t* ck, sym_t* s);

static void error_too_large(check_t* ck, loc_t loc, const type_t* t) {
    check_msg_begin(ck);
    msg_str(&ck->msg, "type is too large: ");
    check_msg_type(ck, t);
    check_msg_end(ck, loc);
}

static const type_t* referenced_too_large(check_t* ck, loc_t loc, const type_t* t, bool behind);

bool check_size_fits(check_t* ck, loc_t loc, const type_t* t) {
    if (check_poisoned(t) || t->kind == TYPE_VOID) {
        return true;
    }
    const type_t* large = t;
    if (type_size_fits(t)) {
        large = referenced_too_large(ck, loc, t, false);
    }
    if (large == NULL) {
        return true;
    }
    error_too_large(ck, loc, large);
    return false;
}

// Keeps an array for sized_drain.
static void sized_push(check_t* ck, loc_t loc, const type_t* t) {
    if (ck->sized_len == ck->sized_cap) {
        const uint64_t cap = mem_grown_cap(ck->sized_cap, mem_add(ck->sized_len, 1));
        check_sized_t* bigger = mem_alloc(mem_mul(cap, sizeof(check_sized_t)));
        for (uint64_t i = 0; i < ck->sized_len; i++) {
            bigger[i] = ck->sized[i];
        }
        mem_free(ck->sized);
        ck->sized = bigger;
        ck->sized_cap = cap;
    }
    ck->sized[ck->sized_len].type = t;
    ck->sized[ck->sized_len].at = loc;
    ck->sized_len++;
}

// The first array in `t` that passes the ceiling behind a reference, or NULL.
// `behind` says whether a `*`, a `@` or a function type encloses `t`. The walk
// does not go into a struct, because the struct's declaration checked its own
// fields. While a resolution is active, the walk puts an array whose struct has
// no layout yet on the list of sized_drain.
static const type_t* referenced_too_large(check_t* ck, loc_t loc, const type_t* t, bool behind) {
    switch (t->kind) {
    case TYPE_PTR:
    case TYPE_SPAN:
        return referenced_too_large(ck, loc, t->elem, true);
    case TYPE_FN: {
        // no value stores a parameter or a result, but D3.4 refuses its type too
        const type_t* result = referenced_too_large(ck, loc, t->elem, true);
        if (result != NULL) {
            return result;
        }
        for (uint32_t i = 0; i < t->nparams; i++) {
            const type_t* param = referenced_too_large(ck, loc, t->params[i], true);
            if (param != NULL) {
                return param;
            }
        }
        return NULL;
    }
    case TYPE_ARRAY:
        if (!behind) {
            // the size of `t` covers this array and the arrays in it
            return referenced_too_large(ck, loc, t->elem, false);
        }
        if (type_layout_pending(t) != NULL) {
            // only arrays and a struct are below this array
            if (ck->resolve_depth > 0) {
                sized_push(ck, loc, t);
                return NULL;
            }
            if (!check_layout(ck, t)) {
                return NULL;
            }
        }
        if (!type_size_fits(t)) {
            return t;
        }
        return referenced_too_large(ck, loc, t->elem, true);
    default:
        return NULL;
    }
}

// Checks the arrays that check_size_fits kept while a resolution was active.
// The outermost resolve_sym calls it, so a layout here cannot read a struct
// that is still resolving. A check can resolve a struct, which can add an
// entry, so the loop reads the length again on each pass.
static void sized_drain(check_t* ck) {
    if (ck->sized_draining) {
        return;
    }
    ck->sized_draining = true;
    for (uint64_t i = 0; i < ck->sized_len; i++) {
        const check_sized_t c = ck->sized[i];
        if (check_layout(ck, c.type) && !type_size_fits(c.type)) {
            error_too_large(ck, c.at, c.type);
        }
    }
    ck->sized_len = 0;
    ck->sized_draining = false;
}

// The struct whose layout a type needs, behind any fixed arrays.
static const type_t* struct_of(const type_t* t) {
    if (t->kind == TYPE_STRUCT) {
        return t;
    }
    if (t->kind == TYPE_ARRAY) {
        return struct_of(t->elem);
    }
    return NULL;
}

bool check_layout(check_t* ck, const type_t* t) {
    if (check_poisoned(t)) {
        return false;
    }
    if (t->kind == TYPE_VOID) {
        return true;
    }
    const type_t* pending = type_layout_pending(t);
    if (pending != NULL) {
        // a struct is laid out when a declaration first needs it
        resolve_sym(ck, nominal_sym(pending));
        pending = type_layout_pending(t);
    }
    if (pending != NULL) {
        // still resolving means it contains itself by value
        const sym_t* s = nominal_sym(pending);
        check_msg_begin(ck);
        msg_str(&ck->msg, "struct ");
        msg_view(&ck->msg, pending->name);
        msg_str(&ck->msg, " has infinite size");
        check_msg_end(ck,
                      s != NULL && s->node != NULL ? keyword_range(s->node->loc, STRUCT_KEYWORD_LEN)
                                                   : loc_make(NULL, 1, 1));
        type_layout_fail(pending);
        if (s != NULL) {
            sym_fail(ck, (sym_t*)s);
        }
        return false;
    }
    const type_t* nominal = struct_of(t);
    return nominal == NULL || type_layout_state(nominal) != LAYOUT_ERROR;
}

// The declaration `name` denotes in `m`, or NULL. Import bindings are not
// declarations because modules do not re-export them. Qualified access and
// imports both see declarations of `m`, not its imports.
static const binding_t* module_declaration(const module_t* m, str_t name) {
    const binding_t* b = m != NULL ? scope_find(&m->names, name) : NULL;
    return b != NULL && bind_is_declaration(b) ? b : NULL;
}

// The type a name in type position denotes: a struct or an enum, of this module
// or, qualified, of an imported one. `stores_base` says whether the written type
// puts a value of that name in its own storage, which decides whether the name
// is a layout dependency.
static const type_t* named_type(check_t* ck, ast_node_t* n, bool stores_base) {
    const binding_t* b = lookup(ck, n->name);
    if (b == NULL) {
        if (!from_a_failed_import(ck, n->name)) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "unknown type ");
            msg_quote(&ck->msg, n->name);
            check_msg_end(ck, n->name_loc);
        }
        return type_error(&ck->types);
    }
    const sym_t* s = sym_of_binding(b);
    n->sym = s;
    ast_node_t* tail = n->a;
    if (tail != NULL) {
        if (b->kind != BIND_MODULE) {
            check_msg_begin(ck);
            msg_quote(&ck->msg, n->name);
            msg_str(&ck->msg, " is not a module");
            check_msg_end(ck, n->name_loc);
            return type_error(&ck->types);
        }
        const module_t* m = (const module_t*)b->module;
        const binding_t* inner = module_declaration(m, tail->name);
        if (inner == NULL) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "module ");
            msg_quote(&ck->msg, m != NULL ? m->path : n->name);
            msg_str(&ck->msg, " has no declaration named ");
            msg_quote(&ck->msg, tail->name);
            check_msg_end(ck, tail->name_loc);
            return type_error(&ck->types);
        }
        s = sym_of_binding(inner);
        tail->sym = s;
    }
    if (s == NULL || (s->kind != SYM_STRUCT && s->kind != SYM_ENUM)) {
        check_msg_begin(ck);
        msg_quote(&ck->msg, tail != NULL ? tail->name : n->name);
        msg_str(&ck->msg, " is a ");
        msg_str(&ck->msg, s != NULL ? sym_kind_name(s->kind) : "value");
        msg_str(&ck->msg, ", not a type");
        check_msg_end(ck, tail != NULL ? tail->name_loc : n->name_loc);
        return type_error(&ck->types);
    }
    // The declaration can still be unresolved. A type name is one place where
    // lazy resolution reaches another declaration. It does not resolve a struct
    // that the written type does not store by value. That struct does not affect
    // this type's size. Forced resolution would put it on a path for a struct
    // that does not contain it. check_layout would then read an unfinished
    // layout as an infinite size. Resolution edges are exactly the
    // value-containment edges. This makes both declaration orders of a pair one
    // program. collect_module
    // gave every struct its type before any field was read, so the name already
    // denotes the right type.
    const bool identity_only = s->kind == SYM_STRUCT && !stores_base && s->type != NULL;
    if (!identity_only) {
        resolve_sym(ck, (sym_t*)s);
    }
    return s->type;
}

static check_type_t type_result(const type_t* t, bool mut0) {
    check_type_t r;
    r.type = t;
    r.mut0 = mut0;
    return r;
}

// The constant length of a fixed-array suffix.
static bool array_length(check_t* ck, ast_node_t* e, uint64_t* out) {
    expr_t v;
    check_expr_default(ck, e, &v);
    if (check_poisoned(v.type)) {
        return false;
    }
    if (!cv_is_int_like(v.value)) {
        check_error(ck, e->loc, "an array length must be a constant expression");
        return false;
    }
    const cval_t n = cv_as_int(v.value);
    if (cv_is_neg(n) || cv_is_zero(n) || !cv_to_u64(n, out)) {
        check_error(ck, e->loc, "an array length must be greater than 0");
        return false;
    }
    return true;
}

// Whether a written return type is `noreturn`. Each return type passes through
// `type`, so the keyword is under an AST_TYPE wrapper. A speculative parse can
// provide a base type node, which is also accepted.
static bool is_noreturn(const ast_node_t* t) {
    if (t == NULL) {
        return false;
    }
    while (t->kind == AST_TYPE && ast_len(t) == 0 && t->a != NULL) {
        t = t->a;
    }
    return t->kind == AST_TYPE_NORETURN;
}

// Whether a group holds a bare `void`, through any parentheses with no suffix.
static bool is_void_group(const ast_node_t* t) {
    while (t->kind == AST_TYPE && ast_len(t) == 0 && t->a != NULL) {
        t = t->a;
    }
    return t->kind == AST_TYPE_VOID;
}

// Reports a parameter whose checked type is `void` at `loc`, and returns whether it did. Only a
// bare `void` reaches this type: `void mut` and `(void)` fail before it (D3.1).
static bool void_parameter_refused(check_t* ck, loc_t loc, const type_t* t) {
    if (t->kind != TYPE_VOID) {
        return false;
    }
    check_error(ck, loc, "'void' is only a return type or the base of 'void*'");
    return true;
}

static check_type_t check_type_at(check_t* ck, ast_node_t* node, type_pos_t pos, bool in_storage);

// Gives each node of a group of a bare `void` or `noreturn` the `void` that it builds. Returns
// the innermost node of the group that carries a `mut`, or NULL. The parser refuses an `own`
// there.
static const ast_node_t* return_group_marked(check_t* ck, ast_node_t* t) {
    const type_t* v = type_void(&ck->types);
    const ast_node_t* marked = NULL;
    while (t->kind == AST_TYPE) {
        if (ast_is_mut(t)) {
            marked = t;
        }
        t->type = v;
        t = t->a;
    }
    t->type = v;
    return marked;
}

// The base type of a written type: a primitive, `string`, `void`,
// `noreturn`, a qualified name or a function type.
static const type_t* base_type(check_t* ck, ast_node_t* n, bool allow_noreturn, bool stores_base) {
    const type_t* t = type_error(&ck->types);
    switch (n->kind) {
    case AST_TYPE_PRIM: {
        const prim_kind_t k = (prim_kind_t)n->op;
        if (prim_is_float(k)) {
            // Floats are outside the C bootstrap's subset.
            check_error(ck, n->loc, "not supported by the bootstrap compiler: floats");
        } else if (k == PRIM_VOID) {
            t = type_void(&ck->types);
        } else {
            t = type_prim(&ck->types, k);
        }
        break;
    }
    case AST_TYPE_STRING:
        t = type_string(&ck->types, false);
        break;
    case AST_TYPE_VOID:
        t = type_void(&ck->types);
        break;
    case AST_TYPE_NORETURN:
        // `noreturn` is a return type only
        if (!allow_noreturn) {
            check_error(ck, n->loc, "'noreturn' is a return type");
        } else {
            t = type_void(&ck->types);
        }
        break;
    case AST_TYPE_NAME:
        t = named_type(ck, n, stores_base);
        break;
    case AST_TYPE_FN: {
        const type_t* params[CHECK_MAX_MEMBERS];
        const uint64_t count = ast_len(n);
        if (count > CHECK_MAX_MEMBERS) {
            check_error(ck, n->loc, "too many parameters");
            break;
        }
        // a function pointer is one word, so no layout dependency
        const check_type_t ret = check_type_at(ck, n->a, TYPE_POS_RETURN, false);
        bool ok = !check_poisoned(ret.type);
        for (uint64_t i = 0; i < count; i++) {
            // a binding-level `mut` is not part of the type
            ast_node_t* written = ast_child(n, i);
            const check_type_t p = check_type_at(ck, written, TYPE_POS_BINDING, false);
            params[i] = p.type;
            ok = ok && !check_poisoned(p.type);
            if (void_parameter_refused(ck, written->loc, p.type)) {
                ok = false;
            }
        }
        if (ok) {
            t = type_fn(&ck->types, ret.type, params, (uint32_t)count, is_noreturn(n->a));
        }
        break;
    }
    default:
        fatal_internal("check: not a base type");
    }
    n->type = t;
    return t;
}

// Whether the base of a written type ends up in the storage the type describes.
// A reference suffix anywhere breaks the chain. `node*` is one word, and
// `node* @` contains two. A fixed array contains N elements and carries the
// base through. This is the written form of the rule type_layout_pending
// applies to the built type, which looks behind fixed arrays and stops at every
// reference.
static bool suffixes_store_base(const ast_node_t* node) {
    if (node->kind != AST_TYPE) {
        // A bare base type node, which a speculative parse hands over: it
        // carries no suffix at all.
        return true;
    }
    for (uint64_t i = 0; i < ast_len(node); i++) {
        if ((suffix_kind_t)ast_child(node, i)->op != SUFFIX_ARRAY) {
            return false;
        }
    }
    return true;
}

// Reserves a frame of `count` suffixes on the checker's suffix stack and
// returns its first index. The caller sets `suffix_top` back to that index
// before it returns. A growth moves the stack, so the caller indexes
// `ck->suffixes` again after each call that can check a type.
static uint64_t suffix_frame(check_t* ck, uint64_t count) {
    const uint64_t first = ck->suffix_top;
    const uint64_t need = mem_add(first, count);
    if (need > ck->suffix_cap) {
        const uint64_t cap = mem_grown_cap(ck->suffix_cap, need);
        type_suffix_t* bigger = mem_alloc(mem_mul(cap, sizeof(type_suffix_t)));
        for (uint64_t i = 0; i < first; i++) {
            bigger[i] = ck->suffixes[i];
        }
        mem_free(ck->suffixes);
        ck->suffixes = bigger;
        ck->suffix_cap = cap;
    }
    ck->suffix_top = need;
    return first;
}

static uint64_t written_suffix_count(const ast_node_t* node) {
    uint64_t count = 0;
    const ast_node_t* cur = node;
    while (cur->kind == AST_TYPE) {
        count += ast_len(cur);
        cur = cur->a;
    }
    return count;
}

// Calls `check_type` with whether this type stores a base value. A function
// type does not store its result or parameters. Their suffixes do not change
// this answer.
static check_type_t check_type_at(check_t* ck, ast_node_t* node, type_pos_t pos, bool in_storage) {
    const bool wrapped = node->kind == AST_TYPE;
    ast_node_t* base = wrapped ? node->a : node;
    const bool allow_noreturn = pos == TYPE_POS_RETURN;
    const bool stores_base = in_storage && suffixes_store_base(node);
    check_type_t grouped = type_result(type_error(&ck->types), false);
    const bool has_group = base->kind == AST_TYPE;
    // `noreturn`, bare or in groups with no suffix, as the return type itself. This node is the
    // outermost one of that return type, so it checks the groups and reports for all of them.
    const bool return_noreturn = allow_noreturn && is_noreturn(base);
    // A bare `void` in a return type, in groups or with no suffix on this node: this node checks
    // the groups too. `void*` and `void mut*` are not in this set.
    const bool return_void =
        allow_noreturn && wrapped && is_void_group(base) && (has_group || ast_len(node) == 0);
    const ast_node_t* marked = NULL;
    const type_t* b = NULL;
    if ((has_group && return_noreturn) || return_void) {
        marked = return_group_marked(ck, base);
        b = base->type;
    } else if (has_group) {
        grouped = check_type_at(ck, base, TYPE_POS_BINDING, stores_base);
        b = grouped.type;
        // A group of a bare `void` is a complete return type and nothing else, as `void` needs its
        // `*` inside the same group: `(void)*` is an error and `(void*)` the opaque pointer
        // (D3.11).
        if (!check_poisoned(b) && is_void_group(base)) {
            check_error(ck, base->loc, "'void' is only a return type or the base of 'void*'");
            b = type_error(&ck->types);
        }
    } else {
        b = base_type(ck, base, allow_noreturn, stores_base);
    }
    // A `noreturn` return type takes no suffix (D8.5): `noreturn*` would be `void*`. A group of a
    // bare `void` takes none either (D3.11), and the innermost `mut` in the group, or else the
    // group, reports it. A `mut` on a bare `noreturn` or `void` gets the message of `i32 mut`
    // there. In any other position base_type reported `noreturn`, so `return_noreturn` keeps this
    // from a second report.
    if ((return_noreturn || return_void) && wrapped) {
        const char* what = NULL;
        loc_t report_at = node->loc;
        if (return_noreturn && ast_len(node) > 0) {
            what = "'noreturn' is a return type";
        } else if (ast_len(node) > 0) {
            what = "'void' is only a return type or the base of 'void*'";
            report_at = marked != NULL ? marked->loc : base->loc;
        } else if (ast_is_mut(node) || marked != NULL) {
            what = "a return type has no binding: remove the outermost 'mut'";
        }
        if (what != NULL) {
            check_error(ck, report_at, what);
            b = type_error(&ck->types);
        }
    }
    const uint64_t count = wrapped ? ast_len(node) : 0;
    bool ok = !check_poisoned(b);
    if (ok && written_suffix_count(node) > CHECK_MAX_SUFFIXES) {
        check_error(ck, node->loc, "too many type suffixes");
        ok = false;
    }
    const uint64_t first = suffix_frame(ck, count);
    for (uint64_t i = 0; ok && i < count; i++) {
        ast_node_t* s = ast_child(node, i);
        type_suffix_t level;
        level.kind = (suffix_kind_t)s->op;
        level.len = 0;
        level.own = ast_is_own(s);
        level.mut = ast_is_mut(s);
        // array_length can check a type in the length, which can move the stack.
        if (level.kind == SUFFIX_ARRAY && !array_length(ck, s->a, &level.len)) {
            ok = false;
        }
        ck->suffixes[first + i] = level;
    }
    if (!ok) {
        ck->suffix_top = first;
        node->type = type_error(&ck->types);
        return type_result(node->type, false);
    }
    bool base_own = wrapped && ast_is_own(node);
    bool base_mut = (wrapped && ast_is_mut(node)) || grouped.mut0;
    if (has_group && base_own && type_is_reference(b)) {
        // The marker owns the grouped reference, rather than a new storage level.
        switch (b->kind) {
        case TYPE_PTR:
            b = type_ptr(&ck->types, b->elem, true, b->mut);
            break;
        case TYPE_VOIDPTR:
            b = type_voidptr(&ck->types, true, b->mut);
            break;
        case TYPE_SPAN:
            b = type_span(&ck->types, b->elem, true, b->mut);
            break;
        case TYPE_STRING:
            b = type_string(&ck->types, true);
            break;
        default:
            fatal_internal("check: not a grouped reference");
        }
        base_own = false;
    }
    if (pos == TYPE_POS_ALLOC) {
        // `new` fills only the outermost position because it allocates that
        // storage. Each inner position describes storage that `new` did not
        // allocate. Thus, the result has the written element type. A bare
        // `void` has no
        // position and keeps no mark, so check_new reports it rather than the
        // type builder.
        if (count == 0) {
            base_mut = b->kind != TYPE_VOID;
        } else {
            ck->suffixes[first + count - 1].mut = true;
        }
    }
    const type_suffix_t* suffixes = count > 0 ? &ck->suffixes[first] : NULL;
    const type_build_t built =
        type_build(&ck->types, base_own, base_mut, b, suffixes, (uint32_t)count);
    ck->suffix_top = first;
    if (built.type == NULL) {
        check_error(ck, node->loc, built.error);
        node->type = type_error(&ck->types);
        return type_result(node->type, false);
    }
    if (built.mut0 && pos != TYPE_POS_BINDING && pos != TYPE_POS_ALLOC) {
        // no `mut` on a field, a return type or a cast target
        const char* what = "a field's own storage follows its struct";
        if (pos == TYPE_POS_RETURN) {
            what = "a return type has no binding";
        } else if (pos == TYPE_POS_CAST) {
            what = "a cast result has no binding";
        }
        check_msg_begin(ck);
        msg_str(&ck->msg, what);
        msg_str(&ck->msg, ": remove the outermost 'mut'");
        check_msg_end(ck, node->loc);
        node->type = type_error(&ck->types);
        return type_result(node->type, false);
    }
    node->type = built.type;
    return type_result(built.type, built.mut0);
}

check_type_t check_type(check_t* ck, ast_node_t* node, type_pos_t pos) {
    // Every written type outside a function type's own signature describes
    // storage: a field, a binding, a parameter, a result or a cast target.
    return check_type_at(ck, node, pos, true);
}

// ---- ownership -------------------------------------------------------------------

bool check_owning(const type_t* t) {
    if (type_is_reference(t)) {
        return t->own;
    }
    // an unresolved layout has no answer yet
    return type_layout_pending(t) == NULL && type_is_owning_aggregate(t);
}

// An `own` rvalue: `new(...)`, a call result, `move(...)` or a `cast` to an
// `own` type, none of which any binding could later `del`. `null` is not one
// of them: it owns nothing, whatever `own` type it adopts from its context,
// and `del(null)` is a no-op.
static bool is_owning_rvalue(const expr_t* e) {
    return !e->lvalue && e->value.kind != CV_NULL && check_owning(e->type);
}

bool check_owning_temporary(check_t* ck, loc_t loc, const expr_t* e, const char* what) {
    if (check_poisoned(e->type) || !is_owning_rvalue(e)) {
        return false;
    }
    check_msg_begin(ck);
    msg_str(&ck->msg, "owning temporary would leak: ");
    msg_str(&ck->msg, what);
    check_msg_end(ck, loc);
    return true;
}

// ---- untyped constants in context --------------------------------------------------

static bool type_is_integer_prim(const type_t* t) {
    return t->kind == TYPE_PRIM && prim_is_integer(t->prim);
}

// Whether the untyped constant `v` may take the type `t`, with the diagnostic
// that names the reason. A poisoned `t` reports nothing: the diagnostic that
// poisoned it has already reported, and the error type silences every later
// one. That also keeps the internal name `<error>` out of the message, which
// is what the reader saw after `println(2^63)`. `operand_kind_ok` returns
// true on a poisoned type for the same reason.
//
// Both branches of `default_type` that report reach here poisoned: the
// out-of-range constant and `'null' needs a pointer-typed context`. The second
// also said nothing before the guard. `type_assignable` accepts a poisoned type
// on either side. Thus, the `null` arm below already returned true.
static bool constant_fits(check_t* ck, loc_t loc, cval_t v, const type_t* t, const char* what) {
    if (check_poisoned(t)) {
        return true;
    }
    if (v.kind == CV_NULL) {
        if (type_assignable(t, type_null(&ck->types))) {
            return true;
        }
        check_msg_begin(ck);
        msg_str(&ck->msg, "'null' needs a pointer type, not ");
        check_msg_type(ck, t);
        check_msg_end(ck, loc);
        return false;
    }
    if (t->kind == TYPE_PRIM && cv_fits(v, t->prim)) {
        return true;
    }
    check_msg_begin(ck);
    if (t->kind == TYPE_ENUM) {
        msg_str(&ck->msg, "an integer constant does not become an enum: use cast");
    } else if (t->kind == TYPE_PRIM && t->prim == PRIM_CHAR) {
        msg_str(&ck->msg, "an integer constant does not become char: use cast");
    } else if (t->kind == TYPE_PRIM && t->prim == PRIM_BOOL) {
        msg_str(&ck->msg, "an integer constant does not become bool: write '!= 0'");
    } else if (t->kind == TYPE_PRIM) {
        msg_str(&ck->msg, "constant ");
        cv_to_str(v, &ck->msg);
        msg_str(&ck->msg, " does not fit ");
        check_msg_type(ck, t);
    } else {
        // No constant has that type at all, so the context names itself.
        msg_str(&ck->msg, what != NULL ? what : "the expression");
        msg_str(&ck->msg, " expects ");
        check_msg_type(ck, t);
        msg_str(&ck->msg, ", not a constant");
    }
    check_msg_end(ck, loc);
    return false;
}

// A `void` expression in a value position: the call of a function or of a
// universe builtin that yields nothing.
static bool check_no_value(check_t* ck, ast_node_t* n, expr_t* e) {
    if (e->type->kind != TYPE_VOID) {
        return false;
    }
    check_msg_begin(ck);
    if (e->sym != NULL) {
        msg_quote(&ck->msg, e->sym->name);
        msg_str(&ck->msg, " has no value");
    } else {
        msg_str(&ck->msg, "the expression has no value");
    }
    check_msg_end(ck, n->loc);
    e->type = type_error(&ck->types);
    return true;
}

// Uses the operator rules below. retype_untyped runs this function after them.
// An operator with valueless operands meets its rule when a context fixes its
// type.
static bool untyped_operand_ok(check_t* ck, const ast_node_t* n, const type_t* t);

// Gives every node of an untyped constant expression the type its context fixed.
// A folded node represents its full subtree, so only its value meets the
// context. An unfolded node passes the context to operands that have values.
// `1 << n` with a variable count is one example.
static bool retype_untyped(
    check_t* ck, ast_node_t* n, const type_t* t, bool fit, const char* what) {
    if ((n->ann & CHECK_ANN_UNTYPED) == 0) {
        return true;
    }
    n->ann &= ~(uint32_t)CHECK_ANN_UNTYPED;
    n->type = t;
    const cval_t v = check_node_value(ck, n);
    bool ok = true;
    bool deeper = fit;
    if (v.kind != CV_NONE) {
        ok = !fit || constant_fits(ck, n->loc, v, t, what);
        if (ok && v.kind == CV_CHAR && type_is_integer_prim(t)) {
            set_value(ck, n, cv_as_int(v));
        }
        deeper = false;
    }
    ast_node_t* const kids[] = {n->a, n->b, n->c, n->d};
    for (uint64_t i = 0; i < sizeof kids / sizeof kids[0]; i++) {
        if (kids[i] != NULL && !retype_untyped(ck, kids[i], t, deeper, what)) {
            ok = false;
        }
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        if (!retype_untyped(ck, ast_child(n, i), t, deeper, what)) {
            ok = false;
        }
    }
    if (ok && v.kind == CV_NONE && !untyped_operand_ok(ck, n, t)) {
        // An operator that folded to no value reaches its operand rule here because
        // check_binary could not read a value from its operands. The
        // children go first. A constant that does not fit `t` is reported at
        // its location, so `untyped_operand_ok` stays quiet. In `char c = 1 << n;`, the
        // left operand is integer `1`. The useful message names that constant.
        ok = false;
    }
    return ok;
}

// The order in which the clauses of the rule win, and not an order of width.
// One type covers a whole untyped expression, because there is no promotion
// inside one, so the constants of one expression settle on one clause.
// PRIM_VOID ranks 0 and means "no constant here asks for a type"; so does any
// kind the rule does not name. A fourth default type is one more case.
//
// `char` ranks below integer types. A char literal takes an integer type in an
// integer context. An integer constant never becomes `char`. Thus, only the
// integer clause can hold both.
//
// The float clause of the rule is absent here and present in src/fort/check.ft,
// where it ranks below `char`. This compiler does not fold float constants.
// `cval_kind` has no float member, and the lexer rejects float literals.
static int32_t default_rank(prim_kind_t k) {
    switch (k) {
    case PRIM_CHAR:
        return 1;
    case PRIM_I32:
        return 2;
    case PRIM_I64:
        return 3;
    default:
        return 0;
    }
}

// The one of two default types whose clause wins, in default_rank's order.
static prim_kind_t stronger_default(prim_kind_t a, prim_kind_t b) {
    if (default_rank(b) > default_rank(a)) {
        return b;
    }
    return a;
}

// The default type an untyped expression needs when it folded to no value:
// `4294967296 << n` with a variable count. Such an expression has no value of
// its own. It needs the strongest `default_rank` that one of its constants
// requests. The walk matches `retype_untyped`. A folded node represents its
// full subtree, so its value decides. The walk does not read that subtree.
// PRIM_VOID is the answer when no constant asks
// for a type; `default_type` then keeps `i32`.
//
// The type this chooses is not a type every constant of the expression can
// take. `retype_untyped` decides that, one constant at a time: an integer
// constant never becomes `char`, so `c ? 'a' : 98` takes i32 here and the char
// literal is its code point there.
static prim_kind_t untyped_default_kind(const check_t* ck, const ast_node_t* n) {
    if ((n->ann & CHECK_ANN_UNTYPED) == 0) {
        return PRIM_VOID;
    }
    const cval_t v = check_node_value(ck, n);
    if (v.kind != CV_NONE) {
        // The two clauses this compiler can meet, on the value this node folded
        // to. An integer that fits neither type asks for i64, the widest the
        // integer clause offers, so `retype_untyped` reports it against that.
        switch (v.kind) {
        case CV_INT:
            return cv_fits(v, PRIM_I32) ? PRIM_I32 : PRIM_I64;
        case CV_CHAR:
            return PRIM_CHAR;
        case CV_BOOL:
        case CV_NONE:
        case CV_NULL:
        case CV_STR:
            // The rule names no default type for these. `true` and a string
            // literal are typed already, so only `null` arrives here, and the
            // rule of the null literal reports it where it stands. CV_NONE
            // cannot arrive because the branch above tests for it. This case
            // makes the switch name each kind. The compiler then warns when a
            // kind is added.
            return PRIM_VOID;
        }
        return PRIM_VOID;
    }
    prim_kind_t k = PRIM_VOID;
    const ast_node_t* const kids[] = {n->a, n->b, n->c, n->d};
    for (uint64_t i = 0; i < sizeof kids / sizeof kids[0]; i++) {
        if (kids[i] == NULL) {
            continue;
        }
        k = stronger_default(k, untyped_default_kind(ck, kids[i]));
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        k = stronger_default(k, untyped_default_kind(ck, ast_child(n, i)));
    }
    return k;
}

// The default type of an untyped constant with no context.
static void default_type(check_t* ck, ast_node_t* n, expr_t* e) {
    if (!e->untyped) {
        return;
    }
    e->untyped = false;
    prim_kind_t k = PRIM_I32;
    if (e->value.kind == CV_NULL) {
        check_error(ck, n->loc, "'null' needs a pointer-typed context");
        e->type = type_error(&ck->types);
    } else if (e->value.kind != CV_NONE && !cv_default_kind(e->value, &k)) {
        check_error(ck, n->loc, "constant expression out of range");
        e->type = type_error(&ck->types);
    } else if (e->value.kind == CV_NONE) {
        // `1 << n` with no context: the constants inside the expression take
        // their default type, and one type covers them all. A constant that
        // fits no type of the rule is reported by the retype below.
        k = untyped_default_kind(ck, n);
        if (k == PRIM_VOID) {
            // No constant in it asks for a type, so the expression keeps the
            // i32 that `retype_untyped` used for its earlier diagnostic.
            k = PRIM_I32;
        }
        e->type = type_prim(&ck->types, k);
    } else {
        e->type = type_prim(&ck->types, k);
    }
    if (!retype_untyped(ck, n, e->type, true, NULL)) {
        e->type = type_error(&ck->types);
    }
    n->type = e->type;
    e->value = check_node_value(ck, n);
}

// An expression used for its value with no context of its own, which also
// refuses a call that yields nothing.
static void value_of(check_t* ck, ast_node_t* n, expr_t* e) {
    default_type(ck, n, e);
    if (!check_poisoned(e->type)) {
        (void)check_no_value(ck, n, e);
    }
}

// `check_operand` handles an operator operand that gets no context. The thirteen
// positions include `*e`, `e.f`, `e->f`, `e.len`, `e[i]`, and `e[a..b]`. They
// also include `e()`, `&e`, `del(e)`, `move(e)`, an assignment or `++` target,
// and a range `for` collection. Each position
// drops a poisoned operand and says nothing more about it, which is right for
// an operand that was reported where it arose.
//
// An untyped constant without a default type is poisoned and unreported.
// Examples include written `2^63` and that value inside an expression that
// folded to no value. `untyped` gives it the error type. The default-type rule
// leaves the report to the context. The positions above are not contexts.
// Thus, the constant takes its default type here, as it does without an
// operator.
//
// The rule has two outcomes here, and the second is not an error path. It
// usually reports the constant and leaves the error type. The caller then
// returns. A constant that folds back into range leaves a typed operand without
// a diagnostic. `2^63 >> 1` is the i64 value 2^62. The shift leaves it
// poisoned, and this call restores it. The caller continues, so the operator
// can report `cannot dereference i64`.
//
// `default_type` returns at once for an operand that is no untyped constant, so
// operators handle each typed operand normally.
void check_operand(check_t* ck, ast_node_t* n, expr_t* out) {
    check_expr(ck, n, out);
    if (check_poisoned(out->type)) {
        default_type(ck, n, out);
    }
}

// The ownership rules a value meets when it reaches an expected type. An owning
// target is an `own` place. An owning lvalue enters it only as `move(lv)`.
// An owning rvalue enters it directly. A target that does not own borrows an
// owning lvalue. It would leak an owning rvalue. `implicit_move` is the one
// exception, the `return` of a bare `own` local or parameter.
static void convert_ownership(check_t* ck,
                              ast_node_t* n,
                              expr_t* e,
                              const type_t* target,
                              const char* what,
                              bool implicit_move) {
    if (check_owning(target)) {
        if (!e->lvalue || implicit_move || !check_owning(e->type)) {
            return;
        }
        check_msg_begin(ck);
        msg_str(&ck->msg, "copying an owning value requires 'move'");
        if (e->sym != NULL && n->kind == AST_IDENT) {
            // The hint spells a whole expression, so it is offered only for a
            // name: `move(data)` for `b->data` would name nothing in scope.
            msg_str(&ck->msg, ": write move(");
            msg_view(&ck->msg, e->sym->name);
            msg_str(&ck->msg, ")");
        }
        check_msg_end(ck, n->loc);
        e->type = type_error(&ck->types);
        return;
    }
    if (!is_owning_rvalue(e)) {
        return;
    }
    check_msg_begin(ck);
    msg_str(&ck->msg, "owning temporary would leak: ");
    msg_str(&ck->msg, what);
    msg_str(&ck->msg, " expects ");
    check_msg_type(ck, target);
    check_msg_end(ck, n->loc);
    e->type = type_error(&ck->types);
}

// Converts a checked expression to the type its context expects.
static void convert_at(check_t* ck,
                       ast_node_t* n,
                       expr_t* e,
                       const type_t* target,
                       const char* what,
                       bool implicit_move) {
    if (e->untyped) {
        e->untyped = false;
        if (check_poisoned(target)) {
            e->type = target;
        } else if (retype_untyped(ck, n, target, true, what)) {
            e->type = target;
            e->value = check_node_value(ck, n);
        } else {
            e->type = type_error(&ck->types);
        }
        n->type = e->type;
        return;
    }
    if (check_poisoned(e->type) || check_poisoned(target)) {
        return;
    }
    if (check_no_value(ck, n, e)) {
        return;
    }
    if (!type_assignable(target, e->type)) {
        check_msg_begin(ck);
        msg_str(&ck->msg, what);
        msg_str(&ck->msg, " expects ");
        check_msg_type(ck, target);
        msg_str(&ck->msg, ", not ");
        check_msg_type(ck, e->type);
        check_msg_end(ck, n->loc);
        e->type = type_error(&ck->types);
        return;
    }
    convert_ownership(ck, n, e, target, what, implicit_move);
}

static void convert(check_t* ck, ast_node_t* n, expr_t* e, const type_t* target, const char* what) {
    convert_at(ck, n, e, target, what, false);
}

void check_expr_as(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out) {
    check_expr(ck, node, out);
    convert(ck, node, out, target, what);
}

// Whether a `return` operand directly names a local or parameter. Such an
// operand is an implicit move. Fields, elements, and other lvalues require
// written `move`.
static bool names_a_local(const ast_node_t* n) {
    return n->kind == AST_IDENT && n->sym != NULL &&
           (n->sym->kind == SYM_LOCAL || n->sym->kind == SYM_PARAM);
}

void check_return_value(check_t* ck, ast_node_t* ret, const type_t* target, expr_t* out) {
    ast_node_t* node = ret->a;
    check_expr(ck, node, out);
    const bool implicit = check_owning(target) && out->lvalue && names_a_local(node);
    convert_at(ck, node, out, target, "the return value", implicit);
    if (implicit && !check_poisoned(out->type)) {
        // emptied after the read, so a `defer del(x)` sees zero
        ret->ann |= CHECK_ANN_MOVE;
    }
}

void check_expr_default(check_t* ck, ast_node_t* node, expr_t* out) {
    check_expr(ck, node, out);
    value_of(ck, node, out);
    // no context means no `own` place, so this would leak
    if (check_owning_temporary(ck, node->loc, out, "nothing here could free it")) {
        out->type = type_error(&ck->types);
    }
}

void check_condition(check_t* ck, ast_node_t* node, const char* what) {
    expr_t e;
    check_expr(ck, node, &e);
    value_of(ck, node, &e);
    if (check_poisoned(e.type)) {
        return;
    }
    if (e.type->kind != TYPE_PRIM || e.type->prim != PRIM_BOOL) {
        check_msg_begin(ck);
        msg_str(&ck->msg, what);
        msg_str(&ck->msg, " must be bool, not ");
        check_msg_type(ck, e.type);
        check_msg_end(ck, node->loc);
    }
}

// ---- operators ---------------------------------------------------------------------

static bool op_is_shift(int32_t op) {
    return op == TOK_SHL || op == TOK_SHR;
}

static bool op_is_logical(int32_t op) {
    return op == TOK_AND_AND || op == TOK_PIPE_PIPE;
}

static bool op_is_equality(int32_t op) {
    return op == TOK_EQ || op == TOK_NE;
}

static bool op_is_ordering(int32_t op) {
    return op == TOK_LT || op == TOK_LE || op == TOK_GT || op == TOK_GE;
}

static bool op_is_comparison(int32_t op) {
    return op_is_equality(op) || op_is_ordering(op);
}

// `+ - * /` take integers or floats; every other arithmetic, wrapping and
// bitwise operator is integer-only.
static bool op_takes_floats(int32_t op) {
    return op == TOK_PLUS || op == TOK_MINUS || op == TOK_STAR || op == TOK_SLASH;
}

// The reference `t` holds, lent: `==`, `!=` and `?:` lend their operands, so
// an `own` and a non-`own` operand of the same type compare.
const type_t* check_lend(check_t* ck, const type_t* t) {
    switch (t->kind) {
    case TYPE_PTR:
        return type_ptr(&ck->types, t->elem, false, t->mut);
    case TYPE_SPAN:
        return type_span(&ck->types, t->elem, false, t->mut);
    case TYPE_STRING:
        return type_string(&ck->types, false);
    case TYPE_VOIDPTR:
        // Lending clears `own` and never the `mut` of `void mut*`.
        return type_voidptr(&ck->types, false, t->mut);
    default:
        return t;
    }
}

// Equality is defined on integers, floats, bool, char, enums, pointers,
// function pointers, and string. It is an error on structs, fixed arrays, and
// spans.
static bool type_has_equality(const type_t* t) {
    switch (t->kind) {
    case TYPE_PRIM:
    case TYPE_ENUM:
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_FN:
    case TYPE_STRING:
        return true;
    default:
        return false;
    }
}

// Ordering exists on integers, floats and `char` only.
static bool type_has_ordering(const type_t* t) {
    return t->kind == TYPE_PRIM && t->prim != PRIM_BOOL;
}

// Reports "there is no pointer arithmetic" for a pointer operation. Examples
// include `p + 1`, `p++`, and `p[i]`. Pointers come only from null, address
// operations, `new`, `.ptr`, casts, function names, and calls.
bool check_pointer_arithmetic(check_t* ck, loc_t loc, int32_t op, const type_t* t) {
    if (t->kind != TYPE_PTR && t->kind != TYPE_VOIDPTR) {
        return false;
    }
    check_msg_begin(ck);
    msg_str(&ck->msg, "there is no pointer arithmetic: '");
    msg_str(&ck->msg, tok_kind_name((tok_kind_t)op));
    msg_str(&ck->msg, "' does not apply to ");
    check_msg_type(ck, t);
    check_msg_end(ck, loc);
    return true;
}

// "'+' takes integer operands, not char": one shape for every operand rule,
// naming the operator and the type that broke it.
static void error_operand(check_t* ck, loc_t loc, int32_t op, const char* takes, const type_t* t) {
    check_msg_begin(ck);
    msg_str(&ck->msg, "'");
    msg_str(&ck->msg, tok_kind_name((tok_kind_t)op));
    msg_str(&ck->msg, "' takes ");
    msg_str(&ck->msg, takes);
    msg_str(&ck->msg, ", not ");
    check_msg_type(ck, t);
    check_msg_end(ck, loc);
}

// The exact folding on two untyped constants.
static bool fold_untyped(check_t* ck, loc_t loc, int32_t op, cval_t a, cval_t b, cval_t* out) {
    if ((op == TOK_SLASH || op == TOK_PERCENT) && cv_is_zero(b)) {
        check_error(ck, loc, "constant division by zero");
        return false;
    }
    bool ok = true;
    switch (op) {
    case TOK_PLUS:
        ok = cv_add(a, b, out);
        break;
    case TOK_MINUS:
        ok = cv_sub(a, b, out);
        break;
    case TOK_STAR:
        ok = cv_mul(a, b, out);
        break;
    case TOK_SLASH:
        ok = cv_div(a, b, out);
        break;
    case TOK_PERCENT:
        ok = cv_rem(a, b, out);
        break;
    case TOK_AMP:
        ok = cv_and(a, b, out);
        break;
    case TOK_PIPE:
        ok = cv_or(a, b, out);
        break;
    case TOK_CARET:
        ok = cv_xor(a, b, out);
        break;
    case TOK_PLUS_WRAP:
        ok = cv_add(a, b, out);
        break;
    case TOK_MINUS_WRAP:
        ok = cv_sub(a, b, out);
        break;
    case TOK_STAR_WRAP:
        // An untyped integer has no width, so it folds exactly.
        ok = cv_mul(a, b, out);
        break;
    default:
        fatal_internal("check: folding an operator that is not arithmetic");
    }
    if (!ok) {
        check_error(ck, loc, "constant expression out of range");
    }
    return ok;
}

// Applies each checked-mode rule during compile-time folding. Thus, overflow or
// division by zero is a compile error, not a run-time trap.
static bool fold_typed(
    check_t* ck, loc_t loc, int32_t op, cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    if ((op == TOK_SLASH || op == TOK_PERCENT) && cv_is_zero(b)) {
        check_error(ck, loc, "constant division by zero");
        return false;
    }
    bool ok = true;
    switch (op) {
    case TOK_PLUS:
        ok = cv_typed_add(a, b, t, out);
        break;
    case TOK_MINUS:
        ok = cv_typed_sub(a, b, t, out);
        break;
    case TOK_STAR:
        ok = cv_typed_mul(a, b, t, out);
        break;
    case TOK_SLASH:
        ok = cv_typed_div(a, b, t, out);
        break;
    case TOK_PERCENT:
        ok = cv_typed_rem(a, b, t, out);
        break;
    case TOK_AMP:
        ok = cv_typed_and(a, b, t, out);
        break;
    case TOK_PIPE:
        ok = cv_typed_or(a, b, t, out);
        break;
    case TOK_CARET:
        ok = cv_typed_xor(a, b, t, out);
        break;
    case TOK_PLUS_WRAP:
        *out = cv_wrap_add(a, b, t);
        break;
    case TOK_MINUS_WRAP:
        *out = cv_wrap_sub(a, b, t);
        break;
    case TOK_STAR_WRAP:
        *out = cv_wrap_mul(a, b, t);
        break;
    default:
        fatal_internal("check: folding an operator that is not arithmetic");
    }
    if (!ok) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "constant expression overflows ");
        msg_str(&ck->msg, prim_name(t));
        check_msg_end(ck, loc);
    }
    return ok;
}

static cv_rel_t relation_of(int32_t op) {
    switch (op) {
    case TOK_EQ:
        return CV_REL_EQ;
    case TOK_NE:
        return CV_REL_NE;
    case TOK_LT:
        return CV_REL_LT;
    case TOK_LE:
        return CV_REL_LE;
    case TOK_GT:
        return CV_REL_GT;
    default:
        break;
    }
    return CV_REL_GE;
}

// Applies shift rules. The left operand keeps its type. The count does not give
// it a context. A constant count outside the width is a compile error.
static void check_shift(
    check_t* ck, loc_t loc, int32_t op, expr_t* a, ast_node_t* rhs, expr_t* b, expr_t* out) {
    default_type(ck, rhs, b);
    // An untyped left operand whose value has no default type, `2^63` written
    // out, stays poisoned but keeps its constant. It continues to the context.
    // The context reports it against the type that the default rule chose.
    // `untyped` gives such a constant the error type and reports nothing, so
    // an early return on a poisoned operand would lose the diagnostic
    // altogether. Every other early return that can see one calls
    // `check_operand`. That function assigns the default type and reports the
    // constant. A shift keeps this shape because it can fold back into range.
    // For example, `9223372036854775808 >> 1` is a legal 2^62. This return
    // keeps that wide half for the context.
    if ((check_poisoned(a->type) && !a->untyped) || check_poisoned(b->type)) {
        return;
    }
    if (!type_is_integer_prim(b->type)) {
        error_operand(ck, loc, op, "an integer count", b->type);
        return;
    }
    if (!a->untyped && !type_is_integer_prim(a->type)) {
        error_operand(ck, loc, op, "an integer left operand", a->type);
        return;
    }
    out->type = a->type;
    out->untyped = a->untyped;
    const bool counted = cv_is_int(b->value);
    if (a->untyped) {
        // with no width yet, the count is checked against 0..63
        int64_t count = 0;
        if (counted && (!cv_to_i64(b->value, &count) || count < 0 || count > CV_SHIFT_MAX)) {
            check_error(ck, loc, "constant shift count must be in 0..63");
            out->type = type_error(&ck->types);
            out->untyped = false;
            return;
        }
        if (counted && cv_is_int_like(a->value)) {
            cval_t v = cv_none();
            const cval_t left = cv_as_int(a->value);
            const bool ok = op == TOK_SHL ? cv_shl(left, b->value, &v) : cv_shr(left, b->value, &v);
            if (!ok) {
                // the count is in range, so the exact result left it
                check_error(ck, loc, "constant expression out of range");
                out->type = type_error(&ck->types);
                out->untyped = false;
                return;
            }
            out->value = v;
        }
        return;
    }
    const uint32_t width = prim_size(a->type->prim) * PRIM_BITS_PER_BYTE;
    if (counted) {
        int64_t n = 0;
        if (!cv_to_i64(b->value, &n) || n < 0 || (uint64_t)n >= width) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "shift count must be in 0..");
            msg_uint(&ck->msg, width - 1);
            msg_str(&ck->msg, " for ");
            check_msg_type(ck, a->type);
            check_msg_end(ck, loc);
            out->type = type_error(&ck->types);
            return;
        }
        if (cv_is_int(a->value)) {
            cval_t v = cv_none();
            const bool ok = op == TOK_SHL ? cv_typed_shl(a->value, b->value, a->type->prim, &v)
                                          : cv_typed_shr(a->value, b->value, a->type->prim, &v);
            if (ok) {
                out->value = v;
            }
        }
    }
}

// The default type of two untyped operands taken together, when one of them
// folded to no value. Neither operand gives the other a context. The default
// type rule therefore answers for the pair. One type covers the complete
// untyped expression. The strongest clause requested by either side wins.
static const type_t* untyped_pair_type(check_t* ck, const ast_node_t* lhs, const ast_node_t* rhs) {
    prim_kind_t k = stronger_default(untyped_default_kind(ck, lhs), untyped_default_kind(ck, rhs));
    if (k == PRIM_VOID) {
        // No constant on either side asks for a type, so the pair keeps the i32
        // `default_type` keeps in the same case. The one program that reaches
        // this line is `(c ? null : null) == (c ? null : null)`, which this
        // compiler refuses at the `?:`; the fort twin reaches it and names it.
        k = PRIM_I32;
    }
    return type_prim(&ck->types, k);
}

// Two untyped constants: the arithmetic folds exactly and a comparison yields
// a bool.
static void check_untyped_pair(check_t* ck,
                               loc_t loc,
                               int32_t op,
                               ast_node_t* lhs,
                               expr_t* a,
                               ast_node_t* rhs,
                               expr_t* b,
                               expr_t* out) {
    const bool chars = a->value.kind == CV_CHAR && b->value.kind == CV_CHAR;
    if (a->value.kind == CV_NULL || b->value.kind == CV_NULL) {
        check_error(ck, loc, "'null' has no type of its own");
        return;
    }
    if (op_is_logical(op)) {
        error_operand(ck, loc, op, "bool operands", a->type);
        return;
    }
    if (a->value.kind == CV_NONE || b->value.kind == CV_NONE) {
        // One operand folded to no value, so nothing folds and both operands
        // need a type from outside the pair. Neither gives one to the other.
        if (!op_is_comparison(op)) {
            // The pair stays untyped, so the context that fixes its type walks
            // both operands and reports every constant against it.
            out->type = a->type;
            out->untyped = true;
            return;
        }
        // A comparison yields `bool`, so no context reaches its operands. Their
        // default type must be fixed here. The rule sets one type for both
        // sides. Using only the left type dropped the right poison and constant.
        // Then `2^63 > (1 << m)` printed `false`. It also emitted two widths for
        // `2147483648 > (1 << m)`.
        out->type = type_prim(&ck->types, PRIM_BOOL);
        const type_t* const t = untyped_pair_type(ck, lhs, rhs);
        const bool left_ok = retype_untyped(ck, lhs, t, true, NULL);
        const bool right_ok = retype_untyped(ck, rhs, t, true, NULL);
        if (!left_ok || !right_ok) {
            // the constant is reported, so the comparison says no more
            out->type = type_error(&ck->types);
        }
        return;
    }
    if (op_is_comparison(op)) {
        out->type = type_prim(&ck->types, PRIM_BOOL);
        out->value = cv_compare(relation_of(op),
                                chars ? a->value : cv_as_int(a->value),
                                chars ? b->value : cv_as_int(b->value));
        return;
    }
    cval_t v = cv_none();
    if (!fold_untyped(ck, loc, op, cv_as_int(a->value), cv_as_int(b->value), &v)) {
        return;
    }
    out->value = v;
    out->untyped = true;
    prim_kind_t k = PRIM_I32;
    out->type = cv_default_kind(v, &k) ? type_prim(&ck->types, k) : type_error(&ck->types);
}

// The operand rules on the type the operator is applied to.
static bool operand_kind_ok(check_t* ck, loc_t loc, int32_t op, const type_t* t) {
    if (check_poisoned(t)) {
        return true;
    }
    if (op_is_logical(op)) {
        if (t->kind != TYPE_PRIM || t->prim != PRIM_BOOL) {
            error_operand(ck, loc, op, "bool operands", t);
            return false;
        }
        return true;
    }
    if (op_is_comparison(op)) {
        if (op_is_ordering(op) ? !type_has_ordering(t) : !type_has_equality(t)) {
            error_operand(
                ck, loc, op, op_is_ordering(op) ? "ordered operands" : "comparable operands", t);
            return false;
        }
        return true;
    }
    if (!type_is_integer_prim(t) && !(op_takes_floats(op) && type_is_float(t))) {
        if (check_pointer_arithmetic(ck, loc, op, t)) {
            return false;
        }
        error_operand(
            ck, loc, op, op_takes_floats(op) ? "numeric operands" : "integer operands", t);
        return false;
    }
    return true;
}

// The operand rules on the type a context fixed for an operator that folded to
// no value, `1 << n` with a variable count. The operands have no value there,
// so check_operands cannot read their kind. It defers the rule until the type
// is known. retype_untyped is that point. No context can change the rule. A
// shift names its left operand because that operand owns the type. The count
// has its own type, which check_shift already checked.
static bool untyped_operand_ok(check_t* ck, const ast_node_t* n, const type_t* t) {
    if (n->kind != AST_BINARY) {
        return true;
    }
    if (op_is_shift(n->op)) {
        if (check_poisoned(t) || type_is_integer_prim(t)) {
            return true;
        }
        error_operand(ck, n->loc, n->op, "an integer left operand", t);
        return false;
    }
    return operand_kind_ok(ck, n->loc, n->op, t);
}

void check_operands(check_t* ck,
                    loc_t loc,
                    int32_t op,
                    ast_node_t* lhs,
                    expr_t* a,
                    ast_node_t* rhs,
                    expr_t* b,
                    expr_t* out) {
    out->type = type_error(&ck->types);
    out->value = cv_none();
    out->untyped = false;
    out->lvalue = false;
    out->mut = false;
    out->empty = EMPTY_IMMUTABLE;
    out->sym = NULL;
    if (op_is_shift(op)) {
        check_shift(ck, loc, op, a, rhs, b, out);
        return;
    }
    if (a->untyped && b->untyped) {
        check_untyped_pair(ck, loc, op, lhs, a, rhs, b, out);
        return;
    }
    // the typed operand decides before the untyped one adopts
    if (!operand_kind_ok(ck, loc, op, a->untyped ? b->type : a->type)) {
        return;
    }
    if (a->untyped) {
        convert(ck, lhs, a, b->type, "the operand");
    } else if (b->untyped) {
        convert(ck, rhs, b, a->type, "the operand");
    }
    if (check_poisoned(a->type) || check_poisoned(b->type)) {
        return;
    }
    if (op_is_equality(op)) {
        // Lending leaves no owning rvalue to free.
        if (check_owning_temporary(ck, lhs->loc, a, "comparing it leaves no owner") ||
            check_owning_temporary(ck, rhs->loc, b, "comparing it leaves no owner")) {
            return;
        }
    }
    const type_t* lt = op_is_equality(op) ? check_lend(ck, a->type) : a->type;
    const type_t* rt = op_is_equality(op) ? check_lend(ck, b->type) : b->type;
    if (!type_equal(lt, rt)) {
        // there is no promotion, mutability levels included
        check_msg_begin(ck);
        msg_str(&ck->msg, "'");
        msg_str(&ck->msg, tok_kind_name((tok_kind_t)op));
        msg_str(&ck->msg, "' takes two operands of the same type, not ");
        check_msg_type(ck, a->type);
        msg_str(&ck->msg, " and ");
        check_msg_type(ck, b->type);
        check_msg_end(ck, loc);
        return;
    }
    const type_t* t = lt;
    if (op_is_logical(op)) {
        out->type = t;
        if (a->value.kind == CV_BOOL && b->value.kind == CV_BOOL) {
            out->value =
                op == TOK_AND_AND ? cv_land(a->value, b->value) : cv_lor(a->value, b->value);
        }
        return;
    }
    if (op_is_comparison(op)) {
        out->type = type_prim(&ck->types, PRIM_BOOL);
        if (a->value.kind != CV_NONE && b->value.kind != CV_NONE) {
            out->value = cv_compare(relation_of(op), a->value, b->value);
        }
        return;
    }
    out->type = t;
    if (cv_is_int(a->value) && cv_is_int(b->value)) {
        cval_t v = cv_none();
        if (!fold_typed(ck, loc, op, a->value, b->value, t->prim, &v)) {
            out->type = type_error(&ck->types);
            return;
        }
        out->value = v;
    }
}

// ---- expressions ----------------------------------------------

static void expr_clear(check_t* ck, expr_t* out) {
    out->type = type_error(&ck->types);
    out->value = cv_none();
    out->untyped = false;
    out->lvalue = false;
    out->mut = false;
    out->empty = EMPTY_IMMUTABLE;
    out->init_const = false;
    out->sym = NULL;
}

// An untyped constant: its type is provisional until a context fixes it, so
// the node is marked and the value recorded.
static void untyped(check_t* ck, ast_node_t* n, expr_t* out, cval_t v) {
    out->value = v;
    out->untyped = true;
    out->init_const = true;
    prim_kind_t k = PRIM_I32;
    out->type = cv_default_kind(v, &k) ? type_prim(&ck->types, k) : type_error(&ck->types);
    if (v.kind == CV_NULL) {
        out->type = type_null(&ck->types);
    }
    n->ann |= CHECK_ANN_UNTYPED;
    set_value(ck, n, v);
}

// The member of an enum, or NULL: members are scoped to their enum and live in
// no namespace.
static const ast_node_t* enum_member(const sym_t* e, str_t name) {
    const ast_node_t* decl = e->node;
    for (uint64_t i = 0; decl != NULL && i < ast_len(decl); i++) {
        const ast_node_t* m = ast_child(decl, i);
        if (m->kind == AST_ENUM_MEMBER && str_eq(m->name, name)) {
            return m;
        }
    }
    return NULL;
}

static const ast_node_t* struct_field(const sym_t* s, str_t name) {
    const ast_node_t* decl = s->node;
    for (uint64_t i = 0; decl != NULL && i < ast_len(decl); i++) {
        const ast_node_t* f = ast_child(decl, i);
        if (f->kind == AST_FIELD_DECL && str_eq(f->name, name)) {
            return f;
        }
    }
    return NULL;
}

// Sets the value that an identifier or qualified name denotes. A constant
// carries its folded value. A variable is an lvalue with level-0 mutability.
static void value_of_sym(check_t* ck, ast_node_t* n, const sym_t* s, expr_t* out) {
    resolve_sym(ck, (sym_t*)s);
    out->sym = s;
    out->type = s->type != NULL ? s->type : type_error(&ck->types);
    switch (s->kind) {
    case SYM_CONST:
        // `move` and `del` cannot empty read-only memory.
        out->lvalue = true;
        out->empty = EMPTY_READONLY;
        out->value =
            s->node != NULL && s->node->b != NULL ? check_node_value(ck, s->node->b) : cv_none();
        out->init_const = true;
        break;
    case SYM_GLOBAL:
    case SYM_LOCAL:
    case SYM_PARAM:
        // a read of a `mut` global is not constant
        // own storage is emptiable whether or not the binding is `mut`
        out->lvalue = true;
        out->mut = s->mut0;
        out->empty = EMPTY_OK;
        break;
    case SYM_EXTERN_FN:
        if (n != ck->callee) {
            // An `extern fn` in value position is an error. Calls use the
            // variadic LLVM type from its declaration. An indirect call has no
            // callee declaration for that form. Its vector-register count would
            // therefore stay unset.
            check_msg_begin(ck);
            msg_quote(&ck->msg, s->name);
            msg_str(&ck->msg,
                    " is an extern function, which is not a value: wrap it in a fort function "
                    "to take a function pointer");
            check_msg_end(ck, n->name_loc);
            out->type = type_error(&ck->types);
            break;
        }
        out->init_const = true;
        break;
    case SYM_FN:
        // `&f` is an error; a function name initializes
        out->init_const = true;
        break;
    case SYM_ENUM_MEMBER:
        out->value = s->node != NULL ? check_node_value(ck, s->node) : cv_none();
        out->init_const = true;
        break;
    default:
        check_msg_begin(ck);
        msg_quote(&ck->msg, s->name);
        msg_str(&ck->msg, " is a ");
        msg_str(&ck->msg, sym_kind_name(s->kind));
        msg_str(&ck->msg, ", not a value");
        check_msg_end(ck, n->name_loc);
        out->type = type_error(&ck->types);
        break;
    }
}

static void check_ident(check_t* ck, ast_node_t* n, expr_t* out) {
    const binding_t* b = lookup(ck, n->name);
    if (b == NULL) {
        const sym_t* builtin = check_builtin(ck, n->name);
        if (builtin != NULL) {
            n->sym = builtin;
            check_msg_begin(ck);
            msg_quote(&ck->msg, n->name);
            msg_str(&ck->msg, " cannot be used as a value");
            check_msg_end(ck, n->name_loc);
            return;
        }
        error_unknown_name(ck, n->name, n->name_loc);
        return;
    }
    const sym_t* s = sym_of_binding(b);
    n->sym = s;
    if (s == NULL) {
        return;
    }
    value_of_sym(ck, n, s, out);
}

// The module a binding denotes, or NULL: only the module reading of an import
// binds one.
static const module_t* module_of(const binding_t* b) {
    return b != NULL && b->kind == BIND_MODULE ? (const module_t*)b->module : NULL;
}

// What an expression names when it is not a value: a module or a type, which
// only a `.` may follow. NULL for every ordinary expression. The binding is
// returned rather than the symbol because a module is reached through it.
static const binding_t* names_module_or_type(check_t* ck, ast_node_t* n) {
    const binding_t* b = NULL;
    if (n->kind == AST_IDENT) {
        b = lookup(ck, n->name);
    } else if (n->kind == AST_FIELD && n->a != NULL) {
        // Qualified access sees the declarations of a module, not its imports
        // Imports are not re-exported. Thus, `m.other.f` is an
        // error even when `m` imports `other`.
        // `m.other.f` is an error even when `m` imports `other`
        b = module_declaration(module_of(names_module_or_type(ck, n->a)), n->name);
    }
    const sym_t* s = sym_of_binding(b);
    if (s == NULL || (s->kind != SYM_MODULE && s->kind != SYM_STRUCT && s->kind != SYM_ENUM)) {
        return NULL;
    }
    n->sym = s;
    return b;
}

// `.len` and `.ptr`, the read-only pseudo-fields of arrays, spans and strings.
static bool pseudo_field(check_t* ck, ast_node_t* n, const type_t* t, expr_t* op, expr_t* out) {
    const bool len = str_eq(n->name, str_from_cstr("len"));
    const bool ptr = str_eq(n->name, str_from_cstr("ptr"));
    if (!len && !ptr) {
        return false;
    }
    if (t->kind == TYPE_ARRAY && len) {
        // the operand of `.len` is not evaluated
        untyped(ck, n, out, cv_from_u64(t->len));
        return true;
    }
    if (t->kind == TYPE_SPAN || t->kind == TYPE_STRING) {
        if (len) {
            out->type = type_prim(&ck->types, PRIM_U64);
            return true;
        }
        const type_t* elem = t->kind == TYPE_STRING ? type_prim(&ck->types, PRIM_CHAR) : t->elem;
        out->type = type_ptr(&ck->types, elem, false, t->kind == TYPE_SPAN && t->mut);
        return true;
    }
    if (t->kind == TYPE_ARRAY && ptr) {
        check_error(ck, n->name_loc, "a fixed array has no '.ptr'");
        return true;
    }
    (void)op;
    return false;
}

// `e.f`, `p->f` and the qualified forms.
static void check_field(check_t* ck, ast_node_t* n, expr_t* out, bool arrow) {
    if (!arrow) {
        const binding_t* qb = names_module_or_type(ck, n->a);
        const sym_t* q = sym_of_binding(qb);
        const module_t* qm = module_of(qb);
        if (qm != NULL) {
            const binding_t* b = module_declaration(qm, n->name);
            if (b == NULL) {
                check_msg_begin(ck);
                msg_str(&ck->msg, "module ");
                msg_quote(&ck->msg, qm->path);
                msg_str(&ck->msg, " has no declaration named ");
                msg_quote(&ck->msg, n->name);
                check_msg_end(ck, n->name_loc);
                return;
            }
            const sym_t* s = sym_of_binding(b);
            n->sym = s;
            if (s != NULL) {
                value_of_sym(ck, n, s, out);
            }
            return;
        }
        if (q != NULL && q->kind == SYM_ENUM) {
            resolve_sym(ck, (sym_t*)q);
            const ast_node_t* m = enum_member(q, n->name);
            if (m == NULL) {
                check_msg_begin(ck);
                msg_str(&ck->msg, "enum ");
                msg_view(&ck->msg, q->name);
                msg_str(&ck->msg, " has no member ");
                msg_quote(&ck->msg, n->name);
                check_msg_end(ck, n->name_loc);
                return;
            }
            n->sym = m->sym;
            out->sym = m->sym;
            out->type = q->type;
            out->value = check_node_value(ck, m);
            out->init_const = true;
            return;
        }
        if (q != NULL) {
            check_msg_begin(ck);
            msg_quote(&ck->msg, q->name);
            msg_str(&ck->msg, " is a type, not a value");
            check_msg_end(ck, n->a->loc);
            return;
        }
    }
    expr_t op;
    check_operand(ck, n->a, &op);
    if (check_poisoned(op.type)) {
        return;
    }
    if (check_owning_temporary(ck, n->loc, &op, "a field of it leaves no owner")) {
        return;
    }
    const type_t* t = op.type;
    bool mut = op.mut;
    bool lvalue = op.lvalue;
    // a field of a local counts as the local
    empty_kind_t empty = op.empty;
    if (arrow) {
        if (t->kind != TYPE_PTR) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "'->' needs a pointer, not ");
            check_msg_type(ck, t);
            msg_str(&ck->msg, ": use '.'");
            check_msg_end(ck, n->loc);
            return;
        }
        mut = type_level_mut(t, 1);
        // The read-only memory of a constant stops at the first indirection.
        empty = mut ? EMPTY_OK : EMPTY_IMMUTABLE;
        lvalue = true;
        t = t->elem;
    } else if (t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "'.' on a pointer of type ");
        check_msg_type(ck, t);
        msg_str(&ck->msg, ": use '->'");
        check_msg_end(ck, n->loc);
        return;
    }
    if (pseudo_field(ck, n, t, &op, out)) {
        return;
    }
    if (t->kind != TYPE_STRUCT) {
        check_msg_begin(ck);
        check_msg_type(ck, t);
        msg_str(&ck->msg, " has no field ");
        msg_quote(&ck->msg, n->name);
        check_msg_end(ck, n->name_loc);
        return;
    }
    const sym_t* s = nominal_sym(t);
    const ast_node_t* f = s != NULL ? struct_field(s, n->name) : NULL;
    if (f == NULL) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "struct ");
        msg_view(&ck->msg, t->name);
        msg_str(&ck->msg, " has no field ");
        msg_quote(&ck->msg, n->name);
        check_msg_end(ck, n->name_loc);
        return;
    }
    n->sym = f->sym;
    out->sym = f->sym;
    out->type = f->sym != NULL ? f->sym->type : type_error(&ck->types);
    out->lvalue = lvalue;
    out->mut = mut;
    out->empty = empty;
}

// ---- unary, indexing and span expressions ------------------------------------------

static void check_unary(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_t a;
    if (n->op == TOK_AMP) {
        // `&e` requires an lvalue. It yields a borrowed pointer whose level 1
        // matches the mutability of `e`. The checker asks for the operand address,
        // not its value. This lets a module declaration hold its own address. The
        // flag is saved and restored because `&` can occur anywhere.
        const bool outer_addr_only = ck->addr_only;
        ck->addr_only = true;
        check_operand(ck, n->a, &a);
        ck->addr_only = outer_addr_only;
        if (check_poisoned(a.type)) {
            return;
        }
        if (a.sym != NULL && (a.sym->kind == SYM_FN || a.sym->kind == SYM_EXTERN_FN)) {
            check_error(ck, n->loc, "'&' on a function: a function name is already a value");
            return;
        }
        if (!a.lvalue) {
            check_error(ck, n->loc, "'&' requires an lvalue");
            return;
        }
        out->type = type_ptr(&ck->types, a.type, false, a.mut);
        out->init_const = a.sym != NULL && (a.sym->kind == SYM_CONST || a.sym->kind == SYM_GLOBAL);
        return;
    }
    if (n->op == TOK_STAR) {
        check_operand(ck, n->a, &a);
        if (check_poisoned(a.type)) {
            return;
        }
        if (a.type->kind != TYPE_PTR) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "cannot dereference ");
            check_msg_type(ck, a.type);
            check_msg_end(ck, n->loc);
            return;
        }
        if (check_owning_temporary(ck, n->loc, &a, "dereferencing it leaves no owner")) {
            return;
        }
        out->type = a.type->elem;
        out->lvalue = true;
        out->mut = type_level_mut(a.type, 1);
        // read-only memory stops at the indirection
        out->empty = out->mut ? EMPTY_OK : EMPTY_IMMUTABLE;
        return;
    }
    check_expr(ck, n->a, &a);
    if (n->op == TOK_BANG) {
        convert(ck, n->a, &a, type_prim(&ck->types, PRIM_BOOL), "'!'");
        if (check_poisoned(a.type)) {
            return;
        }
        out->type = a.type;
        if (a.value.kind == CV_BOOL) {
            out->value = cv_lnot(a.value);
            out->init_const = true;
        }
        return;
    }
    if (a.untyped) {
        // `~c` is `-c - 1`
        if (cv_is_int_like(a.value)) {
            cval_t v = cv_none();
            const cval_t x = cv_as_int(a.value);
            if (!(n->op == TOK_MINUS ? cv_neg(x, &v) : cv_not(x, &v))) {
                check_error(ck, n->loc, "constant expression out of range");
                return;
            }
            untyped(ck, n, out, v);
            return;
        }
        value_of(ck, n->a, &a);
    }
    if (check_poisoned(a.type)) {
        return;
    }
    if (!type_is_integer_prim(a.type) && !(n->op == TOK_MINUS && type_is_float(a.type))) {
        error_operand(ck, n->loc, n->op, "an integer operand", a.type);
        return;
    }
    if (n->op == TOK_MINUS && !prim_is_signed(a.type->prim) && !type_is_float(a.type)) {
        error_operand(ck, n->loc, n->op, "a signed operand", a.type);
        return;
    }
    out->type = a.type;
    if (cv_is_int(a.value)) {
        cval_t v = cv_none();
        const bool ok = n->op == TOK_MINUS ? cv_typed_neg(a.value, a.type->prim, &v)
                                           : cv_typed_not(a.value, a.type->prim, &v);
        if (!ok) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "constant expression overflows ");
            msg_str(&ck->msg, prim_name(a.type->prim));
            check_msg_end(ck, n->loc);
            out->type = type_error(&ck->types);
            return;
        }
        out->value = v;
        out->init_const = true;
    }
}

// An index, a span bound or a `new` count: any integer type is fine there and
// a negative constant is a compile error.
static bool check_count(check_t* ck, ast_node_t* n, const char* what, cval_t* value) {
    expr_t e;
    check_expr(ck, n, &e);
    *value = e.value;
    if (cv_is_int(e.value) && cv_is_neg(e.value)) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "a negative ");
        msg_str(&ck->msg, what);
        msg_str(&ck->msg, ": ");
        cv_to_str(e.value, &ck->msg);
        check_msg_end(ck, n->loc);
        return false;
    }
    value_of(ck, n, &e);
    if (check_poisoned(e.type)) {
        return false;
    }
    if (!type_is_integer_prim(e.type)) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "a ");
        msg_str(&ck->msg, what);
        msg_str(&ck->msg, " must be an integer, not ");
        check_msg_type(ck, e.type);
        check_msg_end(ck, n->loc);
        return false;
    }
    return true;
}

// The element type of an operand that may be indexed or spanned.
static const type_t* element_of(check_t* ck, const type_t* t) {
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_SPAN) {
        return t->elem;
    }
    if (t->kind == TYPE_STRING) {
        return type_prim(&ck->types, PRIM_CHAR);
    }
    return NULL;
}

static void check_index(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_t a;
    check_operand(ck, n->a, &a);
    cval_t index = cv_none();
    const bool ok = check_count(ck, n->b, "index", &index);
    if (check_poisoned(a.type)) {
        return;
    }
    if (check_owning_temporary(ck, n->loc, &a, "indexing it leaves no owner")) {
        return;
    }
    const type_t* elem = element_of(ck, a.type);
    if (elem == NULL) {
        check_msg_begin(ck);
        check_msg_type(ck, a.type);
        msg_str(&ck->msg,
                a.type->kind == TYPE_PTR ? " cannot be indexed: write '(*p)[i]'"
                                         : " cannot be indexed");
        check_msg_end(ck, n->loc);
        return;
    }
    if (!ok) {
        return;
    }
    if (a.type->kind == TYPE_ARRAY && cv_is_int(index)) {
        uint64_t i = 0;
        if (!cv_to_u64(index, &i) || i >= a.type->len) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "index ");
            cv_to_str(index, &ck->msg);
            msg_str(&ck->msg, " out of range for ");
            check_msg_type(ck, a.type);
            check_msg_end(ck, n->loc);
            return;
        }
    }
    out->type = elem;
    // indexing an rvalue array yields a copy
    out->lvalue = a.type->kind != TYPE_ARRAY || a.lvalue;
    out->mut = a.type->kind == TYPE_ARRAY ? a.mut : (a.type->kind == TYPE_SPAN && a.type->mut);
    if (a.type->kind == TYPE_ARRAY) {
        out->empty = a.empty;
    } else {
        out->empty = out->mut ? EMPTY_OK : EMPTY_IMMUTABLE;
    }
}

static void check_span_expr(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_t a;
    check_operand(ck, n->a, &a);
    bool ok = true;
    cval_t bound = cv_none();
    if (n->b != NULL && !check_count(ck, n->b, "span bound", &bound)) {
        ok = false;
    }
    if (n->c != NULL && !check_count(ck, n->c, "span bound", &bound)) {
        ok = false;
    }
    if (check_poisoned(a.type) || !ok) {
        return;
    }
    if (check_owning_temporary(ck, n->loc, &a, "a span of it leaves no owner")) {
        return;
    }
    if (a.type->kind == TYPE_PTR) {
        // a pointer has no length, so only the two-bound form
        if (n->b == NULL || n->c == NULL) {
            check_error(ck, n->loc, "a pointer has no length: write 'p[lo..hi]'");
            return;
        }
        out->type = type_span(&ck->types, a.type->elem, false, a.type->mut);
        return;
    }
    if (a.type->kind == TYPE_STRING) {
        out->type = type_string(&ck->types, false);
        return;
    }
    const type_t* elem = element_of(ck, a.type);
    if (elem == NULL) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "cannot take a span of ");
        check_msg_type(ck, a.type);
        check_msg_end(ck, n->loc);
        return;
    }
    if (a.type->kind == TYPE_ARRAY && !a.lvalue) {
        check_error(ck, n->loc, "a span of a fixed array needs an lvalue");
        return;
    }
    const bool mut = a.type->kind == TYPE_ARRAY ? a.mut : a.type->mut;
    out->type = type_span(&ck->types, elem, false, mut);
}

// ---- cast, sizeof and new ----------------------------------------------------------

static void check_cast(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_t a;
    check_expr(ck, n->a, &a);
    if (a.value.kind == CV_NULL) {
        check_error(ck, n->a->loc, "'null' is not a cast operand");
        (void)check_type(ck, n->b, TYPE_POS_CAST);
        return;
    }
    // a `cast` is not a context
    value_of(ck, n->a, &a);
    const check_type_t target = check_type(ck, n->b, TYPE_POS_CAST);
    if (check_poisoned(a.type) || check_poisoned(target.type)) {
        return;
    }
    if (!check_layout(ck, target.type) || !check_size_fits(ck, n->loc, target.type)) {
        return;
    }
    if (!type_cast_allowed(target.type, a.type)) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "cannot cast ");
        check_msg_type(ck, a.type);
        msg_str(&ck->msg, " to ");
        check_msg_type(ck, target.type);
        if (type_cast_adds_mut(target.type, a.type) &&
            type_cast_allowed(type_without_mut(&ck->types, target.type), a.type)) {
            // The mark is named when the mark is the reason: the same cast to
            // the same target without its marks is allowed. A pair that another
            // row refuses as well says nothing about the mark, since a reader
            // who drops it meets the second refusal.
            msg_str(&ck->msg, ": a cast never adds 'mut'");
        }
        check_msg_end(ck, n->loc);
        return;
    }
    if (check_owning(target.type)) {
        if (a.lvalue && check_owning(a.type)) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "casting an owning value to an owning type requires 'move'");
            if (a.sym != NULL && n->a->kind == AST_IDENT) {
                // As above: a hint that spells an expression is offered only
                // for a name.
                msg_str(&ck->msg, ": write move(");
                msg_view(&ck->msg, a.sym->name);
                msg_str(&ck->msg, ")");
            }
            check_msg_end(ck, n->loc);
            return;
        }
    } else if (check_owning_temporary(ck, n->loc, &a, "the cast target does not own it")) {
        return;
    }
    out->type = target.type;
    // the value the run time would produce
    const bool numeric = target.type->kind == TYPE_PRIM || target.type->kind == TYPE_ENUM;
    if (numeric && cv_is_int_like(a.value) &&
        (a.type->kind == TYPE_PRIM || a.type->kind == TYPE_ENUM)) {
        const prim_kind_t k =
            target.type->kind == TYPE_ENUM ? (prim_kind_t)ENUM_UNDERLYING : target.type->prim;
        cval_t v = cv_none();
        if (cv_cast(a.value, k, &v)) {
            out->value = v;
            out->init_const = true;
        }
    }
}

static void check_sizeof(check_t* ck, ast_node_t* n, expr_t* out) {
    const check_type_t t = check_type(ck, n->a, TYPE_POS_BINDING);
    if (check_poisoned(t.type)) {
        return;
    }
    if (t.type->kind == TYPE_VOID) {
        check_error(ck, n->loc, "'sizeof' needs a sized type, not void");
        return;
    }
    if (!check_layout(ck, t.type) || !check_size_fits(ck, n->loc, t.type)) {
        return;
    }
    untyped(ck, n, out, cv_from_u64(type_sizeof(t.type)));
}

static void check_new(check_t* ck, ast_node_t* n, expr_t* out) {
    const check_type_t t = check_type(ck, n->a, TYPE_POS_ALLOC);
    cval_t count = cv_none();
    const bool counted = n->b != NULL;
    const bool ok = !counted || check_count(ck, n->b, "count", &count);
    if (check_poisoned(t.type) || !ok) {
        return;
    }
    if (t.type->kind == TYPE_VOID) {
        // `new(void*)` is one pointer slot and is legal
        check_error(ck, n->loc, "'new' needs a sized type, not void");
        return;
    }
    if (!check_layout(ck, t.type) || !check_size_fits(ck, n->loc, t.type)) {
        return;
    }
    out->type = counted ? type_span(&ck->types, t.type, true, t.mut0)
                        : type_ptr(&ck->types, t.type, true, t.mut0);
}

// ---- calls -------------------------------------------------------------------------

static bool is_builtin(const sym_t* s, const char* name) {
    return s != NULL && s->kind == SYM_BUILTIN && str_eq(s->name, str_from_cstr(name));
}

// Printable types exclude structs, arrays, and spans.
static bool type_is_printable(const type_t* t) {
    switch (t->kind) {
    case TYPE_PRIM:
    case TYPE_ENUM:
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_FN:
    case TYPE_STRING:
        return true;
    default:
        return false;
    }
}

// "'assert' takes 1 argument, 2 given" and "'add' takes 2 arguments, 1 given".
static bool check_arity(check_t* ck, ast_node_t* n, str_t name, uint64_t want) {
    const uint64_t got = ast_len(n);
    if (got == want) {
        return true;
    }
    check_msg_begin(ck);
    msg_quote(&ck->msg, name);
    msg_str(&ck->msg, " takes ");
    msg_uint(&ck->msg, want);
    msg_str(&ck->msg, want == 1 ? " argument, " : " arguments, ");
    msg_uint(&ck->msg, got);
    msg_str(&ck->msg, " given");
    check_msg_end(ck, n->loc);
    return false;
}

// A C variable tail accepts zero or more arguments after its fixed prefix.
static bool check_arity_at_least(check_t* ck, ast_node_t* n, str_t name, uint64_t want) {
    const uint64_t got = ast_len(n);
    if (got >= want) {
        return true;
    }
    check_msg_begin(ck);
    msg_quote(&ck->msg, name);
    msg_str(&ck->msg, " takes at least ");
    msg_uint(&ck->msg, want);
    msg_str(&ck->msg, want == 1 ? " argument, " : " arguments, ");
    msg_uint(&ck->msg, got);
    msg_str(&ck->msg, " given");
    check_msg_end(ck, n->loc);
    return false;
}

static bool extern_legal(const type_t* t);

// C default promotions require explicit casts before a variable tail.
static bool c_tail_legal(const type_t* t) {
    switch (t->kind) {
    case TYPE_PTR:
    case TYPE_VOIDPTR:
        return true;
    case TYPE_FN:
        return extern_legal(t);
    case TYPE_PRIM:
        switch (t->prim) {
        case PRIM_I32:
        case PRIM_U32:
        case PRIM_I64:
        case PRIM_U64:
        case PRIM_F64:
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

// The print family (8.3): zero or more printable arguments, each taking its
// default type, after the descriptor of the `fprint` forms.
static void check_print(check_t* ck, ast_node_t* n, uint64_t first) {
    for (uint64_t i = first; i < ast_len(n); i++) {
        ast_node_t* arg = ast_child(n, i);
        expr_t e;
        check_expr_default(ck, arg, &e);
        if (check_poisoned(e.type)) {
            continue;
        }
        if (!type_is_printable(e.type)) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "cannot print a value of type ");
            check_msg_type(ck, e.type);
            check_msg_end(ck, arg->loc);
        }
    }
}

// Whether the lvalue `e` may be emptied by `move` or `del`. An operand reached
// through an indirection needs that level mutable, because the store is visible
// to everyone else who holds the pointer or span. `verb` is the builtin's name,
// which the diagnostic quotes.
static bool check_emptiable(check_t* ck, ast_node_t* arg, const expr_t* e, str_t verb) {
    if (e->empty == EMPTY_OK) {
        return true;
    }
    check_msg_begin(ck);
    msg_quote(&ck->msg, verb);
    // The reason is the lvalue's own, not its outermost symbol's: `C.data` of
    // a module-level constant is read-only memory although `data` is a field.
    msg_str(&ck->msg,
            e->empty == EMPTY_READONLY
                ? " cannot empty a module-level constant: it lives in read-only memory"
                : " cannot empty an immutable indirection: nothing may be taken out of what was "
                  "only lent");
    check_msg_end(ck, arg->loc);
    return false;
}

static void check_builtin_call(check_t* ck, ast_node_t* n, const sym_t* s, expr_t* out) {
    out->sym = s;
    out->type = type_void(&ck->types);
    if (is_builtin(s, "print") || is_builtin(s, "println") || is_builtin(s, "eprint") ||
        is_builtin(s, "eprintln")) {
        check_print(ck, n, 0);
        return;
    }
    if (is_builtin(s, "fprint") || is_builtin(s, "fprintln")) {
        if (ast_len(n) == 0) {
            check_msg_begin(ck);
            msg_quote(&ck->msg, s->name);
            msg_str(&ck->msg, " takes a descriptor and then its arguments");
            check_msg_end(ck, n->loc);
            return;
        }
        expr_t fd;
        check_expr_as(ck, ast_child(n, 0), type_prim(&ck->types, PRIM_I32), "a descriptor", &fd);
        check_print(ck, n, 1);
        return;
    }
    if (is_builtin(s, "assert")) {
        if (check_arity(ck, n, s->name, 1)) {
            // `assert` is active in both build modes
            check_condition(ck, ast_child(n, 0), "'assert'");
        }
        return;
    }
    if (is_builtin(s, "panic")) {
        if (check_arity(ck, n, s->name, 1)) {
            expr_t m;
            check_expr_as(ck, ast_child(n, 0), type_string(&ck->types, false), "'panic'", &m);
        }
        n->ann |= CHECK_ANN_NORETURN;
        return;
    }
    if (is_builtin(s, "del")) {
        if (!check_arity(ck, n, s->name, 1)) {
            return;
        }
        ast_node_t* arg = ast_child(n, 0);
        expr_t e;
        check_operand(ck, arg, &e);
        if (e.untyped && e.value.kind == CV_NULL) {
            // `del(null)` is a no-op
            convert(ck, arg, &e, type_voidptr(&ck->types, true, false), "'del'");
            return;
        }
        if (check_poisoned(e.type)) {
            return;
        }
        if (!type_is_reference(e.type) || !e.type->own) {
            check_msg_begin(ck);
            msg_quote(&ck->msg, s->name);
            msg_str(&ck->msg,
                    type_is_reference(e.type) ? " needs an owning operand, not "
                                              : " takes a reference, not ");
            check_msg_type(ck, e.type);
            check_msg_end(ck, arg->loc);
            return;
        }
        if (e.lvalue) {
            // on an rvalue `del` only frees
            (void)check_emptiable(ck, arg, &e, s->name);
        }
        return;
    }
    if (is_builtin(s, "move")) {
        if (!check_arity(ck, n, s->name, 1)) {
            return;
        }
        ast_node_t* arg = ast_child(n, 0);
        expr_t e;
        check_operand(ck, arg, &e);
        if (check_poisoned(e.type)) {
            return;
        }
        // a `void` result would add "'move' has no value"
        out->type = type_error(&ck->types);
        if (!e.lvalue) {
            check_error(ck, arg->loc, "'move' takes an lvalue");
            return;
        }
        if (!check_owning(e.type)) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "'move' needs an owning operand, not ");
            check_msg_type(ck, e.type);
            check_msg_end(ck, arg->loc);
            return;
        }
        if (!check_emptiable(ck, arg, &e, s->name)) {
            return;
        }
        // the one universe function with a value
        out->type = e.type;
        return;
    }
    fatal_internal("check: unknown builtin");
}

static void check_call(check_t* ck, ast_node_t* n, expr_t* out) {
    ast_node_t* callee = n->a;
    if (callee->kind == AST_IDENT && lookup(ck, callee->name) == NULL) {
        const sym_t* b = check_builtin(ck, callee->name);
        if (b != NULL) {
            callee->sym = b;
            callee->type = type_void(&ck->types);
            check_builtin_call(ck, n, b, out);
            return;
        }
    }
    expr_t f;
    // the callee is the one position an `extern fn` may stand in
    const ast_node_t* outer_callee = ck->callee;
    ck->callee = callee;
    check_operand(ck, callee, &f);
    ck->callee = outer_callee;
    out->sym = f.sym;
    if (!check_poisoned(f.type) && f.type->kind != TYPE_FN) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "cannot call a value of type ");
        check_msg_type(ck, f.type);
        check_msg_end(ck, n->loc);
        f.type = type_error(&ck->types);
    }
    const bool known = !check_poisoned(f.type);
    if (known) {
        const str_t name = f.sym != NULL ? f.sym->name : str_from_cstr("the callee");
        const bool variadic = f.sym != NULL && f.sym->kind == SYM_EXTERN_FN &&
                              (f.sym->node->flags & AST_FLAG_VARIADIC) != 0;
        const bool arity_ok = variadic ? check_arity_at_least(ck, n, name, f.type->nparams)
                                       : check_arity(ck, n, name, f.type->nparams);
        if (!arity_ok) {
            return;
        }
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        expr_t arg;
        if (known && i < f.type->nparams) {
            check_expr_as(ck, ast_child(n, i), f.type->params[i], "the argument", &arg);
        } else {
            check_expr_default(ck, ast_child(n, i), &arg);
            if (known && !check_poisoned(arg.type) && !c_tail_legal(arg.type)) {
                check_msg_begin(ck);
                msg_str(&ck->msg, "C variable tail cannot use type '");
                check_msg_type(ck, arg.type);
                msg_str(&ck->msg, "'");
                check_msg_end(ck, ast_child(n, i)->loc);
            }
        }
    }
    if (!known) {
        return;
    }
    out->type = f.type->elem;
    if (f.type->noreturn) {
        n->ann |= CHECK_ANN_NORETURN;
    }
}

// ---- struct and array literals -----------------------------------------------------

// The designated form: any order, omitted fields zeroed, no duplicates, and
// designators only on structs.
static void check_designated(check_t* ck, ast_node_t* n, const type_t* t, expr_t* out) {
    const sym_t* s = nominal_sym(t);
    for (uint64_t i = 0; i < ast_len(n); i++) {
        ast_node_t* d = ast_child(n, i);
        if (d->kind != AST_DESIGNATOR) {
            continue;
        }
        const ast_node_t* f = s != NULL ? struct_field(s, d->name) : NULL;
        if (f == NULL) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "struct ");
            msg_view(&ck->msg, t->name);
            msg_str(&ck->msg, " has no field ");
            msg_quote(&ck->msg, d->name);
            check_msg_end(ck, d->name_loc);
            out->type = type_error(&ck->types);
            continue;
        }
        for (uint64_t k = 0; k < i; k++) {
            if (str_eq(ast_child(n, k)->name, d->name)) {
                check_msg_begin(ck);
                msg_str(&ck->msg, "duplicate field ");
                msg_quote(&ck->msg, d->name);
                check_msg_end(ck, d->name_loc);
                out->type = type_error(&ck->types);
            }
        }
        d->sym = f->sym;
        d->type = f->sym != NULL ? f->sym->type : type_error(&ck->types);
        expr_t v;
        check_initializer(ck, d->a, d->type, "the field", &v);
        out->init_const = out->init_const && v.init_const;
    }
}

// A bare `{... }` against the type it initializes.
static void check_brace(check_t* ck, ast_node_t* n, const type_t* t, expr_t* out) {
    n->type = t;
    out->type = t;
    out->init_const = true;
    if (check_poisoned(t)) {
        out->type = type_error(&ck->types);
        return;
    }
    const uint64_t count = ast_len(n);
    if ((n->flags & AST_FLAG_DESIGNATED) != 0) {
        if (t->kind != TYPE_STRUCT) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "designated initializers need a struct, not ");
            check_msg_type(ck, t);
            check_msg_end(ck, n->loc);
            out->type = type_error(&ck->types);
            return;
        }
        check_designated(ck, n, t, out);
        return;
    }
    if (count == 0) {
        if (t->kind == TYPE_STRUCT || t->kind == TYPE_ARRAY || t->kind == TYPE_SPAN ||
            t->kind == TYPE_STRING || t->kind == TYPE_ENUM) {
            return;
        }
        check_msg_begin(ck);
        msg_str(&ck->msg, "'{}' initializes an aggregate, span, string or enum, not ");
        check_msg_type(ck, t);
        check_msg_end(ck, n->loc);
        out->type = type_error(&ck->types);
        return;
    }
    if (t->kind == TYPE_STRUCT) {
        const sym_t* s = nominal_sym(t);
        uint64_t fields = 0;
        for (uint64_t i = 0; s != NULL && s->node != NULL && i < ast_len(s->node); i++) {
            if (ast_child(s->node, i)->kind == AST_FIELD_DECL) {
                fields++;
            }
        }
        if (count != fields) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "a positional literal of struct ");
            msg_view(&ck->msg, t->name);
            msg_str(&ck->msg, " needs its ");
            msg_uint(&ck->msg, fields);
            msg_str(&ck->msg, fields == 1 ? " field, " : " fields, ");
            msg_uint(&ck->msg, count);
            msg_str(&ck->msg, " given");
            check_msg_end(ck, n->loc);
            out->type = type_error(&ck->types);
            return;
        }
        uint64_t at = 0;
        for (uint64_t i = 0; i < ast_len(s->node); i++) {
            const ast_node_t* f = ast_child(s->node, i);
            if (f->kind != AST_FIELD_DECL) {
                continue;
            }
            expr_t v;
            const type_t* ft = f->sym != NULL ? f->sym->type : type_error(&ck->types);
            check_initializer(ck, ast_child(n, at), ft, "the field", &v);
            out->init_const = out->init_const && v.init_const;
            at++;
        }
        return;
    }
    if (t->kind == TYPE_ARRAY) {
        if (count != t->len) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "an array literal for ");
            check_msg_type(ck, t);
            msg_str(&ck->msg, " needs ");
            msg_uint(&ck->msg, t->len);
            msg_str(&ck->msg, t->len == 1 ? " element, " : " elements, ");
            msg_uint(&ck->msg, count);
            msg_str(&ck->msg, " given");
            check_msg_end(ck, n->loc);
            out->type = type_error(&ck->types);
            return;
        }
        for (uint64_t i = 0; i < count; i++) {
            expr_t v;
            check_initializer(ck, ast_child(n, i), t->elem, "the element", &v);
            out->init_const = out->init_const && v.init_const;
        }
        return;
    }
    check_msg_begin(ck);
    if (t->kind == TYPE_SPAN) {
        msg_str(&ck->msg, "a span literal does not exist: write '{}' for the zero span");
    } else {
        msg_str(&ck->msg, "a brace initializer needs a struct or array type, not ");
        check_msg_type(ck, t);
    }
    check_msg_end(ck, n->loc);
    out->type = type_error(&ck->types);
}

void check_initializer(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out) {
    expr_clear(ck, out);
    if (node->kind == AST_BRACE_INIT) {
        check_brace(ck, node, target, out);
        return;
    }
    check_expr_as(ck, node, target, what, out);
}

// `point{1, 2}` and `i32[3]{1, 2, 3}`: a literal is an rvalue whose type its
// own name gives.
static void check_literal(check_t* ck, ast_node_t* n, expr_t* out, bool array) {
    const check_type_t t = array ? check_type(ck, n->a, TYPE_POS_BINDING)
                                 : type_result(base_type(ck, n->a, false, true), false);
    if (check_poisoned(t.type)) {
        expr_t ignored;
        check_brace(ck, n->b, t.type, &ignored);
        return;
    }
    if (array ? t.type->kind != TYPE_ARRAY : t.type->kind != TYPE_STRUCT) {
        check_msg_begin(ck);
        check_msg_type(ck, t.type);
        msg_str(&ck->msg, array ? " is not an array type" : " is not a struct type");
        check_msg_end(ck, n->a->loc);
        return;
    }
    if (!check_layout(ck, t.type) || !check_size_fits(ck, n->loc, t.type)) {
        return;
    }
    check_brace(ck, n->b, t.type, out);
}

// ---- the dispatch ------------------------------------------------------------------

void check_expr(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_clear(ck, out);
    switch (n->kind) {
    case AST_INT:
        untyped(ck, n, out, cv_from_u64(n->ival));
        break;
    case AST_CHAR:
        untyped(ck, n, out, cv_from_char(n->ival));
        break;
    case AST_NULL:
        untyped(ck, n, out, cv_null());
        break;
    case AST_BOOL:
        out->type = type_prim(&ck->types, PRIM_BOOL);
        out->value = cv_from_bool(n->ival != 0);
        out->init_const = true;
        break;
    case AST_STRING:
        out->type = type_string(&ck->types, false);
        out->value = cv_from_str(n->name);
        out->init_const = true;
        break;
    case AST_IDENT:
        check_ident(ck, n, out);
        break;
    case AST_UNARY:
        check_unary(ck, n, out);
        break;
    case AST_BINARY: {
        expr_t a;
        expr_t b;
        check_expr(ck, n->a, &a);
        check_expr(ck, n->b, &b);
        check_operands(ck, n->loc, n->op, n->a, &a, n->b, &b, out);
        out->init_const = out->value.kind != CV_NONE;
        break;
    }
    case AST_CALL:
        check_call(ck, n, out);
        break;
    case AST_INDEX:
        check_index(ck, n, out);
        break;
    case AST_SPAN:
        check_span_expr(ck, n, out);
        break;
    case AST_FIELD:
        check_field(ck, n, out, false);
        break;
    case AST_ARROW:
        check_field(ck, n, out, true);
        break;
    case AST_CAST:
        check_cast(ck, n, out);
        break;
    case AST_SIZEOF:
        check_sizeof(ck, n, out);
        break;
    case AST_NEW:
        check_new(ck, n, out);
        break;
    case AST_STRUCT_LIT:
        check_literal(ck, n, out, false);
        break;
    case AST_ARRAY_LIT:
        check_literal(ck, n, out, true);
        break;
    case AST_BRACE_INIT:
        check_error(ck, n->loc, "a brace initializer is not an expression");
        break;
    case AST_TERNARY:
        check_error(ck, n->loc, "not supported by the bootstrap compiler: ?:");
        break;
    case AST_ERROR:
        // already reported
        break;
    default:
        fatal_internal("check: not an expression");
    }
    n->type = out->type;
    if (out->untyped) {
        n->ann |= CHECK_ANN_UNTYPED;
    }
    if (out->value.kind != CV_NONE && (n->ann & CHECK_ANN_CONST) == 0) {
        set_value(ck, n, out->value);
    }
}

// ---- declarations ------------------------------------------------------------------

// The two states of the lazy resolution, kept on the declaring node.
static bool resolving(const ast_node_t* n) {
    return n != NULL && (n->ann & CHECK_ANN_RESOLVING) != 0;
}

static bool resolved(const ast_node_t* n) {
    return n != NULL && (n->ann & CHECK_ANN_RESOLVED) != 0;
}

static void resolve_struct(check_t* ck, sym_t* s);
static void resolve_enum(check_t* ck, sym_t* s);
static void resolve_fn(check_t* ck, sym_t* s);
static void resolve_var(check_t* ck, sym_t* s);

static void resolve_sym(check_t* ck, sym_t* s) {
    if (s == NULL || s->node == NULL || resolved(s->node)) {
        return;
    }
    if (resolving(s->node)) {
        // only value containment is a cycle
        if (s->kind == SYM_STRUCT) {
            return;
        }
        if (ck->addr_only && (s->kind == SYM_CONST || s->kind == SYM_GLOBAL) && s->type != NULL) {
            // `&N` inside N's initializer does not close a cycle. It asks only
            // for an address. Any module, including this one, can take the
            // address of a module declaration. resolve_var writes the type before
            // checking the initializer, so all required data is available. The
            // value stays CV_NONE because an address is not a constant value.
            return;
        }
        // An enum member value cannot depend on itself either.
        check_msg_begin(ck);
        msg_quote(&ck->msg, s->name);
        msg_str(&ck->msg, " is defined in terms of itself");
        check_msg_end(ck, s->decl);
        ((ast_node_t*)s->node)->ann |= CHECK_ANN_RESOLVED;
        sym_fail(ck, s);
        return;
    }
    ast_node_t* node = (ast_node_t*)s->node;
    node->ann |= CHECK_ANN_RESOLVING;
    ck->resolve_depth++;
    switch (s->kind) {
    case SYM_STRUCT:
        resolve_struct(ck, s);
        break;
    case SYM_ENUM:
        resolve_enum(ck, s);
        break;
    case SYM_FN:
    case SYM_EXTERN_FN:
        resolve_fn(ck, s);
        break;
    case SYM_CONST:
    case SYM_GLOBAL:
        resolve_var(ck, s);
        break;
    default:
        break;
    }
    ck->resolve_depth--;
    node->ann &= ~(uint32_t)CHECK_ANN_RESOLVING;
    node->ann |= CHECK_ANN_RESOLVED;
    node->type = s->type;
    node->sym = s;
    if (ck->resolve_depth == 0) {
        sized_drain(ck);
    }
}

static void resolve_struct(check_t* ck, sym_t* s) {
    ast_node_t* decl = (ast_node_t*)s->node;
    // a field that fails poisons the type; still close the layout
    const type_t* record = s->type;
    if (!type_layout_begin(record)) {
        fatal_internal("check: a struct laid out twice");
    }
    const type_t* fields[CHECK_MAX_MEMBERS];
    uint64_t offsets[CHECK_MAX_MEMBERS];
    uint64_t n = 0;
    bool ok = true;
    for (uint64_t i = 0; i < ast_len(decl); i++) {
        ast_node_t* f = ast_child(decl, i);
        if (f->kind != AST_FIELD_DECL) {
            continue;
        }
        if (n >= CHECK_MAX_MEMBERS) {
            check_error(ck, f->loc, "too many fields");
            ok = false;
            break;
        }
        const check_type_t ft = check_type(ck, f->a, TYPE_POS_FIELD);
        sym_t* fs = check_sym_new(ck, SYM_FIELD, f->name, f, s);
        fs->type = ft.type;
        f->sym = fs;
        f->type = ft.type;
        if (check_poisoned(ft.type) || !check_layout(ck, ft.type) ||
            !check_size_fits(ck, f->loc, ft.type)) {
            sym_fail(ck, fs);
            ok = false;
            continue;
        }
        fields[n] = ft.type;
        n++;
    }
    if (!ok || n == 0) {
        // the parser reports a body with no field, so these failed
        if (type_layout_state(record) != LAYOUT_ERROR) {
            type_layout_fail(record);
        }
        sym_fail(ck, s);
        return;
    }
    if (!type_layout_struct(record, fields, (uint32_t)n, offsets)) {
        // Report the error at the `struct` keyword.
        check_msg_begin(ck);
        msg_str(&ck->msg, "type is too large: struct ");
        msg_view(&ck->msg, s->name);
        check_msg_end(ck, keyword_range(decl->loc, STRUCT_KEYWORD_LEN));
        sym_fail(ck, s);
        return;
    }
    uint64_t at = 0;
    for (uint64_t i = 0; i < ast_len(decl); i++) {
        ast_node_t* f = ast_child(decl, i);
        if (f->kind == AST_FIELD_DECL) {
            // Records the field byte offset on its declaration. The emitter names
            // fields by index and lets LLVM use the same layout. Only check_test.c
            // and check_conv_test.c read these offsets. They record the compiler
            // answer for a complete declaration. The loop visits exactly the
            // fields in the IR struct type. Each failed field already left `ok`
            // false.
            f->aux = offsets[at];
            at++;
        }
    }
}

static void resolve_enum(check_t* ck, sym_t* s) {
    ast_node_t* decl = (ast_node_t*)s->node;
    const prim_kind_t underlying = (prim_kind_t)ENUM_UNDERLYING;
    cval_t next = cv_from_i64(0);
    bool ok = true;
    for (uint64_t i = 0; i < ast_len(decl); i++) {
        ast_node_t* m = ast_child(decl, i);
        if (m->kind != AST_ENUM_MEMBER) {
            continue;
        }
        sym_t* ms = check_sym_new(ck, SYM_ENUM_MEMBER, m->name, m, s);
        ms->type = s->type;
        m->sym = ms;
        m->type = s->type;
        cval_t v = next;
        if (m->a != NULL) {
            // a value may not refer to the enum itself
            expr_t e;
            check_expr_default(ck, m->a, &e);
            if (!cv_is_int(e.value)) {
                if (!check_poisoned(e.type)) {
                    check_error(ck, m->a->loc, "an enum value must be a constant expression");
                }
                sym_fail(ck, ms);
                ok = false;
                continue;
            }
            v = e.value;
        }
        if (!cv_fits(v, underlying)) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "enum value ");
            cv_to_str(v, &ck->msg);
            msg_str(&ck->msg, " does not fit i32");
            check_msg_end(ck, m->loc);
            sym_fail(ck, ms);
            ok = false;
            continue;
        }
        for (uint64_t k = 0; k < i; k++) {
            const ast_node_t* other = ast_child(decl, k);
            if (other->kind == AST_ENUM_MEMBER && cv_eq(check_node_value(ck, other), v)) {
                check_msg_begin(ck);
                msg_str(&ck->msg, "duplicate enum value ");
                cv_to_str(v, &ck->msg);
                msg_str(&ck->msg, " for ");
                msg_quote(&ck->msg, m->name);
                check_msg_end(ck, m->name_loc);
                ok = false;
                break;
            }
        }
        set_value(ck, m, v);
        if (!cv_add(v, cv_from_i64(1), &next)) {
            next = cv_from_i64(0);
        }
    }
    if (!ok) {
        sym_fail(ck, s);
    }
}

// The types an extern signature may use: no spans, strings, structs or arrays.
// A function-pointer parameter is legal when its full signature is extern-legal.
// This includes its result because C calls it with the same convention.
// Recursion terminates because a function type uses earlier written types and
// cannot reach itself.
static bool extern_legal(const type_t* t) {
    switch (t->kind) {
    case TYPE_PRIM:
    case TYPE_ENUM:
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_VOID:
    case TYPE_ERROR:
        return true;
    case TYPE_FN:
        if (!extern_legal(t->elem)) {
            return false;
        }
        for (uint32_t i = 0; i < t->nparams; i++) {
            if (!extern_legal(t->params[i])) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

// The `i`-th parameter declaration of a function, for the position a
// signature difference is reported at.
static const ast_node_t* param_at(const ast_node_t* decl, uint64_t k) {
    uint64_t n = 0;
    for (uint64_t i = 0; i < ast_len(decl); i++) {
        const ast_node_t* p = ast_child(decl, i);
        if (p->kind != AST_PARAM) {
            continue;
        }
        if (n == k) {
            return p;
        }
        n++;
    }
    return decl;
}

// ---- two extern declarations of one C symbol -------------------------------------------

// Whether a primitive is the C `unsigned char` type. fort `char` uses that type
// at the boundary. Thus, `char` and `u8` name one C type. `char*` can also be
// written `u8*`.
static bool is_c_byte(prim_kind_t k) {
    return k == PRIM_U8 || k == PRIM_CHAR;
}

// Whether two extern signature types name one C type.
// The types must match, except that `char` and `u8` name the same C byte type.
// Type-level marks must match. Binding-level `mut` does not enter a function type.
static bool extern_type_agrees(const type_t* a, const type_t* b) {
    if (a == NULL || b == NULL) {
        return a == b;
    }
    if (a == b) {
        return true;
    }
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
    case TYPE_VOID:
        return true;
    case TYPE_PRIM:
        return a->prim == b->prim || (is_c_byte(a->prim) && is_c_byte(b->prim));
    case TYPE_PTR:
        return a->mut == b->mut && a->own == b->own && extern_type_agrees(a->elem, b->elem);
    case TYPE_VOIDPTR:
        // `void mut*` and `void*` are two signatures, as `node mut*` and
        // `node*` are.
        return a->own == b->own && a->mut == b->mut;
    case TYPE_FN:
        if (a->noreturn != b->noreturn || a->nparams != b->nparams ||
            !extern_type_agrees(a->elem, b->elem)) {
            return false;
        }
        for (uint32_t i = 0; i < a->nparams; i++) {
            if (!extern_type_agrees(a->params[i], b->params[i])) {
                return false;
            }
        }
        return true;
    default:
        // This includes a struct or enum reached through a pointer. It also
        // includes each kind that extern signatures cannot use. Interning did
        // not equate the nodes, so they are different types.
        return false;
    }
}

// Whether a struct or an enum, whose identity is the declaration it comes from
// and not its spelling.
static bool is_nominal(const type_t* t) {
    return t->kind == TYPE_STRUCT || t->kind == TYPE_ENUM;
}

// Whether either side of the first difference between two disagreeing extern
// types names a struct or enum. Its identity is its declaration, not its
// spelling. Equal words in both modules cannot make two declarations agree.
// The note must therefore offer an imported type or a wrapper. Either side can
// name the nominal type. One module can use `i32` while another uses an enum.
// The enum remains importable. Two spellings of one type never reach here, since `color` and
// `shade.color` denote one declaration and the comparison above already agreed.
//
// It runs only after extern_type_agrees returns false. Thus, it walks a pair
// already known to differ and does not decide agreement. It stops at `void*`
// because that type erases its pointee. It also accepts unequal function arity
// and walks the shorter list. It identifies the difference kind, not whether a
// difference exists.
static bool differs_by_nominal(const type_t* a, const type_t* b) {
    if (a == NULL || b == NULL || a == b) {
        return false;
    }
    if (is_nominal(a) || is_nominal(b)) {
        return true;
    }
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == TYPE_PTR) {
        return differs_by_nominal(a->elem, b->elem);
    }
    if (a->kind == TYPE_FN) {
        if (differs_by_nominal(a->elem, b->elem)) {
            return true;
        }
        for (uint32_t i = 0; i < a->nparams && i < b->nparams; i++) {
            if (differs_by_nominal(a->params[i], b->params[i])) {
                return true;
            }
        }
    }
    return false;
}

// Reports `conflicting declarations of extern 'write': parameter 1 differs`.
// The error points to the differing part of the later declaration. A note
// points to the earlier declaration. `param` is the 1-based parameter, or 0
// when the difference names no parameter. `wrapper` requests guidance when no
// shared spelling exists.
//
// The later declaration must carry the error. `at` is in the module being
// checked. `first` is in an earlier module that the reader might not own. A
// client can drop diagnostics for files that it cannot open. A nested note
// stays with its error. Reversing them could hide all diagnostics from a reader
// who redeclares a libc symbol.
static void error_extern_conflict(check_t* ck,
                                  loc_t at,
                                  loc_t first,
                                  str_t name,
                                  const char* what,
                                  uint64_t param,
                                  bool wrapper) {
    check_msg_begin(ck);
    msg_str(&ck->msg, "conflicting declarations of extern ");
    msg_quote(&ck->msg, name);
    msg_str(&ck->msg, what);
    if (param > 0) {
        msg_uint(&ck->msg, param);
        msg_str(&ck->msg, " differs");
    }
    check_msg_end(ck, at);
    // the notes follow the error rather than standing alone
    if (ck->mute) {
        return;
    }
    msg_begin(&ck->msg);
    msg_str(&ck->msg, "previous declaration of ");
    msg_quote(&ck->msg, name);
    msg_str(&ck->msg, " here");
    diag_note(first, msg_end(&ck->msg));
    if (!wrapper) {
        return;
    }
    msg_begin(&ck->msg);
    // Rewording cannot make a nominal type agree with a different type. The
    // module must import the shared type. Alternatively, one module declares
    // the symbol and exports a fort wrapper that other modules call.
    msg_str(&ck->msg,
            "a struct or an enum stands here, and its identity is its declaration and not its "
            "spelling: give both declarations that one type, importing it where it is missing, or "
            "declare the symbol in one module and call it through a fort function the others "
            "import");
    diag_note(at, msg_end(&ck->msg));
}

// Several modules can declare one C symbol when their signatures match,
// including `own`. The checker uses loader dependency order. It compares each
// later declaration with the first and reports differences at the later one.
// It compares types, not written text. Thus, two spellings of one imported type
// agree. Two local types with one spelling do not agree.
static bool check_extern_agreement(check_t* ck, const ast_node_t* decl, const sym_t* s) {
    int64_t at = 0;
    if (!strmap_get(&ck->extern_first, s->name, &at)) {
        (void)strmap_put(&ck->extern_first, s->name, (int64_t)ck->externs.len);
        ptrvec_push(&ck->externs, (void*)s);
        return true;
    }
    const sym_t* first = (const sym_t*)ck->externs.items[at];
    if (first->node == decl) {
        // The same declaration can pass twice through one checker. Its symbol
        // and nominal types are new records for the input tree. Replace the entry
        // instead of comparing it with itself. This path requires the second
        // pass to check successfully. A failed pass leaves the first symbol in
        // the map. Its nominal types belong to a replaced resolution. A later
        // module could compare against types that no module can name. Each prior
        // failure path reports first, so that run already has a diagnostic. The
        // stale comparison could only add noise to a failing file.
        ck->externs.items[at] = (void*)s;
        return true;
    }
    const type_t* a = first->type;
    const type_t* b = s->type;
    if (a == NULL || b == NULL || a->kind != TYPE_FN || b->kind != TYPE_FN) {
        return true;
    }
    const loc_t note = first->node->name_loc;
    if (a->noreturn != b->noreturn || !extern_type_agrees(a->elem, b->elem)) {
        error_extern_conflict(ck,
                              decl->a->loc,
                              note,
                              s->name,
                              ": the result type differs",
                              0,
                              differs_by_nominal(a->elem, b->elem));
        return false;
    }
    if (a->nparams != b->nparams) {
        error_extern_conflict(
            ck, decl->name_loc, note, s->name, ": the number of parameters differs", 0, false);
        return false;
    }
    if ((first->node->flags & AST_FLAG_VARIADIC) != (decl->flags & AST_FLAG_VARIADIC)) {
        loc_t diff_loc = decl->name_loc;
        if ((decl->flags & AST_FLAG_VARIADIC) != 0) {
            diff_loc = decl->tail_loc;
        }
        error_extern_conflict(
            ck, diff_loc, note, s->name, ": the variable-tail mark differs", 0, false);
        return false;
    }
    for (uint32_t i = 0; i < a->nparams; i++) {
        if (extern_type_agrees(a->params[i], b->params[i])) {
            continue;
        }
        error_extern_conflict(ck,
                              param_at(decl, i)->loc,
                              note,
                              s->name,
                              ": parameter ",
                              i + 1,
                              differs_by_nominal(a->params[i], b->params[i]));
        return false;
    }
    return true;
}

// The two unmangled definitions the compiler emits in the entry module, whose
// names an `extern` may not declare.
static const char ENTRY_SYMBOL[] = "fort_entry";
static const char MAIN_SYMBOL[] = "main";

static bool is_reserved_c_name(str_t name) {
    return str_eq(name, str_from_cstr(ENTRY_SYMBOL)) || str_eq(name, str_from_cstr(MAIN_SYMBOL));
}

static void resolve_fn(check_t* ck, sym_t* s) {
    ast_node_t* decl = (ast_node_t*)s->node;
    const bool is_extern = s->kind == SYM_EXTERN_FN;
    if (is_extern && is_reserved_c_name(s->name)) {
        // the compiler emits these two definitions
        check_msg_begin(ck);
        msg_quote(&ck->msg, s->name);
        msg_str(&ck->msg, " is reserved: the compiler emits it");
        check_msg_end(ck, decl->name_loc);
        sym_fail(ck, s);
        return;
    }
    const check_type_t ret = check_type(ck, decl->a, TYPE_POS_RETURN);
    bool ok = !check_poisoned(ret.type) && check_layout(ck, ret.type) &&
              check_size_fits(ck, decl->a->loc, ret.type);
    const type_t* params[CHECK_MAX_MEMBERS];
    uint64_t n = 0;
    for (uint64_t i = 0; i < ast_len(decl); i++) {
        ast_node_t* p = ast_child(decl, i);
        if (p->kind != AST_PARAM) {
            continue;
        }
        if (n >= CHECK_MAX_MEMBERS) {
            check_error(ck, p->loc, "too many parameters");
            ok = false;
            break;
        }
        const check_type_t pt = check_type(ck, p->a, TYPE_POS_BINDING);
        sym_t* ps = check_sym_new(ck, SYM_PARAM, p->name, p, s);
        ps->type = pt.type;
        ps->mut0 = pt.mut0;
        p->sym = ps;
        p->type = pt.type;
        if (check_poisoned(pt.type) || !check_layout(ck, pt.type) ||
            !check_size_fits(ck, p->loc, pt.type)) {
            sym_fail(ck, ps);
            ok = false;
            continue;
        }
        if (void_parameter_refused(ck, p->loc, pt.type)) {
            sym_fail(ck, ps);
            ok = false;
            continue;
        }
        if (is_extern && !extern_legal(pt.type)) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "extern signature cannot use type '");
            check_msg_type(ck, pt.type);
            msg_str(&ck->msg, "'");
            check_msg_end(ck, p->loc);
            sym_fail(ck, ps);
            ok = false;
            continue;
        }
        params[n] = pt.type;
        n++;
    }
    if (ok && is_extern && !extern_legal(ret.type)) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "extern signature cannot use type '");
        check_msg_type(ck, ret.type);
        msg_str(&ck->msg, "'");
        check_msg_end(ck, decl->a->loc);
        ok = false;
    }
    if (!ok) {
        sym_fail(ck, s);
        return;
    }
    s->type = type_fn(&ck->types, ret.type, params, (uint32_t)n, is_noreturn(decl->a));
    if (is_extern && !check_extern_agreement(ck, decl, s)) {
        // no one signature, so the symbol is poisoned
        sym_fail(ck, s);
    }
}

static void resolve_var(check_t* ck, sym_t* s) {
    ast_node_t* decl = (ast_node_t*)s->node;
    const check_type_t t = check_type(ck, decl->a, TYPE_POS_BINDING);
    s->type = t.type;
    s->mut0 = t.mut0;
    s->kind = t.mut0 ? SYM_GLOBAL : SYM_CONST;
    if (check_poisoned(t.type) || !check_layout(ck, t.type) ||
        !check_size_fits(ck, decl->loc, t.type)) {
        sym_fail(ck, s);
        return;
    }
    if (t.type->kind == TYPE_VOID) {
        check_error(ck, decl->loc, "'void' is only a return type or the base of 'void*'");
        sym_fail(ck, s);
        return;
    }
    if (decl->b == NULL) {
        // the parser enforces it
        sym_fail(ck, s);
        return;
    }
    expr_t e;
    check_initializer(ck, decl->b, t.type, "the initializer", &e);
    if (check_poisoned(e.type)) {
        sym_fail(ck, s);
        return;
    }
    if (!e.init_const) {
        // no calls and no reads of `mut` globals
        check_error(ck, decl->b->loc, "a module-level initializer must be a constant expression");
        sym_fail(ck, s);
    }
}

// ---- modules -----------------------------------------------------------------------

// The symbol kind a top-level declaration introduces; a module-level variable
// is a constant or a global once its type is resolved.
static bool decl_sym_kind(const ast_node_t* decl, sym_kind_t* out) {
    switch (decl->kind) {
    case AST_FN_DECL:
        *out = (decl->flags & AST_FLAG_EXTERN) != 0 ? SYM_EXTERN_FN : SYM_FN;
        return true;
    case AST_STRUCT_DECL:
        *out = SYM_STRUCT;
        return true;
    case AST_ENUM_DECL:
        *out = SYM_ENUM;
        return true;
    case AST_VAR_DECL:
        *out = SYM_CONST;
        return true;
    default:
        return false;
    }
}

// Each import node whose name denotes the imported item carries its symbol.
// This includes the item, its `as` alias, and the last path segment. Earlier
// segments name search directories rather than
// modules, so they carry nothing.
static void annotate_path(const ast_node_t* imp, const sym_t* last, const sym_t* module) {
    ast_node_t* path = imp->a;
    if (path == NULL || ast_len(path) == 0) {
        return;
    }
    const uint64_t n = ast_len(path);
    ast_child(path, n - 1)->sym = last;
    if (n >= 2 && module != NULL && module != last) {
        ast_child(path, n - 2)->sym = module;
    }
}

// The module a symbol was declared in, which is its owner (sym.h).
static const sym_t* owning_module(const sym_t* s) {
    return s != NULL && s->owner != NULL && s->owner->kind == SYM_MODULE ? s->owner : NULL;
}

static void annotate_import(const binding_t* b) {
    ast_node_t* n = (ast_node_t*)b->node;
    const sym_t* s = sym_of_binding(b);
    if (n == NULL) {
        return;
    }
    n->sym = s;
    if (n->kind == AST_IMPORT_ITEM) {
        if (n->a != NULL) {
            n->a->sym = s;
        }
        return;
    }
    if (n->b != NULL) {
        n->b->sym = s;
    }
    annotate_path(n, s, b->kind == BIND_SYMBOL ? owning_module(s) : s);
}

// The path of an item list names the module every item comes from, which is
// the owner of the first item that resolved.
static void annotate_item_path(const module_t* m, ast_node_t* imp) {
    for (uint64_t i = 0; i < ast_len(imp); i++) {
        const ast_node_t* item = ast_child(imp, i);
        const sym_t* module = owning_module(item->sym);
        if (module != NULL) {
            annotate_path(imp, module, module);
            return;
        }
    }
    (void)m;
}

// Clears the annotation slots that this pass owns before writing them. Symbols
// from an earlier check can belong to a deleted checker. A module checked twice
// therefore starts from the parser output.
static void clear_annotations(ast_node_t* n) {
    if (n == NULL) {
        return;
    }
    n->type = NULL;
    n->sym = NULL;
    n->aux = 0;
    // Every bit the checker owns, named once (check.h): a bit cleared here by
    // hand would be forgotten the day a new one is added.
    n->ann &= ~(uint32_t)CHECK_ANN_ALL;
    clear_annotations(n->a);
    clear_annotations(n->b);
    clear_annotations(n->c);
    clear_annotations(n->d);
    for (uint64_t i = 0; i < ast_len(n); i++) {
        clear_annotations(ast_child(n, i));
    }
}

// The name an import binds: the `as` alias, the item's own name, or the last
// segment of the path.
static str_t bound_name(const ast_node_t* imp, const ast_node_t* item) {
    if (item != NULL) {
        return item->a != NULL ? item->a->name : item->name;
    }
    if (imp->b != NULL) {
        return imp->b->name;
    }
    const ast_node_t* path = imp->a;
    return path != NULL && ast_len(path) > 0 ? ast_child(path, ast_len(path) - 1)->name
                                             : str_from_range(NULL, 0);
}

// The names the module's imports did not bind, which is what an import the
// loader reported leaves behind: a use of one says nothing further.
static void collect_failed_imports(check_t* ck, const module_t* m) {
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        const ast_node_t* imp = ast_child(m->ast, i);
        if (imp->kind != AST_IMPORT) {
            continue;
        }
        const uint64_t items = ast_len(imp);
        for (uint64_t k = 0; k == 0 || k < items; k++) {
            const str_t name = bound_name(imp, items > 0 ? ast_child(imp, k) : NULL);
            if (name.len > 0 && scope_find(&m->names, name) == NULL) {
                (void)strmap_put(&ck->bad_imports, name, 1);
            }
        }
    }
}

// Phase one: one symbol per top-level declaration, so that the declarations of
// a module are order-independent.
static void collect_module(check_t* ck, const module_t* m) {
    sym_t* ms = check_sym_new(ck, SYM_MODULE, m->path, m->ast, NULL);
    // A module's own record hangs on its AST_MODULE node, which is how a
    // binding reaches it (sym.h).
    m->ast->sym = ms;
    ck->module_sym = ms;
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        ast_node_t* decl = ast_child(m->ast, i);
        sym_kind_t kind = SYM_CONST;
        if (!decl_sym_kind(decl, &kind)) {
            continue;
        }
        sym_t* s = check_sym_new(ck, kind, decl->name, decl, ms);
        decl->sym = s;
        if (kind == SYM_STRUCT) {
            // the type exists before the fields are read
            s->type = type_struct(&ck->types, decl->name, s);
        } else if (kind == SYM_ENUM) {
            s->type = type_enum(&ck->types, decl->name, s);
        }
    }
    for (uint64_t i = 0; i < scope_count(&m->names); i++) {
        const binding_t* b = scope_at(&m->names, i);
        if (b->kind == BIND_MODULE || b->kind == BIND_SYMBOL) {
            annotate_import(b);
        }
    }
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        ast_node_t* imp = ast_child(m->ast, i);
        if (imp->kind == AST_IMPORT && ast_len(imp) > 0) {
            annotate_item_path(m, imp);
        }
    }
}

// The entry module defines `fn main() i32` or `fn main(string@ args) i32`.
static void check_main(check_t* ck, const module_t* m) {
    const binding_t* b = scope_find(&m->names, str_from_cstr("main"));
    const sym_t* s = sym_of_binding(b);
    bool ok = s != NULL && s->kind == SYM_FN && !check_poisoned(s->type) &&
              s->type->kind == TYPE_FN && !s->type->noreturn &&
              type_equal(s->type->elem, type_prim(&ck->types, PRIM_I32));
    if (ok && s->type->nparams > 1) {
        ok = false;
    }
    if (ok && s->type->nparams == 1) {
        const type_t* args = type_span(&ck->types, type_string(&ck->types, false), false, false);
        ok = type_equal(s->type->params[0], args);
    }
    if (ok || (s != NULL && s->error)) {
        return;
    }
    check_msg_begin(ck);
    msg_str(&ck->msg, "entry module ");
    msg_quote(&ck->msg, m->path);
    msg_str(&ck->msg, " must define 'fn main() i32' or 'fn main(string@ args) i32'");
    check_msg_end(ck, s != NULL ? s->decl : loc_make(m->file.ptr, 1, 1));
}

bool check_module(check_t* ck, const module_t* m) {
    if (m->ast == NULL) {
        return true;
    }
    const uint64_t before = ck->errors;
    ck->module = m;
    ck->scope = NULL;
    ck->in_function = false;
    clear_annotations(m->ast);
    strmap_free(&ck->bad_imports);
    strmap_init(&ck->bad_imports);
    collect_failed_imports(ck, m);
    collect_module(ck, m);
    // phase two, every declaration resolved on demand
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        ast_node_t* decl = ast_child(m->ast, i);
        sym_kind_t kind = SYM_CONST;
        if (decl_sym_kind(decl, &kind)) {
            resolve_sym(ck, (sym_t*)decl->sym);
        }
    }
    // The bodies come last, when every declaration of the module has a type.
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        ast_node_t* decl = ast_child(m->ast, i);
        if (decl->kind == AST_FN_DECL && decl->b != NULL && decl->sym != NULL) {
            check_function_body(ck, decl, decl->sym);
        }
    }
    if (m->entry && ck->require_main) {
        // a module checked on its own is not a program
        check_main(ck, m);
    }
    ck->module = NULL;
    ck->module_sym = NULL;
    return ck->errors == before;
}

bool check_program(check_t* ck, const module_set_t* set) {
    bool ok = true;
    // Checks each module in the pass order from modules.h. Dependency-ordered
    // modules come first, so imports are complete before their importers. Other
    // parsed modules follow. This includes modules with failed imports and
    // importers of files that did not parse. They still report their own errors.
    // The index walk uses the same order. Neither consumer builds another order.
    for (uint64_t i = 0; i < module_set_pass_count(set); i++) {
        if (!check_module(ck, module_set_pass_at(set, i))) {
            ok = false;
        }
    }
    return ok;
}
