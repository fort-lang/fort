// The parser; see parser.h. The grammar is grammar.md and the three
// speculative points are grammar.md 7.
// D14.2: the diagnostics
#include "parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "diag.h"
#include "lexer.h"
#include "prim.h"
#include "str.h"
#include "types.h"

// Blocks, brace initializers, bracketed groups, unary operands, conditional
// branches and types nest at most this deep.
// D2.11
enum { PARSE_MAX_DEPTH = 256 };

// The `for` forms grammar.md 7.3 distinguishes after `for (`.
enum { FOR_PLAIN = 0, FOR_RANGE = 1, FOR_DECL = 2 };

// The binary precedence levels, loosest first; PREC_NONE is not a binary
// operator. `?:` sits below PREC_OR and is parsed on its own, since it is the one
// right-associative form.
// D6.1
enum {
    PREC_NONE = 0,
    PREC_OR = 1,       // ||
    PREC_AND = 2,      // &&
    PREC_BITOR = 3,    // |
    PREC_BITXOR = 4,   // ^
    PREC_BITAND = 5,   // &
    PREC_EQUALITY = 6, // == !=
    PREC_RELATION = 7, // < <= > >=
    PREC_SHIFT = 8,    // << >>
    PREC_ADD = 9,      // + - +% -%
    PREC_MUL = 10      // * / % *%
};

typedef struct {
    const char* file;
    const token_t* toks;
    uint64_t ntoks;
    uint64_t pos;
    ast_arena_t* arena;
    uint32_t depth; // D2.11: open nested constructs
    uint32_t spec;  // running speculative parses; no diagnostic while > 0
    bool failed;    // D14.2: unwinding the construct a syntax error hit
    loc_t last;     // where the last reported error started, for the dedupe
    sb_t msg;       // the message under construction
} parser_t;

// The state a speculative parse restores when it rewinds.
typedef struct {
    uint64_t pos;
    uint32_t depth;
    bool failed;
} spec_state_t;

// The `own` and `mut` written after one type element, with the position of the
// `mut` for the diagnostics that point at it.
// D5.3, D17.2
typedef struct {
    uint32_t flags;
    loc_t mut_loc;
} markers_t;

static ast_node_t* parse_expr(parser_t* p);
static ast_node_t* parse_unary(parser_t* p);
static ast_node_t* parse_type(parser_t* p, bool allow_noreturn);
static ast_node_t* parse_initializer(parser_t* p);
static ast_node_t* parse_statement(parser_t* p);
static ast_node_t* parse_block(parser_t* p);

// ---- the cursor -----------------------------------------------------------

static const token_t* cur(const parser_t* p) {
    return &p->toks[p->pos];
}

static tok_kind_t kind(const parser_t* p) {
    return p->toks[p->pos].kind;
}

// The kind `n` tokens ahead; the end of the file repeats, so a lookahead
// never leaves the array.
static tok_kind_t peek_kind(const parser_t* p, uint64_t n) {
    const uint64_t i = p->pos + n;
    if (i >= p->ntoks) {
        return TOK_EOF;
    }
    return p->toks[i].kind;
}

// The range one token covers: no token spans lines, so it ends on its own
// line, one past its last byte.
// D2.9, D20.4
static loc_t tok_loc(const parser_t* p, const token_t* t) {
    return loc_range(p->file, t->line, t->col, t->line, (uint32_t)(t->col + t->len));
}

static loc_t here(const parser_t* p) {
    return tok_loc(p, cur(p));
}

// Consumes the current token; the end of the file is never consumed.
static void bump(parser_t* p) {
    if (kind(p) != TOK_EOF) {
        p->pos++;
    }
}

static bool at(const parser_t* p, tok_kind_t k) {
    return kind(p) == k;
}

// The empty range just after the last consumed token, where a node with no
// token of its own begins: the implicit block of a case body.
// D20.4
static loc_t here_implicit(const parser_t* p) {
    if (p->pos == 0) {
        return loc_make(p->file, cur(p)->line, cur(p)->col);
    }
    const token_t* t = &p->toks[p->pos - 1];
    return loc_make(p->file, t->line, (uint32_t)(t->col + t->len));
}

// Ends `n`'s range at the last consumed token (`toks[pos - 1]`; bump never
// consumes the end of the file), which is the construct's last token wherever a
// node is returned successfully. Every function ends the ranges of the nodes it
// creates, so one that returns a node a callee made leaves it alone; joining
// never shrinks, so a node that grows as its construct grows keeps the widest
// end. A NULL node is a failed parse and passes through.
// D20.4
static ast_node_t* finish(parser_t* p, ast_node_t* n) {
    if (n != NULL && p->pos > 0) {
        n->loc = loc_extend(n->loc, tok_loc(p, &p->toks[p->pos - 1]));
    }
    return n;
}

// ---- diagnostics ----------------------------------------------------------

// Whether two ranges begin at the same place; one parse reports one file, so
// the file of every range it builds is the same.
static bool same_start(loc_t a, loc_t b) {
    return a.line == b.line && a.col == b.col;
}

// Reports `text` at `loc` and begins unwinding the failed construct: every later
// report is silent until a recovery point clears `failed`, so one mistake costs
// one diagnostic. Two more rules keep a broken file readable: an error that
// starts where the one before it started is dropped, and nothing is reported
// after the twentieth diagnostic of the file, the lexer's included since the
// budget is one (DIAG_MAX_PER_FILE), though the parse goes on. A speculative
// parse reports nothing, so a rewind leaves the diagnostics untouched.
// D14.2
static void report(parser_t* p, loc_t loc, const char* text) {
    const uint64_t reported = diag_file_count();
    if (p->spec == 0 && !p->failed && reported < DIAG_MAX_PER_FILE &&
        !(reported > 0 && same_start(loc, p->last))) {
        diag_error(loc, text);
        p->last = loc;
    }
    p->failed = true;
}

static void error_at(parser_t* p, loc_t loc, const char* text) {
    report(p, loc, text);
}

static void error_here(parser_t* p, const char* text) {
    report(p, here(p), text);
}

// Appends the current token as a diagnostic names it: a literal or an
// identifier by class and by text, everything else by its spelling.
static void msg_found(parser_t* p) {
    const token_t* t = cur(p);
    msg_str(&p->msg, ", found ");
    switch (t->kind) {
    case TOK_IDENT:
        msg_str(&p->msg, "identifier ");
        msg_quote(&p->msg, t->text);
        break;
    case TOK_INT:
    case TOK_FLOAT:
    case TOK_CHAR:
    case TOK_STRING:
    case TOK_EOF:
        msg_str(&p->msg, tok_kind_name(t->kind));
        break;
    default:
        msg_quote(&p->msg, str_from_cstr(tok_kind_name(t->kind)));
        break;
    }
}

// `expected <what>, found <token>` at the current token.
static void error_expected(parser_t* p, const char* what) {
    msg_begin(&p->msg);
    msg_str(&p->msg, "expected ");
    msg_str(&p->msg, what);
    msg_found(p);
    report(p, here(p), msg_end(&p->msg));
}

// Consumes a token of kind `k`, or reports `expected <what>`.
static bool expect(parser_t* p, tok_kind_t k, const char* what) {
    if (!at(p, k)) {
        error_expected(p, what);
        return false;
    }
    bump(p);
    return true;
}

// Records the current identifier as `n`'s name, with the range an editor jumps
// to for it, and consumes it.
// D20.4
static void take_name(parser_t* p, ast_node_t* n) {
    n->name = cur(p)->text;
    n->name_loc = here(p);
    bump(p);
}

// Consumes the identifier that names `n`, or reports `expected an
// identifier`.
static bool expect_name(parser_t* p, ast_node_t* n) {
    if (!at(p, TOK_IDENT)) {
        error_expected(p, "an identifier");
        return false;
    }
    take_name(p, n);
    return true;
}

// A feature the C bootstrap deliberately lacks (toolchain.md 7.3). A
// speculative parse skips the check and returns false, so the construct still
// decides the shape and the committed parse reports it; true means the parse
// must stop.
static bool unsupported(parser_t* p, loc_t loc, const char* feature) {
    if (p->spec > 0) {
        return false;
    }
    msg_begin(&p->msg);
    msg_str(&p->msg, "not supported by the bootstrap compiler: ");
    msg_str(&p->msg, feature);
    report(p, loc, msg_end(&p->msg));
    return true;
}

// ---- nesting and speculation ----------------------------------------------

// Enters a nested construct, or reports the nesting limit. It does not count
// on failure, so a caller that returns at once leaves the depth balanced.
// D2.11
static bool enter(parser_t* p) {
    if (p->depth >= PARSE_MAX_DEPTH) {
        msg_begin(&p->msg);
        msg_str(&p->msg, "nesting deeper than ");
        msg_uint(&p->msg, PARSE_MAX_DEPTH);
        report(p, here(p), msg_end(&p->msg));
        return false;
    }
    p->depth++;
    return true;
}

static void leave(parser_t* p) {
    p->depth--;
}

// Begins a speculative parse (grammar.md 7): nothing is reported until it
// rewinds. A speculation asks what the tokens ahead are, which does not depend on
// whether a construct is unwinding, so it starts with `failed` clear and
// spec_rewind puts the caller's back; every speculation reads `failed` afterwards
// to tell a type that parsed from one that did not, and would read the unwind
// instead. A recovery point speculates while `failed` is set, and so does the
// first statement of a body whose `{` was missing.
// D14.2
static spec_state_t spec_begin(parser_t* p) {
    spec_state_t s;
    s.pos = p->pos;
    s.depth = p->depth;
    s.failed = p->failed;
    p->failed = false;
    p->spec++;
    return s;
}

