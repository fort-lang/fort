// The LLVM IR emitter: the module skeleton, type lowering, names, the
// emission primitives and the function definitions (toolchain.md 6, D19); see
// gen.h.
#include "gen.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "consts.h"
#include "containers.h"
#include "diag.h"
#include "modules.h"
#include "prim.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// The two named types every module carries, used or not, so that the emitter
// tracks nothing (item 2). `%fort.span` serves every span and `string`, since
// with opaque pointers they have identical IR (D3.5, D3.7).
static const char SPAN_TYPE[] = "%fort.span";
static const char ENUM_MEMBER_TYPE[] = "%fort.enum_member";

// The bytes a span or `string` header occupies: a pointer and a 64-bit
// length (D3.5).
enum { SPAN_SIZE = 16, SPAN_ALIGN = 8 };

// The width of the widest integer the emitter prints a constant of.
enum { BITS_PER_U64 = 64 };

// The header fields of a span, in the order item 17 gives them.
enum { SPAN_FIELD_PTR = 0, SPAN_FIELD_LEN = 1 };

void gen_args_init(gen_args_t* a) {
    sb_init(&a->text);
    a->count = 0;
}

void gen_args_free(gen_args_t* a) {
    sb_free(&a->text);
    a->count = 0;
}

void gen_args_add_ext(gen_args_t* a, gen_val_t v, const char* attr) {
    if (a->count > 0) {
        sb_append(&a->text, ", ");
    }
    sb_append_str(&a->text, v.ty);
    if (attr != NULL) {
        sb_push(&a->text, ' ');
        sb_append(&a->text, attr);
    }
    sb_push(&a->text, ' ');
    sb_append_str(&a->text, v.val);
    a->count++;
}

void gen_args_add(gen_args_t* a, gen_val_t v) {
    gen_args_add_ext(a, v, NULL);
}

// ---- the emitter ------------------------------------------------------------------

// The one type node the emitter builds itself: the `char` a `string`'s
// elements are, which `string` does not carry (D3.7). Every field is written,
// since a type node is compared and lowered by all of them (types.h).
static void char_type_init(type_t* t) {
    t->kind = TYPE_PRIM;
    t->prim = PRIM_CHAR;
    t->mut = false;
    t->own = false;
    t->noreturn = false;
    t->elem = NULL;
    t->len = 0;
    t->params = NULL;
    t->nparams = 0;
    t->name = str_from_range(NULL, 0);
    t->decl = NULL;
    t->layout = NULL;
}

void gen_init(gen_t* g, gen_options_t opts) {
    g->opts = opts;
    g->ck = NULL;
    g->set = NULL;
    sb_init(&g->named);
    sb_init(&g->globals);
    sb_init(&g->funcs);
    sb_init(&g->out);
    sb_init(&g->allocas);
    sb_init(&g->body);
    sb_init(&g->fail);
    g->temps = 0;
    g->labels = 0;
    g->tmps = 0;
    g->break_label = 0;
    g->continue_label = 0;
    g->has_break = false;
    g->has_continue = false;
    ptrvec_init(&g->slots);
    g->terminated = false;
    ptrvec_init(&g->files);
    ptrvec_init(&g->strs);
    ptrvec_init(&g->enums);
    ptrvec_init(&g->externs);
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        g->rt[i] = false;
    }
    for (uint64_t i = 0; i < (uint64_t)IN_COUNT; i++) {
        g->intrinsics[i] = false;
    }
    for (uint64_t i = 0; i < (uint64_t)ATTR_COUNT; i++) {
        g->attrs[i] = false;
    }
    char_type_init(&g->char_type);
    str_pool_init(&g->pool);
    sb_init(&g->scratch);
    g->module = NULL;
    g->entry_defined = false;
    g->failed = false;
}

static void free_records(ptrvec_t* v) {
    for (uint64_t i = 0; i < v->len; i++) {
        mem_free(v->items[i]);
    }
    ptrvec_free(v);
}

void gen_free(gen_t* g) {
    sb_free(&g->named);
    sb_free(&g->globals);
    sb_free(&g->funcs);
    sb_free(&g->out);
    sb_free(&g->allocas);
    sb_free(&g->body);
    sb_free(&g->fail);
    free_records(&g->slots);
    free_records(&g->files);
    free_records(&g->strs);
    free_records(&g->enums);
    ptrvec_free(&g->externs);
    str_pool_free(&g->pool);
    sb_free(&g->scratch);
    g->module = NULL;
}

str_t gen_take(gen_t* g) {
    const str_t s = str_pool_intern(&g->pool, sb_view(&g->scratch));
    sb_clear(&g->scratch);
    return s;
}

str_t gen_text(const gen_t* g) {
    return sb_view(&g->out);
}

void gen_todo(gen_t* g, loc_t loc, const char* what) {
    sb_clear(&g->scratch);
    sb_append(&g->scratch, "the bootstrap compiler cannot generate code yet for ");
    sb_append(&g->scratch, what);
    diag_error(loc, sb_cstr(&g->scratch));
    sb_clear(&g->scratch);
    g->failed = true;
}

// ---- types (item 2) ---------------------------------------------------------------

bool gen_is_aggregate(const type_t* t) {
    // Only scalars are SSA values; everything else occupies a place (D19.3).
    return t != NULL && (t->kind == TYPE_ARRAY || t->kind == TYPE_SPAN || t->kind == TYPE_STRING ||
                         t->kind == TYPE_STRUCT);
}

