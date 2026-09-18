// Module resolution: the search roots, the two import readings, the import
// closure and its order; see modules.h.
#include "modules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ast.h"
#include "containers.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "scope.h"
#include "str.h"

// The real file path, which is a module's identity. POSIX declares realpath in
// <stdlib.h>. glibc hides it behind __USE_XOPEN_EXTENDED, which these C11
// options do not set. This file therefore declares it, as driver.c declares
// `environ`.
extern char* realpath(const char* name, char* resolved);

// The extension of a fort source file; a file with any other extension is
// never a module.
static const char MODULE_SUFFIX[] = ".ft";

// The separator of module paths and the one of file paths.
enum { PATH_SEPARATOR = '.' };
enum { DIRECTORY_SEPARATOR = '/' };

// The first segment reserved for the standard library. A path that starts with
// it uses only the standard library directory. No other path uses that directory.
static const char STD_SEGMENT[] = "std";

// Each closure holds the runtime. The compiler loads it as a root beside the
// entry file for builds and `--check`. It is parsed, checked, and emitted like
// other modules. Membership does not bind the
// name: a module that wants to call it writes `import std.rt;` like any other
// importer.
static const char RUNTIME_PATH[] = "std.rt";
static const char RUNTIME_FILE[] = "rt.ft";

// Bytes read from a source file per call.
enum { READ_CHUNK = 4096 };

// ---- paths ---------------------------------------------------------------------------

// The scratch buffer's contents, interned in the set's pool and
// NUL-terminated, so that a loc_t may borrow them; the buffer is empty
// afterwards. Every string a message quotes is interned this way before the
// message is built, since the message uses the same buffer.
static str_t take(module_set_t* set, sb_t* b) {
    const str_t s = str_pool_intern(&set->pool, sb_view(b));
    sb_clear(b);
    return s;
}

// Returns the path directory without its trailing separator. The entry file
// directory is the first search root. A path without a directory returns an
// empty view.
static str_t directory_of(str_t path) {
    uint64_t len = 0;
    bool separated = false;
    for (uint64_t i = 0; i < path.len; i++) {
        if (path.ptr[i] == DIRECTORY_SEPARATOR) {
            len = i;
            separated = true;
        }
    }
    if (separated && len == 0) {
        // an empty root would mean the current directory, never a root
        return str_from_range(path.ptr, 1);
    }
    return str_from_range(path.ptr, len);
}

// The last path component without its `.ft` suffix.
// For example, `src/app.ft` gives the entry module path `app`.
static str_t base_name(str_t path) {
    uint64_t start = 0;
    for (uint64_t i = 0; i < path.len; i++) {
        if (path.ptr[i] == DIRECTORY_SEPARATOR) {
            start = i + 1;
        }
    }
    str_t base = str_from_range(path.ptr + start, path.len - start);
    const str_t suffix = str_from_cstr(MODULE_SUFFIX);
    if (base.len > suffix.len) {
        const str_t tail = str_from_range(base.ptr + base.len - suffix.len, suffix.len);
        if (str_eq(tail, suffix)) {
            base = str_from_range(base.ptr, base.len - suffix.len);
        }
    }
    return base;
}

// Whether `base` contains the `.` that spells a module path. Symbol mangling
// reads that character. All other characters enter the symbol unchanged and
// cannot spell a path.
static bool holds_path_separator(str_t base) {
    for (uint64_t i = 0; i < base.len; i++) {
        if (base.ptr[i] == PATH_SEPARATOR) {
            return true;
        }
    }
    return false;
}

// The `i`-th segment of an import path: the path node holds one identifier per
// segment, in source order.
static str_t segment_at(const ast_node_t* path, uint64_t i) {
    return ast_child(path, i)->name;
}

// The first `n` segments joined with `.`, the spelling of a module path in a
// diagnostic.
static str_t path_text(module_set_t* set, const ast_node_t* path, uint64_t n) {
    sb_t* b = &set->msg;
    sb_clear(b);
    for (uint64_t i = 0; i < n; i++) {
        if (i > 0) {
            sb_push(b, PATH_SEPARATOR);
        }
        sb_append_str(b, segment_at(path, i));
    }
    return take(set, b);
}

