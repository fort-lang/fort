// Unit tests of the ownership rules of the checker (core-language.md 3.9,
// memory-model.md 2): who needs `move`, what an owning temporary may land in,
// which storage `move` and `del` may empty, and the lending a range loop and a
// comparison do.
// D17.5 to D17.10, D17.12, D17.13
//
// The type-level half -- placement, identity and the monotone drops -- is in
// types_build_test.c and check_conv_test.c; the emitted code is in
// gen_own_test.c.
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// A struct with one owning field, which makes it an owning aggregate.
// D17.7
#define VEC "struct vec {\n    i32 mut@ own data;\n    u64 len;\n}\n"
// A node with an owning `next`, the shape memory-model.md 2.3 uses.
#define NODE "struct node {\n    i32 value;\n    node mut* own next;\n}\n"
// A struct that owns only through a struct field, so the owning mark reaches
// it through one level of nesting.
// D17.7
#define OUTER                                                                                      \
    "struct inner {\n    i32 mut@ own data;\n}\n"                                                  \
    "struct outer {\n    inner in;\n    u64 n;\n}\n"

// ---- transfer needs `move` ----------------------------------------------------------
// D17.5

TEST(copying_an_owning_lvalue_into_a_declaration_needs_move, {
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                 "    i32 mut@ own b = a;\n    del(a);"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
    // With the `move` the same program checks, and the source need not be mut.
    // D17.6
    TEST_ASSERT_TRUE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                "    i32 mut@ own b = move(a);\n    del(b);"));
})

TEST(copying_an_owning_lvalue_into_an_assignment_needs_move, {
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                 "    i32 mut@ own mut b = {};\n    b = a;\n    del(a);"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move'"));
    TEST_ASSERT_TRUE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                "    i32 mut@ own mut b = {};\n    b = move(a);\n    del(b);"));
})

