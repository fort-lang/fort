// The type grammar of the parser (grammar.md 4): the base types, the marker
// positions of the two tables, the reading order, function types and every
// spelling those rules call an error.
// D5.3, D17.2, D3.6, D3.10
#include <stdint.h>

#include "ast.h"
#include "parser.h"
#include "parser_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources and the trees they parse
// to are the test data.

// ---- base types (grammar.md 4) --------------------------------------------

TEST(every_primitive_is_a_base_type, {
    TEST_ASSERT_EQ_STR(dump_type("i8"), "(type (prim i8))");
    TEST_ASSERT_EQ_STR(dump_type("i16"), "(type (prim i16))");
    TEST_ASSERT_EQ_STR(dump_type("i32"), "(type (prim i32))");
    TEST_ASSERT_EQ_STR(dump_type("i64"), "(type (prim i64))");
    TEST_ASSERT_EQ_STR(dump_type("u8"), "(type (prim u8))");
    TEST_ASSERT_EQ_STR(dump_type("u16"), "(type (prim u16))");
    TEST_ASSERT_EQ_STR(dump_type("u32"), "(type (prim u32))");
    TEST_ASSERT_EQ_STR(dump_type("u64"), "(type (prim u64))");
    TEST_ASSERT_EQ_STR(dump_type("f32"), "(type (prim f32))");
    TEST_ASSERT_EQ_STR(dump_type("f64"), "(type (prim f64))");
    TEST_ASSERT_EQ_STR(dump_type("bool"), "(type (prim bool))");
    TEST_ASSERT_EQ_STR(dump_type("char"), "(type (prim char))");
})

