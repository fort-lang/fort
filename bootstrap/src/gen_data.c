// Emits LLVM IR data, declarations, attributes, and module sections.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

// The module header: clang's normalized spelling of the triple, and
// no datalayout, module flags, comments or source_filename.
static const char MODULE_HEADER[] = "target triple = \"x86_64-unknown-linux-gnu\"\n";

// The program entry point the compiler emits in the entry module: the one C
// name a fort program's own definitions occupy.
static const char ENTRY_NAME[] = "fort_entry";

// The overflow intrinsics, in the order `sadd ssub smul uadd usub
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

// The attribute groups, at their fixed indices; only the used
// ones are emitted. `mustprogress` is deliberately absent everywhere: it would
// license the optimizer to delete a `while (true) { }`, which fort keeps running.
static const char* const ATTR_TEXT[ATTR_COUNT] = {
    "nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\"",
    "noreturn nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\"",
    // `#2` is never emitted: it held `cold noreturn nounwind` on the C runtime's
    // declarations, which LLVM IR no longer produces. The index stays so that `#3` to
    // `#7` keep the numbers LLVM IR fixes for them.
    "cold noreturn nounwind",
    "nobuiltin",
    "nocallback nofree nosync nounwind speculatable willreturn memory(none)",
    "nocallback nofree nounwind willreturn memory(argmem: readwrite)",
    "nocallback nofree nounwind willreturn memory(argmem: write)",
    "cold noreturn nounwind memory(inaccessiblemem: write)",
    "cold noreturn nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\"",
};

// The first byte that needs no `\XX` escape and the last: a string constant
// writes every other byte as a hex pair.
enum { PRINTABLE_FIRST = 0x20, PRINTABLE_LAST = 0x7E, HEX_DIGITS = 16 };

// ---- private data --------------------------------------------------------

// One record of the private data: a file name, a string literal or an enum
// table, each numbered on first use.
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
    // the path exactly as the compiler opened it
    ptrvec_push(&g->files, record_new(str_pool_intern(&g->pool, file)));
    return data_name(g, "file", g->files.len - 1);
}

str_t gen_str_ref(gen_t* g, str_t s) {
    // assigned on first use, never deduplicated by content
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

// `@.enum.<path.name>`, the table's name.
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
    // Emit a table only for an enum that a `print` call reaches.
    // Each member name is a separate string constant.
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
// both write.
static const char HEX[] = "0123456789ABCDEF";

static void append_hex(sb_t* out, unsigned char byte) {
    sb_push(out, '\\');
    sb_push(out, HEX[byte / HEX_DIGITS]);
    sb_push(out, HEX[byte % HEX_DIGITS]);
}

// Returns whether `byte` is valid in an unquoted IR name. LLVM accepts
// `[-a-zA-Z$._][-a-zA-Z$._0-9]*`. Every dotted module path matches the pattern.
static bool name_byte_is_plain(unsigned char byte, bool first) {
    if (byte == '-' || byte == '$' || byte == '.' || byte == '_') {
        return true;
    }
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')) {
        return true;
    }
    return !first && byte >= '0' && byte <= '9';
}

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
        // LLVM reads `\XX` back, so the ELF symbol is the name
        if (byte < PRINTABLE_FIRST || byte > PRINTABLE_LAST || byte == '"' || byte == '\\') {
            append_hex(out, byte);
            continue;
        }
        sb_push(out, (char)byte);
    }
    sb_push(out, '"');
}

// Appends `c"..."` with the trailing NUL that `len` excludes and every byte
// outside the printable range written as a `\XX` hex pair.
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
// private data marks `unnamed_addr`. A named fort constant does not.
static void emit_bytes(sb_t* out, str_t name, str_t bytes) {
    sb_append_str(out, name);
    sb_append(out, " = private unnamed_addr constant [");
    sb_append_u64(out, bytes.len + 1);
    sb_append(out, " x i8] ");
    append_bytes(out, bytes);
    sb_append(out, ", align 1\n");
}

// The private data: `@.file.N` before `@.str.N` before `@.enum.*`.
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
            // One entry per member in declaration order.
            sb_append(out, "%fort.enum_member { i32 ");
            sb_append_i64(out, (int64_t)cv_bits(check_node_value(g->ck, m)));
            sb_append(out, ", ptr ");
            sb_append_str(out, data_name(g, "str", r->first + at));
            sb_append(out, " }");
            at++;
        }
        // `%fort.enum_member` has C's 16-byte layout, so the table matches
        // std.rt's struct enum_member.
        sb_append(out, "], align 8\n");
    }
}

// ---- module-level data ---------------------------------------------------

// The value of a module-level initializer, appended to `out`, and whether
// every byte of it is zero, which is what `zeroinitializer` spells.
static bool const_value(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n);

// The zero constant of `t` as an initializer spells it: `zeroinitializer` for
// an aggregate, `null` for a pointer and `0` for every other scalar.
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

// `<memtype> <value>`: one element or field of a constant aggregate, whose type
// is written before every member. A NULL node is a field the designated
// form omitted, which is zeroed.
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

// The member of a designated struct literal that initializes the field at index `at`, or NULL when
// it omits it. It places each designator through gen_field_index, the only field-to-index map. An
// invalid designator ends compilation instead of leaving a zeroed read-only field. An unfinished
// path produces a diagnostic, not incorrect data.
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