TEST(passing_an_owning_lvalue_to_an_own_parameter_needs_move, {
    TEST_ASSERT_FALSE(check_src("fn void eat(i32 mut@ own s) {\n    del(s);\n}\n"
                                "fn i32 main() {\n    i32 mut@ own a = new(i32, 2);\n"
                                "    eat(a);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
    TEST_ASSERT_TRUE(check_src("fn void eat(i32 mut@ own s) {\n    del(s);\n}\n"
                               "fn i32 main() {\n    i32 mut@ own a = new(i32, 2);\n"
                               "    eat(move(a));\n    return 0;\n}\n"));
    // A borrowing parameter lends instead, and needs no `move`.
    // D17.4
    TEST_ASSERT_TRUE(check_src("fn u64 look(i32@ s) {\n    return s.len;\n}\n"
                               "fn i32 main() {\n    i32 mut@ own a = new(i32, 2);\n"
                               "    println(look(a));\n    del(a);\n    return 0;\n}\n"));
})

TEST(an_own_field_of_a_literal_needs_move, {
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 main() {\n"
                                    "    i32 mut@ own a = new(i32, 2);\n"
                                    "    vec v = vec{a, 2};\n    del(a);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 main() {\n"
                                   "    i32 mut@ own a = new(i32, 2);\n"
                                   "    vec v = vec{move(a), 2};\n    del(v.data);\n"
                                   "    return 0;\n}\n"));
})

TEST(an_own_element_of_an_array_literal_needs_move, {
    TEST_ASSERT_FALSE(check_src(NODE "fn i32 main() {\n"
                                     "    node mut* own a = new(node);\n"
                                     "    node mut* own[1] t = {a};\n    del(a);\n"
                                     "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
    TEST_ASSERT_TRUE(check_src(NODE "fn i32 main() {\n"
                                    "    node mut* own a = new(node);\n"
                                    "    node mut* own[1] t = {move(a)};\n    del(t[0]);\n"
                                    "    return 0;\n}\n"));
})

TEST(an_own_rvalue_lands_in_an_own_place_as_it_is, {
    // `new`, a call result, `move` and a `cast` to an `own` type all flow into
    // an `own` place without a `move` of their own.
    // D17.5
    TEST_ASSERT_TRUE(check_src("fn i32 mut@ own make(u64 n) {\n    return new(i32, n);\n}\n"
                               "fn void eat(i32 mut@ own s) {\n    del(s);\n}\n"
                               "fn i32 main() {\n    i32 mut@ own a = make(2);\n"
                               "    eat(new(i32, 1));\n    eat(make(3));\n    eat(move(a));\n"
                               "    u8 mut@ own b = new(u8, 1);\n"
                               "    string own s = cast(move(b), string own);\n"
                               "    del(s);\n    return 0;\n}\n"));
})

// ---- the implicit move of a `return` ------------------------------------------------
// D17.5, D17.7

TEST(returning_a_bare_own_local_is_an_implicit_move, {
    TEST_ASSERT_TRUE(check_src("fn i32 mut@ own make() {\n"
                               "    i32 mut@ own buf = new(i32, 2);\n    return buf;\n}\n"
                               "fn i32 main() {\n    i32 mut@ own b = make();\n"
                               "    del(b);\n    return 0;\n}\n"));
    // The checker records the implicit move for the emitter.
    // D17.5
    const ast_node_t* ret = node_in_main(AST_RETURN, NULL);
    TEST_ASSERT_TRUE(ret != NULL && (ret->ann & CHECK_ANN_MOVE) != 0);
})

TEST(returning_an_own_parameter_is_an_implicit_move, {
    TEST_ASSERT_TRUE(check_src("fn i32 mut@ own pass(i32 mut@ own s) {\n    return s;\n}\n"
                               "fn i32 main() {\n    i32 mut@ own b = pass(new(i32, 2));\n"
                               "    del(b);\n    return 0;\n}\n"));
})

TEST(returning_a_local_owning_aggregate_is_an_implicit_move, {
    TEST_ASSERT_TRUE(check_src(VEC "fn vec make() {\n    vec mut v = {};\n"
                                   "    v.data = new(i32, 2);\n    return v;\n}\n"
                                   "fn i32 main() {\n    vec v = make();\n"
                                   "    del(v.data);\n    return 0;\n}\n"));
})

TEST(returning_an_owning_lvalue_that_is_not_a_local_needs_move, {
    // A field is not a bare local, so the transfer is written.
    // D17.5
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 mut@ own take(vec mut* v) {\n"
                                    "    return v->data;\n}\n"
                                    "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move'"));
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 mut@ own take(vec mut* v) {\n"
                                   "    return move(v->data);\n}\n"
                                   "fn i32 main() {\n    return 0;\n}\n"));
    // Neither is an element of a local array.
    // D17.5
    TEST_ASSERT_FALSE(check_src(NODE "fn node mut* own first() {\n"
                                     "    node mut* own[2] t = {};\n    return t[0];\n}\n"
                                     "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move'"));
})

TEST(every_annotation_bit_is_cleared_before_a_second_check, {
    // The checker owns the `ann` bits and clears them before it writes them,
    // so a module checked twice starts from the tree the parser left. Nothing
    // else clears `ann`, so a bit left out of the mask would survive a
    // re-check: the test sets every one of them on a `return` the checker
    // marks with none and asserts that the second check wipes them.
    // D20.2
    TEST_ASSERT_TRUE(check_src("fn i32 pass(i32 n) {\n    return n;\n}\n"
                               "fn i32 main() {\n    return pass(1);\n}\n"));
    ast_node_t* ret = node_in_main(AST_RETURN, NULL);
    TEST_ASSERT_NONNULL(ret);
    TEST_ASSERT_EQ_UINT64((uint64_t)ret->ann, (uint64_t)0);
    ret->ann = CHECK_ANN_ALL;
    const module_t* m = module_at("main");
    TEST_ASSERT_NONNULL(m);
    TEST_ASSERT_TRUE(check_module(&checker, m));
    TEST_ASSERT_EQ_UINT64((uint64_t)ret->ann, (uint64_t)0);
    // The sentinel covers the highest bit declared: a new one above
    // CHECK_ANN_MOVE that does not double it is left out of the mask above,
    // which is the shape of the bug this test was written for.
    TEST_ASSERT_EQ_UINT64((uint64_t)CHECK_ANN_END, (uint64_t)CHECK_ANN_MOVE * 2);
})

TEST(the_implicit_move_mark_survives_a_second_check_of_the_same_tree, {
    TEST_ASSERT_TRUE(check_src("fn i32 mut@ own make() {\n"
                               "    i32 mut@ own buf = new(i32, 2);\n    return buf;\n}\n"
                               "fn i32 main() {\n    i32 mut@ own b = make();\n"
                               "    del(b);\n    return 0;\n}\n"));
    const module_t* m = module_at("main");
    TEST_ASSERT_NONNULL(m);
    TEST_ASSERT_TRUE(check_module(&checker, m));
    // Cleared and written again, so the emitter reads the second pass's answer
    // and not the first's.
    // D17.5, D20.2
    const ast_node_t* ret = node_in_main(AST_RETURN, NULL);
    TEST_ASSERT_TRUE(ret != NULL && (ret->ann & CHECK_ANN_MOVE) != 0);
})

TEST(a_returned_local_that_owns_nothing_carries_no_move, {
    TEST_ASSERT_TRUE(check_src("fn i32 mut* pass(i32 mut* p) {\n    return p;\n}\n"
                               "fn i32 main() {\n    i32 mut v = 1;\n"
                               "    println(*pass(&v));\n    return 0;\n}\n"));
    const ast_node_t* ret = node_in_main(AST_RETURN, NULL);
    TEST_ASSERT_TRUE(ret != NULL && (ret->ann & CHECK_ANN_MOVE) == 0);
})

// ---- what `move` may empty ----------------------------------------------------------
// D17.6

TEST(move_needs_no_mut_on_the_binding, {
    // Emptying is not an assignment.
    // D17.6
    TEST_ASSERT_TRUE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                "    i32 mut@ own b = move(a);\n    del(b);"));
})