// Whether a failed speculative type parse read or stopped at a `mut` or an `own`
// that stands outside every bracket. A marker follows the type element it
// qualifies and no expression begins with one, so a statement that begins with a
// type-shaped prefix carrying a marker at that level cannot be read as an
// expression statement, and parsing it as a declaration reports the marker rather
// than the statement's own complaint (grammar.md 7.1). It is a heuristic and not
// a proof: an expression does embed a type, in `cast`, `sizeof`, `new` and an
// array literal's length (grammar.md 6), so a marker inside a bracket may well
// belong to an expression and is not counted.
// D5.3, D17.2
static bool read_marker(const parser_t* p, uint64_t start) {
    uint32_t depth = 0;
    for (uint64_t i = start; i <= p->pos && i < p->ntoks; i++) {
        const tok_kind_t k = p->toks[i].kind;
        if (depth == 0 && (k == TOK_KW_MUT || k == TOK_KW_OWN)) {
            return true;
        }
        if (k == TOK_LPAREN || k == TOK_LBRACKET || k == TOK_LBRACE) {
            depth++;
        } else if ((k == TOK_RPAREN || k == TOK_RBRACKET || k == TOK_RBRACE) && depth > 0) {
            depth--;
        }
    }
    return false;
}

// Restores the cursor, the depth and the failure state the speculation
// started from. Every speculation of this parser rewinds and the chosen
// branch parses the same tokens again, reporting this time.
static void spec_rewind(parser_t* p, spec_state_t s) {
    p->pos = s.pos;
    p->depth = s.depth;
    p->failed = s.failed;
    p->spec--;
}

// ---- types (grammar.md 4) -------------------------------------------------

static ast_node_t* node_at(parser_t* p, ast_kind_t k, loc_t loc) {
    return ast_new(p->arena, k, loc);
}

// The primitive a keyword names; `void` is a type kind of its own.
// D3.1
static bool prim_of_token(tok_kind_t k, prim_kind_t* out) {
    switch (k) {
    case TOK_KW_I8:
        *out = PRIM_I8;
        return true;
    case TOK_KW_I16:
        *out = PRIM_I16;
        return true;
    case TOK_KW_I32:
        *out = PRIM_I32;
        return true;
    case TOK_KW_I64:
        *out = PRIM_I64;
        return true;
    case TOK_KW_U8:
        *out = PRIM_U8;
        return true;
    case TOK_KW_U16:
        *out = PRIM_U16;
        return true;
    case TOK_KW_U32:
        *out = PRIM_U32;
        return true;
    case TOK_KW_U64:
        *out = PRIM_U64;
        return true;
    case TOK_KW_F32:
        *out = PRIM_F32;
        return true;
    case TOK_KW_F64:
        *out = PRIM_F64;
        return true;
    case TOK_KW_BOOL:
        *out = PRIM_BOOL;
        return true;
    case TOK_KW_CHAR:
        *out = PRIM_CHAR;
        return true;
    default:
        return false;
    }
}

// Whether `k` can open a base_type: a primitive, `string`, `void`, `fn` or a
// qualified name (grammar.md 4).
static bool starts_base_type(tok_kind_t k) {
    prim_kind_t prim = PRIM_VOID;
    return prim_of_token(k, &prim) || k == TOK_KW_STRING || k == TOK_KW_VOID || k == TOK_KW_FN ||
           k == TOK_IDENT;
}

// Nothing precedes the base type: every `mut` and `own` follows the type
// element it qualifies, so no type, declaration or statement begins with one.
// D5.3, D17.2
static bool check_no_leading_marker(parser_t* p) {
    if (at(p, TOK_KW_MUT)) {
        error_here(p, "a mut never precedes the base type: write 'node mut* p' or 'node* mut p'");
        return false;
    }
    if (at(p, TOK_KW_OWN)) {
        error_here(p, "an own never precedes the base type: write 'node* own p' or 'string own s'");
        return false;
    }
    return true;
}

// Parses the `[own] [mut]` of one type position. Each storage level has exactly
// one position, so a doubled marker does not parse, and an `own` precedes the
// `mut` of its position. `own_error` is the message for a position that takes no
// `own`, or NULL where one is legal.
// D5.3, D17.2
static bool parse_markers(parser_t* p, markers_t* m, const char* own_error) {
    m->flags = 0;
    m->mut_loc = here(p);
    for (;;) {
        if (at(p, TOK_KW_OWN)) {
            const loc_t loc = here(p);
            if ((m->flags & AST_FLAG_OWN) != 0) {
                error_at(p, loc, "an own appears once in a type position");
                return false;
            }
            if ((m->flags & AST_FLAG_MUT) != 0) {
                error_at(
                    p, loc, "an own precedes the mut of its position: write 'node* own mut p'");
                return false;
            }
            if (own_error != NULL) {
                error_at(p, loc, own_error);
                return false;
            }
            m->flags |= AST_FLAG_OWN;
            bump(p);
            continue;
        }
        if (at(p, TOK_KW_MUT)) {
            const loc_t loc = here(p);
            if ((m->flags & AST_FLAG_MUT) != 0) {
                error_at(p, loc, "a mut appears once in a type position");
                return false;
            }
            m->flags |= AST_FLAG_MUT;
            m->mut_loc = loc;
            bump(p);
            continue;
        }
        return true;
    }
}

// A `mut` marks the storage of the element it follows, and the elements of a
// fixed array share the array's own storage, so the position a `[N]` follows
// never carries one (`i32 mut[4]` is an error).
// D5.3
static bool check_mut_before_array(parser_t* p, const markers_t* m) {
    if ((m->flags & AST_FLAG_MUT) != 0 && at(p, TOK_LBRACKET)) {
        error_at(p,
                 m->mut_loc,
                 "the elements share the array's storage: write the mut after the length, "
                 "as 'i32[4] mut'");
        return false;
    }
    return true;
}

static ast_node_t* parse_base_type(parser_t* p, bool allow_noreturn);
static ast_node_t* parse_return_type(parser_t* p);