uint32_t gen_int_bits(const type_t* t) {
    if (t == NULL) {
        return 0;
    }
    if (t->kind == TYPE_ENUM) {
        // An enum is `i32` (D3.9).
        return 4 * PRIM_BITS_PER_BYTE;
    }
    if (t->kind != TYPE_PRIM) {
        return 0;
    }
    if (t->prim == PRIM_BOOL) {
        // `bool` is `i1` as a value (D3.3).
        return 1;
    }
    return prim_size(t->prim) * PRIM_BITS_PER_BYTE;
}

bool gen_is_signed(const type_t* t) {
    if (t == NULL) {
        return false;
    }
    if (t->kind == TYPE_ENUM) {
        // An enum's `i32` is the signed type of D3.1: a member may be
        // negative, so its constants print signed and a widening cast of one
        // sign-extends (D3.9).
        return true;
    }
    return t->kind == TYPE_PRIM && prim_is_signed(t->prim);
}

// The dotted name of a nominal type's declaration, `%struct.main.point`
// (item 2, D9.7).
static str_t nominal_type_name(gen_t* g, const type_t* t) {
    const sym_t* s = (const sym_t*)t->decl;
    // The dotted name is built first: gen_symbol builds its own text in the
    // same scratch buffer.
    const str_t dotted = s != NULL ? gen_symbol(g, s) : str_from_cstr("");
    sb_clear(&g->scratch);
    sb_push(&g->scratch, '%');
    gen_append_name(&g->scratch, "struct.", dotted, false);
    return gen_take(g);
}

str_t gen_mem_type(gen_t* g, const type_t* t) {
    if (t == NULL) {
        return str_from_cstr("void");
    }
    switch (t->kind) {
    case TYPE_VOID:
        return str_from_cstr("void");
    case TYPE_PRIM:
        switch (t->prim) {
        case PRIM_I8:
        case PRIM_U8:
        case PRIM_CHAR:
        case PRIM_BOOL:
            // `bool` is `i8` in memory, with C's `_Bool` layout (D19.2).
            return str_from_cstr("i8");
        case PRIM_I16:
        case PRIM_U16:
            return str_from_cstr("i16");
        case PRIM_I32:
        case PRIM_U32:
            return str_from_cstr("i32");
        case PRIM_I64:
        case PRIM_U64:
            return str_from_cstr("i64");
        case PRIM_F32:
            return str_from_cstr("float");
        case PRIM_F64:
            return str_from_cstr("double");
        case PRIM_VOID:
            break;
        }
        return str_from_cstr("void");
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_FN:
        // Every pointer, `void*` and function pointer is the opaque `ptr`
        // (D3.10, D3.11).
        return str_from_cstr("ptr");
    case TYPE_ARRAY: {
        // An array type nests outside in, so `i32[3][4]` is
        // `[3 x [4 x i32]]` (D3.6).
        const str_t elem = gen_mem_type(g, t->elem);
        sb_clear(&g->scratch);
        sb_push(&g->scratch, '[');
        sb_append_u64(&g->scratch, t->len);
        sb_append(&g->scratch, " x ");
        sb_append_str(&g->scratch, elem);
        sb_push(&g->scratch, ']');
        return gen_take(g);
    }
    case TYPE_SPAN:
    case TYPE_STRING:
        // One `%fort.span` serves every span and `string` (D3.5, D3.7).
        return str_from_cstr(SPAN_TYPE);
    case TYPE_STRUCT:
        return nominal_type_name(g, t);
    case TYPE_ENUM:
        return str_from_cstr("i32");
    case TYPE_NULL:
        return str_from_cstr("ptr");
    case TYPE_ERROR:
        break;
    }
    return str_from_cstr("void");
}

str_t gen_value_type(gen_t* g, const type_t* t) {
    if (t != NULL && t->kind == TYPE_PRIM && t->prim == PRIM_BOOL) {
        // `bool` is `i1` as a value and `i8` in memory (D19.2).
        return str_from_cstr("i1");
    }
    return gen_mem_type(g, t);
}

str_t gen_result_type(gen_t* g, const type_t* t) {
    if (gen_is_aggregate(t)) {
        // An aggregate result is a leading `sret` pointer on a function whose
        // result type is `void` (item 7).
        return str_from_cstr("void");
    }
    return gen_value_type(g, t);
}

const char* gen_ext_attr(const type_t* t) {
    if (t == NULL || t->kind != TYPE_PRIM) {
        return NULL;
    }
    switch (t->prim) {
    case PRIM_BOOL:
    case PRIM_CHAR:
    case PRIM_U8:
    case PRIM_U16:
        // `bool`, `char`, `u8` and `u16` carry `zeroext` (item 7).
        return "zeroext";
    case PRIM_I8:
    case PRIM_I16:
        return "signext";
    default:
        break;
    }
    return NULL;
}

// ---- names (item 4, D9.7) ---------------------------------------------------------

str_t gen_symbol(gen_t* g, const sym_t* s) {
    if (s == NULL) {
        return str_from_cstr("");
    }
    if (s->kind == SYM_EXTERN_FN) {
        // `extern` names are unmangled (D9.7).
        return s->name;
    }
    sb_clear(&g->scratch);
    if (s->owner != NULL && s->owner->kind == SYM_MODULE) {
        // The module path joined with dots plus the declaration name
        // (D9.7): `std::io::read_file` is `std.io.read_file`.
        const str_t path = s->owner->name;
        for (uint64_t i = 0; i < path.len; i++) {
            if (path.ptr[i] == ':') {
                if (i + 1 < path.len && path.ptr[i + 1] == ':') {
                    sb_push(&g->scratch, '.');
                    i++;
                }
                continue;
            }
            sb_push(&g->scratch, path.ptr[i]);
        }
        sb_push(&g->scratch, '.');
    }
    sb_append_str(&g->scratch, s->name);
    return gen_take(g);
}

