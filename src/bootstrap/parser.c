// Implements the parser and its recovery rules; see parser.h.
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
enum { PARSE_MAX_DEPTH = 256 };

// The `for` forms the grammar distinguishes after `for (`.
enum { FOR_PLAIN = 0, FOR_RANGE = 1, FOR_DECL = 2 };

// The binary precedence levels, loosest first; PREC_NONE is not a binary
// operator. `?:` sits below PREC_OR and is parsed on its own, since it is the one
// right-associative form.
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
    uint32_t depth; // open nested constructs
    uint32_t spec;  // running speculative parses; no diagnostic while > 0
    bool failed;    // unwinding the construct a syntax error hit
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
static loc_t here_implicit(const parser_t* p) {
    if (p->pos == 0) {
        return loc_make(p->file, cur(p)->line, cur(p)->col);
    }
    const token_t* t = &p->toks[p->pos - 1];
    return loc_make(p->file, t->line, (uint32_t)(t->col + t->len));
}

// Ends `n` at the last consumed token. bump never consumes TOK_EOF.
// Each function ends the nodes that it creates.
// A function that returns a child node leaves that node's range unchanged.
// loc_extend never shrinks a range, so a growing construct keeps its widest end.
// A NULL node is a failed parse and passes through.
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

// Reports `text` at `loc` and starts the failed construct's unwind.
// Later reports stay silent until recovery clears `failed`.
// The parser drops a repeated error at the same start position.
// It also stops after the shared DIAG_MAX_PER_FILE budget is spent.
// Parsing continues after that limit. A speculative parse reports nothing.
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

// Reports a feature that the C bootstrap does not support.
// A speculative parse skips this report so it can still determine the syntax shape.
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

// Begins a speculative parse. The parser reports nothing until the rewind.
// The parse starts with `failed` clear and reads it to select a branch.
// spec_rewind restores the caller's failure state.
// Recovery points can speculate while `failed` is set.
// The first statement after a missing `{` can also speculate in that state.
static spec_state_t spec_begin(parser_t* p) {
    spec_state_t s;
    s.pos = p->pos;
    s.depth = p->depth;
    s.failed = p->failed;
    p->failed = false;
    p->spec++;
    return s;
}

// Returns true when a failed speculative type parse reads an unbracketed marker.
// A marker follows its type element, and no expression starts with a marker.
// Thus, the parser treats this prefix as a declaration and reports the marker.
// This test is a heuristic. Expressions can contain types inside brackets.
// A marker in such a bracket can belong to the expression, so the test ignores it.
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

// ---- types -------------------------------------------------

static ast_node_t* node_at(parser_t* p, ast_kind_t k, loc_t loc) {
    return ast_new(p->arena, k, loc);
}

// The primitive a keyword names; `void` is a type kind of its own.
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
// qualified name.
static bool starts_base_type(tok_kind_t k) {
    prim_kind_t prim = PRIM_VOID;
    return prim_of_token(k, &prim) || k == TOK_KW_STRING || k == TOK_KW_VOID || k == TOK_KW_FN ||
           k == TOK_IDENT;
}

// Nothing precedes the base type: every `mut` and `own` follows the type
// element it qualifies, so no type, declaration or statement begins with one.
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

// A `mut` marks the storage of the element that it follows.
// Fixed-array elements share the array storage, so no `mut` precedes `[N]`.
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
        // Resolution decides whether the first part is a module.
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
static const char* base_own_error(const ast_node_t* base) {
    if (base->kind == AST_TYPE_STRING) {
        return NULL;
    }
    return "an own marks a reference: write it after a '*' or an '@', or on a string";
}

// Adds one suffix to `t` and counts it against the nesting limit, which covers
// type suffixes.
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
// marker.
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

// The C bootstrap accepts at most one array or span level in one written type.
// Its layout and code generation do not support a second aggregate level.
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

// Parses the suffixes of a type after its base type.
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
// }: the type of an array literal, which has no base marker, no
// marker on a dimension and no trailing reference suffix.
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

// An allocated type omits markers from the outermost position that `new` fills.
// Inside `new(...)`, `own` follows `*`, and a span uses a count instead of `@`.
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

// A `mut` inside `new(...)` marks storage that `new` does not allocate.
// Thus, a `*` must follow that `mut` and create an outer position.
static bool check_alloc_mut(parser_t* p, const markers_t* m) {
    if ((m->flags & AST_FLAG_MUT) != 0 && !at(p, TOK_STAR)) {
        error_at(p, m->mut_loc, ALLOC_MUT_ERROR);
        return false;
    }
    return true;
}

