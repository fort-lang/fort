// Unit tests of the enum values the emitter writes (toolchain.md 6 items 19
// and 21; D3.9, D19.2, D19.5): the `i32` representation and its scoped
// members, the casts to and from an integer type, the zero value, and the
// `%fort.enum_member` table a `print` of an enum reaches, which is named
// after the module that declares the enum and emitted once for the program.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the literals are the test data: the
// fort programs and the IR text each one must produce.

// An enum whose first member is negative, which is the class that tells a
// signed representation from an unsigned one (D3.9).
static const char SIGN_SOURCE[] = "enum sign { neg = -1, zero, pos }\n"
                                  "fn i32 main() {\n"
                                  "    sign s = sign.neg;\n"
                                  "    sign z = {};\n"
                                  "    i64 wide = cast(s, i64);\n"
                                  "    u8 narrow = cast(s, u8);\n"
                                  "    println(s, z, wide, narrow);\n"
                                  "    return 0;\n}\n";

// The enum of an imported module, printed on both sides of the boundary: one
// declaration, one table (item 21).
static const char PALETTE_SOURCE[] = "enum color { red, green }\n"
                                     "fn void show(color c) { println(c); }\n";
static const char PALETTE_APP[] = "import palette;\n"
                                  "fn i32 main() {\n"
                                  "    palette.color c = palette.color.green;\n"
                                  "    palette.show(c);\n"
                                  "    println(c);\n"
                                  "    return 0;\n}\n";

// ---- members and their values (D3.9) -----------------------------------------------

