// The expressions of the LLVM IR emitter (toolchain.md 6 items 3, 12, 15, 16
// and 19; D19.3, D19.4); see gen.h.
//
// Three entries, because aggregates live in memory (D19.3): gen_expr_value
// for a scalar, gen_expr_place for an lvalue and gen_expr_into for a value
// written into a destination place. A node the checker folded is emitted as a
// literal, since a constant expression has no side effects (D4.6).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "consts.h"
#include "diag.h"
#include "gen.h"
#include "lexer.h"
#include "prim.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// The descriptors the print family writes to (D11.5).
enum { FD_STDOUT = 1, FD_STDERR = 2 };

// The byte `println` and its relatives end with (item 19).
enum { NEWLINE_BYTE = 10 };

// The widths an integer type has, for the overflow intrinsic table (item 15).
enum { BITS_8 = 8, BITS_16 = 16, BITS_32 = 32, BITS_64 = 64 };

// A poisoned or missing type: the checker reported it and nothing is emitted.
static bool bad_type(const type_t* t) {
    return t == NULL || t->kind == TYPE_ERROR;
}

static bool is_bool(const type_t* t) {
    return t != NULL && t->kind == TYPE_PRIM && t->prim == PRIM_BOOL;
}

// A zero value of the type, the operand a negation and a division check need.
static gen_val_t zero_of(gen_t* g, const type_t* t) {
    return gen_const_unsigned(g, gen_value_type(g, t), 0);
}

// ---- constants --------------------------------------------------------------------

// Whether the checker folded the node and its value is a scalar the emitter
// can write as a literal (D4.6). A string constant is an aggregate and has no
// literal form.
static bool folded_scalar(const gen_t* g, const ast_node_t* n) {
    if ((n->ann & CHECK_ANN_CONST) == 0 || gen_is_aggregate(n->type)) {
        return false;
    }
    return check_node_value(g->ck, n).kind != CV_NONE;
}

// ---- the print family (item 19, D12.2) --------------------------------------------

// The bytes and length of a `string` operand: a literal is `@.str.N` and its
// length, and every other string is its two header fields (item 19).
static void string_operand(gen_t* g, ast_node_t* n, gen_val_t* ptr, gen_val_t* len) {
    if ((n->ann & CHECK_ANN_CONST) != 0) {
        const cval_t v = check_node_value(g->ck, n);
        if (v.kind == CV_STR) {
            *ptr = gen_literal(g, str_from_cstr("ptr"), gen_str_ref(g, v.str).ptr);
            *len = gen_const_unsigned(g, str_from_cstr("i64"), v.str.len);
            return;
        }
    }
    const gen_place_t p = gen_expr_place(g, n);
    if (g->failed) {
        *ptr = gen_literal(g, str_from_cstr("ptr"), "null");
        *len = gen_const_unsigned(g, str_from_cstr("i64"), 0);
        return;
    }
    *ptr = gen_span_ptr(g, p.addr);
    *len = gen_span_len(g, p.addr);
}

// One argument of the print family: one call per argument, per type (item
// 19).
static void gen_print_arg(gen_t* g, ast_node_t* n, gen_val_t fd) {
    const type_t* t = n->type;
    if (bad_type(t)) {
        return;
    }
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args, fd);
    if (t->kind == TYPE_STRING) {
        gen_val_t ptr;
        gen_val_t len;
        string_operand(g, n, &ptr, &len);
        gen_args_add(&args, ptr);
        gen_args_add(&args, len);
        gen_call_rt(g, RT_PRINT_STR, &args);
        gen_args_free(&args);
        return;
    }
    const gen_val_t v = gen_expr_value(g, n);
    if (g->failed) {
        gen_args_free(&args);
        return;
    }
    if (t->kind == TYPE_ENUM) {
        // An enum prints as `(i32 %v, ptr @.enum.<path.name>, i64 <count>)`
        // (item 19, item 21).
        const sym_t* e = (const sym_t*)t->decl;
        gen_args_add(&args, v);
        gen_args_add(&args, gen_literal(g, str_from_cstr("ptr"), gen_enum_ref(g, e).ptr));
        gen_args_add(&args, gen_const_unsigned(g, str_from_cstr("i64"), gen_enum_count(e)));
        gen_call_rt(g, RT_PRINT_ENUM, &args);
        gen_args_free(&args);
        return;
    }
    if (t->kind == TYPE_PTR || t->kind == TYPE_VOIDPTR || t->kind == TYPE_FN) {
        // A pointer, `void*` or function pointer goes to `_ptr` (item 19).
        gen_args_add(&args, v);
        gen_call_rt(g, RT_PRINT_PTR, &args);
        gen_args_free(&args);
        return;
    }
    if (is_bool(t)) {
        // A `bool` is `zext`ed from `i1` to `i8` (item 19).
        gen_args_add_ext(&args, gen_cast_op(g, "zext", v, str_from_cstr("i8")), "zeroext");
        gen_call_rt(g, RT_PRINT_BOOL, &args);
        gen_args_free(&args);
        return;
    }
    if (t->kind == TYPE_PRIM && t->prim == PRIM_CHAR) {
        gen_args_add_ext(&args, v, "zeroext");
        gen_call_rt(g, RT_PRINT_CHAR, &args);
        gen_args_free(&args);
        return;
    }
    // `i8 i16 i32 i64` are sign-extended to `i64` and `u8 u16 u32 u64`
    // zero-extended to it (item 19).
    const bool sign = gen_is_signed(t);
    gen_args_add(&args,
                 gen_resize(g, v, gen_int_bits(t), str_from_cstr("i64"), (uint32_t)BITS_64, sign));
    gen_call_rt(g, sign ? RT_PRINT_I64 : RT_PRINT_U64, &args);
    gen_args_free(&args);
}

// The print family: `fd` once, then each argument left to right, one call per
// argument (D6.3, D12.2).
static void gen_print(gen_t* g, ast_node_t* n, gen_val_t fd, uint64_t first, bool newline) {
    for (uint64_t i = first; i < ast_len(n); i++) {
        gen_print_arg(g, ast_child(n, i), fd);
        if (g->failed) {
            return;
        }
    }
    if (!newline) {
        return;
    }
    // `println` and its relatives end with a newline byte (item 19).
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args, fd);
    gen_args_add_ext(
        &args, gen_const_unsigned(g, str_from_cstr("i8"), (uint64_t)NEWLINE_BYTE), "zeroext");
    gen_call_rt(g, RT_PRINT_CHAR, &args);
    gen_args_free(&args);
}

// The byte offset of a position in the module's source, so that `assert` can
// quote its argument verbatim (D11.4).
static uint64_t source_offset(str_t src, uint32_t line, uint32_t col) {
    uint64_t at = 0;
    uint32_t here = 1;
    while (here < line && at < src.len) {
        if (src.ptr[at] == '\n') {
            here++;
        }
        at++;
    }
    at += (uint64_t)col - 1;
    return at < src.len ? at : src.len;
}

// The source text of the one argument of `assert`: the call's range runs from
// its `(` to one past its `)` (D20.4), so the text between them is the
// argument as the user wrote it (D11.4).
static str_t assert_text(gen_t* g, const ast_node_t* call) {
    if (g->module == NULL) {
        return str_from_range(NULL, 0);
    }
    const str_t src = g->module->source;
    const uint64_t open = source_offset(src, call->loc.line, call->loc.col);
    const uint64_t close = source_offset(src, call->loc.end_line, call->loc.end_col);
    if (close < open + 2) {
        return str_from_range(NULL, 0);
    }
    return str_from_range(src.ptr + open + 1, close - open - 2);
}

