// Whole modules, in the shapes the specification and the language tests
// write them: the parser meets every production at once here, so an
// interaction between two of them that the focused suites miss shows up as a
// diagnostic on a program that must parse.
#include <stdint.h>

#include "ast.h"
#include "parser.h"
#include "parser_helpers.h"

#include "test.h"

// The `i`-th statement of the body of the `d`-th declaration.
static const ast_node_t* body_stmt(const ast_node_t* mod, uint64_t d, uint64_t i) {
    const ast_node_t* decl = pt_decl(mod, d);
    if (decl == NULL || decl->b == NULL || ast_len(decl->b) <= i) {
        return NULL;
    }
    return ast_child(decl->b, i);
}

// NOLINTBEGIN(readability-magic-numbers) the programs and the trees they
// parse to are the test data.

// A map over a slice of buckets: `new(T, n)`, indexing, `->`, `del` and the
// ownership markers of D17.2 in fields and locals.
TEST(a_hash_map_module_parses, {
    const ast_node_t* mod = parse_text("struct entry {\n"
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
                                       "fn map make_map(u64 n) {\n"
                                       "    map mut m = {};\n"
                                       "    m.buckets = new(entry* own, n);\n"
                                       "    m.count = 0;\n"
                                       "    return m;\n"
                                       "}\n"
                                       "\n"
                                       "fn void put(map mut* m, string key, i32 value) {\n"
                                       "    u64 h = hash(key) % m->buckets.len;\n"
                                       "    entry mut* own e = new(entry);\n"
                                       "    e->key = key;\n"
                                       "    e->value = value;\n"
                                       "    e->next = move(m->buckets[h]);\n"
                                       "    m->buckets[h] = move(e);\n"
                                       "    m->count += 1;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_UINT64(ast_len(mod), (uint64_t)4);
    TEST_ASSERT_EQ_STR(dumped(ast_child(pt_decl(mod, 1), 0)),
                       "(field-decl (type (name entry) mut (ptr own mut) (slice own)) buckets)");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 2, 1)),
                       "(assign = (field (ident m) buckets)"
                       " (new (type (name entry) (ptr own)) (ident n)))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 3, 0)),
                       "(var h (type (prim u64))"
                       " (binary % (call (ident hash) (ident key))"
                       " (field (arrow (ident m) buckets) len)))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 3, 5)),
                       "(assign = (index (arrow (ident m) buckets) (ident h))"
                       " (call (ident move) (ident e)))");
})

// A tokenizer: `char` literals, `switch` over them, string slicing, an enum
// and a range loop.
TEST(a_tokenizer_module_parses, {
    const ast_node_t* mod = parse_text("enum kind { ident, number, symbol, end }\n"
                                       "\n"
                                       "struct token {\n"
                                       "    kind k;\n"
                                       "    string text;\n"
                                       "}\n"
                                       "\n"
                                       "fn bool is_digit(char c) {\n"
                                       "    return c >= '0' && c <= '9';\n"
                                       "}\n"
                                       "\n"
                                       "fn token next(string src, u64 mut* pos) {\n"
                                       "    mut u64 i = *pos;\n"
                                       "    while (i < src.len && src[i] == ' ') {\n"
                                       "        i += 1;\n"
                                       "    }\n"
                                       "    if (i == src.len) {\n"
                                       "        *pos = i;\n"
                                       "        return token{kind.end, src[i..i]};\n"
                                       "    }\n"
                                       "    switch (src[i]) {\n"
                                       "    case '+', '-', '*', '/':\n"
                                       "        *pos = i + 1;\n"
                                       "        return token{kind.symbol, src[i..i + 1]};\n"
                                       "    default:\n"
                                       "        break;\n"
                                       "    }\n"
                                       "    return token{kind.ident, src[*pos..i]};\n"
                                       "}\n");
    TEST_ASSERT_NULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(),
                       "t.ft:13:5: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
})