TEST(string_void_and_names_are_base_types, {
    TEST_ASSERT_EQ_STR(dump_type("string"), "(type (string))");
    TEST_ASSERT_EQ_STR(dump_type("void*"), "(type (void) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("node"), "(type (name node))");
    TEST_ASSERT_EQ_STR(dump_type("math.vector"), "(type (name math vector))");
})

// `void` is a base type here; that it needs a `*` is the type builder's rule,
// not the parser's.
// D3.11
TEST(void_parses_as_a_base_type_on_its_own,
     { TEST_ASSERT_EQ_STR(dump_type("void"), "(type (void))"); })

// ---- the placement table --------------------------------------------------
// D5.3

TEST(d5_3_table_scalars_and_arrays, {
    TEST_ASSERT_EQ_STR(dump_type("i32 mut"), "(type (prim i32) mut)");
    TEST_ASSERT_EQ_STR(dump_type("point mut"), "(type (name point) mut)");
    TEST_ASSERT_EQ_STR(dump_type("i32[4] mut"), "(type (prim i32) (array (int 4) mut))");
    TEST_ASSERT_EQ_STR(dump_type("string mut"), "(type (string) mut)");
})

TEST(d5_3_table_pointers, {
    TEST_ASSERT_EQ_STR(dump_type("node*"), "(type (name node) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("node* mut"), "(type (name node) (ptr mut))");
    TEST_ASSERT_EQ_STR(dump_type("node mut*"), "(type (name node) mut (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("node mut* mut"), "(type (name node) mut (ptr mut))");
    TEST_ASSERT_EQ_STR(dump_type("node* mut*"), "(type (name node) (ptr mut) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("node mut**"), "(type (name node) mut (ptr) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("void* mut"), "(type (void) (ptr mut))");
})

TEST(d5_3_table_spans, {
    TEST_ASSERT_EQ_STR(dump_type("i32@"), "(type (prim i32) (span))");
    TEST_ASSERT_EQ_STR(dump_type("i32@ mut"), "(type (prim i32) (span mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32 mut@"), "(type (prim i32) mut (span))");
    TEST_ASSERT_EQ_STR(dump_type("node* mut@"), "(type (name node) (ptr mut) (span))");
    TEST_ASSERT_EQ_STR(dump_type("node mut*@"), "(type (name node) mut (ptr) (span))");
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ mut*"), "(type (prim u8) mut (span mut) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("u8@ mut*"), "(type (prim u8) (span mut) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("string@"), "(type (string) (span))");
})

TEST(d5_3_table_arrays_of_references, {
    TEST_ASSERT_EQ_STR(dump_type("node*[4] mut"), "(type (name node) (ptr) (array (int 4) mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32[4]*"), "(type (prim i32) (array (int 4)) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("i32[4]* mut"), "(type (prim i32) (array (int 4)) (ptr mut))");
})

// ---- the placement table --------------------------------------------------
// D17.2

TEST(d17_2_table_owning_references, {
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ own"), "(type (prim u8) mut (span own))");
    TEST_ASSERT_EQ_STR(dump_type("u8@ own"), "(type (prim u8) (span own))");
    TEST_ASSERT_EQ_STR(dump_type("node mut* own"), "(type (name node) mut (ptr own))");
    TEST_ASSERT_EQ_STR(dump_type("node* mut@ own"), "(type (name node) (ptr mut) (span own))");
    TEST_ASSERT_EQ_STR(dump_type("node mut* own mut@ own"),
                       "(type (name node) mut (ptr own mut) (span own))");
    TEST_ASSERT_EQ_STR(dump_type("node* own@"), "(type (name node) (ptr own) (span))");
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ own mut*"), "(type (prim u8) mut (span own mut) (ptr))");
})

// `string` is the reference with no suffix, so it takes an `own` directly,
// and an `own` before a fixed-array suffix marks the elements.
// D17.2
TEST(d17_2_own_on_string_and_before_an_array, {
    TEST_ASSERT_EQ_STR(dump_type("string own"), "(type (string) own)");
    TEST_ASSERT_EQ_STR(dump_type("string own mut"), "(type (string) own mut)");
    TEST_ASSERT_EQ_STR(dump_type("node* own[4]"), "(type (name node) (ptr own) (array (int 4)))");
    TEST_ASSERT_EQ_STR(dump_type("node* own[4] mut"),
                       "(type (name node) (ptr own) (array (int 4) mut))");
    TEST_ASSERT_EQ_STR(dump_type("void* own"), "(type (void) (ptr own))");
})

// ---- function types -------------------------------------------------------
// D3.10

TEST(function_types_read_as_a_base_type, {
    TEST_ASSERT_EQ_STR(dump_type("fn void()"), "(type (fn-type (type (void))))");
    TEST_ASSERT_EQ_STR(dump_type("fn i32(i32)"),
                       "(type (fn-type (type (prim i32)) (type (prim i32))))");
    TEST_ASSERT_EQ_STR(dump_type("fn i32(i32, i32)"),
                       "(type (fn-type (type (prim i32)) (type (prim i32)) (type (prim i32))))");
    TEST_ASSERT_EQ_STR(dump_type("fn noreturn(string)"),
                       "(type (fn-type (type (noreturn)) (type (string))))");
})

// Suffixes after a function type apply to the function type.
// D3.6
TEST(function_types_take_suffixes_and_markers, {
    TEST_ASSERT_EQ_STR(dump_type("fn i32(i32)[4]"),
                       "(type (fn-type (type (prim i32)) (type (prim i32))) (array (int 4)))");
    TEST_ASSERT_EQ_STR(dump_type("fn i32[4](i32)"),
                       "(type (fn-type (type (prim i32) (array (int 4))) (type (prim i32))))");
    TEST_ASSERT_EQ_STR(dump_type("fn i32(i32) mut"),
                       "(type (fn-type (type (prim i32)) (type (prim i32))) mut)");
    TEST_ASSERT_EQ_STR(dump_type("fn void(node mut*)"),
                       "(type (fn-type (type (void)) (type (name node) mut (ptr))))");
    TEST_ASSERT_EQ_STR(dump_type("fn i32(i32)*"),
                       "(type (fn-type (type (prim i32)) (type (prim i32))) (ptr))");
})

// ---- the out-parameter shape ----------------------------------------------
// D3.6

TEST(the_out_parameter_shape_of_d3_6, {
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ own mut*"), "(type (prim u8) mut (span own mut) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("u8@* mut"), "(type (prim u8) (span) (ptr mut))");
})

// ---- what the placement rules forbid --------------------------------------

// Nothing precedes the base type.
// D5.3, D17.2
TEST(a_marker_before_the_base_type_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("mut i32"),
                       "t.ft:1:1: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
    TEST_ASSERT_EQ_STR(type_fails("own node*"),
                       "t.ft:1:1: error: an own never precedes the base type: "
                       "write 'node* own p' or 'string own s'\n");
    TEST_ASSERT_EQ_STR(type_fails("mut node*"),
                       "t.ft:1:1: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
})

// Each storage level has exactly one position, so a doubled marker does not
// parse.
// D5.3
TEST(a_doubled_marker_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("i32 mut mut"),
                       "t.ft:1:9: error: a mut appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("i32 mut* mut mut"),
                       "t.ft:1:14: error: a mut appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("i32 mut* own own"),
                       "t.ft:1:14: error: an own appears once in a type position\n");
})

// An `own` precedes the `mut` of its position.
// D17.2
TEST(an_own_after_the_mut_of_its_position_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("node* mut own"),
                       "t.ft:1:11: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
    TEST_ASSERT_EQ_STR(type_fails("string mut own"),
                       "t.ft:1:12: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
})

// An `own` marks a reference: after a base type only `string` takes one, and
// a fixed-array suffix never does.
// D17.1, D17.2
TEST(an_own_on_something_that_is_not_a_reference_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("node own*"),
                       "t.ft:1:6: error: an own marks a reference: "
                       "write it after a '*' or an '@', or on a string\n");
    TEST_ASSERT_EQ_STR(type_fails("i32 own"),
                       "t.ft:1:5: error: an own marks a reference: "
                       "write it after a '*' or an '@', or on a string\n");
    TEST_ASSERT_EQ_STR(type_fails("fn i32(i32) own"),
                       "t.ft:1:13: error: an own marks a reference: "
                       "write it after a '*' or an '@', or on a string\n");
    TEST_ASSERT_EQ_STR(type_fails("node*[4] own"),
                       "t.ft:1:10: error: an own never follows a fixed-array suffix: "
                       "write it after the '*' or '@' it marks\n");
})

