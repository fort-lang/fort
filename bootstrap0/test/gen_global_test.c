// Tests global linkage, alignment, initializers, and section selection.
//
// Programs cannot expose a shared linkage or alignment error.
// The suite therefore checks the emitted text.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common/gen_helpers.h"
#include "gen.h"
#include "str.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- linkage and mutability -----------------------------------------------

TEST(an_immutable_declaration_is_a_dso_local_constant, {
    // `Type NAME = init;` lives in read-only memory and carries the default external linkage with
    // `dso_local`.
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 100;\nfn main() i32 { return LIMIT; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.LIMIT\" = dso_local constant i32 100, align 4"),
                       "@\"main.LIMIT\" = dso_local constant i32 100, align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_mut_declaration_is_a_dso_local_global, {
    // `Type mut g = init;` is a global in writable memory.
    TEST_ASSERT_TRUE(emit("i64 mut counter = 0;\n"
                          "fn main() i32 {\n    counter = 1;\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.counter\" = dso_local global i64 0, align 8"),
                       "@\"main.counter\" = dso_local global i64 0, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_named_constant_is_not_unnamed_addr, {
    // `unnamed_addr` marks private data alone: a named constant keeps its address significant,
    // because `&CONST` is expressible.
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 100;\n"
                          "i32* LP = &LIMIT;\n"
                          "fn main() i32 { return *LP; }\n"));
    TEST_ASSERT_EQ_STR(absent("@\"main.LIMIT\" = dso_local unnamed_addr"), "absent");
    TEST_ASSERT_EQ_STR(found("@\"main.LP\" = dso_local constant ptr @\"main.LIMIT\", align 8"),
                       "@\"main.LP\" = dso_local constant ptr @\"main.LIMIT\", align 8");
})

TEST(no_global_names_a_section, {
    // LLVM chooses .rodata, .data, .data.rel.ro or .bss from the initializer, so the emitter names
    // none.
    TEST_ASSERT_TRUE(emit("i32 A = 1;\ni32 mut b = 0;\ni32* P = &A;\n"
                          "fn main() i32 { return A + b + *P; }\n"));
    TEST_ASSERT_EQ_STR(absent("section"), "absent");
})

TEST(the_globals_stand_between_the_named_types_and_the_definitions, {
    // Globals follow named types and precede function definitions.
    TEST_ASSERT_TRUE(emit("i32 A = 1;\nfn main() i32 { return A; }\n"));
    TEST_ASSERT_TRUE(before("%fort.enum_member = type", "@\"main.A\" ="));
    TEST_ASSERT_TRUE(before("@\"main.A\" =", "define dso_local i32 @\"main.main\""));
})

TEST(the_globals_of_a_module_follow_source_order, {
    // Every list is appended to in emission order, so the text is a function
    // of the program alone.
    TEST_ASSERT_TRUE(emit("i32 FIRST = 1;\ni32 SECOND = 2;\ni32 mut third = 3;\n"
                          "fn main() i32 { return FIRST + SECOND + third; }\n"));
    TEST_ASSERT_TRUE(before("@\"main.FIRST\" =", "@\"main.SECOND\" ="));
    TEST_ASSERT_TRUE(before("@\"main.SECOND\" =", "@\"main.third\" ="));
})

// ---- global alignment ---------------------------------------------------------------