// fn_type = "fn" "(" [ type { "," type } ] ")" return_type, with the `fn`
// already parsed: the result comes last, as it does in a declaration.
// D3.10, D8.1
static ast_node_t* parse_fn_type_params(parser_t* p, loc_t loc) {
    ast_node_t* n = node_at(p, AST_TYPE_FN, loc);
    if (!expect(p, TOK_LPAREN, "'('")) {
        return NULL;
    }
    if (!at(p, TOK_RPAREN)) {
        for (;;) {
            ast_node_t* param = parse_type(p, false);
            if (param == NULL) {
                return NULL;
            }
            ast_push(n, param);
            if (!at(p, TOK_COMMA)) {
                break;
            }
            bump(p);
        }
    }
    if (!expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    n->a = parse_return_type(p);
    if (n->a == NULL) {
        return NULL;
    }
    return finish(p, n);
}

// return_type = type | "void" | "noreturn"; `void` and `noreturn` are base
// types of their own.
// D8.5
static ast_node_t* parse_return_type(parser_t* p) {
    return parse_type(p, true);
}

static ast_node_t* parse_base_type(parser_t* p, bool allow_noreturn) {
    const loc_t loc = here(p);
    prim_kind_t prim = PRIM_VOID;
    if (prim_of_token(kind(p), &prim)) {
        ast_node_t* n = node_at(p, AST_TYPE_PRIM, loc);
        n->op = (int32_t)prim;
        bump(p);
        return finish(p, n);
    }
    switch (kind(p)) {
    case TOK_KW_STRING:
        bump(p);
        return finish(p, node_at(p, AST_TYPE_STRING, loc));
    case TOK_KW_VOID:
        bump(p);
        return finish(p, node_at(p, AST_TYPE_VOID, loc));
    case TOK_KW_NORETURN:
        if (!allow_noreturn) {
            error_expected(p, "a type");
            return NULL;
        }
        bump(p);
        return finish(p, node_at(p, AST_TYPE_NORETURN, loc));
    case TOK_KW_FN: {
        bump(p);
        return parse_fn_type_params(p, loc);
    }
    case TOK_IDENT: {
        // D9.4: resolution decides whether the first part is a module
        ast_node_t* n = node_at(p, AST_TYPE_NAME, loc);
        take_name(p, n);
        if (at(p, TOK_DOT) && peek_kind(p, 1) == TOK_IDENT) {
            const loc_t second = here(p);
            bump(p);
            ast_node_t* tail = node_at(p, AST_IDENT, second);
            take_name(p, tail);
            n->a = finish(p, tail);
        }
        return finish(p, n);
    }
    default:
        error_expected(p, "a type");
        return NULL;
    }
}

// An `own` marks a reference, so after a base type it is legal only on
// `string`, the reference with no suffix.
// D3.7, D17.2
static const char* base_own_error(const ast_node_t* base) {
    if (base->kind == AST_TYPE_STRING) {
        return NULL;
    }
    return "an own marks a reference: write it after a '*' or an '@', or on a string";
}

// Adds one suffix to `t` and counts it against the nesting limit, which covers
// type suffixes.
// D2.11
static bool push_suffix(parser_t* p, ast_node_t* t, ast_node_t* suffix) {
    if (ast_len(t) >= PARSE_MAX_DEPTH) {
        msg_begin(&p->msg);
        msg_str(&p->msg, "nesting deeper than ");
        msg_uint(&p->msg, PARSE_MAX_DEPTH);
        report(p, suffix->loc, msg_end(&p->msg));
        return false;
    }
    ast_push(t, suffix);
    return true;
}

// A group of reference suffixes, `("*" | "@") [own] [mut]` each: they apply to
// everything to their left, so a group reads inside-out. `array_may_follow` is
// true for the group before the fixed-array group, where a `mut` on the last
// position would mark the array's elements.
// D3.6
static bool parse_ref_suffixes(parser_t* p, ast_node_t* t, bool array_may_follow) {
    while (at(p, TOK_STAR) || at(p, TOK_AT)) {
        const loc_t loc = here(p);
        ast_node_t* s = node_at(p, AST_TYPE_SUFFIX, loc);
        s->op = at(p, TOK_STAR) ? (int32_t)SUFFIX_PTR : (int32_t)SUFFIX_SPAN;
        bump(p);
        markers_t m;
        if (!parse_markers(p, &m, NULL)) {
            return false;
        }
        s->flags = m.flags;
        (void)finish(p, s);
        if (array_may_follow && !check_mut_before_array(p, &m)) {
            return false;
        }
        if (!push_suffix(p, t, s)) {
            return false;
        }
    }
    return true;
}

// The fixed-array group, `"[" const_expr "]" [mut]` each: the group reads
// outside-in like C declarators. `marked` is false for the dimensions of an array
// literal's type and of an allocated type, which the grammar writes without any
// marker (grammar.md 6).
// D3.4, D3.6
static bool parse_array_suffixes(parser_t* p, ast_node_t* t, bool marked) {
    while (at(p, TOK_LBRACKET)) {
        const loc_t loc = here(p);
        bump(p);
        if (!enter(p)) {
            return false;
        }
        ast_node_t* len = parse_expr(p);
        leave(p);
        if (len == NULL || !expect(p, TOK_RBRACKET, "']'")) {
            return false;
        }
        ast_node_t* s = node_at(p, AST_TYPE_SUFFIX, loc);
        s->op = (int32_t)SUFFIX_ARRAY;
        s->a = len;
        if (marked) {
            markers_t m;
            if (!parse_markers(p,
                               &m,
                               "an own never follows a fixed-array suffix: write it after the "
                               "'*' or '@' it marks")) {
                return false;
            }
            s->flags = m.flags;
            if (!check_mut_before_array(p, &m)) {
                return false;
            }
        }
        (void)finish(p, s);
        if (!push_suffix(p, t, s)) {
            return false;
        }
    }
    return true;
}

// The C bootstrap has at most one array or span level in one written type
// (toolchain.md 7.3): a second one is a nested aggregate its layout and
// codegen do not do.
static bool check_one_aggregate_level(parser_t* p, const ast_node_t* t) {
    uint64_t arrays = 0;
    uint64_t spans = 0;
    uint64_t first_extra = 0;
    for (uint64_t i = 0; i < ast_len(t); i++) {
        const ast_node_t* s = ast_child(t, i);
        if ((suffix_kind_t)s->op == SUFFIX_ARRAY) {
            arrays++;
        } else if ((suffix_kind_t)s->op == SUFFIX_SPAN) {
            spans++;
        } else {
            continue;
        }
        if (arrays + spans == 2) {
            first_extra = i;
        }
    }
    if (arrays + spans < 2) {
        return true;
    }
    const char* feature = "spans of spans";
    if (arrays >= 2) {
        feature = "multi-dimensional arrays";
    } else if (spans == 1 && (suffix_kind_t)ast_child(t, first_extra)->op == SUFFIX_SPAN) {
        feature = "spans of arrays";
    } else if (spans == 1) {
        feature = "arrays of spans";
    }
    return !unsupported(p, ast_child(t, first_extra)->loc, feature);
}

// type = base_type [own] [mut] { ref_suffix } { array_suffix } { ref_suffix }
// (grammar.md 4), with the base type already parsed.
static ast_node_t* parse_type_after_base(parser_t* p, loc_t loc, ast_node_t* base) {
    ast_node_t* t = node_at(p, AST_TYPE, loc);
    t->a = base;
    markers_t m;
    if (!parse_markers(p, &m, base_own_error(base))) {
        return NULL;
    }
    t->flags = m.flags;
    if (!check_mut_before_array(p, &m)) {
        return NULL;
    }
    if (!parse_ref_suffixes(p, t, true)) {
        return NULL;
    }
    if (!parse_array_suffixes(p, t, true)) {
        return NULL;
    }
    if (!parse_ref_suffixes(p, t, false)) {
        return NULL;
    }
    // D3.6
    if (at(p, TOK_LBRACKET)) {
        error_here(p, "no array suffix follows a reference suffix: wrap the array in a struct");
        return NULL;
    }
    if (!check_one_aggregate_level(p, t)) {
        return NULL;
    }
    return finish(p, t);
}

static ast_node_t* parse_type_inner(parser_t* p, bool allow_noreturn) {
    if (!check_no_leading_marker(p)) {
        return NULL;
    }
    const loc_t loc = here(p);
    ast_node_t* base = parse_base_type(p, allow_noreturn);
    if (base == NULL) {
        return NULL;
    }
    return parse_type_after_base(p, loc, base);
}

static ast_node_t* parse_type(parser_t* p, bool allow_noreturn) {
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* t = parse_type_inner(p, allow_noreturn);
    leave(p);
    return t;
}

// array_type = base_type { ref_suffix } "[" const_expr "]" { "[" const_expr "]"
// } (grammar.md 6): the type of an array literal, which has no base marker, no
// marker on a dimension and no trailing reference suffix.
// D6.5
static ast_node_t* parse_array_type(parser_t* p) {
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* t = NULL;
    const loc_t loc = here(p);
    ast_node_t* base = parse_base_type(p, false);
    if (base != NULL) {
        t = node_at(p, AST_TYPE, loc);
        t->a = base;
        if (!parse_ref_suffixes(p, t, true) || !at(p, TOK_LBRACKET)) {
            if (!p->failed) {
                error_expected(p, "'[' of an array type");
            }
            t = NULL;
        } else if (!parse_array_suffixes(p, t, false) || !check_one_aggregate_level(p, t)) {
            t = NULL;
        }
    }
    leave(p);
    return finish(p, t);
}

// The two diagnostics of an allocated type, which two sites each report.
static const char ALLOC_MUT_ERROR[] = "new allocates writable storage: remove the outermost 'mut'";
static const char ALLOC_OWN_ERROR[] = "inside new an own follows a '*' of the element type";

// The outermost position of an allocated type is the one `new` fills, because
// it is the storage `new` allocates: `new` marks it writable and owns it, so
// neither marker of the program's parses there. An `own` marks a reference, so
// inside `new(...)` it follows a `*` alone, and a span is asked for with a
// count and not with an `@`.
// D5.8, D10.2, D17.3
static bool check_alloc_end(parser_t* p) {
    if (at(p, TOK_KW_MUT)) {
        error_here(p, ALLOC_MUT_ERROR);
        return false;
    }
    if (at(p, TOK_KW_OWN)) {
        error_here(p, ALLOC_OWN_ERROR);
        return false;
    }
    if (at(p, TOK_AT)) {
        error_here(p, "a span suffix does not parse inside new: write new(T, n)");
        return false;
    }
    return true;
}

// A `mut` inside `new(...)` marks storage `new` does not allocate, which is
// every position of the element type but the outermost one, so a `*` must
// stand outside the `mut` that was just read.
// D5.8, D10.2
static bool check_alloc_mut(parser_t* p, const markers_t* m) {
    if ((m->flags & AST_FLAG_MUT) != 0 && !at(p, TOK_STAR)) {
        error_at(p, m->mut_loc, ALLOC_MUT_ERROR);
        return false;
    }
    return true;
}

// alloc_type = base_type [ "mut" ] { "*" [ "own" ] [ "mut" ] }
// { "[" const_expr "]" } (grammar.md 6), whose last `mut` position is empty.
// D5.8, D10.2
static ast_node_t* parse_alloc_type(parser_t* p) {
    if (!check_no_leading_marker(p)) {
        return NULL;
    }
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* t = NULL;
    const loc_t loc = here(p);
    ast_node_t* base = parse_base_type(p, false);
    if (base != NULL) {
        t = node_at(p, AST_TYPE, loc);
        t->a = base;
        markers_t m;
        if (!parse_markers(p, &m, ALLOC_OWN_ERROR) || !check_mut_before_array(p, &m) ||
            !check_alloc_mut(p, &m)) {
            t = NULL;
        } else {
            t->flags = m.flags;
        }
        while (t != NULL && at(p, TOK_STAR)) {
            ast_node_t* s = node_at(p, AST_TYPE_SUFFIX, here(p));
            s->op = (int32_t)SUFFIX_PTR;
            bump(p);
            if (!parse_markers(p, &m, NULL)) {
                t = NULL;
                break;
            }
            s->flags = m.flags;
            (void)finish(p, s);
            if (!check_mut_before_array(p, &m) || !check_alloc_mut(p, &m) ||
                !push_suffix(p, t, s)) {
                t = NULL;
            }
        }
        if (t != NULL && (!parse_array_suffixes(p, t, false) || !check_alloc_end(p) ||
                          !check_one_aggregate_level(p, t))) {
            t = NULL;
        }
    }
    leave(p);
    return finish(p, t);
}

// ---- expressions (grammar.md 6) -------------------------------------------

// The precedence of a binary operator, PREC_NONE when `k` is not one; higher
// binds tighter. All of them are left-associative.
// D6.1
static int binary_prec(tok_kind_t k) {
    switch (k) {
    case TOK_PIPE_PIPE:
        return PREC_OR;
    case TOK_AND_AND:
        return PREC_AND;
    case TOK_PIPE:
        return PREC_BITOR;
    case TOK_CARET:
        return PREC_BITXOR;
    case TOK_AMP:
        return PREC_BITAND;
    case TOK_EQ:
    case TOK_NE:
        return PREC_EQUALITY;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
        return PREC_RELATION;
    case TOK_SHL:
    case TOK_SHR:
        return PREC_SHIFT;
    case TOK_PLUS:
    case TOK_MINUS:
    case TOK_PLUS_WRAP:
    case TOK_MINUS_WRAP:
        return PREC_ADD;
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT:
    case TOK_STAR_WRAP:
        return PREC_MUL;
    default:
        return PREC_NONE;
    }
}

static bool is_assign_op(tok_kind_t k) {
    switch (k) {
    case TOK_ASSIGN:
    case TOK_PLUS_ASSIGN:
    case TOK_MINUS_ASSIGN:
    case TOK_STAR_ASSIGN:
    case TOK_SLASH_ASSIGN:
    case TOK_PERCENT_ASSIGN:
    case TOK_PLUS_WRAP_ASSIGN:
    case TOK_MINUS_WRAP_ASSIGN:
    case TOK_STAR_WRAP_ASSIGN:
    case TOK_AMP_ASSIGN:
    case TOK_PIPE_ASSIGN:
    case TOK_CARET_ASSIGN:
    case TOK_SHL_ASSIGN:
    case TOK_SHR_ASSIGN:
        return true;
    default:
        return false;
    }
}

static ast_node_t* parse_brace_init(parser_t* p);

// A parenthesized expression: `(` expr `)`.
static ast_node_t* parse_paren_expr(parser_t* p) {
    if (!expect(p, TOK_LPAREN, "'('")) {
        return NULL;
    }
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* e = parse_expr(p);
    leave(p);
    if (e == NULL || !expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    return e;
}

// cast_expr = "cast" "(" expr "," type ")".
// D6.4
static ast_node_t* parse_cast(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    if (!expect(p, TOK_LPAREN, "'('") || !enter(p)) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_CAST, loc);
    n->a = parse_expr(p);
    if (n->a != NULL && expect(p, TOK_COMMA, "','")) {
        n->b = parse_type(p, false);
    }
    leave(p);
    if (n->a == NULL || n->b == NULL || !expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    return finish(p, n);
}

// sizeof_expr = "sizeof" "(" type ")".
// D3.15
static ast_node_t* parse_sizeof(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    if (!expect(p, TOK_LPAREN, "'('") || !enter(p)) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_SIZEOF, loc);
    n->a = parse_type(p, false);
    leave(p);
    if (n->a == NULL || !expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    return finish(p, n);
}

// new_expr = "new" "(" alloc_type [ "," expr ] ")": one `T` without a count, a
// span of `n` elements with one.
// D10.2
static ast_node_t* parse_new(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    if (!expect(p, TOK_LPAREN, "'('") || !enter(p)) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_NEW, loc);
    n->a = parse_alloc_type(p);
    bool ok = n->a != NULL;
    if (ok && at(p, TOK_COMMA)) {
        bump(p);
        n->b = parse_expr(p);
        ok = n->b != NULL;
    }
    leave(p);
    if (!ok || !expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    return finish(p, n);
}

// struct_literal = qualified_name brace_init; grammar.md 7.2: an identifier,
// optionally `. identifier`, directly followed by `{`.
// D6.5
static bool starts_struct_literal(const parser_t* p) {
    if (peek_kind(p, 1) == TOK_LBRACE) {
        return true;
    }
    return peek_kind(p, 1) == TOK_DOT && peek_kind(p, 2) == TOK_IDENT &&
           peek_kind(p, 3) == TOK_LBRACE;
}

static ast_node_t* parse_struct_literal(parser_t* p) {
    const loc_t loc = here(p);
    ast_node_t* name = parse_base_type(p, false);
    if (name == NULL) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_STRUCT_LIT, loc);
    n->a = name;
    n->b = parse_brace_init(p);
    if (n->b == NULL) {
        return NULL;
    }
    return finish(p, n);
}

// grammar.md 7.2: a successful speculative parse of an `array_type` directly
// followed by `{` is an array literal. The speculation only decides; the
// literal is parsed again, loudly, after the rewind.
static bool speculate_array_literal(parser_t* p) {
    spec_state_t s = spec_begin(p);
    ast_node_t* t = parse_array_type(p);
    const bool literal = t != NULL && !p->failed && at(p, TOK_LBRACE);
    spec_rewind(p, s);
    return literal;
}

static ast_node_t* parse_array_literal(parser_t* p) {
    const loc_t loc = here(p);
    ast_node_t* n = node_at(p, AST_ARRAY_LIT, loc);
    n->a = parse_array_type(p);
    if (n->a == NULL) {
        return NULL;
    }
    n->b = parse_brace_init(p);
    if (n->b == NULL) {
        return NULL;
    }
    return finish(p, n);
}

static ast_node_t* parse_primary(parser_t* p) {
    const loc_t loc = here(p);
    ast_node_t* n = NULL;
    switch (kind(p)) {
    case TOK_INT:
        n = node_at(p, AST_INT, loc);
        n->ival = cur(p)->ival;
        bump(p);
        return finish(p, n);
    case TOK_FLOAT:
        // D2.6
        if (unsupported(p, loc, "float literals")) {
            return NULL;
        }
        n = node_at(p, AST_FLOAT, loc);
        n->name = cur(p)->text;
        bump(p);
        return finish(p, n);
    case TOK_CHAR:
        n = node_at(p, AST_CHAR, loc);
        n->ival = cur(p)->ival;
        bump(p);
        return finish(p, n);
    case TOK_STRING:
        n = node_at(p, AST_STRING, loc);
        n->name = cur(p)->text;
        bump(p);
        return finish(p, n);
    case TOK_KW_TRUE:
    case TOK_KW_FALSE:
        n = node_at(p, AST_BOOL, loc);
        n->ival = at(p, TOK_KW_TRUE) ? 1 : 0;
        bump(p);
        return finish(p, n);
    case TOK_KW_NULL:
        bump(p);
        return finish(p, node_at(p, AST_NULL, loc));
    case TOK_LPAREN:
        return parse_paren_expr(p);
    case TOK_KW_CAST:
        return parse_cast(p);
    case TOK_KW_SIZEOF:
        return parse_sizeof(p);
    case TOK_KW_NEW:
        return parse_new(p);
    case TOK_IDENT:
        if (starts_struct_literal(p)) {
            return parse_struct_literal(p);
        }
        if (speculate_array_literal(p)) {
            return parse_array_literal(p);
        }
        n = node_at(p, AST_IDENT, loc);
        take_name(p, n);
        return finish(p, n);
    default:
        break;
    }
    if (starts_base_type(kind(p)) && speculate_array_literal(p)) {
        return parse_array_literal(p);
    }
    error_expected(p, "an expression");
    return NULL;
}

// postfix = call | index | span | "." identifier | "->" identifier.
static ast_node_t* parse_call(parser_t* p, ast_node_t* callee) {
    ast_node_t* n = node_at(p, AST_CALL, here(p));
    n->a = callee;
    bump(p);
    if (!enter(p)) {
        return NULL;
    }
    bool ok = true;
    if (!at(p, TOK_RPAREN)) {
        for (;;) {
            ast_node_t* arg = parse_expr(p);
            if (arg == NULL) {
                ok = false;
                break;
            }
            ast_push(n, arg);
            if (!at(p, TOK_COMMA)) {
                break;
            }
            bump(p);
        }
    }
    leave(p);
    if (!ok || !expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    return finish(p, n);
}

// index and the four span forms, told apart by the `..`.
// D6.8, D6.9
static ast_node_t* parse_index_or_span(parser_t* p, ast_node_t* operand) {
    const loc_t loc = here(p);
    bump(p);
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* n = NULL;
    ast_node_t* low = NULL;
    bool ok = true;
    if (!at(p, TOK_DOT_DOT)) {
        low = parse_expr(p);
        ok = low != NULL;
    }
    if (ok && at(p, TOK_DOT_DOT)) {
        bump(p);
        n = node_at(p, AST_SPAN, loc);
        n->a = operand;
        n->b = low;
        if (!at(p, TOK_RBRACKET)) {
            n->c = parse_expr(p);
            ok = n->c != NULL;
        }
    } else if (ok) {
        n = node_at(p, AST_INDEX, loc);
        n->a = operand;
        n->b = low;
    }
    leave(p);
    if (!ok || !expect(p, TOK_RBRACKET, "']'")) {
        return NULL;
    }
    return finish(p, n);
}

// `.f` and `->f`, the second required through a pointer.
// D6.10
static ast_node_t* parse_member(parser_t* p, ast_node_t* operand) {
    const loc_t loc = here(p);
    const bool arrow = at(p, TOK_ARROW);
    bump(p);
    ast_node_t* n = node_at(p, arrow ? AST_ARROW : AST_FIELD, loc);
    n->a = operand;
    if (!expect_name(p, n)) {
        return NULL;
    }
    return finish(p, n);
}

static ast_node_t* parse_postfix(parser_t* p) {
    ast_node_t* e = parse_primary(p);
    while (e != NULL) {
        switch (kind(p)) {
        case TOK_LPAREN:
            e = parse_call(p, e);
            break;
        case TOK_LBRACKET:
            e = parse_index_or_span(p, e);
            break;
        case TOK_DOT:
        case TOK_ARROW:
            e = parse_member(p, e);
            break;
        default:
            return e;
        }
    }
    return NULL;
}

// unary_expr = ( "!" | "~" | "-" | "*" | "&" ) unary_expr | postfix_expr.
static ast_node_t* parse_unary(parser_t* p) {
    const tok_kind_t k = kind(p);
    if (k != TOK_BANG && k != TOK_TILDE && k != TOK_MINUS && k != TOK_STAR && k != TOK_AMP) {
        return parse_postfix(p);
    }
    const loc_t loc = here(p);
    bump(p);
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* operand = parse_unary(p);
    leave(p);
    if (operand == NULL) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_UNARY, loc);
    n->op = (int32_t)k;
    n->a = operand;
    return finish(p, n);
}

// The binary levels, climbed by precedence; every level is left-associative,
// so the right operand starts one level up.
// D6.1
static ast_node_t* parse_binary(parser_t* p, int min_prec) {
    ast_node_t* left = parse_unary(p);
    while (left != NULL) {
        const tok_kind_t k = kind(p);
        const int prec = binary_prec(k);
        if (prec == PREC_NONE || prec < min_prec) {
            return left;
        }
        const loc_t loc = here(p);
        bump(p);
        ast_node_t* right = parse_binary(p, prec + 1);
        if (right == NULL) {
            return NULL;
        }
        ast_node_t* n = node_at(p, AST_BINARY, loc);
        n->op = (int32_t)k;
        n->a = left;
        n->b = right;
        left = finish(p, n);
    }
    return NULL;
}

// ternary_expr = or_expr [ "?" expr ":" ternary_expr ], right-associative. As
// for `do`-`while`, the whole form is parsed before `?:` is reported as outside
// the C bootstrap's subset.
// D6.1
static ast_node_t* parse_ternary(parser_t* p) {
    ast_node_t* cond = parse_binary(p, PREC_OR);
    if (cond == NULL || !at(p, TOK_QUESTION)) {
        return cond;
    }
    const loc_t loc = here(p);
    bump(p);
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_TERNARY, loc);
    n->a = cond;
    n->b = parse_expr(p);
    if (n->b != NULL && expect(p, TOK_COLON, "':'")) {
        n->c = parse_ternary(p);
    }
    leave(p);
    if (n->b == NULL || n->c == NULL) {
        return NULL;
    }
    if (unsupported(p, loc, "?:")) {
        return NULL;
    }
    return finish(p, n);
}