// The elements of an array share its storage, so the position a `[N]` follows
// never carries a `mut`.
// D5.3
TEST(a_mut_between_an_element_type_and_its_length_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("i32 mut[4]"),
                       "t.ft:1:5: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
    TEST_ASSERT_EQ_STR(type_fails("node* mut[4]"),
                       "t.ft:1:7: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
})

// No array suffix follows a trailing reference suffix.
// D3.6
TEST(an_array_after_a_reference_suffix_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("i32[4]*[2]"),
                       "t.ft:1:8: error: no array suffix follows a reference suffix: "
                       "wrap the array in a struct\n");
})

TEST(a_type_that_is_not_a_type, {
    TEST_ASSERT_EQ_STR(type_fails("noreturn"),
                       "t.ft:1:1: error: expected a type, found 'noreturn'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn 1 f() {}"),
                       "t.ft:1:4: error: expected a type, found integer literal\n");
    TEST_ASSERT_EQ_STR(type_fails("node[]"),
                       "t.ft:1:6: error: expected an expression, found ']'\n");
})

// ---- one array or span level (toolchain.md 7.3) --------------------------

TEST(a_second_array_or_span_level_is_not_supported, {
    TEST_ASSERT_EQ_STR(
        type_fails("i32[3][4]"),
        "t.ft:1:7: error: not supported by the bootstrap compiler: multi-dimensional arrays\n");
    TEST_ASSERT_EQ_STR(
        type_fails("i32[4]@"),
        "t.ft:1:7: error: not supported by the bootstrap compiler: spans of arrays\n");
    TEST_ASSERT_EQ_STR(
        type_fails("u8@@"),
        "t.ft:1:4: error: not supported by the bootstrap compiler: spans of spans\n");
    TEST_ASSERT_EQ_STR(
        type_fails("node@[4]"),
        "t.ft:1:6: error: not supported by the bootstrap compiler: arrays of spans\n");
})

