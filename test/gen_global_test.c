// Unit tests of the module-level data the emitter writes (D7.10, toolchain.md
// 6 items 1, 4 and 5): `dso_local constant` for an immutable declaration and
// `dso_local global` for a `mut` one, the initializer forms a constant
// expression takes in IR, `zeroinitializer` for an all-zero value, the
// explicit alignment, and the rule that no section is named, since LLVM picks
// `.rodata`, `.data`, `.data.rel.ro` or `.bss` from the initializer itself.
//
// None of this is observable from a program the compiler builds: a linkage or
// an alignment both sides of the compilation agree on wrongly still runs. The
// suite therefore holds the emitted text.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- linkage and mutability (item 4, D7.10) ----------------------------------------

TEST(an_immutable_declaration_is_a_dso_local_constant, {
    // `Type NAME = init;` lives in read-only memory (D7.10) and carries the
    // default external linkage with `dso_local` (D9.6, item 4).
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 100;\nfn i32 main() { return LIMIT; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.LIMIT\" = dso_local constant i32 100, align 4"),
                       "@\"main.LIMIT\" = dso_local constant i32 100, align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_mut_declaration_is_a_dso_local_global, {
    // `Type mut g = init;` is a global in writable memory (D7.10).
    TEST_ASSERT_TRUE(emit("i64 mut counter = 0;\n"
                          "fn i32 main() {\n    counter = 1;\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.counter\" = dso_local global i64 0, align 8"),
                       "@\"main.counter\" = dso_local global i64 0, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_named_constant_is_not_unnamed_addr, {
    // `unnamed_addr` marks private data alone: a named constant keeps its
    // address significant, because `&CONST` is expressible (item 5, D6.7).
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 100;\n"
                          "i32* LP = &LIMIT;\n"
                          "fn i32 main() { return *LP; }\n"));
    TEST_ASSERT_EQ_STR(absent("@\"main.LIMIT\" = dso_local unnamed_addr"), "absent");
    TEST_ASSERT_EQ_STR(found("@\"main.LP\" = dso_local constant ptr @\"main.LIMIT\", align 8"),
                       "@\"main.LP\" = dso_local constant ptr @\"main.LIMIT\", align 8");
})

TEST(no_global_names_a_section, {
    // LLVM chooses .rodata, .data, .data.rel.ro or .bss from the initializer,
    // so the emitter names none (item 5).
    TEST_ASSERT_TRUE(emit("i32 A = 1;\ni32 mut b = 0;\ni32* P = &A;\n"
                          "fn i32 main() { return A + b + *P; }\n"));
    TEST_ASSERT_EQ_STR(absent("section"), "absent");
})

TEST(the_globals_stand_between_the_named_types_and_the_definitions, {
    // The section order of item 1.
    TEST_ASSERT_TRUE(emit("i32 A = 1;\nfn i32 main() { return A; }\n"));
    TEST_ASSERT_TRUE(before("%fort.enum_member = type", "@\"main.A\" ="));
    TEST_ASSERT_TRUE(before("@\"main.A\" =", "define dso_local i32 @\"main.main\""));
})

TEST(the_globals_of_a_module_follow_source_order, {
    // Every list is appended to in emission order, so the text is a function
    // of the program alone (D19.5).
    TEST_ASSERT_TRUE(emit("i32 FIRST = 1;\ni32 SECOND = 2;\ni32 mut third = 3;\n"
                          "fn i32 main() { return FIRST + SECOND + third; }\n"));
    TEST_ASSERT_TRUE(before("@\"main.FIRST\" =", "@\"main.SECOND\" ="));
    TEST_ASSERT_TRUE(before("@\"main.SECOND\" =", "@\"main.third\" ="));
})

// ---- the alignment of item 5 (D3.1, D3.8) ------------------------------------------

