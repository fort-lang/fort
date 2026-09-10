// The lexer; see lexer.h. Positions and spans follow D14.2, the token
// classes D2.1 to D2.10, the messages core-language.md 2.
#include "lexer.h"

#include <stddef.h>

#include "diag.h"

// ---- token kinds ----------------------------------------------------------

const char* tok_kind_name(tok_kind_t kind) {
    switch (kind) {
    case TOK_EOF:
        return "end of file";
    case TOK_IDENT:
        return "identifier";
    case TOK_INT:
        return "integer literal";
    case TOK_FLOAT:
        return "float literal";
    case TOK_CHAR:
        return "char literal";
    case TOK_STRING:
        return "string literal";
    case TOK_KW_AS:
        return "as";
    case TOK_KW_BOOL:
        return "bool";
    case TOK_KW_BREAK:
        return "break";
    case TOK_KW_CASE:
        return "case";
    case TOK_KW_CAST:
        return "cast";
    case TOK_KW_CHAR:
        return "char";
    case TOK_KW_CONTINUE:
        return "continue";
    case TOK_KW_DEFAULT:
        return "default";
    case TOK_KW_DEFER:
        return "defer";
    case TOK_KW_DO:
        return "do";
    case TOK_KW_ELSE:
        return "else";
    case TOK_KW_ENUM:
        return "enum";
    case TOK_KW_EXTERN:
        return "extern";
    case TOK_KW_F32:
        return "f32";
    case TOK_KW_F64:
        return "f64";
    case TOK_KW_FALSE:
        return "false";
    case TOK_KW_FN:
        return "fn";
    case TOK_KW_FOR:
        return "for";
    case TOK_KW_I8:
        return "i8";
    case TOK_KW_I16:
        return "i16";
    case TOK_KW_I32:
        return "i32";
    case TOK_KW_I64:
        return "i64";
    case TOK_KW_IF:
        return "if";
    case TOK_KW_IMPORT:
        return "import";
    case TOK_KW_MUT:
        return "mut";
    case TOK_KW_NEW:
        return "new";
    case TOK_KW_NORETURN:
        return "noreturn";
    case TOK_KW_NULL:
        return "null";
    case TOK_KW_OWN:
        return "own";
    case TOK_KW_RETURN:
        return "return";
    case TOK_KW_SIZEOF:
        return "sizeof";
    case TOK_KW_STRING:
        return "string";
    case TOK_KW_STRUCT:
        return "struct";
    case TOK_KW_SWITCH:
        return "switch";
    case TOK_KW_TRUE:
        return "true";
    case TOK_KW_U8:
        return "u8";
    case TOK_KW_U16:
        return "u16";
    case TOK_KW_U32:
        return "u32";
    case TOK_KW_U64:
        return "u64";
    case TOK_KW_VOID:
        return "void";
    case TOK_KW_WHILE:
        return "while";
    case TOK_PLUS:
        return "+";
    case TOK_MINUS:
        return "-";
    case TOK_STAR:
        return "*";
    case TOK_SLASH:
        return "/";
    case TOK_PERCENT:
        return "%";
    case TOK_PLUS_WRAP:
        return "+%";
    case TOK_MINUS_WRAP:
        return "-%";
    case TOK_STAR_WRAP:
        return "*%";
    case TOK_ASSIGN:
        return "=";
    case TOK_PLUS_ASSIGN:
        return "+=";
    case TOK_MINUS_ASSIGN:
        return "-=";
    case TOK_STAR_ASSIGN:
        return "*=";
    case TOK_SLASH_ASSIGN:
        return "/=";
    case TOK_PERCENT_ASSIGN:
        return "%=";
    case TOK_PLUS_WRAP_ASSIGN:
        return "+%=";
    case TOK_MINUS_WRAP_ASSIGN:
        return "-%=";
    case TOK_STAR_WRAP_ASSIGN:
        return "*%=";
    case TOK_AMP_ASSIGN:
        return "&=";
    case TOK_PIPE_ASSIGN:
        return "|=";
    case TOK_CARET_ASSIGN:
        return "^=";
    case TOK_SHL_ASSIGN:
        return "<<=";
    case TOK_SHR_ASSIGN:
        return ">>=";
    case TOK_EQ:
        return "==";
    case TOK_NE:
        return "!=";
    case TOK_LT:
        return "<";
    case TOK_LE:
        return "<=";
    case TOK_GT:
        return ">";
    case TOK_GE:
        return ">=";
    case TOK_AND_AND:
        return "&&";
    case TOK_PIPE_PIPE:
        return "||";
    case TOK_BANG:
        return "!";
    case TOK_AMP:
        return "&";
    case TOK_PIPE:
        return "|";
    case TOK_CARET:
        return "^";
    case TOK_TILDE:
        return "~";
    case TOK_SHL:
        return "<<";
    case TOK_SHR:
        return ">>";
    case TOK_PLUS_PLUS:
        return "++";
    case TOK_MINUS_MINUS:
        return "--";
    case TOK_QUESTION:
        return "?";
    case TOK_COLON:
        return ":";
    case TOK_COLON_COLON:
        return "::";
    case TOK_DOT:
        return ".";
    case TOK_ARROW:
        return "->";
    case TOK_DOT_DOT:
        return "..";
    case TOK_LPAREN:
        return "(";
    case TOK_RPAREN:
        return ")";
    case TOK_LBRACKET:
        return "[";
    case TOK_RBRACKET:
        return "]";
    case TOK_LBRACE:
        return "{";
    case TOK_RBRACE:
        return "}";
    case TOK_COMMA:
        return ",";
    case TOK_SEMI:
        return ";";
    case TOK_AT:
        return "@";
    case TOK_COUNT:
        break;
    }
    fatal_internal("tok_kind_name: bad kind");
}