TEST(every_global_carries_the_alignment_of_its_type, {
    // The emitter writes what its own layout says.
    TEST_ASSERT_TRUE(emit("struct pair { i8 tag; i64 value; }\n"
                          "u8 BYTE = 1;\ni16 SHORT = 2;\nchar LETTER = 'z';\nbool FLAG = true;\n"
                          "u8[3] BYTES = {1, 2, 3};\npair P = {1, 2};\nstring S = \"hi\";\n"
                          "fn main() i32 { return cast(BYTE, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.BYTE\" = dso_local constant i8 1, align 1"),
                       "@\"main.BYTE\" = dso_local constant i8 1, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.SHORT\" = dso_local constant i16 2, align 2"),
                       "@\"main.SHORT\" = dso_local constant i16 2, align 2");
    // A `char` is an unsigned byte and a `bool` is 0 or 1 in an `i8`.
    TEST_ASSERT_EQ_STR(found("@\"main.LETTER\" = dso_local constant i8 122, align 1"),
                       "@\"main.LETTER\" = dso_local constant i8 122, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.FLAG\" = dso_local constant i8 1, align 1"),
                       "@\"main.FLAG\" = dso_local constant i8 1, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.BYTES\" = dso_local constant [3 x i8] "
                             "[i8 1, i8 2, i8 3], align 1"),
                       "@\"main.BYTES\" = dso_local constant [3 x i8] [i8 1, i8 2, i8 3], align 1");
    // The struct's alignment is its most-aligned field's, the one
    // type_layout_struct computed.
    TEST_ASSERT_EQ_STR(
        found("@\"main.P\" = dso_local constant %struct.main.pair "
              "{ i8 1, i64 2 }, align 8"),
        "@\"main.P\" = dso_local constant %struct.main.pair { i8 1, i64 2 }, align 8");
    TEST_ASSERT_EQ_STR(
        found("@\"main.S\" = dso_local constant %fort.span "
              "{ ptr @.str.0, i64 2 }, align 8"),
        "@\"main.S\" = dso_local constant %fort.span { ptr @.str.0, i64 2 }, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the initializer forms ------------------------------------------------

TEST(a_negative_constant_prints_signed_and_an_unsigned_one_prints_unsigned, {
    // An integer constant is printed with the signedness of its fort type.
    TEST_ASSERT_TRUE(emit("i8 LOW = -1;\nu8 HIGH = 255;\n"
                          "fn main() i32 { return cast(LOW, i32) + cast(HIGH, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.LOW\" = dso_local constant i8 -1, align 1"),
                       "@\"main.LOW\" = dso_local constant i8 -1, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.HIGH\" = dso_local constant i8 255, align 1"),
                       "@\"main.HIGH\" = dso_local constant i8 255, align 1");
})

TEST(an_enum_constant_holds_its_i32_value, {
    // An enum is `i32` and its member prints signed.
    TEST_ASSERT_TRUE(emit("enum color { red = -2, green }\n"
                          "color START = color.green;\n"
                          "color LOW = color.red;\n"
                          "fn main() i32 { return cast(START, i32) + cast(LOW, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.START\" = dso_local constant i32 -1, align 4"),
                       "@\"main.START\" = dso_local constant i32 -1, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.LOW\" = dso_local constant i32 -2, align 4"),
                       "@\"main.LOW\" = dso_local constant i32 -2, align 4");
})

TEST(a_null_pointer_constant_is_null, {
    TEST_ASSERT_TRUE(emit("i32* NOWHERE = null;\n"
                          "fn main() i32 {\n    println(NOWHERE == null);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.NOWHERE\" = dso_local constant ptr null, align 8"),
                       "@\"main.NOWHERE\" = dso_local constant ptr null, align 8");
})

TEST(a_folded_constant_expression_is_the_value_it_folded_to, {
    // A module-level initializer is a constant expression, so `sizeof`, an
    // enum member and arithmetic over another constant are all folded before
    // the emitter sees them.
    TEST_ASSERT_TRUE(emit("i32 N = 4;\ni32 M = N * 2 + sizeof(i64);\n"
                          "fn main() i32 { return M; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.M\" = dso_local constant i32 16, align 4"),
                       "@\"main.M\" = dso_local constant i32 16, align 4");
})

