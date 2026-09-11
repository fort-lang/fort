// The tree invariants of a parsed range (D20.4), checked over a corpus that
// covers every node kind the bootstrap builds: every node ends at or after it
// starts, a name range sits inside the node that carries it, and a child's
// range sits inside its parent's, the one exception being the left operand of
// a node named after the operator that follows it (toolchain.md 4).
//
// A missed end shows up here as a child that leaves its parent, which is what
// makes this suite the safety net for the parser's `finish` calls.
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "diag.h"
#include "parser_helpers.h"
#include "str.h"

#include "common.h"
#include "test.h"

// The types, expressions, statements and whole modules the parser suites
// parse, one of each shape they cover, and the broken modules the parser
// recovers in, one of each recovery point.
static const char* const TYPES[] = {"i32",
                                    "u8",
                                    "bool",
                                    "char",
                                    "string",
                                    "node",
                                    "io.writer",
                                    "node*",
                                    "node* mut",
                                    "node* own",
                                    "string own",
                                    "i32@",
                                    "i32[4]",
                                    "i32[4] mut",
                                    "u8 mut@ own mut*",
                                    "fn i32(i32, bool)",
                                    "fn void()"};

static const char* const EXPRS[] = {"1",
                                    "'a'",
                                    "\"hi\"",
                                    "true",
                                    "false",
                                    "null",
                                    "x",
                                    "-x",
                                    "!x",
                                    "~x",
                                    "*p",
                                    "&x",
                                    "a + b * c",
                                    "a && b || c",
                                    "a == b != c",
                                    "a < b <= c",
                                    "a << 2 >> 1",
                                    "a % 3 +% 1 -% 2 *% 3",
                                    "a & b | c ^ d",
                                    "f(1, 2)",
                                    "f(g(x), h)",
                                    "v[0]",
                                    "s[1..2]",
                                    "s[..2]",
                                    "s[1..]",
                                    "s[..]",
                                    "p.f",
                                    "p->f",
                                    "p->f.g[0]",
                                    "cast(x, u8)",
                                    "sizeof(i32)",
                                    "new(node)",
                                    "new(u8, 16)",
                                    "new(node* own, 4)",
                                    "point{1, 2}",
                                    "point{.x = 1, .y = 2}",
                                    "io.point{1}",
                                    "i32[2]{1, 2}",
                                    "node*[2]{null, null}"};

static const char* const STMTS[] = {"i32 n = 1;",
                                    "i32 mut n = 1;",
                                    "n = 1;",
                                    "n += f(1);",
                                    "*p = 1;",
                                    "p->f = 1;",
                                    "n++;",
                                    "n--;",
                                    "f();",
                                    "{ f(); }",
                                    "if (c) { f(); }",
                                    "if (c) { f(); } else { g(); }",
                                    "if (c) { f(); } else if (d) { g(); } else { h(); }",
                                    "while (c) { f(); }",
                                    "while (c) { break; }",
                                    "while (c) { continue; }",
                                    "for (;;) { f(); }",
                                    "for (i32 mut i = 0; i < 4; i++) { f(); }",
                                    "for (n = 0; n < 4; n++) { f(); }",
                                    "for (i32 v : xs) { f(); }",
                                    "switch (c) { case 1: f(); case 2, 3: g(); default: h(); }",
                                    "switch (c) { case 1: }",
                                    "switch (c) { default: }",
                                    "defer f();",
                                    "defer { f(); }",
                                    "return;",
                                    "point p = {1, 2};",
                                    "point p = {.x = 1};"};