// `assert(cond)` is active in both build modes and branches to the
// continuation first, since its operand is already the success condition
// (D12.2, item 19).
static void gen_assert(gen_t* g, ast_node_t* n) {
    ast_node_t* arg = ast_child(n, 0);
    const gen_val_t cond = gen_expr_value(g, arg);
    if (g->failed) {
        return;
    }
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args,
                 gen_literal(g, str_from_cstr("ptr"), gen_str_ref(g, assert_text(g, n)).ptr));
    // The column of `assert` is that of the builtin's name (D11.4).
    gen_check(g, cond, false, RT_ASSERT_FAIL, &args, n->a->loc);
    gen_args_free(&args);
}

// `panic(msg)` writes the message and aborts, so the call is followed by
// `unreachable` (D11.4, item 19).
static void gen_panic(gen_t* g, ast_node_t* n) {
    gen_val_t ptr;
    gen_val_t len;
    string_operand(g, ast_child(n, 0), &ptr, &len);
    if (g->failed) {
        return;
    }
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args, ptr);
    gen_args_add(&args, len);
    gen_args_add_loc(g, &args, n->a->loc);
    gen_call_rt(g, RT_PANIC, &args);
    gen_args_free(&args);
    gen_ins(g);
    gen_text_append(g, "unreachable");
    gen_ins_end(g);
    g->terminated = true;
}

// ---- new and del (item 17, D10.2, D10.3) -------------------------------------------

// Whether `n` designates storage, which is what decides whether `del` empties
// its operand (D6.7, D17.9): an `own` rvalue -- `new(...)`, a call result, a
// `cast` that adopts -- is freed and nothing is stored.
static bool is_place_expr(const ast_node_t* n) {
    switch (n->kind) {
    case AST_IDENT:
    case AST_INDEX:
    case AST_FIELD:
    case AST_ARROW:
        return true;
    case AST_UNARY:
        // `*p` designates the storage the pointer reaches (D6.7).
        return n->op == TOK_STAR;
    default:
        break;
    }
    return false;
}

// `del(x)` (item 17): the pointer is loaded (field 0 for a span or `string`),
// `fort_rt_del` frees it, and an lvalue operand is emptied -- `store ptr null`
// for a pointer, a 16-byte `llvm.memset` for a span or `string` (D17.9). A
// null operand is a no-op in the runtime, so `del(null)` and `del` of a zero
// span need no test of their own (D17.9). The emptying is not an assignment
// and never carries the overwrite check of D17.11, which is what makes
// `del(v); v = new(...)` pass it (item 18).
static void gen_del(gen_t* g, ast_node_t* n) {
    if (ast_len(n) != 1) {
        // The checker reported the arity and the module is not emitted.
        return;
    }
    ast_node_t* arg = ast_child(n, 0);
    const type_t* t = arg->type;
    if (bad_type(t)) {
        return;
    }
    const bool lvalue = is_place_expr(arg);
    // A span or `string` operand is an aggregate, so it always has a place:
    // an rvalue one lands in a temporary the copy is made in (D19.3).
    const bool header = gen_is_aggregate(t);
    gen_place_t place;
    place.addr = gen_literal(g, str_from_cstr("ptr"), "null");
    place.type = t;
    gen_val_t p;
    if (header || lvalue) {
        place = gen_expr_place(g, arg);
        if (g->failed) {
            return;
        }
        p = header ? gen_span_ptr(g, place.addr) : gen_load_place(g, place);
    } else {
        p = gen_expr_value(g, arg);
        if (g->failed) {
            return;
        }
    }
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args, p);
    gen_call_rt(g, RT_DEL, &args);
    gen_args_free(&args);
    if (!lvalue) {
        // On an rvalue nothing is stored (item 17).
        return;
    }
    if (header) {
        gen_memset_zero(g, place.addr, type_alignof(t), type_sizeof(t));
        return;
    }
    gen_store_place(g, place, gen_literal(g, str_from_cstr("ptr"), "null"));
}

// The count of `new(T, n)` as an `i64`, with the check of D10.2: a negative
// count is a runtime error reported with its signed value at the builtin's
// name (D11.4). It is not a bounds check, so neither `--release` nor
// `--no-bounds-check` removes it (D10.6).
static gen_val_t alloc_count(gen_t* g, ast_node_t* n, ast_node_t* count) {
    const str_t i64ty = str_from_cstr("i64");
    const gen_val_t raw = gen_expr_value(g, count);
    if (g->failed) {
        return raw;
    }
    const bool sign = gen_is_signed(count->type);
    const gen_val_t v =
        gen_resize(g, raw, gen_int_bits(count->type), i64ty, (uint32_t)BITS_64, sign);
    if (!sign) {
        // An unsigned count is never negative, so it takes no branch.
        return v;
    }
    const gen_val_t bad = gen_icmp(g, "slt", v, gen_const_unsigned(g, i64ty, 0));
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args, v);
    gen_check(g, bad, true, RT_FAIL_ALLOC_COUNT, &args, n->loc);
    gen_args_free(&args);
    return v;
}

// `fort_rt_new(sizeof(T), count, loc)` (item 17): the runtime zeroes the
// storage and never returns null, `n == 0` included (D10.2), and an
// overflowing size or a failed allocation is its runtime error, so the module
// emits no test of the result.
static gen_val_t alloc_call(gen_t* g, ast_node_t* n, const type_t* elem, gen_val_t count) {
    const str_t i64ty = str_from_cstr("i64");
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args, gen_const_unsigned(g, i64ty, type_sizeof(elem)));
    gen_args_add(&args, count);
    gen_args_add_loc(g, &args, n->loc);
    const gen_val_t p = gen_call_rt_value(g, RT_NEW, str_from_cstr("ptr"), &args);
    gen_args_free(&args);
    return p;
}

// `new(T)`: one zero-initialized `T`, whose address is an ordinary scalar
// (D10.2, D17.3).
static gen_val_t gen_new_object(gen_t* g, ast_node_t* n) {
    const gen_val_t null = gen_literal(g, str_from_cstr("ptr"), "null");
    if (n->b != NULL || n->type == NULL || n->type->kind != TYPE_PTR) {
        // `new(T, n)` is a span and is written into a place; only the checker
        // could produce another shape, and it reports one.
        gen_todo(g, n->loc, "this allocation");
        return null;
    }
    return alloc_call(g, n, n->type->elem, gen_const_unsigned(g, str_from_cstr("i64"), 1));
}

// `new(T, n)`: `n` zero-initialized elements, whose header is written into the
// destination place field by field (item 17, D10.2, D17.3).
static void gen_new_span(gen_t* g, ast_node_t* n, gen_place_t dst) {
    if (n->b == NULL || n->type == NULL || n->type->kind != TYPE_SPAN) {
        gen_todo(g, n->loc, "this allocation");
        return;
    }
    const gen_val_t count = alloc_count(g, n, n->b);
    if (g->failed) {
        return;
    }
    const gen_val_t p = alloc_call(g, n, n->type->elem, count);
    gen_span_init(g, dst.addr, p, count);
}

bool gen_is_move(const ast_node_t* n) {
    const sym_t* s = n->kind == AST_CALL && n->a != NULL ? n->a->sym : NULL;
    return s != NULL && s->kind == SYM_BUILTIN && str_eq(s->name, str_from_cstr("move"));
}

