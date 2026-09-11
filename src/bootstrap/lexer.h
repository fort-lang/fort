// The lexer of the bootstrap compiler: a whole source file to a token array
// (toolchain.md 8, lexer) applying the lexical structure of D2.
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants, a plain switch on the
// current byte. Every token records its kind, its 1-based line and column
// (a tab is one column, D14.2) and its byte range in the source; integer and
// char literals carry their value, identifiers a view into the source and
// string literals their decoded bytes interned in the caller's pool. A
// lexical error is reported and lexing resumes at the start of the next
// line, so the file is lexed whole and reports at most one lexical
// diagnostic per line (D14.2).
#ifndef FORT_LEXER_H
#define FORT_LEXER_H

#include <stdbool.h>
#include <stdint.h>

#include "str.h"

// Token kinds: the literal classes, one entry per keyword of D2.4, one per
// operator or punctuation token of D2.10, and the end of the file. Keywords
// are TOK_KW_<WORD>; operators are named after their glyphs, `_WRAP` for a
// `%` wrapping variant and `_ASSIGN` for a compound assignment.
typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,  // text: the identifier, a view into the source
    TOK_INT,    // ival: the magnitude (D2.5)
    TOK_FLOAT,  // text: the literal (D2.6); unsupported by the bootstrap
    TOK_CHAR,   // ival: the byte value (D2.7)
    TOK_STRING, // text: the decoded bytes, interned (D2.9)

    // Keywords in the order of D2.4.
    TOK_KW_AS,
    TOK_KW_BOOL,
    TOK_KW_BREAK,
    TOK_KW_CASE,
    TOK_KW_CAST,
    TOK_KW_CHAR,
    TOK_KW_CONTINUE,
    TOK_KW_DEFAULT,
    TOK_KW_DEFER,
    TOK_KW_DO,
    TOK_KW_ELSE,
    TOK_KW_ENUM,
    TOK_KW_EXTERN,
    TOK_KW_F32,
    TOK_KW_F64,
    TOK_KW_FALSE,
    TOK_KW_FN,
    TOK_KW_FOR,
    TOK_KW_I8,
    TOK_KW_I16,
    TOK_KW_I32,
    TOK_KW_I64,
    TOK_KW_IF,
    TOK_KW_IMPORT,
    TOK_KW_MUT,
    TOK_KW_NEW,
    TOK_KW_NORETURN,
    TOK_KW_NULL,
    TOK_KW_OWN,
    TOK_KW_RETURN,
    TOK_KW_SIZEOF,
    TOK_KW_STRING,
    TOK_KW_STRUCT,
    TOK_KW_SWITCH,
    TOK_KW_TRUE,
    TOK_KW_U8,
    TOK_KW_U16,
    TOK_KW_U32,
    TOK_KW_U64,
    TOK_KW_VOID,
    TOK_KW_WHILE,

    // Operators and punctuation in the order of D2.10.
    TOK_PLUS,              // +
    TOK_MINUS,             // -
    TOK_STAR,              // *
    TOK_SLASH,             // /
    TOK_PERCENT,           // %
    TOK_PLUS_WRAP,         // +%
    TOK_MINUS_WRAP,        // -%
    TOK_STAR_WRAP,         // *%
    TOK_ASSIGN,            // =
    TOK_PLUS_ASSIGN,       // +=
    TOK_MINUS_ASSIGN,      // -=
    TOK_STAR_ASSIGN,       // *=
    TOK_SLASH_ASSIGN,      // /=
    TOK_PERCENT_ASSIGN,    // %=
    TOK_PLUS_WRAP_ASSIGN,  // +%=
    TOK_MINUS_WRAP_ASSIGN, // -%=
    TOK_STAR_WRAP_ASSIGN,  // *%=
    TOK_AMP_ASSIGN,        // &=
    TOK_PIPE_ASSIGN,       // |=
    TOK_CARET_ASSIGN,      // ^=
    TOK_SHL_ASSIGN,        // <<=
    TOK_SHR_ASSIGN,        // >>=
    TOK_EQ,                // ==
    TOK_NE,                // !=
    TOK_LT,                // <
    TOK_LE,                // <=
    TOK_GT,                // >
    TOK_GE,                // >=
    TOK_AND_AND,           // &&
    TOK_PIPE_PIPE,         // ||
    TOK_BANG,              // !
    TOK_AMP,               // &
    TOK_PIPE,              // |
    TOK_CARET,             // ^
    TOK_TILDE,             // ~
    TOK_SHL,               // <<
    TOK_SHR,               // >>
    TOK_PLUS_PLUS,         // ++
    TOK_MINUS_MINUS,       // --
    TOK_QUESTION,          // ?
    TOK_COLON,             // :
    TOK_COLON_COLON,       // ::
    TOK_DOT,               // .
    TOK_ARROW,             // ->
    TOK_DOT_DOT,           // ..
    TOK_LPAREN,            // (
    TOK_RPAREN,            // )
    TOK_LBRACKET,          // [
    TOK_RBRACKET,          // ]
    TOK_LBRACE,            // {
    TOK_RBRACE,            // }
    TOK_COMMA,             // ,
    TOK_SEMI,              // ;
    TOK_AT,                // @, the span suffix (D2.10, D3.5)

    TOK_COUNT
} tok_kind_t;

