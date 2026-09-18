// Tests syntax trees and the S-expression printer.
// Covers arenas, child access, kind names, and hand-built trees.
#include "ast.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast_dump.h"
#include "diag.h"
#include "lexer.h"
#include "prim.h"
#include "str.h"
#include "types.h"

#include "common.h"
#include "test.h"

static ast_arena_t arena;
static sb_t buf;

static loc_t at(uint32_t line, uint32_t col) {
    return loc_make("t.ft", line, col);
}

static ast_node_t* node(ast_kind_t kind) {
    return ast_new(&arena, kind, at(1, 1));
}

// The S-expression of `n`, valid until the next call.
static const char* dump(const ast_node_t* n) {
    sb_clear(&buf);
    ast_dump(n, &buf);
    return sb_cstr(&buf);
}

static ast_node_t* int_lit(uint64_t v) {
    ast_node_t* n = node(AST_INT);
    n->ival = v;
    return n;
}

static ast_node_t* ident(const char* name) {
    ast_node_t* n = node(AST_IDENT);
    n->name = str_from_cstr(name);
    return n;
}

// `i32` as a base type, with the marks of the type position.
static ast_node_t* prim_type(prim_kind_t k, uint32_t flags) {
    ast_node_t* base = node(AST_TYPE_PRIM);
    base->op = (int32_t)k;
    ast_node_t* t = node(AST_TYPE);
    t->a = base;
    t->flags = flags;
    return t;
}

static void reset(void) {
    ast_arena_free(&arena);
    sb_clear(&buf);
}

TEST(arena_starts_empty, {
    reset();
    TEST_ASSERT_EQ_UINT64(ast_arena_count(&arena), (uint64_t)0);
    TEST_ASSERT_NULL(arena.blocks);
})

