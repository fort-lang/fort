// The data half of the LLVM IR emitter (toolchain.md 6 items 5, 8 and 21):
// the private data, the module's declarations, the attribute groups and the
// assembly of the sections into one module; see gen.h.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "consts.h"
#include "containers.h"
#include "diag.h"
#include "gen.h"
#include "lexer.h"
#include "runtime_sig.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// The module header of item 1: clang's normalized spelling of the triple, and
// no datalayout, module flags, comments or source_filename (D19.1).
static const char MODULE_HEADER[] = "target triple = \"x86_64-unknown-linux-gnu\"\n";

// The program entry point the compiler emits in the entry module (D11.6): the
// one C name a fort program's own definitions occupy.
static const char ENTRY_NAME[] = "fort_entry";

// The overflow intrinsics of item 15, in the order `sadd ssub smul uadd usub
// umul` and, within each, the widths `i8 i16 i32 i64`.
static const char* const OVERFLOW_OP[GEN_OVF_COUNT] = {
    "sadd",
    "ssub",
    "smul",
    "uadd",
    "usub",
    "umul",
};
static const char* const OVERFLOW_WIDTH[GEN_OVF_WIDTHS] = {"i8", "i16", "i32", "i64"};

// The attribute groups of toolchain.md 6, at their fixed indices; only the
// used ones are emitted (D19.5). `mustprogress` is deliberately absent
// everywhere: it would license the optimizer to delete a `while (true) { }`,
// which fort keeps running (D8.4).
static const char* const ATTR_TEXT[ATTR_COUNT] = {
    "nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\"",
    "noreturn nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\"",
    // `#2` is never emitted: it held `cold noreturn nounwind` on the C
    // runtime's declarations, which item 8 no longer produces. The index stays
    // so that `#3` to `#7` keep the numbers item 14 fixes for them (D19.5).
    "cold noreturn nounwind",
    "nobuiltin",
    "nocallback nofree nosync nounwind speculatable willreturn memory(none)",
    "nocallback nofree nounwind willreturn memory(argmem: readwrite)",
    "nocallback nofree nounwind willreturn memory(argmem: write)",
    "cold noreturn nounwind memory(inaccessiblemem: write)",
    "cold noreturn nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\"",
};

// The first byte that needs no `\XX` escape and the last: a string constant
// writes every other byte as a hex pair (item 5).
enum { PRINTABLE_FIRST = 0x20, PRINTABLE_LAST = 0x7E, HEX_DIGITS = 16 };

// ---- private data (item 5) --------------------------------------------------------

// One record of the private data: a file name, a string literal or an enum
// table, each numbered on first use (D19.5).
typedef struct {
    str_t text;       // the bytes of a file name or a string literal
    const sym_t* sym; // an enum table's declaration
    uint64_t first;   // its first member-name string
    uint64_t count;   // its member count
} gen_record_t;

static gen_record_t* record_new(str_t text) {
    gen_record_t* r = mem_alloc((uint64_t)sizeof(gen_record_t));
    r->text = text;
    r->sym = NULL;
    r->first = 0;
    r->count = 0;
    return r;
}

// `@.<kind>.<n>`, the name of a private datum.
static str_t data_name(gen_t* g, const char* kind, uint64_t n) {
    sb_clear(&g->scratch);
    sb_append(&g->scratch, "@.");
    sb_append(&g->scratch, kind);
    sb_push(&g->scratch, '.');
    sb_append_u64(&g->scratch, n);
    return gen_take(g);
}

str_t gen_file_ref(gen_t* g, loc_t loc) {
    const str_t file = str_from_cstr(loc.file);
    for (uint64_t i = 0; i < g->files.len; i++) {
        const gen_record_t* r = (const gen_record_t*)g->files.items[i];
        if (str_eq(r->text, file)) {
            return data_name(g, "file", i);
        }
    }
    // `@.file.N` holds the path exactly as the compiler opened it (D19.5).
    ptrvec_push(&g->files, record_new(str_pool_intern(&g->pool, file)));
    return data_name(g, "file", g->files.len - 1);
}

