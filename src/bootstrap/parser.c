// The parser; see parser.h. The grammar is grammar.md, the diagnostics
// follow D14.2 and the three speculative points are grammar.md 7.
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
// branches and types nest at most this deep (D2.11).
enum { PARSE_MAX_DEPTH = 256 };

// The `for` forms grammar.md 7.3 distinguishes after `for (`.
enum { FOR_PLAIN = 0, FOR_RANGE = 1, FOR_DECL = 2 };

// The binary precedence levels of D6.1, loosest first; PREC_NONE is not a
// binary operator. `?:` sits below PREC_OR and is parsed on its own, since it
// is the one right-associative form.
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
    uint32_t depth; // open nested constructs (D2.11)
    uint32_t spec;  // running speculative parses; no diagnostic while > 0
    bool failed;    // a syntax error was hit; the file is abandoned (D14.2)
    bool reported;  // the one diagnostic of D14.2 was written
    sb_t msg;       // the message under construction
} parser_t;

// The state a speculative parse restores when it rewinds.
typedef struct {
    uint64_t pos;
    uint32_t depth;
    bool failed;
} spec_state_t;

// The `own` and `mut` written after one type element, with the position of
// the `mut` for the diagnostics that point at it (D5.3, D17.2).
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

static loc_t here(const parser_t* p) {
    const token_t* t = cur(p);
    return loc_make(p->file, t->line, t->col);
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

// ---- diagnostics ----------------------------------------------------------

// Reports `text` at `loc` and abandons the file: a syntax error stops the
// compilation of that file after one diagnostic (D14.2). A speculative parse
// reports nothing, so a rewind leaves the diagnostics untouched.
static void report(parser_t* p, loc_t loc, const char* text) {
    if (p->spec == 0 && !p->reported) {
        diag_error(loc, text);
        p->reported = true;
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

// Consumes an identifier and yields its text, or reports and yields the zero
// view.
static bool expect_ident(parser_t* p, str_t* out) {
    if (!at(p, TOK_IDENT)) {
        error_expected(p, "an identifier");
        return false;
    }
    *out = cur(p)->text;
    bump(p);
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

// Enters a nested construct, or reports the limit of D2.11. It does not
// count on failure, so a caller that returns at once leaves the depth
// balanced.
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
// rewinds.
static spec_state_t spec_begin(parser_t* p) {
    if (p->failed) {
        fatal_internal("parser: a speculative parse after a syntax error");
    }
    spec_state_t s;
    s.pos = p->pos;
    s.depth = p->depth;
    s.failed = p->failed;
    p->spec++;
    return s;
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

// The primitive a keyword names (D3.1); `void` is a type kind of its own.
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
// element it qualifies (D5.3, D17.2), so no type, declaration or statement
// begins with one.
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

// Parses the `[own] [mut]` of one type position. Each storage level has
// exactly one position, so a doubled marker does not parse, and an `own`
// precedes the `mut` of its position (D5.3, D17.2). `own_error` is the
// message for a position that takes no `own`, or NULL where one is legal.
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
// never carries one (D5.3: `i32 mut[4]` is an error).
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

// fn_type = "fn" return_type "(" [ type { "," type } ] ")" (D3.10), with the
// `fn` and the return type already parsed.
static ast_node_t* parse_fn_type_params(parser_t* p, loc_t loc, ast_node_t* ret) {
    ast_node_t* n = node_at(p, AST_TYPE_FN, loc);
    n->a = ret;
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
    return n;
}

// return_type = type | "void" | "noreturn" (D8.5); `void` and `noreturn` are
// base types of their own.
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
        return n;
    }
    switch (kind(p)) {
    case TOK_KW_STRING:
        bump(p);
        return node_at(p, AST_TYPE_STRING, loc);
    case TOK_KW_VOID:
        bump(p);
        return node_at(p, AST_TYPE_VOID, loc);
    case TOK_KW_NORETURN:
        if (!allow_noreturn) {
            error_expected(p, "a type");
            return NULL;
        }
        bump(p);
        return node_at(p, AST_TYPE_NORETURN, loc);
    case TOK_KW_FN: {
        bump(p);
        ast_node_t* ret = parse_return_type(p);
        if (ret == NULL) {
            return NULL;
        }
        return parse_fn_type_params(p, loc, ret);
    }
    case TOK_IDENT: {
        // qualified_name = identifier [ "." identifier ] (D9.4); resolution
        // decides whether the first part is a module.
        ast_node_t* n = node_at(p, AST_TYPE_NAME, loc);
        n->name = cur(p)->text;
        bump(p);
        if (at(p, TOK_DOT) && peek_kind(p, 1) == TOK_IDENT) {
            const loc_t second = here(p);
            bump(p);
            ast_node_t* tail = node_at(p, AST_IDENT, second);
            tail->name = cur(p)->text;
            bump(p);
            n->a = tail;
        }
        return n;
    }
    default:
        error_expected(p, "a type");
        return NULL;
    }
}

// An `own` marks a reference, so after a base type it is legal only on
// `string`, the reference with no suffix (D3.7, D17.2).
static const char* base_own_error(const ast_node_t* base) {
    if (base->kind == AST_TYPE_STRING) {
        return NULL;
    }
    return "an own marks a reference: write it after a '*' or an '@', or on a string";
}

// Adds one suffix to `t` and counts it against the limit of D2.11, which
// covers type suffixes.
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

// A group of reference suffixes, `("*" | "@") [own] [mut]` each (D3.6: they
// apply to everything to their left, so a group reads inside-out).
// `array_may_follow` is true for the group before the fixed-array group,
// where a `mut` on the last position would mark the array's elements.
static bool parse_ref_suffixes(parser_t* p, ast_node_t* t, bool array_may_follow) {
    while (at(p, TOK_STAR) || at(p, TOK_AT)) {
        const loc_t loc = here(p);
        ast_node_t* s = node_at(p, AST_TYPE_SUFFIX, loc);
        s->op = at(p, TOK_STAR) ? (int32_t)SUFFIX_PTR : (int32_t)SUFFIX_SLICE;
        bump(p);
        markers_t m;
        if (!parse_markers(p, &m, NULL)) {
            return false;
        }
        s->flags = m.flags;
        if (array_may_follow && !check_mut_before_array(p, &m)) {
            return false;
        }
        if (!push_suffix(p, t, s)) {
            return false;
        }
    }
    return true;
}

// The fixed-array group, `"[" const_expr "]" [mut]` each (D3.4, D3.6: the
// group reads outside-in like C declarators). `marked` is false for the
// dimensions of an array literal's type and of an allocated type, which the
// grammar writes without any marker (grammar.md 6).
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
        if (!push_suffix(p, t, s)) {
            return false;
        }
    }
    return true;
}

// The C bootstrap has at most one array or slice level in one written type
// (toolchain.md 7.3): a second one is a nested aggregate its layout and
// codegen do not do.
static bool check_one_aggregate_level(parser_t* p, const ast_node_t* t) {
    uint64_t arrays = 0;
    uint64_t slices = 0;
    uint64_t first_extra = 0;
    for (uint64_t i = 0; i < ast_len(t); i++) {
        const ast_node_t* s = ast_child(t, i);
        if ((suffix_kind_t)s->op == SUFFIX_ARRAY) {
            arrays++;
        } else if ((suffix_kind_t)s->op == SUFFIX_SLICE) {
            slices++;
        } else {
            continue;
        }
        if (arrays + slices == 2) {
            first_extra = i;
        }
    }
    if (arrays + slices < 2) {
        return true;
    }
    const char* feature = "slices of slices";
    if (arrays >= 2) {
        feature = "multi-dimensional arrays";
    } else if (slices == 1 && (suffix_kind_t)ast_child(t, first_extra)->op == SUFFIX_SLICE) {
        feature = "slices of arrays";
    } else if (slices == 1) {
        feature = "arrays of slices";
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
    // No array suffix follows a trailing reference suffix (D3.6).
    if (at(p, TOK_LBRACKET)) {
        error_here(p, "no array suffix follows a reference suffix: wrap the array in a struct");
        return NULL;
    }
    if (!check_one_aggregate_level(p, t)) {
        return NULL;
    }
    return t;
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

// array_type = base_type { ref_suffix } "[" const_expr "]" { "[" const_expr
// "]" } (grammar.md 6): the type of an array literal, which has no base
// marker, no marker on a dimension and no trailing reference suffix (D6.5).
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
    return t;
}

// Inside `new(...)` a `mut` never parses, an `own` follows only a `*` of the
// element type and a slice is asked for with a count, not with an `@`
// (D10.2, D17.3).
static bool check_alloc_marker(parser_t* p) {
    if (at(p, TOK_KW_MUT)) {
        error_here(p, "a mut does not parse inside new: new allocates writable storage");
        return false;
    }
    if (at(p, TOK_KW_OWN)) {
        error_here(p, "inside new an own follows a '*' of the element type");
        return false;
    }
    if (at(p, TOK_AT)) {
        error_here(p, "a slice suffix does not parse inside new: write new(T, n)");
        return false;
    }
    return true;
}

// alloc_type = base_type { "*" [ "own" ] } { "[" const_expr "]" }
// (grammar.md 6).
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
        while (t != NULL && check_alloc_marker(p) && at(p, TOK_STAR)) {
            ast_node_t* s = node_at(p, AST_TYPE_SUFFIX, here(p));
            s->op = (int32_t)SUFFIX_PTR;
            bump(p);
            if (at(p, TOK_KW_OWN)) {
                s->flags = AST_FLAG_OWN;
                bump(p);
            }
            if (!push_suffix(p, t, s)) {
                t = NULL;
            }
        }
        if (p->failed) {
            t = NULL;
        }
        if (t != NULL && (!parse_array_suffixes(p, t, false) || !check_alloc_marker(p) ||
                          !check_one_aggregate_level(p, t))) {
            t = NULL;
        }
    }
    leave(p);
    return t;
}