TEST(a_constant_holding_a_function_address_names_the_function, {
    // A function name is a module-level initializer and its address is a relocation, which is what
    // puts the table in .data.rel.ro.
    TEST_ASSERT_TRUE(emit("struct slot { fn (i32) i32 f; }\n"
                          "fn f(i32 x) i32 { return x; }\nfn g(i32 x) i32 { return x +% 1; }\n"
                          "slot[2] TABLE = {{f}, {g}};\n"
                          "fn main() i32 { return TABLE[0].f(1); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.TABLE\" = dso_local constant [2 x %struct.main.slot] "
                             "[%struct.main.slot { ptr @\"main.f\" }, %struct.main.slot "
                             "{ ptr @\"main.g\" }], align 8"),
                       "@\"main.TABLE\" = dso_local constant [2 x %struct.main.slot] "
                       "[%struct.main.slot { ptr @\"main.f\" }, %struct.main.slot "
                       "{ ptr @\"main.g\" }], align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_constant_may_hold_the_address_of_a_mut_global, {
    // `&` of a module-level declaration from any module is an initializer,
    // the `mut` ones included.
    TEST_ASSERT_TRUE(emit("i32 mut counter = 0;\ni32* WATCH = &counter;\n"
                          "fn main() i32 { return *WATCH; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.WATCH\" = dso_local constant ptr @\"main.counter\", align 8"),
                       "@\"main.WATCH\" = dso_local constant ptr @\"main.counter\", align 8");
})

TEST(a_constant_that_names_another_one_copies_its_value, {
    // Constant references are evaluated lazily, so an aggregate constant
    // named by another is that other's value.
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point ORIGIN = {1, 2};\npoint COPY = ORIGIN;\n"
                          "fn main() i32 { return COPY.x; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.COPY\" = dso_local constant %struct.main.point "
                             "{ i32 1, i32 2 }, align 4"),
                       "@\"main.COPY\" = dso_local constant %struct.main.point "
                       "{ i32 1, i32 2 }, align 4");
})

TEST(a_constant_may_hold_its_own_address, {
    // A self-pointing sentinel: the initializer names the global it initializes, which is one
    // relocation into itself.
    TEST_ASSERT_TRUE(emit("struct node { i32 v; node* next; }\n"
                          "node N = node{7, &N};\n"
                          "fn main() i32 { return N.next->v; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.N\" = dso_local constant %struct.main.node "
                             "{ i32 7, ptr @\"main.N\" }, align 8"),
                       "@\"main.N\" = dso_local constant %struct.main.node "
                       "{ i32 7, ptr @\"main.N\" }, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_struct_constant_writes_its_fields_in_declaration_order, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point P = point{.y = 7, .x = 3};\n"
                          "fn main() i32 { return P.x; }\n"));
    // The designated form may name the fields in any order; the initializer follows the declaration
    // order the named type uses.
    TEST_ASSERT_EQ_STR(found("@\"main.P\" = dso_local constant %struct.main.point "
                             "{ i32 3, i32 7 }, align 4"),
                       "@\"main.P\" = dso_local constant %struct.main.point "
                       "{ i32 3, i32 7 }, align 4");
})

TEST(a_field_the_designated_form_omits_is_zeroed, {
    TEST_ASSERT_TRUE(emit("struct trio { i32 a; i32 b; i32 c; }\n"
                          "trio T = trio{.b = 5};\n"
                          "fn main() i32 { return T.b; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.T\" = dso_local constant %struct.main.trio "
                             "{ i32 0, i32 5, i32 0 }, align 4"),
                       "@\"main.T\" = dso_local constant %struct.main.trio "
                       "{ i32 0, i32 5, i32 0 }, align 4");
})

TEST(a_nested_aggregate_constant_writes_the_type_of_every_member, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point[2] PTS = {{1, 2}, {3, 4}};\n"
                          "string[2] NAMES = {\"a\", \"bb\"};\n"
                          "fn main() i32 { return PTS[1].x; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.PTS\" = dso_local constant [2 x %struct.main.point] "
                             "[%struct.main.point { i32 1, i32 2 }, "
                             "%struct.main.point { i32 3, i32 4 }], align 4"),
                       "@\"main.PTS\" = dso_local constant [2 x %struct.main.point] "
                       "[%struct.main.point { i32 1, i32 2 }, "
                       "%struct.main.point { i32 3, i32 4 }], align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.NAMES\" = dso_local constant [2 x %fort.span] "
                             "[%fort.span { ptr @.str.0, i64 1 }, "
                             "%fort.span { ptr @.str.1, i64 2 }], align 8"),
                       "@\"main.NAMES\" = dso_local constant [2 x %fort.span] "
                       "[%fort.span { ptr @.str.0, i64 1 }, "
                       "%fort.span { ptr @.str.1, i64 2 }], align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- nested aggregates ----------------------------------------------------