static const char* const MODULES[] = {"",
                                      "i32 N = 4;\n",
                                      "import std::io;\n"
                                      "import std::str as s;\n"
                                      "import util::text::trim;\n"
                                      "import util::text::{pad, split as cut};\n"
                                      "\n"
                                      "fn void main() {\n"
                                      "    io.println(s.dup(\"x\"));\n"
                                      "    util.text.pad(cut(trim(\"  y  \"), ' '), 4);\n"
                                      "}\n",
                                      "struct entry {\n"
                                      "    string key;\n"
                                      "    i32 value;\n"
                                      "    entry mut* own next;\n"
                                      "}\n"
                                      "\n"
                                      "struct map {\n"
                                      "    entry mut* own mut@ own buckets;\n"
                                      "    u64 count;\n"
                                      "}\n"
                                      "\n"
                                      "enum kind { ident, number = 7, symbol, end }\n"
                                      "\n"
                                      "extern fn void* malloc(u64 size);\n"
                                      "\n"
                                      "fn void put(map mut* m, string key, i32 value) {\n"
                                      "    u64 h = hash(key) % m->buckets.len;\n"
                                      "    entry mut* own e = new(entry);\n"
                                      "    e->next = move(m->buckets[h]);\n"
                                      "    m->buckets[h] = move(e);\n"
                                      "    m->count += 1;\n"
                                      "}\n",
                                      "fn noreturn die(string msg) {\n"
                                      "    panic(msg);\n"
                                      "}\n"
                                      "\n"
                                      "fn i32 all(i32@ xs) {\n"
                                      "    i32 mut total = 0;\n"
                                      "    defer log();\n"
                                      "    {\n"
                                      "        i32 mut scoped = 1;\n"
                                      "        total += scoped;\n"
                                      "    }\n"
                                      "    if (xs.len == 0) {\n"
                                      "        return 0;\n"
                                      "    } else if (xs.len == 1) {\n"
                                      "        return xs[0];\n"
                                      "    } else {\n"
                                      "        total = 0;\n"
                                      "    }\n"
                                      "    while (total < 10) {\n"
                                      "        total++;\n"
                                      "    }\n"
                                      "    for (i32 mut i = 0; i < xs.len; i++) {\n"
                                      "        if (xs[i] < 0) {\n"
                                      "            continue;\n"
                                      "        }\n"
                                      "        total += xs[i];\n"
                                      "    }\n"
                                      "    for (i32 v : xs) {\n"
                                      "        switch (v) {\n"
                                      "        case 0:\n"
                                      "            break;\n"
                                      "        default:\n"
                                      "            total += v;\n"
                                      "        }\n"
                                      "    }\n"
                                      "    return total;\n"
                                      "}\n",
                                      "fn i32(i32) chosen = pick;\n"
                                      "i32[3] mut cells = {1, 2, 3};\n"
                                      "string greeting = \"hi\";\n"
                                      "char sep = ',';\n"
                                      "bool ready = true;\n"
                                      "node* root = null;\n"};

// Modules the parser recovers in: every one reports at least one syntax error
// and still yields a tree, whose error nodes cover the skipped regions
// (D14.2), so the invariants below are checked on a recovered tree too.
static const char* const RECOVERED[] = {"i32 a = ;\ni32 b = 1;\n",
                                        "fn i32 f() {\n    x = ;\n    return 0;\n}\n",
                                        "fn i32 f() {\n    if (c) {\n        x = ;\n    }\n"
                                        "    return 0;\n}\n",
                                        "fn void f() {\n    switch (c) {\n    case 1:\n"
                                        "        x = ;\n    }\n}\n",
                                        "struct s {\n    i32 ;\n    i32 y;\n}\n",
                                        "fn void f() {\n    x = 1;\n"
                                        "fn void g() {\n    y = 2;\n}\n",
                                        "fn void f() {\n    if (c) {\n"};

// The violations found in the tree under test, one per line, and the kinds
// the corpus has reached.
static sb_t report;
static bool kind_seen[AST_KIND_COUNT];

static void append_range(sb_t* out, loc_t loc) {
    msg_uint(out, loc.line);
    sb_push(out, ':');
    msg_uint(out, loc.col);
    sb_push(out, '-');
    msg_uint(out, loc.end_line);
    sb_push(out, ':');
    msg_uint(out, loc.end_col);
}

// `binary 1:11-1:18 leaves if 2:5-2:9`, or the same without a parent.
static void note(const char* what, const ast_node_t* n, const ast_node_t* parent) {
    sb_append(&report, ast_kind_name(n->kind));
    sb_push(&report, ' ');
    append_range(&report, n->loc);
    sb_push(&report, ' ');
    sb_append(&report, what);
    if (parent != NULL) {
        sb_push(&report, ' ');
        sb_append(&report, ast_kind_name(parent->kind));
        sb_push(&report, ' ');
        append_range(&report, parent->loc);
    }
    sb_push(&report, '\n');
}