TEST(every_global_carries_the_alignment_of_its_type, {
    // Alignment equals size for a primitive, a pointer and a span align to 8,
    // an array to its element and a struct to its most-aligned field (D3.1,
    // D3.8), and the emitter writes what its own layout says.
    TEST_ASSERT_TRUE(emit("struct pair { i8 tag; i64 value; }\n"
                          "u8 BYTE = 1;\ni16 SHORT = 2;\nchar LETTER = 'z';\nbool FLAG = true;\n"
                          "u8[3] BYTES = {1, 2, 3};\npair P = {1, 2};\nstring S = \"hi\";\n"
                          "fn i32 main() { return cast(BYTE, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.BYTE\" = dso_local constant i8 1, align 1"),
                       "@\"main.BYTE\" = dso_local constant i8 1, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.SHORT\" = dso_local constant i16 2, align 2"),
                       "@\"main.SHORT\" = dso_local constant i16 2, align 2");
    // A `char` is an unsigned byte and a `bool` is 0 or 1 in an `i8` (D3.2,
    // D3.3, D19.2).
    TEST_ASSERT_EQ_STR(found("@\"main.LETTER\" = dso_local constant i8 122, align 1"),
                       "@\"main.LETTER\" = dso_local constant i8 122, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.FLAG\" = dso_local constant i8 1, align 1"),
                       "@\"main.FLAG\" = dso_local constant i8 1, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.BYTES\" = dso_local constant [3 x i8] "
                             "[i8 1, i8 2, i8 3], align 1"),
                       "@\"main.BYTES\" = dso_local constant [3 x i8] [i8 1, i8 2, i8 3], align 1");
    // The struct's alignment is its most-aligned field's, the one
    // type_layout_struct computed (D3.8).
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

// ---- the initializer forms (item 5, D4.6, D7.10) -----------------------------------

TEST(a_negative_constant_prints_signed_and_an_unsigned_one_prints_unsigned, {
    // An integer constant is printed with the signedness of its fort type
    // (D19.5).
    TEST_ASSERT_TRUE(emit("i8 LOW = -1;\nu8 HIGH = 255;\n"
                          "fn i32 main() { return cast(LOW, i32) + cast(HIGH, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.LOW\" = dso_local constant i8 -1, align 1"),
                       "@\"main.LOW\" = dso_local constant i8 -1, align 1");
    TEST_ASSERT_EQ_STR(found("@\"main.HIGH\" = dso_local constant i8 255, align 1"),
                       "@\"main.HIGH\" = dso_local constant i8 255, align 1");
})

TEST(an_enum_constant_holds_its_i32_value, {
    // An enum is `i32` and its member prints signed (D3.9).
    TEST_ASSERT_TRUE(emit("enum color { red = -2, green }\n"
                          "color START = color.green;\n"
                          "color LOW = color.red;\n"
                          "fn i32 main() { return cast(START, i32) + cast(LOW, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.START\" = dso_local constant i32 -1, align 4"),
                       "@\"main.START\" = dso_local constant i32 -1, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.LOW\" = dso_local constant i32 -2, align 4"),
                       "@\"main.LOW\" = dso_local constant i32 -2, align 4");
})

TEST(a_null_pointer_constant_is_null, {
    TEST_ASSERT_TRUE(emit("i32* NOWHERE = null;\n"
                          "fn i32 main() {\n    println(NOWHERE == null);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.NOWHERE\" = dso_local constant ptr null, align 8"),
                       "@\"main.NOWHERE\" = dso_local constant ptr null, align 8");
})

TEST(a_folded_constant_expression_is_the_value_it_folded_to, {
    // A module-level initializer is a constant expression, so `sizeof`, an
    // enum member and arithmetic over another constant are all folded before
    // the emitter sees them (D4.6).
    TEST_ASSERT_TRUE(emit("i32 N = 4;\ni32 M = N * 2 + sizeof(i64);\n"
                          "fn i32 main() { return M; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.M\" = dso_local constant i32 16, align 4"),
                       "@\"main.M\" = dso_local constant i32 16, align 4");
})

