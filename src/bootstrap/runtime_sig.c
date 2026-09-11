// The table of runtime entry points (toolchain.md 5.1) and the two maps over
// it: the IR text of a form, and the form a fort type takes at the C
// boundary; see runtime_sig.h.
#include "runtime_sig.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "containers.h"
#include "prim.h"
#include "str.h"
#include "types.h"

// One entry point: its C name, its result form, whether section 5.1 declares
// it `_Noreturn`, and its parameter forms in order, padded with IR_NONE.
// The list ends at the first IR_NONE, so the arity is read off the row
// rather than counted by hand beside it.
typedef struct {
    const char* name;
    ir_form_t result;
    bool noreturn;
    ir_form_t params[RT_MAX_PARAMS];
} rt_sig_t;

// The C prototypes of toolchain.md 5.1, in that section's order (D19.5),
// mapped to IR forms by item 8: `uint64_t` and `int64_t` are `i64`,
// `int32_t` is `i32`, `uint8_t` is `i8 zeroext`, every pointer is `ptr` and
// `loc` is the three parameters `ptr, i32, i32`. Every `fort_rt_fail_*`
// function, `fort_rt_panic`, `fort_rt_assert_fail` and `fort_rt_exit` is
// `_Noreturn` (section 5.1), which is stated per row rather than as a range
// over the enum, so an entry point added in the middle of the order cannot
// inherit the attribute by position.
static const rt_sig_t RT_SIG[RT_COUNT] = {
    {"fort_rt_new", IR_PTR, false, {IR_I64, IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"fort_rt_del", IR_VOID, false, {IR_PTR, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_str_eq", IR_U8, false, {IR_PTR, IR_I64, IR_PTR, IR_I64, IR_NONE, IR_NONE}},
    {"fort_rt_fail_bounds", IR_VOID, true, {IR_I64, IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"fort_rt_fail_span", IR_VOID, true, {IR_I64, IR_I64, IR_I64, IR_PTR, IR_I32, IR_I32}},
    {"fort_rt_fail_overflow", IR_VOID, true, {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_fail_shift", IR_VOID, true, {IR_I64, IR_PTR, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"fort_rt_fail_div_zero", IR_VOID, true, {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_fail_div_overflow",
     IR_VOID,
     true,
     {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_fail_alloc_count", IR_VOID, true, {IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE}},
    {"fort_rt_fail_overwrite", IR_VOID, true, {IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_fail_enum", IR_VOID, true, {IR_I64, IR_PTR, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"fort_rt_panic", IR_VOID, true, {IR_PTR, IR_I64, IR_PTR, IR_I32, IR_I32, IR_NONE}},
    {"fort_rt_assert_fail", IR_VOID, true, {IR_PTR, IR_PTR, IR_I32, IR_I32, IR_NONE, IR_NONE}},
    {"fort_rt_print_i64", IR_VOID, false, {IR_I32, IR_I64, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_u64", IR_VOID, false, {IR_I32, IR_I64, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_f32", IR_VOID, false, {IR_I32, IR_F32, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_f64", IR_VOID, false, {IR_I32, IR_F64, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_bool", IR_VOID, false, {IR_I32, IR_U8, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_char", IR_VOID, false, {IR_I32, IR_U8, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_ptr", IR_VOID, false, {IR_I32, IR_PTR, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_str", IR_VOID, false, {IR_I32, IR_PTR, IR_I64, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_print_enum", IR_VOID, false, {IR_I32, IR_I32, IR_PTR, IR_I64, IR_NONE, IR_NONE}},
    {"fort_rt_flush", IR_VOID, false, {IR_I32, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_flush_all", IR_VOID, false, {IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_args_init", IR_VOID, false, {IR_I32, IR_PTR, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_args_ptr", IR_PTR, false, {IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_args_len", IR_I64, false, {IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
    {"fort_rt_exit", IR_VOID, true, {IR_I32, IR_NONE, IR_NONE, IR_NONE, IR_NONE, IR_NONE}},
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
    // emitting a plausible declaration.
    return "<none>";
}

const char* ir_result_text(ir_form_t t) {
    // A narrow result carries its extension attribute before the type
    // (`declare zeroext i8 @fort_rt_str_eq(...)`, item 7).
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
        // An enum crosses the C boundary as `i32` (D9.8, D3.9).
        return IR_I32;
    default:
        break;
    }
    return IR_NONE;
}

// `declare <result> @fort_rt_x(<params>) [#2]`: a runtime entry point is
// declared with the C prototype of section 5.1, mapped to IR types by item 8,
// and never variadic, whether the compiler emits the call itself or the
// standard library reached the entry point with an `extern fn` (D13.1). A
// `_Noreturn` one carries the `cold noreturn nounwind` group of item 14.
void rt_declaration(sb_t* out, rt_entry_t rt) {
    sb_append(out, "declare ");
    sb_append(out, ir_result_text(rt_entry_result(rt)));
    sb_append(out, " @");
    sb_append(out, rt_entry_name(rt));
    sb_push(out, '(');
    for (uint32_t i = 0; i < rt_entry_param_count(rt); i++) {
        if (i > 0) {
            sb_append(out, ", ");
        }
        sb_append(out, ir_param_text(rt_entry_param(rt, i)));
    }
    sb_push(out, ')');
    if (rt_entry_noreturn(rt)) {
        sb_append(out, " #2");
    }
    sb_push(out, '\n');
}