// `move(lv)` (item 18, D17.6): the operand's value is read into a place of the
// emitter's own, the operand is then left at its zero value, and only then is
// the value handed on. `dst` is the place an aggregate result is written into
// and is NULL for a scalar one and on the discard path.
//
// The intermediate is not an optimization to drop: `dst` may be the operand
// itself, and nothing here can tell. `s = move(s)`, `*p = move(*q)` and
// `v[i] = move(v[j])` all reach this with two places that may be one address,
// and copying into `dst` first would let the zeroing that follows destroy the
// value D17.6 says the move yields. A scalar's intermediate is the register
// the `load` names; an aggregate's is a `%tmpK` slot (D19.5). The emptying is
// not an assignment, so it carries no overwrite check (item 18, D17.11).
static gen_val_t gen_move(gen_t* g, ast_node_t* n, const gen_place_t* dst) {
    const gen_val_t none = gen_literal(g, str_from_cstr("void"), "");
    if (ast_len(n) != 1 || bad_type(ast_child(n, 0)->type)) {
        // The checker reported the arity or the operand, so the module is not
        // emitted.
        return none;
    }
    ast_node_t* arg = ast_child(n, 0);
    const gen_place_t src = gen_expr_place(g, arg);
    if (g->failed) {
        return none;
    }
    if (gen_is_aggregate(arg->type)) {
        const uint64_t align = type_alignof(arg->type);
        const uint64_t size = type_sizeof(arg->type);
        const gen_place_t held = gen_temp_place(g, arg->type);
        gen_memcpy(g, held.addr, align, src.addr, align, size);
        gen_zero_owner(g, src);
        if (dst != NULL) {
            gen_memcpy(g, dst->addr, align, held.addr, align, size);
        }
        return none;
    }
    const gen_val_t v = gen_load_place(g, src);
    gen_zero_owner(g, src);
    return v;
}

bool gen_builtin_call(gen_t* g, ast_node_t* n) {
    const sym_t* s = n->a != NULL ? n->a->sym : NULL;
    if (s == NULL || s->kind != SYM_BUILTIN) {
        return false;
    }
    const str_t name = s->name;
    const bool out = str_eq(name, str_from_cstr("print"));
    const bool outln = str_eq(name, str_from_cstr("println"));
    const bool err = str_eq(name, str_from_cstr("eprint"));
    const bool errln = str_eq(name, str_from_cstr("eprintln"));
    if (out || outln || err || errln) {
        // `print` and `println` write to stdout, `eprint` and `eprintln` to
        // stderr (D11.5).
        const gen_val_t fd = gen_const_unsigned(
            g, str_from_cstr("i32"), (uint64_t)(out || outln ? FD_STDOUT : FD_STDERR));
        gen_print(g, n, fd, 0, outln || errln);
        return true;
    }
    const bool fout = str_eq(name, str_from_cstr("fprint"));
    const bool foutln = str_eq(name, str_from_cstr("fprintln"));
    if (fout || foutln) {
        // `fd` is evaluated once, before the arguments (D6.3, D12.2).
        const gen_val_t fd = gen_expr_value(g, ast_child(n, 0));
        if (!g->failed) {
            gen_print(g, n, fd, 1, foutln);
        }
        return true;
    }
    if (str_eq(name, str_from_cstr("assert"))) {
        gen_assert(g, n);
        return true;
    }
    if (str_eq(name, str_from_cstr("panic"))) {
        gen_panic(g, n);
        return true;
    }
    if (str_eq(name, str_from_cstr("del"))) {
        gen_del(g, n);
        return true;
    }
    // The value of a discarded `move` has nowhere to go, which the checker
    // refuses (D17.8), so the effect alone is emitted here.
    (void)gen_move(g, n, NULL);
    return true;
}

// ---- calls (item 7) ---------------------------------------------------------------

// A function name used as a value is its address: a function pointer is an
// ordinary `ptr` value, and `&f` and `*f` are errors, so the name alone
// denotes it (D3.10). A qualified name `m.f` is the same value, since the
// symbol a field node resolved to is the function itself (D9.4).
static bool function_ref(gen_t* g, ast_node_t* n, gen_val_t* out) {
    const sym_t* s = n->sym;
    // Only a fort function: an `extern fn` in value position is a checker
    // error, since an indirect call site cannot take the variadic form its
    // declaration supplies (D3.10, D9.8).
    if (s == NULL || s->kind != SYM_FN) {
        return false;
    }
    out->ty = str_from_cstr("ptr");
    out->val = gen_symbol_ref(g, s);
    return true;
}

// One argument of a fort or `extern` call: a scalar is an ordinary parameter,
// and an aggregate is a plain `ptr` to a copy the caller allocates in its
// entry block and memcpys into (item 7).
static void gen_call_arg(gen_t* g, gen_args_t* args, ast_node_t* arg) {
    const type_t* t = arg->type;
    if (gen_is_aggregate(t)) {
        const gen_place_t copy = gen_temp_place(g, t);
        gen_expr_into(g, arg, copy);
        gen_args_add(args, copy.addr);
        return;
    }
    gen_args_add_ext(args, gen_expr_value(g, arg), gen_ext_attr(t));
}

// The parameter types of a call-site function type, `(i32, ptr)`: an
// aggregate parameter is a plain `ptr` and an aggregate result adds the
// leading pointer of item 7. An extern call ends the list with the variadic
// tail of item 8; a call through a function pointer never does, since D3.10
// has no variadic function type.
static void gen_call_type(gen_t* g, const type_t* sig, bool variadic) {
    gen_text_append(g, "(");
    uint64_t written = 0;
    if (gen_is_aggregate(sig->elem)) {
        // An aggregate result is a leading `ptr` parameter (item 7).
        gen_text_append(g, "ptr");
        written++;
    }
    for (uint32_t i = 0; i < sig->nparams; i++) {
        if (written > 0) {
            gen_text_append(g, ", ");
        }
        written++;
        const type_t* pt = sig->params[i];
        // A struct, fixed array, span or `string` parameter is a plain `ptr`
        // (item 7).
        gen_text_append_str(g, gen_is_aggregate(pt) ? str_from_cstr("ptr") : gen_value_type(g, pt));
    }
    if (variadic) {
        if (written > 0) {
            gen_text_append(g, ", ");
        }
        gen_text_append(g, "...");
    }
    gen_text_append(g, ") ");
}

// The call of a fort function, an `extern` function or a function pointer;
// `dst` is the place an aggregate result is written into and is NULL for a
// scalar or void result.
static gen_val_t gen_call(gen_t* g, ast_node_t* n, const gen_place_t* dst) {
    gen_val_t none = gen_literal(g, str_from_cstr("void"), "");
    const sym_t* s = n->a != NULL ? n->a->sym : NULL;
    // A function name, a qualified name and a function-pointer-typed
    // expression are all callable (D6.11); only the first two name a symbol,
    // and the third is an ordinary `ptr` value (D3.10).
    const bool direct = s != NULL && (s->kind == SYM_FN || s->kind == SYM_EXTERN_FN);
    const type_t* sig = NULL;
    if (direct) {
        sig = s->type;
    } else if (n->a != NULL) {
        sig = n->a->type;
    }
    if (sig == NULL || sig->kind != TYPE_FN) {
        // Only the checker can produce a callee that is not a function, and
        // it reports one, so this is unreachable; an unfinished path is a
        // diagnostic and never wrong code.
        gen_todo(g, n->loc, "this callee");
        return none;
    }
    const bool is_extern = direct && s->kind == SYM_EXTERN_FN;
    // An `extern fn` naming a runtime entry point takes that group's
    // prototype, variadic tail included, so it is called through it and not
    // through the variadic type of D9.8 (item 8).
    const bool variadic = is_extern && gen_runtime_entry(s->name) == RT_COUNT;
    if (is_extern) {
        gen_use_extern(g, s);
    }
    if (gen_is_aggregate(sig->elem) && dst == NULL) {
        // An aggregate result needs the place it is written into (item 7);
        // reaching this without one would drop the `sret` argument.
        gen_todo(g, n->loc, "an aggregate result read without a destination");
        return none;
    }
    gen_val_t callee = none;
    if (direct) {
        callee.ty = str_from_cstr("ptr");
        callee.val = gen_symbol_ref(g, s);
    } else {
        // The callee is evaluated before the arguments, which is the source
        // order the walk keeps (D6.3).
        callee = gen_expr_value(g, n->a);
        if (g->failed) {
            return none;
        }
    }
    gen_args_t args;
    gen_args_init(&args);
    if (dst != NULL) {
        // An aggregate result arrives through the leading `sret` pointer
        // (item 7).
        gen_args_add(&args, dst->addr);
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        gen_call_arg(g, &args, ast_child(n, i));
        if (g->failed) {
            gen_args_free(&args);
            return none;
        }
    }
    const str_t ret = gen_result_type(g, sig->elem);
    const bool has_value = !str_eq(ret, str_from_cstr("void"));
    gen_val_t r = none;
    if (has_value) {
        r = gen_temp(g, ret);
    } else {
        gen_ins(g);
    }
    gen_text_append(g, "call ");
    const char* ret_attr = gen_ext_attr(sig->elem);
    if (ret_attr != NULL) {
        gen_text_append(g, ret_attr);
        gen_text_append(g, " ");
    }
    gen_text_append_str(g, ret);
    gen_text_append(g, " ");
    if (variadic || !direct) {
        // An extern call goes through the matching variadic call type, which
        // is what makes the vector-register count right (item 8, D9.8), and a
        // call through a function pointer carries the function type because
        // an opaque pointer carries none (D3.10, D19.2).
        gen_call_type(g, sig, variadic);
    }
    gen_text_append_str(g, callee.val);
    gen_text_append(g, "(");
    gen_text_append_str(g, sb_view(&args.text));
    gen_text_append(g, ")");
    if (variadic) {
        // `#3 = { nobuiltin }` on every extern call site (item 8); a runtime
        // entry point is not one, and its group carries no attribute.
        gen_use_attr(g, ATTR_NOBUILTIN);
        gen_text_append(g, " #3");
    }
    gen_ins_end(g);
    gen_args_free(&args);
    if (sig->noreturn) {
        // Every call site of a `noreturn` function ends with the trap D8.5
        // requires, an `extern` one included: its declaration is unadorned so
        // that the optimizer cannot delete this trap, which is what catches an
        // extern that returns anyway (item 20, D19.7).
        gen_use_intrinsic(g, IN_TRAP);
        gen_use_attr(g, ATTR_TRAP);
        gen_ins(g);
        gen_text_append(g, "call void @llvm.trap()");
        gen_ins_end(g);
        gen_ins(g);
        gen_text_append(g, "unreachable");
        gen_ins_end(g);
        g->terminated = true;
    }
    return r;
}

