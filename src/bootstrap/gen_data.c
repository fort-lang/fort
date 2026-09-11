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
#include "str.h"
#include "sym.h"
#include "types.h"

// The module header of item 1: clang's normalized spelling of the triple, and
// no datalayout, module flags, comments or source_filename (D19.1).
static const char MODULE_HEADER[] = "target triple = \"x86_64-unknown-linux-gnu\"\n";

// The declarations of the runtime entry points, with the C prototypes of
// toolchain.md 5.1 mapped to IR types by item 8, in that section's order
// (D19.5).
static const char* const RT_DECL[RT_COUNT] = {
    "declare ptr @fort_rt_new(i64, i64, ptr, i32, i32)",
    "declare void @fort_rt_del(ptr)",
    "declare void @fort_rt_fail_bounds(i64, i64, ptr, i32, i32) #2",
    "declare void @fort_rt_fail_span(i64, i64, i64, ptr, i32, i32) #2",
    "declare void @fort_rt_fail_overflow(ptr, i32, i32) #2",
    "declare void @fort_rt_fail_shift(i64, ptr, ptr, i32, i32) #2",
    "declare void @fort_rt_fail_div_zero(ptr, i32, i32) #2",
    "declare void @fort_rt_fail_div_overflow(ptr, i32, i32) #2",
    "declare void @fort_rt_fail_alloc_count(i64, ptr, i32, i32) #2",
    "declare void @fort_rt_fail_overwrite(ptr, i32, i32) #2",
    "declare void @fort_rt_panic(ptr, i64, ptr, i32, i32) #2",
    "declare void @fort_rt_assert_fail(ptr, ptr, i32, i32) #2",
    "declare void @fort_rt_print_i64(i32, i64)",
    "declare void @fort_rt_print_u64(i32, i64)",
    "declare void @fort_rt_print_f32(i32, float)",
    "declare void @fort_rt_print_f64(i32, double)",
    "declare void @fort_rt_print_bool(i32, i8 zeroext)",
    "declare void @fort_rt_print_char(i32, i8 zeroext)",
    "declare void @fort_rt_print_ptr(i32, ptr)",
    "declare void @fort_rt_print_str(i32, ptr, i64)",
    "declare void @fort_rt_print_enum(i32, i32, ptr, i64)",
    "declare void @fort_rt_flush(i32)",
    "declare void @fort_rt_flush_all()",
    "declare void @fort_rt_args_init(i32, ptr)",
    "declare ptr @fort_rt_args_ptr()",
    "declare i64 @fort_rt_args_len()",
    "declare void @fort_rt_exit(i32) #2",
};

// The symbol of each entry point, for a call site and for the rule that an
// `extern fn` naming one is declared in this group and not twice (item 8).
static const char* const RT_NAME[RT_COUNT] = {
    "@fort_rt_new",
    "@fort_rt_del",
    "@fort_rt_fail_bounds",
    "@fort_rt_fail_span",
    "@fort_rt_fail_overflow",
    "@fort_rt_fail_shift",
    "@fort_rt_fail_div_zero",
    "@fort_rt_fail_div_overflow",
    "@fort_rt_fail_alloc_count",
    "@fort_rt_fail_overwrite",
    "@fort_rt_panic",
    "@fort_rt_assert_fail",
    "@fort_rt_print_i64",
    "@fort_rt_print_u64",
    "@fort_rt_print_f32",
    "@fort_rt_print_f64",
    "@fort_rt_print_bool",
    "@fort_rt_print_char",
    "@fort_rt_print_ptr",
    "@fort_rt_print_str",
    "@fort_rt_print_enum",
    "@fort_rt_flush",
    "@fort_rt_flush_all",
    "@fort_rt_args_init",
    "@fort_rt_args_ptr",
    "@fort_rt_args_len",
    "@fort_rt_exit",
};

// The result type of each entry point: fort_rt_new, fort_rt_args_ptr and
// fort_rt_args_len are the three that return a value.
static const char* const RT_RESULT[RT_COUNT] = {
    "ptr",  "void", "void", "void", "void", "void", "void", "void", "void",
    "void", "void", "void", "void", "void", "void", "void", "void", "void",
    "void", "void", "void", "void", "void", "void", "ptr",  "i64",  "void",
};

