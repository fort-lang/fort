// Defines semantic symbol records.
// The checker creates one record per declaration, field, enum member, and builtin.
// Syntax tree annotations refer to these records until `check_free`.
#ifndef FORT_SYM_H
#define FORT_SYM_H

#include <stdbool.h>

#include "diag.h"
#include "str.h"
#include "types.h"

// Identifies what a name denotes.
// Fields and enum members are reached through a type instead of a scope.
typedef enum {
    SYM_MODULE,
    SYM_FN,
    SYM_EXTERN_FN,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_ENUM_MEMBER,
    SYM_FIELD,
    SYM_CONST,  // a module-level immutable declaration
    SYM_GLOBAL, // a module-level `mut` declaration
    SYM_LOCAL,
    SYM_PARAM,
    SYM_BUILTIN,
    SYM_KIND_COUNT, // one past the last kind, for tables
} sym_kind_t;

typedef struct sym sym_t;
struct sym {
    sym_kind_t kind;
    str_t name;
    loc_t decl;                  // the declaring node's name_loc
    const type_t* type;          // the error type when `error` is set
    bool mut0;                   // level-0 mutability, as type_to_str_decl spells it
    const struct ast_node* node; // the declaring node; NULL for a builtin
    const sym_t* owner;          // the struct of a field, the enum of a member, the module
    bool error;                  // the declaration failed to check
};

// The kind as a reader spells it: "module", "extern fn", "enum member",
// "constant", "global", "local", "parameter", or "builtin".
static inline const char* sym_kind_name(sym_kind_t kind) {
    switch (kind) {
    case SYM_MODULE:
        return "module";
    case SYM_FN:
        return "fn";
    case SYM_EXTERN_FN:
        return "extern fn";
    case SYM_STRUCT:
        return "struct";
    case SYM_ENUM:
        return "enum";
    case SYM_ENUM_MEMBER:
        return "enum member";
    case SYM_FIELD:
        return "field";
    case SYM_CONST:
        return "constant";
    case SYM_GLOBAL:
        return "global";
    case SYM_LOCAL:
        return "local";
    case SYM_PARAM:
        return "parameter";
    case SYM_BUILTIN:
        return "builtin";
    case SYM_KIND_COUNT:
        break;
    }
    return "?";
}

#endif