// ---- arithmetic and its checks (item 15) ------------------------------------------

// The overflow intrinsic of an operation at a width: the family in the order
// `sadd ssub smul uadd usub umul` and, within each, `i8 i16 i32 i64` (item
// 15).
static uint64_t overflow_slot(gen_overflow_t which, uint32_t bits) {
    uint64_t width = 0;
    if (bits == (uint32_t)BITS_16) {
        width = 1;
    } else if (bits == (uint32_t)BITS_32) {
        width = 2;
    } else if (bits == (uint32_t)BITS_64) {
        width = 3;
    }
    return (uint64_t)IN_OVERFLOW_FIRST + (uint64_t)which * (uint64_t)GEN_OVF_WIDTHS + width;
}

static const char* overflow_name(gen_overflow_t which) {
    switch (which) {
    case GEN_OVF_SADD:
        return "sadd";
    case GEN_OVF_SSUB:
        return "ssub";
    case GEN_OVF_SMUL:
        return "smul";
    case GEN_OVF_UADD:
        return "uadd";
    case GEN_OVF_USUB:
        return "usub";
    case GEN_OVF_UMUL:
    default:
        break;
    }
    return "umul";
}

// One checked operation: the intrinsic, the two `extractvalue`s of the
// `{iN, i1}` it returns, and the branch into a failure block that calls
// fort_rt_fail_overflow (item 15, D19.6).
static gen_val_t gen_checked(gen_t* g, gen_overflow_t which, gen_val_t a, gen_val_t b, loc_t loc) {
    sb_clear(&g->scratch);
    sb_append(&g->scratch, "{ ");
    sb_append_str(&g->scratch, a.ty);
    sb_append(&g->scratch, ", i1 }");
    const str_t pair = gen_take(g);
    const gen_val_t call = gen_temp(g, pair);
    gen_text_append(g, "call ");
    gen_text_append_str(g, pair);
    gen_text_append(g, " @llvm.");
    gen_text_append(g, overflow_name(which));
    gen_text_append(g, ".with.overflow.");
    gen_text_append_str(g, a.ty);
    gen_text_append(g, "(");
    gen_text_append_str(g, a.ty);
    gen_text_append(g, " ");
    gen_text_append_str(g, a.val);
    gen_text_append(g, ", ");
    gen_text_append_str(g, a.ty);
    gen_text_append(g, " ");
    gen_text_append_str(g, b.val);
    gen_text_append(g, ")");
    gen_ins_end(g);
    const gen_val_t value = gen_temp(g, a.ty);
    gen_text_append(g, "extractvalue ");
    gen_text_append_str(g, pair);
    gen_text_append(g, " ");
    gen_text_append_str(g, call.val);
    gen_text_append(g, ", 0");
    gen_ins_end(g);
    const gen_val_t bad = gen_temp(g, str_from_cstr("i1"));
    gen_text_append(g, "extractvalue ");
    gen_text_append_str(g, pair);
    gen_text_append(g, " ");
    gen_text_append_str(g, call.val);
    gen_text_append(g, ", 1");
    gen_ins_end(g);
    gen_args_t args;
    gen_args_init(&args);
    gen_check(g, bad, true, RT_FAIL_OVERFLOW, &args, loc);
    gen_args_free(&args);
    return value;
}

// The shift count of item 15: materialized at 64 bits so that a negative
// count is reported with its signed value, checked against the operand's
// width, then truncated to the operand's type, so a shift never produces
// poison (D6.2, D11.1).
static gen_val_t gen_shift_count(
    gen_t* g, loc_t loc, const type_t* at, const type_t* bt, gen_val_t b, uint32_t bits) {
    const str_t i64ty = str_from_cstr("i64");
    gen_val_t wide =
        gen_resize(g, b, gen_int_bits(bt), i64ty, (uint32_t)BITS_64, gen_is_signed(bt));
    if (g->opts.release) {
        // Release mode takes the count modulo the width (D11.1).
        const gen_val_t mask = gen_const_unsigned(g, i64ty, (uint64_t)bits - 1U);
        wide = gen_binary(g, "and", wide, mask);
    } else {
        const gen_val_t limit = gen_const_unsigned(g, i64ty, bits);
        const gen_val_t bad = gen_icmp(g, "uge", wide, limit);
        gen_args_t args;
        gen_args_init(&args);
        gen_args_add(&args, wide);
        // `@.str.T` is the shifted operand's type name (item 15).
        const char* type_name = at->kind == TYPE_PRIM ? prim_name(at->prim) : "integer";
        gen_args_add(
            &args,
            gen_literal(g, str_from_cstr("ptr"), gen_str_ref(g, str_from_cstr(type_name)).ptr));
        gen_check(g, bad, true, RT_FAIL_SHIFT, &args, loc);
        gen_args_free(&args);
    }
    return gen_resize(g, wide, (uint32_t)BITS_64, gen_value_type(g, at), bits, false);
}

// Division and remainder, checked in both build modes (D6.13, D11.3).
static gen_val_t gen_divide(
    gen_t* g, loc_t loc, int32_t op, const type_t* t, gen_val_t a, gen_val_t b) {
    const bool sign = gen_is_signed(t);
    const uint32_t bits = gen_int_bits(t);
    const gen_val_t zero = zero_of(g, t);
    gen_val_t bad = gen_icmp(g, "eq", b, zero);
    gen_args_t args;
    gen_args_init(&args);
    gen_check(g, bad, true, RT_FAIL_DIV_ZERO, &args, loc);
    gen_args_free(&args);
    if (sign) {
        // `MIN / -1` and `MIN % -1` are runtime errors at every width (D6.13).
        const gen_val_t minus_one = gen_const_signed(g, a.ty, -1);
        const gen_val_t smallest = gen_const_min(g, a.ty, bits);
        const gen_val_t is_minus_one = gen_icmp(g, "eq", b, minus_one);
        const gen_val_t is_min = gen_icmp(g, "eq", a, smallest);
        bad = gen_binary(g, "and", is_minus_one, is_min);
        gen_args_t over;
        gen_args_init(&over);
        gen_check(g, bad, true, RT_FAIL_DIV_OVERFLOW, &over, loc);
        gen_args_free(&over);
    }
    if (op == TOK_PERCENT) {
        return gen_binary(g, sign ? "srem" : "urem", a, b);
    }
    return gen_binary(g, sign ? "sdiv" : "udiv", a, b);
}