TEST(a_struct_inside_a_struct_is_written_with_its_own_type, {
    // Each constant aggregate member uses its memory type.
    // A nested struct names its struct type instead of raw bytes.
    TEST_ASSERT_TRUE(emit("struct inner { i32 a; i32 b; }\n"
                          "struct outer { inner in; i64 n; }\n"
                          "outer O = {{1, 2}, 3};\n"
                          "fn main() i32 { return O.in.b; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.O\" = dso_local constant %struct.main.outer "
                             "{ %struct.main.inner { i32 1, i32 2 }, i64 3 }, align 8"),
                       "@\"main.O\" = dso_local constant %struct.main.outer "
                       "{ %struct.main.inner { i32 1, i32 2 }, i64 3 }, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_all_zero_member_of_a_nonzero_aggregate_collapses_by_itself, {
    // Zero-initializer collapse applies at each aggregate level.
    // The first element collapses even though the complete array does not.
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point[2] PTS = {{0, 0}, {1, 2}};\n"
                          "fn main() i32 { return PTS[1].x; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.PTS\" = dso_local constant [2 x %struct.main.point] "
                             "[%struct.main.point zeroinitializer, "
                             "%struct.main.point { i32 1, i32 2 }], align 4"),
                       "@\"main.PTS\" = dso_local constant [2 x %struct.main.point] "
                       "[%struct.main.point zeroinitializer, "
                       "%struct.main.point { i32 1, i32 2 }], align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_designated_literal_nested_in_an_array_keeps_the_field_order, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point[2] PTS = {point{.y = 1}, {2, 3}};\n"
                          "fn main() i32 { return PTS[0].y; }\n"));
    TEST_ASSERT_EQ_STR(found("[%struct.main.point { i32 0, i32 1 }, "
                             "%struct.main.point { i32 2, i32 3 }]"),
                       "[%struct.main.point { i32 0, i32 1 }, "
                       "%struct.main.point { i32 2, i32 3 }]");
})

TEST(a_typed_array_literal_is_the_same_constant_as_a_brace_list, {
    // `i32[3]{1, 2, 3}` carries its brace list in `b`.
    TEST_ASSERT_TRUE(emit("i32[3] A = i32[3]{1, 2, 3};\n"
                          "fn main() i32 { return A[2]; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.A\" = dso_local constant [3 x i32] "
                             "[i32 1, i32 2, i32 3], align 4"),
                       "@\"main.A\" = dso_local constant [3 x i32] "
                       "[i32 1, i32 2, i32 3], align 4");
})