str_t gen_symbol_ref(gen_t* g, const sym_t* s) {
    const str_t name = gen_symbol(g, s);
    sb_clear(&g->scratch);
    sb_push(&g->scratch, '@');
    if (s != NULL && s->kind == SYM_EXTERN_FN) {
        // A C name is an identifier and stands unquoted (D9.7).
        gen_append_name(&g->scratch, "", name, false);
        return gen_take(g);
    }
    // A dotted name is quoted, which is spelling only: the ELF symbol is
    // unchanged (D9.7).
    gen_append_name(&g->scratch, "", name, true);
    return gen_take(g);
}

// ---- emission primitives ----------------------------------------------------------

void gen_text_append(gen_t* g, const char* s) {
    sb_append(&g->body, s);
}

void gen_text_append_str(gen_t* g, str_t s) {
    sb_append_str(&g->body, s);
}

void gen_text_append_u64(gen_t* g, uint64_t v) {
    sb_append_u64(&g->body, v);
}

void gen_text_append_i64(gen_t* g, int64_t v) {
    sb_append_i64(&g->body, v);
}

void gen_ins(gen_t* g) {
    sb_append(&g->body, "  ");
}

void gen_ins_end(gen_t* g) {
    sb_push(&g->body, '\n');
}

gen_val_t gen_temp(gen_t* g, str_t ty) {
    // Instruction results are `%t<N>` in emission order, per function
    // (D19.5).
    sb_clear(&g->scratch);
    sb_append(&g->scratch, "%t");
    sb_append_u64(&g->scratch, g->temps);
    g->temps++;
    gen_val_t v;
    v.ty = ty;
    v.val = gen_take(g);
    sb_append(&g->body, "  ");
    sb_append_str(&g->body, v.val);
    sb_append(&g->body, " = ");
    return v;
}

// ---- values -----------------------------------------------------------------------

gen_val_t gen_literal(gen_t* g, str_t ty, const char* text) {
    gen_val_t v;
    v.ty = ty;
    v.val = str_pool_intern(&g->pool, str_from_cstr(text));
    return v;
}

gen_val_t gen_const_signed(gen_t* g, str_t ty, int64_t v) {
    sb_clear(&g->scratch);
    sb_append_i64(&g->scratch, v);
    gen_val_t out;
    out.ty = ty;
    out.val = gen_take(g);
    return out;
}

gen_val_t gen_const_unsigned(gen_t* g, str_t ty, uint64_t v) {
    sb_clear(&g->scratch);
    sb_append_u64(&g->scratch, v);
    gen_val_t out;
    out.ty = ty;
    out.val = gen_take(g);
    return out;
}

// A negative constant from its magnitude, so that a value whose magnitude is
// 2^63 (`i64` MIN) never has to be negated in a signed type (D19.5).
static gen_val_t const_negative(gen_t* g, str_t ty, uint64_t magnitude) {
    sb_clear(&g->scratch);
    sb_push(&g->scratch, '-');
    sb_append_u64(&g->scratch, magnitude);
    gen_val_t out;
    out.ty = ty;
    out.val = gen_take(g);
    return out;
}

// The low `bits` of `x` as the fort type reads them: sign-extended for a
// signed type, zero-extended otherwise (D19.5). The magnitude of a negative
// value is the two's complement of the pattern, computed in unsigned
// arithmetic so that every width behaves alike.
static gen_val_t const_bits(gen_t* g, str_t ty, uint64_t x, uint32_t bits, bool sign) {
    const uint32_t width = bits == 0 || bits > BITS_PER_U64 ? BITS_PER_U64 : bits;
    const uint64_t mask = width == BITS_PER_U64 ? UINT64_MAX : ((uint64_t)1 << width) - 1U;
    const uint64_t low = x & mask;
    if (sign && (low & ((uint64_t)1 << (width - 1))) != 0) {
        // A negative value of a signed type prints as `i8 -1` (D19.5).
        return const_negative(g, ty, (~low & mask) + 1U);
    }
    return gen_const_unsigned(g, ty, low);
}

gen_val_t gen_const_min(gen_t* g, str_t ty, uint32_t bits) {
    // The pattern with the sign bit alone set, read as the signed type says
    // (D19.5): `i8` MIN is -128 and `i64` MIN -9223372036854775808.
    return const_bits(g, ty, (uint64_t)1 << (bits - 1), bits, true);
}

gen_val_t gen_const_value(gen_t* g, const type_t* t, cval_t v) {
    const str_t ty = gen_value_type(g, t);
    if (v.kind == CV_BOOL) {
        return gen_literal(g, ty, v.mag != 0 ? "true" : "false");
    }
    if (v.kind == CV_NULL) {
        return gen_literal(g, ty, "null");
    }
    if (t != NULL && (t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR || t->kind == TYPE_FN)) {
        // A pointer constant is `null`; no other address folds (D10.5).
        return gen_literal(g, ty, "null");
    }
    // A char in an integer context is its code point, so the folder is asked
    // for the integer of an integer-like value (D4.3).
    return const_bits(g, ty, cv_bits(cv_as_int(v)), gen_int_bits(t), gen_is_signed(t));
}

gen_val_t gen_binary(gen_t* g, const char* op, gen_val_t a, gen_val_t b) {
    const gen_val_t r = gen_temp(g, a.ty);
    sb_append(&g->body, op);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, a.ty);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, a.val);
    sb_append(&g->body, ", ");
    sb_append_str(&g->body, b.val);
    sb_push(&g->body, '\n');
    return r;
}

gen_val_t gen_icmp(gen_t* g, const char* pred, gen_val_t a, gen_val_t b) {
    const gen_val_t r = gen_temp(g, str_from_cstr("i1"));
    sb_append(&g->body, "icmp ");
    sb_append(&g->body, pred);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, a.ty);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, a.val);
    sb_append(&g->body, ", ");
    sb_append_str(&g->body, b.val);
    sb_push(&g->body, '\n');
    return r;
}