// ---- expressions (grammar.md 6) -------------------------------------------

// The precedence of a binary operator, PREC_NONE when `k` is not one; higher
// binds tighter (D6.1). All of them are left-associative.
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

// cast_expr = "cast" "(" expr "," type ")" (D6.4).
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
    return n;
}

// sizeof_expr = "sizeof" "(" type ")" (D3.15).
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
    return n;
}

// new_expr = "new" "(" alloc_type [ "," expr ] ")": one `T` without a count,
// a slice of `n` elements with one (D10.2).
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
    return n;
}

// struct_literal = qualified_name brace_init (D6.5); grammar.md 7.2: an
// identifier, optionally `. identifier`, directly followed by `{`.
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
    return n;
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
    return n;
}

static ast_node_t* parse_primary(parser_t* p) {
    const loc_t loc = here(p);
    ast_node_t* n = NULL;
    switch (kind(p)) {
    case TOK_INT:
        n = node_at(p, AST_INT, loc);
        n->ival = cur(p)->ival;
        bump(p);
        return n;
    case TOK_FLOAT:
        // Float literals are outside the C bootstrap's subset (D2.6).
        if (unsupported(p, loc, "float literals")) {
            return NULL;
        }
        n = node_at(p, AST_FLOAT, loc);
        n->name = cur(p)->text;
        bump(p);
        return n;
    case TOK_CHAR:
        n = node_at(p, AST_CHAR, loc);
        n->ival = cur(p)->ival;
        bump(p);
        return n;
    case TOK_STRING:
        n = node_at(p, AST_STRING, loc);
        n->name = cur(p)->text;
        bump(p);
        return n;
    case TOK_KW_TRUE:
    case TOK_KW_FALSE:
        n = node_at(p, AST_BOOL, loc);
        n->ival = at(p, TOK_KW_TRUE) ? 1 : 0;
        bump(p);
        return n;
    case TOK_KW_NULL:
        bump(p);
        return node_at(p, AST_NULL, loc);
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
        n->name = cur(p)->text;
        bump(p);
        return n;
    default:
        break;
    }
    if (starts_base_type(kind(p)) && speculate_array_literal(p)) {
        return parse_array_literal(p);
    }
    error_expected(p, "an expression");
    return NULL;
}