// Whether toolchain.md 5.1 declares the entry point `_Noreturn`, which is
// what puts `cold noreturn nounwind` on its declaration (section 5, item 14).
static bool rt_is_noreturn(gen_rt_t rt) {
    return (rt >= RT_FAIL_BOUNDS && rt <= RT_ASSERT_FAIL) || rt == RT_EXIT;
}

gen_rt_t gen_runtime_entry(str_t name) {
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        // RT_NAME holds the operand, so its first byte is the `@`.
        if (str_eq(str_from_cstr(RT_NAME[i] + 1), name)) {
            return (gen_rt_t)i;
        }
    }
    return RT_COUNT;
}

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
    "cold noreturn nounwind",
    "nobuiltin",
    "nocallback nofree nosync nounwind speculatable willreturn memory(none)",
    "nocallback nofree nounwind willreturn memory(argmem: readwrite)",
    "nocallback nofree nounwind willreturn memory(argmem: write)",
    "cold noreturn nounwind memory(inaccessiblemem: write)",
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
    sb_append(&g->scratch, "@.enum.");
    sb_append_str(&g->scratch, dotted);
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

// Appends `c"..."` with the trailing NUL that `len` excludes (D3.7) and every
// byte outside the printable range written as a `\XX` hex pair (item 5).
static void append_bytes(sb_t* out, str_t s) {
    static const char HEX[] = "0123456789ABCDEF";
    sb_append(out, "c\"");
    for (uint64_t i = 0; i < s.len; i++) {
        const unsigned char byte = (unsigned char)s.ptr[i];
        if (byte < PRINTABLE_FIRST || byte > PRINTABLE_LAST || byte == '"' || byte == '\\') {
            sb_push(out, '\\');
            sb_push(out, HEX[byte / HEX_DIGITS]);
            sb_push(out, HEX[byte % HEX_DIGITS]);
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
        // struct fort_rt_enum_member (item 21).
        sb_append(out, "], align 8\n");
    }
}

// ---- declarations (item 8) --------------------------------------------------------

void gen_use_extern(gen_t* g, const sym_t* s) {
    const str_t name = s->name;
    const gen_rt_t rt = gen_runtime_entry(name);
    if (rt != RT_COUNT) {
        // An `extern fn` naming a runtime entry point is declared in the
        // runtime group with that group's prototype, once (item 8).
        g->rt[rt] = true;
        if (rt_is_noreturn(rt)) {
            gen_use_attr(g, ATTR_FAIL);
        }
        return;
    }
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
    sb_append_str(out, s->name);
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

// Marks an entry point used and appends `@fort_rt_x(<args>)`.
static void call_rt_tail(gen_t* g, gen_rt_t rt, const gen_args_t* args) {
    g->rt[rt] = true;
    if (rt_is_noreturn(rt)) {
        // A `_Noreturn` entry point is declared `cold noreturn nounwind`
        // (item 14, section 5).
        gen_use_attr(g, ATTR_FAIL);
    }
    gen_text_append(g, RT_NAME[rt]);
    gen_text_append(g, "(");
    gen_text_append_str(g, sb_view(&args->text));
    gen_text_append(g, ")");
    gen_ins_end(g);
}

void gen_call_rt(gen_t* g, gen_rt_t rt, const gen_args_t* args) {
    gen_ins(g);
    gen_text_append(g, "call ");
    gen_text_append(g, RT_RESULT[rt]);
    gen_text_append(g, " ");
    call_rt_tail(g, rt, args);
}

gen_val_t gen_call_rt_value(gen_t* g, gen_rt_t rt, str_t ret, const gen_args_t* args) {
    const gen_val_t r = gen_temp(g, ret);
    gen_text_append(g, "call ");
    gen_text_append(g, RT_RESULT[rt]);
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
    // `extern` C functions in first-use order (D19.5).
    for (uint64_t i = 0; i < g->externs.len; i++) {
        emit_extern(g, &externs, (const sym_t*)g->externs.items[i]);
    }
    sb_t runtime;
    sb_init(&runtime);
    // Then the runtime entry points in the order of toolchain.md 5.1.
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        if (g->rt[i]) {
            sb_append(&runtime, RT_DECL[i]);
            sb_push(&runtime, '\n');
        }
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
    section(&g->out, sb_view(&runtime));
    section(&g->out, sb_view(&intrinsics));
    section(&g->out, sb_view(&attributes));
    sb_free(&data);
    sb_free(&externs);
    sb_free(&runtime);
    sb_free(&intrinsics);
    sb_free(&attributes);
}
