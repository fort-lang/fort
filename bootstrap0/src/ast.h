// Defines syntax-tree nodes, semantic annotations, and arena operations.
//
// A node location covers its construct. Operator nodes start at the operator token.
// The arena owns all nodes and child lists. Names remain views into lexer storage.
// The source and string pool must outlive the tree.
// The parser leaves annotation fields clear. Later passes fill them.
#ifndef FORT_AST_H
#define FORT_AST_H

#include <stdbool.h>
#include <stdint.h>

#include "containers.h"
#include "diag.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// Syntax-tree node kinds in declaration, type, statement, and expression groups.
typedef enum {
    AST_NONE = 0, // never produced by the parser; the zero of a fresh node

    // ---- module and declarations ----
    AST_MODULE,      // list: imports then declarations, in source order
    AST_IMPORT,      // a: path; b: `as` alias or NULL; list: brace items
    AST_PATH,        // list: the identifiers of an import path
    AST_IMPORT_ITEM, // name: the symbol; a: `as` alias or NULL
    AST_FN_DECL,     // a: return type; list: params; b: body, NULL when extern
    AST_PARAM,       // a: type; name: the parameter
    AST_STRUCT_DECL, // name; list: fields
    AST_FIELD_DECL,  // a: type; name: the field
    AST_ENUM_DECL,   // name; list: members
    AST_ENUM_MEMBER, // name; a: the explicit value or NULL
    AST_VAR_DECL,    // a: type; name; b: initializer (a global or a local)

    // ---- types ----
    AST_TYPE,          // a: base type; flags: the base position's own and mut;
                       // list: the suffixes in source order
    AST_TYPE_PRIM,     // op: the prim_kind_t of prim.h
    AST_TYPE_STRING,   // `string`
    AST_TYPE_VOID,     // `void`
    AST_TYPE_NORETURN, // `noreturn`, a return type only
    AST_TYPE_NAME,     // name: the first identifier; a: the second or NULL
    AST_TYPE_FN,       // a: return type; list: parameter types
    AST_TYPE_SUFFIX,   // op: a suffix_kind_t of types.h; flags: own and mut;
                       // a: the length of an array suffix

    // ---- statements ----
    AST_BLOCK,      // list: the statements
    AST_ASSIGN,     // op: the assignment token; a: target; b: value
    AST_INCDEC,     // op: TOK_PLUS_PLUS or TOK_MINUS_MINUS; a: target
    AST_CALL_STMT,  // a: the call expression
    AST_IF,         // a: condition; b: then block; c: else block or AST_IF
    AST_WHILE,      // a: condition; b: body
    AST_DO_WHILE,   // a: body; b: condition
    AST_FOR,        // a: init; b: condition; c: step; d: body; a, b, c may be NULL
    AST_RANGE_FOR,  // a: element type; name: the element; b: collection; c: body
    AST_SWITCH,     // a: the operand; list: the case clauses
    AST_CASE,       // list: the labels, empty with AST_FLAG_DEFAULT; a: body block
    AST_DEFER,      // a: the deferred statement
    AST_RETURN,     // a: the value or NULL
    AST_BREAK,      //
    AST_CONTINUE,   //
    AST_BRACE_INIT, // list: the initializers; AST_FLAG_DESIGNATED for `.f = v`
    AST_DESIGNATOR, // name: the field; a: its initializer

    // ---- expressions ----
    AST_INT,        // ival: the magnitude
    AST_FLOAT,      // name: the literal
    AST_CHAR,       // ival: the byte value
    AST_STRING,     // name: the decoded bytes
    AST_BOOL,       // ival: 1 for `true`, 0 for `false`
    AST_NULL,       // `null`
    AST_IDENT,      // name: the identifier
    AST_UNARY,      // op: the operator token; a: the operand
    AST_BINARY,     // op: the operator token; a, b: the operands
    AST_TERNARY,    // a: condition; b: then; c: else
    AST_CALL,       // a: the callee; list: the arguments
    AST_INDEX,      // a: the operand; b: the index
    AST_SPAN,       // a: the operand; b: low or NULL; c: high or NULL
    AST_FIELD,      // a: the operand; name: the field (`.`)
    AST_ARROW,      // a: the operand; name: the field (`->`)
    AST_CAST,       // a: the expression; b: the target type
    AST_SIZEOF,     // a: the type
    AST_NEW,        // a: the allocated type; b: the count or NULL
    AST_STRUCT_LIT, // a: the qualified name; b: the brace initializer
    AST_ARRAY_LIT,  // a: the array type; b: the brace initializer

    // The tokens a syntax error made the parser skip; never has children. It stands
    // wherever a list holds children of that level, so every pass that walks such a
    // list skips a node of this kind.
    AST_ERROR,

    AST_KIND_COUNT
} ast_kind_t;

