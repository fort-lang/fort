// The syntax tree of the bootstrap compiler (toolchain.md 8, parser): one fat
// tagged node for every production of grammar.md, an arena that owns the
// nodes, and the small queries the later passes need.
//
// One node type carries every construct, so the file mirrors what the
// self-hosted compiler will do: no unions, no function pointers, no macros
// beyond constants, every field laid out in the open. `kind` names the
// production; `loc` is the position of the construct's first token, except on
// the nodes named after an operator or a punctuation mark (unary, binary,
// assignment, increment, call, index, slice, field, arrow, type suffix),
// which carry that token's own position, since a runtime error is reported at
// the operator of the failing operation and the overwrite check at the `=` of
// an assignment (toolchain.md 4, D17.11); `op` is
// the small kind code the construct needs (a token kind for an operator, a
// suffix kind for a type suffix, a primitive kind for a primitive type); `a`
// to `d` are the fixed children, absent ones NULL; `list` holds the children
// of variable arity (statements of a block, arguments of a call, suffixes of
// a type); `name` is an identifier or the bytes of a literal; `ival` is a
// literal's value; `flags` holds the marks (`own`, `mut`, `extern`,
// designated, `default`).
//
// A node is never freed on its own: the arena owns every node and its list,
// and frees them together (toolchain.md 8, memory). Names and literal bytes
// are views into the source or into the lexer's string pool, so both must
// outlive the tree.
//
// The annotation slots at the end are zero after parsing; the checker fills
// them (toolchain.md 8, checker) and codegen reads them.
#ifndef FORT_AST_H
#define FORT_AST_H

#include <stdbool.h>
#include <stdint.h>

#include "containers.h"
#include "diag.h"
#include "str.h"
#include "types.h"

// The productions of grammar.md, in its order: module and declarations,
// types, statements, expressions.
typedef enum {
    AST_NONE = 0, // never produced by the parser; the zero of a fresh node

    // ---- module and declarations (grammar.md 2, 3) ----
    AST_MODULE,      // list: imports then declarations, in source order
    AST_IMPORT,      // a: path; b: `as` alias or NULL; list: brace items
    AST_PATH,        // list: the identifiers of an import path (D9.1)
    AST_IMPORT_ITEM, // name: the symbol; a: `as` alias or NULL
    AST_FN_DECL,     // a: return type; list: params; b: body, NULL when extern
    AST_PARAM,       // a: type; name: the parameter
    AST_STRUCT_DECL, // name; list: fields
    AST_FIELD_DECL,  // a: type; name: the field
    AST_ENUM_DECL,   // name; list: members
    AST_ENUM_MEMBER, // name; a: the explicit value or NULL
    AST_VAR_DECL,    // a: type; name; b: initializer (a global or a local)

    // ---- types (grammar.md 4) ----
    AST_TYPE,          // a: base type; flags: the base position's own and mut;
                       // list: the suffixes in source order
    AST_TYPE_PRIM,     // op: the prim_kind_t of prim.h
    AST_TYPE_STRING,   // `string`
    AST_TYPE_VOID,     // `void`
    AST_TYPE_NORETURN, // `noreturn`, a return type only (D8.5)
    AST_TYPE_NAME,     // name: the first identifier; a: the second or NULL (D9.4)
    AST_TYPE_FN,       // a: return type; list: parameter types (D3.10)
    AST_TYPE_SUFFIX,   // op: a suffix_kind_t of types.h; flags: own and mut;
                       // a: the length of an array suffix

    // ---- statements (grammar.md 5) ----
    AST_BLOCK,      // list: the statements
    AST_ASSIGN,     // op: the assignment token; a: target; b: value
    AST_INCDEC,     // op: TOK_PLUS_PLUS or TOK_MINUS_MINUS; a: target
    AST_CALL_STMT,  // a: the call expression (D7.3)
    AST_IF,         // a: condition; b: then block; c: else block or AST_IF
    AST_WHILE,      // a: condition; b: body
    AST_DO_WHILE,   // a: body; b: condition
    AST_FOR,        // a: init; b: condition; c: step; d: body; a, b, c may be NULL
    AST_RANGE_FOR,  // a: element type; name: the element; b: collection; c: body
    AST_SWITCH,     // a: the operand; list: the case clauses
    AST_CASE,       // list: the labels, empty with AST_FLAG_DEFAULT; a: body block
    AST_DEFER,      // a: the deferred statement (D7.8)
    AST_RETURN,     // a: the value or NULL
    AST_BREAK,      //
    AST_CONTINUE,   //
    AST_BRACE_INIT, // list: the initializers; AST_FLAG_DESIGNATED for `.f = v`
    AST_DESIGNATOR, // name: the field; a: its initializer

    // ---- expressions (grammar.md 6) ----
    AST_INT,        // ival: the magnitude (D2.5)
    AST_FLOAT,      // name: the literal (D2.6)
    AST_CHAR,       // ival: the byte value (D2.7)
    AST_STRING,     // name: the decoded bytes (D2.9)
    AST_BOOL,       // ival: 1 for `true`, 0 for `false`
    AST_NULL,       // `null` (D10.5)
    AST_IDENT,      // name: the identifier
    AST_UNARY,      // op: the operator token; a: the operand
    AST_BINARY,     // op: the operator token; a, b: the operands
    AST_TERNARY,    // a: condition; b: then; c: else (D6.6)
    AST_CALL,       // a: the callee; list: the arguments
    AST_INDEX,      // a: the operand; b: the index (D6.8)
    AST_SLICE,      // a: the operand; b: low or NULL; c: high or NULL (D6.9)
    AST_FIELD,      // a: the operand; name: the field (`.`)
    AST_ARROW,      // a: the operand; name: the field (`->`, D6.10)
    AST_CAST,       // a: the expression; b: the target type (D6.4)
    AST_SIZEOF,     // a: the type (D3.15)
    AST_NEW,        // a: the allocated type; b: the count or NULL (D10.2)
    AST_STRUCT_LIT, // a: the qualified name; b: the brace initializer (D6.5)
    AST_ARRAY_LIT,  // a: the array type; b: the brace initializer (D6.5)

    AST_KIND_COUNT
} ast_kind_t;

