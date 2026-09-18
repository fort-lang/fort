// Converts one source file into a token array.
// Each token stores its kind, 1-based location, and source byte range.
// Integer and character tokens store values. Identifier tokens view the source.
// String tokens store decoded bytes in the caller's pool.
// After an error, the lexer resumes at the next line. It reports one error per line.
#ifndef FORT_LEXER_H
#define FORT_LEXER_H

#include <stdbool.h>
#include <stdint.h>

#include "str.h"

// Token kinds: the literal classes, one entry per keyword, one per operator or
// punctuation token, and the end of the file. Keywords are TOK_KW_<WORD>;
// operators are named after their glyphs, `_WRAP` for a `%` wrapping variant and
// `_ASSIGN` for a compound assignment.
typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,  // text: the identifier, a view into the source
    TOK_INT,    // ival: the magnitude
    TOK_FLOAT,  // text: the literal; unsupported by the bootstrap
    TOK_CHAR,   // ival: the byte value
    TOK_STRING, // text: the decoded bytes, interned

    // Keywords appear in source order.
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

    // Operators and punctuation appear in source order.
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
    TOK_DOT,               // .
    TOK_ARROW,             // ->
    TOK_DOT_DOT,           // ..
    TOK_ELLIPSIS,          // ...
    TOK_LPAREN,            // (
    TOK_RPAREN,            // )
    TOK_LBRACKET,          // [
    TOK_RBRACKET,          // ]
    TOK_LBRACE,            // {
    TOK_RBRACE,            // }
    TOK_COMMA,             // ,
    TOK_SEMI,              // ;
    TOK_AT,                // @, the span suffix

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

// Appends the tokens from `source` to `out`, followed by TOK_EOF.
// The caller keeps `source` unchanged and alive while it uses the tokens.
// String tokens store decoded bytes in `pool`. `file` identifies diagnostics.
// Returns true when the file has no lexical errors.
// After an error, drops that line's tokens and resumes at the next line.
// Starts the DIAG_MAX_PER_FILE budget, which the parser continues to use.
bool lex_file(const char* file, str_t source, str_pool_t* pool, tokvec_t* out);

// Appends the `--tokens` output to `out`. Each token uses one line:
//
// <line>:<col>-<end_line>:<end_col> <ival> "<spelling>" <kind>
//
// Escapes the spelling so newline, tab, quote, and nonprintable bytes stay on one line.
// The range ends after the token's last byte.
// No token contains a line break, so its end is `<col> + <len>` on the same line.
// Direct lexer tests hold this format.
void tok_dump(const tokvec_t* v, sb_t* out);

#endif