// alloc_type = base_type [ "mut" ] { "*" [ "own" ] [ "mut" ] }
// { "[" const_expr "]" }, whose last `mut` position is empty.
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

// ---- expressions -------------------------------------------

// The precedence of a binary operator, PREC_NONE when `k` is not one; higher
// binds tighter. All of them are left-associative.
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

// A struct literal starts with an identifier, an optional `. identifier`, and `{`.
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

// An `array_type` followed by `{` starts an array literal. Speculation only decides; the
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

// initializer = expr | brace_init.
static ast_node_t* parse_initializer(parser_t* p) {
    if (at(p, TOK_LBRACE)) {
        return parse_brace_init(p);
    }
    return parse_expr(p);
}

// ---- recovery --------------------------------------------

// Recovery can stop at a statement, switch clause, field, or module declaration.
enum { RECOVER_STMT = 0, RECOVER_CASE = 1, RECOVER_FIELD = 2, RECOVER_DECL = 3 };

// A recovery skip stops before a keyword that can start the next statement.
static bool starts_statement(tok_kind_t k) {
    return k == TOK_KW_IF || k == TOK_KW_WHILE || k == TOK_KW_FOR || k == TOK_KW_SWITCH ||
           k == TOK_KW_DEFER || k == TOK_KW_RETURN || k == TOK_KW_BREAK || k == TOK_KW_CONTINUE ||
           k == TOK_KW_DO;
}

// The keywords that begin a case clause. Neither appears in a statement or an
// expression, so a skip inside a switch stops before one rather than
// swallowing the clause that follows the broken one.
static bool starts_case(tok_kind_t k) {
    return k == TOK_KW_CASE || k == TOK_KW_DEFAULT;
}

// Returns true when the current token starts a top-level declaration.
// `struct`, `enum`, `extern`, and `import` always start declarations.
// A `fn` starts one when a name and `(` follow it.
// A function type has `(` directly after `fn` instead.
// Declaration starts also end an open body that has no `}`.
// Thus, one missing `}` produces one diagnostic.
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

// Counts unmatched `(` and `[` in the failed construct.
// Recovery then finds a boundary at the construct's own group level.
// Thus, a `for` header semicolon or argument comma is not a boundary.
// This function does not count braces because their intended scope is ambiguous.
// Recovery counts only the braces that it opens during its skip.
// It leaves other closing braces for their enclosing bodies.
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

// Skips a failed construct to the recovery boundary for `level`.
// Outside open groups, recovery consumes a semicolon and braces that it opened.
// It leaves other closing braces and valid construct starts for the next parse.
// The end of the file ends each skip.
// Field and declaration recovery passes statement starts.
// Declaration recovery also drops an unmatched top-level `}`.
// Case recovery passes statement starts in the failed body.
// Recovery consumes one token when the failed construct consumed none.
static void skip_to_boundary(parser_t* p, uint64_t start, int level) {
    if (p->pos == start) {
        bump(p);
    }
    // A failed block already consumed its opening brace.
    // Thus, the first closing brace belongs to the skipped block.
    // A nesting-limit failure leaves the same unmatched closing brace.
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
            // An unseen `}` before `;` closes an initializer or struct literal.
            // No block ends with `};`.
            if (peek_kind(p, 1) == TOK_SEMI) {
                bump(p);
                bump(p);
                return;
            }
            // A `}` before `)` or `]` can be a typo inside an open group.
            // In that case, it closes no body and belongs to the skipped region.
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
                // A run of `;` is one boundary, not a separate error.
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

// Drops boundary tokens that cannot start a construct.
// These tokens are unmatched `)` or `]`, a following `;`, and a top-level `}`.
// Directly after a skip, they belong to the failed construct.
// Dropping them prevents a second diagnostic for the same error.
// The parser reports the same token when it reaches the token without recovery.
static void drop_leftovers(parser_t* p, int level) {
    while (at(p, TOK_RPAREN) || at(p, TOK_RBRACKET) || at(p, TOK_SEMI) ||
           (level == RECOVER_DECL && at(p, TOK_RBRACE))) {
        bump(p);
    }
}

// Ends the unwind of the construct that began at `start`.
// A construct with no node leaves its failed tokens for recovery.
// Recovery keeps the skipped range as an error node.
// A partial node has already read its tokens and stands at the boundary.
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

// Parses one statement into `parent`.
// On failure, adds an error node for the skipped tokens and resumes at a boundary.
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

// ---- statements --------------------------------------------

// var_decl = type identifier "=" initializer ";": one declarator, the
// initializer mandatory.
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
// unary `*`. An assignment and an increment carry their operator's
// position, which is where the runtime reports the overwrite check and an
// overflow; the target's own position is on the target node.
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
static ast_node_t* parse_condition(parser_t* p) {
    return parse_paren_expr(p);
}

// Ends the range of every `if` of an else-if chain: the chain nests at its
// tail, so each one runs to the end of the whole chain.
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

// After `for (`, a speculative `type identifier` distinguishes the three loop forms.
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
        // level, so that the marker is what the init reports.
        form = FOR_DECL;
    }
    spec_rewind(p, s);
    return form;
}