str_t gen_str_ref(gen_t* g, str_t s) {
    // String constants are assigned on first use and never deduplicated by
    // content (D19.5).
    ptrvec_push(&g->strs, record_new(str_pool_intern(&g->pool, s)));
    return data_name(g, "str", g->strs.len - 1);
}

uint64_t gen_enum_count(const sym_t* e) {
    if (e == NULL || e->node == NULL) {
        return 0;
    }
    uint64_t n = 0;
    for (uint64_t i = 0; i < ast_len(e->node); i++) {
        if (ast_child(e->node, i)->kind == AST_ENUM_MEMBER) {
            n++;
        }
    }
    return n;
}

// `@.enum.<path.name>`, the table's name (item 21).
static str_t enum_name(gen_t* g, const sym_t* e) {
    // The dotted name is built first: gen_symbol builds its own text in the
    // same scratch buffer.
    const str_t dotted = gen_symbol(g, e);
    sb_clear(&g->scratch);
    sb_push(&g->scratch, '@');
    gen_append_name(&g->scratch, ".enum.", dotted, false);
    return gen_take(g);
}

str_t gen_enum_ref(gen_t* g, const sym_t* e) {
    for (uint64_t i = 0; i < g->enums.len; i++) {
        if (((const gen_record_t*)g->enums.items[i])->sym == e) {
            return enum_name(g, e);
        }
    }
    // A table is emitted only for an enum some `print` of that type reaches
    // (item 21); its member names are string constants of their own.
    gen_record_t* r = record_new(str_from_range(NULL, 0));
    r->sym = e;
    r->first = g->strs.len;
    r->count = 0;
    for (uint64_t i = 0; e->node != NULL && i < ast_len(e->node); i++) {
        const ast_node_t* m = ast_child(e->node, i);
        if (m->kind != AST_ENUM_MEMBER) {
            continue;
        }
        (void)gen_str_ref(g, m->name);
        r->count++;
    }
    ptrvec_push(&g->enums, r);
    return enum_name(g, e);
}

// The hex digits of a `\XX` escape, which a string constant and a quoted name
// both write (item 5).
static const char HEX[] = "0123456789ABCDEF";

// Appends `\XX` for `byte`.
static void append_hex(sb_t* out, unsigned char byte) {
    sb_push(out, '\\');
    sb_push(out, HEX[byte / HEX_DIGITS]);
    sb_push(out, HEX[byte % HEX_DIGITS]);
}

// Whether `byte` may stand in an IR name with no quotes around it: LLVM's
// unquoted identifiers are `[-a-zA-Z$._][-a-zA-Z$._0-9]*`, which every module
// path satisfies, since its segments are identifiers joined with dots (D9.1).
static bool name_byte_is_plain(unsigned char byte, bool first) {
    if (byte == '-' || byte == '$' || byte == '.' || byte == '_') {
        return true;
    }
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')) {
        return true;
    }
    return !first && byte >= '0' && byte <= '9';
}

// Whether `prefix` followed by `name` needs quotes around it.
static bool name_needs_quotes(const char* prefix, str_t name) {
    uint64_t at = 0;
    for (const char* p = prefix; *p != '\0'; p++) {
        if (!name_byte_is_plain((unsigned char)*p, at == 0)) {
            return true;
        }
        at++;
    }
    for (uint64_t i = 0; i < name.len; i++) {
        if (!name_byte_is_plain((unsigned char)name.ptr[i], at == 0)) {
            return true;
        }
        at++;
    }
    return at == 0;
}

void gen_append_name(sb_t* out, const char* prefix, str_t name, bool always) {
    if (!always && !name_needs_quotes(prefix, name)) {
        sb_append(out, prefix);
        sb_append_str(out, name);
        return;
    }
    sb_push(out, '"');
    sb_append(out, prefix);
    for (uint64_t i = 0; i < name.len; i++) {
        const unsigned char byte = (unsigned char)name.ptr[i];
        // The two bytes a quoted name cannot hold, and every byte outside the
        // printable range, are written as the `\XX` hex pair of item 5, which
        // LLVM reads back to the byte: the ELF symbol is the name itself
        // (D9.7).
        if (byte < PRINTABLE_FIRST || byte > PRINTABLE_LAST || byte == '"' || byte == '\\') {
            append_hex(out, byte);
            continue;
        }
        sb_push(out, (char)byte);
    }
    sb_push(out, '"');
}