// A constant struct: its fields in declaration order, the order the named type
// of gen.c writes them in, with an omitted field zeroed. This is the
// fourth walk over the AST_FIELD_DECL children of a declaration; gen.c's
// gen_struct_type names the other three and why all four must agree.
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
    // An all-zero aggregate is `zeroinitializer` whatever its shape.
    sb_append_str(out, zero ? str_from_cstr("zeroinitializer") : sb_view(&buf));
    sb_free(&buf);
    return zero;
}

// A constant fixed array: exactly `N` elements or `{}`.
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

// A constant `string` or span: its two header fields, the bytes and the length
// the trailing NUL excludes. The pointer is a relocation, so the value
// is never all-zero.
static bool const_string(gen_t* g, sb_t* out, str_t bytes) {
    const str_t ref = gen_str_ref(g, bytes);
    sb_append(out, "{ ptr ");
    sb_append_str(out, ref);
    sb_append(out, ", i64 ");
    sb_append_u64(out, bytes.len);
    sb_append(out, " }");
    return false;
}

// Handles the three extra module-level constant expressions. These are a declaration address, a
// function address, and another module-level declaration value. Returns false when `n` is none of
// them.
static bool const_symbol(gen_t* g, sb_t* out, const type_t* t, const ast_node_t* n, bool* zero) {
    const sym_t* s = n->sym;
    if (s == NULL) {
        return false;
    }
    if (s->kind == SYM_FN) {
        // a function's address is a relocation
        sb_append_str(out, gen_symbol_ref(g, s));
        *zero = false;
        return true;
    }
    if (s->kind == SYM_CONST && s->node != NULL && s->node->b != NULL) {
        // the checker resolved the chain and refused a cycle
        // a read of a `mut` global is not a constant expression
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
            // a folded constant is emitted as its literal
            const gen_val_t c = gen_const_mem_value(g, t, v);
            sb_append_str(out, c.val);
            return v.kind == CV_NULL || v.mag == 0;
        }
    }
    switch (n->kind) {
    case AST_BRACE_INIT:
        if (ast_len(n) == 0) {
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
            // `&` of a module-level declaration, and of nothing else
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
        // the checker reported it
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
    // An immutable declaration lives in read-only memory and a `mut` one in writable
    // memory. No section is named, since LLVM picks it from the initializer.
    // A named fort constant is not `unnamed_addr`: its address is significant,
    // because `&CONST` is expressible.
    sb_append(&g->globals,
              s->kind == SYM_GLOBAL ? " = dso_local global " : " = dso_local constant ");
    sb_append_str(&g->globals, ty);
    sb_push(&g->globals, ' ');
    sb_append_str(&g->globals, sb_view(&init));
    // an explicit `align N` from the compiler's own layout
    sb_append(&g->globals, ", align ");
    sb_append_u64(&g->globals, type_alignof(s->type));
    sb_push(&g->globals, '\n');
    sb_free(&init);
}

// ---- declarations --------------------------------------------------------

void gen_use_extern(gen_t* g, const sym_t* s) {
    const str_t name = s->name;
    for (uint64_t i = 0; i < g->externs.len; i++) {
        // Declares each symbol once. The linker sees the C name. Two modules can have separate
        // sym_t values for one ELF symbol.
        if (str_eq(((const sym_t*)g->externs.items[i])->name, name)) {
            return;
        }
    }
    ptrvec_push(&g->externs, (void*)s);
}

// Linux uses the variadic form. Darwin fixes an extern without `...`.
// Darwin also marks each declaration `nobuiltin`.
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
    // one spelling for every `@` name. An extern comes out bare
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
    const bool variadic = (s->node->flags & AST_FLAG_VARIADIC) != 0;
    if (variadic) {
        if (sig->nparams > 0) {
            sb_append(out, ", ");
        }
        sb_append(out, "...");
    }
    sb_push(out, ')');
    sb_append(out, " nobuiltin");
    sb_push(out, '\n');
}

// The intrinsics, with the spellings LLVM 18 prints, in the fixed order of
// LLVM IR table. The parameter attributes shown are part of the spelling.
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

// ---- calls to the runtime ------------------------------------------------

// Appends `@"std.rt.x"(<args>)`. Writes a runtime call as an ordinary fort call by mangled name. It
// quotes the dotted name and adds no call attribute.
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

// ---- assembly ------------------------------------------------------------

// Appends a non-empty section. A blank line separates adjacent sections.
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
    // Writes `extern` C functions in first-use order. It skips a symbol that the module defines.
    // One ELF symbol is one IR entity, so `declare` beside `define` would be a redefinition. The
    // checker refuses such a declaration first, since the name is reserved, so this only keeps the
    // invariant local to the emitter.
    for (uint64_t i = 0; i < g->externs.len; i++) {
        const sym_t* s = (const sym_t*)g->externs.items[i];
        if (g->entry_defined && str_eq(s->name, str_from_cstr(ENTRY_NAME))) {
            continue;
        }
        emit_extern(g, &externs, s);
    }
    sb_t intrinsics;
    sb_init(&intrinsics);
    // The emitter writes intrinsics in the fixed table order.
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
    if (g->opts.target == NULL || g->opts.target[0] == '\0' ||
        strcmp(g->opts.target, "x86_64-linux-gnu") == 0) {
        section(&g->out, str_from_cstr(MODULE_HEADER));
    } else {
        sb_t header;
        sb_init(&header);
        sb_append(&header, "target triple = \"");
        sb_append(&header, g->opts.target);
        sb_append(&header, "\"\n");
        section(&g->out, sb_view(&header));
        sb_free(&header);
    }
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