TEST(a_member_is_the_i32_its_declaration_order_gives, {
    TEST_ASSERT_TRUE(emit("enum color { red, green, blue }\n"
                          "fn i32 main() {\n    color a = color.red;\n"
                          "    color b = color.green;\n    color c = color.blue;\n"
                          "    return cast(a, i32) +% cast(b, i32) +% cast(c, i32);\n}\n"));
    // Values start at 0 and increment (D3.9), and a member is a constant the
    // checker folded, so it is stored as a literal (D4.6).
    const char* want = "  store i32 0, ptr %a.0, align 4\n"
                       "  store i32 1, ptr %b.1, align 4\n"
                       "  store i32 2, ptr %c.2, align 4\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_explicit_value_sets_the_members_after_it, {
    TEST_ASSERT_TRUE(emit("enum color { red, green = 5, blue }\n"
                          "fn i32 main() {\n    color b = color.blue;\n"
                          "    return cast(b, i32);\n}\n"));
    // An explicit value is a constant expression and the next member follows
    // it (D3.9).
    TEST_ASSERT_EQ_STR(found("  store i32 6, ptr %b.0, align 4\n"),
                       "  store i32 6, ptr %b.0, align 4\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_negative_member_prints_as_a_negative_i32, {
    TEST_ASSERT_TRUE(emit(SIGN_SOURCE));
    // A member's value may be negative, so the constant prints signed, the
    // way the table below prints it (D3.9, D19.5).
    TEST_ASSERT_EQ_STR(found("  store i32 -1, ptr %s.0, align 4\n"),
                       "  store i32 -1, ptr %s.0, align 4\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_zero_value_of_an_enum_is_zero_whatever_its_members_are, {
    TEST_ASSERT_TRUE(emit(SIGN_SOURCE));
    // A zeroed enum holds 0 even if 0 is not a member; here it is `zero`, and
    // `{}` is still one store of 0 and not a member lookup (D3.9, D6.5).
    TEST_ASSERT_EQ_STR(found("  store i32 0, ptr %z.1, align 4\n"),
                       "  store i32 0, ptr %z.1, align 4\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_zero_enum_is_stored_and_never_memset, {
    TEST_ASSERT_TRUE(emit("enum level { low = 1, high = 2 }\n"
                          "fn i32 main() {\n    level l = {};\n    return cast(l, i32);\n}\n"));
    // An enum is a scalar, so its zero value is a store and not the
    // `llvm.memset` an aggregate's `{}` is (D19.2, D19.3).
    TEST_ASSERT_EQ_STR(found("  store i32 0, ptr %l.0, align 4\n"),
                       "  store i32 0, ptr %l.0, align 4\n");
    TEST_ASSERT_EQ_STR(absent("llvm.memset"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- casts (D3.9, item 12) ---------------------------------------------------------

TEST(a_widening_cast_of_an_enum_is_a_sext, {
    TEST_ASSERT_TRUE(emit(SIGN_SOURCE));
    // The representation is signed, so a cast to a wider integer type
    // sign-extends and `cast(sign.neg, i64)` is -1 and not 4294967295
    // (D3.9, item 12).
    TEST_ASSERT_EQ_STR(found("  %t1 = sext i32 %t0 to i64\n"), "  %t1 = sext i32 %t0 to i64\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_narrowing_cast_of_an_enum_is_a_trunc, {
    TEST_ASSERT_TRUE(emit(SIGN_SOURCE));
    TEST_ASSERT_EQ_STR(found("  %t3 = trunc i32 %t2 to i8\n"), "  %t3 = trunc i32 %t2 to i8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_integer_cast_to_an_enum_is_unchecked, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    i64 v = 9;\n    color c = cast(v, color);\n"
                          "    return cast(c, i32);\n}\n"));
    // Int to enum is unchecked: the value is truncated to `i32` and stored,
    // with no compare against the member list (D3.9).
    TEST_ASSERT_EQ_STR(found("  %t1 = trunc i64 %t0 to i32\n  store i32 %t1, ptr %c.1, align 4\n"),
                       "  %t1 = trunc i64 %t0 to i32\n  store i32 %t1, ptr %c.1, align 4\n");
    TEST_ASSERT_EQ_STR(absent("fort_rt_fail"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_enums_compare_as_i32, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    color a = color.red;\n    color b = color.green;\n"
                          "    return cast(a == b, i32) +% cast(a != b, i32);\n}\n"));
    // Enums support `==` and `!=` alone, on the `i32` representation (D3.9).
    TEST_ASSERT_EQ_STR(found("icmp eq i32 %t0, %t1"), "icmp eq i32 %t0, %t1");
    TEST_ASSERT_EQ_STR(found("icmp ne i32 %t4, %t5"), "icmp ne i32 %t4, %t5");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the table of item 21 ----------------------------------------------------------

TEST(a_table_holds_a_negative_member_as_a_negative_i32, {
    TEST_ASSERT_TRUE(emit(SIGN_SOURCE));
    const char* want = "@.enum.main.sign = private unnamed_addr constant "
                       "[3 x %fort.enum_member] [%fort.enum_member { i32 -1, ptr @.str.0 }, "
                       "%fort.enum_member { i32 0, ptr @.str.1 }, "
                       "%fort.enum_member { i32 1, ptr @.str.2 }], align 8\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_member_name_is_a_string_constant_of_its_own, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() { println(color.red); return 0; }\n"));
    // The names the table points at are ordinary string constants, in
    // declaration order (item 21, item 5).
    const char* want = "@.str.0 = private unnamed_addr constant [4 x i8] c\"red\\00\", align 1\n"
                       "@.str.1 = private unnamed_addr constant [6 x i8] c\"green\\00\", align 1\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(one_table_serves_every_print_of_its_enum, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn void twice(color c) { println(c); println(c); }\n"
                          "fn i32 main() {\n    twice(color.red);\n    println(color.green);\n"
                          "    return 0;\n}\n"));
    // The table is assigned on first use and shared afterwards, so three
    // prints in two definitions emit one table (item 21, D19.5).
    TEST_ASSERT_EQ_UINT64(occurrences("@.enum.main.color = "), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("call void @fort_rt_print_enum"), (uint64_t)3);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_imported_enums_table_is_named_after_the_module_that_declares_it, {
    TEST_ASSERT_TRUE(emit_two("app.ft", PALETTE_APP, "palette.ft", PALETTE_SOURCE));
    // A table is one per enum declaration, so its name is the declaring
    // module's path and its name, whichever module prints it (item 21, D9.7).
    TEST_ASSERT_EQ_UINT64(occurrences("@.enum.palette.color = "), (uint64_t)1);
    TEST_ASSERT_EQ_STR(absent("@.enum.app."), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_modules_printing_one_enum_emit_one_table, {
    TEST_ASSERT_TRUE(emit_two("app.ft", PALETTE_APP, "palette.ft", PALETTE_SOURCE));
    // The print inside the imported module and the print in the importer
    // reach the same table, which is emitted once for the program (item 21).
    TEST_ASSERT_EQ_UINT64(occurrences("@.enum.palette.color = "), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("ptr @.enum.palette.color, i64 2)"), (uint64_t)2);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_module_that_never_prints_an_enum_emits_no_table, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    color c = color.green;\n"
                          "    return cast(c, i32);\n}\n"));
    // A table is emitted only for an enum some `print` reaches (item 21).
    TEST_ASSERT_EQ_STR(absent("@.enum."), "absent");
    TEST_ASSERT_EQ_STR(absent("%fort.enum_member {"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_enum_member_type_is_always_named, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { return 0; }\n"));
    // Both named types are emitted whether they are used or not (item 1), and
    // `%fort.enum_member` has C's 16-byte layout (item 21).
    TEST_ASSERT_EQ_STR(found("%fort.enum_member = type { i32, ptr }\n"),
                       "%fort.enum_member = type { i32, ptr }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_enum_program_is_reproduced_byte_for_byte, {
    static sb_t first;
    TEST_ASSERT_TRUE(emit(SIGN_SOURCE));
    sb_clear(&first);
    sb_append(&first, ir());
    TEST_ASSERT_TRUE(emit(SIGN_SOURCE));
    TEST_ASSERT_EQ_STR(ir(), sb_cstr(&first));
    sb_free(&first);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_enum", argc, argv);
    TEST_RUN(a_member_is_the_i32_its_declaration_order_gives);
    TEST_RUN(an_explicit_value_sets_the_members_after_it);
    TEST_RUN(a_negative_member_prints_as_a_negative_i32);
    TEST_RUN(the_zero_value_of_an_enum_is_zero_whatever_its_members_are);
    TEST_RUN(a_zero_enum_is_stored_and_never_memset);
    TEST_RUN(a_widening_cast_of_an_enum_is_a_sext);
    TEST_RUN(a_narrowing_cast_of_an_enum_is_a_trunc);
    TEST_RUN(an_integer_cast_to_an_enum_is_unchecked);
    TEST_RUN(two_enums_compare_as_i32);
    TEST_RUN(a_table_holds_a_negative_member_as_a_negative_i32);
    TEST_RUN(a_member_name_is_a_string_constant_of_its_own);
    TEST_RUN(one_table_serves_every_print_of_its_enum);
    TEST_RUN(an_imported_enums_table_is_named_after_the_module_that_declares_it);
    TEST_RUN(two_modules_printing_one_enum_emit_one_table);
    TEST_RUN(a_module_that_never_prints_an_enum_emits_no_table);
    TEST_RUN(the_enum_member_type_is_always_named);
    TEST_RUN(an_enum_program_is_reproduced_byte_for_byte);
    gen_done();
    TEST_EXIT();
}