gen_val_t gen_cast_op(gen_t* g, const char* op, gen_val_t a, str_t to) {
    const gen_val_t r = gen_temp(g, to);
    sb_append(&g->body, op);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, a.ty);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, a.val);
    sb_append(&g->body, " to ");
    sb_append_str(&g->body, to);
    sb_push(&g->body, '\n');
    return r;
}

gen_val_t gen_resize(gen_t* g, gen_val_t v, uint32_t from, str_t ty, uint32_t to, bool sign) {
    if (from == to) {
        // A narrow value is not widened to 32 bits: its width is in its type
        // (item 9).
        return v;
    }
    if (from > to) {
        return gen_cast_op(g, "trunc", v, ty);
    }
    // Widening reads the source's signedness (item 12).
    return gen_cast_op(g, sign ? "sext" : "zext", v, ty);
}

gen_val_t gen_load(gen_t* g, str_t ty, gen_val_t addr, uint64_t align) {
    const gen_val_t r = gen_temp(g, ty);
    sb_append(&g->body, "load ");
    sb_append_str(&g->body, ty);
    sb_append(&g->body, ", ptr ");
    sb_append_str(&g->body, addr.val);
    // Every load and store carries an explicit alignment (item 5).
    sb_append(&g->body, ", align ");
    sb_append_u64(&g->body, align);
    sb_push(&g->body, '\n');
    return r;
}

void gen_store(gen_t* g, gen_val_t v, gen_val_t addr, uint64_t align) {
    sb_append(&g->body, "  store ");
    sb_append_str(&g->body, v.ty);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, v.val);
    sb_append(&g->body, ", ptr ");
    sb_append_str(&g->body, addr.val);
    sb_append(&g->body, ", align ");
    sb_append_u64(&g->body, align);
    sb_push(&g->body, '\n');
}

gen_val_t gen_load_place(gen_t* g, gen_place_t p) {
    const uint64_t align = type_alignof(p.type);
    const gen_val_t raw = gen_load(g, gen_mem_type(g, p.type), p.addr, align);
    if (p.type != NULL && p.type->kind == TYPE_PRIM && p.type->prim == PRIM_BOOL) {
        // Every load of a `bool` place is a `load i8` and a `trunc` (D19.2).
        return gen_cast_op(g, "trunc", raw, str_from_cstr("i1"));
    }
    return raw;
}

void gen_store_place(gen_t* g, gen_place_t p, gen_val_t v) {
    const uint64_t align = type_alignof(p.type);
    gen_val_t out = v;
    if (p.type != NULL && p.type->kind == TYPE_PRIM && p.type->prim == PRIM_BOOL) {
        // Every store of a `bool` place is a `zext` and a `store i8` (D19.2).
        out = gen_cast_op(g, "zext", v, str_from_cstr("i8"));
    }
    gen_store(g, out, p.addr, align);
}

// ---- the three getelementptr shapes (item 3) ---------------------------------------

// Opens `  %tN = getelementptr inbounds <ty>, ptr <base>`; `inbounds` is
// always true, since a bounds check precedes an element access and a field
// access is in bounds by construction (item 3).
static gen_val_t gep_begin(gen_t* g, str_t ty, gen_val_t base) {
    const gen_val_t r = gen_temp(g, str_from_cstr("ptr"));
    sb_append(&g->body, "getelementptr inbounds ");
    sb_append_str(&g->body, ty);
    sb_append(&g->body, ", ptr ");
    sb_append_str(&g->body, base.val);
    return r;
}

gen_val_t gen_gep_array(gen_t* g, const type_t* array, gen_val_t base, gen_val_t index) {
    const gen_val_t r = gep_begin(g, gen_mem_type(g, array), base);
    sb_append(&g->body, ", i64 0, i64 ");
    sb_append_str(&g->body, index.val);
    sb_push(&g->body, '\n');
    return r;
}

gen_val_t gen_gep_element(gen_t* g, const type_t* elem, gen_val_t base, gen_val_t index) {
    const gen_val_t r = gep_begin(g, gen_mem_type(g, elem), base);
    sb_append(&g->body, ", i64 ");
    sb_append_str(&g->body, index.val);
    sb_push(&g->body, '\n');
    return r;
}

gen_val_t gen_gep_field(gen_t* g, str_t ty, gen_val_t base, uint64_t k) {
    const gen_val_t r = gep_begin(g, ty, base);
    sb_append(&g->body, ", i32 0, i32 ");
    sb_append_u64(&g->body, k);
    sb_push(&g->body, '\n');
    return r;
}

// ---- the span header (item 17) -----------------------------------------------------

gen_val_t gen_span_ptr(gen_t* g, gen_val_t base) {
    // Field 0 of `%fort.span` is the pointer (D3.5, D19.2).
    const gen_val_t field = gen_gep_field(g, str_from_cstr(SPAN_TYPE), base, SPAN_FIELD_PTR);
    return gen_load(g, str_from_cstr("ptr"), field, (uint64_t)sizeof(void*));
}

gen_val_t gen_span_len(gen_t* g, gen_val_t base) {
    // Field 1 is the length, a `u64` (D3.5, D19.2).
    const gen_val_t field = gen_gep_field(g, str_from_cstr(SPAN_TYPE), base, SPAN_FIELD_LEN);
    return gen_load(g, str_from_cstr("i64"), field, (uint64_t)sizeof(uint64_t));
}