// The nodes named after the operator that follows their first child (binary,
// assignment, increment, call, index, span, field, arrow): the child starts
// before the operator the node is anchored at, so it is the one child that
// may begin before its parent (toolchain.md 4).
static bool is_infix_kind(ast_kind_t k) {
    return k == AST_BINARY || k == AST_ASSIGN || k == AST_INCDEC || k == AST_CALL ||
           k == AST_INDEX || k == AST_SPAN || k == AST_FIELD || k == AST_ARROW;
}

static void check_node(const ast_node_t* n, const ast_node_t* parent);

static void check_child(const ast_node_t* child, const ast_node_t* parent, bool left_operand) {
    if (child == NULL) {
        return;
    }
    if (!loc_ends_at_or_before(child->loc, parent->loc)) {
        note("ends after", child, parent);
    } else if (left_operand && is_infix_kind(parent->kind)) {
        // The left operand starts before the operator its parent is at.
        if (!loc_starts_at_or_before(child->loc, parent->loc)) {
            note("does not start before", child, parent);
        }
    } else if (!loc_starts_at_or_before(parent->loc, child->loc)) {
        note("starts before", child, parent);
    }
    check_node(child, parent);
}

static void check_node(const ast_node_t* n, const ast_node_t* parent) {
    kind_seen[n->kind] = true;
    if (!loc_is_ordered(n->loc)) {
        note("ends before it starts", n, parent);
    }
    if (n->name_loc.line != 0) {
        // A name range is a token inside the node that carries it (D20.4).
        if (!loc_is_ordered(n->name_loc) || !loc_starts_at_or_before(n->loc, n->name_loc) ||
            !loc_ends_at_or_before(n->name_loc, n->loc)) {
            note("has a name range outside itself", n, parent);
        }
    } else if (n->name_loc.col != 0 || n->name_loc.end_line != 0 || n->name_loc.end_col != 0) {
        note("has a half-filled name range", n, parent);
    }
    check_child(n->a, n, true);
    check_child(n->b, n, false);
    check_child(n->c, n, false);
    check_child(n->d, n, false);
    for (uint64_t i = 0; i < ast_len(n); i++) {
        check_child(ast_child(n, i), n, false);
    }
}

// Parses `src` and reports every invariant it breaks, the empty string when
// it breaks none; a source that yields no tree is reported as such, since the
// parser returns one whatever it reported (D14.2) and only a lexical error
// stops it, which no source here has.
static const char* violations(const char* src) {
    const ast_node_t* mod = parse_text(src);
    sb_clear(&report);
    if (mod == NULL) {
        sb_append(&report, "did not parse: ");
        sb_append(&report, parse_diags());
        return sb_cstr(&report);
    }
    check_node(mod, NULL);
    if (report.len != 0) {
        sb_append(&report, "in: ");
        sb_append(&report, src);
    }
    return sb_cstr(&report);
}

TEST(every_type_of_the_corpus_holds_the_invariants, {
    for (uint64_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++) {
        TEST_ASSERT_EQ_STR(violations(pt_wrap("", TYPES[i], " x = 0;")), "");
    }
})

TEST(every_expression_of_the_corpus_holds_the_invariants, {
    for (uint64_t i = 0; i < sizeof(EXPRS) / sizeof(EXPRS[0]); i++) {
        TEST_ASSERT_EQ_STR(violations(pt_wrap("i32 x = ", EXPRS[i], ";")), "");
    }
})

TEST(every_statement_of_the_corpus_holds_the_invariants, {
    for (uint64_t i = 0; i < sizeof(STMTS) / sizeof(STMTS[0]); i++) {
        TEST_ASSERT_EQ_STR(violations(pt_wrap("fn void f() {\n", STMTS[i], "\n}\n")), "");
    }
})

