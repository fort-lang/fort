// The type grammar of the parser (grammar.md 4): the base types, the marker
// positions of the D5.3 and D17.2 tables, the reading order of D3.6, function
// types (D3.10) and every spelling those decisions call an error.
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

// `void` is a base type here; that it needs a `*` is the type builder's rule
// (D3.11), not the parser's.
TEST(void_parses_as_a_base_type_on_its_own,
     { TEST_ASSERT_EQ_STR(dump_type("void"), "(type (void))"); })

// ---- the placement table of D5.3 ------------------------------------------

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

TEST(d5_3_table_slices, {
    TEST_ASSERT_EQ_STR(dump_type("i32@"), "(type (prim i32) (slice))");
    TEST_ASSERT_EQ_STR(dump_type("i32@ mut"), "(type (prim i32) (slice mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32 mut@"), "(type (prim i32) mut (slice))");
    TEST_ASSERT_EQ_STR(dump_type("node* mut@"), "(type (name node) (ptr mut) (slice))");
    TEST_ASSERT_EQ_STR(dump_type("node mut*@"), "(type (name node) mut (ptr) (slice))");
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ mut*"), "(type (prim u8) mut (slice mut) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("u8@ mut*"), "(type (prim u8) (slice mut) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("string@"), "(type (string) (slice))");
})

TEST(d5_3_table_arrays_of_references, {
    TEST_ASSERT_EQ_STR(dump_type("node*[4] mut"), "(type (name node) (ptr) (array (int 4) mut))");
    TEST_ASSERT_EQ_STR(dump_type("i32[4]*"), "(type (prim i32) (array (int 4)) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("i32[4]* mut"), "(type (prim i32) (array (int 4)) (ptr mut))");
})

// ---- the placement table of D17.2 -----------------------------------------

TEST(d17_2_table_owning_references, {
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ own"), "(type (prim u8) mut (slice own))");
    TEST_ASSERT_EQ_STR(dump_type("u8@ own"), "(type (prim u8) (slice own))");
    TEST_ASSERT_EQ_STR(dump_type("node mut* own"), "(type (name node) mut (ptr own))");
    TEST_ASSERT_EQ_STR(dump_type("node* mut@ own"), "(type (name node) (ptr mut) (slice own))");
    TEST_ASSERT_EQ_STR(dump_type("node mut* own mut@ own"),
                       "(type (name node) mut (ptr own mut) (slice own))");
    TEST_ASSERT_EQ_STR(dump_type("node* own@"), "(type (name node) (ptr own) (slice))");
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ own mut*"), "(type (prim u8) mut (slice own mut) (ptr))");
})

// `string` is the reference with no suffix, so it takes an `own` directly,
// and an `own` before a fixed-array suffix marks the elements (D17.2).
TEST(d17_2_own_on_string_and_before_an_array, {
    TEST_ASSERT_EQ_STR(dump_type("string own"), "(type (string) own)");
    TEST_ASSERT_EQ_STR(dump_type("string own mut"), "(type (string) own mut)");
    TEST_ASSERT_EQ_STR(dump_type("node* own[4]"), "(type (name node) (ptr own) (array (int 4)))");
    TEST_ASSERT_EQ_STR(dump_type("node* own[4] mut"),
                       "(type (name node) (ptr own) (array (int 4) mut))");
    TEST_ASSERT_EQ_STR(dump_type("void* own"), "(type (void) (ptr own))");
})

// ---- function types (D3.10) -----------------------------------------------

TEST(function_types_read_as_a_base_type, {
    TEST_ASSERT_EQ_STR(dump_type("fn void()"), "(type (fn-type (type (void))))");
    TEST_ASSERT_EQ_STR(dump_type("fn i32(i32)"),
                       "(type (fn-type (type (prim i32)) (type (prim i32))))");
    TEST_ASSERT_EQ_STR(dump_type("fn i32(i32, i32)"),
                       "(type (fn-type (type (prim i32)) (type (prim i32)) (type (prim i32))))");
    TEST_ASSERT_EQ_STR(dump_type("fn noreturn(string)"),
                       "(type (fn-type (type (noreturn)) (type (string))))");
})

// Suffixes after a function type apply to the function type (D3.6).
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

// ---- the out-parameter shape of D3.6 --------------------------------------

TEST(the_out_parameter_shape_of_d3_6, {
    TEST_ASSERT_EQ_STR(dump_type("u8 mut@ own mut*"), "(type (prim u8) mut (slice own mut) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("u8@* mut"), "(type (prim u8) (slice) (ptr mut))");
})

// ---- what the placement rules forbid --------------------------------------

// Nothing precedes the base type (D5.3, D17.2).
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
// parse (D5.3).
TEST(a_doubled_marker_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("i32 mut mut"),
                       "t.ft:1:9: error: a mut appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("i32 mut* mut mut"),
                       "t.ft:1:14: error: a mut appears once in a type position\n");
    TEST_ASSERT_EQ_STR(type_fails("i32 mut* own own"),
                       "t.ft:1:14: error: an own appears once in a type position\n");
})

