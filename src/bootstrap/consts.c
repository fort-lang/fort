// Exact constant folding (consts.h). Every integer computation runs on the
// unsigned magnitudes with the overflow builtins; nothing here relies on
// signed overflow or on the conversion of an out-of-range value to a signed
// type, so the file is clean under -fsanitize=undefined.
#include "consts.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prim.h"
#include "str.h"

// The magnitude of the most negative constant, 2^63.
static const uint64_t NEG_MAX_MAG = UINT64_C(1) << 63U;

// The number of bits of a u64, the widest integer.
enum { WORD_BITS = 64 };

// The hex digits of a `\xHH` escape in cv_to_str.
static const char HEX_DIGITS[] = "0123456789ABCDEF";
enum { HEX_SHIFT = 4, HEX_MASK = 0xFU, ASCII_PRINT_MIN = 0x20, ASCII_PRINT_MAX = 0x7E };

// ---- construction ------------------------------------------------------------

static cval_t int_make(bool neg, uint64_t mag) {
    cval_t v = {CV_INT, neg && mag != 0, mag, {NULL, 0}};
    return v;
}

// Stores `-mag` or `mag` when it lies in the constant range, else none.
static bool int_check(bool neg, uint64_t mag, cval_t* out) {
    if (neg && mag > NEG_MAX_MAG) {
        *out = cv_none();
        return false;
    }
    *out = int_make(neg, mag);
    return true;
}

static void require_int(cval_t v) {
    if (v.kind != CV_INT) {
        fatal_internal("constant folding on a non-integer");
    }
}

static void require_bool(cval_t v) {
    if (v.kind != CV_BOOL) {
        fatal_internal("logical folding on a non-bool");
    }
}

cval_t cv_none(void) {
    cval_t v = {CV_NONE, false, 0, {NULL, 0}};
    return v;
}

cval_t cv_from_i64(int64_t v) {
    if (v >= 0) {
        return int_make(false, (uint64_t)v);
    }
    // -(v + 1) is representable for every negative v, INT64_MIN included.
    return int_make(true, (uint64_t)(-(v + 1)) + 1U);
}

cval_t cv_from_u64(uint64_t v) {
    return int_make(false, v);
}

cval_t cv_from_char(uint64_t code) {
    if (code > CV_CHAR_MAX) {
        fatal_internal("char constant outside 0..255");
    }
    cval_t v = {CV_CHAR, false, code, {NULL, 0}};
    return v;
}

cval_t cv_from_bool(bool b) {
    cval_t v = {CV_BOOL, false, b ? 1U : 0U, {NULL, 0}};
    return v;
}

cval_t cv_null(void) {
    cval_t v = {CV_NULL, false, 0, {NULL, 0}};
    return v;
}

cval_t cv_from_str(str_t s) {
    cval_t v = {CV_STR, false, 0, s};
    return v;
}

// ---- queries -------------------------------------------------------------------

bool cv_is_int(cval_t v) {
    return v.kind == CV_INT;
}

bool cv_is_int_like(cval_t v) {
    return v.kind == CV_INT || v.kind == CV_CHAR;
}

cval_t cv_as_int(cval_t v) {
    if (v.kind == CV_CHAR) {
        return int_make(false, v.mag);
    }
    require_int(v);
    return v;
}

bool cv_is_zero(cval_t v) {
    require_int(v);
    return v.mag == 0;
}

bool cv_is_neg(cval_t v) {
    require_int(v);
    return v.neg;
}

bool cv_to_i64(cval_t v, int64_t* out) {
    require_int(v);
    if (v.neg) {
        if (v.mag > NEG_MAX_MAG) {
            return false;
        }
        // -(mag - 1) - 1: mag - 1 is at most INT64_MAX.
        *out = -(int64_t)(v.mag - 1U) - 1;
        return true;
    }
    if (v.mag > (uint64_t)INT64_MAX) {
        return false;
    }
    *out = (int64_t)v.mag;
    return true;
}

bool cv_to_u64(cval_t v, uint64_t* out) {
    require_int(v);
    if (v.neg) {
        return false;
    }
    *out = v.mag;
    return true;
}

uint64_t cv_bits(cval_t v) {
    require_int(v);
    return v.neg ? UINT64_C(0) - v.mag : v.mag;
}

