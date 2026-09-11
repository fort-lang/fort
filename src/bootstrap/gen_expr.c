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

// The header fields of a span or `string` (item 17).
enum { SPAN_FIELD_PTR = 0, SPAN_FIELD_LEN = 1 };

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
    const gen_val_t pf = gen_gep_field(g, str_from_cstr("%fort.span"), p.addr, SPAN_FIELD_PTR);
    *ptr = gen_load(g, str_from_cstr("ptr"), pf, (uint64_t)sizeof(void*));
    const gen_val_t lf = gen_gep_field(g, str_from_cstr("%fort.span"), p.addr, SPAN_FIELD_LEN);
    *len = gen_load(g, str_from_cstr("i64"), lf, (uint64_t)sizeof(uint64_t));
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
    // `del` and `move` are the ownership half of D12.2, which T-021 and T-022
    // own.
    gen_todo(g, n->loc, "del and move");
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

static gen_val_t gen_compare(gen_t* g, ast_node_t* n) {
    const type_t* t = n->a->type;
    if (t != NULL && t->kind == TYPE_STRING) {
        // `string ==` compares lengths and then bytes, which T-021 owns.
        gen_todo(g, n->loc, "comparing strings");
        return gen_literal(g, str_from_cstr("i1"), "false");
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
        // `zext i1` for `bool` to integer (item 12).
        return gen_cast_op(g, "zext", v, gen_value_type(g, to));
    }
    if (is_bool(to)) {
        gen_todo(g, n->loc, "a cast to bool");
        return v;
    }
    // Integer to integer widens by the source's signedness (item 12).
    return gen_resize(g, v, from_bits, gen_value_type(g, to), to_bits, gen_is_signed(from));
}

// ---- places (item 3) --------------------------------------------------------------

gen_val_t gen_length_of(gen_t* g, const type_t* t, gen_val_t base) {
    if (t->kind == TYPE_ARRAY) {
        // A fixed array's length is an `i64` literal (item 16).
        return gen_const_unsigned(g, str_from_cstr("i64"), t->len);
    }
    const gen_val_t field = gen_gep_field(g, str_from_cstr("%fort.span"), base, SPAN_FIELD_LEN);
    return gen_load(g, str_from_cstr("i64"), field, (uint64_t)sizeof(uint64_t));
}

gen_val_t gen_element_addr(
    gen_t* g, const type_t* t, const type_t* elem, gen_val_t base, gen_val_t index) {
    if (t->kind == TYPE_ARRAY) {
        return gen_gep_array(g, t, base, index);
    }
    // A span's elements are reached through its `.ptr` (item 3).
    const gen_val_t field = gen_gep_field(g, str_from_cstr("%fort.span"), base, SPAN_FIELD_PTR);
    const gen_val_t ptr = gen_load(g, str_from_cstr("ptr"), field, (uint64_t)sizeof(void*));
    // A `string` has `char` elements and a span its own (D3.5, D3.7).
    return gen_gep_element(g, t->kind == TYPE_STRING ? elem : t->elem, ptr, index);
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
    out.addr = gen_element_addr(g, ot, n->type, operand.addr, index);
    return out;
}

// The index of a field in its struct, which is what the field shape of item 3
// names: the position among the AST_FIELD_DECL children, which is the order
// the named type of gen.c writes them in.
static bool field_index(const sym_t* field, uint64_t* out) {
    const sym_t* owner = field != NULL ? field->owner : NULL;
    if (owner == NULL || owner->node == NULL) {
        return false;
    }
    uint64_t at = 0;
    for (uint64_t i = 0; i < ast_len(owner->node); i++) {
        const ast_node_t* f = ast_child(owner->node, i);
        if (f->kind != AST_FIELD_DECL) {
            continue;
        }
        if (f->sym == field) {
            *out = at;
            return true;
        }
        at++;
    }
    return false;
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
    if (st == NULL || st->kind != TYPE_STRUCT || !field_index(n->sym, &k)) {
        gen_todo(g, n->loc, "this field access");
        return out;
    }
    out.addr = gen_gep_field(g, gen_mem_type(g, st), base, k);
    return out;
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
        gen_todo(g, n->loc, "a module-level name");
        return out;
    }
    case AST_INDEX:
        return gen_index_place(g, n);
    case AST_FIELD:
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
    case AST_STRING:
    case AST_STRUCT_LIT:
    case AST_ARRAY_LIT:
    case AST_BRACE_INIT:
        // A `string` literal is a place only as the source of a copy, whose
        // header is built in a temporary (D3.7), and `point{1, 2}.x` and
        // `i32[3]{7, 8, 9}[1]` read a member out of the literal's own
        // temporary (D6.5, D6.7).
        return rvalue_place(g, n);
    case AST_CAST:
    case AST_CALL:
        if (gen_is_aggregate(n->type)) {
            // A call result, and a cast between aggregates, which only drops
            // marks and emits nothing of its own, are rvalues as well, so a
            // member of either is read out of a copy (D3.14, D5.4, D6.7).
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
    const gen_val_t field =
        gen_gep_field(g, str_from_cstr("%fort.span"), base, len ? SPAN_FIELD_LEN : SPAN_FIELD_PTR);
    *out = gen_load(g,
                    len ? str_from_cstr("i64") : str_from_cstr("ptr"),
                    field,
                    len ? (uint64_t)sizeof(uint64_t) : (uint64_t)sizeof(void*));
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
        if (gen_builtin_call(g, n)) {
            // A builtin call yields no value but `move`, which T-022 owns.
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
        gen_todo(g, n->loc, "new");
        return gen_literal(g, str_from_cstr("ptr"), "null");
    case AST_SPAN:
        gen_todo(g, n->loc, "a span expression");
        return gen_literal(g, str_from_cstr("ptr"), "null");
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
            if (d->kind != AST_DESIGNATOR || !field_index(d->sym, &k)) {
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
        const gen_val_t pf =
            gen_gep_field(g, str_from_cstr("%fort.span"), dst.addr, SPAN_FIELD_PTR);
        gen_store(g, ptr, pf, (uint64_t)sizeof(void*));
        const gen_val_t lf =
            gen_gep_field(g, str_from_cstr("%fort.span"), dst.addr, SPAN_FIELD_LEN);
        gen_store(g, len, lf, (uint64_t)sizeof(uint64_t));
        return;
    }
    case AST_CALL:
        if (!gen_builtin_call(g, n)) {
            (void)gen_call(g, n, &dst);
        }
        return;
    case AST_CAST:
        // A cast between aggregates only drops marks, so nothing is emitted
        // for it (item 12, D5.4, D17.4).
        gen_expr_into(g, n->a, dst);
        return;
    default:
        break;
    }
    if ((n->ann & CHECK_ANN_CONST) != 0 && check_node_value(g->ck, n).kind == CV_STR) {
        gen_val_t ptr;
        gen_val_t len;
        string_operand(g, n, &ptr, &len);
        const gen_val_t pf =
            gen_gep_field(g, str_from_cstr("%fort.span"), dst.addr, SPAN_FIELD_PTR);
        gen_store(g, ptr, pf, (uint64_t)sizeof(void*));
        const gen_val_t lf =
            gen_gep_field(g, str_from_cstr("%fort.span"), dst.addr, SPAN_FIELD_LEN);
        gen_store(g, len, lf, (uint64_t)sizeof(uint64_t));
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