// postfix = call | index | slice | "." identifier | "->" identifier.
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
    return n;
}

// index (D6.8) and the four slicing forms (D6.9), told apart by the `..`.
static ast_node_t* parse_index_or_slice(parser_t* p, ast_node_t* operand) {
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
        n = node_at(p, AST_SLICE, loc);
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
    return n;
}

// `.f` and `->f`, the second required through a pointer (D6.10).
static ast_node_t* parse_member(parser_t* p, ast_node_t* operand) {
    const loc_t loc = here(p);
    const bool arrow = at(p, TOK_ARROW);
    bump(p);
    ast_node_t* n = node_at(p, arrow ? AST_ARROW : AST_FIELD, loc);
    n->a = operand;
    if (!expect_ident(p, &n->name)) {
        return NULL;
    }
    return n;
}

static ast_node_t* parse_postfix(parser_t* p) {
    ast_node_t* e = parse_primary(p);
    while (e != NULL) {
        switch (kind(p)) {
        case TOK_LPAREN:
            e = parse_call(p, e);
            break;
        case TOK_LBRACKET:
            e = parse_index_or_slice(p, e);
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
    return n;
}

// The binary levels of D6.1, climbed by precedence; every level is
// left-associative, so the right operand starts one level up.
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
        left = n;
    }
    return NULL;
}