// One level is supported, and a pointer is not a level of its own.
TEST(one_array_or_span_level_with_pointers_is_supported, {
    TEST_ASSERT_EQ_STR(dump_type("node*[16]"), "(type (name node) (ptr) (array (int 16)))");
    TEST_ASSERT_EQ_STR(dump_type("u8@*"), "(type (prim u8) (span) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("node***"), "(type (name node) (ptr) (ptr) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("string@"), "(type (string) (span))");
})

// The bootstrap's limit is per written type: a function type's parameters
// are types of their own.
TEST(the_level_count_is_per_written_type, {
    TEST_ASSERT_EQ_STR(dump_type("fn void(i32[4], u8@)"),
                       "(type (fn-type (type (void)) (type (prim i32) (array (int 4)))"
                       " (type (prim u8) (span))))");
    TEST_ASSERT_EQ_STR(
        type_fails("fn void(u8@@)"),
        "t.ft:1:12: error: not supported by the bootstrap compiler: spans of spans\n");
})

// ---- inside new -----------------------------------------------------------
// D10.2, D17.3

TEST(new_refuses_a_mut_in_the_outermost_position_a_span_and_a_misplaced_own, {
    TEST_ASSERT_EQ_STR(expr_fails("new(i32 mut)"),
                       "t.ft:1:17: error: new allocates writable storage: "
                       "remove the outermost 'mut'\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(i32@)"),
                       "t.ft:1:16: error: a span suffix does not parse inside new: "
                       "write new(T, n)\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(string own)"),
                       "t.ft:1:20: error: inside new an own follows a '*' of the element type\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(own node)"),
                       "t.ft:1:13: error: an own never precedes the base type: "
                       "write 'node* own p' or 'string own s'\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(i32[4] mut)"),
                       "t.ft:1:20: error: new allocates writable storage: "
                       "remove the outermost 'mut'\n");
    // The elements of a fixed array share its storage, so the position a `[N]`
    // follows carries no `mut` here either, and that rule is what says so.
    // D5.3
    TEST_ASSERT_EQ_STR(expr_fails("new(i32 mut[4], n)"),
                       "t.ft:1:17: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
})

// A `mut` below the outermost position marks storage `new` does not allocate:
// what the fresh pointer would reach, which is null until the program stores
// something there. So it parses, and it is the program's to write.
// D5.8, D10.2
TEST(new_takes_a_mut_below_the_outermost_position, {
    TEST_ASSERT_EQ_STR(dump_expr("new(node mut*)"), "(new (type (name node) mut (ptr)) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("new(node mut*, n)"),
                       "(new (type (name node) mut (ptr)) (ident n))");
    TEST_ASSERT_EQ_STR(dump_expr("new(void mut*, n)"), "(new (type (void) mut (ptr)) (ident n))");
    TEST_ASSERT_EQ_STR(dump_expr("new(node mut* own, n)"),
                       "(new (type (name node) mut (ptr own)) (ident n))");
    TEST_ASSERT_EQ_STR(dump_expr("new(node mut* mut*)"),
                       "(new (type (name node) mut (ptr mut) (ptr)) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("new(node* own mut*)"),
                       "(new (type (name node) (ptr own mut) (ptr)) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("new(node mut*[2])"),
                       "(new (type (name node) mut (ptr) (array (int 2))) nil)");
    // An `own` precedes the `mut` of its position, and each marker appears
    // once.
    // D17.2
    TEST_ASSERT_EQ_STR(expr_fails("new(node* mut own*)"),
                       "t.ft:1:23: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(node mut mut*)"),
                       "t.ft:1:22: error: a mut appears once in a type position\n");
})