TEST(a_constant_holding_a_function_address_names_the_function, {
    // A function name is a module-level initializer and its address is a
    // relocation, which is what puts the table in .data.rel.ro (D7.10, item
    // 5).
    TEST_ASSERT_TRUE(emit("fn i32 f(i32 x) { return x; }\nfn i32 g(i32 x) { return x +% 1; }\n"
                          "fn i32(i32)[2] TABLE = {f, g};\n"
                          "fn i32 main() { return TABLE[0](1); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.TABLE\" = dso_local constant [2 x ptr] "
                             "[ptr @\"main.f\", ptr @\"main.g\"], align 8"),
                       "@\"main.TABLE\" = dso_local constant [2 x ptr] "
                       "[ptr @\"main.f\", ptr @\"main.g\"], align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_constant_may_hold_the_address_of_a_mut_global, {
    // `&` of a module-level declaration from any module is an initializer
    // (D7.10), the `mut` ones included.
    TEST_ASSERT_TRUE(emit("i32 mut counter = 0;\ni32* WATCH = &counter;\n"
                          "fn i32 main() { return *WATCH; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.WATCH\" = dso_local constant ptr @\"main.counter\", align 8"),
                       "@\"main.WATCH\" = dso_local constant ptr @\"main.counter\", align 8");
})

TEST(a_constant_that_names_another_one_copies_its_value, {
    // Constant references are evaluated lazily (D4.6), so an aggregate
    // constant named by another is that other's value.
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point ORIGIN = {1, 2};\npoint COPY = ORIGIN;\n"
                          "fn i32 main() { return COPY.x; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.COPY\" = dso_local constant %struct.main.point "
                             "{ i32 1, i32 2 }, align 4"),
                       "@\"main.COPY\" = dso_local constant %struct.main.point "
                       "{ i32 1, i32 2 }, align 4");
})

TEST(a_constant_may_hold_its_own_address, {
    // A self-pointing sentinel: the initializer names the global it
    // initializes, which is one relocation into itself (D7.10, item 5).
    TEST_ASSERT_TRUE(emit("struct node { i32 v; node* next; }\n"
                          "node N = node{7, &N};\n"
                          "fn i32 main() { return N.next->v; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.N\" = dso_local constant %struct.main.node "
                             "{ i32 7, ptr @\"main.N\" }, align 8"),
                       "@\"main.N\" = dso_local constant %struct.main.node "
                       "{ i32 7, ptr @\"main.N\" }, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_struct_constant_writes_its_fields_in_declaration_order, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point P = point{.y = 7, .x = 3};\n"
                          "fn i32 main() { return P.x; }\n"));
    // The designated form may name the fields in any order; the initializer
    // follows the declaration order the named type uses (D6.5, item 2).
    TEST_ASSERT_EQ_STR(found("@\"main.P\" = dso_local constant %struct.main.point "
                             "{ i32 3, i32 7 }, align 4"),
                       "@\"main.P\" = dso_local constant %struct.main.point "
                       "{ i32 3, i32 7 }, align 4");
})

TEST(a_field_the_designated_form_omits_is_zeroed, {
    TEST_ASSERT_TRUE(emit("struct trio { i32 a; i32 b; i32 c; }\n"
                          "trio T = trio{.b = 5};\n"
                          "fn i32 main() { return T.b; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.T\" = dso_local constant %struct.main.trio "
                             "{ i32 0, i32 5, i32 0 }, align 4"),
                       "@\"main.T\" = dso_local constant %struct.main.trio "
                       "{ i32 0, i32 5, i32 0 }, align 4");
})

TEST(a_nested_aggregate_constant_writes_the_type_of_every_member, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point[2] PTS = {{1, 2}, {3, 4}};\n"
                          "string[2] NAMES = {\"a\", \"bb\"};\n"
                          "fn i32 main() { return PTS[1].x; }\n"));
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

// ---- nested aggregates (item 5, D6.5) ----------------------------------------------