TEST(move_through_an_immutable_indirection_is_refused, {
    TEST_ASSERT_FALSE(check_src(NODE "fn node mut* own take(node* n) {\n"
                                     "    return move(n->next);\n}\n"
                                     "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'move' cannot empty an immutable indirection"));
    // Level 1 mutable: the same move is allowed.
    // D17.6
    TEST_ASSERT_TRUE(check_src(NODE "fn node mut* own take(node mut* n) {\n"
                                    "    return move(n->next);\n}\n"
                                    "fn i32 main() {\n    return 0;\n}\n"));
})

TEST(move_out_of_an_immutable_span_slot_is_refused, {
    TEST_ASSERT_FALSE(check_src(NODE "fn node mut* own take(node mut* own@ v) {\n"
                                     "    return move(v[0]);\n}\n"
                                     "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'move' cannot empty an immutable indirection"));
    TEST_ASSERT_TRUE(check_src(NODE "fn node mut* own take(node mut* own mut@ v) {\n"
                                    "    return move(v[0]);\n}\n"
                                    "fn i32 main() {\n    return 0;\n}\n"));
})

TEST(move_through_an_immutable_pointer_is_refused, {
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 mut@ own take(vec* v) {\n"
                                    "    return move(v->data);\n}\n"
                                    "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'move' cannot empty an immutable indirection"));
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 mut@ own take(vec mut* v) {\n"
                                   "    return move(v->data);\n}\n"
                                   "fn i32 main() {\n    return 0;\n}\n"));
})

TEST(a_field_or_element_of_a_local_counts_as_the_local, {
    // The local itself need not be mut for its storage to be emptied.
    // D17.6
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 main() {\n    vec v = {};\n"
                                   "    i32 mut@ own d = move(v.data);\n"
                                   "    del(d);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(check_src(NODE "fn i32 main() {\n"
                                    "    node mut* own[2] t = {};\n"
                                    "    node mut* own e = move(t[0]);\n    del(e);\n"
                                    "    return 0;\n}\n"));
    // A dereference of a mutable pointer to a local reaches it too.
    // D17.6
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 main() {\n    vec mut v = {};\n"
                                   "    vec mut* p = &v;\n"
                                   "    i32 mut@ own d = move(p->data);\n"
                                   "    del(d);\n    return 0;\n}\n"));
})