static ast_node_t* parse_expr(parser_t* p) {
    return parse_ternary(p);
}

// brace_init = "{" [ init_list ] "}", positional or designated and never both,
// with a trailing comma allowed.
// D6.5
static ast_node_t* parse_brace_init(parser_t* p) {
    const loc_t loc = here(p);
    if (!expect(p, TOK_LBRACE, "'{'") || !enter(p)) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_BRACE_INIT, loc);
    bool ok = true;
    if (!at(p, TOK_RBRACE)) {
        const bool designated = at(p, TOK_DOT);
        if (designated) {
            n->flags = AST_FLAG_DESIGNATED;
        }
        while (ok) {
            if (at(p, TOK_DOT) != designated) {
                error_here(p, "positional and designated initializers do not mix");
                ok = false;
                break;
            }
            ast_node_t* member = NULL;
            if (designated) {
                member = node_at(p, AST_DESIGNATOR, here(p));
                bump(p);
                ok = expect_name(p, member) && expect(p, TOK_ASSIGN, "'='");
                if (ok) {
                    member->a = parse_initializer(p);
                    ok = member->a != NULL;
                }
                if (ok) {
                    (void)finish(p, member);
                }
            } else {
                member = parse_initializer(p);
                ok = member != NULL;
            }
            if (!ok) {
                break;
            }
            ast_push(n, member);
            if (!at(p, TOK_COMMA)) {
                break;
            }
            bump(p);
            if (at(p, TOK_RBRACE)) {
                break;
            }
        }
    }
    leave(p);
    if (!ok || !expect(p, TOK_RBRACE, "'}'")) {
        return NULL;
    }
    return finish(p, n);
}

