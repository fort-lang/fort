// The table of runtime entry points (toolchain.md 5.1) and the maps over it:
// the IR text of a form, the form a fort type takes as a value, and the call
// shape of a fort signature; see runtime_sig.h.
#include "runtime_sig.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prim.h"
#include "str.h"
#include "types.h"

// One entry point: its mangled fort name, its result form, whether section
// 5.1 declares it `fn noreturn`, and its parameter forms in order, padded
// with IR_NONE. The list ends at the first IR_NONE, so the arity is read off
// the row rather than counted by hand beside it.
typedef struct {
    const char* name;
    ir_form_t result;
    bool noreturn;
    ir_form_t params[RT_MAX_PARAMS];
} rt_sig_t;

// The fort signatures of toolchain.md 5.1, in that section's order, mapped to
// IR forms by item 7: `u64` and `i64` are both `i64`, `u32` is `i32`, `char`
// is `i8 zeroext`, `bool` is `i1 zeroext`, every pointer is `ptr` and `loc`
// is the three parameters `ptr, i32, i32`. `args` returns a `string@`, an
// aggregate, so it is a `void` function with a leading `ptr` (item 7, D9.9).
// Every `fail_*` function, `panic`, `assert_fail` and `exit` is `fn noreturn`
// (section 5.1), which is stated per row rather than as a range over the
// enum, so an entry point added in the middle of the order cannot inherit the
// attribute by position. The two float printers stand in `std.rt_float` and
// not in `std.rt`, because a compiler without floats cannot compile them
// (D18.1).
static const rt_sig_t RT_SIG[RT_COUNT] = {
    {"std.rt.alloc", IR_PTR, false, {IR_I64, IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"std.rt.free", IR_VOID, false, {IR_PTR, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.str_eq", IR_BOOL, false, {IR_PTR, IR_I64, IR_PTR, IR_I64, IR_NONE, IR_NONE}},
    {"std.rt.fail_bounds", IR_VOID, true, {IR_I64, IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"std.rt.fail_span", IR_VOID, true, {IR_I64, IR_I64, IR_I64, IR_PTR, IR_I32, IR_I32}},
    {"std.rt.fail_overflow", IR_VOID, true, {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.fail_shift", IR_VOID, true, {IR_I64, IR_PTR, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"std.rt.fail_div_zero", IR_VOID, true, {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.fail_div_overflow",
     IR_VOID,
     true,
     {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.fail_alloc_count", IR_VOID, true, {IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE}},
    {"std.rt.fail_overwrite", IR_VOID, true, {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.fail_enum", IR_VOID, true, {IR_I64, IR_PTR, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"std.rt.panic", IR_VOID, true, {IR_PTR, IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"std.rt.assert_fail", IR_VOID, true, {IR_PTR, IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE}},
    {"std.rt.print_i64", IR_VOID, false, {IR_I32, IR_I64, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.print_u64", IR_VOID, false, {IR_I32, IR_I64, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt_float.print_f32",
     IR_VOID,
     false,
     {IR_I32, IR_F32, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt_float.print_f64",
     IR_VOID,
     false,
     {IR_I32, IR_F64, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.print_bool", IR_VOID, false, {IR_I32, IR_BOOL, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.print_char", IR_VOID, false, {IR_I32, IR_U8, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.print_ptr", IR_VOID, false, {IR_I32, IR_PTR, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.print_str", IR_VOID, false, {IR_I32, IR_PTR, IR_I64, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.print_enum", IR_VOID, false, {IR_I32, IR_I32, IR_PTR, IR_I64, IR_NONE, IR_NONE}},
    {"std.rt.flush", IR_VOID, false, {IR_I32, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.flush_all", IR_VOID, false, {IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.args_init", IR_VOID, false, {IR_I32, IR_PTR, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.args", IR_VOID, false, {IR_PTR, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"std.rt.exit", IR_VOID, true, {IR_I32, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
};

rt_entry_t rt_entry_of(str_t name) {
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        if (str_eq(str_from_cstr(RT_SIG[i].name), name)) {
            return (rt_entry_t)i;
        }
    }
    return RT_COUNT;
}

const char* rt_entry_name(rt_entry_t rt) {
    return RT_SIG[rt].name;
}

bool rt_entry_noreturn(rt_entry_t rt) {
    return RT_SIG[rt].noreturn;
}

ir_form_t rt_entry_result(rt_entry_t rt) {
    return RT_SIG[rt].result;
}

uint32_t rt_entry_param_count(rt_entry_t rt) {
    uint32_t n = 0;
    while (n < (uint32_t)RT_MAX_PARAMS && RT_SIG[rt].params[n] != IR_NONE) {
        n++;
    }
    return n;
}

ir_form_t rt_entry_param(rt_entry_t rt, uint32_t i) {
    return RT_SIG[rt].params[i];
}

const char* ir_param_text(ir_form_t t) {
    switch (t) {
    case IR_PTR:
        return "ptr";
    case IR_BOOL:
        return "i1 zeroext";
    case IR_I8:
        return "i8 signext";
    case IR_U8:
        return "i8 zeroext";
    case IR_I16:
        return "i16 signext";
    case IR_U16:
        return "i16 zeroext";
    case IR_I32:
        return "i32";
    case IR_I64:
        return "i64";
    case IR_F32:
        return "float";
    case IR_F64:
        return "double";
    case IR_VOID:
        return "void";
    case IR_NONE:
        break;
    }
    // A type with no IR form has no text either: the spelling is deliberately
    // not IR so that a form that escaped a check fails a tool rather than
    // emitting a plausible call.
    return "<none>";
}

const char* ir_result_text(ir_form_t t) {
    // A narrow result carries its extension attribute before the type
    // (`call zeroext i1 @"std.rt.str_eq"(...)`, item 7).
    switch (t) {
    case IR_BOOL:
        return "zeroext i1";
    case IR_I8:
        return "signext i8";
    case IR_U8:
        return "zeroext i8";
    case IR_I16:
        return "signext i16";
    case IR_U16:
        return "zeroext i16";
    default:
        break;
    }
    return ir_param_text(t);
}

// The IR form of a primitive: the value type of item 7 and the extension
// attribute of D9.9, which `bool`, `char`, `u8`, `u16`, `i8` and `i16` carry.
static ir_form_t ir_of_prim(prim_kind_t k) {
    switch (k) {
    case PRIM_BOOL:
        return IR_BOOL;
    case PRIM_CHAR:
    case PRIM_U8:
        return IR_U8;
    case PRIM_I8:
        return IR_I8;
    case PRIM_U16:
        return IR_U16;
    case PRIM_I16:
        return IR_I16;
    case PRIM_I32:
    case PRIM_U32:
        return IR_I32;
    case PRIM_I64:
    case PRIM_U64:
        return IR_I64;
    case PRIM_F32:
        return IR_F32;
    case PRIM_F64:
        return IR_F64;
    case PRIM_VOID:
        break;
    }
    return IR_VOID;
}

ir_form_t ir_form_of_type(const type_t* t) {
    if (t == NULL) {
        return IR_NONE;
    }
    switch (t->kind) {
    case TYPE_VOID:
        return IR_VOID;
    case TYPE_PRIM:
        return ir_of_prim(t->prim);
    case TYPE_PTR:
    case TYPE_VOIDPTR:
    case TYPE_FN:
        // Every pointer, `void*` and function pointer is the opaque `ptr`
        // (D3.10, D3.11).
        return IR_PTR;
    case TYPE_ENUM:
        // An enum is passed as `i32` (D3.9, D9.8).
        return IR_I32;
    default:
        break;
    }
    // A struct, fixed array, span or `string` has no value form: it lives in
    // memory and travels as a pointer (D19.3).
    return IR_NONE;
}

// Whether a fort type is one of the aggregates of item 7, which travel as a
// plain `ptr`.
static bool form_is_aggregate(const type_t* t) {
    if (t == NULL) {
        return false;
    }
    return t->kind == TYPE_STRUCT || t->kind == TYPE_ARRAY || t->kind == TYPE_SPAN ||
           t->kind == TYPE_STRING;
}

bool rt_signature_of_type(const type_t* fn, ir_form_t* result, ir_form_t* params, uint32_t* n) {
    if (fn == NULL || fn->kind != TYPE_FN) {
        return false;
    }
    uint32_t count = 0;
    if (form_is_aggregate(fn->elem)) {
        // An aggregate result is a leading `ptr sret(%T)` parameter on a
        // function whose result type is `void` (item 7).
        *result = IR_VOID;
        params[count] = IR_PTR;
        count++;
    } else {
        *result = ir_form_of_type(fn->elem);
    }
    for (uint32_t i = 0; i < fn->nparams; i++) {
        if (count >= (uint32_t)RT_MAX_PARAMS) {
            return false;
        }
        // An aggregate argument is a plain `ptr` parameter (item 7); no entry
        // point takes one, so this keeps the map total rather than covering a
        // row.
        params[count] = form_is_aggregate(fn->params[i]) ? IR_PTR : ir_form_of_type(fn->params[i]);
        count++;
    }
    *n = count;
    for (uint32_t i = count; i < (uint32_t)RT_MAX_PARAMS; i++) {
        params[i] = IR_NONE;
    }
    return true;
}