TEST(arena_hands_out_zeroed_nodes, {
    reset();
    ast_node_t* n = ast_new(&arena, AST_IDENT, at(3, 7));
    TEST_ASSERT_EQ_INT32((int32_t)n->kind, (int32_t)AST_IDENT);
    TEST_ASSERT_EQ_INT64((int64_t)n->loc.line, (int64_t)3);
    TEST_ASSERT_EQ_INT64((int64_t)n->loc.col, (int64_t)7);
    TEST_ASSERT_EQ_STR(n->loc.file, "t.ft");
    TEST_ASSERT_NULL(n->a);
    TEST_ASSERT_NULL(n->b);
    TEST_ASSERT_NULL(n->c);
    TEST_ASSERT_NULL(n->d);
    TEST_ASSERT_NULL(n->name.ptr);
    TEST_ASSERT_EQ_UINT64(n->ival, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(n->list.len, (uint64_t)0);
    TEST_ASSERT_EQ_INT64((int64_t)n->flags, (int64_t)0);
    TEST_ASSERT_EQ_INT64((int64_t)n->name_loc.line, (int64_t)0);
    TEST_ASSERT_EQ_INT64((int64_t)n->tail_loc.line, (int64_t)0);
    TEST_ASSERT_NULL(n->type);
    TEST_ASSERT_NULL(n->sym);
    TEST_ASSERT_EQ_UINT64(n->aux, (uint64_t)0);
    TEST_ASSERT_EQ_INT64((int64_t)n->ann, (int64_t)0);
    TEST_ASSERT_EQ_UINT64(ast_arena_count(&arena), (uint64_t)1);
})

// A node keeps its address and its contents when later nodes fill new
// blocks: the parser holds pointers to nodes it allocated earlier.
TEST(arena_nodes_are_stable_across_blocks, {
    reset();
    ast_node_t* first = int_lit(1);
    ast_node_t* last = NULL;
    const uint64_t n = AST_ARENA_BLOCK_NODES * 2 + 1;
    for (uint64_t i = 1; i < n; i++) {
        last = int_lit(i);
    }
    TEST_ASSERT_EQ_UINT64(ast_arena_count(&arena), n);
    TEST_ASSERT_EQ_UINT64(arena.block_len, (uint64_t)3);
    TEST_ASSERT_EQ_UINT64(arena.used, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(first->ival, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(last->ival, n - 1);
    TEST_ASSERT_EQ_STR(dump(first), "(int 1)");
})

// The last node of a block and the first of the next one: the boundary the
// arena's block_nodes count depends on.
TEST(arena_fills_a_block_exactly, {
    reset();
    for (uint64_t i = 0; i < AST_ARENA_BLOCK_NODES; i++) {
        TEST_UNUSED(int_lit(i));
    }
    TEST_ASSERT_EQ_UINT64(arena.block_len, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(arena.used, (uint64_t)AST_ARENA_BLOCK_NODES);
    TEST_ASSERT_EQ_UINT64(ast_arena_count(&arena), (uint64_t)AST_ARENA_BLOCK_NODES);
    TEST_UNUSED(int_lit(0));
    TEST_ASSERT_EQ_UINT64(arena.block_len, (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(arena.used, (uint64_t)1);
})

TEST(arena_free_empties_and_is_reusable, {
    reset();
    ast_node_t* n = node(AST_BLOCK);
    ast_push(n, int_lit(1));
    ast_arena_free(&arena);
    TEST_ASSERT_EQ_UINT64(ast_arena_count(&arena), (uint64_t)0);
    TEST_ASSERT_NULL(arena.blocks);
    ast_node_t* again = int_lit(2);
    TEST_ASSERT_EQ_STR(dump(again), "(int 2)");
})

// Lists live in the nodes and the arena releases them: under the address
// sanitizer this test fails if ast_arena_free forgets a block's lists.
TEST(arena_free_releases_the_lists, {
    reset();
    for (uint64_t i = 0; i < AST_ARENA_BLOCK_NODES + 1; i++) {
        ast_node_t* n = node(AST_BLOCK);
        for (uint64_t j = 0; j < 32; j++) {
            ast_push(n, int_lit(j));
        }
    }
    ast_arena_free(&arena);
    TEST_ASSERT_EQ_UINT64(ast_arena_count(&arena), (uint64_t)0);
})

TEST(children_are_pushed_and_read_back, {
    reset();
    ast_node_t* n = node(AST_BLOCK);
    TEST_ASSERT_EQ_UINT64(ast_len(n), (uint64_t)0);
    ast_node_t* a = int_lit(7);
    ast_node_t* b = ident("x");
    ast_push(n, a);
    ast_push(n, b);
    TEST_ASSERT_EQ_UINT64(ast_len(n), (uint64_t)2);
    TEST_ASSERT_TRUE(ast_child(n, 0) == a);
    TEST_ASSERT_TRUE(ast_child(n, 1) == b);
})

TEST(marks_are_read_from_the_flags, {
    reset();
    ast_node_t* plain = node(AST_TYPE);
    ast_node_t* owned = node(AST_TYPE);
    owned->flags = AST_FLAG_OWN;
    ast_node_t* both = node(AST_TYPE);
    both->flags = AST_FLAG_OWN | AST_FLAG_MUT;
    TEST_ASSERT_FALSE(ast_is_own(plain));
    TEST_ASSERT_FALSE(ast_is_mut(plain));
    TEST_ASSERT_TRUE(ast_is_own(owned));
    TEST_ASSERT_FALSE(ast_is_mut(owned));
    TEST_ASSERT_TRUE(ast_is_own(both));
    TEST_ASSERT_TRUE(ast_is_mut(both));
})

// A rename that nobody meant fails here rather than silently changing what an editor reads.
static const char KIND_NAMES[] =
    "none module import path item fn param struct field-decl enum member var "
    "type prim string void noreturn name fn-type suffix "
    "block assign incdec call-stmt if while do for range-for switch case defer return "
    "break continue init designator "
    "int float char str bool null ident unary binary ternary call index span field arrow "
    "cast sizeof new struct-lit array-lit error";

TEST(the_kind_names_are_the_ones_toolchain_md_1_lists, {
    sb_t names;
    sb_init(&names);
    for (int32_t k = 0; k < (int32_t)AST_KIND_COUNT; k++) {
        if (k != 0) {
            sb_push(&names, ' ');
        }
        sb_append(&names, ast_kind_name((ast_kind_t)k));
    }
    const char* joined = sb_cstr(&names);
    TEST_ASSERT_EQ_STR(joined, KIND_NAMES);
    sb_free(&names);
})

TEST(every_kind_has_a_name, {
    for (int32_t k = 0; k < (int32_t)AST_KIND_COUNT; k++) {
        const char* name = ast_kind_name((ast_kind_t)k);
        TEST_ASSERT_NONNULL(name);
        TEST_ASSERT_NE_CHAR(name[0], '?');
    }
    TEST_ASSERT_EQ_STR(ast_kind_name(AST_KIND_COUNT), "?");
    TEST_ASSERT_EQ_STR(ast_kind_name(AST_VAR_DECL), "var");
    TEST_ASSERT_EQ_STR(ast_kind_name(AST_RANGE_FOR), "range-for");
})

TEST(dump_of_null_is_nil, {
    reset();
    TEST_ASSERT_EQ_STR(dump(NULL), "nil");
})

// The node that stands for a region the parser skipped after a syntax error
// prints without children, whatever was left in it.
TEST(dump_of_an_error_node_has_no_children, {
    reset();
    ast_node_t* err = node(AST_ERROR);
    TEST_ASSERT_EQ_STR(ast_kind_name(AST_ERROR), "error");
    TEST_ASSERT_EQ_STR(dump(err), "(error)");
    err->a = node(AST_BREAK);
    ast_push(err, node(AST_CONTINUE));
    TEST_ASSERT_EQ_STR(dump(err), "(error)");
})

// NOLINTBEGIN(readability-magic-numbers) the literals below are the test data.
TEST(dump_of_literals, {
    reset();
    TEST_ASSERT_EQ_STR(dump(int_lit(0)), "(int 0)");
    TEST_ASSERT_EQ_STR(dump(int_lit(18446744073709551615U)), "(int 18446744073709551615)");
    ast_node_t* ch = node(AST_CHAR);
    ch->ival = 'a';
    TEST_ASSERT_EQ_STR(dump(ch), "(char 97)");
    ast_node_t* yes = node(AST_BOOL);
    yes->ival = 1;
    TEST_ASSERT_EQ_STR(dump(yes), "(bool true)");
    ast_node_t* no = node(AST_BOOL);
    TEST_ASSERT_EQ_STR(dump(no), "(bool false)");
    TEST_ASSERT_EQ_STR(dump(node(AST_NULL)), "(null)");
    TEST_ASSERT_EQ_STR(dump(ident("count")), "(ident count)");
})

// A string literal prints its decoded bytes: a quote, a backslash and every
// byte outside 0x20..0x7E are escaped.
TEST(dump_escapes_string_bytes, {
    reset();
    ast_node_t* s = node(AST_STRING);
    s->name = str_from_range("a\"b\\c\n\x7F\xC3", 8);
    TEST_ASSERT_EQ_STR(dump(s), "(str \"a\\\"b\\\\c\\x0A\\x7F\\xC3\")");
    ast_node_t* empty = node(AST_STRING);
    empty->name = str_from_cstr("");
    TEST_ASSERT_EQ_STR(dump(empty), "(str \"\")");
    ast_node_t* f = node(AST_FLOAT);
    f->name = str_from_cstr("1.5e-3");
    TEST_ASSERT_EQ_STR(dump(f), "(float \"1.5e-3\")");
})

TEST(dump_of_operators_uses_the_token_spelling, {
    reset();
    ast_node_t* sum = node(AST_BINARY);
    sum->op = TOK_PLUS_WRAP;
    sum->a = int_lit(1);
    sum->b = int_lit(2);
    TEST_ASSERT_EQ_STR(dump(sum), "(binary +% (int 1) (int 2))");
    ast_node_t* neg = node(AST_UNARY);
    neg->op = TOK_MINUS;
    neg->a = ident("x");
    TEST_ASSERT_EQ_STR(dump(neg), "(unary - (ident x))");
    ast_node_t* set = node(AST_ASSIGN);
    set->op = TOK_SHL_ASSIGN;
    set->a = ident("x");
    set->b = int_lit(1);
    TEST_ASSERT_EQ_STR(dump(set), "(assign <<= (ident x) (int 1))");
    ast_node_t* inc = node(AST_INCDEC);
    inc->op = TOK_PLUS_PLUS;
    inc->a = ident("i");
    TEST_ASSERT_EQ_STR(dump(inc), "(incdec ++ (ident i))");
})

// A type prints its base, then the marks of the base position, then its
// suffixes in source order: `u8 mut@ own`.
TEST(dump_of_a_type_with_marks_and_suffixes, {
    reset();
    ast_node_t* t = prim_type(PRIM_U8, AST_FLAG_MUT);
    ast_node_t* suffix = node(AST_TYPE_SUFFIX);
    suffix->op = SUFFIX_SPAN;
    suffix->flags = AST_FLAG_OWN;
    ast_push(t, suffix);
    TEST_ASSERT_EQ_STR(dump(t), "(type (prim u8) mut (span own))");
})

TEST(dump_of_array_and_pointer_suffixes, {
    reset();
    ast_node_t* t = prim_type(PRIM_I32, 0);
    ast_node_t* arr = node(AST_TYPE_SUFFIX);
    arr->op = SUFFIX_ARRAY;
    arr->a = int_lit(4);
    arr->flags = AST_FLAG_MUT;
    ast_push(t, arr);
    ast_node_t* ptr = node(AST_TYPE_SUFFIX);
    ptr->op = SUFFIX_PTR;
    ptr->flags = AST_FLAG_OWN | AST_FLAG_MUT;
    ast_push(t, ptr);
    TEST_ASSERT_EQ_STR(dump(t), "(type (prim i32) (array (int 4) mut) (ptr own mut))");
})

TEST(dump_of_named_and_function_types, {
    reset();
    ast_node_t* name = node(AST_TYPE_NAME);
    name->name = str_from_cstr("math");
    name->a = ident("vector");
    ast_node_t* t = node(AST_TYPE);
    t->a = name;
    TEST_ASSERT_EQ_STR(dump(t), "(type (name math vector))");

    ast_node_t* plain = node(AST_TYPE_NAME);
    plain->name = str_from_cstr("node");
    ast_node_t* pt = node(AST_TYPE);
    pt->a = plain;
    TEST_ASSERT_EQ_STR(dump(pt), "(type (name node))");

    ast_node_t* fn = node(AST_TYPE_FN);
    fn->a = prim_type(PRIM_I32, 0);
    ast_push(fn, prim_type(PRIM_I32, 0));
    ast_node_t* ft = node(AST_TYPE);
    ft->a = fn;
    TEST_ASSERT_EQ_STR(dump(ft), "(type (fn-type (type (prim i32)) (type (prim i32))))");
})

TEST(dump_of_string_void_and_noreturn_bases, {
    reset();
    ast_node_t* s = node(AST_TYPE);
    s->a = node(AST_TYPE_STRING);
    s->flags = AST_FLAG_OWN;
    TEST_ASSERT_EQ_STR(dump(s), "(type (string) own)");
    ast_node_t* v = node(AST_TYPE);
    v->a = node(AST_TYPE_VOID);
    TEST_ASSERT_EQ_STR(dump(v), "(type (void))");
    ast_node_t* nr = node(AST_TYPE);
    nr->a = node(AST_TYPE_NORETURN);
    TEST_ASSERT_EQ_STR(dump(nr), "(type (noreturn))");
})

// Absent fixed children print as nil, so a for without init, condition or
// step is not confused with one that has them.
TEST(dump_keeps_the_position_of_absent_children, {
    reset();
    ast_node_t* loop = node(AST_FOR);
    loop->d = node(AST_BLOCK);
    TEST_ASSERT_EQ_STR(dump(loop), "(for nil nil nil (block))");
    ast_node_t* ret = node(AST_RETURN);
    TEST_ASSERT_EQ_STR(dump(ret), "(return nil)");
    ast_node_t* sl = node(AST_SPAN);
    sl->a = ident("s");
    sl->c = int_lit(3);
    TEST_ASSERT_EQ_STR(dump(sl), "(span (ident s) nil (int 3))");
})

TEST(dump_of_a_module_with_declarations, {
    reset();
    ast_node_t* mod = node(AST_MODULE);

    ast_node_t* path = node(AST_PATH);
    ast_push(path, ident("std"));
    ast_push(path, ident("io"));
    ast_node_t* imp = node(AST_IMPORT);
    imp->a = path;
    imp->b = ident("io2");
    ast_push(mod, imp);

    ast_node_t* fn = node(AST_FN_DECL);
    fn->a = prim_type(PRIM_I32, 0);
    fn->name = str_from_cstr("main");
    fn->b = node(AST_BLOCK);
    ast_push(mod, fn);

    TEST_ASSERT_EQ_STR(dump(mod),
                       "(module (import (path std io) (ident io2))"
                       " (fn (type (prim i32)) main (params) (block)))");
})

TEST(dump_of_an_extern_declaration_and_parameters, {
    reset();
    ast_node_t* fn = node(AST_FN_DECL);
    fn->flags = AST_FLAG_EXTERN;
    fn->a = node(AST_TYPE);
    fn->a->a = node(AST_TYPE_VOID);
    fn->name = str_from_cstr("puts");
    ast_node_t* param = node(AST_PARAM);
    param->a = node(AST_TYPE);
    param->a->a = node(AST_TYPE_STRING);
    param->name = str_from_cstr("s");
    ast_push(fn, param);
    TEST_ASSERT_EQ_STR(dump(fn),
                       "(extern-fn (type (void)) puts (params (param (type (string)) s)) nil)");
})

TEST(dump_of_an_extern_variable_tail, {
    reset();
    ast_node_t* fn = node(AST_FN_DECL);
    fn->flags = AST_FLAG_EXTERN | AST_FLAG_VARIADIC;
    fn->a = node(AST_TYPE);
    fn->a->a = node(AST_TYPE_VOID);
    fn->name = str_from_cstr("printf");
    fn->tail_loc = at(1, 28);
    ast_node_t* param = node(AST_PARAM);
    param->a = node(AST_TYPE);
    param->a->a = node(AST_TYPE_STRING);
    param->name = str_from_cstr("fmt");
    ast_push(fn, param);
    TEST_ASSERT_EQ_STR(
        dump(fn), "(extern-fn (type (void)) printf (params (param (type (string)) fmt) ...) nil)");
})

TEST(dump_of_struct_enum_and_import_items, {
    reset();
    ast_node_t* st = node(AST_STRUCT_DECL);
    st->name = str_from_cstr("point");
    ast_node_t* field = node(AST_FIELD_DECL);
    field->a = prim_type(PRIM_I32, 0);
    field->name = str_from_cstr("x");
    ast_push(st, field);
    TEST_ASSERT_EQ_STR(dump(st), "(struct point (field-decl (type (prim i32)) x))");

    ast_node_t* en = node(AST_ENUM_DECL);
    en->name = str_from_cstr("color");
    ast_node_t* red = node(AST_ENUM_MEMBER);
    red->name = str_from_cstr("red");
    ast_node_t* green = node(AST_ENUM_MEMBER);
    green->name = str_from_cstr("green");
    green->a = int_lit(5);
    ast_push(en, red);
    ast_push(en, green);
    TEST_ASSERT_EQ_STR(dump(en), "(enum color (member red nil) (member green (int 5)))");

    ast_node_t* item = node(AST_IMPORT_ITEM);
    item->name = str_from_cstr("s1");
    TEST_ASSERT_EQ_STR(dump(item), "(item s1 nil)");
})

TEST(dump_of_statements, {
    reset();
    ast_node_t* sw = node(AST_SWITCH);
    sw->a = ident("c");
    ast_node_t* one = node(AST_CASE);
    ast_push(one, int_lit(1));
    ast_push(one, int_lit(2));
    one->a = node(AST_BLOCK);
    ast_node_t* other = node(AST_CASE);
    other->flags = AST_FLAG_DEFAULT;
    other->a = node(AST_BLOCK);
    ast_push(sw, one);
    ast_push(sw, other);
    TEST_ASSERT_EQ_STR(dump(sw),
                       "(switch (ident c) (case (labels (int 1) (int 2)) (block))"
                       " (case default (block)))");

    ast_node_t* def = node(AST_DEFER);
    def->a = node(AST_CALL_STMT);
    def->a->a = node(AST_CALL);
    def->a->a->a = ident("f");
    TEST_ASSERT_EQ_STR(dump(def), "(defer (call-stmt (call (ident f))))");
    TEST_ASSERT_EQ_STR(dump(node(AST_BREAK)), "(break)");
    TEST_ASSERT_EQ_STR(dump(node(AST_CONTINUE)), "(continue)");
})

TEST(dump_of_postfix_and_literal_expressions, {
    reset();
    ast_node_t* call = node(AST_CALL);
    call->a = ident("f");
    ast_push(call, int_lit(1));
    ast_push(call, int_lit(2));
    TEST_ASSERT_EQ_STR(dump(call), "(call (ident f) (int 1) (int 2))");

    ast_node_t* idx = node(AST_INDEX);
    idx->a = ident("a");
    idx->b = int_lit(0);
    TEST_ASSERT_EQ_STR(dump(idx), "(index (ident a) (int 0))");

    ast_node_t* field = node(AST_FIELD);
    field->a = ident("p");
    field->name = str_from_cstr("x");
    TEST_ASSERT_EQ_STR(dump(field), "(field (ident p) x)");

    ast_node_t* arrow = node(AST_ARROW);
    arrow->a = ident("p");
    arrow->name = str_from_cstr("next");
    TEST_ASSERT_EQ_STR(dump(arrow), "(arrow (ident p) next)");

    ast_node_t* init = node(AST_BRACE_INIT);
    init->flags = AST_FLAG_DESIGNATED;
    ast_node_t* des = node(AST_DESIGNATOR);
    des->name = str_from_cstr("x");
    des->a = int_lit(1);
    ast_push(init, des);
    TEST_ASSERT_EQ_STR(dump(init), "(init (designator x (int 1)))");
})

TEST(dump_of_cast_sizeof_and_new, {
    reset();
    ast_node_t* cast = node(AST_CAST);
    cast->a = ident("x");
    cast->b = prim_type(PRIM_I64, 0);
    TEST_ASSERT_EQ_STR(dump(cast), "(cast (ident x) (type (prim i64)))");

    ast_node_t* sz = node(AST_SIZEOF);
    sz->a = prim_type(PRIM_U8, 0);
    TEST_ASSERT_EQ_STR(dump(sz), "(sizeof (type (prim u8)))");

    ast_node_t* one = node(AST_NEW);
    one->a = prim_type(PRIM_I32, 0);
    TEST_ASSERT_EQ_STR(dump(one), "(new (type (prim i32)) nil)");

    ast_node_t* many = node(AST_NEW);
    many->a = prim_type(PRIM_I32, 0);
    many->b = ident("n");
    TEST_ASSERT_EQ_STR(dump(many), "(new (type (prim i32)) (ident n))");
})

TEST(dump_of_ternary_and_literals_with_types, {
    reset();
    ast_node_t* t = node(AST_TERNARY);
    t->a = ident("c");
    t->b = int_lit(1);
    t->c = int_lit(2);
    TEST_ASSERT_EQ_STR(dump(t), "(ternary (ident c) (int 1) (int 2))");

    ast_node_t* name = node(AST_TYPE_NAME);
    name->name = str_from_cstr("point");
    ast_node_t* lit = node(AST_STRUCT_LIT);
    lit->a = name;
    lit->b = node(AST_BRACE_INIT);
    ast_push(lit->b, int_lit(1));
    TEST_ASSERT_EQ_STR(dump(lit), "(struct-lit (name point) (init (int 1)))");

    ast_node_t* arr = node(AST_ARRAY_LIT);
    arr->a = prim_type(PRIM_I32, 0);
    arr->b = node(AST_BRACE_INIT);
    TEST_ASSERT_EQ_STR(dump(arr), "(array-lit (type (prim i32)) (init))");
})
// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("ast", argc, argv);
    ast_arena_init(&arena);
    sb_init(&buf);
    TEST_RUN(arena_starts_empty);
    TEST_RUN(arena_hands_out_zeroed_nodes);
    TEST_RUN(arena_nodes_are_stable_across_blocks);
    TEST_RUN(arena_fills_a_block_exactly);
    TEST_RUN(arena_free_empties_and_is_reusable);
    TEST_RUN(arena_free_releases_the_lists);
    TEST_RUN(children_are_pushed_and_read_back);
    TEST_RUN(marks_are_read_from_the_flags);
    TEST_RUN(every_kind_has_a_name);
    TEST_RUN(the_kind_names_are_the_ones_toolchain_md_1_lists);
    TEST_RUN(dump_of_null_is_nil);
    TEST_RUN(dump_of_an_error_node_has_no_children);
    TEST_RUN(dump_of_literals);
    TEST_RUN(dump_escapes_string_bytes);
    TEST_RUN(dump_of_operators_uses_the_token_spelling);
    TEST_RUN(dump_of_a_type_with_marks_and_suffixes);
    TEST_RUN(dump_of_array_and_pointer_suffixes);
    TEST_RUN(dump_of_named_and_function_types);
    TEST_RUN(dump_of_string_void_and_noreturn_bases);
    TEST_RUN(dump_keeps_the_position_of_absent_children);
    TEST_RUN(dump_of_a_module_with_declarations);
    TEST_RUN(dump_of_an_extern_declaration_and_parameters);
    TEST_RUN(dump_of_an_extern_variable_tail);
    TEST_RUN(dump_of_struct_enum_and_import_items);
    TEST_RUN(dump_of_statements);
    TEST_RUN(dump_of_postfix_and_literal_expressions);
    TEST_RUN(dump_of_cast_sizeof_and_new);
    TEST_RUN(dump_of_ternary_and_literals_with_types);
    ast_arena_free(&arena);
    sb_free(&buf);
    TEST_EXIT();
}