// Appends `c"..."` with the trailing NUL that `len` excludes (D3.7) and every
// byte outside the printable range written as a `\XX` hex pair (item 5).
static void append_bytes(sb_t* out, str_t s) {
    sb_append(out, "c\"");
    for (uint64_t i = 0; i < s.len; i++) {
        const unsigned char byte = (unsigned char)s.ptr[i];
        if (byte < PRINTABLE_FIRST || byte > PRINTABLE_LAST || byte == '"' || byte == '\\') {
            append_hex(out, byte);
            continue;
        }
        sb_push(out, (char)byte);
    }
    sb_append(out, "\\00\"");
}

// `@.x.N = private unnamed_addr constant [len + 1 x i8] c"...", align 1`:
// private data marks `unnamed_addr`, a named fort constant never does (item
// 5).
static void emit_bytes(sb_t* out, str_t name, str_t bytes) {
    sb_append_str(out, name);
    sb_append(out, " = private unnamed_addr constant [");
    sb_append_u64(out, bytes.len + 1);
    sb_append(out, " x i8] ");
    append_bytes(out, bytes);
    sb_append(out, ", align 1\n");
}

// The private data of item 5: `@.file.N` before `@.str.N` before `@.enum.*`
// (D19.5).
static void emit_data(gen_t* g, sb_t* out) {
    for (uint64_t i = 0; i < g->files.len; i++) {
        const gen_record_t* r = (const gen_record_t*)g->files.items[i];
        emit_bytes(out, data_name(g, "file", i), r->text);
    }
    for (uint64_t i = 0; i < g->strs.len; i++) {
        const gen_record_t* r = (const gen_record_t*)g->strs.items[i];
        emit_bytes(out, data_name(g, "str", i), r->text);
    }
    for (uint64_t i = 0; i < g->enums.len; i++) {
        const gen_record_t* r = (const gen_record_t*)g->enums.items[i];
        sb_append_str(out, enum_name(g, r->sym));
        sb_append(out, " = private unnamed_addr constant [");
        sb_append_u64(out, r->count);
        sb_append(out, " x %fort.enum_member] [");
        uint64_t at = 0;
        for (uint64_t k = 0; r->sym->node != NULL && k < ast_len(r->sym->node); k++) {
            const ast_node_t* m = ast_child(r->sym->node, k);
            if (m->kind != AST_ENUM_MEMBER) {
                continue;
            }
            if (at > 0) {
                sb_append(out, ", ");
            }
            // One entry per member in declaration order (item 21).
            sb_append(out, "%fort.enum_member { i32 ");
            sb_append_i64(out, (int64_t)cv_bits(check_node_value(g->ck, m)));
            sb_append(out, ", ptr ");
            sb_append_str(out, data_name(g, "str", r->first + at));
            sb_append(out, " }");
            at++;
        }
        // `%fort.enum_member` has C's 16-byte layout, so the table matches
        // std.rt's struct enum_member (item 21).
        sb_append(out, "], align 8\n");
    }
}

// ---- module-level data (item 5, D7.10) --------------------------------------------

// The value of a module-level initializer, appended to `out`, and whether
// every byte of it is zero, which is what `zeroinitializer` spells (item 5).
static bool const_value(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n);

// The zero constant of `t` as an initializer spells it: `zeroinitializer` for
// an aggregate, `null` for a pointer and `0` for every other scalar (item 5).
static void const_zero(sb_t* out, const type_t* t) {
    if (gen_is_aggregate(t)) {
        sb_append(out, "zeroinitializer");
        return;
    }
    if (t != NULL && (t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR || t->kind == TYPE_FN)) {
        sb_append(out, "null");
        return;
    }
    sb_push(out, '0');
}