// initializer = expr | brace_init (grammar.md 3).
static ast_node_t* parse_initializer(parser_t* p) {
    if (at(p, TOK_LBRACE)) {
        return parse_brace_init(p);
    }
    return parse_expr(p);
}

// ---- recovery (toolchain.md 4) --------------------------------------------
// D14.2

// The recovery points, which differ in what ends a skip: the statements of a
// block or of a case clause, the clauses of a switch, the fields of a struct
// body, the declarations of the module (toolchain.md 4).
enum { RECOVER_STMT = 0, RECOVER_CASE = 1, RECOVER_FIELD = 2, RECOVER_DECL = 3 };

// The keywords that begin a statement (grammar.md 5). A skip stops before one
// rather than swallowing it, since it is where the next statement starts
// (toolchain.md 4).
static bool starts_statement(tok_kind_t k) {
    return k == TOK_KW_IF || k == TOK_KW_WHILE || k == TOK_KW_FOR || k == TOK_KW_SWITCH ||
           k == TOK_KW_DEFER || k == TOK_KW_RETURN || k == TOK_KW_BREAK || k == TOK_KW_CONTINUE ||
           k == TOK_KW_DO;
}

// The keywords that begin a case clause. Neither appears in a statement or an
// expression, so a skip inside a switch stops before one rather than
// swallowing the clause that follows the broken one (toolchain.md 4).
// D7.6
static bool starts_case(tok_kind_t k) {
    return k == TOK_KW_CASE || k == TOK_KW_DEFAULT;
}

// Whether the current token starts a top-level declaration (grammar.md 7).
// `struct`, `enum`, `extern` and `import` always do; a `fn` does when a name and
// a `(` follow it, since the `fn` of a statement or of a field is the base of a
// function type, which is followed by `(` at once. A block,
// a case clause, a struct body and an enum body end here as well as at their `}`,
// so a file with a missing `}` costs one diagnostic rather than one per following
// declaration.
// D3.10, D14.2
static bool at_decl_start(parser_t* p) {
    switch (kind(p)) {
    case TOK_KW_STRUCT:
    case TOK_KW_ENUM:
    case TOK_KW_EXTERN:
    case TOK_KW_IMPORT:
        return true;
    case TOK_KW_FN:
        break;
    default:
        return false;
    }
    return peek_kind(p, 1) == TOK_IDENT && peek_kind(p, 2) == TOK_LPAREN;
}

// The `(` and `[` the failed construct left open, counted over the tokens it
// consumed: a skip that begins inside them looks for the construct's end at
// the construct's own level, so the `;` of a `for` header and the `,` of an
// argument list never pass for a statement boundary (toolchain.md 4). Braces
// are not counted here: which `{` an unclosed one was meant to be is not
// decidable from the tokens, and assuming the construct owns the next `}` ends
// with the enclosing block's brace being eaten, so a skip counts only the
// braces it opens itself and leaves any other `}` to the body it belongs to.
static uint32_t count_open_groups(const parser_t* p, uint64_t start) {
    uint32_t groups = 0;
    for (uint64_t i = start; i < p->pos; i++) {
        const tok_kind_t k = p->toks[i].kind;
        if (k == TOK_LPAREN || k == TOK_LBRACKET) {
            groups++;
        } else if ((k == TOK_RPAREN || k == TOK_RBRACKET) && groups > 0) {
            groups--;
        }
    }
    return groups;
}

// Skips what is left of the construct that began at `start` and failed, to the
// boundary of toolchain.md 4. Outside the `(` and `[` the construct left open, a
// `;` and the `}` that closes a brace the skip saw opened are consumed, and a `}`
// it did not see opened, a case keyword, a statement keyword and a token that
// starts a top-level declaration are left where the next construct reads them;
// the end of the file ends every skip. `level` is the recovery point: a struct
// field and a declaration run over the statement keywords, which start no field
// and no declaration, a declaration also drops a `}`, which closes nothing at the
// top level, and a case clause runs over the statement keywords of the body it is
// skipping. The first token is consumed when the construct consumed none, so a
// recovery always makes progress and the parse terminates.
// D14.2
static void skip_to_boundary(parser_t* p, uint64_t start, int level) {
    if (p->pos == start) {
        bump(p);
    }
    // A construct that begins with `{` is a block statement, which consumed its
    // brace before it failed, so the first `}` ahead closes it; only the nesting
    // limit makes a block fail to open, and the `}` it leaves unmatched belongs to
    // the skipped region.
    // D2.11
    uint32_t braces = p->toks[start].kind == TOK_LBRACE ? 1U : 0U;
    uint32_t groups = count_open_groups(p, start);
    while (!at(p, TOK_EOF)) {
        const tok_kind_t k = kind(p);
        if (k == TOK_RBRACE) {
            if (braces > 0) {
                braces--;
                bump(p);
                if (braces == 0) {
                    return;
                }
                continue;
            }
            // A `}` the skip did not see opened and that a `;` follows closes a brace
            // initializer or a struct literal the construct opened, as no block is followed
            // by a `;`.
            // D7.3
            if (peek_kind(p, 1) == TOK_SEMI) {
                bump(p);
                bump(p);
                return;
            }
            // One that a `)` or a `]` follows stands inside a bracket the
            // construct left open, where it closes nothing, so it goes with
            // the skipped region. The premise is that a `}` before a closing
            // bracket is a typo of the construct and not the brace of the body
            // around it, which holds for the half-typed `g(});` and fails only
            // where a stray `)` follows a body's genuine `}`.
            if (groups > 0 && (peek_kind(p, 1) == TOK_RPAREN || peek_kind(p, 1) == TOK_RBRACKET)) {
                bump(p);
                continue;
            }
            // Any other one closes the body the construct sits in and is left
            // to it, except at the top level, where it closes nothing.
            if (level != RECOVER_DECL) {
                return;
            }
            bump(p);
            continue;
        }
        // A declaration keyword outranks a `(` the construct left open, which
        // is a typo of the construct; a `;` inside one is part of it.
        if (braces == 0) {
            const bool in_body = level == RECOVER_STMT || level == RECOVER_CASE;
            if (at_decl_start(p) || (in_body && starts_case(k)) ||
                (level == RECOVER_STMT && starts_statement(k))) {
                return;
            }
            if (groups == 0 && k == TOK_SEMI) {
                // D7.3: a run of `;` is one boundary, not an error of its own
                while (at(p, TOK_SEMI)) {
                    bump(p);
                }
                return;
            }
        }
        if (k == TOK_LBRACE) {
            braces++;
        } else if (k == TOK_LPAREN || k == TOK_LBRACKET) {
            groups++;
        } else if ((k == TOK_RPAREN || k == TOK_RBRACKET) && groups > 0) {
            groups--;
        }
        bump(p);
    }
}