void gen_span_init(gen_t* g, gen_val_t base, gen_val_t ptr, gen_val_t len) {
    // The header is written field by field, pointer first (item 17).
    const gen_val_t pf = gen_gep_field(g, str_from_cstr(SPAN_TYPE), base, SPAN_FIELD_PTR);
    gen_store(g, ptr, pf, (uint64_t)sizeof(void*));
    const gen_val_t lf = gen_gep_field(g, str_from_cstr(SPAN_TYPE), base, SPAN_FIELD_LEN);
    gen_store(g, len, lf, (uint64_t)sizeof(uint64_t));
}

// ---- aggregates (item 3) ----------------------------------------------------------

void gen_memcpy(
    gen_t* g, gen_val_t dst, uint64_t dst_align, gen_val_t src, uint64_t src_align, uint64_t size) {
    gen_use_intrinsic(g, IN_MEMCPY);
    gen_use_attr(g, ATTR_MEMCPY);
    sb_append(&g->body, "  call void @llvm.memcpy.p0.p0.i64(ptr align ");
    sb_append_u64(&g->body, dst_align);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, dst.val);
    sb_append(&g->body, ", ptr align ");
    sb_append_u64(&g->body, src_align);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, src.val);
    sb_append(&g->body, ", i64 ");
    sb_append_u64(&g->body, size);
    sb_append(&g->body, ", i1 false)\n");
}

void gen_memset_zero(gen_t* g, gen_val_t dst, uint64_t align, uint64_t size) {
    gen_use_intrinsic(g, IN_MEMSET);
    gen_use_attr(g, ATTR_MEMSET);
    sb_append(&g->body, "  call void @llvm.memset.p0.i64(ptr align ");
    sb_append_u64(&g->body, align);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, dst.val);
    sb_append(&g->body, ", i8 0, i64 ");
    sb_append_u64(&g->body, size);
    sb_append(&g->body, ", i1 false)\n");
}

// ---- blocks (item 10) -------------------------------------------------------------

uint64_t gen_label(gen_t* g) {
    // Blocks are `%L<N>` in creation order, the entry block excepted (D19.5).
    const uint64_t n = g->labels;
    g->labels++;
    return n;
}

void gen_block_begin(gen_t* g, uint64_t n) {
    sb_push(&g->body, '\n');
    sb_push(&g->body, 'L');
    sb_append_u64(&g->body, n);
    sb_append(&g->body, ":\n");
    g->terminated = false;
}

void gen_br(gen_t* g, uint64_t n) {
    sb_append(&g->body, "  br label %L");
    sb_append_u64(&g->body, n);
    sb_push(&g->body, '\n');
    g->terminated = true;
}

void gen_br_cond(gen_t* g, gen_val_t cond, uint64_t t, uint64_t f) {
    sb_append(&g->body, "  br i1 ");
    sb_append_str(&g->body, cond.val);
    sb_append(&g->body, ", label %L");
    sb_append_u64(&g->body, t);
    sb_append(&g->body, ", label %L");
    sb_append_u64(&g->body, f);
    sb_push(&g->body, '\n');
    g->terminated = true;
}

// A fort `switch` is one LLVM `switch` on the operand, with one case per
// label and a default block (item 10). The case list is indented one level
// further than an instruction, and the `]` that closes it stands where an
// instruction does, which is how LLVM prints it.
void gen_switch_begin(gen_t* g, gen_val_t operand, uint64_t default_label) {
    sb_append(&g->body, "  switch ");
    sb_append_str(&g->body, operand.ty);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, operand.val);
    sb_append(&g->body, ", label %L");
    sb_append_u64(&g->body, default_label);
    sb_append(&g->body, " [\n");
}

void gen_switch_case(gen_t* g, gen_val_t value, uint64_t label) {
    sb_append(&g->body, "    ");
    sb_append_str(&g->body, value.ty);
    sb_push(&g->body, ' ');
    sb_append_str(&g->body, value.val);
    sb_append(&g->body, ", label %L");
    sb_append_u64(&g->body, label);
    sb_push(&g->body, '\n');
}

void gen_switch_end(gen_t* g) {
    sb_append(&g->body, "  ]\n");
    g->terminated = true;
}

// ---- checks and failure blocks (item 14, D19.6) -----------------------------------

void gen_args_add_loc(gen_t* g, gen_args_t* args, loc_t loc) {
    // Each failure call carries the file constant and the line and column of
    // D11.4's position rule (item 14).
    gen_args_add(args, gen_literal(g, str_from_cstr("ptr"), gen_file_ref(g, loc).ptr));
    gen_args_add(args, gen_const_unsigned(g, str_from_cstr("i32"), loc.line));
    gen_args_add(args, gen_const_unsigned(g, str_from_cstr("i32"), loc.col));
}

// One failure block: the call to the entry point and `unreachable`, and
// nothing else, because the callee aborts (item 14). It is written to the
// function's failure buffer, which is appended after every normal block.
static void fail_block(gen_t* g, uint64_t label, gen_rt_t rt, const gen_args_t* args) {
    // A failure block is built in the same shape as a normal one, so it is
    // written through the body buffer and moved afterwards.
    const uint64_t mark = g->body.len;
    gen_block_begin(g, label);
    gen_call_rt(g, rt, args);
    sb_append(&g->body, "  unreachable\n");
    const str_t text = str_from_range(g->body.data + mark, g->body.len - mark);
    sb_append_str(&g->fail, text);
    g->body.len = mark;
}

void gen_check(gen_t* g, gen_val_t cond, bool fail_when, gen_rt_t rt, gen_args_t* args, loc_t loc) {
    // The continuation label is allocated before the failure label (D19.6).
    const uint64_t cont = gen_label(g);
    const uint64_t bad = gen_label(g);
    // Every check computes one `i1` that is true on failure and branches with
    // the failure label first; `assert` is the exception, since its operand is
    // already the success condition (D19.6, D12.2).
    if (fail_when) {
        gen_br_cond(g, cond, bad, cont);
    } else {
        gen_br_cond(g, cond, cont, bad);
    }
    gen_args_add_loc(g, args, loc);
    fail_block(g, bad, rt, args);
    gen_block_begin(g, cont);
}