TEST(a_member_of_every_scalar_kind_keeps_its_memory_form, {
    // A `bool` member uses `i8 0` or `i8 1`, never `i1 true`.
    // Characters use bytes, enums use `i32`, and pointers use addresses.
    TEST_ASSERT_TRUE(emit("enum color { red, green = 5 }\n"
                          "struct row { bool on; char tag; color hue; i32* link; string name; }\n"
                          "i32 TARGET = 1;\n"
                          "row R = {true, 'z', color.green, &TARGET, \"hey\"};\n"
                          "fn main() i32 { return *R.link; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.R\" = dso_local constant %struct.main.row "
                             "{ i8 1, i8 122, i32 5, ptr @\"main.TARGET\", "
                             "%fort.span { ptr @.str.0, i64 3 } }, align 8"),
                       "@\"main.R\" = dso_local constant %struct.main.row "
                       "{ i8 1, i8 122, i32 5, ptr @\"main.TARGET\", "
                       "%fort.span { ptr @.str.0, i64 3 } }, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_enum_table_constant_holds_the_member_values, {
    TEST_ASSERT_TRUE(emit("enum color { red, green = 5, blue }\n"
                          "color[3] ALL = {color.red, color.green, color.blue};\n"
                          "fn main() i32 { return cast(ALL[2], i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.ALL\" = dso_local constant [3 x i32] "
                             "[i32 0, i32 5, i32 6], align 4"),
                       "@\"main.ALL\" = dso_local constant [3 x i32] "
                       "[i32 0, i32 5, i32 6], align 4");
})

TEST(a_string_a_global_holds_is_numbered_before_one_a_body_holds, {
    // The private data is numbered on first use and the globals are emitted before the function
    // definitions, so a global's string is `@.str.0`.
    TEST_ASSERT_TRUE(emit("string GREETING = \"hi\";\n"
                          "fn main() i32 {\n    println(GREETING, \" \", \"bye\");\n"
                          "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [3 x i8] c\"hi\\00\""),
                       "@.str.0 = private unnamed_addr constant [3 x i8] c\"hi\\00\"");
    // A string constant is assigned on first use and never deduplicated by content. The `GREETING`
    // the body prints is a second copy of the same bytes under a second name.
    TEST_ASSERT_EQ_STR(found("@.str.1 = private unnamed_addr constant [3 x i8] c\"hi\\00\""),
                       "@.str.1 = private unnamed_addr constant [3 x i8] c\"hi\\00\"");
    TEST_ASSERT_EQ_STR(found("@.str.3 = private unnamed_addr constant [4 x i8] c\"bye\\00\""),
                       "@.str.3 = private unnamed_addr constant [4 x i8] c\"bye\\00\"");
})

// ---- zeroinitializer ------------------------------------------------------

TEST(an_all_zero_aggregate_is_a_zeroinitializer, {
    // An all-zero `global` lands in .bss. An all-zero `constant` uses .rodata zero pages. Both use
    // `zeroinitializer`.
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point ORIGIN = {};\npoint mut cell = {};\n"
                          "i32[3] mut cells = {};\npoint[2] PAIR = {{0, 0}, {0, 0}};\n"
                          "fn main() i32 {\n    cell.x = 1;\n    cells[0] = 1;\n"
                          "    return ORIGIN.x + PAIR[0].y;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.ORIGIN\" = dso_local constant %struct.main.point "
                             "zeroinitializer, align 4"),
                       "@\"main.ORIGIN\" = dso_local constant %struct.main.point "
                       "zeroinitializer, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.cell\" = dso_local global %struct.main.point "
                             "zeroinitializer, align 4"),
                       "@\"main.cell\" = dso_local global %struct.main.point "
                       "zeroinitializer, align 4");
    TEST_ASSERT_EQ_STR(
        found("@\"main.cells\" = dso_local global [3 x i32] zeroinitializer, align 4"),
        "@\"main.cells\" = dso_local global [3 x i32] zeroinitializer, align 4");
    // An aggregate written out in full but holding only zeros collapses too,
    // at every level.
    TEST_ASSERT_EQ_STR(found("@\"main.PAIR\" = dso_local constant [2 x %struct.main.point] "
                             "zeroinitializer, align 4"),
                       "@\"main.PAIR\" = dso_local constant [2 x %struct.main.point] "
                       "zeroinitializer, align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_zeroed_span_or_string_global_is_a_zeroinitializer, {
    // `= {}` zero-initializes spans and strings as `%fort.span` values.
    TEST_ASSERT_TRUE(emit("i32@ VIEW = {};\ni32 mut@ own mut pool = {};\nstring EMPTY = {};\n"
                          "fn main() i32 {\n    pool = new(i32, 1);\n    del(pool);\n"
                          "    return cast(VIEW.len + EMPTY.len, i32);\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.VIEW\" = dso_local constant %fort.span zeroinitializer, "
                             "align 8"),
                       "@\"main.VIEW\" = dso_local constant %fort.span zeroinitializer, align 8");
    TEST_ASSERT_EQ_STR(found("@\"main.pool\" = dso_local global %fort.span zeroinitializer, "
                             "align 8"),
                       "@\"main.pool\" = dso_local global %fort.span zeroinitializer, align 8");
    TEST_ASSERT_EQ_STR(found("@\"main.EMPTY\" = dso_local constant %fort.span zeroinitializer, "
                             "align 8"),
                       "@\"main.EMPTY\" = dso_local constant %fort.span zeroinitializer, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_zero_scalar_keeps_its_literal_form, {
    // `zeroinitializer` collapses an aggregate; a scalar prints its value.
    TEST_ASSERT_TRUE(emit("i32 mut counter = 0;\ni32* NOWHERE = null;\nbool OFF = false;\n"
                          "fn main() i32 { return counter; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.counter\" = dso_local global i32 0, align 4"),
                       "@\"main.counter\" = dso_local global i32 0, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.OFF\" = dso_local constant i8 0, align 1"),
                       "@\"main.OFF\" = dso_local constant i8 0, align 1");
    TEST_ASSERT_EQ_STR(absent("i32 zeroinitializer"), "absent");
})