// ternary_expr = or_expr [ "?" expr ":" ternary_expr ], right-associative
// (D6.1); `?:` is outside the C bootstrap's subset.
static ast_node_t* parse_ternary(parser_t* p) {
    ast_node_t* cond = parse_binary(p, PREC_OR);
    if (cond == NULL || !at(p, TOK_QUESTION)) {
        return cond;
    }
    const loc_t loc = here(p);
    if (unsupported(p, loc, "?:")) {
        return NULL;
    }
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
    return n;
}

static ast_node_t* parse_expr(parser_t* p) {
    return parse_ternary(p);
}

// brace_init = "{" [ init_list ] "}", positional or designated and never
// both, with a trailing comma allowed (D6.5).
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
                ok = expect_ident(p, &member->name) && expect(p, TOK_ASSIGN, "'='");
                if (ok) {
                    member->a = parse_initializer(p);
                    ok = member->a != NULL;
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
    return n;
}

// initializer = expr | brace_init (grammar.md 3).
static ast_node_t* parse_initializer(parser_t* p) {
    if (at(p, TOK_LBRACE)) {
        return parse_brace_init(p);
    }
    return parse_expr(p);
}

// ---- statements (grammar.md 5) --------------------------------------------

// var_decl = type identifier "=" initializer ";" (D7.1: one declarator, the
// initializer mandatory).
static ast_node_t* parse_var_decl(parser_t* p, bool want_semi) {
    const loc_t loc = here(p);
    ast_node_t* n = node_at(p, AST_VAR_DECL, loc);
    n->a = parse_type(p, false);
    if (n->a == NULL || !expect_ident(p, &n->name) || !expect(p, TOK_ASSIGN, "'='")) {
        return NULL;
    }
    n->b = parse_initializer(p);
    if (n->b == NULL) {
        return NULL;
    }
    if (want_semi && !expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return n;
}

// assign_head, incdec_head or call_expr (D7.2, D7.3): the target is a
// postfix expression or a unary `*` (grammar.md 5).
static ast_node_t* parse_simple_head(parser_t* p) {
    const loc_t loc = here(p);
    ast_node_t* e = at(p, TOK_STAR) ? parse_unary(p) : parse_postfix(p);
    if (e == NULL) {
        return NULL;
    }
    if (is_assign_op(kind(p))) {
        ast_node_t* n = node_at(p, AST_ASSIGN, loc);
        n->op = (int32_t)kind(p);
        n->a = e;
        bump(p);
        n->b = parse_expr(p);
        if (n->b == NULL) {
            return NULL;
        }
        return n;
    }
    if (at(p, TOK_PLUS_PLUS) || at(p, TOK_MINUS_MINUS)) {
        ast_node_t* n = node_at(p, AST_INCDEC, loc);
        n->op = (int32_t)kind(p);
        n->a = e;
        bump(p);
        return n;
    }
    // Expression statements are calls only (D7.3).
    if (e->kind == AST_CALL) {
        ast_node_t* n = node_at(p, AST_CALL_STMT, loc);
        n->a = e;
        return n;
    }
    error_expected(p, "an assignment, an increment or a call");
    return NULL;
}

static ast_node_t* parse_simple_statement(parser_t* p) {
    ast_node_t* n = parse_simple_head(p);
    if (n == NULL || !expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return n;
}

// The condition of `if` and `while`, always parenthesized (D7.4).
static ast_node_t* parse_condition(parser_t* p) {
    return parse_paren_expr(p);
}

// if_stmt = "if" "(" expr ")" block { "else" "if" "(" expr ")" block }
// [ "else" block ] (D7.4: braces on every branch). The chain is built
// iteratively, so a long one does not recurse.
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
            return first;
        }
        bump(p);
        if (at(p, TOK_KW_IF)) {
            continue;
        }
        prev->c = parse_block(p);
        if (prev->c == NULL) {
            return NULL;
        }
        return first;
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
    return n;
}