// `<dir>/<segments from..n joined with '/'>.ft` in `out`: the file the segments
// name under one root, `/` standing for the `.` of the module path. A root of
// zero length, which the entry file's directory is when the entry names no
// directory, contributes no separator. The result is a
// NUL-terminated view of `out`, valid until the next call on it, so a path the
// loader only probes is never interned.
static str_t file_of(sb_t* out, str_t dir, const ast_node_t* path, uint64_t from, uint64_t n) {
    sb_clear(out);
    if (dir.len > 0) {
        sb_append_str(out, dir);
        if (dir.ptr[dir.len - 1] != DIRECTORY_SEPARATOR) {
            sb_push(out, DIRECTORY_SEPARATOR);
        }
    }
    for (uint64_t i = from; i < n; i++) {
        if (i > from) {
            sb_push(out, DIRECTORY_SEPARATOR);
        }
        sb_append_str(out, segment_at(path, i));
    }
    sb_append(out, MODULE_SUFFIX);
    return str_from_range(sb_cstr(out), out->len);
}

// Whether the path begins with the reserved segment `std`.
static bool is_std_path(const ast_node_t* path, uint64_t n) {
    return n > 0 && str_eq(segment_at(path, 0), str_from_cstr(STD_SEGMENT));
}

// The number of files the first `n` segments may name, one per search root. A
// A path that starts with `std` has one candidate in the standard library
// directory. `std` names that directory, so `import std;` names no file. Other
// paths use the entry directory and `-I` roots. They never use the standard
// library directory.
static uint64_t candidate_count(const module_set_t* set, const ast_node_t* path, uint64_t n) {
    if (n == 0) {
        return 0;
    }
    if (is_std_path(path, n)) {
        return (n >= 2 && set->std_dir.len > 0) ? 1U : 0U;
    }
    return mem_add(1U, set->roots.len);
}

// The `i`-th candidate file of the first `n` segments, the roots in order,
// built into `out`.
static str_t candidate_at(
    const module_set_t* set, sb_t* out, const ast_node_t* path, uint64_t n, uint64_t i) {
    if (is_std_path(path, n)) {
        // `std.io` is `<std>/io.ft`
        return file_of(out, set->std_dir, path, 1, n);
    }
    if (i == 0) {
        return file_of(out, set->entry_dir, path, 0, n);
    }
    const str_t root = str_from_cstr((const char*)set->roots.items[i - 1]);
    return file_of(out, root, path, 0, n);
}

// ---- the file system -------------------------------------------------------------------

// Whether `path` names a readable file.
static bool file_exists(str_t path) {
    return access(path.ptr, R_OK) == 0;
}

// Returns the first root that holds the file for the first `n` segments.
// Returns an empty view when no root holds it. The first matching root wins.
static str_t find_file(module_set_t* set, const ast_node_t* path, uint64_t n) {
    const uint64_t count = candidate_count(set, path, n);
    sb_t b;
    sb_init(&b);
    str_t found = str_from_range(NULL, 0);
    for (uint64_t i = 0; i < count && found.ptr == NULL; i++) {
        const str_t file = candidate_at(set, &b, path, n, i);
        if (file_exists(file)) {
            // Only the file that is there is interned: the loader keeps no
            // copy of a path it merely looked at.
            found = str_pool_intern(&set->pool, file);
        }
    }
    sb_free(&b);
    return found;
}

// The whole file, interned in the set's pool; `*ok` is false when it could
// not be read. The tree's names point into these bytes, so they must live
// until module_set_free.
static str_t read_source(module_set_t* set, str_t path, bool* ok) {
    *ok = false;
    FILE* file = fopen(path.ptr, "rb");
    if (file == NULL) {
        return str_from_range(NULL, 0);
    }
    sb_t b;
    sb_init(&b);
    for (;;) {
        sb_reserve(&b, READ_CHUNK);
        const size_t got = fread(b.data + b.len, 1, READ_CHUNK, file);
        b.len += (uint64_t)got;
        if (got < READ_CHUNK) {
            break;
        }
    }
    *ok = ferror(file) == 0;
    (void)fclose(file);
    const str_t source = str_pool_intern(&set->pool, sb_view(&b));
    sb_free(&b);
    return source;
}

