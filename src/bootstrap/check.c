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
#include "runtime_sig.h"
#include "scope.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// The limits of one declaration: a type with more suffixes, or a function,
// function type or struct with more parameters or fields, is refused rather
// than sized dynamically, as D2.11 caps nesting. No program of this project
// comes near either.
enum { CHECK_MAX_SUFFIXES = 32, CHECK_MAX_MEMBERS = 128 };

// The width of the `struct` keyword, which is where an infinite-size struct
// is reported (D14.2).
enum { STRUCT_KEYWORD_LEN = 6 };

// The underlying type of an enum is i32 (D3.9).
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
    // One symbol per universe name of D12.2, so that every use of a builtin
    // denotes the same record.
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
// says so (D4.6).
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
    // A muted checker annotates the tree without reporting, so that an editor
    // mode can index a file it is not compiling (D20.2).
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
    // The range of a construct ends one past its last byte (D20.4), and its
    // last byte is the `}`, so the brace is the one column before the end.
    if (loc.end_col <= 1) {
        return loc;
    }
    return loc_range(loc.file, loc.end_line, loc.end_col - 1, loc.end_line, loc.end_col);
}

// The range of the keyword a declaration starts at, where an infinite-size
// struct is reported (D14.2).
static loc_t keyword_range(loc_t loc, uint32_t len) {
    return loc_range(loc.file, loc.line, loc.col, loc.line, loc.col + len);
}

// ---- symbols -----------------------------------------------------------------------

sym_t* check_sym_new(
    check_t* ck, sym_kind_t kind, str_t name, const ast_node_t* node, const sym_t* owner) {
    sym_t* s = mem_alloc((uint64_t)sizeof(sym_t));
    s->kind = kind;
    s->name = name;
    // A symbol is found at the name it declares, never at the construct's
    // first token (D20.4).
    s->decl = node != NULL ? node->name_loc : loc_make(NULL, 1, 1);
    s->type = NULL;
    s->mut0 = false;
    s->node = node;
    s->owner = owner;
    s->error = false;
    ptrvec_push(&ck->syms, s);
    // Only the top-level declarations are resolved lazily; a field, an enum
    // member, a local and a parameter are complete when their container makes
    // them (D7.10).
    if (node != NULL &&
        (kind == SYM_FIELD || kind == SYM_ENUM_MEMBER || kind == SYM_LOCAL || kind == SYM_PARAM)) {
        ((ast_node_t*)node)->ann |= CHECK_ANN_RESOLVED;
    }
    return s;
}

// Marks a symbol as failed: the error type silences every later diagnostic
// that involves the declaration (D14.2).
static void sym_fail(check_t* ck, sym_t* s) {
    s->error = true;
    s->type = type_error(&ck->types);
}

// The declaration a binding denotes: an import binding stands for the module
// or for the declaration it names, which is what its own name denotes (D9.3).
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

// ---- names (D7.9) ------------------------------------------------------------------

// Steps 1 and 2 of D7.9: the innermost block outward, then the module
// namespace. The universe of step 3 is a builtin, which check_builtin reads.
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
// so a declaration of the same name shadows it (D7.9, D12.2).
static const sym_t* check_builtin(const check_t* ck, str_t name) {
    for (uint64_t i = 0; i < UNIVERSE_COUNT; i++) {
        if (str_eq(ck->builtins[i]->name, name)) {
            return ck->builtins[i];
        }
    }
    return NULL;
}

// Whether the name is one an import of this module failed to bind: the
// loader reported that import, so nothing more is said about the name
// (D14.2).
static bool from_a_failed_import(const check_t* ck, str_t name) {
    return strmap_has(&ck->bad_imports, name);
}

// "unknown name 'x'", with the hint D3.9 asks for when the name is an enum
// member, which is written `color.red` and lives in no namespace.
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
                // An enum member is scoped to its enum (D3.9).
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

// ---- types (D3, D5.3) --------------------------------------------------------------

// The declaring symbol of a struct or enum type, which the checker stored in
// the node when it made it (types.h keeps it opaque).
static sym_t* nominal_sym(const type_t* t) {
    return (sym_t*)t->decl;
}

static void resolve_sym(check_t* ck, sym_t* s);

bool check_size_fits(check_t* ck, loc_t loc, const type_t* t) {
    // `void` is a return type and has no size to check (D3.1).
    if (check_poisoned(t) || t->kind == TYPE_VOID || type_size_fits(t)) {
        return true;
    }
    // A type whose size would pass 2^63 - 1 bytes is an error at the
    // declaration that introduces it (D3.4).
    check_msg_begin(ck);
    msg_str(&ck->msg, "type is too large: ");
    check_msg_type(ck, t);
    check_msg_end(ck, loc);
    return false;
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
        // A struct is laid out when a declaration or a sizeof first needs its
        // size, which is what makes the sizes of D7.10 lazy.
        resolve_sym(ck, nominal_sym(pending));
        pending = type_layout_pending(t);
    }
    if (pending != NULL) {
        // The struct is still on the resolution path, so it contains itself
        // by value: an infinite size, reported at its `struct` keyword (D3.8,
        // D14.2).
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

// The declaration `name` denotes in the module `m`, or NULL. A module's own
// import bindings are not among its declarations, since there is no
// re-export (D9.3): qualified access and an import both see the declarations
// of `m` and not its imports (module-system.md 3, 4), and every lookup into
// another module's namespace asks this one question.
static const binding_t* module_declaration(const module_t* m, str_t name) {
    const binding_t* b = m != NULL ? scope_find(&m->names, name) : NULL;
    return b != NULL && bind_is_declaration(b) ? b : NULL;
}

// The type a name in type position denotes: a struct or an enum, of this
// module or, qualified, of an imported one (D9.4). `stores_base` says whether
// the written type puts a value of that name in its own storage, which is
// what decides whether the name is a layout dependency.
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
        // A qualified name: the first identifier is the module, the second
        // the declaration in it (D9.4).
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
    // The declaration may still be unresolved: a type name is one of the
    // places the lazy resolution of D7.10 reaches another declaration. A
    // struct the written type does not store by value is not one of them:
    // its size is no part of this type's size, so forcing it would put it on
    // the resolution path of a struct that does not contain it, and
    // check_layout would read that unfinished layout as an infinite size
    // (D3.8). The resolution edges are exactly the value-containment edges,
    // which is what makes the two declaration orders of a pair one program
    // (D7.10). collect_module gave every struct its type before any field
    // was read, so the name already denotes the right type (D3.8).
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

// The constant length of a fixed-array suffix (D3.4).
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
        // D3.4: N is a constant expression greater than 0.
        check_error(ck, e->loc, "an array length must be greater than 0");
        return false;
    }
    return true;
}

// Whether a written return type is `noreturn` (D8.5). Every return type is
// parsed through `type`, so the keyword sits under the AST_TYPE wrapper; a
// base type node is accepted too, since a speculative parse hands one over.
static bool is_noreturn(const ast_node_t* t) {
    if (t == NULL) {
        return false;
    }
    if (t->kind == AST_TYPE) {
        return t->a != NULL && t->a->kind == AST_TYPE_NORETURN;
    }
    return t->kind == AST_TYPE_NORETURN;
}

static check_type_t check_type_at(check_t* ck, ast_node_t* node, type_pos_t pos, bool in_storage);