// `<memtype> <value>`: one element or field of a constant aggregate, whose
// type is written before every member (item 5). A NULL node is a field the
// designated form omitted, which is zeroed (D6.5).
static void const_member(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n, bool* zero) {
    // The member's type is appended before its value is built, since both
    // texts come out of the emitter's one scratch buffer.
    const str_t ty = gen_mem_type(g, t);
    sb_append_str(out, ty);
    sb_push(out, ' ');
    if (n == NULL) {
        const_zero(out, t);
        return;
    }
    if (!const_value(g, out, t, n)) {
        *zero = false;
    }
}

// The member of a designated struct literal that initializes the field at
// index `at`, or NULL when it omits it (D6.5). It places every designator
// through gen_field_index, the one map from a field symbol to its index, so a
// designator the emitter cannot place ends the compilation instead of leaving
// a zeroed field in read-only memory: an unfinished path is a diagnostic,
// never wrong data.
static const ast_node_t* designated_value(gen_t* g, const ast_node_t* lit, uint64_t at) {
    for (uint64_t i = 0; i < ast_len(lit); i++) {
        const ast_node_t* d = ast_child(lit, i);
        uint64_t k = 0;
        if (d->kind != AST_DESIGNATOR || !gen_field_index(d->sym, &k)) {
            gen_todo(g, d->loc, "this initializer");
            return NULL;
        }
        if (k == at) {
            return d->a;
        }
    }
    return NULL;
}

// A constant struct: its fields in declaration order, the order the named
// type of gen.c writes them in, with an omitted field zeroed (D6.5, item 5).
// This is the fourth walk over the AST_FIELD_DECL children of a declaration;
// gen.c's gen_struct_type names the other three and why all four must agree.
static bool const_struct(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n) {
    const sym_t* s = (const sym_t*)t->decl;
    if (s == NULL || s->node == NULL) {
        gen_todo(g, n->loc, "this initializer");
        return true;
    }
    const bool designated = (n->flags & AST_FLAG_DESIGNATED) != 0;
    sb_t buf;
    sb_init(&buf);
    bool zero = true;
    uint64_t at = 0;
    sb_append(&buf, "{ ");
    for (uint64_t i = 0; i < ast_len(s->node); i++) {
        const ast_node_t* f = ast_child(s->node, i);
        if (f->kind != AST_FIELD_DECL) {
            continue;
        }
        if (at > 0) {
            sb_append(&buf, ", ");
        }
        const ast_node_t* v = NULL;
        if (designated) {
            v = designated_value(g, n, at);
        } else if (at < ast_len(n)) {
            v = ast_child(n, at);
        }
        const_member(g, &buf, f->type, v, &zero);
        if (g->failed) {
            // One report per initializer: the walk stops at the first member
            // the emitter cannot lower.
            break;
        }
        at++;
    }
    sb_append(&buf, " }");
    // An all-zero aggregate is `zeroinitializer` whatever its shape (item 5).
    sb_append_str(out, zero ? str_from_cstr("zeroinitializer") : sb_view(&buf));
    sb_free(&buf);
    return zero;
}

// A constant fixed array: exactly `N` elements or `{}` (D6.5, item 5).
static bool const_array(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n) {
    sb_t buf;
    sb_init(&buf);
    bool zero = true;
    sb_push(&buf, '[');
    for (uint64_t i = 0; i < ast_len(n); i++) {
        if (i > 0) {
            sb_append(&buf, ", ");
        }
        const_member(g, &buf, t->elem, ast_child(n, i), &zero);
    }
    sb_push(&buf, ']');
    sb_append_str(out, zero ? str_from_cstr("zeroinitializer") : sb_view(&buf));
    sb_free(&buf);
    return zero;
}

// A constant `string` or span: its two header fields, the bytes and the
// length the trailing NUL excludes (D3.7, item 5). The pointer is a
// relocation, so the value is never all-zero.
static bool const_string(gen_t* g, sb_t* out, str_t bytes) {
    const str_t ref = gen_str_ref(g, bytes);
    sb_append(out, "{ ptr ");
    sb_append_str(out, ref);
    sb_append(out, ", i64 ");
    sb_append_u64(out, bytes.len);
    sb_append(out, " }");
    return false;
}