// ---- the marker matrix ----------------------------------------------------

// Every marker set a reference suffix can carry, in the order the rule fixes
// (`own` then `mut`).
// D17.2
TEST(every_marker_set_on_a_reference_suffix, {
    TEST_ASSERT_EQ_STR(dump_type("i32*"), "(type (prim i32) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("i32* own"), "(type (prim i32) (ptr own))");
    TEST_ASSERT_EQ_STR(dump_type("i32* mut"), "(type (prim i32) (ptr mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32* own mut"), "(type (prim i32) (ptr own mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32@"), "(type (prim i32) (span))");
    TEST_ASSERT_EQ_STR(dump_type("i32@ own"), "(type (prim i32) (span own))");
    TEST_ASSERT_EQ_STR(dump_type("i32@ mut"), "(type (prim i32) (span mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32@ own mut"), "(type (prim i32) (span own mut))");
})

// The base position takes a `mut` for every base type and an `own` only for
// `string`.
// D5.3, D17.2
TEST(every_marker_set_on_a_base_type, {
    TEST_ASSERT_EQ_STR(dump_type("u8 mut*"), "(type (prim u8) mut (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("char mut@"), "(type (prim char) mut (span))");
    TEST_ASSERT_EQ_STR(dump_type("bool[4] mut"), "(type (prim bool) (array (int 4) mut))");
    TEST_ASSERT_EQ_STR(dump_type("node mut*"), "(type (name node) mut (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("m.t mut*"), "(type (name m t) mut (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("string"), "(type (string))");
    TEST_ASSERT_EQ_STR(dump_type("string own"), "(type (string) own)");
    TEST_ASSERT_EQ_STR(dump_type("string mut"), "(type (string) mut)");
})

// A reference chain reads inside-out, so a marker sticks to the suffix it
// follows however long the chain is.
// D3.6, D5.3
TEST(long_reference_chains_keep_their_markers, {
    TEST_ASSERT_EQ_STR(dump_type("i32****"), "(type (prim i32) (ptr) (ptr) (ptr) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("i32 mut* mut* mut* mut"),
                       "(type (prim i32) mut (ptr mut) (ptr mut) (ptr mut))");
    TEST_ASSERT_EQ_STR(dump_type("node* own* own* own"),
                       "(type (name node) (ptr own) (ptr own) (ptr own))");
    TEST_ASSERT_EQ_STR(dump_type("m.t* mut@ own"), "(type (name m t) (ptr mut) (span own))");
})

// A fixed-array group may sit between the two reference groups, and the group
// after it refers to the whole array.
// D3.6
TEST(the_array_group_between_the_reference_groups, {
    TEST_ASSERT_EQ_STR(dump_type("i32*[2]"), "(type (prim i32) (ptr) (array (int 2)))");
    TEST_ASSERT_EQ_STR(dump_type("i32* own[2]"), "(type (prim i32) (ptr own) (array (int 2)))");
    TEST_ASSERT_EQ_STR(dump_type("i32*[2] mut"), "(type (prim i32) (ptr) (array (int 2) mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32[2]*"), "(type (prim i32) (array (int 2)) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("i32[2]* own mut"),
                       "(type (prim i32) (array (int 2)) (ptr own mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32 mut*[2] mut"),
                       "(type (prim i32) mut (ptr) (array (int 2) mut))");
})

// An array length is a constant expression, which the parser only parses.
// D4.6: the checker requires it to be constant
TEST(an_array_length_is_any_expression_here, {
    TEST_ASSERT_EQ_STR(dump_type("i32[N]"), "(type (prim i32) (array (ident N)))");
    TEST_ASSERT_EQ_STR(dump_type("i32[2 + 2]"),
                       "(type (prim i32) (array (binary + (int 2) (int 2))))");
    TEST_ASSERT_EQ_STR(dump_type("i32[sizeof(u64)]"),
                       "(type (prim i32) (array (sizeof (type (prim u64)))))");
    TEST_ASSERT_EQ_STR(dump_type("i32[m.LIMIT]"),
                       "(type (prim i32) (array (field (ident m) LIMIT)))");
    TEST_ASSERT_EQ_STR(dump_type("i32[0]"), "(type (prim i32) (array (int 0)))");
})

// A function type nests: as a parameter, as a return type and as the base of
// another function type.
// D3.10
TEST(function_types_nest, {
    TEST_ASSERT_EQ_STR(dump_type("fn void(fn i32(i32))"),
                       "(type (fn-type (type (void))"
                       " (type (fn-type (type (prim i32)) (type (prim i32))))))");
    TEST_ASSERT_EQ_STR(dump_type("fn fn void()()"),
                       "(type (fn-type (type (fn-type (type (void))))))");
    TEST_ASSERT_EQ_STR(dump_type("fn u8@(string, i32 mut*)"),
                       "(type (fn-type (type (prim u8) (span)) (type (string))"
                       " (type (prim i32) mut (ptr))))");
    TEST_ASSERT_EQ_STR(dump_type("fn noreturn()"), "(type (fn-type (type (noreturn))))");
})

// A `noreturn` is a return type only.
// D8.5
TEST(noreturn_is_only_a_return_type, {
    TEST_ASSERT_EQ_STR(type_fails("noreturn*"),
                       "t.ft:1:1: error: expected a type, found 'noreturn'\n");
    TEST_ASSERT_EQ_STR(type_fails("fn void(noreturn)"),
                       "t.ft:1:9: error: expected a type, found 'noreturn'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn void f(noreturn n) { }"),
                       "t.ft:1:11: error: expected a type, found 'noreturn'\n");
    TEST_ASSERT_EQ_STR(expr_fails("sizeof(noreturn)"),
                       "t.ft:1:16: error: expected a type, found 'noreturn'\n");
})