TEST(move_and_del_of_a_module_level_constant_are_errors, {
    TEST_ASSERT_FALSE(check_src("i32* own P = null;\n"
                                "fn i32 main() {\n    i32* own q = move(P);\n"
                                "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'move' cannot empty a module-level constant"));
    TEST_ASSERT_FALSE(check_src("i32* own P = null;\n"
                                "fn i32 main() {\n    del(P);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'del' cannot empty a module-level constant"));
    // A member of one is read-only memory too, and the reason it is refused
    // for is its own and not its field's.
    // D7.10, D17.6
    TEST_ASSERT_FALSE(check_src(VEC "vec C = {};\n"
                                    "fn i32 main() {\n    del(C.data);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'del' cannot empty a module-level constant"));
    // The read-only memory stops at the first indirection: what a pointer in a
    // constant reaches is not the constant's storage.
    // D17.6
    TEST_ASSERT_TRUE(check_src(VEC "vec mut G = {};\n"
                                   "fn i32 main() {\n    G.data = new(i32, 2);\n"
                                   "    del(G.data);\n    return 0;\n}\n"));
})

TEST(the_move_hint_is_offered_for_a_name_and_for_nothing_else, {
    // The hint spells a whole expression, so `move(data)` for `v->data` would
    // name nothing in scope.
    // D14.2
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 mut@ own steal(vec mut* v) {\n"
                                    "    return v->data;\n}\n"
                                    "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move'"));
    TEST_ASSERT_FALSE(said("write move("));
})

TEST(a_failed_move_does_not_also_report_that_it_has_no_value, {
    // `move` is the one universe function with a value, so a failure of its
    // own must poison the result rather than leave it `void`.
    // D12.2, D14.2
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own t = move(new(i32, 2));\n    del(t);"));
    TEST_ASSERT_TRUE(said("'move' takes an lvalue"));
    TEST_ASSERT_FALSE(said("has no value"));
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    i32 y = move(x);\n    println(y);"));
    TEST_ASSERT_FALSE(said("has no value"));
})

TEST(move_needs_an_owning_lvalue, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    i32 y = move(x);\n    println(y);"));
    TEST_ASSERT_TRUE(said("'move' needs an owning operand, not i32"));
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                 "    i32@ v = a;\n    i32@ own t = move(v);\n    del(a);"));
    TEST_ASSERT_TRUE(said("'move' needs an owning operand, not i32@"));
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own t = move(new(i32, 2));\n    del(t);"));
    TEST_ASSERT_TRUE(said("'move' takes an lvalue"));
})

// ---- what `del` may empty -----------------------------------------------------------
// D17.9

TEST(del_through_an_immutable_indirection_is_refused, {
    TEST_ASSERT_FALSE(check_src(NODE "fn void drop(node* n) {\n    del(n->next);\n}\n"
                                     "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'del' cannot empty an immutable indirection"));
    TEST_ASSERT_TRUE(check_src(NODE "fn void drop(node mut* n) {\n    del(n->next);\n}\n"
                                    "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src(NODE "fn void drop(node mut* own@ v) {\n    del(v[0]);\n}\n"
                                     "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'del' cannot empty an immutable indirection"));
})

TEST(del_of_an_rvalue_empties_nothing_and_is_never_refused, {
    // There is no storage to empty, so no level has to be mutable.
    // D17.9
    TEST_ASSERT_TRUE(check_src(NODE "fn node mut* own make() {\n    return new(node);\n}\n"
                                    "fn i32 main() {\n    del(make());\n"
                                    "    del(new(i32, 4));\n    return 0;\n}\n"));
})

// ---- owning temporaries must land ---------------------------------------------------
// D17.8

