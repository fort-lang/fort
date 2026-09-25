// Provides shared lexer helpers.
// They lex text, capture diagnostics, and assert token and diagnostic results.
#ifndef FORT_TEST_LEXER_HELPERS_H
#define FORT_TEST_LEXER_HELPERS_H

#include <stdint.h>
#include <string.h>

#include "diag.h"
#include "lexer.h"

#include "test.h"

// The results of the last lex() call: tokens, the pool the strings were decoded into, and the
// captured diagnostics.
static tokvec_t toks;
static str_pool_t pool;
static sb_t sink;
static bool capturing = false;

// Lexes `len` bytes of `src` as file t.ft with diagnostics captured; the previous results are
// released first.
static inline bool lex_bytes(const char* src, uint64_t len) {
    if (!capturing) {
        sb_init(&sink);
        str_pool_init(&pool);
        tokvec_init(&toks);
        diag_capture(&sink);
        capturing = true;
    }
    sb_clear(&sink);
    str_pool_free(&pool);
    tokvec_free(&toks);
    diag_reset();
    return lex_file("t.ft", str_from_range(src, len), &pool, &toks);
}

static inline bool lex(const char* src) {
    return lex_bytes(src, strlen(src));
}

// Releases the results of the last lex() call and restores stderr.
static inline void lex_done(void) {
    if (capturing) {
        diag_capture(NULL);
        sb_free(&sink);
        str_pool_free(&pool);
        tokvec_free(&toks);
        capturing = false;
    }
}

static inline const token_t* tok(uint64_t i) {
    if (i >= toks.len) {
        fatal_internal("lexer_test: token index out of range");
    }
    return &toks.items[i];
}

// Returns the captured diagnostics.
// The next lex or `lex_done` invalidates the result.
static inline const char* captured(void) {
    return sb_cstr(&sink);
}

static inline bool text_is(uint64_t i, const char* expected) {
    return str_eq(tok(i)->text, str_from_cstr(expected));
}

// The kind, the text, the position and the byte range of token i.
#define ASSERT_TOK_KIND(i, k) TEST_ASSERT_EQ_STR(tok_kind_name(tok(i)->kind), tok_kind_name(k))
#define ASSERT_TOK_TEXT(i, s) TEST_ASSERT_TRUE(text_is(i, s))
#define ASSERT_TOK_POS(i, l, c)                                                                    \
    do {                                                                                           \
        TEST_ASSERT_EQ_INT64((int64_t)tok(i)->line, (int64_t)(l));                                 \
        TEST_ASSERT_EQ_INT64((int64_t)tok(i)->col, (int64_t)(c));                                  \
    } while (0)
#define ASSERT_TOK_RANGE(i, o, n)                                                                  \
    do {                                                                                           \
        TEST_ASSERT_EQ_UINT64(tok(i)->off, (uint64_t)(o));                                         \
        TEST_ASSERT_EQ_UINT64(tok(i)->len, (uint64_t)(n));                                         \
    } while (0)

// lex() failed with exactly the diagnostic `line` (with its newline) and
// still lexed the file whole, so the tokens end in TOK_EOF.
#define ASSERT_LEX_ERROR(src, line)                                                                \
    do {                                                                                           \
        TEST_ASSERT_FALSE(lex(src));                                                               \
        TEST_ASSERT_EQ_STR(captured(), line);                                                      \
        TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);                                          \
        ASSERT_ENDS_IN_EOF();                                                                      \
    } while (0)

// lex() failed with exactly the diagnostics `lines` (each with its newline),
// `n` of them, one per line of the source at most.
#define ASSERT_LEX_ERRORS(src, n, lines)                                                           \
    do {                                                                                           \
        TEST_ASSERT_FALSE(lex(src));                                                               \
        TEST_ASSERT_EQ_STR(captured(), lines);                                                     \
        TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)(n));                                        \
        ASSERT_ENDS_IN_EOF();                                                                      \
    } while (0)

// The tokens of the last lex() end in TOK_EOF.
#define ASSERT_ENDS_IN_EOF()                                                                       \
    do {                                                                                           \
        TEST_ASSERT_TRUE(toks.len > 0);                                                            \
        ASSERT_TOK_KIND(toks.len - 1, TOK_EOF);                                                    \
    } while (0)

// The last lex() left `n` tokens before its TOK_EOF: what a resync kept.
#define ASSERT_TOK_COUNT(n)                                                                        \
    do {                                                                                           \
        ASSERT_ENDS_IN_EOF();                                                                      \
        TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)(n) + 1);                                        \
    } while (0)

// lex() succeeded with `n` tokens before the TOK_EOF and no diagnostic.
#define ASSERT_LEX_OK(src, n)                                                                      \
    do {                                                                                           \
        TEST_ASSERT_TRUE(lex(src));                                                                \
        TEST_ASSERT_EQ_STR(captured(), "");                                                        \
        TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)(n) + 1);                                        \
        ASSERT_TOK_KIND((n), TOK_EOF);                                                             \
    } while (0)

#endif