// Drops the tokens the skip stopped in front of that begin no construct: a `)`
// or a `]` that closes nothing, the `;` that follows one, and, at the top level,
// a `}`, which closes nothing there either. Directly after a skip they are the
// tail of the construct that failed, so reading them as the next construct would
// cost a second diagnostic for one mistake. A token of the same kind that reaches
// a parse any other way is a stray one and is reported like any other.
// D14.2
static void drop_leftovers(parser_t* p, int level) {
    while (at(p, TOK_RPAREN) || at(p, TOK_RBRACKET) || at(p, TOK_SEMI) ||
           (level == RECOVER_DECL && at(p, TOK_RBRACE))) {
        bump(p);
    }
}

// Ends the unwind of the construct that began at `start`. A construct that
// produced no node leaves the tokens it failed on to a skip, which the tree keeps
// as an error node so that a later pass sees the region was skipped; one that
// produced a node in spite of the error, the block of a missing `}`, has read
// them already and stands at the boundary itself.
// D14.2
static void recover(parser_t* p, ast_node_t* parent, uint64_t start, bool skipping, int level) {
    if (skipping) {
        skip_to_boundary(p, start, level);
        drop_leftovers(p, level);
        if (p->pos > start) {
            ast_node_t* err = node_at(p, AST_ERROR, tok_loc(p, &p->toks[start]));
            ast_push(parent, finish(p, err));
        }
    }
    p->failed = false;
}

// Parses one statement into `parent` and recovers from a syntax error where
// the statement began: the statement's node when it parsed, an error node over
// the skipped tokens when it did not.
// D14.2
static void parse_statement_into(parser_t* p, ast_node_t* parent) {
    const uint64_t start = p->pos;
    ast_node_t* s = parse_statement(p);
    if (s != NULL) {
        ast_push(parent, s);
    }
    if (p->failed) {
        recover(p, parent, start, s == NULL, RECOVER_STMT);
    }
}

// ---- statements (grammar.md 5) --------------------------------------------

// var_decl = type identifier "=" initializer ";": one declarator, the
// initializer mandatory.
// D7.1
static ast_node_t* parse_var_decl(parser_t* p, bool want_semi) {
    const loc_t loc = here(p);
    ast_node_t* n = node_at(p, AST_VAR_DECL, loc);
    n->a = parse_type(p, false);
    if (n->a == NULL || !expect_name(p, n) || !expect(p, TOK_ASSIGN, "'='")) {
        return NULL;
    }
    n->b = parse_initializer(p);
    if (n->b == NULL) {
        return NULL;
    }
    if (want_semi && !expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return finish(p, n);
}

// assign_head, incdec_head or call_expr: the target is a postfix expression or a
// unary `*` (grammar.md 5). An assignment and an increment carry their operator's
// position, which is where the runtime reports the overwrite check and an
// overflow (toolchain.md 4); the target's own position is on the target node.
// D7.2, D7.3, D17.11
static ast_node_t* parse_simple_head(parser_t* p) {
    const loc_t loc = here(p);
    ast_node_t* e = at(p, TOK_STAR) ? parse_unary(p) : parse_postfix(p);
    if (e == NULL) {
        return NULL;
    }
    if (is_assign_op(kind(p))) {
        ast_node_t* n = node_at(p, AST_ASSIGN, here(p));
        n->op = (int32_t)kind(p);
        n->a = e;
        bump(p);
        n->b = parse_expr(p);
        if (n->b == NULL) {
            return NULL;
        }
        return finish(p, n);
    }
    if (at(p, TOK_PLUS_PLUS) || at(p, TOK_MINUS_MINUS)) {
        ast_node_t* n = node_at(p, AST_INCDEC, here(p));
        n->op = (int32_t)kind(p);
        n->a = e;
        bump(p);
        return finish(p, n);
    }
    // D7.3
    if (e->kind == AST_CALL) {
        ast_node_t* n = node_at(p, AST_CALL_STMT, loc);
        n->a = e;
        return finish(p, n);
    }
    error_expected(p, "an assignment, an increment or a call");
    return NULL;
}

static ast_node_t* parse_simple_statement(parser_t* p) {
    ast_node_t* n = parse_simple_head(p);
    if (n == NULL || !expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return finish(p, n);
}

// The condition of `if` and `while`, always parenthesized.
// D7.4
static ast_node_t* parse_condition(parser_t* p) {
    return parse_paren_expr(p);
}

// Ends the range of every `if` of an else-if chain: the chain nests at its
// tail, so each one runs to the end of the whole chain.
// D20.4
static ast_node_t* finish_if_chain(parser_t* p, ast_node_t* first) {
    ast_node_t* n = first;
    while (n != NULL && n->kind == AST_IF) {
        n = finish(p, n)->c;
    }
    return first;
}

// if_stmt = "if" "(" expr ")" block { "else" "if" "(" expr ")" block } [ "else"
// block ]: braces on every branch. The chain is built iteratively, so a long one
// does not recurse.
// D7.4
static ast_node_t* parse_if(parser_t* p) {
    ast_node_t* first = NULL;
    ast_node_t* prev = NULL;
    for (;;) {
        ast_node_t* n = node_at(p, AST_IF, here(p));
        bump(p);
        n->a = parse_condition(p);
        if (n->a == NULL) {
            return NULL;
        }
        n->b = parse_block(p);
        if (n->b == NULL) {
            return NULL;
        }
        if (prev != NULL) {
            prev->c = n;
        }
        if (first == NULL) {
            first = n;
        }
        prev = n;
        if (!at(p, TOK_KW_ELSE)) {
            return finish_if_chain(p, first);
        }
        bump(p);
        if (at(p, TOK_KW_IF)) {
            continue;
        }
        prev->c = parse_block(p);
        if (prev->c == NULL) {
            return NULL;
        }
        return finish_if_chain(p, first);
    }
}

static ast_node_t* parse_while(parser_t* p) {
    ast_node_t* n = node_at(p, AST_WHILE, here(p));
    bump(p);
    n->a = parse_condition(p);
    if (n->a == NULL) {
        return NULL;
    }
    n->b = parse_block(p);
    if (n->b == NULL) {
        return NULL;
    }
    return finish(p, n);
}

// do_stmt = "do" block "while" "(" expr ")" ";". The whole form is parsed before
// `do`-`while` is reported as outside the C bootstrap's subset, so this is the
// parser the self-hosted compiler keeps, minus the one check.
// D7.5
static ast_node_t* parse_do(parser_t* p) {
    const loc_t loc = here(p);
    ast_node_t* n = node_at(p, AST_DO_WHILE, loc);
    bump(p);
    n->a = parse_block(p);
    if (n->a == NULL || !expect(p, TOK_KW_WHILE, "'while'")) {
        return NULL;
    }
    n->b = parse_condition(p);
    if (n->b == NULL || !expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    if (unsupported(p, loc, "do-while")) {
        return NULL;
    }
    return finish(p, n);
}

// grammar.md 7.3: after `for (`, a speculative parse of `type identifier`
// decides between the range loop, a declaration init and everything else.
static int speculate_for_form(parser_t* p) {
    if (at(p, TOK_SEMI)) {
        return FOR_PLAIN;
    }
    const spec_state_t s = spec_begin(p);
    int form = FOR_PLAIN;
    const ast_node_t* t = parse_type(p, false);
    if (t != NULL && !p->failed && at(p, TOK_IDENT)) {
        bump(p);
        if (at(p, TOK_COLON)) {
            form = FOR_RANGE;
        } else if (at(p, TOK_ASSIGN)) {
            form = FOR_DECL;
        }
    } else if (p->failed && read_marker(p, s.pos)) {
        // A misplaced marker makes the init a declaration, as at statement
        // level, so that the marker is what the init reports (grammar.md 7.1).
        form = FOR_DECL;
    }
    spec_rewind(p, s);
    return form;
}

// range_for_stmt = "for" "(" type identifier ":" expr ")" block.
// D7.5
static ast_node_t* parse_range_for(parser_t* p, loc_t loc) {
    ast_node_t* n = node_at(p, AST_RANGE_FOR, loc);
    n->a = parse_type(p, false);
    if (n->a == NULL || !expect_name(p, n) || !expect(p, TOK_COLON, "':'")) {
        return NULL;
    }
    n->b = parse_expr(p);
    if (n->b == NULL || !expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    n->c = parse_block(p);
    if (n->c == NULL) {
        return NULL;
    }
    return finish(p, n);
}

// for_stmt = "for" "(" [ for_init ] ";" [ expr ] ";" [ for_step ] ")" block:
// `for (;;)` is legal and every part may be empty.
// D7.5
static ast_node_t* parse_for_tail(parser_t* p, loc_t loc, int form) {
    ast_node_t* n = node_at(p, AST_FOR, loc);
    if (form == FOR_DECL || !at(p, TOK_SEMI)) {
        n->a = form == FOR_DECL ? parse_var_decl(p, false) : parse_simple_head(p);
        if (n->a == NULL) {
            return NULL;
        }
    }
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    if (!at(p, TOK_SEMI)) {
        n->b = parse_expr(p);
        if (n->b == NULL) {
            return NULL;
        }
    }
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    if (!at(p, TOK_RPAREN)) {
        n->c = parse_simple_head(p);
        if (n->c == NULL) {
            return NULL;
        }
    }
    if (!expect(p, TOK_RPAREN, "')'")) {
        return NULL;
    }
    n->d = parse_block(p);
    if (n->d == NULL) {
        return NULL;
    }
    return finish(p, n);
}

static ast_node_t* parse_for(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    if (!expect(p, TOK_LPAREN, "'('") || !enter(p)) {
        return NULL;
    }
    ast_node_t* n = NULL;
    if (check_no_leading_marker(p)) {
        const int form = speculate_for_form(p);
        n = form == FOR_RANGE ? parse_range_for(p, loc) : parse_for_tail(p, loc, form);
    }
    leave(p);
    return n;
}

// Where the statements of a case body stop: the next clause or the end of the
// switch, and, since a missing `}` ends every body, a top-level declaration.
// D7.6, D14.2
static bool at_case_end(parser_t* p) {
    return at(p, TOK_KW_CASE) || at(p, TOK_KW_DEFAULT) || at(p, TOK_RBRACE) || at(p, TOK_EOF) ||
           at_decl_start(p);
}

// switch_stmt = "switch" "(" expr ")" "{" { case_clause } "}"; each case body
// is an implicit block scope.
// D7.6
static ast_node_t* parse_case(parser_t* p) {
    ast_node_t* n = node_at(p, AST_CASE, here(p));
    if (at(p, TOK_KW_DEFAULT)) {
        n->flags = AST_FLAG_DEFAULT;
        bump(p);
    } else {
        bump(p);
        for (;;) {
            ast_node_t* label = parse_expr(p);
            if (label == NULL) {
                return NULL;
            }
            ast_push(n, label);
            if (!at(p, TOK_COMMA)) {
                break;
            }
            bump(p);
        }
    }
    if (!expect(p, TOK_COLON, "':'")) {
        return NULL;
    }
    // D20.4: an empty clause's block starts just after the `:`
    ast_node_t* body = node_at(p, AST_BLOCK, at_case_end(p) ? here_implicit(p) : here(p));
    // D14.2: the statements of the clause are a recovery point
    while (!at_case_end(p)) {
        parse_statement_into(p, body);
    }
    n->a = finish(p, body);
    return finish(p, n);
}

static ast_node_t* parse_switch(parser_t* p) {
    ast_node_t* n = node_at(p, AST_SWITCH, here(p));
    bump(p);
    n->a = parse_paren_expr(p);
    if (n->a == NULL || !expect(p, TOK_LBRACE, "'{'") || !enter(p)) {
        return NULL;
    }
    // The clauses are a recovery point of their own, so a broken label list, a
    // missing `:` or a token that is no clause at all costs one diagnostic and the
    // clauses after it are read; the switch is returned whether or not its `}` was
    // found, as a block is. A switch body holds clauses and nothing else, so anything
    // else in it is skipped here rather than handed back to the enclosing block,
    // which would read the clauses after it as statements.
    // D7.6, D14.2
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF) && !at_decl_start(p)) {
        const uint64_t start = p->pos;
        ast_node_t* clause = NULL;
        if (at(p, TOK_KW_CASE) || at(p, TOK_KW_DEFAULT)) {
            clause = parse_case(p);
            if (clause != NULL) {
                ast_push(n, clause);
            }
        } else {
            error_expected(p, "'case' or 'default'");
        }
        if (p->failed) {
            recover(p, n, start, clause == NULL, RECOVER_CASE);
        }
    }
    leave(p);
    (void)expect(p, TOK_RBRACE, "'}'");
    return finish(p, n);
}

// defer_stmt = "defer" ( assign_stmt | incdec_stmt | call_stmt | block ).
// D7.8
static ast_node_t* parse_defer(parser_t* p) {
    ast_node_t* n = node_at(p, AST_DEFER, here(p));
    bump(p);
    n->a = at(p, TOK_LBRACE) ? parse_block(p) : parse_simple_statement(p);
    if (n->a == NULL) {
        return NULL;
    }
    return finish(p, n);
}

// return_stmt = "return" [ expr ] ";".
// D7.11
static ast_node_t* parse_return(parser_t* p) {
    ast_node_t* n = node_at(p, AST_RETURN, here(p));
    bump(p);
    if (!at(p, TOK_SEMI)) {
        n->a = parse_expr(p);
        if (n->a == NULL) {
            return NULL;
        }
    }
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return finish(p, n);
}

static ast_node_t* parse_break_or_continue(parser_t* p) {
    ast_node_t* n = node_at(p, at(p, TOK_KW_BREAK) ? AST_BREAK : AST_CONTINUE, here(p));
    bump(p);
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return finish(p, n);
}

// grammar.md 7.1: a `prim_type`, `string` or `fn` starts a declaration, and
// so does `void`, which opens no expression either.
static bool starts_declaration(const parser_t* p) {
    prim_kind_t prim = PRIM_VOID;
    const tok_kind_t k = kind(p);
    return prim_of_token(k, &prim) || k == TOK_KW_STRING || k == TOK_KW_VOID || k == TOK_KW_FN;
}

// grammar.md 7.1: otherwise a speculative type parse decides, and the
// statement is a declaration when an identifier follows the type.
static bool speculate_declaration(parser_t* p) {
    const spec_state_t s = spec_begin(p);
    const ast_node_t* t = parse_type(p, false);
    // A type parse that died on a marker leaves no expression reading of the
    // statement, so the declaration branch wins and reports the marker.
    const bool decl =
        (t != NULL && !p->failed && at(p, TOK_IDENT)) || (p->failed && read_marker(p, s.pos));
    spec_rewind(p, s);
    return decl;
}

static ast_node_t* parse_statement(parser_t* p) {
    switch (kind(p)) {
    case TOK_LBRACE:
        return parse_block(p);
    case TOK_KW_IF:
        return parse_if(p);
    case TOK_KW_WHILE:
        return parse_while(p);
    case TOK_KW_DO:
        return parse_do(p);
    case TOK_KW_FOR:
        return parse_for(p);
    case TOK_KW_SWITCH:
        return parse_switch(p);
    case TOK_KW_DEFER:
        return parse_defer(p);
    case TOK_KW_RETURN:
        return parse_return(p);
    case TOK_KW_BREAK:
    case TOK_KW_CONTINUE:
        return parse_break_or_continue(p);
    case TOK_SEMI:
        // D7.3
        error_here(p, "an empty statement is not allowed");
        return NULL;
    default:
        break;
    }
    if (!check_no_leading_marker(p)) {
        return NULL;
    }
    if (starts_declaration(p) || speculate_declaration(p)) {
        return parse_var_decl(p, true);
    }
    return parse_simple_statement(p);
}

// The statements of a block and its closing `}`, with the `{` already read or
// reported missing. The statements are a recovery point, and the block ends at a
// top-level declaration as well as at its `}`, so a missing `}` is reported once,
// here. The node is returned whether or not the `}` was found, so that the
// enclosing constructs unwind quietly to the next recovery point and the tree
// still covers the file.
// D14.2
static ast_node_t* parse_block_tail(parser_t* p, loc_t loc) {
    if (!enter(p)) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_BLOCK, loc);
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF) && !at_decl_start(p)) {
        parse_statement_into(p, n);
    }
    leave(p);
    (void)expect(p, TOK_RBRACE, "'}'");
    return finish(p, n);
}

