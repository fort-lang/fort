// The environment the parser suites share: lexing and parsing a text as
// `t.ft` with the diagnostics captured, and the S-expression of the tree or
// of the one part of it a test is about (ast_dump.h).
//
// A type, an expression and a statement are each tested through parse_module,
// the parser's only entry point, by wrapping them in the smallest declaration
// that carries them: `<type> x = 0;`, `i32 x = <expr>;` and
// `fn void f() { <stmt> }`.
#ifndef FORT_TEST_PARSER_HELPERS_H
#define FORT_TEST_PARSER_HELPERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "ast_dump.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "str.h"

#include "test.h"

// The results of the last parse: the tokens and the pool they were lexed
// with (both keep the names in the tree alive), the arena, the captured
// diagnostics and the buffer the dumps are printed into.
static tokvec_t pt_toks;
static str_pool_t pt_pool;
static ast_arena_t pt_arena;
static sb_t pt_diags;
static sb_t pt_out;
static sb_t pt_src;
static bool pt_ready = false;

static inline void pt_init(void) {
    if (pt_ready) {
        return;
    }
    tokvec_init(&pt_toks);
    str_pool_init(&pt_pool);
    ast_arena_init(&pt_arena);
    sb_init(&pt_diags);
    sb_init(&pt_out);
    sb_init(&pt_src);
    diag_capture(&pt_diags);
    pt_ready = true;
}

// Releases the results of the last parse and restores stderr.
static inline void parse_done(void) {
    if (!pt_ready) {
        return;
    }
    diag_capture(NULL);
    tokvec_free(&pt_toks);
    str_pool_free(&pt_pool);
    ast_arena_free(&pt_arena);
    sb_free(&pt_diags);
    sb_free(&pt_out);
    sb_free(&pt_src);
    pt_ready = false;
}

// Lexes and parses `src` as t.ft with the diagnostics captured; NULL when the
// lexer or the parser reported one. The previous parse's tokens, strings and
// nodes are released first, so every tree lives until the next call.
static inline ast_node_t* parse_text(const char* src) {
    pt_init();
    sb_clear(&pt_diags);
    tokvec_free(&pt_toks);
    str_pool_free(&pt_pool);
    ast_arena_free(&pt_arena);
    diag_reset();
    if (!lex_file("t.ft", str_from_cstr(src), &pt_pool, &pt_toks)) {
        return NULL;
    }
    return parse_module("t.ft", pt_toks.items, pt_toks.len, &pt_arena);
}

// The captured diagnostics of the last parse, one line each.
static inline const char* parse_diags(void) {
    return sb_cstr(&pt_diags);
}

// The S-expression of `n`, valid until the next dump.
static inline const char* dumped(const ast_node_t* n) {
    sb_clear(&pt_out);
    ast_dump(n, &pt_out);
    return sb_cstr(&pt_out);
}

// The position of `n` as `line:col`, valid until the next dump.
static inline const char* loc_of(const ast_node_t* n) {
    sb_clear(&pt_out);
    if (n == NULL) {
        sb_append(&pt_out, "nil");
        return sb_cstr(&pt_out);
    }
    msg_uint(&pt_out, n->loc.line);
    sb_push(&pt_out, ':');
    msg_uint(&pt_out, n->loc.col);
    return sb_cstr(&pt_out);
}

// The S-expression of the whole module, or the diagnostics when the parse
// failed, so that a failing assertion shows why.
static inline const char* parse_dump(const char* src) {
    const ast_node_t* mod = parse_text(src);
    if (mod == NULL) {
        return parse_diags();
    }
    return dumped(mod);
}

// The `i`-th declaration of the module, or NULL when the parse failed or the
// module has no such declaration.
static inline ast_node_t* pt_decl(const ast_node_t* mod, uint64_t i) {
    if (mod == NULL || ast_len(mod) <= i) {
        return NULL;
    }
    return ast_child(mod, i);
}

// The source `prefix<middle>suffix`, held until the next call.
static inline const char* pt_wrap(const char* prefix, const char* middle, const char* suffix) {
    pt_init();
    sb_clear(&pt_src);
    sb_append(&pt_src, prefix);
    sb_append(&pt_src, middle);
    sb_append(&pt_src, suffix);
    return sb_cstr(&pt_src);
}

// The type of `<type> x = 0;`, as the parser reads it (grammar.md 4).
static inline const char* dump_type(const char* type_src) {
    const ast_node_t* mod = parse_text(pt_wrap("", type_src, " x = 0;"));
    const ast_node_t* decl = pt_decl(mod, 0);
    if (decl == NULL) {
        return parse_diags();
    }
    return dumped(decl->a);
}

// The initializer of `i32 x = <expr>;` (grammar.md 6).
static inline const char* dump_expr(const char* expr_src) {
    const ast_node_t* mod = parse_text(pt_wrap("i32 x = ", expr_src, ";"));
    const ast_node_t* decl = pt_decl(mod, 0);
    if (decl == NULL) {
        return parse_diags();
    }
    return dumped(decl->b);
}

// The first statement of `fn void f() { <stmt> }` (grammar.md 5).
static inline const char* dump_stmt(const char* stmt_src) {
    const ast_node_t* mod = parse_text(pt_wrap("fn void f() {\n", stmt_src, "\n}"));
    const ast_node_t* fn = pt_decl(mod, 0);
    if (fn == NULL || fn->b == NULL || ast_len(fn->b) == 0) {
        return parse_diags();
    }
    return dumped(ast_child(fn->b, 0));
}

// The one diagnostic a source that must not parse produces (D14.2), or the
// tree when it parsed after all, so that a failing assertion shows what the
// parser accepted.
static inline const char* parse_fails(const char* src) {
    const ast_node_t* mod = parse_text(src);
    if (mod != NULL) {
        return dumped(mod);
    }
    return parse_diags();
}

// The diagnostic of a type, an expression or a statement that must not
// parse, in the same wrappers the dumps use.
static inline const char* type_fails(const char* type_src) {
    return parse_fails(pt_wrap("", type_src, " x = 0;"));
}

static inline const char* expr_fails(const char* expr_src) {
    return parse_fails(pt_wrap("i32 x = ", expr_src, ";"));
}

static inline const char* stmt_fails(const char* stmt_src) {
    return parse_fails(pt_wrap("fn void f() {\n", stmt_src, "\n}"));
}

#endif