gen_val_t gen_arith(
    gen_t* g, loc_t loc, int32_t op, const type_t* at, gen_val_t a, const type_t* bt, gen_val_t b) {
    const bool sign = gen_is_signed(at);
    const uint32_t bits = gen_int_bits(at);
    switch (op) {
    case TOK_PLUS:
    case TOK_MINUS:
    case TOK_STAR: {
        if (g->opts.release) {
            // Release mode emits plain `add`, `sub` and `mul`; `nsw` and
            // `nuw` are never emitted, since wrapping is defined (D11.1, D16).
            const char* plain = "mul";
            if (op == TOK_PLUS) {
                plain = "add";
            } else if (op == TOK_MINUS) {
                plain = "sub";
            }
            return gen_binary(g, plain, a, b);
        }
        gen_overflow_t which = GEN_OVF_UADD;
        if (op == TOK_PLUS) {
            which = sign ? GEN_OVF_SADD : GEN_OVF_UADD;
        } else if (op == TOK_MINUS) {
            which = sign ? GEN_OVF_SSUB : GEN_OVF_USUB;
        } else {
            which = sign ? GEN_OVF_SMUL : GEN_OVF_UMUL;
        }
        gen_use_intrinsic(g, (gen_intrinsic_t)overflow_slot(which, bits));
        gen_use_attr(g, ATTR_OVERFLOW);
        return gen_checked(g, which, a, b, loc);
    }
    case TOK_PLUS_WRAP:
        // `+% -% *%` wrap in both modes (D11.2).
        return gen_binary(g, "add", a, b);
    case TOK_MINUS_WRAP:
        return gen_binary(g, "sub", a, b);
    case TOK_STAR_WRAP:
        return gen_binary(g, "mul", a, b);
    case TOK_SLASH:
    case TOK_PERCENT:
        return gen_divide(g, loc, op, at, a, b);
    case TOK_AMP:
        return gen_binary(g, "and", a, b);
    case TOK_PIPE:
        return gen_binary(g, "or", a, b);
    case TOK_CARET:
        return gen_binary(g, "xor", a, b);
    case TOK_SHL:
    case TOK_SHR: {
        const gen_val_t count = gen_shift_count(g, loc, at, bt, b, bits);
        if (op == TOK_SHL) {
            // `<<` discards the bits shifted out and never checks for
            // overflow (D6.2).
            return gen_binary(g, "shl", a, count);
        }
        // `>>` is arithmetic for a signed operand and logical for an
        // unsigned one (D6.2).
        return gen_binary(g, sign ? "ashr" : "lshr", a, count);
    }
    default:
        break;
    }
    gen_todo(g, loc, "this operator");
    return a;
}

// ---- comparisons (D6.2) -----------------------------------------------------------

// The `icmp` predicate of a relational operator on a type: signedness is in
// the instruction and never in the type (D3.1).
static const char* compare_pred(int32_t op, bool sign) {
    switch (op) {
    case TOK_EQ:
        return "eq";
    case TOK_NE:
        return "ne";
    case TOK_LT:
        return sign ? "slt" : "ult";
    case TOK_LE:
        return sign ? "sle" : "ule";
    case TOK_GT:
        return sign ? "sgt" : "ugt";
    case TOK_GE:
    default:
        break;
    }
    return sign ? "sge" : "uge";
}

// `==` and `!=` on strings (D3.7): the runtime compares the lengths and then
// the bytes, so the zero string equals `""`. The comparison is a runtime entry
// point because the compiler emits no call to a C library symbol of its own
// accord, memcmp included (D9.8).
static gen_val_t gen_string_compare(gen_t* g, ast_node_t* n) {
    gen_val_t aptr;
    gen_val_t alen;
    string_operand(g, n->a, &aptr, &alen);
    gen_val_t bptr;
    gen_val_t blen;
    string_operand(g, n->b, &bptr, &blen);
    if (g->failed) {
        return gen_literal(g, str_from_cstr("i1"), "false");
    }
    gen_args_t args;
    gen_args_init(&args);
    gen_args_add(&args, aptr);
    gen_args_add(&args, alen);
    gen_args_add(&args, bptr);
    gen_args_add(&args, blen);
    // The entry point answers in a `u8`, which is `i8 zeroext` at the
    // boundary and `i1` as a value (D9.9, D19.2).
    const gen_val_t r = gen_call_rt_value(g, RT_STR_EQ, str_from_cstr("i8"), &args);
    gen_args_free(&args);
    const gen_val_t eq = gen_cast_op(g, "trunc", r, str_from_cstr("i1"));
    if (n->op == TOK_EQ) {
        return eq;
    }
    if (n->op != TOK_NE) {
        // Only `==` and `!=` compare strings; the checker rejects an ordering
        // operator on them (D3.7), so this is unreachable and an unfinished
        // path is a diagnostic rather than a wrong default.
        gen_todo(g, n->loc, "this comparison of strings");
        return eq;
    }
    return gen_binary(g, "xor", eq, gen_literal(g, str_from_cstr("i1"), "true"));
}

static gen_val_t gen_compare(gen_t* g, ast_node_t* n) {
    const type_t* t = n->a->type;
    if (t != NULL && t->kind == TYPE_STRING) {
        return gen_string_compare(g, n);
    }
    const gen_val_t a = gen_expr_value(g, n->a);
    const gen_val_t b = gen_expr_value(g, n->b);
    if (g->failed) {
        return gen_literal(g, str_from_cstr("i1"), "false");
    }
    return gen_icmp(g, compare_pred(n->op, gen_is_signed(t)), a, b);
}

// `&&` and `||` short-circuit through a stack slot rather than a `phi`, so
// the tree walk never has to know its predecessors (item 10, D19.4).
static gen_val_t gen_short_circuit(gen_t* g, ast_node_t* n) {
    const gen_place_t slot = gen_temp_place(g, n->type);
    const gen_val_t lhs = gen_expr_value(g, n->a);
    if (g->failed) {
        return lhs;
    }
    gen_store_place(g, slot, lhs);
    const uint64_t rhs_block = gen_label(g);
    const uint64_t done = gen_label(g);
    if (n->op == TOK_AND_AND) {
        gen_br_cond(g, lhs, rhs_block, done);
    } else {
        gen_br_cond(g, lhs, done, rhs_block);
    }
    gen_block_begin(g, rhs_block);
    const gen_val_t rhs = gen_expr_value(g, n->b);
    if (g->failed) {
        return rhs;
    }
    gen_store_place(g, slot, rhs);
    gen_br(g, done);
    gen_block_begin(g, done);
    return gen_load_place(g, slot);
}

// ---- casts (item 12, D3.14) -------------------------------------------------------

