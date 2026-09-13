// Compile-time constant values and exact folding (type-system.md 10).
// D4
//
// An untyped integer constant is an exact integer in [-2^63, 2^64 - 1], held
// as a sign and a 64-bit magnitude: `neg` implies 1 <= mag <= 2^63, and zero
// is never negative. Every operation computes the exact result with the
// overflow builtins and reports a result outside that range, or a division by
// zero, by returning false; the caller turns that into the compile error.
// Nothing here wraps unless the operation is a `cast` or a wrapping operator.
// D4.4, D3.14, D11.2
//
// The other kinds are the remaining constants: `bool`, a char literal
// (a code point 0..255 whose default type is `char`), `null`, a string literal
// (a view of its decoded bytes) and "none", the value of an expression that is
// not constant.
// D4.3
//
// Bitwise `& | ^` on untyped integers: the operands are taken as
// two's-complement bit patterns with infinite sign extension, that is the
// mathematical bitwise operation on integers as in Go, and the result must lie
// in the constant range. It agrees with `~c` being `-c - 1`: `-1 & x` is `x`,
// `-1 | x` is `-1`, `-1 ^ 0xFFFFFFFFFFFFFFFF` is `-2^64` and hence an error;
// only `^` can leave the range.
// D4.4
//
// Two families of operations:
// - untyped folding (cv_add ... cv_shr, cv_compare): exact.
// D4.4
//
// - typed folding (cv_typed_*, cv_wrap_*): the operands already have the
//   integer type `t`, every operand fits it, and the result must fit it too
//   or the operation is a compile error (`i32 A = 2147483647; A + 1` is an
//   error, not a trap), except that `cast`, the wrapping operators and `<<`
//   wrap exactly as at run time.
// D3.14, D4.6, D6.2, D11.2
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants.
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

/// The largest shift count of untyped folding (a count in `0..63`; the value
/// range has 64 magnitude bits).
/// D4.4
enum { CV_SHIFT_MAX = 63 };

/// The last code point of a char literal.
/// D2.7, D2.8
enum { CV_CHAR_MAX = 255 };

// ---- constructors and queries --------------------------------------------

cval_t cv_none(void);
cval_t cv_from_i64(int64_t v);

/// Also the constructor for an integer literal: the lexer's magnitude.
cval_t cv_from_u64(uint64_t v);

/// A char literal from the lexer's code point, at most CV_CHAR_MAX (an
/// internal error otherwise).
cval_t cv_from_char(uint64_t code);
cval_t cv_from_bool(bool b);
cval_t cv_null(void);
cval_t cv_from_str(str_t s);

/// The exact integer of a CV_INT or CV_CHAR value: a char in an integer context
/// is its code point. Both are "integer-like"; every arithmetic function below
/// requires CV_INT operands, so the checker calls cv_as_int on a char first.
/// D4.3
bool cv_is_int(cval_t v);
bool cv_is_int_like(cval_t v);
cval_t cv_as_int(cval_t v);

/// Whether a CV_INT is 0, or negative: a negative constant in an index, bound or
/// count position is a compile error.
/// D4.1
bool cv_is_zero(cval_t v);
bool cv_is_neg(cval_t v);

/// The value of a CV_INT as an int64_t (false when outside its range) or as a
/// uint64_t (false when negative).
bool cv_to_i64(cval_t v, int64_t* out);
bool cv_to_u64(cval_t v, uint64_t* out);

/// The low 64 bits of the two's-complement pattern of a CV_INT: the value
/// modulo 2^64, as a u64 register would hold it.
uint64_t cv_bits(cval_t v);

/// Two CV_INT or two CV_CHAR values in order: -1, 0 or 1.
int cv_cmp(cval_t a, cval_t b);

/// Equality of two values of the same kind: integers and chars by value,
/// bools, null (equal to null), strings by bytes; none never equals. Values
/// of different kinds are not equal.
bool cv_eq(cval_t a, cval_t b);

// ---- untyped integer folding -----------------------------------------------
// D4.4

/// Each stores the exact result and returns true, or returns false when the
/// result lies outside [-2^63, 2^64 - 1] or a divisor is zero, in which case
/// `*out` is none. `/` truncates toward zero and `%` takes the dividend's sign.
/// `~c` is `-c - 1`. Shifts take a count in 0..CV_SHIFT_MAX, a count outside that
/// being a compile error; `<<` is an exact multiplication by 2^n and `>>` a floor
/// division by 2^n (arithmetic, so `-1 >> 1` is -1 and `-3 >> 1` is -2).
/// D4.4, D6.13
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

