// The primitive types of D3.1: `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool
// char void`, as a kind enum shared by the constant folder (consts.h) and the
// type representation (types.h). Sizes and alignments are those of D3.1 and
// type-system.md 11.1: alignment equals size for every primitive.
//
// Header-only: the enum and five one-line queries, so that every module can
// name a primitive without depending on the type representation.
#ifndef FORT_PRIM_H
#define FORT_PRIM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PRIM_I8,
    PRIM_I16,
    PRIM_I32,
    PRIM_I64,
    PRIM_U8,
    PRIM_U16,
    PRIM_U32,
    PRIM_U64,
    PRIM_BOOL,
    PRIM_CHAR,
    PRIM_F32, // never produced by the bootstrap compiler (T-041)
    PRIM_F64, // never produced by the bootstrap compiler (T-041)
    PRIM_VOID,
} prim_kind_t;

// One past the last kind, for tables indexed by prim_kind_t.
enum { PRIM_COUNT = PRIM_VOID + 1 };

// prim_is_integer and prim_is_signed are range tests over this order, so the
// signed kinds come first and the unsigned ones close the integers.
_Static_assert(PRIM_I8 == 0 && PRIM_I64 == 3 && PRIM_U8 == PRIM_I64 + 1 && PRIM_U64 == PRIM_U8 + 3,
               "the integer kinds lead prim_kind_t, the four signed ones first");

// The bit width of an integer is prim_size times this.
enum { PRIM_BITS_PER_BYTE = 8 };

// Whether `k` is one of i8..u64. `bool` and `char` are not integers (D3.2,
// D3.3).
static inline bool prim_is_integer(prim_kind_t k) {
    return k <= PRIM_U64;
}

// Whether `k` is one of i8..i64.
static inline bool prim_is_signed(prim_kind_t k) {
    return k <= PRIM_I64;
}

// Whether `k` is f32 or f64.
static inline bool prim_is_float(prim_kind_t k) {
    return k == PRIM_F32 || k == PRIM_F64;
}

// The size in bytes (D3.1); the alignment is the same; 0 for `void`.
static inline uint32_t prim_size(prim_kind_t k) {
    switch (k) {
    case PRIM_I8:
    case PRIM_U8:
    case PRIM_BOOL:
    case PRIM_CHAR:
        return 1;
    case PRIM_I16:
    case PRIM_U16:
        return 2;
    case PRIM_I32:
    case PRIM_U32:
    case PRIM_F32:
        return 4;
    case PRIM_I64:
    case PRIM_U64:
    case PRIM_F64:
        return 8;
    case PRIM_VOID:
        return 0;
    }
    return 0;
}

// The spelling of the type in source and in diagnostics.
static inline const char* prim_name(prim_kind_t k) {
    switch (k) {
    case PRIM_I8:
        return "i8";
    case PRIM_I16:
        return "i16";
    case PRIM_I32:
        return "i32";
    case PRIM_I64:
        return "i64";
    case PRIM_U8:
        return "u8";
    case PRIM_U16:
        return "u16";
    case PRIM_U32:
        return "u32";
    case PRIM_U64:
        return "u64";
    case PRIM_F32:
        return "f32";
    case PRIM_F64:
        return "f64";
    case PRIM_BOOL:
        return "bool";
    case PRIM_CHAR:
        return "char";
    case PRIM_VOID:
        return "void";
    }
    return "?";
}

#endif