TEST(an_own_rvalue_may_not_be_lent, {
    TEST_ASSERT_FALSE(check_body("    i32 mut@ v = new(i32, 2);\n    println(v.len);"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: the initializer expects i32 mut@"));
    TEST_ASSERT_FALSE(check_src("fn u64 look(i32@ s) {\n    return s.len;\n}\n"
                                "fn i32 main() {\n    println(look(new(i32, 2)));\n"
                                "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: the argument expects i32@"));
})

TEST(an_own_rvalue_may_not_be_cast_to_a_type_that_does_not_own, {
    TEST_ASSERT_FALSE(check_src(NODE "fn i32 main() {\n"
                                     "    node* p = cast(new(node), node*);\n"
                                     "    println(p == null);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: the cast target does not own it"));
    // A cast to an `own` target keeps it owned, so it lands.
    // D3.14, D17.3
    TEST_ASSERT_TRUE(check_src(NODE "fn i32 main() {\n"
                                    "    void* own p = cast(new(node), void* own);\n"
                                    "    del(p);\n    return 0;\n}\n"));
})

TEST(an_own_rvalue_may_not_be_indexed_spanned_or_read_through, {
    TEST_ASSERT_FALSE(check_body("    i32 x = new(i32, 2)[0];\n    println(x);"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: indexing it leaves no owner"));
    TEST_ASSERT_FALSE(check_body("    u8@ v = new(u8, 8)[..4];\n    println(v.len);"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: a span of it leaves no owner"));
    TEST_ASSERT_FALSE(check_body("    u8* q = new(u8, 8).ptr;\n    println(q == null);"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: a field of it leaves no owner"));
    TEST_ASSERT_FALSE(check_src(NODE "fn i32 main() {\n"
                                     "    println(new(node)->value);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: a field of it leaves no owner"));
    TEST_ASSERT_FALSE(check_src(NODE "fn i32 main() {\n"
                                     "    println((*new(node)).value);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: dereferencing it leaves no owner"));
})

TEST(a_field_of_an_owning_aggregate_rvalue_is_refused, {
    TEST_ASSERT_FALSE(check_src(VEC "fn vec make() {\n"
                                    "    return vec{new(i32, 4), 4};\n}\n"
                                    "fn i32 main() {\n    println(make().len);\n"
                                    "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: a field of it leaves no owner"));
})

TEST(an_own_rvalue_may_not_be_printed_or_compared, {
    // No context is no `own` place, so the value could never be freed.
    // D4.5, D17.8
    TEST_ASSERT_FALSE(check_body("    println(new(i32, 2).len > 0);\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak"));
    TEST_ASSERT_FALSE(check_src(NODE "fn node mut* own make() {\n    return new(node);\n}\n"
                                     "fn i32 main() {\n    println(make() == null);\n"
                                     "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: comparing it leaves no owner"));
    // An owning lvalue compares freely: the operands lend.
    // D6.2, D17.4
    TEST_ASSERT_TRUE(check_src(NODE "fn i32 main() {\n"
                                    "    node mut* own p = new(node);\n"
                                    "    println(p == null);\n    del(p);\n    return 0;\n}\n"));
})

TEST(an_own_rvalue_may_not_be_discarded_or_iterated, {
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own a = new(i32, 2);\n    move(a);\n    del(a);"));
    TEST_ASSERT_TRUE(said("owning temporary would leak"));
    TEST_ASSERT_FALSE(check_body("    for (i32 v : new(i32, 2)) {\n        println(v);\n    }"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: the loop only lends its collection"));
})

TEST(an_own_rvalue_lands_in_del_and_in_an_own_place, {
    TEST_ASSERT_TRUE(check_body("    del(new(i32, 2));\n    i32 mut@ own a = new(i32, 2);\n"
                                "    del(a);"));
})

// ---- owning aggregates --------------------------------------------------------------
// D17.7

TEST(copying_an_owning_aggregate_needs_move, {
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 main() {\n    vec mut a = {};\n"
                                    "    a.data = new(i32, 2);\n    vec b = a;\n"
                                    "    del(a.data);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 main() {\n    vec mut a = {};\n"
                                   "    a.data = new(i32, 2);\n    vec b = move(a);\n"
                                   "    del(b.data);\n    return 0;\n}\n"));
})

TEST(a_by_value_owning_parameter_needs_move, {
    TEST_ASSERT_FALSE(check_src(VEC "fn void eat(vec v) {\n    del(v.data);\n}\n"
                                    "fn i32 main() {\n    vec mut a = {};\n"
                                    "    a.data = new(i32, 2);\n    eat(a);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
})

TEST(an_array_of_owning_elements_is_an_owning_aggregate, {
    TEST_ASSERT_FALSE(check_src(NODE "fn i32 main() {\n"
                                     "    node mut* own[2] mut a = {};\n"
                                     "    a[0] = new(node);\n"
                                     "    node mut* own[2] b = a;\n    del(a[0]);\n"
                                     "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
})

TEST(a_struct_that_owns_nothing_copies_freely, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "fn i32 main() {\n    point a = {1, 2};\n    point b = a;\n"
                               "    println(b.x);\n    return 0;\n}\n"));
})

TEST(del_of_an_aggregate_is_an_error, {
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 main() {\n    vec mut a = {};\n"
                                    "    a.data = new(i32, 2);\n    del(a);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'del' takes a reference, not vec"));
})

TEST(a_nested_owning_aggregate_is_owning_too, {
    // A struct that holds an owning struct by value owns through it.
    // D17.7
    TEST_ASSERT_FALSE(check_src(OUTER "fn i32 main() {\n    outer mut a = {};\n"
                                      "    a.in.data = new(i32, 2);\n    outer b = a;\n"
                                      "    del(a.in.data);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
    TEST_ASSERT_TRUE(check_src(OUTER "fn i32 main() {\n    outer mut a = {};\n"
                                     "    a.in.data = new(i32, 2);\n    outer b = move(a);\n"
                                     "    del(b.in.data);\n    return 0;\n}\n"));
    // A struct of the same shape with no `own` anywhere copies freely.
    // D17.7
    TEST_ASSERT_TRUE(check_src("struct inner {\n    i32 mut@ data;\n}\n"
                               "struct outer {\n    inner in;\n    u64 n;\n}\n"
                               "fn i32 main() {\n    outer a = {};\n    outer b = a;\n"
                               "    println(b.n);\n    return 0;\n}\n"));
})

TEST(a_designated_field_is_an_own_place_like_a_positional_one, {
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 main() {\n"
                                    "    i32 mut@ own a = new(i32, 2);\n"
                                    "    vec v = vec{.data = a};\n    del(a);\n"
                                    "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("copying an owning value requires 'move': write move(a)"));
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 main() {\n"
                                   "    i32 mut@ own a = new(i32, 2);\n"
                                   "    vec v = vec{.data = move(a)};\n    del(v.data);\n"
                                   "    return 0;\n}\n"));
    // An `own` rvalue enters a designated field as it is.
    // D17.5
    TEST_ASSERT_TRUE(check_src(VEC "fn i32 main() {\n"
                                   "    vec v = vec{.data = new(i32, 2)};\n"
                                   "    del(v.data);\n    return 0;\n}\n"));
})

TEST(a_whole_owning_aggregate_moves_through_a_mutable_pointer, {
    TEST_ASSERT_TRUE(check_src(VEC "fn vec steal(vec mut* v) {\n    return move(*v);\n}\n"
                                   "fn i32 main() {\n    vec mut a = {};\n"
                                   "    a.data = new(i32, 2);\n    vec b = steal(&a);\n"
                                   "    del(b.data);\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src(VEC "fn vec steal(vec* v) {\n    return move(*v);\n}\n"
                                    "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'move' cannot empty an immutable indirection"));
})

TEST(moving_a_zero_value_is_allowed, {
    // Moving a zero value yields a zero value; nothing about it is special.
    // D17.6
    TEST_ASSERT_TRUE(check_body("    i32 mut@ own z = {};\n"
                                "    i32 mut@ own y = move(z);\n    del(y);\n    del(z);"));
    TEST_ASSERT_TRUE(check_src("fn i32 main() {\n    i32 mut* own p = null;\n"
                               "    i32 mut* own q = move(p);\n    del(q);\n"
                               "    return 0;\n}\n"));
})

// ---- loops lend ---------------------------------------------------------------------
// D17.10

TEST(a_range_loop_lends_its_owning_collection, {
    TEST_ASSERT_TRUE(check_src(NODE "fn i32 main() {\n"
                                    "    node mut* own[2] mut kids = {};\n"
                                    "    for (node mut* c : kids) {\n"
                                    "        println(c == null);\n    }\n"
                                    "    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src(NODE "fn i32 main() {\n"
                                     "    node mut* own[2] mut kids = {};\n"
                                     "    for (node mut* own c : kids) {\n"
                                     "        println(c == null);\n    }\n"
                                     "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("a range variable cannot be own"));
})

TEST(a_range_loop_over_owning_aggregates_is_refused, {
    TEST_ASSERT_FALSE(check_src(VEC "fn i32 main() {\n    vec[2] v = {};\n"
                                    "    for (vec e : v) {\n        println(e.len);\n    }\n"
                                    "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("the elements are owning: iterate by index"));
})

// ---- strings and externs ------------------------------------------------------------
// D17.12, D17.13

TEST(a_string_own_is_built_by_casting_a_moved_buffer, {
    TEST_ASSERT_TRUE(check_src("fn string own build() {\n"
                               "    u8 mut@ own buf = new(u8, 2);\n"
                               "    return cast(move(buf), string own);\n}\n"
                               "fn i32 main() {\n    string own s = build();\n"
                               "    del(s);\n    return 0;\n}\n"));
    // The target says `own`, so the source is moved, not lent.
    // D17.5, D17.12
    TEST_ASSERT_FALSE(check_src("fn i32 main() {\n"
                                "    u8 mut@ own buf = new(u8, 2);\n"
                                "    string own s = cast(buf, string own);\n"
                                "    del(s);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("casting an owning value to an owning type requires 'move': write "
                          "move(buf)"));
    // `cast(buf, string)` lends a view instead.
    // D17.12
    TEST_ASSERT_TRUE(check_src("fn i32 main() {\n"
                               "    u8 mut@ own buf = new(u8, 2);\n"
                               "    string v = cast(buf, string);\n"
                               "    println(v.len);\n    del(buf);\n    return 0;\n}\n"));
})

TEST(own_in_an_extern_signature_is_part_of_the_type, {
    // `own` documents the C side's convention and is erased, but it is part of
    // the parameter type, so a view does not pass.
    // D17.13, D9.8, D17.4
    TEST_ASSERT_TRUE(check_src("extern fn void free(void* own p);\n"
                               "fn i32 main() {\n"
                               "    i32 mut* own p = new(i32);\n"
                               "    free(cast(move(p), void* own));\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("extern fn void free(void* own p);\n"
                                "fn i32 main() {\n"
                                "    i32 mut* own p = new(i32);\n"
                                "    free(cast(p, void*));\n    del(p);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("the argument expects void* own, not void*"));
})

TEST(an_extern_result_that_says_own_is_an_owning_rvalue, {
    TEST_ASSERT_TRUE(check_src("extern fn void* own malloc(u64 n);\n"
                               "fn i32 main() {\n"
                               "    u8 mut* own p = cast(malloc(1), u8 mut* own);\n"
                               "    del(p);\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("extern fn void* own malloc(u64 n);\n"
                                "fn i32 main() {\n    void* p = malloc(1);\n"
                                "    println(p == null);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: the initializer expects void*"));
})

// ---- producers yield views ----------------------------------------------------------
// D17.3

TEST(a_span_expression_a_ptr_and_an_address_are_never_own, {
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                 "    i32 mut@ own b = a[..];\n    del(a);"));
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut@ own, not i32 mut@"));
    TEST_ASSERT_FALSE(check_body("    i32 mut@ own a = new(i32, 2);\n"
                                 "    i32 mut* own p = a.ptr;\n    del(a);"));
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut* own, not i32 mut*"));
    TEST_ASSERT_FALSE(check_body("    i32 mut v = 1;\n    i32 mut* own p = &v;\n"
                                 "    println(*p);"));
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut* own, not i32 mut*"));
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_own", argc, argv);
    TEST_RUN(copying_an_owning_lvalue_into_a_declaration_needs_move);
    TEST_RUN(copying_an_owning_lvalue_into_an_assignment_needs_move);
    TEST_RUN(passing_an_owning_lvalue_to_an_own_parameter_needs_move);
    TEST_RUN(an_own_field_of_a_literal_needs_move);
    TEST_RUN(an_own_element_of_an_array_literal_needs_move);
    TEST_RUN(an_own_rvalue_lands_in_an_own_place_as_it_is);
    TEST_RUN(returning_a_bare_own_local_is_an_implicit_move);
    TEST_RUN(returning_an_own_parameter_is_an_implicit_move);
    TEST_RUN(returning_a_local_owning_aggregate_is_an_implicit_move);
    TEST_RUN(returning_an_owning_lvalue_that_is_not_a_local_needs_move);
    TEST_RUN(every_annotation_bit_is_cleared_before_a_second_check);
    TEST_RUN(the_implicit_move_mark_survives_a_second_check_of_the_same_tree);
    TEST_RUN(a_returned_local_that_owns_nothing_carries_no_move);
    TEST_RUN(move_needs_no_mut_on_the_binding);
    TEST_RUN(move_through_an_immutable_indirection_is_refused);
    TEST_RUN(move_out_of_an_immutable_span_slot_is_refused);
    TEST_RUN(move_through_an_immutable_pointer_is_refused);
    TEST_RUN(a_field_or_element_of_a_local_counts_as_the_local);
    TEST_RUN(move_and_del_of_a_module_level_constant_are_errors);
    TEST_RUN(the_move_hint_is_offered_for_a_name_and_for_nothing_else);
    TEST_RUN(a_failed_move_does_not_also_report_that_it_has_no_value);
    TEST_RUN(move_needs_an_owning_lvalue);
    TEST_RUN(del_through_an_immutable_indirection_is_refused);
    TEST_RUN(del_of_an_rvalue_empties_nothing_and_is_never_refused);
    TEST_RUN(an_own_rvalue_may_not_be_lent);
    TEST_RUN(an_own_rvalue_may_not_be_cast_to_a_type_that_does_not_own);
    TEST_RUN(an_own_rvalue_may_not_be_indexed_spanned_or_read_through);
    TEST_RUN(a_field_of_an_owning_aggregate_rvalue_is_refused);
    TEST_RUN(an_own_rvalue_may_not_be_printed_or_compared);
    TEST_RUN(an_own_rvalue_may_not_be_discarded_or_iterated);
    TEST_RUN(an_own_rvalue_lands_in_del_and_in_an_own_place);
    TEST_RUN(copying_an_owning_aggregate_needs_move);
    TEST_RUN(a_by_value_owning_parameter_needs_move);
    TEST_RUN(an_array_of_owning_elements_is_an_owning_aggregate);
    TEST_RUN(a_struct_that_owns_nothing_copies_freely);
    TEST_RUN(del_of_an_aggregate_is_an_error);
    TEST_RUN(a_nested_owning_aggregate_is_owning_too);
    TEST_RUN(a_designated_field_is_an_own_place_like_a_positional_one);
    TEST_RUN(a_whole_owning_aggregate_moves_through_a_mutable_pointer);
    TEST_RUN(moving_a_zero_value_is_allowed);
    TEST_RUN(a_range_loop_lends_its_owning_collection);
    TEST_RUN(a_range_loop_over_owning_aggregates_is_refused);
    TEST_RUN(a_string_own_is_built_by_casting_a_moved_buffer);
    TEST_RUN(own_in_an_extern_signature_is_part_of_the_type);
    TEST_RUN(an_extern_result_that_says_own_is_an_owning_rvalue);
    TEST_RUN(a_span_expression_a_ptr_and_an_address_are_never_own);
    check_reset();
    done();
    TEST_EXIT();
}