static gen_val_t gen_cast(gen_t* g, ast_node_t* n) {
    const type_t* to = n->type;
    const type_t* from = n->a->type;
    const gen_val_t v = gen_expr_value(g, n->a);
    if (g->failed || bad_type(to) || bad_type(from)) {
        return v;
    }
    const bool to_ptr = to->kind == TYPE_PTR || to->kind == TYPE_VOIDPTR || to->kind == TYPE_FN;
    const bool from_ptr =
        from->kind == TYPE_PTR || from->kind == TYPE_VOIDPTR || from->kind == TYPE_FN;
    if (to_ptr && from_ptr) {
        // Nothing at all for pointer to pointer, and for casts that only drop
        // mutability or ownership (item 12, D5.4, D17.4).
        return v;
    }
    if (to_ptr) {
        return gen_cast_op(g, "inttoptr", v, gen_value_type(g, to));
    }
    if (from_ptr) {
        return gen_cast_op(g, "ptrtoint", v, gen_value_type(g, to));
    }
    const uint32_t from_bits = gen_int_bits(from);
    const uint32_t to_bits = gen_int_bits(to);
    if (from_bits == 0 || to_bits == 0) {
        gen_todo(g, n->loc, "this cast");
        return v;
    }
    if (is_bool(from)) {
        // `zext i1` for `bool` to an integer, and nothing at all for
        // `cast(b, bool)`, which is the identity of D3.14 and would otherwise
        // be a `zext i1` to `i1` (item 12).
        return gen_resize(g, v, from_bits, gen_value_type(g, to), to_bits, false);
    }
    // No row of D3.14 converts anything but a `bool` to `bool`: an integer to
    // `bool` is a checker error (D3.3) and so is every other source type, so
    // a `bool` target is the identity above and nothing else.
    // Integer to integer widens by the source's signedness (item 12).
    return gen_resize(g, v, from_bits, gen_value_type(g, to), to_bits, gen_is_signed(from));
}

// ---- places (item 3) --------------------------------------------------------------

gen_val_t gen_length_of(gen_t* g, const type_t* t, gen_val_t base) {
    if (t->kind == TYPE_ARRAY) {
        // A fixed array's length is an `i64` literal (item 16).
        return gen_const_unsigned(g, str_from_cstr("i64"), t->len);
    }
    return gen_span_len(g, base);
}

const type_t* gen_element_type(gen_t* g, const type_t* t) {
    if (t == NULL) {
        return NULL;
    }
    if (t->kind == TYPE_STRING) {
        // A `string` is an immutable span of `char` whose type carries no
        // element, so the emitter's own node stands for it (D3.7).
        return &g->char_type;
    }
    return t->elem;
}

gen_val_t gen_element_addr(gen_t* g, const type_t* t, gen_val_t base, gen_val_t index) {
    if (t->kind == TYPE_ARRAY) {
        return gen_gep_array(g, t, base, index);
    }
    // A span's elements are reached through its `.ptr` (item 3).
    const gen_val_t ptr = gen_span_ptr(g, base);
    return gen_gep_element(g, gen_element_type(g, t), ptr, index);
}

// `e[i]` (D6.8): the index is extended to `i64`, one `icmp uge` branches to
// fort_rt_fail_bounds, and the element is reached with the array or the
// element shape of item 3.
static gen_place_t gen_index_place(gen_t* g, ast_node_t* n) {
    gen_place_t out;
    out.addr = gen_literal(g, str_from_cstr("ptr"), "null");
    out.type = n->type;
    const type_t* ot = n->a->type;
    if (bad_type(ot)) {
        return out;
    }
    const gen_place_t operand = gen_expr_place(g, n->a);
    if (g->failed) {
        return out;
    }
    const gen_val_t raw = gen_expr_value(g, n->b);
    if (g->failed) {
        return out;
    }
    // A negative index fails the same compare, since it is extended by its
    // own signedness (item 16).
    const gen_val_t index = gen_resize(g,
                                       raw,
                                       gen_int_bits(n->b->type),
                                       str_from_cstr("i64"),
                                       (uint32_t)BITS_64,
                                       gen_is_signed(n->b->type));
    if (!g->opts.no_bounds_check) {
        const gen_val_t len = gen_length_of(g, ot, operand.addr);
        const gen_val_t bad = gen_icmp(g, "uge", index, len);
        gen_args_t args;
        gen_args_init(&args);
        gen_args_add(&args, index);
        gen_args_add(&args, len);
        gen_check(g, bad, true, RT_FAIL_BOUNDS, &args, n->loc);
        gen_args_free(&args);
    }
    out.addr = gen_element_addr(g, ot, operand.addr, index);
    return out;
}

// ---- span expressions (item 16, D6.9) -----------------------------------------------

// One bound of a span expression as an `i64`, extended by its own signedness
// so that a negative bound fails the unsigned compares below (item 16).
static gen_val_t span_bound(gen_t* g, ast_node_t* e) {
    const gen_val_t raw = gen_expr_value(g, e);
    return gen_resize(g,
                      raw,
                      gen_int_bits(e->type),
                      str_from_cstr("i64"),
                      (uint32_t)BITS_64,
                      gen_is_signed(e->type));
}

// `e[lo..hi]`, `e[lo..]`, `e[..hi]` and `e[..]` (D6.9): the result is a view
// of the operand, whose two bounds are checked against the operand's own
// length in one branch, and `p[lo..hi]` on a raw pointer is the explicit
// unsafe escape that checks nothing (D6.9, D10.4).
static void gen_span_expr(gen_t* g, ast_node_t* n, gen_place_t dst) {
    const type_t* ot = n->a != NULL ? n->a->type : NULL;
    if (ot == NULL || bad_type(ot)) {
        return;
    }
    const str_t i64ty = str_from_cstr("i64");
    if (ot->kind == TYPE_PTR) {
        // A pointer has no length, so only the two-bound form exists and
        // nothing is checked (D6.9, D10.4).
        const gen_val_t base = gen_expr_value(g, n->a);
        if (g->failed) {
            return;
        }
        if (n->b == NULL || n->c == NULL) {
            // `p[lo..]`, `p[..hi]` and `p[..]` are checker errors, a pointer
            // having no length (D10.4), so this is unreachable; an
            // unfinished path is a diagnostic and never wrong code.
            gen_todo(g, n->loc, "this span expression");
            return;
        }
        const gen_val_t lo = span_bound(g, n->b);
        const gen_val_t hi = span_bound(g, n->c);
        if (g->failed) {
            return;
        }
        // Two instructions, so two statements (D19.5), as below.
        const gen_val_t at = gen_gep_element(g, ot->elem, base, lo);
        const gen_val_t length = gen_binary(g, "sub", hi, lo);
        gen_span_init(g, dst.addr, at, length);
        return;
    }
    const gen_place_t operand = gen_expr_place(g, n->a);
    if (g->failed) {
        return;
    }
    // The operand and then the bounds, left to right (D6.3); an absent low
    // bound is zero (D6.9).
    const gen_val_t lo = n->b != NULL ? span_bound(g, n->b) : gen_const_unsigned(g, i64ty, 0);
    if (g->failed) {
        return;
    }
    gen_val_t hi;
    if (n->c != NULL) {
        hi = span_bound(g, n->c);
    } else {
        // `e[lo..]` is `e[lo..len]` (D6.9).
        hi = gen_length_of(g, ot, operand.addr);
    }
    if (g->failed) {
        return;
    }
    if (!g->opts.no_bounds_check) {
        // The length is read after the bounds the source writes, so the check
        // holds against the length the operation sees (D6.3); an absent high
        // bound is that length and was read above. With both bounds written
        // and `--no-bounds-check` nothing reads it, so nothing loads it
        // (D10.6).
        const gen_val_t len = n->c != NULL ? gen_length_of(g, ot, operand.addr) : hi;
        // `0 <= lo <= hi <= len` in one branch: the two compares are
        // unsigned, so a negative bound fails them too (item 16, D6.9).
        const gen_val_t over = gen_icmp(g, "ugt", hi, len);
        const gen_val_t inverted = gen_icmp(g, "ugt", lo, hi);
        const gen_val_t bad = gen_binary(g, "or", over, inverted);
        gen_args_t args;
        gen_args_init(&args);
        gen_args_add(&args, lo);
        gen_args_add(&args, hi);
        gen_args_add(&args, len);
        gen_check(g, bad, true, RT_FAIL_SPAN, &args, n->loc);
        gen_args_free(&args);
    }
    // Two instructions, so two statements: C leaves the order of two
    // arguments unspecified and the text is a function of the program alone
    // (D19.5).
    const gen_val_t base = gen_element_addr(g, ot, operand.addr, lo);
    const gen_val_t length = gen_binary(g, "sub", hi, lo);
    gen_span_init(g, dst.addr, base, length);
}