TEST(a_tokenizer_module_in_the_east_marker_spelling_parses, {
    const ast_node_t* mod = parse_text("enum kind { ident, number, symbol, end }\n"
                                       "\n"
                                       "struct token {\n"
                                       "    kind k;\n"
                                       "    string text;\n"
                                       "}\n"
                                       "\n"
                                       "fn bool is_digit(char c) {\n"
                                       "    return c >= '0' && c <= '9';\n"
                                       "}\n"
                                       "\n"
                                       "fn token next(string src, u64 mut* pos) {\n"
                                       "    u64 mut i = *pos;\n"
                                       "    while (i < src.len && src[i] == ' ') {\n"
                                       "        i += 1;\n"
                                       "    }\n"
                                       "    if (i == src.len) {\n"
                                       "        *pos = i;\n"
                                       "        return token{kind.end, src[i..i]};\n"
                                       "    }\n"
                                       "    switch (src[i]) {\n"
                                       "    case '+', '-', '*', '/':\n"
                                       "        *pos = i + 1;\n"
                                       "        return token{kind.symbol, src[i..i + 1]};\n"
                                       "    default:\n"
                                       "        break;\n"
                                       "    }\n"
                                       "    return token{kind.ident, src[*pos..i]};\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 0)),
                       "(enum kind (member ident nil) (member number nil)"
                       " (member symbol nil) (member end nil))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 2, 0)),
                       "(return (binary && (binary >= (ident c) (char 48))"
                       " (binary <= (ident c) (char 57))))");
    const ast_node_t* sw = body_stmt(mod, 3, 3);
    TEST_ASSERT_EQ_STR(dumped(ast_child(sw, 0)),
                       "(case (labels (char 43) (char 45) (char 42) (char 47))"
                       " (block (assign = (unary * (ident pos)) (binary + (ident i) (int 1)))"
                       " (return (struct-lit (name token) (init (field (ident kind) symbol)"
                       " (slice (ident src) (ident i) (binary + (ident i) (int 1))))))))");
})

// An FFI module: externs, `void*`, casts and a `noreturn` function (D9.8,
// D3.11, D8.5).
TEST(an_ffi_module_parses, {
    const ast_node_t* mod = parse_text("extern fn void* malloc(u64 size);\n"
                                       "extern fn void free(void* p);\n"
                                       "extern fn i32 write(i32 fd, void* buf, u64 n);\n"
                                       "extern fn noreturn exit(i32 status);\n"
                                       "\n"
                                       "fn u8 mut@ own take(u64 n) {\n"
                                       "    void* raw = malloc(n);\n"
                                       "    if (raw == null) {\n"
                                       "        exit(1);\n"
                                       "    }\n"
                                       "    u8 mut* own p = cast(raw, u8 mut* own);\n"
                                       "    return p[0..n];\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 1)),
                       "(extern-fn (type (void)) free (params (param (type (void) (ptr)) p)) nil)");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 3)),
                       "(extern-fn (type (noreturn)) exit"
                       " (params (param (type (prim i32)) status)) nil)");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 4)->a), "(type (prim u8) mut (slice own))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 4, 2)),
                       "(var p (type (prim u8) mut (ptr own))"
                       " (cast (ident raw) (type (prim u8) mut (ptr own))))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 4, 3)),
                       "(return (slice (ident p) (int 0) (ident n)))");
})

// A module that imports in every form and qualifies names across modules
// (D9.3, D9.4).
TEST(an_importing_module_parses, {
    const ast_node_t* mod = parse_text("import std::io;\n"
                                       "import std::str as s;\n"
                                       "import util::text::trim;\n"
                                       "import util::text::{pad, split as cut};\n"
                                       "\n"
                                       "fn void main() {\n"
                                       "    io.println(s.dup(\"x\"));\n"
                                       "    string t = trim(\"  y  \");\n"
                                       "    string u = cut(t, ' ');\n"
                                       "    util.text.pad(u, 4);\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 1)), "(import (path std str) (ident s))");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 2)), "(import (path util text trim) nil)");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 3)),
                       "(import (path util text) nil (item pad nil) (item split (ident cut)))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 4, 0)),
                       "(call-stmt (call (field (ident io) println)"
                       " (call (field (ident s) dup) (str \"x\"))))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 4, 3)),
                       "(call-stmt (call (field (field (ident util) text) pad)"
                       " (ident u) (int 4)))");
})