// The real path of a file, symbolic links and `..` resolved: a module's
// identity. A path realpath cannot resolve stands for itself, so a file that
// vanished between the probe and here is still one module.
static str_t real_path(module_set_t* set, str_t path) {
    char* resolved = realpath(path.ptr, NULL);
    if (resolved == NULL) {
        return str_pool_intern(&set->pool, path);
    }
    const str_t real = str_pool_intern(&set->pool, str_from_cstr(resolved));
    free(resolved);
    return real;
}

// ---- diagnostics ----------------------------------------------

// The position of an error without one in the file: 1:1.
static loc_t file_start(str_t file) {
    return loc_make(file.ptr, 1, 1);
}

// `note: looked for <path>` once per root and reading, the notes a module
// that was not found carries.
static void note_candidates(module_set_t* set, loc_t at, const ast_node_t* path, uint64_t n) {
    const uint64_t count = candidate_count(set, path, n);
    sb_t b;
    sb_init(&b);
    for (uint64_t i = 0; i < count; i++) {
        const str_t file = candidate_at(set, &b, path, n, i);
        msg_begin(&set->msg);
        msg_str(&set->msg, "looked for ");
        msg_view(&set->msg, file);
        diag_note(at, msg_end(&set->msg));
    }
    sb_free(&b);
}

// `module 'util.strings' not found`: no file exists for either reading.
static void error_not_found(module_set_t* set, loc_t at, const ast_node_t* path, uint64_t n) {
    const str_t name = path_text(set, path, n);
    msg_begin(&set->msg);
    msg_str(&set->msg, "module ");
    msg_quote(&set->msg, name);
    msg_str(&set->msg, " not found");
    diag_error(at, msg_end(&set->msg));
    note_candidates(set, at, path, n);
    if (n >= 2) {
        note_candidates(set, at, path, n - 1);
    }
}

// `module 'util' has no declaration named 'strngs'`: the symbol reading found
// the file of the prefix, which declares no such name.
static void error_no_declaration(module_set_t* set, loc_t at, str_t module, str_t name) {
    msg_begin(&set->msg);
    msg_str(&set->msg, "module ");
    msg_quote(&set->msg, module);
    msg_str(&set->msg, " has no declaration named ");
    msg_quote(&set->msg, name);
    diag_error(at, msg_end(&set->msg));
}

// `cannot import 'x': it is an import of module 'a.b'`: the import bindings of
// another module are not importable, there is no re-export.
static void error_not_exported(module_set_t* set, loc_t at, str_t module, str_t name) {
    msg_begin(&set->msg);
    msg_str(&set->msg, "cannot import ");
    msg_quote(&set->msg, name);
    msg_str(&set->msg, ": it is an import of module ");
    msg_quote(&set->msg, module);
    diag_error(at, msg_end(&set->msg));
}

// `ambiguous import 'a.b.c': a/b/c.ft and a/b.ft exist`: both readings
// succeed, whichever roots the two files live under.
static void error_ambiguous(module_set_t* set,
                            loc_t at,
                            const ast_node_t* path,
                            uint64_t n,
                            str_t module_file,
                            str_t prefix_file) {
    const str_t name = path_text(set, path, n);
    msg_begin(&set->msg);
    msg_str(&set->msg, "ambiguous import ");
    msg_quote(&set->msg, name);
    msg_str(&set->msg, ": ");
    msg_view(&set->msg, module_file);
    msg_str(&set->msg, " and ");
    msg_view(&set->msg, prefix_file);
    msg_str(&set->msg, " exist");
    diag_error(at, msg_end(&set->msg));
}

// Reports a `.` in an entry file base name. That base name is the entry module
// path. A `.` would spell another module symbol prefix and make mangling
// ambiguous.
static void error_entry_name_separator(module_set_t* set, loc_t at, str_t base) {
    char text[2];
    text[0] = PATH_SEPARATOR;
    text[1] = '\0';
    msg_begin(&set->msg);
    msg_str(&set->msg, "entry file name ");
    msg_quote(&set->msg, base);
    msg_str(&set->msg, " cannot contain '");
    msg_str(&set->msg, text);
    msg_str(&set->msg, "'");
    diag_error(at, msg_end(&set->msg));
}