// The address of a module-level declaration, of a function, or the value of
// another module-level declaration: the three forms D7.10 adds to the
// constant expressions of D4.6. Returns false when `n` is none of them.
static bool const_symbol(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n, bool* zero) {
    const sym_t* s = n->sym;
    if (s == NULL) {
        return false;
    }
    if (s->kind == SYM_FN) {
        // A function name is a module-level initializer and its address is a
        // relocation (D3.10, D7.10).
        sb_append_str(out, gen_symbol_ref(g, s));
        *zero = false;
        return true;
    }
    if (s->kind == SYM_CONST && s->node != NULL && s->node->b != NULL) {
        // A constant that names another one holds that one's value; the
        // checker resolved the chain lazily and refused a cycle (D4.6, D7.10).
        // Only an immutable declaration: a read of a `mut` global is not a
        // constant expression, so one reaching here is refused rather than
        // frozen into another constant (D4.6).
        *zero = const_value(g, out, t, s->node->b);
        return true;
    }
    return false;
}

static bool const_value(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n) {
    if (g->failed || t == NULL || n == NULL) {
        const_zero(out, t);
        return true;
    }
    if ((n->ann & CHECK_ANN_CONST) != 0) {
        const cval_t v = check_node_value(g->ck, n);
        if (v.kind == CV_STR) {
            return const_string(g, out, v.str);
        }
        if (v.kind != CV_NONE) {
            // Every folded constant is emitted as its literal, whatever node
            // it stands on (D4.6).
            const gen_val_t c = gen_const_mem_value(g, t, v);
            sb_append_str(out, c.val);
            return v.kind == CV_NULL || v.mag == 0;
        }
    }
    switch (n->kind) {
    case AST_BRACE_INIT:
        if (ast_len(n) == 0) {
            // `{}` zeroes any aggregate, span or string (D6.5).
            const_zero(out, t);
            return true;
        }
        if (t->kind == TYPE_ARRAY) {
            return const_array(g, out, t, n);
        }
        if (t->kind == TYPE_STRUCT) {
            return const_struct(g, out, t, n);
        }
        break;
    case AST_STRUCT_LIT:
    case AST_ARRAY_LIT:
        // A typed literal carries its brace list in `b` (D6.5).
        return const_value(g, out, t, n->b);
    case AST_IDENT:
    case AST_FIELD: {
        bool zero = true;
        if (const_symbol(g, out, t, n, &zero)) {
            return zero;
        }
        break;
    }
    case AST_UNARY:
        if (n->op == TOK_AMP && n->a != NULL && n->a->sym != NULL &&
            (n->a->sym->kind == SYM_CONST || n->a->sym->kind == SYM_GLOBAL)) {
            // `&` of a module-level declaration from any module, and of
            // nothing else, is an initializer (D7.10).
            sb_append_str(out, gen_symbol_ref(g, n->a->sym));
            return false;
        }
        break;
    default:
        break;
    }
    gen_todo(g, n->loc, "this initializer");
    const_zero(out, t);
    return true;
}

void gen_global(gen_t* g, const ast_node_t* decl) {
    const sym_t* s = decl->sym;
    if (s == NULL || s->error || s->type == NULL) {
        // A declaration that failed to check is silent here: the checker
        // reported it (D14.2).
        return;
    }
    if (s->kind != SYM_CONST && s->kind != SYM_GLOBAL) {
        return;
    }
    // Name, type and value each pass through the one scratch buffer, so each
    // is finished before the next begins.
    const str_t name = gen_symbol_ref(g, s);
    const str_t ty = gen_mem_type(g, s->type);
    sb_t init;
    sb_init(&init);
    (void)const_value(g, &init, s->type, decl->b);
    sb_append_str(&g->globals, name);
    // An immutable declaration lives in read-only memory and a `mut` one in
    // writable memory (D7.10); no section is named, since LLVM picks it from
    // the initializer (item 5). A named fort constant is not `unnamed_addr`:
    // its address is significant, because `&CONST` is expressible (D6.7).
    sb_append(&g->globals,
              s->kind == SYM_GLOBAL ? " = dso_local global " : " = dso_local constant ");
    sb_append_str(&g->globals, ty);
    sb_push(&g->globals, ' ');
    sb_append_str(&g->globals, sb_view(&init));
    // Every global carries an explicit `align N` from the compiler's own
    // layout (D3.1, D3.8, item 5).
    sb_append(&g->globals, ", align ");
    sb_append_u64(&g->globals, type_alignof(s->type));
    sb_push(&g->globals, '\n');
    sb_free(&init);
}