// The keyword and operator ranges of tok_kind_t, both inclusive.
enum {
    TOK_KW_FIRST = TOK_KW_AS,
    TOK_KW_LAST = TOK_KW_WHILE,
    TOK_OP_FIRST = TOK_PLUS,
    TOK_OP_LAST = TOK_AT
};

// The spelling of a keyword or operator kind, or a description of the other
// kinds ("identifier", "integer literal", "float literal", "char literal",
// "string literal", "end of file").
const char* tok_kind_name(tok_kind_t kind);

typedef struct {
    tok_kind_t kind;
    uint32_t line; // 1-based line of the first byte
    uint32_t col;  // 1-based column of the first byte; a tab is one column
    uint64_t off;  // byte offset of the first byte in the source
    uint64_t len;  // byte length of the token in the source; 0 for EOF
    uint64_t ival; // TOK_INT: the magnitude; TOK_CHAR: the byte value
    str_t text;    // TOK_STRING: the decoded bytes, owned by the pool; EOF: the
                   // zero view; every other kind: the token's bytes, a view
                   // into the source
} token_t;

// A growable array of tokens. Zero-initialized storage is a valid empty
// vector.
typedef struct {
    token_t* items; // the slots, or NULL
    uint64_t len;   // slots in use
    uint64_t cap;   // slots allocated
} tokvec_t;

void tokvec_init(tokvec_t* v);

// Releases the slots; the vector is empty and usable afterwards.
void tokvec_free(tokvec_t* v);

// Ensures room for `extra` more tokens.
void tokvec_reserve(tokvec_t* v, uint64_t extra);

void tokvec_push(tokvec_t* v, token_t t);

// Drops every token after the first `len`; a `len` at or past the end leaves
// the vector alone.
void tokvec_truncate(tokvec_t* v, uint64_t len);

// Lexes the whole of `source`, which the caller keeps alive and unchanged
// for as long as the tokens are used, appending the tokens to `out` ending
// in TOK_EOF; string literals are decoded into `pool`. `file` names the
// source in diagnostics. Returns whether the file was clean. A lexical
// error is reported and lexing resumes at the start of the next line
// (D14.2), dropping the whole of that line and reporting at most one
// diagnostic per line, so `out` covers the rest of the file and ends in
// TOK_EOF either way and the tokens can be parsed. It opens the file's
// budget of DIAG_MAX_PER_FILE diagnostics, which the parser then spends what
// is left of.
bool lex_file(const char* file, str_t source, str_pool_t* pool, tokvec_t* out);

#endif