// The base type of a written type (grammar.md 4): a primitive, `string`,
// `void`, `noreturn`, a qualified name or a function type.
static const type_t* base_type(check_t* ck, ast_node_t* n, bool allow_noreturn, bool stores_base) {
    const type_t* t = type_error(&ck->types);
    switch (n->kind) {
    case AST_TYPE_PRIM: {
        const prim_kind_t k = (prim_kind_t)n->op;
        if (prim_is_float(k)) {
            // Floats are outside the C bootstrap's subset (toolchain.md 7.3).
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
        // `noreturn` is a return type only (D8.5); a function with it returns
        // void and carries the mark on its type.
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
        // A function pointer is one word and its signature is stored
        // nowhere (D3.10), so neither the result nor a parameter is a
        // layout dependency of whatever holds the pointer.
        const check_type_t ret = check_type_at(ck, n->a, TYPE_POS_RETURN, false);
        bool ok = !check_poisoned(ret.type);
        for (uint64_t i = 0; i < count; i++) {
            // A binding-level `mut` on a parameter is not part of the
            // function's type (D3.10), so mut0 is dropped here.
            const check_type_t p = check_type_at(ck, ast_child(n, i), TYPE_POS_BINDING, false);
            params[i] = p.type;
            ok = ok && !check_poisoned(p.type);
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

// Whether the base of a written type ends up in the storage the type
// describes. A reference suffix anywhere breaks the chain: `node*` is one
// word and `node* @` two, whatever a `node` is (D3.11, D3.5, D5.8), while a
// fixed array is N of its element and carries the base through (D3.4). It is
// the written form of the rule `type_layout_pending` applies to the built
// type, which looks behind fixed arrays and stops at every reference.
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

// `check_type` with the answer to "does anything store a value of the base
// type here": a function type's own result and parameters are written types
// that store nothing, and pass false whatever their suffixes say.
static check_type_t check_type_at(check_t* ck, ast_node_t* node, type_pos_t pos, bool in_storage) {
    const bool wrapped = node->kind == AST_TYPE;
    ast_node_t* base = wrapped ? node->a : node;
    const bool allow_noreturn = pos == TYPE_POS_RETURN;
    const bool stores_base = in_storage && suffixes_store_base(node);
    const type_t* b = base_type(ck, base, allow_noreturn, stores_base);
    if (base->kind == AST_TYPE_NORETURN && wrapped && ast_len(node) > 0) {
        // `noreturn` is a return type and nothing else (D8.5).
        check_error(ck, node->loc, "'noreturn' is a return type");
        b = type_error(&ck->types);
    }
    type_suffix_t suffixes[CHECK_MAX_SUFFIXES];
    const uint64_t count = wrapped ? ast_len(node) : 0;
    bool ok = !check_poisoned(b);
    if (count > CHECK_MAX_SUFFIXES) {
        check_error(ck, node->loc, "too many type suffixes");
        ok = false;
    }
    for (uint64_t i = 0; ok && i < count; i++) {
        ast_node_t* s = ast_child(node, i);
        suffixes[i].kind = (suffix_kind_t)s->op;
        suffixes[i].len = 0;
        suffixes[i].own = ast_is_own(s);
        suffixes[i].mut = ast_is_mut(s);
        if (suffixes[i].kind == SUFFIX_ARRAY && !array_length(ck, s->a, &suffixes[i].len)) {
            ok = false;
        }
    }
    if (!ok) {
        node->type = type_error(&ck->types);
        return type_result(node->type, false);
    }
    bool base_own = wrapped && ast_is_own(node);
    bool base_mut = wrapped && ast_is_mut(node);
    if (pos == TYPE_POS_ALLOC) {
        // `new` allocates storage that is writable at every level (D5.8), so
        // every position but the one a fixed-array suffix follows is marked.
        // `void` is the exception: it has no target level to carry a marker
        // (D3.11), and `new(void*)` is legal (D17.3), so marking the base
        // would reject the one pointer slot the standard library's ptr_vec
        // allocates.
        base_mut = (count == 0 || suffixes[0].kind != SUFFIX_ARRAY) && b->kind != TYPE_VOID;
        for (uint64_t i = 0; i < count; i++) {
            suffixes[i].mut = i + 1 == count || suffixes[i + 1].kind != SUFFIX_ARRAY;
        }
    }
    const type_build_t built =
        type_build(&ck->types, base_own, base_mut, b, suffixes, (uint32_t)count);
    if (built.type == NULL) {
        check_error(ck, node->loc, built.error);
        node->type = type_error(&ck->types);
        return type_result(node->type, false);
    }
    if (built.mut0 && pos != TYPE_POS_BINDING && pos != TYPE_POS_ALLOC) {
        // The outermost position of a field or of a return type never carries
        // `mut` (D5.5), and neither does a cast target, a result having no
        // binding (D3.14).
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

// ---- ownership (D17) -------------------------------------------------------------

bool check_owning(const type_t* t) {
    if (type_is_reference(t)) {
        // An `own` reference owns the allocation it designates (D17.1).
        return t->own;
    }
    // A struct or fixed array that holds an `own` reference by value is an
    // owning aggregate (D17.7); one whose layout is still unresolved has no
    // answer yet, and the declaration that needed it reported that (D3.8).
    return type_layout_pending(t) == NULL && type_is_owning_aggregate(t);
}

// An `own` rvalue: `new(...)`, a call result, `move(...)` or a `cast` to an
// `own` type, none of which any binding could later `del` (D17.5, D17.8).
// `null` is not one of them: it owns nothing, whatever `own` type it adopts
// from its context, and `del(null)` is a no-op (D10.5, D17.9).
static bool is_owning_rvalue(const expr_t* e) {
    return !e->lvalue && e->value.kind != CV_NULL && check_owning(e->type);
}

bool check_owning_temporary(check_t* ck, loc_t loc, const expr_t* e, const char* what) {
    if (check_poisoned(e->type) || !is_owning_rvalue(e)) {
        return false;
    }
    // An `own` rvalue may only land in an `own` place, reach an `own`
    // parameter or be `del`ed; anything else leaks it (D17.8).
    check_msg_begin(ck);
    msg_str(&ck->msg, "owning temporary would leak: ");
    msg_str(&ck->msg, what);
    check_msg_end(ck, loc);
    return true;
}

// ---- untyped constants in context (D4.1 to D4.5) -----------------------------------

static bool type_is_integer_prim(const type_t* t) {
    return t->kind == TYPE_PRIM && prim_is_integer(t->prim);
}

// Whether the untyped constant `v` may take the type `t` (D4.2, D4.3, D10.5),
// with the diagnostic that names the reason.
static bool constant_fits(check_t* ck, loc_t loc, cval_t v, const type_t* t, const char* what) {
    if (v.kind == CV_NULL) {
        // `null` has no type of its own: it takes a pointer, `void*` or
        // function-pointer type from its context (D10.5).
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
        // An integer never becomes an enum implicitly (D3.9).
        msg_str(&ck->msg, "an integer constant does not become an enum: use cast");
    } else if (t->kind == TYPE_PRIM && t->prim == PRIM_CHAR) {
        // An integer literal never becomes `char` implicitly (D4.3).
        msg_str(&ck->msg, "an integer constant does not become char: use cast");
    } else if (t->kind == TYPE_PRIM && t->prim == PRIM_BOOL) {
        // There is no truthiness: a condition is a `bool` (D3.3).
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
// universe builtin that yields nothing (D12.2).
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

// Gives every node of an untyped constant expression the type its context
// fixed (D4.1). A node that folded stands for its whole subtree, so only its
// value meets the context (D4.4: the intermediates are exact); a node that
// did not fold, `1 << n` with a variable count, passes the context on to the
// operands that do carry a value.
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
            // A char constant in an integer context is its code point (D4.3).
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
    return ok;
}

// The default type of an untyped constant with no context (D4.5).
static void default_type(check_t* ck, ast_node_t* n, expr_t* e) {
    if (!e->untyped) {
        return;
    }
    e->untyped = false;
    prim_kind_t k = PRIM_I32;
    if (e->value.kind == CV_NULL) {
        // `null` is an error where no pointer type is expected (D10.5).
        check_error(ck, n->loc, "'null' needs a pointer-typed context");
        e->type = type_error(&ck->types);
    } else if (e->value.kind != CV_NONE && !cv_default_kind(e->value, &k)) {
        // An untyped integer that fits neither i32 nor i64 has no default
        // type (D4.5).
        check_error(ck, n->loc, "constant expression out of range");
        e->type = type_error(&ck->types);
    } else if (e->value.kind == CV_NONE) {
        // `1 << n` with no context: the left operand takes its default type.
        k = PRIM_I32;
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

// An expression used for its value with no context of its own (D4.5), which
// also refuses a call that yields nothing (D12.2).
static void value_of(check_t* ck, ast_node_t* n, expr_t* e) {
    default_type(ck, n, e);
    if (!check_poisoned(e->type)) {
        (void)check_no_value(ck, n, e);
    }
}

// The ownership rules a value meets when it reaches an expected type. An
// owning target is an `own` place, which an owning lvalue enters only as
// `move(lv)` and an owning rvalue enters as it is (D17.5, D17.7); a target
// that does not own takes the lend of an owning lvalue (D17.4) but would
// leak an owning rvalue (D17.8). `implicit_move` is the one exception, the
// `return` of a bare `own` local or parameter (D17.5).
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
        // Transfer is written: copying an `own` lvalue into an `own` place
        // requires `move`, which empties the source (D17.5, D17.7).
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
    // Nothing could ever `del` the temporary once it has been lent (D17.8).
    check_msg_begin(ck);
    msg_str(&ck->msg, "owning temporary would leak: ");
    msg_str(&ck->msg, what);
    msg_str(&ck->msg, " expects ");
    check_msg_type(ck, target);
    check_msg_end(ck, n->loc);
    e->type = type_error(&ck->types);
}

// Converts a checked expression to the type its context expects: an untyped
// constant takes that type (D4.1), a typed one may drop mutability and
// ownership (D5.4, D17.4) and nothing else, and the ownership rules of D17.5
// and D17.8 decide whether the value may be copied there at all.
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

// Whether the operand of a `return` is a local or a parameter named outright,
// which is the implicit move of D17.5 and D17.7; a field, an element or any
// other lvalue is written `move`.
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
        // The emitter empties the operand after reading it, which is what
        // lets a `defer del(x)` written above see the zero value (D7.8,
        // D17.5).
        ret->ann |= CHECK_ANN_MOVE;
    }
}

void check_expr_default(check_t* ck, ast_node_t* node, expr_t* out) {
    check_expr(ck, node, out);
    value_of(ck, node, out);
    // No context means no `own` place, so an owning rvalue used here --
    // `println(str.dup(s))` above all -- would leak (D4.5, D17.8).
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
    // Conditions must be `bool`: there is no truthiness (D3.3).
    if (e.type->kind != TYPE_PRIM || e.type->prim != PRIM_BOOL) {
        check_msg_begin(ck);
        msg_str(&ck->msg, what);
        msg_str(&ck->msg, " must be bool, not ");
        check_msg_type(ck, e.type);
        check_msg_end(ck, node->loc);
    }
}

// ---- operators (D6.2) --------------------------------------------------------------

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
// bitwise operator is integer-only (D6.2).
static bool op_takes_floats(int32_t op) {
    return op == TOK_PLUS || op == TOK_MINUS || op == TOK_STAR || op == TOK_SLASH;
}

// The reference `t` holds, lent: `==`, `!=` and `?:` lend their operands, so
// an `own` and a non-`own` operand of the same type compare (D6.2, D17.4).
const type_t* check_lend(check_t* ck, const type_t* t) {
    switch (t->kind) {
    case TYPE_PTR:
        return type_ptr(&ck->types, t->elem, false, t->mut);
    case TYPE_SPAN:
        return type_span(&ck->types, t->elem, false, t->mut);
    case TYPE_STRING:
        return type_string(&ck->types, false);
    case TYPE_VOIDPTR:
        return type_voidptr(&ck->types, false);
    default:
        return t;
    }
}

// Equality is defined on integers, floats, bool, char, enums, pointers,
// function pointers and string, and is an error on structs, fixed arrays and
// spans (D3.13).
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

// Ordering exists on integers, floats and `char` only (D6.2, D3.2).
static bool type_has_ordering(const type_t* t) {
    return t->kind == TYPE_PRIM && t->prim != PRIM_BOOL;
}

// "there is no pointer arithmetic": `p + 1`, `p++` and `p[i]` are errors, and
// the only ways to obtain a pointer are null, &, new, .ptr, cast, a function
// name and calls (D10.4).
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

// The exact folding of D4.4 on two untyped constants.
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
        // A wrapping operator among untyped constants has no width to wrap
        // at, so it folds exactly like its checked form and a result outside
        // the constant range is an error (D4.4, D4.6, D11.2).
        ok = cv_mul(a, b, out);
        break;
    default:
        fatal_internal("check: folding an operator that is not arithmetic");
    }
    if (!ok) {
        // Untyped integers are evaluated exactly in [-2^63, 2^64 - 1] (D4.4).
        check_error(ck, loc, "constant expression out of range");
    }
    return ok;
}

// The typed folding of D4.6: every checked-mode rule applies at compile time,
// so an overflow or a division by zero is a compile error and not a trap.
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

// The shift rules of D6.2 and D4.4: the left operand keeps its own type, the
// count is not a context for it, and a constant count outside the width is a
// compile error.
static void check_shift(
    check_t* ck, loc_t loc, int32_t op, expr_t* a, ast_node_t* rhs, expr_t* b, expr_t* out) {
    default_type(ck, rhs, b);
    if (check_poisoned(a->type) || check_poisoned(b->type)) {
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
        // The left operand has no width until its context fixes one, so the
        // count is checked against the range of an untyped constant, 0..63
        // (D4.4), whether or not the operand folded.
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
                // The count is in range, so an exact result that leaves the
                // constant range is what failed (D4.4).
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
            // A constant count that is negative or at least the width of the
            // left operand is a compile error (D6.2).
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

// Two untyped constants: the arithmetic folds exactly and a comparison yields
// a bool (D4.4).
static void check_untyped_pair(
    check_t* ck, loc_t loc, int32_t op, expr_t* a, expr_t* b, expr_t* out) {
    const bool chars = a->value.kind == CV_CHAR && b->value.kind == CV_CHAR;
    if (a->value.kind == CV_NULL || b->value.kind == CV_NULL) {
        // `null` has no type of its own, so two of them do not compare (D10.5).
        check_error(ck, loc, "'null' has no type of its own");
        return;
    }
    if (op_is_logical(op)) {
        error_operand(ck, loc, op, "bool operands", a->type);
        return;
    }
    if (a->value.kind == CV_NONE || b->value.kind == CV_NONE) {
        // A shift by a variable count leaves an untyped expression with no
        // value; it still takes its type from context (D4.1).
        out->type = a->type;
        out->untyped = !op_is_comparison(op);
        if (op_is_comparison(op)) {
            out->type = type_prim(&ck->types, PRIM_BOOL);
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
    // The provisional type is the default one; a context replaces it (D4.5).
    out->type = cv_default_kind(v, &k) ? type_prim(&ck->types, k) : type_error(&ck->types);
}

// The operand rules of D6.2 on the type the operator is applied to.
static bool operand_kind_ok(check_t* ck, loc_t loc, int32_t op, const type_t* t) {
    if (check_poisoned(t)) {
        return true;
    }
    if (op_is_logical(op)) {
        // `! && ||` take bool only (D3.3).
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
    // Arithmetic, wrapping and bitwise operators take integers, and `+ - * /`
    // floats as well (D6.2).
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
        check_untyped_pair(ck, loc, op, a, b, out);
        return;
    }
    // The typed operand decides whether the operator applies at all, before
    // the untyped one adopts its type, so that `c + 1` on a char reports the
    // arithmetic rule of D3.2 and not a constant that does not fit.
    if (!operand_kind_ok(ck, loc, op, a->untyped ? b->type : a->type)) {
        return;
    }
    // An untyped operand takes the type of the other one (D4.1).
    if (a->untyped) {
        convert(ck, lhs, a, b->type, "the operand");
    } else if (b->untyped) {
        convert(ck, rhs, b, a->type, "the operand");
    }
    if (check_poisoned(a->type) || check_poisoned(b->type)) {
        return;
    }
    if (op_is_equality(op)) {
        // The operands of `==` and `!=` lend, so `own` never blocks a
        // comparison (D6.2, D17.4); an owning rvalue is lent too and nothing
        // would be left to free it (D17.8).
        if (check_owning_temporary(ck, lhs->loc, a, "comparing it leaves no owner") ||
            check_owning_temporary(ck, rhs->loc, b, "comparing it leaves no owner")) {
            return;
        }
    }
    const type_t* lt = op_is_equality(op) ? check_lend(ck, a->type) : a->type;
    const type_t* rt = op_is_equality(op) ? check_lend(ck, b->type) : b->type;
    if (!type_equal(lt, rt)) {
        // There is no promotion: every mixed-type operation is an error
        // (D6.2), mutability levels included.
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
        // `&& ||` fold only when both operands are constants (D4.6).
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

// ---- expressions (core-language.md 5) ----------------------------------------------

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

// An untyped constant: its type is provisional until a context fixes it
// (D4.1), so the node is marked and the value recorded.
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

// The member of an enum, or NULL: members are scoped to their enum and live
// in no namespace (D3.9).
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

// The field of a struct, or NULL.
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

// The value an identifier or a qualified name denotes, once its symbol is
// known: a constant carries its folded value (D4.6), a variable is an lvalue
// with its level-0 mutability (D5.7).
static void value_of_sym(check_t* ck, ast_node_t* n, const sym_t* s, expr_t* out) {
    resolve_sym(ck, (sym_t*)s);
    out->sym = s;
    out->type = s->type != NULL ? s->type : type_error(&ck->types);
    switch (s->kind) {
    case SYM_CONST:
        // A module-level immutable declaration is a constant expression and
        // an immutable lvalue in read-only memory, which `move` and `del`
        // may not empty (D4.6, D6.7, D17.6).
        out->lvalue = true;
        out->empty = EMPTY_READONLY;
        out->value =
            s->node != NULL && s->node->b != NULL ? check_node_value(ck, s->node->b) : cv_none();
        out->init_const = true;
        break;
    case SYM_GLOBAL:
    case SYM_LOCAL:
    case SYM_PARAM:
        // A variable is an lvalue with its level-0 mutability; a read of a
        // `mut` global is not a constant expression (D4.6, D6.7). Its own
        // storage is emptiable whether or not the binding is `mut`, since
        // emptying is not an assignment (D17.6); a constant's is not, being
        // read-only memory.
        out->lvalue = true;
        out->mut = s->mut0;
        out->empty = EMPTY_OK;
        break;
    case SYM_EXTERN_FN:
        if (n != ck->callee) {
            // An `extern fn` in value position is an error: an extern is
            // called through the variadic LLVM type its declaration supplies,
            // and an indirect call site has no callee to take that form from,
            // so the vector-register count would go unset (D3.10, D9.8).
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
        // A function name used as a value has its function type; `&f` is an
        // error (D3.10), and a function name is a module-level initializer
        // (D7.10).
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
            // A universe function is callable and nothing else (D12.2).
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

// The module a binding denotes, or NULL: only the module reading of an
// import binds one (D9.3).
static const module_t* module_of(const binding_t* b) {
    return b != NULL && b->kind == BIND_MODULE ? (const module_t*)b->module : NULL;
}

// What an expression names when it is not a value: a module or a type, which
// only a `.` may follow (D9.4, D3.9). NULL for every ordinary expression. The
// binding is returned rather than the symbol because a module is reached
// through it.
static const binding_t* names_module_or_type(check_t* ck, ast_node_t* n) {
    const binding_t* b = NULL;
    if (n->kind == AST_IDENT) {
        b = lookup(ck, n->name);
    } else if (n->kind == AST_FIELD && n->a != NULL) {
        // Qualified access sees the declarations of a module, not its
        // imports, which are not re-exported (D9.3, module-system.md 4):
        // `m.other.f()` is an error even when `m` imports `other`. The caller
        // then checks the operand as an expression, which reports that `m`
        // has no declaration of that name.
        b = module_declaration(module_of(names_module_or_type(ck, n->a)), n->name);
    }
    const sym_t* s = sym_of_binding(b);
    if (s == NULL || (s->kind != SYM_MODULE && s->kind != SYM_STRUCT && s->kind != SYM_ENUM)) {
        return NULL;
    }
    n->sym = s;
    return b;
}

// `.len` and `.ptr`, the read-only pseudo-fields of arrays, spans and strings
// (D3.4, D3.5, D3.7).
static bool pseudo_field(check_t* ck, ast_node_t* n, const type_t* t, expr_t* op, expr_t* out) {
    const bool len = str_eq(n->name, str_from_cstr("len"));
    const bool ptr = str_eq(n->name, str_from_cstr("ptr"));
    if (!len && !ptr) {
        return false;
    }
    if (t->kind == TYPE_ARRAY && len) {
        // `.len` of a fixed array is an untyped integer constant and its
        // operand is not evaluated (D3.4, D4.6).
        untyped(ck, n, out, cv_from_u64(t->len));
        return true;
    }
    if (t->kind == TYPE_SPAN || t->kind == TYPE_STRING) {
        if (len) {
            out->type = type_prim(&ck->types, PRIM_U64);
            return true;
        }
        // `.ptr` carries the element level's mutability and is never `own`
        // (D3.5, D3.7, D17.3).
        const type_t* elem = t->kind == TYPE_STRING ? type_prim(&ck->types, PRIM_CHAR) : t->elem;
        out->type = type_ptr(&ck->types, elem, false, t->kind == TYPE_SPAN && t->mut);
        return true;
    }
    if (t->kind == TYPE_ARRAY && ptr) {
        // Fixed arrays have no `.ptr` (D3.4).
        check_error(ck, n->name_loc, "a fixed array has no '.ptr'");
        return true;
    }
    (void)op;
    return false;
}

// `e.f`, `p->f` and the qualified forms of D9.4.
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
            // `color.red` is the only spelling of a member (D3.9).
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
    check_expr(ck, n->a, &op);
    if (check_poisoned(op.type)) {
        return;
    }
    // A field of an owning aggregate rvalue, and `.len` or `.ptr` of an
    // owning span rvalue, would leave the allocation with no owner (D17.8).
    if (check_owning_temporary(ck, n->loc, &op, "a field of it leaves no owner")) {
        return;
    }
    const type_t* t = op.type;
    bool mut = op.mut;
    bool lvalue = op.lvalue;
    // A field of a local counts as the local, and one reached through `->`
    // needs level 1 of the pointer mutable (D17.6).
    empty_kind_t empty = op.empty;
    if (arrow) {
        // `p->f` is `(*p).f` and is required for pointers (D6.10).
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
        // `.len` and `.ptr` are never lvalues (D6.7).
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
    // A field's own storage is as mutable as the value that contains it
    // (D5.5, D5.7).
    out->lvalue = lvalue;
    out->mut = mut;
    out->empty = empty;
}

// ---- unary, indexing and span expressions ------------------------------------------

static void check_unary(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_t a;
    if (n->op == TOK_AMP) {
        // `&e` requires an lvalue and yields a borrowed pointer whose level 1
        // is the mutability of `e` (D5.8, D17.3). Its operand is asked for an
        // address and not for a value, which is what lets a module-level
        // declaration hold its own address (D7.10); the flag is saved and
        // restored, since a `&` may stand anywhere.
        const bool outer_addr_only = ck->addr_only;
        ck->addr_only = true;
        check_expr(ck, n->a, &a);
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
        // `&` of a module-level declaration is a module-level initializer
        // (D7.10).
        out->init_const = a.sym != NULL && (a.sym->kind == SYM_CONST || a.sym->kind == SYM_GLOBAL);
        return;
    }
    if (n->op == TOK_STAR) {
        check_expr(ck, n->a, &a);
        if (check_poisoned(a.type)) {
            return;
        }
        if (a.type->kind != TYPE_PTR) {
            // `void*` has no pointee level and a function pointer is called,
            // not dereferenced (D3.11, D3.10).
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
        // What `*p` designates is emptiable when level 1 of `p` is mutable
        // (D17.6); a constant's read-only memory stops at the indirection.
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
        // `-c` and `~c` fold exactly on an untyped constant, `~c` being
        // `-c - 1` (D4.4).
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
        // Unary `-` takes signed integers and floats only (D6.2).
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
// a negative constant is a compile error (D4.1, D6.8, D6.9, D10.2).
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

// The element type of an operand that may be indexed or spanned (D6.8).
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
    check_expr(ck, n->a, &a);
    cval_t index = cv_none();
    const bool ok = check_count(ck, n->b, "index", &index);
    if (check_poisoned(a.type)) {
        return;
    }
    // Indexing an owning rvalue reads through an allocation nothing owns
    // (D17.8).
    if (check_owning_temporary(ck, n->loc, &a, "indexing it leaves no owner")) {
        return;
    }
    const type_t* elem = element_of(ck, a.type);
    if (elem == NULL) {
        // Pointers cannot be indexed, not even pointers to arrays (D6.8,
        // D10.4).
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
            // A constant index out of range for a fixed array is a compile
            // error (D6.8).
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
    // `e[i]` is an lvalue where `e` is an lvalue fixed array, or any span or
    // string expression (D6.7): indexing an rvalue array yields a copy.
    out->lvalue = a.type->kind != TYPE_ARRAY || a.lvalue;
    // The mutability of an element: of the array's own storage, of level 1 of
    // a span, never of a string (D5.7).
    out->mut = a.type->kind == TYPE_ARRAY ? a.mut : (a.type->kind == TYPE_SPAN && a.type->mut);
    // An element of a local array counts as the local, a constant array's
    // read-only memory included; one behind a span is emptiable only where
    // level 1 of the span is mutable (D17.6).
    if (a.type->kind == TYPE_ARRAY) {
        out->empty = a.empty;
    } else {
        out->empty = out->mut ? EMPTY_OK : EMPTY_IMMUTABLE;
    }
}

static void check_span_expr(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_t a;
    check_expr(ck, n->a, &a);
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
    // A span of an owning rvalue is a view of an allocation nothing owns
    // (D17.8).
    if (check_owning_temporary(ck, n->loc, &a, "a span of it leaves no owner")) {
        return;
    }
    if (a.type->kind == TYPE_PTR) {
        // `p[lo..hi]` is the unchecked escape for foreign memory and has only
        // the two-bound form, a pointer having no length (D6.9, D10.4).
        if (n->b == NULL || n->c == NULL) {
            check_error(ck, n->loc, "a pointer has no length: write 'p[lo..hi]'");
            return;
        }
        out->type = type_span(&ck->types, a.type->elem, false, a.type->mut);
        return;
    }
    if (a.type->kind == TYPE_STRING) {
        // A span of a string is a string (D3.7).
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
        // A span of a fixed array needs an lvalue (D6.9).
        check_error(ck, n->loc, "a span of a fixed array needs an lvalue");
        return;
    }
    // The result is a view whose element mutability is that of the operand's
    // elements, and never `own` (D6.9, D17.3).
    const bool mut = a.type->kind == TYPE_ARRAY ? a.mut : a.type->mut;
    out->type = type_span(&ck->types, elem, false, mut);
}

// ---- cast, sizeof and new ----------------------------------------------------------

static void check_cast(check_t* ck, ast_node_t* n, expr_t* out) {
    expr_t a;
    check_expr(ck, n->a, &a);
    if (a.value.kind == CV_NULL) {
        // `null` is not a valid operand, having no type of its own (D3.14,
        // D10.5).
        check_error(ck, n->a->loc, "'null' is not a cast operand");
        (void)check_type(ck, n->b, TYPE_POS_CAST);
        return;
    }
    // A `cast` is not a context: an untyped operand takes its default type
    // first and is then converted with run-time semantics (D4.1, D3.14).
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
        check_msg_end(ck, n->loc);
        return;
    }
    // A cast is `own` exactly when its target says so (D3.14): an owning
    // lvalue reaches an owning target only through `move` (D17.5, D17.12) and
    // an owning rvalue cast to a target that does not own would leak (D17.8).
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
    // `cast` among numeric types, char and enums folds (D4.6); the value is
    // the one the run time would produce (D3.14).
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
        // `sizeof(void)` is an error (D3.15).
        check_error(ck, n->loc, "'sizeof' needs a sized type, not void");
        return;
    }
    if (!check_layout(ck, t.type) || !check_size_fits(ck, n->loc, t.type)) {
        return;
    }
    // `sizeof(Type)` yields an untyped integer constant (D3.15, D4.6).
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
        // `new(void)` is an error because `void` has no size; `new(void*)`,
        // one pointer slot, is not (D10.2, D17.3).
        check_error(ck, n->loc, "'new' needs a sized type, not void");
        return;
    }
    if (!check_layout(ck, t.type) || !check_size_fits(ck, n->loc, t.type)) {
        return;
    }
    // `new(T)` is `T mut* own` and `new(T, n)` is `T mut@ own` (D10.2,
    // D17.3).
    out->type = counted ? type_span(&ck->types, t.type, true, t.mut0)
                        : type_ptr(&ck->types, t.type, true, t.mut0);
}

// ---- calls (D6.11, D12.2) ----------------------------------------------------------

static bool is_builtin(const sym_t* s, const char* name) {
    return s != NULL && s->kind == SYM_BUILTIN && str_eq(s->name, str_from_cstr(name));
}

// The printable types of D12.2 and 8.3: structs, arrays and spans are not
// printable.
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

// The print family (D12.2, 8.3): zero or more printable arguments, each
// taking its default type, after the descriptor of the `fprint` forms.
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

// Whether the lvalue `e` may be emptied by `move` or `del` (D17.6, D17.9):
// a module-level constant lives in read-only memory, and an operand reached
// through an indirection needs that level mutable, because the store is
// visible to everyone else who holds the pointer or span. `verb` is the
// builtin's name, which the diagnostic quotes.
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
    // Every universe function but `move` yields no value (D12.2).
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
        // `fprint(fd, ...)` writes to the i32 descriptor `fd` (D12.2).
        check_expr_as(ck, ast_child(n, 0), type_prim(&ck->types, PRIM_I32), "a descriptor", &fd);
        check_print(ck, n, 1);
        return;
    }
    if (is_builtin(s, "assert")) {
        if (check_arity(ck, n, s->name, 1)) {
            // `assert(cond)` takes a bool and is active in both build modes
            // (D12.2).
            check_condition(ck, ast_child(n, 0), "'assert'");
        }
        return;
    }
    if (is_builtin(s, "panic")) {
        if (check_arity(ck, n, s->name, 1)) {
            expr_t m;
            check_expr_as(ck, ast_child(n, 0), type_string(&ck->types, false), "'panic'", &m);
        }
        // `panic` is noreturn, so a `panic(...)` statement terminates (D8.4).
        n->ann |= CHECK_ANN_NORETURN;
        return;
    }
    if (is_builtin(s, "del")) {
        if (!check_arity(ck, n, s->name, 1)) {
            return;
        }
        ast_node_t* arg = ast_child(n, 0);
        expr_t e;
        check_expr(ck, arg, &e);
        if (e.untyped && e.value.kind == CV_NULL) {
            // `del(null)`: the literal adopts `void* own` and the call is a
            // no-op (D12.2).
            convert(ck, arg, &e, type_voidptr(&ck->types, true), "'del'");
            return;
        }
        if (check_poisoned(e.type)) {
            return;
        }
        if (!type_is_reference(e.type) || !e.type->own) {
            // `del` frees an operand of an `own` type; a view, a stack
            // address or an aggregate is a compile error (D12.2, D17.9).
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
            // On an lvalue `del` empties the operand under the rules of
            // D17.6; on an rvalue it only frees (D17.9).
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
        check_expr(ck, arg, &e);
        if (check_poisoned(e.type)) {
            return;
        }
        // Every failure below poisons the result: a `void` one would add
        // "'move' has no value" to the real diagnostic (D12.2, D14.2).
        out->type = type_error(&ck->types);
        if (!e.lvalue) {
            check_error(ck, arg->loc, "'move' takes an lvalue");
            return;
        }
        if (!check_owning(e.type)) {
            // `move` takes an owning lvalue and leaves the zero value behind
            // (D12.2, D17.6).
            check_msg_begin(ck);
            msg_str(&ck->msg, "'move' needs an owning operand, not ");
            check_msg_type(ck, e.type);
            check_msg_end(ck, arg->loc);
            return;
        }
        if (!check_emptiable(ck, arg, &e, s->name)) {
            return;
        }
        // The one universe function with a value, an `own` rvalue (D17.6).
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
            // A universe function is reached only after every scope, so a
            // declaration of the same name shadows it (D7.9, D12.2).
            callee->sym = b;
            callee->type = type_void(&ck->types);
            check_builtin_call(ck, n, b, out);
            return;
        }
    }
    expr_t f;
    // The callee is the one position an `extern fn` name may stand in (D3.10).
    const ast_node_t* outer_callee = ck->callee;
    ck->callee = callee;
    check_expr(ck, callee, &f);
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
    if (known &&
        !check_arity(
            ck, n, f.sym != NULL ? f.sym->name : str_from_cstr("the callee"), f.type->nparams)) {
        return;
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        expr_t arg;
        if (known && i < f.type->nparams) {
            // Arguments are matched by position and must convert to the
            // parameter types (D6.11).
            check_expr_as(ck, ast_child(n, i), f.type->params[i], "the argument", &arg);
        } else {
            check_expr_default(ck, ast_child(n, i), &arg);
        }
    }
    if (!known) {
        return;
    }
    out->type = f.type->elem;
    if (f.type->noreturn) {
        // A call to a `noreturn` function is a terminating statement (D8.4).
        n->ann |= CHECK_ANN_NORETURN;
    }
}

// ---- struct and array literals (D6.5) ----------------------------------------------

// The designated form: any order, omitted fields zeroed, no duplicates, and
// designators only on structs (D6.5).
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

// A bare `{ ... }` against the type it initializes (D6.5).
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
        // `= {}` zero-initializes any aggregate, span, string or enum (D6.5).
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
            // A positional literal has every field, in order (D6.5).
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
            // A typed array literal has exactly N elements or is `{}` (D6.5).
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
        // A span literal does not exist; the zero span is `{}` (D3.5).
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
// own name gives (D6.5).
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
        // An integer literal is an untyped constant (D4.1); the parser holds
        // its magnitude (D2.5).
        untyped(ck, n, out, cv_from_u64(n->ival));
        break;
    case AST_CHAR:
        untyped(ck, n, out, cv_from_char(n->ival));
        break;
    case AST_NULL:
        // `null` has no type of its own and takes one from context (D10.5).
        untyped(ck, n, out, cv_null());
        break;
    case AST_BOOL:
        out->type = type_prim(&ck->types, PRIM_BOOL);
        out->value = cv_from_bool(n->ival != 0);
        out->init_const = true;
        break;
    case AST_STRING:
        // A string literal has type `string` and is borrowed (D3.7, D2.9).
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
        // Bare braces initialize a declaration; they are not an expression
        // (D6.5).
        check_error(ck, n->loc, "a brace initializer is not an expression");
        break;
    case AST_TERNARY:
        check_error(ck, n->loc, "not supported by the bootstrap compiler: ?:");
        break;
    case AST_ERROR:
        // The tokens a syntax error made the parser skip: already reported
        // (D14.2).
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

// ---- declarations (D7.10, D8.1, D3.8, D3.9) ----------------------------------------

// The two states of the lazy resolution of D7.10, kept on the declaring node.
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
        // A struct reached through a pointer while it is being resolved is
        // ordinary recursion; only value containment is a cycle, and the
        // layout catches that (D3.8).
        if (s->kind == SYM_STRUCT) {
            return;
        }
        if (ck->addr_only && (s->kind == SYM_CONST || s->kind == SYM_GLOBAL) && s->type != NULL) {
            // `&N` inside N's own initializer closes no cycle: it asks for an
            // address, and D7.10 admits `&` of a module-level declaration
            // from any module, this one included. Everything that answer
            // needs is already known, because resolve_var writes the type
            // before it checks the initializer; the value stays CV_NONE,
            // which is what an address is (D4.6).
            return;
        }
        // A declaration reached while it is being resolved closes a cycle
        // (D7.10, D4.6); D3.9 forbids the same for an enum member value.
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
        // Which of the two it is follows from the type it declares (D7.10).
        resolve_var(ck, s);
        break;
    default:
        break;
    }
    node->ann &= ~(uint32_t)CHECK_ANN_RESOLVING;
    node->ann |= CHECK_ANN_RESOLVED;
    node->type = s->type;
    node->sym = s;
}

static void resolve_struct(check_t* ck, sym_t* s) {
    ast_node_t* decl = (ast_node_t*)s->node;
    // The record the layout is driven through: a field that fails poisons the
    // symbol's type, and the layout still has to be closed on the struct node
    // itself (D3.8).
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
        // An error node stands where a field was expected (D14.2).
        if (f->kind != AST_FIELD_DECL) {
            continue;
        }
        if (n >= CHECK_MAX_MEMBERS) {
            check_error(ck, f->loc, "too many fields");
            ok = false;
            break;
        }
        // A field's own storage follows its struct, so its outermost position
        // carries no `mut` (D5.5).
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
        // An empty struct is an error (D3.8); the parser reports a body with
        // no field, so a body with none here is one whose fields all failed.
        if (type_layout_state(record) != LAYOUT_ERROR) {
            type_layout_fail(record);
        }
        sym_fail(ck, s);
        return;
    }
    if (!type_layout_struct(record, fields, (uint32_t)n, offsets)) {
        // The struct passes the ceiling of D3.4, reported like the
        // infinite-size error of D3.8, at the `struct` keyword (D14.2).
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
            // The byte offset D3.8 gives the field, recorded on its
            // declaration. The IR emitter names a field by its index and lets
            // LLVM compute the address from the same layout (toolchain.md 6
            // item 3), so the only consumers are check_test.c and
            // check_conv_test.c, where these offsets are the one place the
            // compiler states its own answer for a whole declaration; the
            // loop walks exactly the fields of the struct's IR type, since
            // every field that failed left `ok` false above.
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
            // An explicit value is a constant expression that may not refer
            // to the enum itself (D3.9).
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
            // The underlying type of an enum is i32 (D3.9).
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
                // Duplicate values are errors (D3.9).
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
        // Values start at 0 and increment (D3.9).
        if (!cv_add(v, cv_from_i64(1), &next)) {
            next = cv_from_i64(0);
        }
    }
    if (!ok) {
        sym_fail(ck, s);
    }
}

// The types an extern signature may use (D9.8): no spans, strings, structs or
// arrays. A function-pointer parameter is legal exactly when its own
// signature is extern-legal, result type included, since the C side calls
// through it with the same convention (module-system.md 8.1, D9.9); the
// recursion terminates because a function type is built from types written
// before it and cannot reach itself.
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

// `note: the compiler declares it as 'declare void @fort_rt_del(ptr)'`: the
// declaration the program conflicts with is the compiler's own, so the note
// shows it rather than pointing at an earlier one, as the note of a conflict
// between two modules does (module-system.md 13). A muted checker annotates
// the tree without reporting (D20.2), so the note follows the error.
static void note_runtime_declaration(check_t* ck, loc_t at, rt_entry_t rt) {
    if (ck->mute) {
        return;
    }
    sb_t decl;
    sb_init(&decl);
    rt_declaration(&decl, rt);
    str_t text = sb_view(&decl);
    if (text.len > 0 && text.ptr[text.len - 1] == '\n') {
        // The rendering ends the line; a note is one line already.
        text.len--;
    }
    msg_begin(&ck->msg);
    msg_str(&ck->msg, "the compiler declares it as '");
    msg_view(&ck->msg, text);
    msg_str(&ck->msg, "'");
    diag_note(at, msg_end(&ck->msg));
    sb_free(&decl);
}

// `conflicting declarations of extern 'fort_rt_del': parameter 1 differs from
// the runtime's`, in the wording module-system.md 13 gives the same conflict
// between two modules. `param` is the 1-based parameter for the difference
// that names one and 0 otherwise.
static void error_runtime_conflict(
    check_t* ck, loc_t at, str_t name, rt_entry_t rt, const char* what, uint64_t param) {
    check_msg_begin(ck);
    msg_str(&ck->msg, "conflicting declarations of extern ");
    msg_quote(&ck->msg, name);
    msg_str(&ck->msg, what);
    if (param > 0) {
        msg_uint(&ck->msg, param);
        msg_str(&ck->msg, " differs");
    }
    msg_str(&ck->msg, " from the runtime's");
    check_msg_end(ck, at);
    note_runtime_declaration(ck, at, rt);
}

// An `extern fn` naming a runtime entry point must agree with the signature
// toolchain.md 5.1 fixes for it. D9.8 requires the extern declarations of one
// symbol to agree with each other, and the compiler's own declaration is one
// of them: it replaces the user's with the canonical prototype (item 8), so a
// mismatch reaches no tool below -- opaque pointers make a call site's type
// independent of its callee's -- and is silently ABI-wrong. The standard
// library declares these legitimately (D13.1), so what is refused is a
// disagreement and never the declaration itself. Two signatures agree when
// each type takes the same IR form, attribute included (D9.9), since that is
// what the call the emitter writes is made of. Reports the first difference
// only, at the piece of the declaration that carries it.
static bool check_runtime_signature(check_t* ck,
                                    const ast_node_t* decl,
                                    str_t name,
                                    const type_t* ret,
                                    const type_t* const* params,
                                    uint64_t nparams) {
    const rt_entry_t rt = rt_entry_of(name);
    if (rt == RT_COUNT) {
        return true;
    }
    // A `noreturn` declaration of an entry point that returns claims more
    // than is true and suppresses the missing-`return` analysis of D8.5, so
    // the mark is part of the result; an entry point section 5.1 declares
    // `_Noreturn` may be written `void`, which claims less and is safe by
    // construction, since `gen_use_extern` stamps the `cold noreturn nounwind`
    // group from the table and never from the user's spelling (item 14).
    const bool noreturn = is_noreturn(decl->a);
    if (ir_form_of_type(ret) != rt_entry_result(rt) || (noreturn && !rt_entry_noreturn(rt))) {
        error_runtime_conflict(ck, decl->a->loc, name, rt, ": the result type differs", 0);
        return false;
    }
    if (nparams != (uint64_t)rt_entry_param_count(rt)) {
        error_runtime_conflict(
            ck, decl->name_loc, name, rt, ": the number of parameters differs", 0);
        return false;
    }
    for (uint64_t i = 0; i < nparams; i++) {
        if (ir_form_of_type(params[i]) == rt_entry_param(rt, (uint32_t)i)) {
            continue;
        }
        error_runtime_conflict(ck, param_at(decl, i)->loc, name, rt, ": parameter ", i + 1);
        return false;
    }
    return true;
}

// ---- two extern declarations of one C symbol (D9.8) ------------------------------------

// Whether a primitive is the byte C spells `unsigned char`: fort `char` is
// that type at the boundary (D3.2), so `char` and `u8` name one C type and
// `char*` may equally be written `u8*` (D9.8).
static bool is_c_byte(prim_kind_t k) {
    return k == PRIM_U8 || k == PRIM_CHAR;
}

// Whether two types in an extern signature name one C type. D9.8 requires the
// declarations of one C symbol to be identical, so this is type identity --
// interning makes it pointer equality below the nominal types, whose identity
// is their declaration (D3.8, D3.9) -- with the one relaxation D9.8 states
// itself, `char` for `u8`. Level marks below the binding are part of a type
// (D3.12) and `own` is too (D17.1), so both are compared; a binding-level
// `mut` is not part of a function type (D3.10) and never reaches here.
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
        return a->own == b->own;
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
        // A struct or enum reached through a pointer, and every kind an
        // extern signature may not use: two nodes the interning above did not
        // equate are two types.
        return false;
    }
}

// Whether a struct or an enum, whose identity is the declaration it comes
// from and not its spelling (D3.8, D3.9).
static bool is_nominal(const type_t* t) {
    return t->kind == TYPE_STRUCT || t->kind == TYPE_ENUM;
}

// Whether either side of the first difference between two disagreeing extern
// types names a struct or an enum, whose identity is its declaration and not
// its spelling (D3.8, D3.9): that is the case where writing the same words in
// both modules cannot make the declarations agree, so the note must offer the
// type or the wrapper. It says "either side" and not "each": one module
// spelling the parameter `i32` where the other names an enum is the same
// class of fix, the enum being importable. Two spellings of one type never
// reach here, since `color` and `shade.color` denote one declaration (D9.4)
// and the comparison above already agreed.
//
// It runs only after extern_type_agrees returned false, so it is a second
// walk over a pair known to differ and never a verdict on its own. Two
// deliberate differences from that walk: it stops at a `void*`, which erases
// its pointee and so can hide no nominal type, and it tolerates two function
// types of unequal arity by walking the shorter list, since it is asked which
// kind of difference was found and not whether one exists.
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

// `conflicting declarations of extern 'write': parameter 1 differs`, in the
// wording module-system.md 13 gives every row of this conflict: the error at
// the piece of the later declaration that carries the difference, then a note
// at the earlier declaration. `param` is the 1-based parameter for the
// difference that names one and 0 otherwise; `wrapper` asks for the note that
// says what to do when no shared spelling exists.
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
    // A muted checker annotates the tree without reporting, so the notes
    // follow the error rather than standing alone (D20.2).
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
    // A struct or an enum is identified by its declaration (D3.8, D3.9), so a
    // module that names one where the other names another type, or another
    // module's, cannot reach agreement by rewording: it imports the type, or
    // one module declares the symbol and exports a fort function the others
    // call (module-system.md 8.1).
    msg_str(&ck->msg,
            "a struct or an enum stands here, and its identity is its declaration and not its "
            "spelling: give both declarations that one type, importing it where it is missing, or "
            "declare the symbol in one module and call it through a fort function the others "
            "import");
    diag_note(at, msg_end(&ck->msg));
}

// The same C symbol may be declared `extern` in several modules provided the
// signatures are identical, `own` included (D9.8, D17.13): every module of
// the closure is checked in the dependency order the loader left, so the
// first declaration of a name is the one every later declaration is held
// against, and the difference is reported at the later one. The comparison is
// of types and not of what the two modules wrote, which is why it stands here
// and not in the loader: two spellings of one imported type are one type
// (D9.4), and two local types of one spelling are two.
static bool check_extern_agreement(check_t* ck, const ast_node_t* decl, const sym_t* s) {
    int64_t at = 0;
    if (!strmap_get(&ck->extern_first, s->name, &at)) {
        (void)strmap_put(&ck->extern_first, s->name, (int64_t)ck->externs.len);
        ptrvec_push(&ck->externs, (void*)s);
        return true;
    }
    const sym_t* first = (const sym_t*)ck->externs.items[at];
    if (first->node == decl) {
        // The same declaration, checked a second time through one checker
        // (the editor mode of D20.2): its symbol and the nominal types it
        // names are new records of the old tree, so the entry is replaced
        // rather than compared against itself. Replacing assumes this second
        // pass reaches here at all, which it does only when the declaration
        // still checks: a pass that fails it leaves the map pointing at the
        // first pass's symbol, whose nominal types belong to a resolution
        // that has been superseded, so a later module would be held against
        // types no module can name any more. Every failure path above this
        // one reports first, so such a run is already diagnosed and the stale
        // comparison can only add to a file that is not compiling; nothing
        // reaches it otherwise.
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

// The program entry point the compiler emits in the entry module (D11.6),
// whose name an `extern` may not declare (D9.7).
static const char ENTRY_SYMBOL[] = "fort_entry";

static void resolve_fn(check_t* ck, sym_t* s) {
    ast_node_t* decl = (ast_node_t*)s->node;
    const bool is_extern = s->kind == SYM_EXTERN_FN;
    if (is_extern && str_eq(s->name, str_from_cstr(ENTRY_SYMBOL))) {
        // `fort_entry` is reserved: the compiler emits its definition, so an
        // `extern` declaring it is not a second declaration of one C function
        // but a signature nothing can check against that definition (D9.7).
        check_msg_begin(ck);
        msg_quote(&ck->msg, s->name);
        msg_str(&ck->msg, " is reserved: the compiler emits it");
        check_msg_end(ck, decl->name_loc);
        sym_fail(ck, s);
        return;
    }
    // A return type has no binding, so its outermost position carries no
    // `mut` (D5.5); `noreturn` is a return type of its own (D8.5).
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
        // A `mut` in the outermost position makes the callee's copy
        // assignable and is not part of the function's type (D5.6, D3.10).
        ps->mut0 = pt.mut0;
        p->sym = ps;
        p->type = pt.type;
        if (check_poisoned(pt.type) || !check_layout(ck, pt.type) ||
            !check_size_fits(ck, p->loc, pt.type)) {
            sym_fail(ck, ps);
            ok = false;
            continue;
        }
        if (pt.type->kind == TYPE_VOID) {
            check_error(ck, p->loc, "'void' is only a return type or the base of 'void*'");
            sym_fail(ck, ps);
            ok = false;
            continue;
        }
        if (is_extern && !extern_legal(pt.type)) {
            // An extern signature may use only scalars, pointers and function
            // pointers (D9.8).
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
    if (ok && is_extern && !check_runtime_signature(ck, decl, s->name, ret.type, params, n)) {
        ok = false;
    }
    if (!ok) {
        sym_fail(ck, s);
        return;
    }
    s->type = type_fn(&ck->types, ret.type, params, (uint32_t)n, is_noreturn(decl->a));
    if (is_extern && !check_extern_agreement(ck, decl, s)) {
        // The declaration is well formed and what failed is its agreement
        // with another module's declaration of the same C symbol, but the
        // program has no one signature for it, so the symbol is poisoned and
        // every call to it in this module stays silent (D9.8, D14.2).
        sym_fail(ck, s);
    }
}

static void resolve_var(check_t* ck, sym_t* s) {
    ast_node_t* decl = (ast_node_t*)s->node;
    const check_type_t t = check_type(ck, decl->a, TYPE_POS_BINDING);
    s->type = t.type;
    s->mut0 = t.mut0;
    // `Type NAME = init;` is a compile-time constant and `mut Type g = init;`
    // a global (D7.10).
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
        // The initializer is mandatory and the parser enforces it (D7.1).
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
        // Module-level initializers are constant expressions extended with
        // `null`, function names and `&` of a module-level declaration
        // (D7.10): no calls and no reads of `mut` globals.
        check_error(ck, decl->b->loc, "a module-level initializer must be a constant expression");
        sym_fail(ck, s);
    }
}

// ---- modules (D9.10, D14.2) --------------------------------------------------------

// The symbol kind a top-level declaration introduces; a module-level variable
// is a constant or a global once its type is resolved (D7.10).
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

// Every node of an import whose own name token denotes the imported module or
// declaration carries its symbol: the item, its `as` alias, and the last
// segment of the path, which names the module of an item list and the
// declaration of a symbol import (D9.3). The segments before it name search
// directories rather than modules, so they carry nothing.
static void annotate_path(const ast_node_t* imp, const sym_t* last, const sym_t* module) {
    ast_node_t* path = imp->a;
    if (path == NULL || ast_len(path) == 0) {
        return;
    }
    const uint64_t n = ast_len(path);
    ast_child(path, n - 1)->sym = last;
    if (n >= 2 && module != NULL && module != last) {
        // `import a.b.c;` reads `c` in the module `a.b` (D9.3).
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
        // An item names a declaration of the module the path names (D9.3).
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
// the owner of the first item that resolved (D9.3).
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

// The annotation slots this pass owns, cleared before it writes them: the
// symbols of an earlier check belong to a checker that may be gone, so a
// module checked twice starts from the tree the parser left, which an editor
// that re-checks a file after an edit needs (D20.2).
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
// segment of the path (D9.3).
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
// loader reported leaves behind: a use of one says nothing further (D14.2).
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

// Phase one: one symbol per top-level declaration, so that the declarations
// of a module are order-independent (D7.10).
static void collect_module(check_t* ck, const module_t* m) {
    sym_t* ms = check_sym_new(ck, SYM_MODULE, m->path, m->ast, NULL);
    // A module's own record hangs on its AST_MODULE node, which is how a
    // binding reaches it (sym.h).
    m->ast->sym = ms;
    ck->module_sym = ms;
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        ast_node_t* decl = ast_child(m->ast, i);
        sym_kind_t kind = SYM_CONST;
        // An error node stands where a declaration was expected (D14.2).
        if (!decl_sym_kind(decl, &kind)) {
            continue;
        }
        sym_t* s = check_sym_new(ck, kind, decl->name, decl, ms);
        decl->sym = s;
        if (kind == SYM_STRUCT) {
            // The type exists before the fields are read, so that a struct
            // may contain itself through a pointer (D3.8).
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

// The entry module defines `fn i32 main()` or `fn i32 main(string@ args)`
// (D8.6).
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
    msg_str(&ck->msg, " must define 'fn i32 main()' or 'fn i32 main(string@ args)'");
    // An error without a position in the file uses 1:1 (D14.2).
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
    // Phase two: every declaration is resolved, each on demand, so that a
    // type or a constant reached from another one is complete when it is read
    // (D7.10).
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
        // The entry module defines main (D8.6). A module checked on its own
        // is a file under inspection and not a program, so the rule does not
        // apply to it at all (D20.1).
        check_main(ck, m);
    }
    ck->module = NULL;
    ck->module_sym = NULL;
    return ck->errors == before;
}

bool check_program(check_t* ck, const module_set_t* set) {
    bool ok = true;
    // Every module of the closure in the pass order of modules.h: the
    // dependency order first, so an imported module is complete before its
    // importer reads it (D9.10), then a module the walk did not finish -- one
    // whose own import failed, or an importer of a file that did not parse --
    // which is still checked when it parsed itself, so that its own errors
    // are reported and not only its import's (D14.2, D20.1). The index walk
    // reads the same order, which is why neither builds one of its own.
    for (uint64_t i = 0; i < module_set_pass_count(set); i++) {
        if (!check_module(ck, module_set_pass_at(set, i))) {
            ok = false;
        }
    }
    return ok;
}
