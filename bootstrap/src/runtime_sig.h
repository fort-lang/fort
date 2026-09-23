// Defines canonical LLVM IR signatures for std.rt entry points.
#ifndef FORT_RUNTIME_SIG_H
#define FORT_RUNTIME_SIG_H

#include <stdbool.h>
#include <stdint.h>

#include "str.h"
#include "types.h"

// The runtime entry points, in the order the table
// lists them.
typedef enum {
    RT_ALLOC,
    RT_FREE,
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
    RT_ARGS,
    RT_EXIT,
    RT_COUNT,
} rt_entry_t;

// An IR parameter or result form: the type text together with the
// extension attribute a narrow value carries on both sides of the boundary.
// `bool` is `i1` as a value and `i8` in memory, so a `bool` argument is `i1
// zeroext` and a `u8` argument `i8 zeroext`.
typedef enum {
    IR_VOID, // `void`, a result only
    IR_PTR,  // `ptr`: every pointer, `void*` and function pointer
    IR_BOOL, // `i1 zeroext`
    IR_I8,   // `i8 signext`
    IR_U8,   // `i8 zeroext`: `u8` and `char`, C's `unsigned char`
    IR_I16,  // `i16 signext`
    IR_U16,  // `i16 zeroext`
    IR_I32,  // `i32`, `u32` and every enum, which is passed as `i32`
    IR_I64,  // `i64`: `i64` and `u64`
    IR_F32,  // `float`
    IR_F64,  // `double`
    IR_NONE, // no IR form: an aggregate, or a type with no value form
} ir_form_t;

// The widest parameter list, which is
// `fail_span(i64, i64, i64, ptr, i32, i32)`.
enum { RT_MAX_PARAMS = 6 };

// The entry point a mangled symbol name denotes, or RT_COUNT. The name is the
// mangled one, `std.rt.print_i64`, since that is what the emitter writes and what
// the definitions in `std/rt.ft` carry.
rt_entry_t rt_entry_of(str_t name);

// The mangled fort name of an entry point. It is written `@"..."`, quoted like
// every other dotted name.
const char* rt_entry_name(rt_entry_t rt);

// Whether the entry point is `noreturn`. Its definition uses attribute group `#8`.
bool rt_entry_noreturn(rt_entry_t rt);

// Its result form, the number of parameters it takes and the form of the
// `i`-th one. An aggregate result is a leading `ptr` parameter on a `void`
// function, so `args` reads as `IR_VOID` with one `IR_PTR`.
ir_form_t rt_entry_result(rt_entry_t rt);
uint32_t rt_entry_param_count(rt_entry_t rt);
ir_form_t rt_entry_param(rt_entry_t rt, uint32_t i);

// Returns a form's parameter text, such as `i8 zeroext`. For a call result, the attribute precedes
// the type, as in `zeroext i1`.
const char* ir_param_text(ir_form_t t);
const char* ir_result_text(ir_form_t t);

// The IR form a fort type takes as a scalar value, or IR_NONE for an
// aggregate: the value type with its extension attribute.
ir_form_t ir_form_of_type(const type_t* t);

// The IR call shape of a fort function type. An aggregate result
// becomes a leading `ptr` parameter and a `void` result. Writes the result
// form into `*result` and the parameter forms into `params`, which holds
// RT_MAX_PARAMS entries, and the count into `*n`. Answers false when the
// signature does not fit that many parameters. This is how a row is held
// against the fort signature that defines it.
bool rt_signature_of_type(const type_t* fn, ir_form_t* result, ir_form_t* params, uint32_t* n);

#endif