TEST(a_partly_zero_aggregate_is_written_out, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point P = {0, 1};\n"
                          "fn main() i32 { return P.y; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.P\" = dso_local constant %struct.main.point "
                             "{ i32 0, i32 1 }, align 4"),
                       "@\"main.P\" = dso_local constant %struct.main.point "
                       "{ i32 0, i32 1 }, align 4");
})

TEST(a_zero_length_string_constant_is_not_all_zero, {
    // The header of a `string` holds the address of its bytes, which is a relocation and never
    // zero.
    TEST_ASSERT_TRUE(emit("string EMPTY = \"\";\n"
                          "fn main() i32 { return cast(EMPTY.len, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.EMPTY\" = dso_local constant %fort.span "
                             "{ ptr @.str.0, i64 0 }, align 8"),
                       "@\"main.EMPTY\" = dso_local constant %fort.span "
                       "{ ptr @.str.0, i64 0 }, align 8");
})

// ---- reading and writing module-level data -----------------------------------------

TEST(a_mut_global_is_read_and_written_through_its_symbol, {
    TEST_ASSERT_TRUE(emit("i32 mut counter = 0;\n"
                          "fn bump() void { counter = counter +% 1; }\n"
                          "fn main() i32 {\n    bump();\n    return counter;\n}\n"));
    TEST_ASSERT_EQ_STR(found("load i32, ptr @\"main.counter\", align 4"),
                       "load i32, ptr @\"main.counter\", align 4");
    TEST_ASSERT_EQ_STR(found("store i32 %t1, ptr @\"main.counter\", align 4"),
                       "store i32 %t1, ptr @\"main.counter\", align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_scalar_constant_is_folded_into_its_uses, {
    // A constant expression has no side effects and is emitted as a literal,
    // so a scalar constant's storage is never loaded.
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 10;\n"
                          "fn main() i32 {\n    i32 mut n = 0;\n    n +%= LIMIT;\n"
                          "    return n;\n}\n"));
    TEST_ASSERT_EQ_STR(absent("load i32, ptr @\"main.LIMIT\""), "absent");
    TEST_ASSERT_EQ_STR(found("add i32 %t0, 10"), "add i32 %t0, 10");
})

TEST(an_aggregate_constant_is_read_out_of_its_storage, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\npoint ORIGIN = {1, 2};\n"
                          "fn main() i32 { return ORIGIN.y; }\n"));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %struct.main.point, "
                             "ptr @\"main.ORIGIN\", i32 0, i32 1"),
                       "getelementptr inbounds %struct.main.point, "
                       "ptr @\"main.ORIGIN\", i32 0, i32 1");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_address_of_a_constant_is_its_symbol, {
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 10;\n"
                          "fn main() i32 {\n    i32* p = &LIMIT;\n    return *p;\n}\n"));
    TEST_ASSERT_EQ_STR(found("store ptr @\"main.LIMIT\", ptr %p.0, align 8"),
                       "store ptr @\"main.LIMIT\", ptr %p.0, align 8");
})