TEST(a_struct_inside_a_struct_is_written_with_its_own_type, {
    // Every member of a constant aggregate carries its memory type, so a
    // nested struct names the type of item 2 and not the bytes of one
    // (item 5).
    TEST_ASSERT_TRUE(emit("struct inner { i32 a; i32 b; }\n"
                          "struct outer { inner in; i64 n; }\n"
                          "outer O = {{1, 2}, 3};\n"
                          "fn i32 main() { return O.in.b; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.O\" = dso_local constant %struct.main.outer "
                             "{ %struct.main.inner { i32 1, i32 2 }, i64 3 }, align 8"),
                       "@\"main.O\" = dso_local constant %struct.main.outer "
                       "{ %struct.main.inner { i32 1, i32 2 }, i64 3 }, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_all_zero_member_of_a_nonzero_aggregate_collapses_by_itself, {
    // The collapse of item 5 applies at every level: the array below is not
    // all-zero, but its first element is.
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point[2] PTS = {{0, 0}, {1, 2}};\n"
                          "fn i32 main() { return PTS[1].x; }\n"));
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
                          "fn i32 main() { return PTS[0].y; }\n"));
    TEST_ASSERT_EQ_STR(found("[%struct.main.point { i32 0, i32 1 }, "
                             "%struct.main.point { i32 2, i32 3 }]"),
                       "[%struct.main.point { i32 0, i32 1 }, "
                       "%struct.main.point { i32 2, i32 3 }]");
})

TEST(a_typed_array_literal_is_the_same_constant_as_a_brace_list, {
    // `i32[3]{1, 2, 3}` carries its brace list in `b` (D6.5).
    TEST_ASSERT_TRUE(emit("i32[3] A = i32[3]{1, 2, 3};\n"
                          "fn i32 main() { return A[2]; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.A\" = dso_local constant [3 x i32] "
                             "[i32 1, i32 2, i32 3], align 4"),
                       "@\"main.A\" = dso_local constant [3 x i32] "
                       "[i32 1, i32 2, i32 3], align 4");
})

TEST(a_member_of_every_scalar_kind_keeps_its_memory_form, {
    // A `bool` member is `i8 0` or `i8 1` and never `i1 true`, a `char` is
    // its byte, an enum is its `i32` and a pointer member is an address
    // (D19.2, D3.9).
    TEST_ASSERT_TRUE(emit("enum color { red, green = 5 }\n"
                          "struct row { bool on; char tag; color hue; i32* link; string name; }\n"
                          "i32 TARGET = 1;\n"
                          "row R = {true, 'z', color.green, &TARGET, \"hey\"};\n"
                          "fn i32 main() { return *R.link; }\n"));
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
                          "fn i32 main() { return cast(ALL[2], i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.ALL\" = dso_local constant [3 x i32] "
                             "[i32 0, i32 5, i32 6], align 4"),
                       "@\"main.ALL\" = dso_local constant [3 x i32] "
                       "[i32 0, i32 5, i32 6], align 4");
})

TEST(a_string_a_global_holds_is_numbered_before_one_a_body_holds, {
    // The private data is numbered on first use and the globals are emitted
    // before the function definitions, so a global's string is `@.str.0`
    // (item 1, D19.5).
    TEST_ASSERT_TRUE(emit("string GREETING = \"hi\";\n"
                          "fn i32 main() {\n    println(GREETING, \" \", \"bye\");\n"
                          "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [3 x i8] c\"hi\\00\""),
                       "@.str.0 = private unnamed_addr constant [3 x i8] c\"hi\\00\"");
    // A string constant is assigned on first use and never deduplicated by
    // content (D19.5), so the `GREETING` the body prints is a second copy of
    // the same bytes under a second name. Nothing in fort reads that as one
    // address: strings compare by contents (D3.13), and whether the linker
    // merges the two `.rodata` entries is its business and not the
    // emitter's.
    TEST_ASSERT_EQ_STR(found("@.str.1 = private unnamed_addr constant [3 x i8] c\"hi\\00\""),
                       "@.str.1 = private unnamed_addr constant [3 x i8] c\"hi\\00\"");
    TEST_ASSERT_EQ_STR(found("@.str.3 = private unnamed_addr constant [4 x i8] c\"bye\\00\""),
                       "@.str.3 = private unnamed_addr constant [4 x i8] c\"bye\\00\"");
})