// An `own` precedes the `mut` of its position (D17.2).
TEST(an_own_after_the_mut_of_its_position_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("node* mut own"),
                       "t.ft:1:11: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
    TEST_ASSERT_EQ_STR(type_fails("string mut own"),
                       "t.ft:1:12: error: an own precedes the mut of its position: "
                       "write 'node* own mut p'\n");
})

// An `own` marks a reference: after a base type only `string` takes one, and
// a fixed-array suffix never does (D17.1, D17.2).
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

// The elements of an array share its storage, so the position a `[N]`
// follows never carries a `mut` (D5.3).
TEST(a_mut_between_an_element_type_and_its_length_is_an_error, {
    TEST_ASSERT_EQ_STR(type_fails("i32 mut[4]"),
                       "t.ft:1:5: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
    TEST_ASSERT_EQ_STR(type_fails("node* mut[4]"),
                       "t.ft:1:7: error: the elements share the array's storage: "
                       "write the mut after the length, as 'i32[4] mut'\n");
})

// No array suffix follows a trailing reference suffix (D3.6).
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

// ---- one array or slice level (toolchain.md 7.3) --------------------------

TEST(a_second_array_or_slice_level_is_not_supported, {
    TEST_ASSERT_EQ_STR(
        type_fails("i32[3][4]"),
        "t.ft:1:7: error: not supported by the bootstrap compiler: multi-dimensional arrays\n");
    TEST_ASSERT_EQ_STR(
        type_fails("i32[4]@"),
        "t.ft:1:7: error: not supported by the bootstrap compiler: slices of arrays\n");
    TEST_ASSERT_EQ_STR(
        type_fails("u8@@"),
        "t.ft:1:4: error: not supported by the bootstrap compiler: slices of slices\n");
    TEST_ASSERT_EQ_STR(
        type_fails("node@[4]"),
        "t.ft:1:6: error: not supported by the bootstrap compiler: arrays of slices\n");
})

// One level is supported, and a pointer is not a level of its own.
TEST(one_array_or_slice_level_with_pointers_is_supported, {
    TEST_ASSERT_EQ_STR(dump_type("node*[16]"), "(type (name node) (ptr) (array (int 16)))");
    TEST_ASSERT_EQ_STR(dump_type("u8@*"), "(type (prim u8) (slice) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("node***"), "(type (name node) (ptr) (ptr) (ptr))");
    TEST_ASSERT_EQ_STR(dump_type("string@"), "(type (string) (slice))");
})

// The bootstrap's limit is per written type: a function type's parameters
// are types of their own.
TEST(the_level_count_is_per_written_type, {
    TEST_ASSERT_EQ_STR(dump_type("fn void(i32[4], u8@)"),
                       "(type (fn-type (type (void)) (type (prim i32) (array (int 4)))"
                       " (type (prim u8) (slice))))");
    TEST_ASSERT_EQ_STR(
        type_fails("fn void(u8@@)"),
        "t.ft:1:12: error: not supported by the bootstrap compiler: slices of slices\n");
})

// ---- inside new (D10.2, D17.3) --------------------------------------------

TEST(new_refuses_a_mut_a_slice_and_a_misplaced_own, {
    TEST_ASSERT_EQ_STR(expr_fails("new(i32 mut)"),
                       "t.ft:1:17: error: a mut does not parse inside new: "
                       "new allocates writable storage\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(i32@)"),
                       "t.ft:1:16: error: a slice suffix does not parse inside new: "
                       "write new(T, n)\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(string own)"),
                       "t.ft:1:20: error: inside new an own follows a '*' of the element type\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(own node)"),
                       "t.ft:1:13: error: an own never precedes the base type: "
                       "write 'node* own p' or 'string own s'\n");
    TEST_ASSERT_EQ_STR(expr_fails("new(i32[4] mut)"),
                       "t.ft:1:20: error: a mut does not parse inside new: "
                       "new allocates writable storage\n");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("parser_type", argc, argv);
    TEST_RUN(every_primitive_is_a_base_type);
    TEST_RUN(string_void_and_names_are_base_types);
    TEST_RUN(void_parses_as_a_base_type_on_its_own);
    TEST_RUN(d5_3_table_scalars_and_arrays);
    TEST_RUN(d5_3_table_pointers);
    TEST_RUN(d5_3_table_slices);
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
    TEST_RUN(a_second_array_or_slice_level_is_not_supported);
    TEST_RUN(one_array_or_slice_level_with_pointers_is_supported);
    TEST_RUN(the_level_count_is_per_written_type);
    TEST_RUN(new_refuses_a_mut_a_slice_and_a_misplaced_own);
    parse_done();
    TEST_EXIT();
}