// Reports one file reached through two module paths. The real file path defines
// module identity.
static void error_same_file(module_set_t* set, loc_t at, str_t path, str_t other, str_t real) {
    msg_begin(&set->msg);
    msg_str(&set->msg, "module ");
    msg_quote(&set->msg, path);
    msg_str(&set->msg, " is the same file as module ");
    msg_quote(&set->msg, other);
    diag_error(at, msg_end(&set->msg));
    msg_begin(&set->msg);
    msg_str(&set->msg, "both name ");
    msg_view(&set->msg, real);
    diag_note(at, msg_end(&set->msg));
}

// Reports a circular import at the import that closes it. The message lists the
// walk from the repeated module, then lists that module again.
static void error_cycle(module_set_t* set, loc_t at, const module_t* reached) {
    uint64_t from = 0;
    for (uint64_t i = 0; i < set->stack.len; i++) {
        if ((const module_t*)set->stack.items[i] == reached) {
            from = i;
            break;
        }
    }
    msg_begin(&set->msg);
    msg_str(&set->msg, "circular import: ");
    for (uint64_t i = from; i < set->stack.len; i++) {
        const module_t* m = (const module_t*)set->stack.items[i];
        msg_quote(&set->msg, m->path);
        msg_str(&set->msg, " imports ");
    }
    msg_quote(&set->msg, reached->path);
    diag_error(at, msg_end(&set->msg));
}

// Reports a redeclaration at the later position and notes the earlier position.
// This permits collection before import bindings are bound. Both names are declared by
// one module, so the two places have no dependency order between them and the
// rule orders them by position.
static void error_redeclaration(module_set_t* set, loc_t first, loc_t second, str_t name) {
    loc_t earlier = first;
    loc_t later = second;
    if (first.line > second.line || (first.line == second.line && first.col > second.col)) {
        earlier = second;
        later = first;
    }
    msg_begin(&set->msg);
    msg_str(&set->msg, "redeclaration of ");
    msg_quote(&set->msg, name);
    diag_error(later, msg_end(&set->msg));
    msg_begin(&set->msg);
    msg_str(&set->msg, "previous declaration of ");
    msg_quote(&set->msg, name);
    msg_str(&set->msg, " here");
    diag_note(earlier, msg_end(&set->msg));
}

// ---- the module namespace -------------------------------------------------------------

// Returns the binding kind of a top-level declaration. Returns BIND_NONE for no
// node, including an AST_ERROR from a file with syntax errors.
static bind_kind_t decl_kind(const ast_node_t* decl) {
    switch (decl->kind) {
    case AST_FN_DECL:
        // `extern fn` is importable like any other declaration
        return (decl->flags & AST_FLAG_EXTERN) != 0 ? BIND_EXTERN_FN : BIND_FN;
    case AST_STRUCT_DECL:
        return BIND_STRUCT;
    case AST_ENUM_DECL:
        return BIND_ENUM;
    case AST_VAR_DECL:
        // a constant and a `mut` global both occupy one name
        return BIND_VAR;
    default:
        return BIND_NONE;
    }
}

// Binds a name in the module namespace, reporting a collision when it is
// taken: any two entries collide whatever their kinds, import bindings
// included.
static binding_t* bind_name(module_set_t* set,
                            module_t* m,
                            str_t name,
                            bind_kind_t kind,
                            loc_t at,
                            const ast_node_t* node) {
    binding_t* bound = scope_declare(&m->names, name, kind, at, node);
    if (bound == NULL) {
        error_redeclaration(set, scope_find(&m->names, name)->loc, at, name);
    }
    return bound;
}

// Collects declarations before resolving imports. Another module can then ask
// whether this module declares a name. Enum members are not names, so only the
// top-level declarations enter. Returns false when a collision was reported;
// every collision of the module is reported first, as they are that module's own
// errors.
static bool collect_declarations(module_set_t* set, module_t* m) {
    if (m->ast == NULL) {
        return true;
    }
    bool ok = true;
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        ast_node_t* decl = ast_child(m->ast, i);
        const bind_kind_t kind = decl_kind(decl);
        if (kind == BIND_NONE) {
            continue;
        }
        if (bind_name(set, m, decl->name, kind, decl->loc, decl) == NULL) {
            ok = false;
        }
    }
    return ok;
}