// block = "{" { statement } "}" (grammar.md 5). A block that stands where a
// statement is expected needs its `{`: the statement forms are braced, so a
// missing one is a different mistake and the construct fails.
// D7.4
static ast_node_t* parse_block(parser_t* p) {
    const loc_t loc = here(p);
    if (!expect(p, TOK_LBRACE, "'{'")) {
        return NULL;
    }
    return parse_block_tail(p, loc);
}

// Reads the `{` that opens the body of a declaration, or reports it missing and
// reads the body all the same, which is the mirror of the missing `}`: a
// declaration header is complete before its `{` and the `}` that closes the body
// is usually still in the file, so one missing brace costs one diagnostic instead
// of one per statement or field in the body.
// D14.2
static void open_body(parser_t* p) {
    if (at(p, TOK_LBRACE)) {
        bump(p);
    } else {
        error_expected(p, "'{'");
    }
}

// The body of a function, whose `{` may be missing.
// D8.1
static ast_node_t* parse_body(parser_t* p) {
    const loc_t loc = here(p);
    open_body(p);
    return parse_block_tail(p, loc);
}

// ---- declarations (grammar.md 2, 3) ---------------------------------------

// param_list = param { "," param } with param = type identifier; no trailing
// comma in a parameter list.
// D6.5
static bool parse_params(parser_t* p, ast_node_t* fn) {
    if (!expect(p, TOK_LPAREN, "'('")) {
        return false;
    }
    if (!at(p, TOK_RPAREN)) {
        for (;;) {
            ast_node_t* param = node_at(p, AST_PARAM, here(p));
            param->a = parse_type(p, false);
            if (param->a == NULL || !expect_name(p, param)) {
                return false;
            }
            ast_push(fn, finish(p, param));
            if (!at(p, TOK_COMMA)) {
                break;
            }
            bump(p);
        }
    }
    return expect(p, TOK_RPAREN, "')'");
}

// An extern parameter list has one or more fixed parameters before `...`.
// The tail mark is a flag, not a parameter node.
// D9.8
static bool parse_extern_params(parser_t* p, ast_node_t* fn) {
    if (!expect(p, TOK_LPAREN, "'('")) {
        return false;
    }
    if (!at(p, TOK_RPAREN)) {
        for (;;) {
            ast_node_t* param = node_at(p, AST_PARAM, here(p));
            param->a = parse_type(p, false);
            if (param->a == NULL || !expect_name(p, param)) {
                return false;
            }
            ast_push(fn, finish(p, param));
            if (!at(p, TOK_COMMA)) {
                break;
            }
            bump(p);
            if (at(p, TOK_ELLIPSIS)) {
                fn->tail_loc = here(p);
                fn->flags |= AST_FLAG_VARIADIC;
                bump(p);
                break;
            }
        }
    }
    return expect(p, TOK_RPAREN, "')'");
}

