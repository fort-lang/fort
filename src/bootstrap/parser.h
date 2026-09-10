// The parser of the bootstrap compiler (toolchain.md 8, parser): recursive
// descent over the token array of one file, producing one AST_MODULE node in
// the caller's arena.
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants, plain switches on the
// current token.
//
// Diagnostics. A syntax error stops the file after one diagnostic (D14.2):
// every parse function returns NULL from there on and parse_module returns
// NULL with the tokens it had consumed left behind in the arena.
//
// Speculation. The grammar is LL(1) but for the three points of grammar.md 7:
// the declaration-versus-statement choice, the array literal in an expression
// and the two `for` forms. Each is decided by a speculative parse over the
// token array that reports nothing and always rewinds, after which the
// committed parse re-parses the same tokens loudly. A rewind therefore leaves
// no diagnostics, and an error inside a speculation is reported once, by
// whichever branch the decision committed to.
//
// Nesting. Blocks, brace initializers, bracketed groups, unary operands,
// conditional branches and types count towards one depth limit of 256
// (D2.11), so the parser's recursion is bounded and the self-hosted compiler
// needs no unbounded stack.
//
// Type placement rules. The parser refuses the spellings D5.3 and D17.2 make
// unwritable, since all of them are decidable from the written form: a `mut`
// or `own` before the base type, a marker written twice in one position, a
// `mut` before the `own` of its position, a `mut` on the position a
// fixed-array suffix follows, an `own` after a fixed-array suffix or after a
// base type other than `string`, and an array suffix after a trailing
// reference suffix (D3.6). What needs the resolved type stays with the type
// builder and the checker: whether a named base is a reference, `void`
// outside `void*`, an array length that is not a positive constant, and a
// `mut` in the outermost position of a field, a return type or a cast target.
//
// Features the C bootstrap deliberately lacks are reported here as `not
// supported by the bootstrap compiler: <feature>` (toolchain.md 7.3): float
// literals, a second array or slice level in one type (`i32[3][4]`,
// `i32[4]@`, `u8@@`, `node@[4]`), `do`-`while` and `?:`. A speculative parse
// skips the check, so the construct still decides the shape and the
// committed parse reports it.
#ifndef FORT_PARSER_H
#define FORT_PARSER_H

#include <stdint.h>

#include "ast.h"
#include "lexer.h"

// Parses the tokens of one file, which must end in TOK_EOF as lex_file
// leaves them, into an AST_MODULE whose list holds the imports then the
// declarations in source order (D9.3). `file` names the source in
// diagnostics. The nodes come from `arena` and the names in them are views
// into the source and into the pool the tokens were lexed with, so both must
// outlive the tree. On a syntax error one diagnostic is reported and NULL is
// returned.
ast_node_t* parse_module(const char* file, const token_t* toks, uint64_t ntoks, ast_arena_t* arena);

#endif