// The keyword kind of `word`, or TOK_IDENT when it is none (D2.3, D2.4).
static tok_kind_t keyword_kind(str_t word) {
    for (int k = TOK_KW_FIRST; k <= TOK_KW_LAST; k++) {
        if (str_eq(word, str_from_cstr(tok_kind_name((tok_kind_t)k)))) {
            return (tok_kind_t)k;
        }
    }
    return TOK_IDENT;
}

// The words reserved for future use, usable nowhere (D2.4).
static const char* const RESERVED_WORDS[] = {
    "async",
    "await",
    "const",
    "match",
    "pub",
    "priv",
    "trait",
    "type",
    "union",
    "yield",
};
enum { RESERVED_WORD_COUNT = sizeof(RESERVED_WORDS) / sizeof(RESERVED_WORDS[0]) };

static bool is_reserved_word(str_t word) {
    for (int i = 0; i < RESERVED_WORD_COUNT; i++) {
        if (str_eq(word, str_from_cstr(RESERVED_WORDS[i]))) {
            return true;
        }
    }
    return false;
}

// ---- token vector -----------------------------------------------------------

void tokvec_init(tokvec_t* v) {
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
}

void tokvec_free(tokvec_t* v) {
    mem_free(v->items);
    tokvec_init(v);
}

void tokvec_reserve(tokvec_t* v, uint64_t extra) {
    const uint64_t need = mem_add(v->len, extra);
    if (need <= v->cap) {
        return;
    }
    const uint64_t cap = mem_grown_cap(v->cap, need);
    token_t* bigger = mem_alloc(mem_mul(cap, sizeof(token_t)));
    for (uint64_t i = 0; i < v->len; i++) {
        bigger[i] = v->items[i];
    }
    mem_free(v->items);
    v->items = bigger;
    v->cap = cap;
}

void tokvec_push(tokvec_t* v, token_t t) {
    tokvec_reserve(v, 1U);
    v->items[v->len] = t;
    v->len++;
}