// do_stmt = "do" block "while" "(" expr ")" ";" (D7.5); `do`-`while` is
// outside the C bootstrap's subset.
static ast_node_t* parse_do(parser_t* p) {
    const loc_t loc = here(p);
    if (unsupported(p, loc, "do-while")) {
        return NULL;
    }
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
    return n;
}

// grammar.md 7.3: after `for (`, a speculative parse of `type identifier`
// decides between the range loop, a declaration init and everything else.
static int speculate_for_form(parser_t* p) {
    if (at(p, TOK_SEMI)) {
        return FOR_PLAIN;
    }
    spec_state_t s = spec_begin(p);
    int form = FOR_PLAIN;
    ast_node_t* t = parse_type(p, false);
    if (t != NULL && !p->failed && at(p, TOK_IDENT)) {
        bump(p);
        if (at(p, TOK_COLON)) {
            form = FOR_RANGE;
        } else if (at(p, TOK_ASSIGN)) {
            form = FOR_DECL;
        }
    }
    spec_rewind(p, s);
    return form;
}

// range_for_stmt = "for" "(" type identifier ":" expr ")" block (D7.5).
static ast_node_t* parse_range_for(parser_t* p, loc_t loc) {
    ast_node_t* n = node_at(p, AST_RANGE_FOR, loc);
    n->a = parse_type(p, false);
    if (n->a == NULL || !expect_ident(p, &n->name) || !expect(p, TOK_COLON, "':'")) {
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
    return n;
}

// for_stmt = "for" "(" [ for_init ] ";" [ expr ] ";" [ for_step ] ")" block
// (D7.5): `for (;;)` is legal and every part may be empty.
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
    return n;
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

// switch_stmt = "switch" "(" expr ")" "{" { case_clause } "}" (D7.6); each
// case body is an implicit block scope.
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
    ast_node_t* body = node_at(p, AST_BLOCK, here(p));
    while (!at(p, TOK_KW_CASE) && !at(p, TOK_KW_DEFAULT) && !at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        ast_node_t* s = parse_statement(p);
        if (s == NULL) {
            return NULL;
        }
        ast_push(body, s);
    }
    n->a = body;
    return n;
}

static ast_node_t* parse_switch(parser_t* p) {
    ast_node_t* n = node_at(p, AST_SWITCH, here(p));
    bump(p);
    n->a = parse_paren_expr(p);
    if (n->a == NULL || !expect(p, TOK_LBRACE, "'{'") || !enter(p)) {
        return NULL;
    }
    bool ok = true;
    while (ok && (at(p, TOK_KW_CASE) || at(p, TOK_KW_DEFAULT))) {
        ast_node_t* clause = parse_case(p);
        ok = clause != NULL;
        if (ok) {
            ast_push(n, clause);
        }
    }
    leave(p);
    if (!ok || !expect(p, TOK_RBRACE, "'}'")) {
        return NULL;
    }
    return n;
}

// defer_stmt = "defer" ( assign_stmt | incdec_stmt | call_stmt | block )
// (D7.8).
static ast_node_t* parse_defer(parser_t* p) {
    ast_node_t* n = node_at(p, AST_DEFER, here(p));
    bump(p);
    n->a = at(p, TOK_LBRACE) ? parse_block(p) : parse_simple_statement(p);
    if (n->a == NULL) {
        return NULL;
    }
    return n;
}

// return_stmt = "return" [ expr ] ";" (D7.11).
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
    return n;
}