// The type of an array literal has no base marker and no marker on a
// dimension (grammar.md 6), so a marked type followed by `{` is not one.
TEST(an_array_literal_type_carries_no_marker, {
    TEST_ASSERT_EQ_STR(dump_expr("i32[2]{1, 2}"),
                       "(array-lit (type (prim i32) (array (int 2))) (init (int 1) (int 2)))");
    TEST_ASSERT_EQ_STR(dump_expr("node* own[2]{a, b}"),
                       "(array-lit (type (name node) (ptr own) (array (int 2)))"
                       " (init (ident a) (ident b)))");
    TEST_ASSERT_EQ_STR(expr_fails("i32 mut[2]{1, 2}"),
                       "t.ft:1:9: error: expected an expression, found 'i32'\n");
    TEST_ASSERT_EQ_STR(expr_fails("i32[2] mut{1, 2}"),
                       "t.ft:1:9: error: expected an expression, found 'i32'\n");
})

// The allocated type of `new` has no `@`, no `mut` in its outermost position
// and an `own` only after a `*`, and its dimensions carry no marker either.
// D10.2, D17.3
TEST(an_allocated_type_takes_pointers_and_dimensions, {
    TEST_ASSERT_EQ_STR(dump_expr("new(node**)"), "(new (type (name node) (ptr) (ptr)) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("new(node* own* own)"),
                       "(new (type (name node) (ptr own) (ptr own)) nil)");
    TEST_ASSERT_EQ_STR(dump_expr("new(u8[16], n)"),
                       "(new (type (prim u8) (array (int 16))) (ident n))");
    TEST_ASSERT_EQ_STR(dump_expr("new(string)"), "(new (type (string)) nil)");
    TEST_ASSERT_EQ_STR(expr_fails("new(node* mut)"),
                       "t.ft:1:19: error: new allocates writable storage: "
                       "remove the outermost 'mut'\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(node own*)"),
                       "t.ft:1:18: error: inside new an own follows a '*' of the element type\n");
})

// ---- a marker in a place no position exists -------------------------------

