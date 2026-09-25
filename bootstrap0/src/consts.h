// Represents compile-time constants and folds them exactly.
// Untyped integer operations fail on overflow or division by zero.
// Typed operations use target ranges. Wrapping operations use target widths.
#ifndef FORT_CONSTS_H
#define FORT_CONSTS_H

#include <stdbool.h>
#include <stdint.h>

#include "prim.h"
#include "str.h"

typedef enum {
    CV_NONE, // not a constant expression
    CV_INT,  // an integer, exact in [-2^63, 2^64 - 1]
    CV_BOOL, // true or false
    CV_CHAR, // a char literal: a code point 0..255
    CV_NULL, // the literal null
    CV_STR,  // a string literal: a view of its decoded bytes
} cv_kind_t;

typedef struct {
    cv_kind_t kind;
    bool neg;     // CV_INT: the value is -mag; never set when mag is 0
    uint64_t mag; // CV_INT: |value|; CV_CHAR: the code point; CV_BOOL: 0/1
    str_t str;    // CV_STR: the bytes
} cval_t;

// The largest shift count of untyped folding (a count in `0..63`; the value
// range has 64 magnitude bits).
enum { CV_SHIFT_MAX = 63 };

// The last code point of a char literal.
enum { CV_CHAR_MAX = 255 };

// ---- constructors and queries --------------------------------------------

cval_t cv_none(void);
cval_t cv_from_i64(int64_t v);

// Also the constructor for an integer literal: the lexer's magnitude.
cval_t cv_from_u64(uint64_t v);

// A char literal from the lexer's code point, at most CV_CHAR_MAX (an
// internal error otherwise).
cval_t cv_from_char(uint64_t code);
cval_t cv_from_bool(bool b);
cval_t cv_null(void);
cval_t cv_from_str(str_t s);

// The exact integer of a CV_INT or CV_CHAR value: a char in an integer context
// is its code point. Both are "integer-like"; every arithmetic function below
// requires CV_INT operands, so the checker calls cv_as_int on a char first.
bool cv_is_int(cval_t v);
bool cv_is_int_like(cval_t v);
cval_t cv_as_int(cval_t v);

// Whether a CV_INT is 0, or negative: a negative constant in an index, bound or
// count position is a compile error.
bool cv_is_zero(cval_t v);
bool cv_is_neg(cval_t v);

// The value of a CV_INT as an int64_t (false when outside its range) or as a
// uint64_t (false when negative).
bool cv_to_i64(cval_t v, int64_t* out);
bool cv_to_u64(cval_t v, uint64_t* out);

// The low 64 bits of the two's-complement pattern of a CV_INT: the value
// modulo 2^64, as a u64 register would hold it.
uint64_t cv_bits(cval_t v);

// Two CV_INT or two CV_CHAR values in order: -1, 0 or 1.
int cv_cmp(cval_t a, cval_t b);

// Equality of two values of the same kind: integers and chars by value,
// bools, null values, and strings by bytes. None never equals. Values of
// different kinds are not equal.
bool cv_eq(cval_t a, cval_t b);

// ---- untyped integer folding -----------------------------------------------

// Each function stores the exact result and returns true. It returns false when
// the result is outside [-2^63, 2^64 - 1] or a divisor is zero. It then stores
// none in `*out`. `/` truncates toward zero. `%` takes the dividend's sign.
// `~c` is `-c - 1`. Shift counts use 0..CV_SHIFT_MAX. Other counts are compile
// errors. `<<` is exact multiplication by 2^n. `>>` is arithmetic floor
// division by 2^n. Thus, `-1 >> 1` is -1 and `-3 >> 1` is -2.
bool cv_add(cval_t a, cval_t b, cval_t* out);
bool cv_sub(cval_t a, cval_t b, cval_t* out);
bool cv_mul(cval_t a, cval_t b, cval_t* out);
bool cv_div(cval_t a, cval_t b, cval_t* out);
bool cv_rem(cval_t a, cval_t b, cval_t* out);
bool cv_neg(cval_t a, cval_t* out);
bool cv_not(cval_t a, cval_t* out);
bool cv_and(cval_t a, cval_t b, cval_t* out);
bool cv_or(cval_t a, cval_t b, cval_t* out);
bool cv_xor(cval_t a, cval_t b, cval_t* out);
bool cv_shl(cval_t a, cval_t count, cval_t* out);
bool cv_shr(cval_t a, cval_t count, cval_t* out);