// ---- loading a module --------------------------------------------

static bool resolve_imports(module_set_t* set, module_t* m);

// The module at `file` under the module path `path`, read, parsed and walked if
// it is new; NULL after a diagnostic was reported at `at`. A module already on
// the walk closes a cycle and a file already read under another path is one file
// with two identities.
static module_t* load_module(module_set_t* set, str_t path, str_t file, loc_t at, bool entry) {
    int64_t known = 0;
    if (strmap_get(&set->by_path, path, &known)) {
        module_t* found = (module_t*)set->modules.items[known];
        if (found->state == MODULE_LOADING) {
            error_cycle(set, at, found);
            return NULL;
        }
        return found;
    }
    if (set->stopped) {
        // the closure already has errors, so no further file is read
        return NULL;
    }
    bool readable = false;
    const str_t source = read_source(set, file, &readable);
    if (!readable) {
        msg_begin(&set->msg);
        msg_str(&set->msg, "cannot read ");
        msg_quote(&set->msg, file);
        diag_error(at, msg_end(&set->msg));
        return NULL;
    }
    const str_t real = real_path(set, file);
    int64_t twin = 0;
    if (strmap_get(&set->by_real, real, &twin)) {
        error_same_file(set, at, path, ((const module_t*)set->modules.items[twin])->path, real);
        return NULL;
    }
    module_t* m = mem_alloc((uint64_t)sizeof(module_t));
    m->path = path;
    m->file = file;
    m->real = real;
    m->source = source;
    m->ast = NULL;
    m->parsed = false;
    m->entry = entry;
    m->state = MODULE_LOADING;
    scope_init(&m->names, SCOPE_MODULE, NULL);
    (void)strmap_put(&set->by_path, m->path, (int64_t)set->modules.len);
    (void)strmap_put(&set->by_real, m->real, (int64_t)set->modules.len);
    ptrvec_push(&set->modules, m);

    // the diagnostic count says whether the file parsed, not NULL
    const uint64_t before = diag_count();
    tokvec_t toks;
    tokvec_init(&toks);
    // the tokens cover the file, so the parser runs either way
    (void)lex_file(m->file.ptr, m->source, &set->pool, &toks);
    m->ast = parse_module(m->file.ptr, toks.items, toks.len, &set->arena);
    tokvec_free(&toks);
    m->parsed = diag_count() == before;
    if (!m->parsed) {
        return NULL;
    }
    if (!collect_declarations(set, m)) {
        return NULL;
    }
    ptrvec_push(&set->stack, m);
    const bool resolved = resolve_imports(set, m);
    (void)ptrvec_pop(&set->stack);
    if (!resolved) {
        return NULL;
    }
    // post-order, so every import is in the order first
    m->state = MODULE_READY;
    ptrvec_push(&set->order, m);
    return m;
}

// ---- resolving one import -----------------------------------------

// Binds one declaration of another module under `as` name or its own, the
// symbol reading of an import. The import bindings of that module are not
// importable, there is no re-export.
static bool bind_symbol(module_set_t* set,
                        module_t* m,
                        const module_t* from,
                        str_t name,
                        str_t bound,
                        loc_t at,
                        const ast_node_t* node) {
    const binding_t* target = scope_find(&from->names, name);
    if (target == NULL) {
        error_no_declaration(set, at, from->path, name);
        return false;
    }
    if (!bind_is_declaration(target)) {
        error_not_exported(set, at, from->path, name);
        return false;
    }
    binding_t* b = bind_name(set, m, bound, BIND_SYMBOL, at, node);
    if (b == NULL) {
        return false;
    }
    b->to = target;
    return true;
}

