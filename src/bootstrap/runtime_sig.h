// The runtime entry points of toolchain.md 5.1: the one table of their
// canonical signatures, which both the emitter and the checker read.
//
// A runtime entry point used to be described by three positional arrays of
// gen_data.c beside its C prototype in `runtime/fort_rt.h` and its row in
// toolchain.md 5.1, with nothing holding them together. Here the IR types of
// one entry point are written once, and the declaration text of item 8, the
// result type of a call site, the `_Noreturn` attribute group and the
// signature the checker holds an `extern fn` against are all derived from
// that one row.
//
// The alphabet is the IR parameter forms of toolchain.md 6 item 7, attribute
// included, and not the fort types: what the check of D9.8 asks is whether
// the call the emitter would write matches the declaration it emits, which is
// a question about IR types.
#ifndef FORT_RUNTIME_SIG_H
#define FORT_RUNTIME_SIG_H

#include <stdbool.h>
#include <stdint.h>

#include "containers.h"
#include "str.h"
#include "types.h"

// The runtime entry points of toolchain.md 5.1, in the order that section
// lists them, which is the order their declarations are emitted in (D19.5).
typedef enum {
    RT_NEW,
    RT_DEL,
    RT_STR_EQ,
    RT_FAIL_BOUNDS,
    RT_FAIL_SPAN,
    RT_FAIL_OVERFLOW,
    RT_FAIL_SHIFT,
    RT_FAIL_DIV_ZERO,
    RT_FAIL_DIV_OVERFLOW,
    RT_FAIL_ALLOC_COUNT,
    RT_FAIL_OVERWRITE,
    RT_FAIL_ENUM,
    RT_PANIC,
    RT_ASSERT_FAIL,
    RT_PRINT_I64,
    RT_PRINT_U64,
    RT_PRINT_F32,
    RT_PRINT_F64,
    RT_PRINT_BOOL,
    RT_PRINT_CHAR,
    RT_PRINT_PTR,
    RT_PRINT_STR,
    RT_PRINT_ENUM,
    RT_FLUSH,
    RT_FLUSH_ALL,
    RT_ARGS_INIT,
    RT_ARGS_PTR,
    RT_ARGS_LEN,
    RT_EXIT,
    RT_COUNT,
} rt_entry_t;

// An IR parameter or result form of item 7: the type text together with the
// extension attribute a narrow value carries on both sides of the boundary
// (D9.9). `bool` is a form of its own because it is `i1` as a value and `i8`
// in memory (D19.2), so a `bool` argument is not a `u8` argument.
typedef enum {
    IR_VOID, // `void`, a result only
    IR_PTR,  // `ptr`: every pointer, `void*` and function pointer (D3.10, D3.11)
    IR_BOOL, // `i1 zeroext`
    IR_I8,   // `i8 signext`
    IR_U8,   // `i8 zeroext`: `u8` and `char`, C's `unsigned char` (D3.2)
    IR_I16,  // `i16 signext`
    IR_U16,  // `i16 zeroext`
    IR_I32,  // `i32`: `i32`, `u32` and every enum, which is passed as `i32` (D9.8)
    IR_I64,  // `i64`: `i64` and `u64`
    IR_F32,  // `float`
    IR_F64,  // `double`
    IR_NONE, // no IR form: a type an extern signature may not use (D9.8)
} ir_form_t;

// The widest parameter list of toolchain.md 5.1, which is
// `fort_rt_fail_span(i64, i64, i64, ptr, i32, i32)`.
enum { RT_MAX_PARAMS = 6 };

// The entry point a C name denotes, or RT_COUNT.
rt_entry_t rt_entry_of(str_t name);

// The C name of an entry point, unmangled as every extern name is (D9.7).
const char* rt_entry_name(rt_entry_t rt);

// Whether toolchain.md 5.1 declares the entry point `_Noreturn`, which is
// what puts `cold noreturn nounwind` on its declaration (section 5, item 14).
bool rt_entry_noreturn(rt_entry_t rt);

// Its result form, the number of parameters it takes and the form of the
// `i`-th one.
ir_form_t rt_entry_result(rt_entry_t rt);
uint32_t rt_entry_param_count(rt_entry_t rt);
ir_form_t rt_entry_param(rt_entry_t rt, uint32_t i);

// The text of a form in a parameter list (`i8 zeroext`) and in the result
// position of a `declare` or a `call`, where the attribute precedes the type
// (`zeroext i8`), as item 7 spells them.
const char* ir_param_text(ir_form_t t);
const char* ir_result_text(ir_form_t t);

// Appends the `declare` line of item 8 for an entry point, newline included:
// the C prototype of section 5.1 mapped to IR types, never variadic, with
// `#2` on a `_Noreturn` one (item 14).
void rt_declaration(sb_t* out, rt_entry_t rt);

// The IR form a fort type takes at the C boundary, or IR_NONE: the value
// type of item 7 with its extension attribute, which is what an extern call
// site passes and what an extern declaration states (D9.8, D9.9).
ir_form_t ir_form_of_type(const type_t* t);

#endif