// ---- byte classes -----------------------------------------------------------

// Byte values the classification needs by name.
enum {
    BYTE_NUL = 0x00,
    BYTE_TAB = 0x09,
    BYTE_LF = 0x0A,
    BYTE_CR = 0x0D,
    BYTE_SPACE = 0x20,
    BYTE_TILDE = 0x7E,    // the last printable ASCII byte
    BYTE_NON_ASCII = 0x80 // the first non-ASCII byte
};

// The bytes of the UTF-8 byte-order mark (D2.1).
enum { BOM_0 = 0xEF, BOM_1 = 0xBB, BOM_2 = 0xBF, BOM_LEN = 3 };

// The radices of the integer literal forms (D2.5).
enum { RADIX_BIN = 2, RADIX_OCT = 8, RADIX_DEC = 10, RADIX_HEX = 16 };

// The hex digits an escape needs (D2.8).
enum { ESCAPE_HEX_DIGITS = 2 };

static bool is_letter(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_dec_digit(int c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_letter(int c) {
    return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_ident_byte(int c) {
    return is_letter(c) || is_dec_digit(c);
}

static bool is_printable(int c) {
    return c >= BYTE_SPACE && c <= BYTE_TILDE;
}

static bool is_non_ascii(int c) {
    return c >= BYTE_NON_ASCII;
}

// The value of a digit in `radix`, or -1 when `c` is no such digit.
static int digit_value(int c, int radix) {
    int v = -1;
    if (is_dec_digit(c)) {
        v = c - '0';
    } else if (c >= 'a' && c <= 'f') {
        v = c - 'a' + RADIX_DEC;
    } else if (c >= 'A' && c <= 'F') {
        v = c - 'A' + RADIX_DEC;
    }
    if (v >= radix) {
        return -1;
    }
    return v;
}

// ---- lexer state -------------------------------------------------------------

typedef struct {
    const char* file;
    str_t src;
    uint64_t pos;  // byte offset of the next byte
    uint32_t line; // line of the next byte
    uint32_t col;  // column of the next byte
    str_pool_t* pool;
    tokvec_t* out;
    sb_t buf; // the decoded bytes of the string literal being lexed
    sb_t msg; // the diagnostic being built
} lexer_t;

// The byte `ahead` bytes after the next one, or -1 past the end.
static int peek_at(const lexer_t* lx, uint64_t ahead) {
    const uint64_t at = lx->pos + ahead;
    if (at >= lx->src.len) {
        return -1;
    }
    return (unsigned char)lx->src.ptr[at];
}

static int peek(const lexer_t* lx) {
    return peek_at(lx, 0);
}

static bool at_end(const lexer_t* lx) {
    return lx->pos >= lx->src.len;
}

// Consumes the next byte, which exists, and tracks the position: a newline
// starts the next line, every other byte (a tab too) is one column.
static void advance(lexer_t* lx) {
    if (peek(lx) == BYTE_LF) {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static loc_t here(const lexer_t* lx) {
    return loc_make(lx->file, lx->line, lx->col);
}

// Reports the message built in lx->msg at `at`; always false so that a
// lexing function can `return fail(lx, at)`.
static bool fail(lexer_t* lx, loc_t at) {
    diag_error(at, msg_end(&lx->msg));
    return false;
}

// Reports a fixed message at `at`.
static bool fail_text(lexer_t* lx, loc_t at, const char* text) {
    msg_begin(&lx->msg);
    msg_str(&lx->msg, text);
    return fail(lx, at);
}

// Appends a byte as 0xHH.
static void msg_hex_byte(sb_t* m, int byte) {
    static const char* const HEX_DIGITS = "0123456789ABCDEF";
    msg_str(m, "0x");
    sb_push(m, HEX_DIGITS[byte / RADIX_HEX]);
    sb_push(m, HEX_DIGITS[byte % RADIX_HEX]);
}

// A token of `kind` starting at `start` (whose position is `at`) and ending
// at the next byte, with the source bytes as text.
static token_t make_token(const lexer_t* lx, tok_kind_t kind, uint64_t start, loc_t at) {
    token_t t;
    t.kind = kind;
    t.line = at.line;
    t.col = at.col;
    t.off = start;
    t.len = lx->pos - start;
    t.ival = 0;
    // An empty source has no bytes to point at: str_from_span keeps the null
    // view rather than forming lx->src.ptr + start on a NULL pointer.
    t.text =
        lx->src.ptr == NULL ? str_from_span(NULL, 0) : str_from_span(lx->src.ptr + start, t.len);
    return t;
}

// ---- whitespace and comments (D2.1, D2.2) ----------------------------------

// Skips whitespace and line comments, the only comment form (D2.2); false
// on the adjacent pair `/*`, which is a lexical error rather than a division
// by a dereference. `a / *p`, with the operators separated, is that
// division and reaches lex_operator.
static bool skip_blanks(lexer_t* lx) {
    for (;;) {
        const int c = peek(lx);
        if (c == BYTE_SPACE || c == BYTE_TAB || c == BYTE_LF || c == BYTE_CR) {
            advance(lx);
        } else if (c == '/' && peek_at(lx, 1) == '/') {
            while (!at_end(lx) && peek(lx) != BYTE_LF) {
                advance(lx);
            }
        } else if (c == '/' && peek_at(lx, 1) == '*') {
            return fail_text(lx, here(lx), "block comments are not supported, use '//'");
        } else {
            return true;
        }
    }
}

// ---- identifiers and keywords (D2.3, D2.4) ---------------------------------

static bool lex_ident(lexer_t* lx) {
    const loc_t at = here(lx);
    const uint64_t start = lx->pos;
    while (is_ident_byte(peek(lx))) {
        advance(lx);
    }
    token_t t = make_token(lx, TOK_IDENT, start, at);
    if (is_reserved_word(t.text)) {
        msg_begin(&lx->msg);
        msg_quote(&lx->msg, t.text);
        msg_str(&lx->msg, " is a reserved word");
        return fail(lx, at);
    }
    t.kind = keyword_kind(t.text);
    tokvec_push(lx->out, t);
    return true;
}

// ---- numbers (D2.5, D2.6) ---------------------------------------------------

// Consumes a run of digits of `radix` with `_` between two of them (D2.5),
// counting the digits into `*count` and, when `value` is not NULL,
// accumulating them into `*value` and setting `*overflow` once the
// accumulation would pass 2^64 - 1; false only on a misplaced `_`. An
// overflow is an error of integer literals alone, and whether the token is
// one is known only after the float lookahead (D2.6), so the caller reports
// it. Digits beyond the radix end the run silently.
static bool lex_digits(lexer_t* lx, int radix, uint64_t* value, uint64_t* count, bool* overflow) {
    for (;;) {
        const int c = peek(lx);
        if (c == '_') {
            const loc_t at = here(lx);
            if (*count == 0 || digit_value(peek_at(lx, 1), radix) < 0) {
                return fail_text(lx, at, "'_' must stand between two digits");
            }
            advance(lx);
            continue;
        }
        const int d = digit_value(c, radix);
        if (d < 0) {
            return true;
        }
        if (value != NULL) {
            if (*value > (UINT64_MAX - (uint64_t)d) / (uint64_t)radix) {
                *overflow = true;
            } else {
                *value = *value * (uint64_t)radix + (uint64_t)d;
            }
        }
        *count += 1;
        advance(lx);
    }
}

// The name of a radix in a message.
static const char* radix_name(int radix) {
    if (radix == RADIX_BIN) {
        return "binary";
    }
    if (radix == RADIX_OCT) {
        return "octal";
    }
    if (radix == RADIX_HEX) {
        return "hex";
    }
    return "decimal";
}

// Whether the next bytes are an exponent: `e` or `E`, an optional sign and a
// digit (D2.6). A bare `e` is no exponent and is left for the suffix check.
static bool exponent_follows(const lexer_t* lx) {
    const int c = peek(lx);
    if (c != 'e' && c != 'E') {
        return false;
    }
    const int sign = peek_at(lx, 1);
    if (sign == '+' || sign == '-') {
        return is_dec_digit(peek_at(lx, 2));
    }
    return is_dec_digit(sign);
}

// The fraction and exponent of a float literal after its integer part
// (D2.6): a `.` followed by a digit, an exponent, or both. The value is not
// computed: the bootstrap rejects float literals in the parser.
static bool lex_float_rest(lexer_t* lx, uint64_t start, loc_t at) {
    uint64_t count = 0;
    if (peek(lx) == '.') {
        advance(lx);
        if (!lex_digits(lx, RADIX_DEC, NULL, &count, NULL)) {
            return false;
        }
    }
    if (exponent_follows(lx)) {
        advance(lx);
        if (peek(lx) == '+' || peek(lx) == '-') {
            advance(lx);
        }
        count = 0;
        if (!lex_digits(lx, RADIX_DEC, NULL, &count, NULL)) {
            return false;
        }
    }
    if (is_ident_byte(peek(lx))) {
        return fail_text(lx, here(lx), "float literals have no suffix");
    }
    tokvec_push(lx->out, make_token(lx, TOK_FLOAT, start, at));
    return true;
}

static bool lex_number(lexer_t* lx) {
    const loc_t at = here(lx);
    const uint64_t start = lx->pos;
    int radix = RADIX_DEC;
    if (peek(lx) == '0') {
        const int p = peek_at(lx, 1);
        if (p == 'x') {
            radix = RADIX_HEX;
        } else if (p == 'o') {
            radix = RADIX_OCT;
        } else if (p == 'b') {
            radix = RADIX_BIN;
        } else if (is_dec_digit(p) || p == '_') {
            // A decimal literal other than `0` may not start with `0`, and a
            // float literal may not either (D2.5, D2.6): `09.5` is this error.
            return fail_text(lx, at, "decimal literal may not start with '0'");
        }
        if (radix != RADIX_DEC) {
            advance(lx);
            advance(lx);
        }
    }
    uint64_t value = 0;
    uint64_t count = 0;
    bool overflow = false;
    if (!lex_digits(lx, radix, &value, &count, &overflow)) {
        return false;
    }
    const int c = peek(lx);
    if (radix == RADIX_DEC) {
        // The integer part of a float literal has no magnitude limit (D2.6),
        // so the float lookahead comes before the overflow is reported.
        if ((c == '.' && is_dec_digit(peek_at(lx, 1))) || exponent_follows(lx)) {
            return lex_float_rest(lx, start, at);
        }
        if (c == '.' && peek_at(lx, 1) != '.') {
            advance(lx);
            msg_begin(&lx->msg);
            msg_quote(&lx->msg, str_from_span(lx->src.ptr + start, lx->pos - start));
            msg_str(&lx->msg, " is not a float literal");
            return fail(lx, at);
        }
    }
    if (overflow) {
        return fail_text(lx, at, "integer literal too large");
    }
    if (is_dec_digit(c) || (radix < RADIX_DEC && is_hex_letter(c))) {
        msg_begin(&lx->msg);
        msg_str(&lx->msg, "invalid digit ");
        msg_quote(&lx->msg, str_from_span(lx->src.ptr + lx->pos, 1));
        msg_str(&lx->msg, " in ");
        msg_str(&lx->msg, radix_name(radix));
        msg_str(&lx->msg, " literal");
        return fail(lx, here(lx));
    }
    if (count == 0) {
        msg_begin(&lx->msg);
        msg_str(&lx->msg, radix_name(radix));
        msg_str(&lx->msg, " literal needs at least one digit");
        return fail(lx, at);
    }
    if (is_ident_byte(c)) {
        return fail_text(lx, here(lx), "integer literals have no suffix");
    }
    token_t t = make_token(lx, TOK_INT, start, at);
    t.ival = value;
    tokvec_push(lx->out, t);
    return true;
}

// ---- char and string literals (D2.7, D2.8, D2.9) ----------------------------

// Whether the byte after the next one, a backslash, ends the line or the
// file: the literal is then unterminated, whatever the escape.
static bool escape_is_cut(const lexer_t* lx) {
    const int c = peek_at(lx, 1);
    return c < 0 || c == BYTE_LF;
}

// Decodes the escape at the next byte, a backslash followed by a byte that
// is neither a newline nor the end, into `*out` (D2.8).
static bool lex_escape(lexer_t* lx, int* out) {
    const loc_t at = here(lx);
    advance(lx);
    const int c = peek(lx);
    switch (c) {
    case 'n':
        *out = BYTE_LF;
        break;
    case 't':
        *out = BYTE_TAB;
        break;
    case 'r':
        *out = BYTE_CR;
        break;
    case '0':
        *out = BYTE_NUL;
        break;
    case '\\':
    case '\'':
    case '"':
        *out = c;
        break;
    case 'x': {
        advance(lx);
        int v = 0;
        for (int i = 0; i < ESCAPE_HEX_DIGITS; i++) {
            const int d = digit_value(peek(lx), RADIX_HEX);
            if (d < 0) {
                return fail_text(lx, at, "'\\x' needs exactly two hex digits");
            }
            v = v * RADIX_HEX + d;
            advance(lx);
        }
        *out = v;
        return true;
    }
    default:
        msg_begin(&lx->msg);
        msg_str(&lx->msg, "unknown escape ");
        if (is_printable(c)) {
            msg_str(&lx->msg, "'\\");
            sb_push(&lx->msg, (char)c);
            msg_str(&lx->msg, "'");
        } else {
            msg_str(&lx->msg, "'\\' followed by byte ");
            msg_hex_byte(&lx->msg, c);
        }
        return fail(lx, at);
    }
    advance(lx);
    return true;
}

static bool lex_char(lexer_t* lx) {
    const loc_t at = here(lx);
    const uint64_t start = lx->pos;
    advance(lx);
    int value = 0;
    const int c = peek(lx);
    if (c == '\\') {
        if (escape_is_cut(lx)) {
            return fail_text(lx, at, "unterminated char literal");
        }
        if (!lex_escape(lx, &value)) {
            return false;
        }
    } else if (c == '\'') {
        return fail_text(lx, at, "char literal holds exactly one character");
    } else if (c < 0 || c == BYTE_LF) {
        return fail_text(lx, at, "unterminated char literal");
    } else if (is_non_ascii(c)) {
        return fail_text(lx, here(lx), "non-ASCII byte in char literal; use a string");
    } else if (!is_printable(c)) {
        return fail_text(lx, here(lx), "control character in char literal; use an escape");
    } else {
        value = c;
        advance(lx);
    }
    const int close = peek(lx);
    if (close != '\'') {
        if (close < 0 || close == BYTE_LF) {
            return fail_text(lx, at, "unterminated char literal");
        }
        return fail_text(lx, at, "char literal holds exactly one character");
    }
    advance(lx);
    token_t t = make_token(lx, TOK_CHAR, start, at);
    t.ival = (uint64_t)value;
    tokvec_push(lx->out, t);
    return true;
}

static bool lex_string(lexer_t* lx) {
    const loc_t at = here(lx);
    const uint64_t start = lx->pos;
    advance(lx);
    sb_clear(&lx->buf);
    for (;;) {
        const int c = peek(lx);
        if (c == '"') {
            advance(lx);
            break;
        }
        if (c < 0 || c == BYTE_LF) {
            return fail_text(lx, at, "unterminated string literal");
        }
        int byte = c;
        if (c == '\\') {
            if (escape_is_cut(lx)) {
                return fail_text(lx, at, "unterminated string literal");
            }
            if (!lex_escape(lx, &byte)) {
                return false;
            }
        } else {
            advance(lx);
        }
        sb_push(&lx->buf, (char)byte);
    }
    token_t t = make_token(lx, TOK_STRING, start, at);
    t.text = str_pool_intern(lx->pool, sb_view(&lx->buf));
    tokvec_push(lx->out, t);
    return true;
}

// ---- operators and punctuation (D2.10) ---------------------------------------

// The operator kind of the next `*len` bytes, longest match first; TOK_EOF
// when the next byte starts no operator.
static tok_kind_t operator_kind(const lexer_t* lx, uint64_t* len) {
    const int c0 = peek(lx);
    const int c1 = peek_at(lx, 1);
    const int c2 = peek_at(lx, 2);
    tok_kind_t one = TOK_EOF;
    tok_kind_t two = TOK_EOF;
    tok_kind_t three = TOK_EOF;
    switch (c0) {
    case '+':
        one = TOK_PLUS;
        if (c1 == '%') {
            two = TOK_PLUS_WRAP;
            three = c2 == '=' ? TOK_PLUS_WRAP_ASSIGN : TOK_EOF;
        } else if (c1 == '=') {
            two = TOK_PLUS_ASSIGN;
        } else if (c1 == '+') {
            two = TOK_PLUS_PLUS;
        }
        break;
    case '-':
        one = TOK_MINUS;
        if (c1 == '%') {
            two = TOK_MINUS_WRAP;
            three = c2 == '=' ? TOK_MINUS_WRAP_ASSIGN : TOK_EOF;
        } else if (c1 == '=') {
            two = TOK_MINUS_ASSIGN;
        } else if (c1 == '-') {
            two = TOK_MINUS_MINUS;
        } else if (c1 == '>') {
            two = TOK_ARROW;
        }
        break;
    case '*':
        one = TOK_STAR;
        if (c1 == '%') {
            two = TOK_STAR_WRAP;
            three = c2 == '=' ? TOK_STAR_WRAP_ASSIGN : TOK_EOF;
        } else if (c1 == '=') {
            two = TOK_STAR_ASSIGN;
        }
        break;
    case '/':
        one = TOK_SLASH;
        two = c1 == '=' ? TOK_SLASH_ASSIGN : TOK_EOF;
        break;
    case '%':
        one = TOK_PERCENT;
        two = c1 == '=' ? TOK_PERCENT_ASSIGN : TOK_EOF;
        break;
    case '=':
        one = TOK_ASSIGN;
        two = c1 == '=' ? TOK_EQ : TOK_EOF;
        break;
    case '!':
        one = TOK_BANG;
        two = c1 == '=' ? TOK_NE : TOK_EOF;
        break;
    case '<':
        one = TOK_LT;
        if (c1 == '<') {
            two = TOK_SHL;
            three = c2 == '=' ? TOK_SHL_ASSIGN : TOK_EOF;
        } else if (c1 == '=') {
            two = TOK_LE;
        }
        break;
    case '>':
        one = TOK_GT;
        if (c1 == '>') {
            two = TOK_SHR;
            three = c2 == '=' ? TOK_SHR_ASSIGN : TOK_EOF;
        } else if (c1 == '=') {
            two = TOK_GE;
        }
        break;
    case '&':
        one = TOK_AMP;
        if (c1 == '&') {
            two = TOK_AND_AND;
        } else if (c1 == '=') {
            two = TOK_AMP_ASSIGN;
        }
        break;
    case '|':
        one = TOK_PIPE;
        if (c1 == '|') {
            two = TOK_PIPE_PIPE;
        } else if (c1 == '=') {
            two = TOK_PIPE_ASSIGN;
        }
        break;
    case '^':
        one = TOK_CARET;
        two = c1 == '=' ? TOK_CARET_ASSIGN : TOK_EOF;
        break;
    case '~':
        one = TOK_TILDE;
        break;
    case '?':
        one = TOK_QUESTION;
        break;
    case ':':
        one = TOK_COLON;
        two = c1 == ':' ? TOK_COLON_COLON : TOK_EOF;
        break;
    case '.':
        one = TOK_DOT;
        two = c1 == '.' ? TOK_DOT_DOT : TOK_EOF;
        break;
    case '(':
        one = TOK_LPAREN;
        break;
    case ')':
        one = TOK_RPAREN;
        break;
    case '[':
        one = TOK_LBRACKET;
        break;
    case ']':
        one = TOK_RBRACKET;
        break;
    case '{':
        one = TOK_LBRACE;
        break;
    case '}':
        one = TOK_RBRACE;
        break;
    case ',':
        one = TOK_COMMA;
        break;
    case ';':
        one = TOK_SEMI;
        break;
    // `@` is the slice suffix, one byte, no longer match (D2.10, D3.5).
    case '@':
        one = TOK_AT;
        break;
    default:
        break;
    }
    if (three != TOK_EOF) {
        *len = 3;
        return three;
    }
    if (two != TOK_EOF) {
        *len = 2;
        return two;
    }
    *len = 1;
    return one;
}

static bool lex_operator(lexer_t* lx) {
    const loc_t at = here(lx);
    const uint64_t start = lx->pos;
    uint64_t len = 0;
    const tok_kind_t kind = operator_kind(lx, &len);
    if (kind == TOK_EOF) {
        const int c = peek(lx);
        msg_begin(&lx->msg);
        if (is_non_ascii(c)) {
            msg_str(&lx->msg, "non-ASCII byte outside a string literal or comment");
        } else if (is_printable(c)) {
            msg_str(&lx->msg, "unexpected character ");
            msg_quote(&lx->msg, str_from_span(lx->src.ptr + start, 1));
        } else {
            msg_str(&lx->msg, "unexpected byte ");
            msg_hex_byte(&lx->msg, c);
        }
        return fail(lx, at);
    }
    for (uint64_t i = 0; i < len; i++) {
        advance(lx);
    }
    tokvec_push(lx->out, make_token(lx, kind, start, at));
    return true;
}

// ---- the file ------------------------------------------------------------------

// Lexes one token at the next byte, which is not blank.
static bool lex_token(lexer_t* lx) {
    const int c = peek(lx);
    if (is_letter(c)) {
        return lex_ident(lx);
    }
    if (is_dec_digit(c)) {
        return lex_number(lx);
    }
    if (c == '\'') {
        return lex_char(lx);
    }
    if (c == '"') {
        return lex_string(lx);
    }
    return lex_operator(lx);
}

static bool lex_all(lexer_t* lx) {
    if (peek_at(lx, 0) == BOM_0 && peek_at(lx, 1) == BOM_1 && peek_at(lx, 2) == BOM_2) {
        lx->pos = BOM_LEN;
    }
    for (;;) {
        if (!skip_blanks(lx)) {
            return false;
        }
        if (at_end(lx)) {
            token_t eof = make_token(lx, TOK_EOF, lx->pos, here(lx));
            eof.text = str_from_span(NULL, 0);
            tokvec_push(lx->out, eof);
            return true;
        }
        if (!lex_token(lx)) {
            return false;
        }
    }
}

bool lex_file(const char* file, str_t source, str_pool_t* pool, tokvec_t* out) {
    lexer_t lx;
    lx.file = file;
    lx.src = source;
    lx.pos = 0;
    lx.line = 1;
    lx.col = 1;
    lx.pool = pool;
    lx.out = out;
    sb_init(&lx.buf);
    sb_init(&lx.msg);
    const bool ok = lex_all(&lx);
    sb_free(&lx.buf);
    sb_free(&lx.msg);
    return ok;
}
