// Prints syntax trees in the S-expression format used by `fort --ast` and parser tests.
//
// The form is one line per tree: `(kind field... child...)`.
// An absent fixed child prints as `nil`, which keeps each child position visible.
//
// Example: i32 mut x = 1 + 2; (var x (type (prim i32) mut) (binary + (int 1) (int 2))).
//
// Type positions print their marks as the words `own` and `mut` after what
// they qualify, as the source writes them.
#ifndef FORT_AST_DUMP_H
#define FORT_AST_DUMP_H

#include "ast.h"
#include "str.h"

// Appends the S-expression of `n` to `out`; a NULL node prints as `nil`.
void ast_dump(const ast_node_t* n, sb_t* out);

#endif