// range_for_stmt = "for" "(" type identifier ":" expr ")" block.
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
static bool at_case_end(parser_t* p) {
    return at(p, TOK_KW_CASE) || at(p, TOK_KW_DEFAULT) || at(p, TOK_RBRACE) || at(p, TOK_EOF) ||
           at_decl_start(p);
}

// switch_stmt = "switch" "(" expr ")" "{" { case_clause } "}"; each case body
// is an implicit block scope.
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
    // An empty clause block starts just after the `:`.
    ast_node_t* body = node_at(p, AST_BLOCK, at_case_end(p) ? here_implicit(p) : here(p));
    // The clause statements form a recovery point.
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
    // Switch clauses have their own recovery point.
    // A broken label, missing `:`, or invalid clause produces one diagnostic.
    // Recovery then reads the later clauses.
    // The parser returns the switch even when its closing brace is missing.
    // It skips non-clause tokens here so the enclosing block cannot read them.
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

// A primitive type, `string`, `fn`, or `void` starts a declaration.
static bool starts_declaration(const parser_t* p) {
    prim_kind_t prim = PRIM_VOID;
    const tok_kind_t k = kind(p);
    return prim_of_token(k, &prim) || k == TOK_KW_STRING || k == TOK_KW_VOID || k == TOK_KW_FN;
}

// Otherwise, an identifier after a speculative type makes the statement a declaration.
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

// Parses a block after the parser reads or reports its opening brace.
// Statements form a recovery point.
// A top-level declaration also ends a block with no closing brace.
// The parser reports that missing brace once.
// It returns the partial node so enclosing constructs can recover.
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

// block = "{" { statement } "}". A block that stands where a
// statement is expected needs its `{`: the statement forms are braced, so a
// missing one is a different mistake and the construct fails.
static ast_node_t* parse_block(parser_t* p) {
    const loc_t loc = here(p);
    if (!expect(p, TOK_LBRACE, "'{'")) {
        return NULL;
    }
    return parse_block_tail(p, loc);
}

// Reads the `{` that opens a declaration body.
// If it is missing, reports one error and reads the body without it.
// The declaration header is complete, and the closing brace usually remains.
// Thus, one missing opening brace produces one diagnostic.
static void open_body(parser_t* p) {
    if (at(p, TOK_LBRACE)) {
        bump(p);
    } else {
        error_expected(p, "'{'");
    }
}

// The body of a function, whose `{` may be missing.
static ast_node_t* parse_body(parser_t* p) {
    const loc_t loc = here(p);
    open_body(p);
    return parse_block_tail(p, loc);
}

// ---- declarations ---------------------------------------

// param_list = param { "," param } with param = type identifier; no trailing
// comma in a parameter list.
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

// At the top level a `fn` opens a function definition. When the
// `fn` is followed by `(` rather than by a name, it is the base of a function
// type and the declaration is a global of that type.
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
    // Skip a broken field to its `;` or the body's `}`.
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
    // The body still ends where a top-level declaration starts.
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
    // A failed member leaves the body for declaration recovery.
    // A body that reaches a top-level declaration keeps its node.
    // Thus, the tree keeps the enum and the declarations that follow it.
    if (!ok) {
        return NULL;
    }
    (void)expect(p, TOK_RBRACE, "'}'");
    return finish(p, n);
}

// Resolution decides whether the last segment names a module or a symbol.
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

// Reports two invalid path separators at the separator position.
// Longest-match lexing makes `..` one token in `import a..b;`.
// Old source can also contain `::`, which preceded the current `.` separator.
// Without this test, either form causes an incorrect missing-`;` diagnostic.
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

// A top-level declaration is a function, extern, struct, enum, or global variable.
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
    // Both loops are recovery points, so one error removes one subtree.
    while (at(&p, TOK_KW_IMPORT)) {
        parse_decl_into(&p, mod, true);
    }
    while (!at(&p, TOK_EOF)) {
        parse_decl_into(&p, mod, false);
    }
    sb_free(&p.msg);
    return finish(&p, mod);
}