// ---- several modules ------------------------------------------------------

TEST(each_module_emits_its_own_globals_once, {
    // Modules are emitted in dependency order, every module after the ones it
    // imports, and a constant belongs to the module that declares it.
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import shapes;\n"
                              "i32 HERE = 1;\n"
                              "fn main() i32 { return shapes.SIDES + HERE; }\n",
                              "shapes.ft",
                              "i32 SIDES = 4;\n"));
    TEST_ASSERT_EQ_STR(found("@\"shapes.SIDES\" = dso_local constant i32 4, align 4"),
                       "@\"shapes.SIDES\" = dso_local constant i32 4, align 4");
    TEST_ASSERT_EQ_INT64((int64_t)occurrences("@\"shapes.SIDES\" ="), (int64_t)1);
    TEST_ASSERT_TRUE(before("@\"shapes.SIDES\" =", "@\"main.HERE\" ="));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_global_of_another_module_is_reached_through_its_own_symbol, {
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import store;\n"
                              "fn main() i32 {\n    store.total = 2;\n    return store.total;\n}\n",
                              "store.ft",
                              "i32 mut total = 0;\n"));
    TEST_ASSERT_EQ_STR(found("store i32 2, ptr @\"store.total\", align 4"),
                       "store i32 2, ptr @\"store.total\", align 4");
    TEST_ASSERT_EQ_STR(found("load i32, ptr @\"store.total\", align 4"),
                       "load i32, ptr @\"store.total\", align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_constant_may_hold_the_address_of_another_modules_declaration, {
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import shapes;\n"
                              "i32* THERE = &shapes.SIDES;\n"
                              "fn main() i32 { return *THERE; }\n",
                              "shapes.ft",
                              "i32 SIDES = 4;\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.THERE\" = dso_local constant ptr @\"shapes.SIDES\", align 8"),
                       "@\"main.THERE\" = dso_local constant ptr @\"shapes.SIDES\", align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_constant_of_another_modules_struct_type_names_that_modules_type, {
    // The named types of every module stand in one section before the globals, so a constant may
    // hold a struct another module declares.
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import shapes;\n"
                              "shapes.square UNIT = {3};\n"
                              "fn main() i32 { return UNIT.side; }\n",
                              "shapes.ft",
                              "struct square { i32 side; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.UNIT\" = dso_local constant %struct.shapes.square "
                             "{ i32 3 }, align 4"),
                       "@\"main.UNIT\" = dso_local constant %struct.shapes.square "
                       "{ i32 3 }, align 4");
    TEST_ASSERT_TRUE(before("%struct.shapes.square = type", "@\"main.UNIT\" ="));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_global_declared_after_its_use_is_emitted_all_the_same, {
    // Top-level declarations are order-independent within a module, and a forward reference to a
    // global is legal in `.ll`.
    TEST_ASSERT_TRUE(emit("fn main() i32 { return B; }\ni32 B = A * 2;\ni32 A = 3;\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.B\" = dso_local constant i32 6, align 4"),
                       "@\"main.B\" = dso_local constant i32 6, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.A\" = dso_local constant i32 3, align 4"),
                       "@\"main.A\" = dso_local constant i32 3, align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_declaration_nothing_refers_to_is_emitted_all_the_same, {
    // A fort constant or global has the default external linkage, so it is a
    // definition of the module whether or not this program reads it.
    TEST_ASSERT_TRUE(emit("i32 NEVER = 5;\ni32 mut nobody = 7;\n"
                          "fn main() i32 { return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.NEVER\" = dso_local constant i32 5, align 4"),
                       "@\"main.NEVER\" = dso_local constant i32 5, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.nobody\" = dso_local global i32 7, align 4"),
                       "@\"main.nobody\" = dso_local global i32 7, align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_declaration_that_failed_to_check_emits_no_global, {
    // A module-level initializer must be a constant expression; the checker
    // reported it, so the emitter is silent and emits nothing for it.
    TEST_ASSERT_FALSE(emit("fn f() i32 { return 1; }\ni32 BAD = f();\n"
                           "fn main() i32 { return BAD; }\n"));
    TEST_ASSERT_EQ_STR(absent("@\"main.BAD\""), "absent");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_global", argc, argv);
    TEST_RUN(an_immutable_declaration_is_a_dso_local_constant);
    TEST_RUN(a_mut_declaration_is_a_dso_local_global);
    TEST_RUN(a_named_constant_is_not_unnamed_addr);
    TEST_RUN(no_global_names_a_section);
    TEST_RUN(the_globals_stand_between_the_named_types_and_the_definitions);
    TEST_RUN(the_globals_of_a_module_follow_source_order);
    TEST_RUN(every_global_carries_the_alignment_of_its_type);
    TEST_RUN(a_negative_constant_prints_signed_and_an_unsigned_one_prints_unsigned);
    TEST_RUN(an_enum_constant_holds_its_i32_value);
    TEST_RUN(a_null_pointer_constant_is_null);
    TEST_RUN(a_folded_constant_expression_is_the_value_it_folded_to);
    TEST_RUN(a_constant_holding_a_function_address_names_the_function);
    TEST_RUN(a_constant_may_hold_the_address_of_a_mut_global);
    TEST_RUN(a_constant_that_names_another_one_copies_its_value);
    TEST_RUN(a_constant_may_hold_its_own_address);
    TEST_RUN(a_struct_constant_writes_its_fields_in_declaration_order);
    TEST_RUN(a_field_the_designated_form_omits_is_zeroed);
    TEST_RUN(a_nested_aggregate_constant_writes_the_type_of_every_member);
    TEST_RUN(a_struct_inside_a_struct_is_written_with_its_own_type);
    TEST_RUN(an_all_zero_member_of_a_nonzero_aggregate_collapses_by_itself);
    TEST_RUN(a_designated_literal_nested_in_an_array_keeps_the_field_order);
    TEST_RUN(a_typed_array_literal_is_the_same_constant_as_a_brace_list);
    TEST_RUN(a_member_of_every_scalar_kind_keeps_its_memory_form);
    TEST_RUN(an_enum_table_constant_holds_the_member_values);
    TEST_RUN(a_string_a_global_holds_is_numbered_before_one_a_body_holds);
    TEST_RUN(an_all_zero_aggregate_is_a_zeroinitializer);
    TEST_RUN(a_zeroed_span_or_string_global_is_a_zeroinitializer);
    TEST_RUN(a_zero_scalar_keeps_its_literal_form);
    TEST_RUN(a_partly_zero_aggregate_is_written_out);
    TEST_RUN(a_zero_length_string_constant_is_not_all_zero);
    TEST_RUN(a_mut_global_is_read_and_written_through_its_symbol);
    TEST_RUN(a_scalar_constant_is_folded_into_its_uses);
    TEST_RUN(an_aggregate_constant_is_read_out_of_its_storage);
    TEST_RUN(the_address_of_a_constant_is_its_symbol);
    TEST_RUN(each_module_emits_its_own_globals_once);
    TEST_RUN(a_global_of_another_module_is_reached_through_its_own_symbol);
    TEST_RUN(a_constant_may_hold_the_address_of_another_modules_declaration);
    TEST_RUN(a_constant_of_another_modules_struct_type_names_that_modules_type);
    TEST_RUN(a_global_declared_after_its_use_is_emitted_all_the_same);
    TEST_RUN(a_declaration_nothing_refers_to_is_emitted_all_the_same);
    TEST_RUN(a_declaration_that_failed_to_check_emits_no_global);
    gen_done();
    TEST_EXIT();
}