TEST(every_module_of_the_corpus_holds_the_invariants, {
    for (uint64_t i = 0; i < sizeof(MODULES) / sizeof(MODULES[0]); i++) {
        TEST_ASSERT_EQ_STR(violations(MODULES[i]), "");
    }
})

// A recovered tree holds them too: an error node lies inside the block or the
// module that holds it and ends at the last token the skip dropped (D14.2).
TEST(every_recovered_module_holds_the_invariants, {
    for (uint64_t i = 0; i < sizeof(RECOVERED) / sizeof(RECOVERED[0]); i++) {
        TEST_ASSERT_EQ_STR(violations(RECOVERED[i]), "");
    }
})

// A statement nested in a switch in a loop in a function still lies inside
// every one of them.
TEST(a_deeply_nested_statement_lies_inside_every_ancestor, {
    TEST_ASSERT_EQ_STR(violations("fn void f(i32@ xs) {\n"
                                  "    for (i32 v : xs) {\n"
                                  "        switch (v) {\n"
                                  "        case 1:\n"
                                  "            if (v > 0) {\n"
                                  "                while (v < 9) {\n"
                                  "                    g(v, h(v) + 1);\n"
                                  "                }\n"
                                  "            }\n"
                                  "        }\n"
                                  "    }\n"
                                  "}\n"),
                       "");
})

// Parses every source of the corpus, for the kinds it reaches.
static void walk_corpus(void) {
    for (uint64_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++) {
        TEST_UNUSED(violations(pt_wrap("", TYPES[i], " x = 0;")));
    }
    for (uint64_t i = 0; i < sizeof(EXPRS) / sizeof(EXPRS[0]); i++) {
        TEST_UNUSED(violations(pt_wrap("i32 x = ", EXPRS[i], ";")));
    }
    for (uint64_t i = 0; i < sizeof(STMTS) / sizeof(STMTS[0]); i++) {
        TEST_UNUSED(violations(pt_wrap("fn void f() {\n", STMTS[i], "\n}\n")));
    }
    for (uint64_t i = 0; i < sizeof(MODULES) / sizeof(MODULES[0]); i++) {
        TEST_UNUSED(violations(MODULES[i]));
    }
    for (uint64_t i = 0; i < sizeof(RECOVERED) / sizeof(RECOVERED[0]); i++) {
        TEST_UNUSED(violations(RECOVERED[i]));
    }
}

// The kinds the C bootstrap rejects (toolchain.md 7.3) never reach a tree:
// the parse that builds them reports and fails.
static bool is_unsupported_kind(ast_kind_t k) {
    return k == AST_FLOAT || k == AST_DO_WHILE || k == AST_TERNARY;
}

// Every kind but those is in the corpus, so the invariants above are checked
// on each of them.
TEST(the_corpus_covers_every_node_kind_the_bootstrap_builds, {
    walk_corpus();
    sb_t missing;
    sb_init(&missing);
    for (int32_t k = (int32_t)AST_NONE + 1; k < (int32_t)AST_KIND_COUNT; k++) {
        if (!kind_seen[k] && !is_unsupported_kind((ast_kind_t)k)) {
            sb_append(&missing, ast_kind_name((ast_kind_t)k));
            sb_push(&missing, ' ');
        }
    }
    TEST_ASSERT_EQ_STR(sb_cstr(&missing), "");
    sb_free(&missing);
})

int main(int argc, char** argv) {
    TEST_INIT("parser_range", argc, argv);
    sb_init(&report);
    TEST_RUN(every_type_of_the_corpus_holds_the_invariants);
    TEST_RUN(every_expression_of_the_corpus_holds_the_invariants);
    TEST_RUN(every_statement_of_the_corpus_holds_the_invariants);
    TEST_RUN(every_module_of_the_corpus_holds_the_invariants);
    TEST_RUN(every_recovered_module_holds_the_invariants);
    TEST_RUN(a_deeply_nested_statement_lies_inside_every_ancestor);
    TEST_RUN(the_corpus_covers_every_node_kind_the_bootstrap_builds);
    sb_free(&report);
    parse_done();
    TEST_EXIT();
}