// ---- declarations (item 8) --------------------------------------------------------

void gen_use_extern(gen_t* g, const sym_t* s) {
    const str_t name = s->name;
    for (uint64_t i = 0; i < g->externs.len; i++) {
        // A symbol is declared exactly once (item 8), and what the linker
        // sees is the C name: two modules of one program may each declare the
        // same function, which is two sym_t and one ELF symbol.
        if (str_eq(((const sym_t*)g->externs.items[i])->name, name)) {
            return;
        }
    }
    ptrvec_push(&g->externs, (void*)s);
}

// `declare <ret> @name(<params>, ...)`: an `extern` function is declared with
// its C types, unmangled, and with a variadic tail, which is what makes a
// fixed-prototype declaration of a variadic C function safe (item 8, D9.8).
static void emit_extern(gen_t* g, sb_t* out, const sym_t* s) {
    const type_t* sig = s->type;
    sb_append(out, "declare ");
    const char* ret_attr = gen_ext_attr(sig->elem);
    if (ret_attr != NULL) {
        sb_append(out, ret_attr);
        sb_push(out, ' ');
    }
    sb_append_str(out, gen_value_type(g, sig->elem));
    sb_append(out, " @");
    // Through the one spelling of item 4, like every other `@` name: an
    // `extern` name is a fort identifier and comes out bare, and no site of
    // the emitter has a rule of its own (D9.7).
    gen_append_name(out, "", s->name, false);
    sb_push(out, '(');
    for (uint32_t i = 0; i < sig->nparams; i++) {
        if (i > 0) {
            sb_append(out, ", ");
        }
        sb_append_str(out, gen_value_type(g, sig->params[i]));
        const char* attr = gen_ext_attr(sig->params[i]);
        if (attr != NULL) {
            sb_push(out, ' ');
            sb_append(out, attr);
        }
    }
    if (sig->nparams > 0) {
        sb_append(out, ", ");
    }
    sb_append(out, "...)\n");
}

// The intrinsics, with the spellings LLVM 18 prints, in the fixed order of
// item 8's table; the parameter attributes shown are part of the spelling.
static void emit_intrinsic(sb_t* out, uint64_t which) {
    if (which == (uint64_t)IN_MEMCPY) {
        sb_append(out,
                  "declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly, "
                  "ptr noalias nocapture readonly, i64, i1 immarg) #5\n");
        return;
    }
    if (which == (uint64_t)IN_MEMSET) {
        sb_append(out,
                  "declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, "
                  "i1 immarg) #6\n");
        return;
    }
    if (which == (uint64_t)IN_TRAP) {
        sb_append(out, "declare void @llvm.trap() #7\n");
        return;
    }
    const uint64_t at = which - (uint64_t)IN_OVERFLOW_FIRST;
    const char* op = OVERFLOW_OP[at / (uint64_t)GEN_OVF_WIDTHS];
    const char* width = OVERFLOW_WIDTH[at % (uint64_t)GEN_OVF_WIDTHS];
    sb_append(out, "declare { ");
    sb_append(out, width);
    sb_append(out, ", i1 } @llvm.");
    sb_append(out, op);
    sb_append(out, ".with.overflow.");
    sb_append(out, width);
    sb_push(out, '(');
    sb_append(out, width);
    sb_append(out, ", ");
    sb_append(out, width);
    sb_append(out, ") #4\n");
}