// ---- zeroinitializer (item 5) ------------------------------------------------------

TEST(an_all_zero_aggregate_is_a_zeroinitializer, {
    // An all-zero `global` lands in .bss and an all-zero `constant` in
    // .rodata's zero pages; either way the value is `zeroinitializer` (item
    // 5).
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point ORIGIN = {};\npoint mut cell = {};\n"
                          "i32[3] mut cells = {};\npoint[2] PAIR = {{0, 0}, {0, 0}};\n"
                          "fn i32 main() {\n    cell.x = 1;\n    cells[0] = 1;\n"
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
    // `= {}` zero-initializes a span or a `string` too (D6.5), and its
    // memory type is the one `%fort.span` of item 2.
    TEST_ASSERT_TRUE(emit("i32@ VIEW = {};\ni32 mut@ own mut pool = {};\nstring EMPTY = {};\n"
                          "fn i32 main() {\n    pool = new(i32, 1);\n    del(pool);\n"
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
    // `zeroinitializer` collapses an aggregate; a scalar prints its value
    // (item 5).
    TEST_ASSERT_TRUE(emit("i32 mut counter = 0;\ni32* NOWHERE = null;\nbool OFF = false;\n"
                          "fn i32 main() { return counter; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.counter\" = dso_local global i32 0, align 4"),
                       "@\"main.counter\" = dso_local global i32 0, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.OFF\" = dso_local constant i8 0, align 1"),
                       "@\"main.OFF\" = dso_local constant i8 0, align 1");
    TEST_ASSERT_EQ_STR(absent("i32 zeroinitializer"), "absent");
})

TEST(a_partly_zero_aggregate_is_written_out, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "point P = {0, 1};\n"
                          "fn i32 main() { return P.y; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.P\" = dso_local constant %struct.main.point "
                             "{ i32 0, i32 1 }, align 4"),
                       "@\"main.P\" = dso_local constant %struct.main.point "
                       "{ i32 0, i32 1 }, align 4");
})

TEST(a_zero_length_string_constant_is_not_all_zero, {
    // The header of a `string` holds the address of its bytes, which is a
    // relocation and never zero (D3.7, item 5).
    TEST_ASSERT_TRUE(emit("string EMPTY = \"\";\n"
                          "fn i32 main() { return cast(EMPTY.len, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.EMPTY\" = dso_local constant %fort.span "
                             "{ ptr @.str.0, i64 0 }, align 8"),
                       "@\"main.EMPTY\" = dso_local constant %fort.span "
                       "{ ptr @.str.0, i64 0 }, align 8");
})

// ---- reading and writing module-level data (D6.7, D7.10) ---------------------------

TEST(a_mut_global_is_read_and_written_through_its_symbol, {
    TEST_ASSERT_TRUE(emit("i32 mut counter = 0;\n"
                          "fn void bump() { counter = counter +% 1; }\n"
                          "fn i32 main() {\n    bump();\n    return counter;\n}\n"));
    TEST_ASSERT_EQ_STR(found("load i32, ptr @\"main.counter\", align 4"),
                       "load i32, ptr @\"main.counter\", align 4");
    TEST_ASSERT_EQ_STR(found("store i32 %t1, ptr @\"main.counter\", align 4"),
                       "store i32 %t1, ptr @\"main.counter\", align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_scalar_constant_is_folded_into_its_uses, {
    // A constant expression has no side effects and is emitted as a literal,
    // so a scalar constant's storage is never loaded (D4.6).
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 10;\n"
                          "fn i32 main() {\n    i32 mut n = 0;\n    n +%= LIMIT;\n"
                          "    return n;\n}\n"));
    TEST_ASSERT_EQ_STR(absent("load i32, ptr @\"main.LIMIT\""), "absent");
    TEST_ASSERT_EQ_STR(found("add i32 %t0, 10"), "add i32 %t0, 10");
})