// The value whose low 64 bits are `bits` and whose bits above are all
// `high` (the infinite two's-complement extension; `high` is `neg` for a
// CV_INT): non-negative when `high` is clear, else bits - 2^64, which lies in
// the constant range exactly when bits >= 2^63.
static bool int_from_bits(bool high, uint64_t bits, cval_t* out) {
    if (!high) {
        *out = int_make(false, bits);
        return true;
    }
    if (bits < NEG_MAX_MAG) {
        *out = cv_none();
        return false;
    }
    return int_check(true, UINT64_C(0) - bits, out);
}

int cv_cmp(cval_t a, cval_t b) {
    if (a.kind != CV_CHAR || b.kind != CV_CHAR) {
        require_int(a);
        require_int(b);
        if (a.neg != b.neg) {
            return a.neg ? -1 : 1;
        }
    }
    if (a.mag == b.mag) {
        return 0;
    }
    // Two negatives order by magnitude in reverse.
    const bool less = a.mag < b.mag;
    return less != a.neg ? -1 : 1;
}

bool cv_eq(cval_t a, cval_t b) {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
    case CV_NONE:
        return false;
    case CV_INT:
        return a.neg == b.neg && a.mag == b.mag;
    case CV_BOOL:
    case CV_CHAR:
        return a.mag == b.mag;
    case CV_NULL:
        return true;
    case CV_STR:
        return str_eq(a.str, b.str);
    }
    return false;
}

// ---- untyped arithmetic -----------------------------------------------------
// D4.4

// The sum of two signed magnitudes, exact; the raw pair is validated by the
// caller through int_check.
static bool add_raw(bool an, uint64_t am, bool bn, uint64_t bm, bool* rn, uint64_t* rm) {
    if (an == bn) {
        *rn = an;
        return !__builtin_add_overflow(am, bm, rm);
    }
    if (am >= bm) {
        *rn = an;
        *rm = am - bm;
    } else {
        *rn = bn;
        *rm = bm - am;
    }
    return true;
}

bool cv_add(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    bool neg = false;
    uint64_t mag = 0;
    if (!add_raw(a.neg, a.mag, b.neg, b.mag, &neg, &mag)) {
        *out = cv_none();
        return false;
    }
    return int_check(neg, mag, out);
}

bool cv_sub(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    bool neg = false;
    uint64_t mag = 0;
    // a - b is a + (-b); the raw pair (!b.neg, b.mag) needs no validation.
    if (!add_raw(a.neg, a.mag, !b.neg, b.mag, &neg, &mag)) {
        *out = cv_none();
        return false;
    }
    return int_check(neg, mag, out);
}

bool cv_mul(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    uint64_t mag = 0;
    if (__builtin_mul_overflow(a.mag, b.mag, &mag)) {
        *out = cv_none();
        return false;
    }
    return int_check(a.neg != b.neg, mag, out);
}

bool cv_div(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    if (b.mag == 0) {
        *out = cv_none();
        return false;
    }
    // D6.13: truncation toward zero
    return int_check(a.neg != b.neg, a.mag / b.mag, out);
}

bool cv_rem(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    if (b.mag == 0) {
        *out = cv_none();
        return false;
    }
    // D6.13: the remainder takes the dividend's sign, so always in range
    return int_check(a.neg, a.mag % b.mag, out);
}

bool cv_neg(cval_t a, cval_t* out) {
    require_int(a);
    return int_check(!a.neg, a.mag, out);
}

bool cv_not(cval_t a, cval_t* out) {
    require_int(a);
    // ~c is -c - 1: for c >= 0 that is -(c + 1); for c < 0 it is |c| - 1.
    if (a.neg) {
        return int_check(false, a.mag - 1U, out);
    }
    uint64_t mag = 0;
    if (__builtin_add_overflow(a.mag, UINT64_C(1), &mag)) {
        *out = cv_none();
        return false;
    }
    return int_check(true, mag, out);
}

bool cv_and(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    return int_from_bits(a.neg && b.neg, cv_bits(a) & cv_bits(b), out);
}

bool cv_or(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    return int_from_bits(a.neg || b.neg, cv_bits(a) | cv_bits(b), out);
}

bool cv_xor(cval_t a, cval_t b, cval_t* out) {
    require_int(a);
    require_int(b);
    return int_from_bits(a.neg != b.neg, cv_bits(a) ^ cv_bits(b), out);
}