static gen_place_t gen_field_place(gen_t* g, ast_node_t* n, bool arrow) {
    gen_place_t out;
    out.addr = gen_literal(g, str_from_cstr("ptr"), "null");
    out.type = n->type;
    gen_val_t base;
    if (arrow) {
        // `p->f` is `(*p).f` (D6.10).
        base = gen_expr_value(g, n->a);
    } else {
        base = gen_expr_place(g, n->a).addr;
    }
    if (g->failed) {
        return out;
    }
    const type_t* st = arrow ? n->a->type->elem : n->a->type;
    uint64_t k = 0;
    if (st == NULL || st->kind != TYPE_STRUCT || !gen_field_index(n->sym, &k)) {
        gen_todo(g, n->loc, "this field access");
        return out;
    }
    out.addr = gen_gep_field(g, gen_mem_type(g, st), base, k);
    return out;
}

// The storage of a module-level constant or global, which is its symbol
// itself: a constant is an immutable lvalue in read-only memory and a `mut`
// global an assignable one (D6.7, D7.10). Returns false when `n` denotes
// something else.
static bool global_place(gen_t* g, const ast_node_t* n, gen_place_t* out) {
    const sym_t* s = n->sym;
    if (s == NULL || (s->kind != SYM_CONST && s->kind != SYM_GLOBAL) || bad_type(s->type)) {
        return false;
    }
    gen_val_t addr;
    addr.ty = str_from_cstr("ptr");
    addr.val = gen_symbol_ref(g, s);
    out->addr = addr;
    // The declaration's own type, as a local's slot takes it from its symbol:
    // a place is storage, not the expression that reached it.
    out->type = s->type;
    return true;
}

// The place of an rvalue: a field access or an index on an rvalue struct or
// array is allowed and copies it through a temporary (D6.7), and a `string`
// literal and an aggregate call result reach their place the same way (D19.3).
static gen_place_t rvalue_place(gen_t* g, ast_node_t* n) {
    const gen_place_t tmp = gen_temp_place(g, n->type);
    gen_expr_into(g, n, tmp);
    return tmp;
}

gen_place_t gen_expr_place(gen_t* g, ast_node_t* n) {
    gen_place_t out;
    out.addr = gen_literal(g, str_from_cstr("ptr"), "null");
    out.type = n->type;
    if (g->failed) {
        return out;
    }
    switch (n->kind) {
    case AST_IDENT: {
        const sym_t* s = n->sym;
        if (s != NULL && (s->kind == SYM_LOCAL || s->kind == SYM_PARAM)) {
            return gen_slot_place(g, s);
        }
        if (global_place(g, n, &out)) {
            return out;
        }
        gen_todo(g, n->loc, "a module-level name");
        return out;
    }
    case AST_INDEX:
        return gen_index_place(g, n);
    case AST_FIELD:
        // `m.NAME` across modules designates that module's storage, not a
        // field of a value (D7.10, D9.4).
        if (global_place(g, n, &out)) {
            return out;
        }
        return gen_field_place(g, n, false);
    case AST_ARROW:
        return gen_field_place(g, n, true);
    case AST_UNARY:
        if (n->op == TOK_STAR) {
            // `*p` designates the storage the pointer reaches (D6.7).
            out.addr = gen_expr_value(g, n->a);
            return out;
        }
        break;
    case AST_SPAN:
    case AST_STRING:
    case AST_STRUCT_LIT:
    case AST_ARRAY_LIT:
    case AST_BRACE_INIT:
        // A span expression yields a view, which is a value and not storage
        // (D6.9), so its header is built in a temporary, as a `string`
        // literal's is (D3.7); `point{1, 2}.x` and `i32[3]{7, 8, 9}[1]` read
        // a member out of the literal's own temporary (D6.5, D6.7).
        return rvalue_place(g, n);
    case AST_CAST:
    case AST_CALL:
    case AST_NEW:
        if (gen_is_aggregate(n->type)) {
            // A call result, a cast between aggregates, which only drops
            // marks and emits nothing of its own, and the span `new(T, n)`
            // returns are rvalues as well, so a member of either is read out
            // of a copy and `del(new(T, n))`, which D17.8 admits, frees the
            // copy's pointer (D3.14, D5.4, D6.7, D17.9).
            return rvalue_place(g, n);
        }
        break;
    default:
        break;
    }
    gen_todo(g, n->loc, "this expression as a place");
    return out;
}

// ---- values -----------------------------------------------------------------------

// `.len` and `.ptr` of a span or `string` are its header fields; `.len` of a
// fixed array is a constant the checker already folded (D3.4, D3.5).
static bool pseudo_field_value(gen_t* g, ast_node_t* n, gen_val_t* out) {
    // Through a pointer to a span or string, `->` reaches them too, since
    // `p->f` is `(*p).f` (D6.10).
    const bool arrow = n->kind == AST_ARROW;
    const type_t* ot = n->a != NULL ? n->a->type : NULL;
    if (arrow) {
        ot = ot != NULL && ot->kind == TYPE_PTR ? ot->elem : NULL;
    }
    if (ot == NULL || (ot->kind != TYPE_SPAN && ot->kind != TYPE_STRING)) {
        return false;
    }
    const bool len = str_eq(n->name, str_from_cstr("len"));
    const bool ptr = str_eq(n->name, str_from_cstr("ptr"));
    if (!len && !ptr) {
        return false;
    }
    // The header is where the pointer reaches for `->` and where the operand
    // stands for `.` (D6.7, D6.10).
    const gen_val_t base = arrow ? gen_expr_value(g, n->a) : gen_expr_place(g, n->a).addr;
    if (g->failed) {
        *out = gen_literal(g, str_from_cstr("i64"), "0");
        return true;
    }
    *out = len ? gen_span_len(g, base) : gen_span_ptr(g, base);
    return true;
}

static gen_val_t gen_unary_value(gen_t* g, ast_node_t* n) {
    if (n->op == TOK_AMP) {
        // `&e` is the address of the place `e` designates (D6.7).
        gen_place_t p = gen_expr_place(g, n->a);
        p.addr.ty = str_from_cstr("ptr");
        return p.addr;
    }
    if (n->op == TOK_STAR) {
        return gen_load_place(g, gen_expr_place(g, n));
    }
    const gen_val_t v = gen_expr_value(g, n->a);
    if (g->failed) {
        return v;
    }
    if (n->op == TOK_BANG) {
        return gen_binary(g, "xor", v, gen_literal(g, str_from_cstr("i1"), "true"));
    }
    if (n->op == TOK_TILDE) {
        // `~x` is `x ^ -1` and never overflows (D6.2).
        return gen_binary(g, "xor", v, gen_const_signed(g, v.ty, -1));
    }
    // Unary `-` is `0 - x` and uses the same intrinsics as `-` (item 15).
    return gen_arith(g, n->loc, TOK_MINUS, n->type, zero_of(g, n->type), n->type, v);
}

static gen_val_t gen_binary_value(gen_t* g, ast_node_t* n) {
    switch (n->op) {
    case TOK_EQ:
    case TOK_NE:
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
        return gen_compare(g, n);
    case TOK_AND_AND:
    case TOK_PIPE_PIPE:
        return gen_short_circuit(g, n);
    default:
        break;
    }
    const gen_val_t a = gen_expr_value(g, n->a);
    const gen_val_t b = gen_expr_value(g, n->b);
    if (g->failed) {
        return a;
    }
    return gen_arith(g, n->loc, n->op, n->a->type, a, n->b->type, b);
}

