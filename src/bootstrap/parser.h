// Parses one token array into a syntax tree.
//
// The parser recovers at statement, case, field, and declaration boundaries.
// It keeps skipped regions as AST_ERROR nodes and continues through the file.
// Speculative parses never report diagnostics and always rewind.
// The parser limits nested constructs to 256 levels.
//
// The bootstrap rejects float literals, nested aggregate types, `do`-`while`, and `?:`.
#ifndef FORT_PARSER_H
#define FORT_PARSER_H

#include <stdint.h>

#include "ast.h"
#include "lexer.h"

// Parses one file into an AST_MODULE node. Imports precede declarations in source order.
// `toks` must end in TOK_EOF. The source and string pool must outlive the tree.
// `arena` owns the result nodes.
// Syntax errors use the budget that lex_file started.
// The result is never NULL. Syntax errors produce a partial tree with AST_ERROR nodes.
ast_node_t* parse_module(const char* file, const token_t* toks, uint64_t ntoks, ast_arena_t* arena);

#endif