void gen_fail_block(gen_t* g, uint64_t label, gen_rt_t rt, gen_args_t* args, loc_t loc) {
    gen_args_add_loc(g, args, loc);
    fail_block(g, label, rt, args);
}

void gen_use_intrinsic(gen_t* g, gen_intrinsic_t which) {
    g->intrinsics[which] = true;
}

void gen_use_attr(gen_t* g, gen_attr_t which) {
    g->attrs[which] = true;
}

// ---- slots (item 10) --------------------------------------------------------------

// Records the place of a local or parameter under the name D19.5 gives it.
static str_t slot_name(gen_t* g, const sym_t* s, uint64_t slot, bool incoming) {
    sb_clear(&g->scratch);
    sb_push(&g->scratch, '%');
    sb_append_str(&g->scratch, s->name);
    sb_push(&g->scratch, '.');
    if (incoming) {
        // A parameter arrives as `%<ident>.in` (D19.5).
        sb_append(&g->scratch, "in");
    } else {
        sb_append_u64(&g->scratch, slot);
    }
    return gen_take(g);
}

static void slot_add(gen_t* g, const sym_t* s, str_t name) {
    gen_slot_t* slot = mem_alloc((uint64_t)sizeof(gen_slot_t));
    slot->sym = s;
    slot->name = name;
    ptrvec_push(&g->slots, slot);
}

gen_place_t gen_slot_place(gen_t* g, const sym_t* s) {
    gen_place_t p;
    p.addr.ty = str_from_cstr("ptr");
    p.addr.val = str_from_cstr("");
    p.type = s != NULL ? s->type : NULL;
    for (uint64_t i = 0; i < g->slots.len; i++) {
        const gen_slot_t* slot = (const gen_slot_t*)g->slots.items[i];
        if (slot->sym == s) {
            p.addr.val = slot->name;
            return p;
        }
    }
    fatal_internal("gen: a local with no slot");
}

gen_place_t gen_temp_place_raw(gen_t* g, str_t mem_type, uint64_t align) {
    // A place the compiler invents is `%tmp<K>` from a third counter, and
    // never contains a dot, so it cannot collide with a local (D19.5).
    sb_clear(&g->scratch);
    sb_append(&g->scratch, "%tmp");
    sb_append_u64(&g->scratch, g->tmps);
    g->tmps++;
    gen_place_t p;
    p.addr.ty = str_from_cstr("ptr");
    p.addr.val = gen_take(g);
    p.type = NULL;
    // Every compiler temporary is an alloca in the entry block (D19.4).
    sb_append(&g->allocas, "  ");
    sb_append_str(&g->allocas, p.addr.val);
    sb_append(&g->allocas, " = alloca ");
    sb_append_str(&g->allocas, mem_type);
    sb_append(&g->allocas, ", align ");
    sb_append_u64(&g->allocas, align);
    sb_push(&g->allocas, '\n');
    return p;
}

gen_place_t gen_temp_place(gen_t* g, const type_t* t) {
    gen_place_t p = gen_temp_place_raw(g, gen_mem_type(g, t), type_alignof(t));
    p.type = t;
    return p;
}

// ---- function definitions (item 7) ------------------------------------------------

// Every local of the body, in declaration order, which is the order their
// allocas are emitted in (D19.4).
static void collect_locals(gen_t* g, ast_node_t* n, ptrvec_t* out) {
    if (n == NULL) {
        return;
    }
    if ((n->kind == AST_VAR_DECL || n->kind == AST_RANGE_FOR) && n->sym != NULL) {
        // A range `for` declares its loop variable on the loop node itself,
        // and that variable is a local like any other (D7.5).
        ptrvec_push(out, (void*)n);
    }
    collect_locals(g, n->a, out);
    collect_locals(g, n->b, out);
    collect_locals(g, n->c, out);
    collect_locals(g, n->d, out);
    for (uint64_t i = 0; i < ast_len(n); i++) {
        collect_locals(g, ast_child(n, i), out);
    }
}

// `  %x.0 = alloca <ty>, align <n>`, the storage of one local or parameter.
static void emit_alloca(gen_t* g, str_t name, const type_t* t) {
    sb_append(&g->allocas, "  ");
    sb_append_str(&g->allocas, name);
    sb_append(&g->allocas, " = alloca ");
    sb_append_str(&g->allocas, gen_mem_type(g, t));
    sb_append(&g->allocas, ", align ");
    sb_append_u64(&g->allocas, type_alignof(t));
    sb_push(&g->allocas, '\n');
}

// The signature of a fort definition (item 7): scalars are ordinary
// parameters, an aggregate is a plain `ptr`, and an aggregate result is a
// leading `ptr sret(%T) %ret.sret` on a `void` function.
// One blank line stands between two definitions (item 1).
static void definition_begin(gen_t* g) {
    if (g->funcs.len > 0) {
        sb_push(&g->funcs, '\n');
    }
}