// A shift count for untyped folding: 0..CV_SHIFT_MAX, else false.
static bool shift_count(cval_t count, unsigned* n) {
    require_int(count);
    if (count.neg || count.mag > CV_SHIFT_MAX) {
        return false;
    }
    *n = (unsigned)count.mag;
    return true;
}

bool cv_shl(cval_t a, cval_t count, cval_t* out) {
    require_int(a);
    unsigned n = 0;
    if (!shift_count(count, &n)) {
        *out = cv_none();
        return false;
    }
    // An exact multiplication by 2^n: the magnitude must keep every bit, that
    // is mag * 2^n must not exceed 2^64 - 1.
    if (a.mag > (UINT64_MAX >> n)) {
        *out = cv_none();
        return false;
    }
    return int_check(a.neg, a.mag << n, out);
}

bool cv_shr(cval_t a, cval_t count, cval_t* out) {
    require_int(a);
    unsigned n = 0;
    if (!shift_count(count, &n)) {
        *out = cv_none();
        return false;
    }
    // A floor division by 2^n: for a negative value the magnitude rounds up
    // (-1 >> 1 is -1); a rounded-up magnitude never exceeds the original.
    uint64_t mag = a.mag >> n;
    if (a.neg && (a.mag & ((UINT64_C(1) << n) - 1U)) != 0) {
        mag += 1U;
    }
    return int_check(a.neg, mag, out);
}

cval_t cv_compare(cv_rel_t rel, cval_t a, cval_t b) {
    if (rel == CV_REL_EQ || rel == CV_REL_NE) {
        if (a.kind != b.kind || a.kind == CV_NONE) {
            fatal_internal("constant comparison across kinds");
        }
        const bool eq = cv_eq(a, b);
        return cv_from_bool(rel == CV_REL_EQ ? eq : !eq);
    }
    const int c = cv_cmp(a, b);
    switch (rel) {
    case CV_REL_LT:
        return cv_from_bool(c < 0);
    case CV_REL_LE:
        return cv_from_bool(c <= 0);
    case CV_REL_GT:
        return cv_from_bool(c > 0);
    case CV_REL_GE:
        return cv_from_bool(c >= 0);
    case CV_REL_EQ:
    case CV_REL_NE:
        break;
    }
    fatal_internal("unknown constant comparison");
}

cval_t cv_lnot(cval_t a) {
    require_bool(a);
    return cv_from_bool(a.mag == 0);
}

cval_t cv_land(cval_t a, cval_t b) {
    require_bool(a);
    require_bool(b);
    return cv_from_bool(a.mag != 0 && b.mag != 0);
}

cval_t cv_lor(cval_t a, cval_t b) {
    require_bool(a);
    require_bool(b);
    return cv_from_bool(a.mag != 0 || b.mag != 0);
}

// ---- types ------------------------------------------------------------------

static unsigned int_width(prim_kind_t t) {
    if (!prim_is_integer(t)) {
        fatal_internal("integer folding on a non-integer type");
    }
    return prim_size(t) * PRIM_BITS_PER_BYTE;
}

static bool int_fits(cval_t v, prim_kind_t t) {
    const unsigned width = int_width(t);
    if (prim_is_signed(t)) {
        // [-2^(w-1), 2^(w-1) - 1]
        const uint64_t half = UINT64_C(1) << (width - 1U);
        return v.neg ? v.mag <= half : v.mag < half;
    }
    if (v.neg) {
        return false;
    }
    return width == WORD_BITS || v.mag < (UINT64_C(1) << width);
}

bool cv_fits(cval_t v, prim_kind_t t) {
    switch (v.kind) {
    case CV_INT:
        return prim_is_float(t) || (prim_is_integer(t) && int_fits(v, t));
    case CV_CHAR:
        return t == PRIM_CHAR || (prim_is_integer(t) && int_fits(cv_as_int(v), t));
    case CV_BOOL:
        return t == PRIM_BOOL;
    case CV_NONE:
    case CV_NULL:
    case CV_STR:
        return false;
    }
    return false;
}

bool cv_default_kind(cval_t v, prim_kind_t* out) {
    switch (v.kind) {
    case CV_INT:
        if (int_fits(v, PRIM_I32)) {
            *out = PRIM_I32;
            return true;
        }
        if (int_fits(v, PRIM_I64)) {
            *out = PRIM_I64;
            return true;
        }
        return false;
    case CV_CHAR:
        *out = PRIM_CHAR;
        return true;
    case CV_BOOL:
        *out = PRIM_BOOL;
        return true;
    case CV_NONE:
    case CV_NULL:
    case CV_STR:
        return false;
    }
    return false;
}