static ast_node_t* parse_break_or_continue(parser_t* p) {
    ast_node_t* n = node_at(p, at(p, TOK_KW_BREAK) ? AST_BREAK : AST_CONTINUE, here(p));
    bump(p);
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return n;
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
    spec_state_t s = spec_begin(p);
    ast_node_t* t = parse_type(p, false);
    const bool decl = t != NULL && !p->failed && at(p, TOK_IDENT);
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
        // The empty statement is an error (D7.3).
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

static ast_node_t* parse_block(parser_t* p) {
    const loc_t loc = here(p);
    if (!expect(p, TOK_LBRACE, "'{'") || !enter(p)) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_BLOCK, loc);
    bool ok = true;
    while (ok && !at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        ast_node_t* s = parse_statement(p);
        ok = s != NULL;
        if (ok) {
            ast_push(n, s);
        }
    }
    leave(p);
    if (!ok || !expect(p, TOK_RBRACE, "'}'")) {
        return NULL;
    }
    return n;
}

// ---- declarations (grammar.md 2, 3) ---------------------------------------

// param_list = param { "," param } with param = type identifier; no trailing
// comma in a parameter list (D6.5).
static bool parse_params(parser_t* p, ast_node_t* fn) {
    if (!expect(p, TOK_LPAREN, "'('")) {
        return false;
    }
    if (!at(p, TOK_RPAREN)) {
        for (;;) {
            ast_node_t* param = node_at(p, AST_PARAM, here(p));
            param->a = parse_type(p, false);
            if (param->a == NULL || !expect_ident(p, &param->name)) {
                return false;
            }
            ast_push(fn, param);
            if (!at(p, TOK_COMMA)) {
                break;
            }
            bump(p);
        }
    }
    return expect(p, TOK_RPAREN, "')'");
}

// fn_decl = "fn" return_type identifier "(" [ param_list ] ")" block (D8.1),
// with the `fn` and the return type already parsed.
static ast_node_t* parse_fn_decl(parser_t* p, loc_t loc, ast_node_t* ret) {
    ast_node_t* n = node_at(p, AST_FN_DECL, loc);
    n->a = ret;
    if (!expect_ident(p, &n->name) || !parse_params(p, n)) {
        return NULL;
    }
    n->b = parse_block(p);
    if (n->b == NULL) {
        return NULL;
    }
    return n;
}

// At the top level a `fn` opens a function definition (grammar.md 7). When
// the return type is followed by `(` rather than by a name, the `fn` is the
// base of a function type and the declaration is a global of that type
// (D3.10, D7.10).
static ast_node_t* parse_fn_top_decl(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    ast_node_t* ret = parse_return_type(p);
    if (ret == NULL) {
        return NULL;
    }
    if (at(p, TOK_IDENT)) {
        return parse_fn_decl(p, loc, ret);
    }
    ast_node_t* base = parse_fn_type_params(p, loc, ret);
    if (base == NULL) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_VAR_DECL, loc);
    n->a = parse_type_after_base(p, loc, base);
    if (n->a == NULL || !expect_ident(p, &n->name) || !expect(p, TOK_ASSIGN, "'='")) {
        return NULL;
    }
    n->b = parse_initializer(p);
    if (n->b == NULL || !expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return n;
}

// extern_decl = "extern" "fn" return_type identifier "(" [ param_list ] ")"
// ";" (D9.8): a declaration with no body.
static ast_node_t* parse_extern_decl(parser_t* p) {
    const loc_t loc = here(p);
    bump(p);
    if (!expect(p, TOK_KW_FN, "'fn'")) {
        return NULL;
    }
    ast_node_t* n = node_at(p, AST_FN_DECL, loc);
    n->flags = AST_FLAG_EXTERN;
    n->a = parse_return_type(p);
    if (n->a == NULL || !expect_ident(p, &n->name) || !parse_params(p, n)) {
        return NULL;
    }
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return n;
}