// fn_decl = "fn" identifier "(" [ param_list ] ")" return_type block, with the
// `fn` already parsed: the name is the second token and the result comes last.
// D8.1
static ast_node_t* parse_fn_decl(parser_t* p, loc_t loc) {
    ast_node_t* n = node_at(p, AST_FN_DECL, loc);
    if (!expect_name(p, n) || !parse_params(p, n)) {
        return NULL;
    }
    n->a = parse_return_type(p);
    if (n->a == NULL) {
        return NULL;
    }
    n->b = parse_body(p);
    if (n->b == NULL) {
        return NULL;
    }
    return finish(p, n);
}

// At the top level a `fn` opens a function definition (grammar.md 7). When the
// `fn` is followed by `(` rather than by a name, it is the base of a function
// type and the declaration is a global of that type.
// D3.10, D7.10
static ast_node_t* parse_fn_top_decl(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    if (at(p, TOK_IDENT)) {
        return parse_fn_decl(p, loc);
    }
    ast_node_t* base = parse_fn_type_params(p, loc);
    if (base == NULL) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_VAR_DECL, loc);
    n->a = parse_type_after_base(p, loc, base);
    if (n->a == NULL || !expect_name(p, n) || !expect(p, TOK_ASSIGN, "'='")) {
        return NULL;
    }
    n->b = parse_initializer(p);
    if (n->b == NULL || !expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return finish(p, n);
}

// extern_decl = "extern" "fn" identifier "(" extern_params ")" return_type
// ";": a declaration with no body. An extern variable tail follows one or
// more fixed parameters.
// D9.8
static ast_node_t* parse_extern_decl(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    if (!expect(p, TOK_KW_FN, "'fn'")) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_FN_DECL, loc);
    n->flags = AST_FLAG_EXTERN;
    if (!expect_name(p, n) || !parse_extern_params(p, n)) {
        return NULL;
    }
    n->a = parse_return_type(p);
    if (n->a == NULL) {
        return NULL;
    }
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return finish(p, n);
}

// struct_decl = "struct" identifier "{" field { field } "}": no trailing
// semicolon and at least one field.
// D3.8
static ast_node_t* parse_struct_decl(parser_t* p) {
    ast_node_t* n = node_at(p, AST_STRUCT_DECL, here(p));
    bump(p);
    if (!expect_name(p, n)) {
        return NULL;
    }
    open_body(p);
    if (!enter(p)) {
        return NULL;
    }
    if (at(p, TOK_RBRACE)) {
        error_here(p, "a struct has at least one field");
    }
    // D14.2: a broken field is skipped to its `;` or to the body's `}`
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF) && !at_decl_start(p)) {
        const uint64_t start = p->pos;
        ast_node_t* field = node_at(p, AST_FIELD_DECL, here(p));
        field->a = parse_type(p, false);
        const bool ok = field->a != NULL && expect_name(p, field) && expect(p, TOK_SEMI, "';'");
        if (ok) {
            ast_push(n, finish(p, field));
        }
        if (p->failed) {
            recover(p, n, start, !ok, RECOVER_FIELD);
        }
    }
    leave(p);
    (void)expect(p, TOK_RBRACE, "'}'");
    return finish(p, n);
}

// enum_decl = "enum" identifier "{" enum_member { "," enum_member } [ "," ]
// "}".
// D3.9
static ast_node_t* parse_enum_decl(parser_t* p) {
    ast_node_t* n = node_at(p, AST_ENUM_DECL, here(p));
    bump(p);
    if (!expect_name(p, n)) {
        return NULL;
    }
    open_body(p);
    if (!enter(p)) {
        return NULL;
    }
    bool ok = true;
    if (at(p, TOK_RBRACE)) {
        error_here(p, "an enum has at least one member");
        ok = false;
    }
    // D14.2: the body still ends where a top-level declaration starts
    while (ok && !at_decl_start(p)) {
        ast_node_t* member = node_at(p, AST_ENUM_MEMBER, here(p));
        ok = expect_name(p, member);
        if (ok && at(p, TOK_ASSIGN)) {
            bump(p);
            member->a = parse_expr(p);
            ok = member->a != NULL;
        }
        if (!ok) {
            break;
        }
        ast_push(n, finish(p, member));
        if (!at(p, TOK_COMMA)) {
            break;
        }
        bump(p);
        if (at(p, TOK_RBRACE)) {
            break;
        }
    }
    leave(p);
    // A member that did not parse leaves the body to the declaration skip; a body
    // that ran to a top-level declaration keeps its node, as a block and a struct
    // body do, so the enum and the declarations after it stay in the tree.
    // D14.2
    if (!ok) {
        return NULL;
    }
    (void)expect(p, TOK_RBRACE, "'}'");
    return finish(p, n);
}

// D9.3: whether the last segment names a module or a symbol is
// resolution's business
static bool parse_import_items(parser_t* p, ast_node_t* n) {
    bump(p);
    for (;;) {
        ast_node_t* item = node_at(p, AST_IMPORT_ITEM, here(p));
        if (!expect_name(p, item)) {
            return false;
        }
        if (at(p, TOK_KW_AS)) {
            const loc_t loc = here(p);
            bump(p);
            item->a = node_at(p, AST_IDENT, loc);
            if (!expect_name(p, item->a)) {
                return false;
            }
            (void)finish(p, item->a);
        }
        ast_push(n, finish(p, item));
        if (!at(p, TOK_COMMA)) {
            break;
        }
        bump(p);
        if (at(p, TOK_RBRACE)) {
            break;
        }
    }
    return expect(p, TOK_RBRACE, "'}'");
}

// The two spellings a path separator is mistaken for, each reported where the
// separator stands. `..` is one token and longest match wins, so the separator of
// `import a..b;` never reaches the test for `.`; `::` separated a path until the
// separator became `.`, so it is what every source written before that spells.
// Either would otherwise end the path and be reported as the `;` that is then
// missing, which names the wrong mistake.
// D2.10, D9.1
static bool check_path_separator(parser_t* p) {
    if (at(p, TOK_DOT_DOT)) {
        error_here(p, "a module path is separated by '.', not '..'");
        return false;
    }
    if (!at(p, TOK_COLON)) {
        return true;
    }
    if (peek_kind(p, 1) == TOK_COLON) {
        error_here(p, "a module path is separated by '.', not '::'");
    } else {
        error_here(p, "a module path is separated by '.', not ':'");
    }
    return false;
}

static ast_node_t* parse_import(parser_t* p) {
    ast_node_t* n = node_at(p, AST_IMPORT, here(p));
    bump(p);
    ast_node_t* path = node_at(p, AST_PATH, here(p));
    n->a = path;
    for (;;) {
        ast_node_t* segment = node_at(p, AST_IDENT, here(p));
        if (!expect_name(p, segment)) {
            return NULL;
        }
        ast_push(path, finish(p, segment));
        // The path ends at its last segment, so it is extended here and not
        // after the loop: the `.{ ... }` of an item list is not part of it.
        (void)finish(p, path);
        if (!check_path_separator(p)) {
            return NULL;
        }
        if (!at(p, TOK_DOT)) {
            break;
        }
        bump(p);
        if (at(p, TOK_LBRACE)) {
            if (!parse_import_items(p, n)) {
                return NULL;
            }
            break;
        }
    }
    if (ast_len(n) == 0 && at(p, TOK_KW_AS)) {
        const loc_t loc = here(p);
        bump(p);
        n->b = node_at(p, AST_IDENT, loc);
        if (!expect_name(p, n->b)) {
            return NULL;
        }
        (void)finish(p, n->b);
    }
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return finish(p, n);
}

// top_decl = fn_decl | extern_decl | struct_decl | enum_decl | global_decl
// (grammar.md 2); a global is a var_decl whose initializer is constant.
// D7.10
static ast_node_t* parse_top_decl(parser_t* p) {
    switch (kind(p)) {
    case TOK_KW_FN:
        return parse_fn_top_decl(p);
    case TOK_KW_EXTERN:
        return parse_extern_decl(p);
    case TOK_KW_STRUCT:
        return parse_struct_decl(p);
    case TOK_KW_ENUM:
        return parse_enum_decl(p);
    case TOK_KW_IMPORT:
        // D9.3
        error_here(p, "an import comes before every declaration");
        return NULL;
    default:
        break;
    }
    if (!check_no_leading_marker(p)) {
        return NULL;
    }
    return parse_var_decl(p, true);
}

// Parses one declaration into `mod` and recovers from a syntax error where the
// declaration began, as a block does with a statement. `import` parses the import
// form, which comes first in a file; every declaration of a file is read,
// whatever the ones before it did.
// D9.3, D14.2
static void parse_decl_into(parser_t* p, ast_node_t* mod, bool import) {
    const uint64_t start = p->pos;
    ast_node_t* decl = import ? parse_import(p) : parse_top_decl(p);
    if (decl != NULL) {
        ast_push(mod, decl);
    }
    if (p->failed) {
        recover(p, mod, start, decl == NULL, RECOVER_DECL);
    }
}

ast_node_t* parse_module(const char* file,
                         const token_t* toks,
                         uint64_t ntoks,
                         ast_arena_t* arena) {
    if (ntoks == 0 || toks[ntoks - 1].kind != TOK_EOF) {
        fatal_internal("parse_module: the token array does not end in end of file");
    }
    parser_t p;
    p.file = file;
    p.toks = toks;
    p.ntoks = ntoks;
    p.pos = 0;
    p.arena = arena;
    p.depth = 0;
    p.spec = 0;
    p.failed = false;
    p.last = loc_make(file, 0, 0);
    sb_init(&p.msg);

    ast_node_t* mod = ast_new(arena, AST_MODULE, loc_make(file, 1, 1));
    // D9.3, D14.2: both loops are recovery points, so one error costs one
    while (at(&p, TOK_KW_IMPORT)) {
        parse_decl_into(&p, mod, true);
    }
    while (!at(&p, TOK_EOF)) {
        parse_decl_into(&p, mod, false);
    }
    sb_free(&p.msg);
    return finish(&p, mod);
}