TEST(an_aggregate_constant_is_read_out_of_its_storage, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\npoint ORIGIN = {1, 2};\n"
                          "fn i32 main() { return ORIGIN.y; }\n"));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %struct.main.point, "
                             "ptr @\"main.ORIGIN\", i32 0, i32 1"),
                       "getelementptr inbounds %struct.main.point, "
                       "ptr @\"main.ORIGIN\", i32 0, i32 1");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_address_of_a_constant_is_its_symbol, {
    TEST_ASSERT_TRUE(emit("i32 LIMIT = 10;\n"
                          "fn i32 main() {\n    i32* p = &LIMIT;\n    return *p;\n}\n"));
    TEST_ASSERT_EQ_STR(found("store ptr @\"main.LIMIT\", ptr %p.0, align 8"),
                       "store ptr @\"main.LIMIT\", ptr %p.0, align 8");
})

// ---- several modules (item 1, D9.10) -----------------------------------------------

TEST(each_module_emits_its_own_globals_once, {
    // Modules are emitted in dependency order, every module after the ones it
    // imports, and a constant belongs to the module that declares it (D9.10,
    // D19.5).
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import shapes;\n"
                              "i32 HERE = 1;\n"
                              "fn i32 main() { return shapes.SIDES + HERE; }\n",
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
                              "fn i32 main() {\n    store.total = 2;\n    return store.total;\n}\n",
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
                              "fn i32 main() { return *THERE; }\n",
                              "shapes.ft",
                              "i32 SIDES = 4;\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.THERE\" = dso_local constant ptr @\"shapes.SIDES\", align 8"),
                       "@\"main.THERE\" = dso_local constant ptr @\"shapes.SIDES\", align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_constant_of_another_modules_struct_type_names_that_modules_type, {
    // The named types of every module stand in one section before the
    // globals, so a constant may hold a struct another module declares
    // (item 1, item 2).
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import shapes;\n"
                              "shapes.square UNIT = {3};\n"
                              "fn i32 main() { return UNIT.side; }\n",
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
    // Top-level declarations are order-independent within a module (D7.10),
    // and a forward reference to a global is legal in `.ll` (item 1).
    TEST_ASSERT_TRUE(emit("fn i32 main() { return B; }\ni32 B = A * 2;\ni32 A = 3;\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.B\" = dso_local constant i32 6, align 4"),
                       "@\"main.B\" = dso_local constant i32 6, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.A\" = dso_local constant i32 3, align 4"),
                       "@\"main.A\" = dso_local constant i32 3, align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_declaration_nothing_refers_to_is_emitted_all_the_same, {
    // A fort constant or global has the default external linkage (D9.6), so
    // it is a definition of the module whether or not this program reads it.
    TEST_ASSERT_TRUE(emit("i32 NEVER = 5;\ni32 mut nobody = 7;\n"
                          "fn i32 main() { return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.NEVER\" = dso_local constant i32 5, align 4"),
                       "@\"main.NEVER\" = dso_local constant i32 5, align 4");
    TEST_ASSERT_EQ_STR(found("@\"main.nobody\" = dso_local global i32 7, align 4"),
                       "@\"main.nobody\" = dso_local global i32 7, align 4");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_declaration_that_failed_to_check_emits_no_global, {
    // A module-level initializer must be a constant expression (D7.10); the
    // checker reported it, so the emitter is silent and emits nothing for it
    // (D14.2).
    TEST_ASSERT_FALSE(emit("fn i32 f() { return 1; }\ni32 BAD = f();\n"
                           "fn i32 main() { return BAD; }\n"));
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