gen_val_t gen_expr_value(gen_t* g, ast_node_t* n) {
    if (g->failed || n == NULL || bad_type(n->type)) {
        return gen_literal(g, str_from_cstr("i32"), "0");
    }
    if (folded_scalar(g, n)) {
        // A constant expression has no side effects, so it is emitted as a
        // literal (D4.6).
        return gen_const_value(g, n->type, check_node_value(g->ck, n));
    }
    switch (n->kind) {
    case AST_IDENT: {
        gen_val_t fn;
        // A function name is a value of its function type and has no storage
        // to load from (D3.10).
        if (function_ref(g, n, &fn)) {
            return fn;
        }
        return gen_load_place(g, gen_expr_place(g, n));
    }
    case AST_INDEX:
        return gen_load_place(g, gen_expr_place(g, n));
    case AST_ARROW: {
        gen_val_t v;
        // `out->len` and `out->ptr` read the header the pointer reaches
        // (D6.10).
        if (pseudo_field_value(g, n, &v)) {
            return v;
        }
        return gen_load_place(g, gen_expr_place(g, n));
    }
    case AST_FIELD: {
        gen_val_t v;
        // A qualified name `m.f` of a function is that function's address,
        // not a field of a value (D3.10, D9.4).
        if (function_ref(g, n, &v)) {
            return v;
        }
        if (pseudo_field_value(g, n, &v)) {
            return v;
        }
        return gen_load_place(g, gen_expr_place(g, n));
    }
    case AST_UNARY:
        return gen_unary_value(g, n);
    case AST_BINARY:
        return gen_binary_value(g, n);
    case AST_CAST:
        return gen_cast(g, n);
    case AST_CALL: {
        // `move` is the one universe function with a value (D12.2, item 18).
        if (gen_is_move(n)) {
            return gen_move(g, n, NULL);
        }
        if (gen_builtin_call(g, n)) {
            return gen_literal(g, str_from_cstr("void"), "");
        }
        return gen_call(g, n, NULL);
    }
    case AST_BRACE_INIT:
        // `{}` on a scalar is the zero value, which the checker allows for an
        // enum alone: a zeroed enum holds 0 even when 0 is not a member
        // (D3.9, D6.5).
        if (ast_len(n) == 0) {
            return zero_of(g, n->type);
        }
        break;
    case AST_NEW:
        // `new(T)` yields the address of one object, which is a scalar;
        // `new(T, n)` yields a span and is written into a place (D10.2).
        return gen_new_object(g, n);
    default:
        break;
    }
    gen_todo(g, n->loc, "this expression");
    return gen_literal(g, str_from_cstr("i32"), "0");
}

// ---- values written into a place (D19.3) -------------------------------------------

// A `{ ... }` initializer (D6.5): `{}` zeroes the place with `llvm.memset`
// and every other form writes member by member.
static void gen_brace_init(gen_t* g, ast_node_t* n, gen_place_t dst) {
    const type_t* t = dst.type;
    if (ast_len(n) == 0) {
        // `{}` is the zero value of the type (D6.5).
        gen_memset_zero(g, dst.addr, type_alignof(t), type_sizeof(t));
        return;
    }
    if (t->kind == TYPE_ARRAY) {
        for (uint64_t i = 0; i < ast_len(n); i++) {
            gen_place_t elem;
            elem.type = t->elem;
            elem.addr =
                gen_gep_array(g, t, dst.addr, gen_const_unsigned(g, str_from_cstr("i64"), i));
            gen_expr_into(g, ast_child(n, i), elem);
            if (g->failed) {
                return;
            }
        }
        return;
    }
    if (t->kind != TYPE_STRUCT) {
        gen_todo(g, n->loc, "this initializer");
        return;
    }
    if ((n->flags & AST_FLAG_DESIGNATED) != 0) {
        // The designated form zeroes the place first, since omitted fields
        // are zeroed (D6.5).
        gen_memset_zero(g, dst.addr, type_alignof(t), type_sizeof(t));
        for (uint64_t i = 0; i < ast_len(n); i++) {
            ast_node_t* d = ast_child(n, i);
            uint64_t k = 0;
            if (d->kind != AST_DESIGNATOR || !gen_field_index(d->sym, &k)) {
                gen_todo(g, d->loc, "this initializer");
                return;
            }
            gen_place_t field;
            field.type = d->a->type;
            field.addr = gen_gep_field(g, gen_mem_type(g, t), dst.addr, k);
            gen_expr_into(g, d->a, field);
            if (g->failed) {
                return;
            }
        }
        return;
    }
    const sym_t* s = (const sym_t*)t->decl;
    uint64_t at = 0;
    for (uint64_t i = 0; s != NULL && s->node != NULL && i < ast_len(s->node); i++) {
        const ast_node_t* f = ast_child(s->node, i);
        if (f->kind != AST_FIELD_DECL) {
            continue;
        }
        if (at >= ast_len(n)) {
            break;
        }
        gen_place_t field;
        field.type = f->type;
        field.addr = gen_gep_field(g, gen_mem_type(g, t), dst.addr, at);
        gen_expr_into(g, ast_child(n, at), field);
        if (g->failed) {
            return;
        }
        at++;
    }
}

void gen_expr_into(gen_t* g, ast_node_t* n, gen_place_t dst) {
    if (g->failed || n == NULL || bad_type(n->type)) {
        return;
    }
    if (!gen_is_aggregate(dst.type)) {
        gen_store_place(g, dst, gen_expr_value(g, n));
        return;
    }
    switch (n->kind) {
    case AST_BRACE_INIT:
        gen_brace_init(g, n, dst);
        return;
    case AST_STRUCT_LIT:
    case AST_ARRAY_LIT:
        gen_brace_init(g, n->b, dst);
        return;
    case AST_STRING: {
        // A string value is its two header fields: the bytes and the length
        // the trailing NUL excludes (D3.7).
        gen_val_t ptr;
        gen_val_t len;
        string_operand(g, n, &ptr, &len);
        gen_span_init(g, dst.addr, ptr, len);
        return;
    }
    case AST_CALL:
        if (gen_is_move(n)) {
            // A span, a `string` or an owning aggregate is copied into the
            // destination and the operand is then zeroed (item 18).
            (void)gen_move(g, n, &dst);
            return;
        }
        if (!gen_builtin_call(g, n)) {
            (void)gen_call(g, n, &dst);
        }
        return;
    case AST_CAST:
        // A cast between aggregates only drops marks, so nothing is emitted
        // for it (item 12, D5.4, D17.4).
        gen_expr_into(g, n->a, dst);
        return;
    case AST_NEW:
        gen_new_span(g, n, dst);
        return;
    case AST_SPAN:
        gen_span_expr(g, n, dst);
        return;
    default:
        break;
    }
    if ((n->ann & CHECK_ANN_CONST) != 0 && check_node_value(g->ck, n).kind == CV_STR) {
        gen_val_t ptr;
        gen_val_t len;
        string_operand(g, n, &ptr, &len);
        gen_span_init(g, dst.addr, ptr, len);
        return;
    }
    // Every other aggregate is an lvalue, so the value is a copy of its
    // storage (D19.3).
    const gen_place_t src = gen_expr_place(g, n);
    if (g->failed) {
        return;
    }
    const uint64_t align = type_alignof(dst.type);
    gen_memcpy(g, dst.addr, align, src.addr, align, type_sizeof(dst.type));
}

void gen_expr_discard(gen_t* g, ast_node_t* n) {
    if (g->failed || n == NULL) {
        return;
    }
    if (n->kind == AST_CALL) {
        if (gen_builtin_call(g, n)) {
            return;
        }
        if (gen_is_aggregate(n->type)) {
            // An aggregate result still needs a place, which nothing reads
            // (item 7).
            const gen_place_t sink = gen_temp_place(g, n->type);
            (void)gen_call(g, n, &sink);
            return;
        }
        (void)gen_call(g, n, NULL);
        return;
    }
    gen_todo(g, n->loc, "this statement expression");
}