// Whether the file of the prefix module declares `name`, which is what the
// symbol reading asks. A module already in the closure answers from its
// namespace. Its declarations exist before its import walk. This function reads
// and parses any other file without reports or closure changes. The module
// reading can still win, and an outside module is never loaded. A file that does not parse
// declares nothing: the error belongs to whoever imports it for real.
static bool prefix_declares(module_set_t* set, str_t path, str_t file, str_t name) {
    if (set->stopped) {
        return false;
    }
    const module_t* known = module_set_find(set, path);
    if (known != NULL) {
        const binding_t* found = scope_find(&known->names, name);
        return found != NULL && bind_is_declaration(found);
    }
    bool readable = false;
    const str_t source = read_source(set, file, &readable);
    if (!readable) {
        return false;
    }
    bool declares = false;
    ast_arena_t arena;
    ast_arena_init(&arena);
    tokvec_t toks;
    tokvec_init(&toks);
    diag_mute();
    const uint64_t before = diag_count();
    // a file that reported anything declares nothing
    (void)lex_file(file.ptr, source, &set->pool, &toks);
    const ast_node_t* probed = parse_module(file.ptr, toks.items, toks.len, &arena);
    if (probed != NULL && diag_count() == before) {
        for (uint64_t i = 0; i < ast_len(probed) && !declares; i++) {
            const ast_node_t* decl = ast_child(probed, i);
            declares = decl_kind(decl) != BIND_NONE && str_eq(decl->name, name);
        }
    }
    (void)diag_unmute();
    tokvec_free(&toks);
    ast_arena_free(&arena);
    return declares;
}

// Resolves each item in `import a.b.{s1, s2 as t};` as an independent symbol
// import from `a.b`. The prefix must name a module file. Each item must name one
// of its declarations.
static bool resolve_items(module_set_t* set, module_t* m, const ast_node_t* imp) {
    const ast_node_t* path = imp->a;
    const uint64_t n = ast_len(path);
    const str_t file = find_file(set, path, n);
    if (file.ptr == NULL) {
        error_not_found(set, imp->loc, path, n);
        return false;
    }
    const module_t* from = load_module(set, path_text(set, path, n), file, imp->loc, false);
    if (from == NULL) {
        return false;
    }
    for (uint64_t i = 0; i < ast_len(imp); i++) {
        const ast_node_t* item = ast_child(imp, i);
        const str_t bound = item->a != NULL ? item->a->name : item->name;
        if (!bind_symbol(set, m, from, item->name, bound, imp->loc, item)) {
            return false;
        }
    }
    return true;
}

// Resolves the two readings of `import a.b.c;`. A module reading finds
// `a/b/c.ft`. A symbol reading finds declaration `c` in `a/b.ft`.
// Exactly one must succeed; a one-segment path has only the module reading, since
// at most one trailing segment names a declaration.
//
// Only the module the import resolves to is loaded. When both files exist the
// prefix is probed only for the name. It does not enter the closure. Thus, an
// import resolved to `a/b/c.ft` does not inherit errors from unrelated
// `a/b.ft`. It also does not inherit its imports or extern declarations.
static bool resolve_path(module_set_t* set, module_t* m, const ast_node_t* imp) {
    const ast_node_t* path = imp->a;
    const uint64_t n = ast_len(path);
    const str_t last = segment_at(path, n - 1);
    const str_t bound = imp->b != NULL ? imp->b->name : last;
    const str_t module_file = find_file(set, path, n);
    const str_t prefix_file = n >= 2 ? find_file(set, path, n - 1) : str_from_range(NULL, 0);
    if (module_file.ptr != NULL) {
        const str_t prefix_path = n >= 2 ? path_text(set, path, n - 1) : str_from_range(NULL, 0);
        if (prefix_file.ptr != NULL && prefix_declares(set, prefix_path, prefix_file, last)) {
            error_ambiguous(set, imp->loc, path, n, module_file, prefix_file);
            return false;
        }
        const module_t* target =
            load_module(set, path_text(set, path, n), module_file, imp->loc, false);
        if (target == NULL) {
            return false;
        }
        binding_t* b = bind_name(set, m, bound, BIND_MODULE, imp->loc, imp);
        if (b == NULL) {
            return false;
        }
        b->module = target;
        return true;
    }
    if (prefix_file.ptr != NULL) {
        // The symbol reading is the only one left, so the prefix module is
        // this import's module and is read into the closure.
        const module_t* prefix =
            load_module(set, path_text(set, path, n - 1), prefix_file, imp->loc, false);
        if (prefix == NULL) {
            return false;
        }
        return bind_symbol(set, m, prefix, last, bound, imp->loc, imp);
    }
    error_not_found(set, imp->loc, path, n);
    return false;
}