static void emit_signature(gen_t* g, const ast_node_t* fn, const sym_t* s, const type_t* sig) {
    definition_begin(g);
    sb_append(&g->funcs, "define dso_local ");
    const char* ret_attr = gen_ext_attr(sig->elem);
    if (ret_attr != NULL) {
        // A narrow result carries the same attribute as a narrow parameter,
        // so that LLVM normalizes on both sides of a call (item 7).
        sb_append(&g->funcs, ret_attr);
        sb_push(&g->funcs, ' ');
    }
    sb_append_str(&g->funcs, gen_result_type(g, sig->elem));
    sb_push(&g->funcs, ' ');
    sb_append_str(&g->funcs, gen_symbol_ref(g, s));
    sb_push(&g->funcs, '(');
    uint64_t written = 0;
    if (gen_is_aggregate(sig->elem)) {
        sb_append(&g->funcs, "ptr sret(");
        sb_append_str(&g->funcs, gen_mem_type(g, sig->elem));
        sb_append(&g->funcs, ") %ret.sret");
        written++;
    }
    for (uint64_t i = 0; i < ast_len(fn); i++) {
        const ast_node_t* p = ast_child(fn, i);
        if (p->kind != AST_PARAM || p->sym == NULL) {
            continue;
        }
        if (written > 0) {
            sb_append(&g->funcs, ", ");
        }
        written++;
        const type_t* t = p->sym->type;
        if (gen_is_aggregate(t)) {
            // A struct, fixed array, span or `string` argument is a plain
            // `ptr` parameter and `byval` is never used (item 7).
            sb_append(&g->funcs, "ptr ");
        } else {
            sb_append_str(&g->funcs, gen_value_type(g, t));
            const char* attr = gen_ext_attr(t);
            if (attr != NULL) {
                sb_push(&g->funcs, ' ');
                sb_append(&g->funcs, attr);
            }
            sb_push(&g->funcs, ' ');
        }
        sb_append_str(&g->funcs, slot_name(g, p->sym, 0, true));
    }
    sb_push(&g->funcs, ')');
    // `#0` on every fort definition, `#1` when it is `noreturn` (items 7, 20).
    if (sig->noreturn) {
        gen_use_attr(g, ATTR_FN_NORET);
        sb_append(&g->funcs, " #1 {\n");
    } else {
        gen_use_attr(g, ATTR_FN);
        sb_append(&g->funcs, " #0 {\n");
    }
}

// Resets the per-function counters and buffers: every name is fixed per
// function and reset at each definition (D19.5).
static void function_begin(gen_t* g) {
    g->temps = 0;
    g->labels = 0;
    g->tmps = 0;
    g->break_label = 0;
    g->continue_label = 0;
    g->has_break = false;
    g->has_continue = false;
    free_records(&g->slots);
    ptrvec_init(&g->slots);
    sb_clear(&g->allocas);
    sb_clear(&g->body);
    sb_clear(&g->fail);
    g->terminated = false;
}

// Appends the entry block, the normal blocks and then the failure blocks
// (D19.6), and closes the definition.
static void function_end(gen_t* g) {
    sb_append(&g->funcs, "entry:\n");
    sb_append_str(&g->funcs, sb_view(&g->allocas));
    sb_append_str(&g->funcs, sb_view(&g->body));
    sb_append_str(&g->funcs, sb_view(&g->fail));
    sb_append(&g->funcs, "}\n");
}

static void gen_function(gen_t* g, ast_node_t* fn) {
    const sym_t* s = fn->sym;
    if (s == NULL || s->error || s->type == NULL || s->type->kind != TYPE_FN) {
        return;
    }
    const type_t* sig = s->type;
    function_begin(g);
    emit_signature(g, fn, s, sig);
    uint64_t slot = 0;
    // The parameters first, then the locals in declaration order (D19.4).
    for (uint64_t i = 0; i < ast_len(fn); i++) {
        const ast_node_t* p = ast_child(fn, i);
        if (p->kind != AST_PARAM || p->sym == NULL) {
            continue;
        }
        const type_t* t = p->sym->type;
        if (gen_is_aggregate(t)) {
            // An aggregate parameter is not copied again: its place is the
            // caller-made copy the incoming pointer designates (item 10).
            slot_add(g, p->sym, slot_name(g, p->sym, slot, true));
        } else {
            const str_t name = slot_name(g, p->sym, slot, false);
            slot_add(g, p->sym, name);
            emit_alloca(g, name, t);
        }
        slot++;
    }
    ptrvec_t locals;
    ptrvec_init(&locals);
    collect_locals(g, fn->b, &locals);
    for (uint64_t i = 0; i < locals.len; i++) {
        const ast_node_t* d = (const ast_node_t*)locals.items[i];
        const str_t name = slot_name(g, d->sym, slot, false);
        slot_add(g, d->sym, name);
        emit_alloca(g, name, d->sym->type);
        slot++;
    }
    ptrvec_free(&locals);
    // A scalar parameter is stored into its slot immediately (item 10).
    for (uint64_t i = 0; i < ast_len(fn); i++) {
        const ast_node_t* p = ast_child(fn, i);
        if (p->kind != AST_PARAM || p->sym == NULL || gen_is_aggregate(p->sym->type)) {
            continue;
        }
        gen_val_t in;
        in.ty = gen_value_type(g, p->sym->type);
        in.val = slot_name(g, p->sym, 0, true);
        gen_store_place(g, gen_slot_place(g, p->sym), in);
    }
    gen_block(g, fn->b);
    if (!g->terminated) {
        if (sig->noreturn) {
            // The block that would fall off the end of a `noreturn` body ends
            // with a trap (D8.5, D19.7).
            gen_use_intrinsic(g, IN_TRAP);
            gen_use_attr(g, ATTR_TRAP);
            sb_append(&g->body, "  call void @llvm.trap()\n  unreachable\n");
        } else if (sig->elem != NULL && sig->elem->kind == TYPE_VOID) {
            sb_append(&g->body, "  ret void\n");
        } else {
            // The checker proved that a non-void body ends in a terminating
            // statement (D8.4), so this block is unreachable.
            sb_append(&g->body, "  unreachable\n");
        }
    }
    function_end(g);
}