// The bits of `flags`.
enum {
    AST_FLAG_OWN = 1U,         // AST_TYPE, AST_TYPE_SUFFIX: an `own` in the position
    AST_FLAG_MUT = 2U,         // AST_TYPE, AST_TYPE_SUFFIX: a `mut` in the position
    AST_FLAG_EXTERN = 4U,      // AST_FN_DECL: an `extern fn` declaration
    AST_FLAG_DEFAULT = 8U,     // AST_CASE: the `default` clause
    AST_FLAG_DESIGNATED = 16U, // AST_BRACE_INIT: `.field = value` members
    AST_FLAG_VARIADIC = 32U    // AST_FN_DECL: an extern C variable tail
};

typedef struct ast_node ast_node_t;
struct ast_node {
    ast_kind_t kind;
    loc_t loc;
    // The range of a name that this node declares or mentions.
    // Other nodes use an empty range. The range repeats `loc.file` for diag_error.
    loc_t name_loc;
    loc_t tail_loc; // location of `...` on an extern declaration
    int32_t op;     // a tok_kind_t, a suffix_kind_t or a prim_kind_t; 0 otherwise
    ast_node_t* a;  // the fixed children, in the order the comments above give
    ast_node_t* b;
    ast_node_t* c;
    ast_node_t* d;
    ptrvec_t list; // the children of variable arity, owned by the arena
    str_t name;    // an identifier or the bytes of a literal
    uint64_t ival; // an integer or char literal, 0 or 1 for a bool
    uint32_t flags;

    // ---- annotation slots, zero after parsing ----
    const type_t* type; // the checked type of an expression or type node
    const sym_t* sym;   // what this node's own name token denotes (sym.h)
    uint64_t aux;       // a constant value, an offset or a slot index
    uint32_t ann;       // the annotation bits of the pass that set them
};

// The name of a kind as the S-expression dumper and diagnostics spell it:
// "module", "var-decl", "type-suffix", "binary".
const char* ast_kind_name(ast_kind_t kind);

// Nodes per block of the arena; a block is one allocation.
enum { AST_ARENA_BLOCK_NODES = 256 };

// Owns every node it hands out and the list of every node. Zero-initialized
// storage is a valid empty arena.
typedef struct {
    ast_node_t** blocks; // every block the arena owns
    uint64_t block_len;  // blocks in use
    uint64_t block_cap;  // slots in blocks
    uint64_t used;       // nodes used in the last block
} ast_arena_t;

void ast_arena_init(ast_arena_t* ar);

// Releases every node and every node's list; the arena is empty and usable
// afterwards.
void ast_arena_free(ast_arena_t* ar);

uint64_t ast_arena_count(const ast_arena_t* ar);

// A zeroed node of `kind` at `loc`, owned by the arena.
ast_node_t* ast_new(ast_arena_t* ar, ast_kind_t kind, loc_t loc);

// Appends `child`, which may not be NULL, to `parent`'s list.
void ast_push(ast_node_t* parent, ast_node_t* child);

uint64_t ast_len(const ast_node_t* n);

// The child at `i`, which must be in range.
ast_node_t* ast_child(const ast_node_t* n, uint64_t i);

// The marks of a type position, each written once and after what it qualifies.
bool ast_is_own(const ast_node_t* n);
bool ast_is_mut(const ast_node_t* n);

#endif