// The placement rules hold in every position a type can be written: a
// parameter, a field, a return type, a cast target and an array length's
// type.
// D5.3, D17.2
TEST(the_placement_rules_hold_in_every_type_position, {
    TEST_ASSERT_EQ_STR(parse_fails("fn void f(mut i32 a) { }"),
                       "t.ft:1:11: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
    TEST_ASSERT_EQ_STR(parse_fails("struct s { own node* p; }"),
                       "t.ft:1:12: error: an own never precedes the base type: "
                       "write 'node* own p' or 'string own s'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn mut i32 f() { }"),
                       "t.ft:1:4: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
    TEST_ASSERT_EQ_STR(expr_fails("cast(p, mut i32*)"),
                       "t.ft:1:17: error: a mut never precedes the base type: "
                       "write 'node mut* p' or 'node* mut p'\n");
    TEST_ASSERT_EQ_STR(expr_fails("sizeof(own u8@)"),
                       "t.ft:1:16: error: an own never precedes the base type: "
                       "write 'node* own p' or 'string own s'\n");
    TEST_ASSERT_EQ_STR(type_fails("i32[mut N]"),
                       "t.ft:1:5: error: expected an expression, found 'mut'\n");
})

// A doubled marker is refused in every position, on a base type, on a
// pointer, on a span and on an array.
// D5.3
TEST(a_doubled_marker_in_every_position, {
    TEST_ASSERT_EQ_STR(type_fails("i32 own own"),
                       "t.ft:1:5: error: an own marks a reference: "
                       "write it after a '*' or an '@', or on a string\n");
    TEST_ASSERT_EQ_STR(type_fails("string own own"),
                       "t.ft:1:12: error: an own appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("i32@ mut mut"),
                       "t.ft:1:10: error: a mut appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("i32@ own own"),
                       "t.ft:1:10: error: an own appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("i32[4] mut mut"),
                       "t.ft:1:12: error: a mut appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("string mut mut"),
                       "t.ft:1:12: error: a mut appears once in a type position\n");
})

// An `own` never follows the `mut` of its position, wherever that position
// is.
// D17.2
TEST(an_own_after_a_mut_in_every_position, {
    TEST_ASSERT_EQ_STR(type_fails("i32@ mut own"),
                       "t.ft:1:10: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
    TEST_ASSERT_EQ_STR(type_fails("i32* mut own*"),
                       "t.ft:1:10: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
    TEST_ASSERT_EQ_STR(type_fails("i32 mut own*"),
                       "t.ft:1:9: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
})

// An `own` marks a reference, so no base type but `string` and no array
// suffix takes one.
// D17.1, D17.2
TEST(an_own_where_no_reference_is, {
    TEST_ASSERT_EQ_STR(type_fails("point own"),
                       "t.ft:1:7: error: an own marks a reference: "
                       "write it after a '*' or an '@', or on a string\n");
    TEST_ASSERT_EQ_STR(type_fails("bool own*"),
                       "t.ft:1:6: error: an own marks a reference: "
                       "write it after a '*' or an '@', or on a string\n");
    TEST_ASSERT_EQ_STR(type_fails("void own*"),
                       "t.ft:1:6: error: an own marks a reference: "
                       "write it after a '*' or an '@', or on a string\n");
    TEST_ASSERT_EQ_STR(type_fails("i32[4] own"),
                       "t.ft:1:8: error: an own never follows a fixed-array suffix: "
                       "write it after the '*' or '@' it marks\n");
    TEST_ASSERT_EQ_STR(type_fails("i32@[4] own"),
                       "t.ft:1:9: error: an own never follows a fixed-array suffix: "
                       "write it after the '*' or '@' it marks\n");
})

// The `mut` of a position that a `[N]` follows belongs after the length, in
// every group.
// D5.3
TEST(a_mut_before_a_length_in_every_group, {
    TEST_ASSERT_EQ_STR(type_fails("string mut[4]"),
                       "t.ft:1:8: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
    TEST_ASSERT_EQ_STR(type_fails("i32@ mut[4]"),
                       "t.ft:1:6: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
    TEST_ASSERT_EQ_STR(type_fails("i32* own mut[4]"),
                       "t.ft:1:10: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
    TEST_ASSERT_EQ_STR(parse_fails("fn void f(i32 mut[4] a) { }"),
                       "t.ft:1:15: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
})

// No array suffix follows a trailing reference suffix, whichever suffix it
// is.
// D3.6
TEST(an_array_after_any_trailing_reference_suffix, {
    TEST_ASSERT_EQ_STR(type_fails("i32[2]*[2]"),
                       "t.ft:1:8: error: no array suffix follows a reference suffix: "
                       "wrap the array in a struct\n");
    TEST_ASSERT_EQ_STR(type_fails("i32[2]* mut[2]"),
                       "t.ft:1:12: error: no array suffix follows a reference suffix: "
                       "wrap the array in a struct\n");
    // The shape is settled before the bootstrap's own limits are: an array
    // after a trailing reference suffix does not parse at all.
    TEST_ASSERT_EQ_STR(type_fails("string@[2]@[2]"),
                       "t.ft:1:12: error: no array suffix follows a reference suffix: "
                       "wrap the array in a struct\n");
})
// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser_type", argc, argv);
    TEST_RUN(every_primitive_is_a_base_type);
    TEST_RUN(string_void_and_names_are_base_types);
    TEST_RUN(void_parses_as_a_base_type_on_its_own);
    TEST_RUN(d5_3_table_scalars_and_arrays);
    TEST_RUN(d5_3_table_pointers);
    TEST_RUN(d5_3_table_spans);
    TEST_RUN(d5_3_table_arrays_of_references);
    TEST_RUN(d17_2_table_owning_references);
    TEST_RUN(d17_2_own_on_string_and_before_an_array);
    TEST_RUN(function_types_read_as_a_base_type);
    TEST_RUN(function_types_take_suffixes_and_markers);
    TEST_RUN(the_out_parameter_shape_of_d3_6);
    TEST_RUN(a_marker_before_the_base_type_is_an_error);
    TEST_RUN(a_doubled_marker_is_an_error);
    TEST_RUN(an_own_after_the_mut_of_its_position_is_an_error);
    TEST_RUN(an_own_on_something_that_is_not_a_reference_is_an_error);
    TEST_RUN(a_mut_between_an_element_type_and_its_length_is_an_error);
    TEST_RUN(an_array_after_a_reference_suffix_is_an_error);
    TEST_RUN(a_type_that_is_not_a_type);
    TEST_RUN(a_second_array_or_span_level_is_not_supported);
    TEST_RUN(one_array_or_span_level_with_pointers_is_supported);
    TEST_RUN(the_level_count_is_per_written_type);
    TEST_RUN(new_refuses_a_mut_in_the_outermost_position_a_span_and_a_misplaced_own);
    TEST_RUN(new_takes_a_mut_below_the_outermost_position);
    TEST_RUN(every_marker_set_on_a_reference_suffix);
    TEST_RUN(every_marker_set_on_a_base_type);
    TEST_RUN(long_reference_chains_keep_their_markers);
    TEST_RUN(the_array_group_between_the_reference_groups);
    TEST_RUN(an_array_length_is_any_expression_here);
    TEST_RUN(function_types_nest);
    TEST_RUN(noreturn_is_only_a_return_type);
    TEST_RUN(an_array_literal_type_carries_no_marker);
    TEST_RUN(an_allocated_type_takes_pointers_and_dimensions);
    TEST_RUN(the_placement_rules_hold_in_every_type_position);
    TEST_RUN(a_doubled_marker_in_every_position);
    TEST_RUN(an_own_after_a_mut_in_every_position);
    TEST_RUN(an_own_where_no_reference_is);
    TEST_RUN(a_mut_before_a_length_in_every_group);
    TEST_RUN(an_array_after_any_trailing_reference_suffix);
    parse_done();
    TEST_EXIT();
}