/// The comparison operators, on two integers or two chars, or `==`/`!=` on
/// two bools, nulls or strings: a bool value.
typedef enum {
    CV_REL_EQ,
    CV_REL_NE,
    CV_REL_LT,
    CV_REL_LE,
    CV_REL_GT,
    CV_REL_GE,
} cv_rel_t;

cval_t cv_compare(cv_rel_t rel, cval_t a, cval_t b);

/// `! && ||` on bools.
cval_t cv_lnot(cval_t a);
cval_t cv_land(cval_t a, cval_t b);
cval_t cv_lor(cval_t a, cval_t b);

// ---- types ------------------------------------------------------------------
// D4.2, D4.3, D4.5, D3.14

/// Whether `v` may take the type `t` by contextual conversion: an integer fits
/// any integer type whose range holds it and either float type; a char fits
/// `char` and any integer type that holds its code point; an integer never fits
/// `char`; a bool fits `bool`.
/// D4.2, D4.3
bool cv_fits(cval_t v, prim_kind_t t);

/// The default type of a constant with no context: i32 if the integer fits, else
/// i64; `char` for a char; `bool` for a bool. False, and `*out` untouched, for an
/// integer outside i64 and for null, strings and none.
/// D4.5
bool cv_default_kind(cval_t v, prim_kind_t* out);

/// `cast(v, t)` with run-time semantics: an integer, char or bool to an integer
/// type keeps the low bits of its two's-complement pattern and sign- or
/// zero-extends by the target (`cast(300, u8)` is 44, `cast(-1, u32)` is
/// 4294967295, `cast(0x80000000, i32)` is -2147483648); to `char` it keeps the low
/// byte; a bool becomes 0 or 1. False for a cast to `bool`, which is forbidden, to
/// a float, which the bootstrap lacks, to `void`, and for null, strings and none.
/// An untyped operand of a source `cast` takes its default type first: the checker
/// calls cv_default_kind and reports `constant expression out of range` when it
/// fails, so `cast(1 << 63, i32)` is rejected there and this function never sees a
/// value outside i64 from a source `cast`. The step cannot change a value that
/// does fit i64, which is why cv_cast takes the value alone. `cast(true, i32)` is
/// 1 at run time but is not a constant expression: whether a cast may be folded at
/// all is the checker's classification, not this function's, which only gives the
/// value the run time would produce.
/// D3.14, D4.1, D4.6
bool cv_cast(cval_t v, prim_kind_t t, cval_t* out);

// ---- typed folding ------------------------------------------------------------
// D4.6, D6.2, D6.13, D11.2

/// The operators applied to constants of the integer type `t`: the exact result
/// must fit `t`, or the function returns false and `*out` is none. Division by
/// zero, `MIN / -1` and `MIN % -1` are false (the remainder is 0 exactly, but the
/// run time errors on it, so folding must too). `~` complements the bits of the
/// width, so `~0` is 4294967295 in u32. Shifts: a count that is negative or at
/// least the width is false; `<<` discards the bits shifted out (`1 << 31` is
/// -2147483648 in i32); `>>` is arithmetic for signed and logical for unsigned
/// types.
///
/// Every value operand must already fit `t`, and `cv_typed_neg` needs a signed
/// `t`: the checker has typed and checked its operands before folding, so a
/// violation is an internal error (`fatal_internal`, exit 2), not a program error.
/// A shift count is not a value operand, any integer type being admitted there, so
/// the shifts check their left operand only.
/// D6.2, D6.13, D11.3
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

/// `+% -% *%` on constants of the integer type `t` (both operands fit `t`, as
/// above): two's-complement wrapping at the width of `t`; never false.
/// D11.2
cval_t cv_wrap_add(cval_t a, cval_t b, prim_kind_t t);
cval_t cv_wrap_sub(cval_t a, cval_t b, prim_kind_t t);
cval_t cv_wrap_mul(cval_t a, cval_t b, prim_kind_t t);

// ---- diagnostics -------------------------------------------------------------

/// Appends the value as it reads in a diagnostic: an integer in decimal with a
/// `-` prefix, a char as a char literal, `true`, `false`, `null`, a string as a
/// string literal between double quotes with the escapes of the source form (so a
/// quote, a newline or a NUL byte cannot break the diagnostic line), and `<none>`.
/// D2.8
void cv_to_str(cval_t v, sb_t* out);

#endif
