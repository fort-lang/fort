// The S-expression printer for the syntax tree, used by the parser suites
// (toolchain.md 7.5: unit tests cover what language tests cannot observe
// directly). It is test-only: no compiler pass prints a tree.
//
// The form is one line per tree, `(kind field... child...)`, with `nil` for an
// absent fixed child so that the position of every child is visible:
//
//   i32 mut x = 1 + 2;
//   (var x (type (prim i32) mut) (binary + (int 1) (int 2)))
//
// Type positions print their marks as the words `own` and `mut` after what
// they qualify, as the source writes them (D5.3, D17.2).
#ifndef FORT_TEST_AST_DUMP_H
#define FORT_TEST_AST_DUMP_H

#include "ast.h"
#include "str.h"

// Appends the S-expression of `n` to `out`; a NULL node prints as `nil`.
void ast_dump(const ast_node_t* n, sb_t* out);

#endif