// Resolves every import of the module, in source order; imports appear before
// every declaration, which the parser has already enforced.
//
// Reports each failed import in the module, so its errors stay together. Then
// `stopped` prevents remaining imports from reading more files. Processing stops
// at the module boundary.
static bool resolve_imports(module_set_t* set, module_t* m) {
    if (m->ast == NULL) {
        return true;
    }
    bool ok = true;
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        const ast_node_t* imp = ast_child(m->ast, i);
        if (imp->kind != AST_IMPORT) {
            continue;
        }
        if (!(ast_len(imp) > 0 ? resolve_items(set, m, imp) : resolve_path(set, m, imp))) {
            ok = false;
            set->stopped = true;
        }
    }
    return ok;
}

// ---- the set -----------------------------------------------------------------------------

void module_set_init(module_set_t* set) {
    ptrvec_init(&set->modules);
    ptrvec_init(&set->order);
    ptrvec_init(&set->stack);
    ptrvec_init(&set->roots);
    strmap_init(&set->by_path);
    strmap_init(&set->by_real);
    set->entry_dir = str_from_range(NULL, 0);
    set->std_dir = str_from_range(NULL, 0);
    set->stopped = false;
    str_pool_init(&set->pool);
    ast_arena_init(&set->arena);
    sb_init(&set->msg);
}

void module_set_free(module_set_t* set) {
    for (uint64_t i = 0; i < set->modules.len; i++) {
        module_t* m = (module_t*)set->modules.items[i];
        scope_free(&m->names);
        mem_free(m);
    }
    ptrvec_free(&set->modules);
    ptrvec_free(&set->order);
    ptrvec_free(&set->stack);
    ptrvec_free(&set->roots);
    strmap_free(&set->by_path);
    strmap_free(&set->by_real);
    ast_arena_free(&set->arena);
    sb_free(&set->msg);
    // The pool holds every module's source and every file name a loc_t
    // borrows, so it goes last.
    str_pool_free(&set->pool);
}

void module_set_add_root(module_set_t* set, const char* dir) {
    const str_t root = str_pool_intern(&set->pool, str_from_cstr(dir));
    ptrvec_push(&set->roots, (void*)root.ptr);
}

void module_set_std_dir(module_set_t* set, const char* dir) {
    set->std_dir = str_pool_intern(&set->pool, str_from_cstr(dir));
}

uint64_t module_set_count(const module_set_t* set) {
    return set->order.len;
}

const module_t* module_set_at(const module_set_t* set, uint64_t i) {
    if (i >= set->order.len) {
        fatal_internal("module_set_at: index out of range");
    }
    return (const module_t*)set->order.items[i];
}

bool module_set_is_ordered(const module_set_t* set, const module_t* m) {
    for (uint64_t i = 0; i < set->order.len; i++) {
        if (set->order.items[i] == m) {
            return true;
        }
    }
    return false;
}

// Whether the module belongs to the second group of the pass order: it parsed
// and the loader never ordered it.
static bool is_unordered_pass_module(const module_set_t* set, const module_t* m) {
    return m->parsed && m->ast != NULL && !module_set_is_ordered(set, m);
}

uint64_t module_set_pass_count(const module_set_t* set) {
    uint64_t n = set->order.len;
    for (uint64_t i = 0; i < set->modules.len; i++) {
        if (is_unordered_pass_module(set, (const module_t*)set->modules.items[i])) {
            n++;
        }
    }
    return n;
}

const module_t* module_set_pass_at(const module_set_t* set, uint64_t i) {
    if (i < set->order.len) {
        return (const module_t*)set->order.items[i];
    }
    // read depth first, so visited in the reverse of read order
    uint64_t seen = set->order.len;
    for (uint64_t k = set->modules.len; k > 0; k--) {
        const module_t* m = (const module_t*)set->modules.items[k - 1];
        if (!is_unordered_pass_module(set, m)) {
            continue;
        }
        if (seen == i) {
            return m;
        }
        seen++;
    }
    fatal_internal("module_set_pass_at: index out of range");
    return NULL;
}