// The comparison operators, on two integers or two chars, or `==`/`!=` on
// two bools, nulls or strings: a bool value.
typedef enum {
    CV_REL_EQ,
    CV_REL_NE,
    CV_REL_LT,
    CV_REL_LE,
    CV_REL_GT,
    CV_REL_GE,
} cv_rel_t;

cval_t cv_compare(cv_rel_t rel, cval_t a, cval_t b);

// `! && ||` on bools.
cval_t cv_lnot(cval_t a);
cval_t cv_land(cval_t a, cval_t b);
cval_t cv_lor(cval_t a, cval_t b);

// ---- types ------------------------------------------------------------------

// Whether contextual conversion can give `v` type `t`. An integer fits each
// integer type that holds it and either float type. A char fits `char` or an
// integer type that holds its code point. An integer never fits `char`. A bool
// fits `bool`.
bool cv_fits(cval_t v, prim_kind_t t);

// The default type of a constant with no context: i32 if the integer fits, else
// i64; `char` for a char; `bool` for a bool. False, and `*out` untouched, for an
// integer outside i64 and for null, strings and none.
bool cv_default_kind(cval_t v, prim_kind_t* out);

// Applies `cast(v, t)` with run-time semantics. An integer, char, or bool cast
// to an integer keeps its low bits. The target sign controls extension.
// `cast(300, u8)` is 44. `cast(-1, u32)` is 4294967295.
// `cast(0x80000000, i32)` is -2147483648. A cast to `char` keeps the low byte.
// A bool becomes 0 or 1. The function returns false for bool, float, or void
// targets. It also returns false for null, strings, and none.
// On failure, the function stores none in `*out`.
//
// The checker first gives an untyped source operand its default type. It reports
// `constant expression out of range` when cv_default_kind fails. Thus,
// `cast(1 << 63, i32)` fails before this function. The function never sees a
// source `cast` value outside i64. Default typing cannot change an i64 value, so
// cv_cast needs only the value. `cast(true, i32)` is 1 at run time. It is not a
// constant expression. The checker decides whether it can fold a cast. This
// function only computes the run-time value.
bool cv_cast(cval_t v, prim_kind_t t, cval_t* out);

// ---- typed folding ------------------------------------------------------------

// The operators applied to constants of the integer type `t`: the exact result
// must fit `t`, or the function returns false and `*out` is none. Division by
// zero, `MIN / -1`, and `MIN % -1` return false. The remainder is exactly 0,
// but the run time reports an error. Folding must also fail. `~` complements
// the width, so `~0` is 4294967295 in u32. A negative shift count returns
// false. A count at least equal to the width also returns false. `<<` discards
// shifted-out bits. For example, `1 << 31` is -2147483648 in i32. `>>` is
// arithmetic for signed types and logical for unsigned types.
//
// Each value operand must already fit `t`. `cv_typed_neg` needs a signed `t`.
// The checker types and checks operands before folding. A violation is an
// internal error (`fatal_internal`, exit 2), not a program error.
// A shift count is not a value operand. The shifts accept any integer type for
// the count. Therefore, the shifts check only their left operand.
bool cv_typed_add(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_sub(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_mul(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_div(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_rem(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_neg(cval_t a, prim_kind_t t, cval_t* out);
bool cv_typed_not(cval_t a, prim_kind_t t, cval_t* out);
bool cv_typed_and(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_or(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_xor(cval_t a, cval_t b, prim_kind_t t, cval_t* out);
bool cv_typed_shl(cval_t a, cval_t count, prim_kind_t t, cval_t* out);
bool cv_typed_shr(cval_t a, cval_t count, prim_kind_t t, cval_t* out);

// `+% -% *%` on constants of the integer type `t` (both operands fit `t`, as
// above): two's-complement wrapping at the width of `t`; never false.
cval_t cv_wrap_add(cval_t a, cval_t b, prim_kind_t t);
cval_t cv_wrap_sub(cval_t a, cval_t b, prim_kind_t t);
cval_t cv_wrap_mul(cval_t a, cval_t b, prim_kind_t t);

// ---- diagnostics -------------------------------------------------------------

// Appends the diagnostic spelling of the value. Integers use decimal and a `-`
// prefix. Chars use char literals. Bools use `true` or `false`. Null uses
// `null`. Strings use double quotes and source escapes. Thus, quotes, newlines,
// and NUL bytes cannot break the diagnostic line. None uses `<none>`.
void cv_to_str(cval_t v, sb_t* out);

#endif