// The integer of type `t` whose two's-complement pattern has the low bits
// of `bits`: truncated to the width, then sign- or zero-extended.
static cval_t int_from_truncated(uint64_t bits, prim_kind_t t) {
    const unsigned width = int_width(t);
    const uint64_t mask = width == WORD_BITS ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
    const uint64_t low = bits & mask;
    if (prim_is_signed(t) && (low & (UINT64_C(1) << (width - 1U))) != 0) {
        // Negative: the magnitude is 2^width - low, which is (~low & mask) + 1
        // and at most 2^(width - 1).
        return int_make(true, ((~low) & mask) + 1U);
    }
    return int_make(false, low);
}

bool cv_cast(cval_t v, prim_kind_t t, cval_t* out) {
    if (!cv_is_int_like(v) && v.kind != CV_BOOL) {
        *out = cv_none();
        return false;
    }
    if (prim_is_integer(t)) {
        *out = int_from_truncated(v.kind == CV_BOOL ? v.mag : cv_bits(cv_as_int(v)), t);
        return true;
    }
    if (t == PRIM_CHAR && v.kind != CV_BOOL) {
        *out = cv_from_char(cv_bits(cv_as_int(v)) & CV_CHAR_MAX);
        return true;
    }
    // D3.14: integer to bool, and bool to char, are not casts
    // T-041: floats are not in the bootstrap; void is never a value
    *out = cv_none();
    return false;
}

// ---- typed folding ------------------------------------------------------------

// Every cv_typed_* and cv_wrap_* function documents that its value operands
// already have the integer type `t`, so a value outside that type is a bug in the
// checker, not a program error: it fails loudly rather than folding
// `cv_typed_and(-1, 5, u8)` to 5. A shift count is not a value operand, any
// integer type being admitted there, so the shifts guard their left operand only.
// D6.2
static void require_typed(cval_t v, prim_kind_t t) {
    require_int(v);
    if (!int_fits(v, t)) {
        fatal_internal("typed constant folding on a value outside its type");
    }
}

// The typed result of an exact operation: `ok` and in the range of `t`.
static bool typed_check(bool ok, cval_t r, prim_kind_t t, cval_t* out) {
    if (!ok || !int_fits(r, t)) {
        *out = cv_none();
        return false;
    }
    *out = r;
    return true;
}

bool cv_typed_add(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    require_typed(b, t);
    cval_t r = cv_none();
    return typed_check(cv_add(a, b, &r), r, t, out);
}

bool cv_typed_sub(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    require_typed(b, t);
    cval_t r = cv_none();
    return typed_check(cv_sub(a, b, &r), r, t, out);
}

bool cv_typed_mul(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    require_typed(b, t);
    cval_t r = cv_none();
    return typed_check(cv_mul(a, b, &r), r, t, out);
}

bool cv_typed_div(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    require_typed(b, t);
    // D6.13: MIN / -1 is 2^(w-1), which the range check rejects
    cval_t r = cv_none();
    return typed_check(cv_div(a, b, &r), r, t, out);
}

bool cv_typed_rem(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    // D6.13: MIN % -1 is 0 exactly but a run-time error
    cval_t q = cv_none();
    cval_t r = cv_none();
    return typed_check(cv_typed_div(a, b, t, &q) && cv_rem(a, b, &r), r, t, out);
}

bool cv_typed_neg(cval_t a, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    if (!prim_is_signed(t)) {
        // D6.2: the checker never asks to negate an unsigned constant
        fatal_internal("unary minus on an unsigned type");
    }
    cval_t r = cv_none();
    return typed_check(cv_neg(a, &r), r, t, out);
}

bool cv_typed_not(cval_t a, prim_kind_t t, cval_t* out) {
    // The complement within the width: -a - 1 modulo 2^width.
    require_typed(a, t);
    *out = int_from_truncated(~cv_bits(a), t);
    return true;
}

bool cv_typed_and(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    require_typed(b, t);
    cval_t r = cv_none();
    return typed_check(cv_and(a, b, &r), r, t, out);
}

bool cv_typed_or(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    require_typed(b, t);
    cval_t r = cv_none();
    return typed_check(cv_or(a, b, &r), r, t, out);
}