// The bits of `flags`.
enum {
    AST_FLAG_OWN = 1U,        // AST_TYPE, AST_TYPE_SUFFIX: an `own` in the position (D17.2)
    AST_FLAG_MUT = 2U,        // AST_TYPE, AST_TYPE_SUFFIX: a `mut` in the position (D5.3)
    AST_FLAG_EXTERN = 4U,     // AST_FN_DECL: an `extern fn` declaration (D9.8)
    AST_FLAG_DEFAULT = 8U,    // AST_CASE: the `default` clause (D7.6)
    AST_FLAG_DESIGNATED = 16U // AST_BRACE_INIT: `.field = value` members (D6.5)
};

typedef struct ast_node ast_node_t;
struct ast_node {
    ast_kind_t kind;
    loc_t loc;
    int32_t op;    // a tok_kind_t, a suffix_kind_t or a prim_kind_t; 0 otherwise
    ast_node_t* a; // the fixed children, in the order the comments above give
    ast_node_t* b;
    ast_node_t* c;
    ast_node_t* d;
    ptrvec_t list; // the children of variable arity, owned by the arena
    str_t name;    // an identifier or the bytes of a literal
    uint64_t ival; // an integer or char literal, 0 or 1 for a bool
    uint32_t flags;

    // ---- annotation slots, zero after parsing ----
    const type_t* type; // the checked type of an expression or type node
    const void* sym;    // the declaration a name resolves to, opaque here
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

// The number of nodes the arena has handed out.
uint64_t ast_arena_count(const ast_arena_t* ar);

// A zeroed node of `kind` at `loc`, owned by the arena.
ast_node_t* ast_new(ast_arena_t* ar, ast_kind_t kind, loc_t loc);

// Appends `child`, which may not be NULL, to `parent`'s list.
void ast_push(ast_node_t* parent, ast_node_t* child);

// The number of children in `n`'s list.
uint64_t ast_len(const ast_node_t* n);

// The child at `i`, which must be in range.
ast_node_t* ast_child(const ast_node_t* n, uint64_t i);

// Whether the node carries the mark: the `own` or `mut` of a type position
// (D5.3, D17.2).
bool ast_is_own(const ast_node_t* n);
bool ast_is_mut(const ast_node_t* n);

#endif