// A module of constants and globals: every literal form in an initializer
// (D7.10, D6.5).
TEST(a_module_of_constants_parses, {
    const ast_node_t* mod = parse_text("i32 LIMIT = 16;\n"
                                       "u64 MASK = 0xFF;\n"
                                       "bool DEBUG = false;\n"
                                       "char TAB = '\\t';\n"
                                       "string NAME = \"fort\";\n"
                                       "i32[3] PRIMES = {2, 3, 5};\n"
                                       "point ORIGIN = {};\n"
                                       "point CORNER = {.x = 1, .y = 2};\n"
                                       "color FIRST = color.red;\n"
                                       "node* NOTHING = null;\n"
                                       "fn i32(i32) OP = twice;\n"
                                       "i32 mut counter = 0;\n"
                                       "i32 DERIVED = LIMIT * 2 + 1;\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_UINT64(ast_len(mod), (uint64_t)13);
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 3)), "(var TAB (type (prim char)) (char 9))");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 5)),
                       "(var PRIMES (type (prim i32) (array (int 3)))"
                       " (init (int 2) (int 3) (int 5)))");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 7)),
                       "(var CORNER (type (name point))"
                       " (init (designator x (int 1)) (designator y (int 2))))");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 10)),
                       "(var OP (type (fn-type (type (prim i32)) (type (prim i32))))"
                       " (ident twice))");
    TEST_ASSERT_EQ_STR(dumped(pt_decl(mod, 12)),
                       "(var DERIVED (type (prim i32))"
                       " (binary + (binary * (ident LIMIT) (int 2)) (int 1)))");
})

// A function using every statement form of grammar.md 5 in one body.
TEST(a_function_with_every_statement_form_parses, {
    const ast_node_t* mod = parse_text("fn i32 all(i32@ xs) {\n"
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
                                       "            total -= v;\n"
                                       "        }\n"
                                       "    }\n"
                                       "    return total;\n"
                                       "}\n");
    TEST_ASSERT_NONNULL(mod);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    const ast_node_t* fn = pt_decl(mod, 0);
    TEST_ASSERT_EQ_UINT64(ast_len(fn->b), (uint64_t)8);
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 2)),
                       "(block (var scoped (type (prim i32) mut) (int 1))"
                       " (assign += (ident total) (ident scoped)))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 4)),
                       "(while (binary < (ident total) (int 10))"
                       " (block (incdec ++ (ident total))))");
    TEST_ASSERT_EQ_STR(dumped(body_stmt(mod, 0, 7)), "(return (ident total))");
})

// The same source parses to the same tree twice: no state survives a parse.
TEST(parsing_is_repeatable, {
    const char* src = "fn i32 f(i32@ xs) {\n"
                      "    i32 mut t = 0;\n"
                      "    for (i32 v : xs) {\n"
                      "        t += v;\n"
                      "    }\n"
                      "    return t;\n"
                      "}\n";
    const char* first = parse_dump(src);
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    sb_t held;
    sb_init(&held);
    sb_append(&held, first);
    const char* second = parse_dump(src);
    TEST_ASSERT_EQ_STR(second, sb_cstr(&held));
    sb_free(&held);
})

// A file that fails to parse leaves the next one unaffected: the parser
// holds no state between calls (D14.2).
TEST(a_failed_parse_does_not_poison_the_next, {
    TEST_ASSERT_EQ_STR(parse_fails("i32 x = ;"),
                       "t.ft:1:9: error: expected an expression, found ';'\n");
    TEST_ASSERT_EQ_STR(parse_dump("i32 x = 1;"), "(module (var x (type (prim i32)) (int 1)))");
    TEST_ASSERT_EQ_STR(parse_diags(), "");
    TEST_ASSERT_EQ_STR(parse_fails("fn void f( { }"),
                       "t.ft:1:12: error: expected a type, found '{'\n");
    TEST_ASSERT_EQ_STR(parse_dump("fn void f() { }"),
                       "(module (fn (type (void)) f (params) (block)))");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser_program", argc, argv);
    TEST_RUN(a_hash_map_module_parses);
    TEST_RUN(a_tokenizer_module_parses);
    TEST_RUN(a_tokenizer_module_in_the_east_marker_spelling_parses);
    TEST_RUN(an_ffi_module_parses);
    TEST_RUN(an_importing_module_parses);
    TEST_RUN(a_module_of_constants_parses);
    TEST_RUN(a_function_with_every_statement_form_parses);
    TEST_RUN(parsing_is_repeatable);
    TEST_RUN(a_failed_parse_does_not_poison_the_next);
    parse_done();
    TEST_EXIT();
}