// ---- fort_entry (item 22, D11.6) --------------------------------------------------

static void gen_fort_entry(gen_t* g, const sym_t* main_sym) {
    function_begin(g);
    gen_use_attr(g, ATTR_FN);
    definition_begin(g);
    // `fort_entry` is a C name, so it is unquoted (D9.7), and it receives the
    // argument span by hidden pointer (D11.6).
    sb_append(&g->funcs, "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n");
    // The module defines the name from here on, so an `extern fn fort_entry`
    // is not declared beside it: two C declarations of one name are one ELF
    // symbol, and a `declare` beside a `define` is a redefinition (item 8).
    g->entry_defined = true;
    gen_args_t args;
    gen_args_init(&args);
    if (main_sym->type->nparams == 1) {
        // It copies the span into its own frame when `main` declares the
        // parameter, because its caller is the C runtime (item 22).
        gen_val_t slot;
        slot.ty = str_from_cstr("ptr");
        slot.val = str_from_cstr("%args.0");
        sb_append(&g->allocas, "  %args.0 = alloca ");
        sb_append(&g->allocas, SPAN_TYPE);
        sb_append(&g->allocas, ", align ");
        sb_append_u64(&g->allocas, (uint64_t)SPAN_ALIGN);
        sb_push(&g->allocas, '\n');
        gen_val_t incoming;
        incoming.ty = str_from_cstr("ptr");
        incoming.val = str_from_cstr("%args.in");
        gen_memcpy(g, slot, SPAN_ALIGN, incoming, SPAN_ALIGN, SPAN_SIZE);
        gen_args_add(&args, slot);
    }
    const gen_val_t r = gen_temp(g, str_from_cstr("i32"));
    sb_append(&g->body, "call i32 ");
    sb_append_str(&g->body, gen_symbol_ref(g, main_sym));
    sb_push(&g->body, '(');
    sb_append_str(&g->body, sb_view(&args.text));
    sb_append(&g->body, ")\n");
    gen_args_free(&args);
    sb_append(&g->body, "  ret i32 ");
    sb_append_str(&g->body, r.val);
    sb_push(&g->body, '\n');
    function_end(g);
}

// ---- the module (item 1) ----------------------------------------------------------

// The named type of every struct the module declares, in source order, so
// that a field access names a type the module defines (item 2).
//
// This loop, the field index of gen_expr.c and the positional literal there
// must count the same declarations, or a field index names the wrong field
// and every GEP after it reads the wrong bytes. All three take the
// AST_FIELD_DECL children in order and skip on the kind alone; a field with
// no type would leave the three disagreeing, so it ends the compilation here
// rather than shifting an index silently.
static void gen_struct_type(gen_t* g, const ast_node_t* decl) {
    const sym_t* s = decl->sym;
    if (s == NULL || s->error || s->type == NULL || s->type->kind != TYPE_STRUCT) {
        return;
    }
    sb_append_str(&g->named, gen_mem_type(g, s->type));
    sb_append(&g->named, " = type { ");
    uint64_t written = 0;
    for (uint64_t i = 0; i < ast_len(decl); i++) {
        const ast_node_t* f = ast_child(decl, i);
        if (f->kind != AST_FIELD_DECL) {
            continue;
        }
        if (f->type == NULL) {
            // The checker types every field of a struct it lets through, so
            // this is a broken tree and not a program the emitter refuses.
            fatal_internal("gen: a struct field with no type");
        }
        if (written > 0) {
            sb_append(&g->named, ", ");
        }
        written++;
        // Fields in declaration order and never `packed` (D3.8).
        sb_append_str(&g->named, gen_mem_type(g, f->type));
    }
    sb_append(&g->named, " }\n");
}

static void gen_module(gen_t* g, const module_t* m) {
    g->module = m;
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        const ast_node_t* decl = ast_child(m->ast, i);
        if (decl->kind == AST_STRUCT_DECL) {
            gen_struct_type(g, decl);
        }
    }
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        ast_node_t* decl = ast_child(m->ast, i);
        if (decl->kind == AST_VAR_DECL) {
            // Module-level data is T-024's; a program that declares one is
            // refused rather than miscompiled.
            gen_todo(g, decl->loc, "a module-level variable");
            continue;
        }
        if (decl->kind != AST_FN_DECL || decl->b == NULL) {
            continue;
        }
        gen_function(g, decl);
    }
    g->module = NULL;
}

bool gen_program(gen_t* g, const check_t* ck, const module_set_t* set) {
    g->ck = ck;
    g->set = set;
    // The named types every module carries, used or not (item 2).
    sb_append(&g->named, SPAN_TYPE);
    sb_append(&g->named, " = type { ptr, i64 }\n");
    sb_append(&g->named, ENUM_MEMBER_TYPE);
    sb_append(&g->named, " = type { i32, ptr }\n");
    // Modules in dependency order, every module after the ones it imports
    // (D9.10, D19.5).
    for (uint64_t i = 0; i < module_set_count(set); i++) {
        gen_module(g, module_set_at(set, i));
    }
    const module_t* entry = module_set_entry(set);
    if (entry != NULL && entry->ast != NULL) {
        for (uint64_t i = 0; i < ast_len(entry->ast); i++) {
            const ast_node_t* decl = ast_child(entry->ast, i);
            if (decl->kind == AST_FN_DECL && decl->sym != NULL && !decl->sym->error &&
                decl->sym->type != NULL && decl->sym->type->kind == TYPE_FN &&
                str_eq(decl->sym->name, str_from_cstr("main"))) {
                // `fort_entry` is emitted in the entry module (D11.6).
                gen_fort_entry(g, decl->sym);
            }
        }
    }
    gen_finish(g);
    return !g->failed;
}
