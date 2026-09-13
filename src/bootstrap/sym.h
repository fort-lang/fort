// The symbol records of the bootstrap compiler: one record for every
// declaration, enum member, field and builtin, and a pointer to one on every
// node of the tree whose own name token denotes something.
// D7.9, D20.3
//
// The checker writes them; the index walk, the symbol list of the structured
// output and the language server read them. A record therefore holds what a
// reader needs and nothing about checking: the kind, the name, the range of
// the declaring name token (never the construct's first token), the type,
// level-0 mutability, the declaring node, the owner and whether the
// declaration failed to check.
// D20.4
//
// `node` is the declaring node, so `sym->node->sym == sym` for every record
// but a builtin's, which no source declares. `type` is NULL for the two kinds
// that have no fort type, SYM_MODULE and SYM_BUILTIN, and is the error type of
// the table whenever `error` is set, so that every later diagnostic about the
// declaration is silenced by the poison the type table already carries.
// D14.2
//
// A record lives as long as the checker that made it: every `sym` and `type`
// slot of the tree dangles once check_free has run, so a pass that reads the
// annotations runs before it. The one carve-out in the rule above is an import
// path: only its last segment denotes the module or the declaration the import
// binds, the segments before it naming search directories rather than modules,
// so they carry no symbol.
// D9.2, D9.3
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, a plain struct laid out in the open. The declaring node
// is spelled `struct ast_node` because ast.h includes this header for the
// `sym` slot of a node.
#ifndef FORT_SYM_H
#define FORT_SYM_H

#include <stdbool.h>

#include "diag.h"
#include "str.h"
#include "types.h"

/// What a name denotes. The module-level kinds are the importable declarations of
/// module-system.md 3; SYM_FIELD and SYM_ENUM_MEMBER are reached through a type
/// rather than through a scope; SYM_BUILTIN is a universe function.
/// D3.9, D12.2
typedef enum {
    SYM_MODULE,
    SYM_FN,
    SYM_EXTERN_FN,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_ENUM_MEMBER,
    SYM_FIELD,
    SYM_CONST,  // D7.10: a module-level immutable declaration
    SYM_GLOBAL, // D7.10: a module-level `mut` declaration
    SYM_LOCAL,
    SYM_PARAM,
    SYM_BUILTIN,
    SYM_KIND_COUNT, // one past the last kind, for tables
} sym_kind_t;

typedef struct sym sym_t;
struct sym {
    sym_kind_t kind;
    str_t name;
    loc_t decl;                  // D20.4: the declaring node's name_loc
    const type_t* type;          // the error type when `error` is set
    bool mut0;                   // D5.2: level-0 mutability, as type_to_str_decl spells it
    const struct ast_node* node; // the declaring node; NULL for a builtin
    const sym_t* owner;          // the struct of a field, the enum of a member, the module
    bool error;                  // D14.2: the declaration failed to check
};

/// The kind as a reader spells it: "module", "extern fn", "enum member",
/// "constant", "global", "local", "parameter", "builtin".
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