// ---- calls to the runtime (item 8) ------------------------------------------------

// Appends `@"std.rt.x"(<args>)`. A call into the runtime is an ordinary
// fort-to-fort call by the mangled name of D9.7, quoted like every other
// dotted name, and no attribute stands on it (items 4, 8, 14).
static void call_rt_tail(gen_t* g, rt_entry_t rt, const gen_args_t* args) {
    gen_text_append(g, "@");
    gen_append_name(&g->body, "", str_from_cstr(rt_entry_name(rt)), true);
    gen_text_append(g, "(");
    gen_text_append_str(g, sb_view(&args->text));
    gen_text_append(g, ")");
    gen_ins_end(g);
}

void gen_call_rt(gen_t* g, rt_entry_t rt, const gen_args_t* args) {
    gen_ins(g);
    gen_text_append(g, "call ");
    gen_text_append(g, ir_result_text(rt_entry_result(rt)));
    gen_text_append(g, " ");
    call_rt_tail(g, rt, args);
}

gen_val_t gen_call_rt_value(gen_t* g, rt_entry_t rt, str_t ret, const gen_args_t* args) {
    const gen_val_t r = gen_temp(g, ret);
    gen_text_append(g, "call ");
    gen_text_append(g, ir_result_text(rt_entry_result(rt)));
    gen_text_append(g, " ");
    call_rt_tail(g, rt, args);
    return r;
}

// ---- assembly (item 1) ------------------------------------------------------------

// Appends one section, separated from the one before it by a blank line
// (item 1); an empty section is skipped, so the sections a module does not
// need leave no trace.
static void section(sb_t* out, str_t text) {
    if (text.len == 0) {
        return;
    }
    if (out->len > 0) {
        sb_push(out, '\n');
    }
    sb_append_str(out, text);
}

void gen_finish(gen_t* g) {
    sb_t data;
    sb_init(&data);
    emit_data(g, &data);
    sb_t externs;
    sb_init(&externs);
    // `extern` C functions in first-use order (D19.5), except one naming a
    // symbol the module defines: one ELF symbol is one IR entity, so a
    // `declare` beside a `define` of `fort_entry` would be a redefinition
    // (item 8). The checker refuses such a declaration first, since the name
    // is reserved (D9.7), so this only keeps the invariant local to the
    // emitter.
    for (uint64_t i = 0; i < g->externs.len; i++) {
        const sym_t* s = (const sym_t*)g->externs.items[i];
        if (g->entry_defined && str_eq(s->name, str_from_cstr(ENTRY_NAME))) {
            continue;
        }
        emit_extern(g, &externs, s);
    }
    sb_t intrinsics;
    sb_init(&intrinsics);
    // Then the intrinsics in the fixed table order.
    for (uint64_t i = 0; i < (uint64_t)IN_COUNT; i++) {
        if (g->intrinsics[i]) {
            emit_intrinsic(&intrinsics, i);
        }
    }
    sb_t attributes;
    sb_init(&attributes);
    for (uint64_t i = 0; i < (uint64_t)ATTR_COUNT; i++) {
        if (!g->attrs[i]) {
            continue;
        }
        sb_append(&attributes, "attributes #");
        sb_append_u64(&attributes, i);
        sb_append(&attributes, " = { ");
        sb_append(&attributes, ATTR_TEXT[i]);
        sb_append(&attributes, " }\n");
    }
    sb_clear(&g->out);
    section(&g->out, str_from_cstr(MODULE_HEADER));
    section(&g->out, sb_view(&g->named));
    section(&g->out, sb_view(&g->globals));
    section(&g->out, sb_view(&g->funcs));
    section(&g->out, sb_view(&data));
    section(&g->out, sb_view(&externs));
    section(&g->out, sb_view(&intrinsics));
    section(&g->out, sb_view(&attributes));
    sb_free(&data);
    sb_free(&externs);
    sb_free(&intrinsics);
    sb_free(&attributes);
}