bool cv_typed_xor(cval_t a, cval_t b, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    require_typed(b, t);
    cval_t r = cv_none();
    return typed_check(cv_xor(a, b, &r), r, t, out);
}

// A shift count for the width of `t`: 0..width-1, else false.
// D6.2
static bool typed_shift_count(cval_t count, prim_kind_t t, unsigned* n) {
    require_int(count);
    if (count.neg || count.mag >= int_width(t)) {
        return false;
    }
    *n = (unsigned)count.mag;
    return true;
}

bool cv_typed_shl(cval_t a, cval_t count, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    unsigned n = 0;
    if (!typed_shift_count(count, t, &n)) {
        *out = cv_none();
        return false;
    }
    // D6.2: the bits shifted out are discarded, never checked
    *out = int_from_truncated(cv_bits(a) << n, t);
    return true;
}

bool cv_typed_shr(cval_t a, cval_t count, prim_kind_t t, cval_t* out) {
    require_typed(a, t);
    unsigned n = 0;
    if (!typed_shift_count(count, t, &n)) {
        *out = cv_none();
        return false;
    }
    // The operand fits `t`, so the exact floor division is the arithmetic
    // shift of a signed type and the logical shift of an unsigned one.
    cval_t r = cv_none();
    return typed_check(cv_shr(a, count, &r), r, t, out);
}

cval_t cv_wrap_add(cval_t a, cval_t b, prim_kind_t t) {
    require_typed(a, t);
    require_typed(b, t);
    return int_from_truncated(cv_bits(a) + cv_bits(b), t);
}

cval_t cv_wrap_sub(cval_t a, cval_t b, prim_kind_t t) {
    require_typed(a, t);
    require_typed(b, t);
    return int_from_truncated(cv_bits(a) - cv_bits(b), t);
}

cval_t cv_wrap_mul(cval_t a, cval_t b, prim_kind_t t) {
    require_typed(a, t);
    require_typed(b, t);
    return int_from_truncated(cv_bits(a) * cv_bits(b), t);
}

// ---- diagnostics --------------------------------------------------------------

// One byte inside a literal delimited by `quote`, with the escapes of the source
// form: `\n \t \r \0 \\`, the delimiter itself, and `\xHH` for every other
// byte outside printable ASCII.
// D2.8
static void escape_char(uint64_t code, char quote, sb_t* out) {
    if (code == (uint64_t)(unsigned char)quote) {
        sb_push(out, '\\');
        sb_push(out, quote);
        return;
    }
    switch (code) {
    case '\n':
        sb_append(out, "\\n");
        return;
    case '\t':
        sb_append(out, "\\t");
        return;
    case '\r':
        sb_append(out, "\\r");
        return;
    case 0:
        sb_append(out, "\\0");
        return;
    case '\\':
        sb_append(out, "\\\\");
        return;
    default:
        break;
    }
    if (code >= ASCII_PRINT_MIN && code <= ASCII_PRINT_MAX) {
        sb_push(out, (char)code);
        return;
    }
    sb_append(out, "\\x");
    sb_push(out, HEX_DIGITS[(code >> HEX_SHIFT) & HEX_MASK]);
    sb_push(out, HEX_DIGITS[code & HEX_MASK]);
}

static void char_to_str(uint64_t code, sb_t* out) {
    sb_push(out, '\'');
    escape_char(code, '\'', out);
    sb_push(out, '\'');
}

// The bytes of a string literal between double quotes, escaped as they would be
// written in source, so a diagnostic stays on one line.
// D2.8, D2.9
static void str_to_str(str_t s, sb_t* out) {
    sb_push(out, '"');
    for (uint64_t k = 0; k < s.len; k++) {
        escape_char((uint64_t)(unsigned char)s.ptr[k], '"', out);
    }
    sb_push(out, '"');
}

void cv_to_str(cval_t v, sb_t* out) {
    switch (v.kind) {
    case CV_NONE:
        sb_append(out, "<none>");
        return;
    case CV_INT:
        if (v.neg) {
            sb_push(out, '-');
        }
        sb_append_u64(out, v.mag);
        return;
    case CV_BOOL:
        sb_append(out, v.mag != 0 ? "true" : "false");
        return;
    case CV_CHAR:
        char_to_str(v.mag, out);
        return;
    case CV_NULL:
        sb_append(out, "null");
        return;
    case CV_STR:
        str_to_str(v.str, out);
        return;
    }
    sb_append(out, "<none>");
}