// struct_decl = "struct" identifier "{" field { field } "}" (D3.8): no
// trailing semicolon and at least one field.
static ast_node_t* parse_struct_decl(parser_t* p) {
    ast_node_t* n = node_at(p, AST_STRUCT_DECL, here(p));
    bump(p);
    if (!expect_ident(p, &n->name) || !expect(p, TOK_LBRACE, "'{'") || !enter(p)) {
        return NULL;
    }
    bool ok = true;
    if (at(p, TOK_RBRACE)) {
        error_here(p, "a struct has at least one field");
        ok = false;
    }
    while (ok && !at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        ast_node_t* field = node_at(p, AST_FIELD_DECL, here(p));
        field->a = parse_type(p, false);
        ok = field->a != NULL && expect_ident(p, &field->name) && expect(p, TOK_SEMI, "';'");
        if (ok) {
            ast_push(n, field);
        }
    }
    leave(p);
    if (!ok || !expect(p, TOK_RBRACE, "'}'")) {
        return NULL;
    }
    return n;
}

// enum_decl = "enum" identifier "{" enum_member { "," enum_member } [ "," ]
// "}" (D3.9).
static ast_node_t* parse_enum_decl(parser_t* p) {
    ast_node_t* n = node_at(p, AST_ENUM_DECL, here(p));
    bump(p);
    if (!expect_ident(p, &n->name) || !expect(p, TOK_LBRACE, "'{'") || !enter(p)) {
        return NULL;
    }
    bool ok = true;
    if (at(p, TOK_RBRACE)) {
        error_here(p, "an enum has at least one member");
        ok = false;
    }
    while (ok) {
        ast_node_t* member = node_at(p, AST_ENUM_MEMBER, here(p));
        ok = expect_ident(p, &member->name);
        if (ok && at(p, TOK_ASSIGN)) {
            bump(p);
            member->a = parse_expr(p);
            ok = member->a != NULL;
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
    leave(p);
    if (!ok || !expect(p, TOK_RBRACE, "'}'")) {
        return NULL;
    }
    return n;
}

// import_decl = "import" import_path [ "as" identifier ] ";" or "import"
// import_path "::" "{" import_item { "," import_item } [ "," ] "}" ";"
// (D9.3); whether the last segment names a module or a symbol is
// resolution's business.
static bool parse_import_items(parser_t* p, ast_node_t* n) {
    bump(p);
    for (;;) {
        ast_node_t* item = node_at(p, AST_IMPORT_ITEM, here(p));
        if (!expect_ident(p, &item->name)) {
            return false;
        }
        if (at(p, TOK_KW_AS)) {
            const loc_t loc = here(p);
            bump(p);
            item->a = node_at(p, AST_IDENT, loc);
            if (!expect_ident(p, &item->a->name)) {
                return false;
            }
        }
        ast_push(n, item);
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

static ast_node_t* parse_import(parser_t* p) {
    ast_node_t* n = node_at(p, AST_IMPORT, here(p));
    bump(p);
    ast_node_t* path = node_at(p, AST_PATH, here(p));
    n->a = path;
    for (;;) {
        ast_node_t* segment = node_at(p, AST_IDENT, here(p));
        if (!expect_ident(p, &segment->name)) {
            return NULL;
        }
        ast_push(path, segment);
        if (!at(p, TOK_COLON_COLON)) {
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
        if (!expect_ident(p, &n->b->name)) {
            return NULL;
        }
    }
    if (!expect(p, TOK_SEMI, "';'")) {
        return NULL;
    }
    return n;
}

// top_decl = fn_decl | extern_decl | struct_decl | enum_decl | global_decl
// (grammar.md 2); a global is a var_decl whose initializer is constant
// (D7.10).
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
        // Imports come first in a file (D9.3).
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
    p.reported = false;
    sb_init(&p.msg);

    ast_node_t* mod = ast_new(arena, AST_MODULE, loc_make(file, 1, 1));
    // module = { import_decl } { top_decl }: the imports come first (D9.3).
    while (!p.failed && at(&p, TOK_KW_IMPORT)) {
        ast_node_t* imp = parse_import(&p);
        if (imp == NULL) {
            break;
        }
        ast_push(mod, imp);
    }
    while (!p.failed && !at(&p, TOK_EOF)) {
        ast_node_t* decl = parse_top_decl(&p);
        if (decl == NULL) {
            break;
        }
        ast_push(mod, decl);
    }
    const bool failed = p.failed;
    sb_free(&p.msg);
    return failed ? NULL : mod;
}