// Every file read, whether or not its module parsed: a module is recorded
// before it is lexed, so a file with errors is listed too.
uint64_t module_set_file_count(const module_set_t* set) {
    return set->modules.len;
}

str_t module_set_file_at(const module_set_t* set, uint64_t i) {
    if (i >= set->modules.len) {
        fatal_internal("module_set_file_at: index out of range");
    }
    return ((const module_t*)set->modules.items[i])->file;
}

const module_t* module_set_find(const module_set_t* set, str_t path) {
    int64_t at = 0;
    if (!strmap_get(&set->by_path, path, &at)) {
        return NULL;
    }
    return (const module_t*)set->modules.items[at];
}

const module_t* module_set_entry(const module_set_t* set) {
    for (uint64_t i = 0; i < set->modules.len; i++) {
        const module_t* m = (const module_t*)set->modules.items[i];
        if (m->entry) {
            return m;
        }
    }
    return NULL;
}

// `<std-dir>/rt.ft`, the file of the runtime, in `out`. The runtime is
// searched under the standard library directory like any other `std` module.
static str_t runtime_file(const module_set_t* set, sb_t* out) {
    sb_clear(out);
    sb_append_str(out, set->std_dir);
    if (set->std_dir.len > 0 && set->std_dir.ptr[set->std_dir.len - 1] != DIRECTORY_SEPARATOR) {
        sb_push(out, DIRECTORY_SEPARATOR);
    }
    sb_append(out, RUNTIME_FILE);
    return str_from_range(sb_cstr(out), out->len);
}

// Loads `std.rt` before the entry file when a standard library directory is set.
// The runtime then precedes program modules in dependency order.
static bool load_runtime(module_set_t* set) {
    if (set->std_dir.len == 0) {
        return true;
    }
    sb_t b;
    sb_init(&b);
    const str_t path = str_pool_intern(&set->pool, str_from_cstr(RUNTIME_PATH));
    const str_t file = str_pool_intern(&set->pool, runtime_file(set, &b));
    sb_free(&b);
    return load_module(set, path, file, file_start(file), false) != NULL;
}

// Returns the module that the runtime closure read at `real`, or NULL. The entry
// file can be `std/rt.ft` or `std/libc.ft`. One file is one module. Thus, that
// module becomes the entry instead of getting a second identity. `error_same_file`
// would otherwise report. It keeps the path the runtime's import gave it, since a
// module path is what its importer wrote.
static module_t* entry_already_read(module_set_t* set, str_t real) {
    int64_t at = 0;
    if (!strmap_get(&set->by_real, real, &at)) {
        return NULL;
    }
    return (module_t*)set->modules.items[at];
}

bool module_set_load(module_set_t* set, const char* entry) {
    const uint64_t before = diag_count();
    const str_t file = str_pool_intern(&set->pool, str_from_cstr(entry));
    // the entry file's directory is a root, the current one never
    set->entry_dir = str_pool_intern(&set->pool, directory_of(file));
    // The entry file's module path is its base name: `src/app.ft` is the module
    // `app`. The base name need not be a path segment. The command line names
    // the entry file, not an import. Thus, `007_case.ft` is module `007_case`,
    // which no import can spell.
    const str_t base = str_pool_intern(&set->pool, base_name(file));
    // A base name cannot contain `.`, which spells a module path. `my.app.ft`
    // would be module `my.app`. Its `main` would collide with the symbol from
    // `my/app.ft`. Every other character
    // reaches the symbol verbatim, `my:app.ft` emitting `my:app.main`, which no
    // module path can spell. The error has no position in the file, so it is reported
    // at 1:1.
    if (holds_path_separator(base)) {
        error_entry_name_separator(set, file_start(file), base);
        return false;
    }
    if (!load_runtime(set)) {
        return false;
    }
    module_t* twin = entry_already_read(set, real_path(set, file));
    if (twin != NULL) {
        twin->entry = true;
        return diag_count() == before;
    }
    if (load_module(set, base, file, file_start(file), true) == NULL) {
        return false;
    }
    return diag_count() == before;
}
