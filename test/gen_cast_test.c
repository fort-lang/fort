// Unit tests of the cast matrix in the emitted IR (toolchain.md 6 item 12):
// which instruction each conversion lowers to, and which conversions lower to
// no instruction at all. A wrong answer here is silent -- a program that casts
// wrongly still runs and still agrees with itself -- so every row the
// bootstrap admits is pinned against the emitted text. The float rows have no
// test: the bootstrap refuses floats before code generation
// (bootstrap-unsupported.txt).
// D3.14
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- integer to integer ------------------------------------------------------------
// D3.14

TEST(a_widening_cast_of_a_signed_source_is_a_sext, {
    // Widening extends by the source's signedness, not the target's.
    // D3.14
    TEST_ASSERT_TRUE(emit(in_main("    i32 a = 1;\n    i64 b = cast(a, i64);\n")));
    TEST_ASSERT_EQ_STR(found("sext i32 %t0 to i64"), "sext i32 %t0 to i64");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_widening_cast_of_a_signed_source_into_an_unsigned_target_still_sexts, {
    TEST_ASSERT_TRUE(emit(in_main("    i8 a = 1;\n    u64 b = cast(a, u64);\n")));
    TEST_ASSERT_EQ_STR(found("sext i8 %t0 to i64"), "sext i8 %t0 to i64");
})

TEST(a_widening_cast_of_an_unsigned_source_is_a_zext, {
    TEST_ASSERT_TRUE(emit(in_main("    u32 a = 1;\n    u64 b = cast(a, u64);\n")));
    TEST_ASSERT_EQ_STR(found("zext i32 %t0 to i64"), "zext i32 %t0 to i64");
})

TEST(a_widening_cast_of_an_unsigned_source_into_a_signed_target_still_zexts, {
    TEST_ASSERT_TRUE(emit(in_main("    u8 a = 1;\n    i64 b = cast(a, i64);\n")));
    TEST_ASSERT_EQ_STR(found("zext i8 %t0 to i64"), "zext i8 %t0 to i64");
})

TEST(a_narrowing_cast_is_a_trunc, {
    TEST_ASSERT_TRUE(emit(in_main("    i64 a = 1;\n    i8 b = cast(a, i8);\n")));
    TEST_ASSERT_EQ_STR(found("trunc i64 %t0 to i8"), "trunc i64 %t0 to i8");
})

TEST(a_narrowing_cast_of_an_unsigned_source_is_a_trunc_too, {
    TEST_ASSERT_TRUE(emit(in_main("    u64 a = 1;\n    u16 b = cast(a, u16);\n")));
    TEST_ASSERT_EQ_STR(found("trunc i64 %t0 to i16"), "trunc i64 %t0 to i16");
})

TEST(a_same_width_sign_change_emits_nothing, {
    // Same-width sign change reinterprets, and signedness is in the
    // instruction and never in the type, so the loaded value is stored as it
    // stands.
    // D3.14, D19.2
    TEST_ASSERT_TRUE(emit(in_main("    i32 a = 1;\n    u32 b = cast(a, u32);\n")));
    TEST_ASSERT_EQ_STR(found("%t0 = load i32, ptr %a.0, align 4\n"
                             "  store i32 %t0, ptr %b.1, align 4"),
                       "%t0 = load i32, ptr %a.0, align 4\n"
                       "  store i32 %t0, ptr %b.1, align 4");
})

// ---- bool and char -----------------------------------------------------------------
// D3.2, D3.3, D3.14

TEST(a_bool_to_integer_cast_is_a_zext_of_i1, {
    TEST_ASSERT_TRUE(emit(in_main("    bool b = true;\n    i32 n = cast(b, i32);\n")));
    TEST_ASSERT_EQ_STR(found("zext i1 %t2 to i32"), "zext i1 %t2 to i32");
})

TEST(a_bool_widens_to_a_byte_because_its_value_is_one_bit, {
    // A `bool` is one bit as a value, so a cast of it to `u8` is a widening
    // and emits a `zext`. The width comes from gen_int_bits, and an eight-bit
    // answer there makes the cast the identity: the `i1` is then stored into
    // the byte slot as it stands. Both modules run the same and `opt` accepts
    // the `store i1`, so the difference is visible in the text alone -- the
    // mutation that made gen_int_bits answer 8 left all 78 unit tests of that
    // tree and ten of the eleven tests of the lang label green, and turned
    // only `diff-ir` red, on one of 492 compared files. It is the one rule of
    // the emitter that no named assertion held. Every other target width is
    // blind to it, since a `zext i1` to `i32` reads the same whether the
    // source is called one bit wide or eight.
    // D3.3, D19.2, T-078: the audit that measured the mutation
    TEST_ASSERT_TRUE(emit(in_main("    bool b = true;\n    u8 n = cast(b, u8);\n"
                                  "    println(n);\n")));
    TEST_ASSERT_EQ_STR(found("  %t2 = trunc i8 %t1 to i1\n"
                             "  %t3 = zext i1 %t2 to i8\n"
                             "  store i8 %t3, ptr %n.1, align 1\n"),
                       "  %t2 = trunc i8 %t1 to i1\n"
                       "  %t3 = zext i1 %t2 to i8\n"
                       "  store i8 %t3, ptr %n.1, align 1\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
    // The same step on a `bool` constant, which the checker does not fold into
    // the cast: the `zext i1 true` is what says so.
    // D4.6
    TEST_ASSERT_TRUE(emit(in_main("    println(cast(true, u8));\n")));
    TEST_ASSERT_EQ_STR(found("  %t0 = zext i1 true to i8\n  %t1 = zext i8 %t0 to i64\n"),
                       "  %t0 = zext i1 true to i8\n  %t1 = zext i8 %t0 to i64\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_bool_to_bool_cast_emits_nothing, {
    // The identity: `zext i1 ... to i1` is not an instruction, so a cast of a
    // `bool` to `bool` must emit none.
    // D3.14
    TEST_ASSERT_TRUE(emit(in_main("    bool b = true;\n    bool c = cast(b, bool);\n")));
    // The `trunc` and the `zext` below are the load and the store of a `bool`
    // place, which is `i8` in memory; the cast itself adds nothing between
    // them.
    // D19.2
    TEST_ASSERT_EQ_STR(found("%t1 = load i8, ptr %b.0, align 1\n"
                             "  %t2 = trunc i8 %t1 to i1\n"
                             "  %t3 = zext i1 %t2 to i8\n"
                             "  store i8 %t3, ptr %c.1, align 1"),
                       "%t1 = load i8, ptr %b.0, align 1\n"
                       "  %t2 = trunc i8 %t1 to i1\n"
                       "  %t3 = zext i1 %t2 to i8\n"
                       "  store i8 %t3, ptr %c.1, align 1");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_char_to_integer_cast_zero_extends, {
    // `char` is an unsigned byte, so widening it zero-extends.
    // D3.2
    TEST_ASSERT_TRUE(emit(in_main("    char c = 'q';\n    i32 n = cast(c, i32);\n")));
    TEST_ASSERT_EQ_STR(found("zext i8 %t0 to i32"), "zext i8 %t0 to i32");
})

TEST(an_integer_to_char_cast_truncates, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 n = 65;\n    char c = cast(n, char);\n")));
    TEST_ASSERT_EQ_STR(found("trunc i32 %t0 to i8"), "trunc i32 %t0 to i8");
})

TEST(a_char_to_byte_cast_emits_nothing, {
    TEST_ASSERT_TRUE(emit(in_main("    char c = 'q';\n    u8 b = cast(c, u8);\n")));
    TEST_ASSERT_EQ_STR(found("%t0 = load i8, ptr %c.0, align 1\n"
                             "  store i8 %t0, ptr %b.1, align 1"),
                       "%t0 = load i8, ptr %c.0, align 1\n"
                       "  store i8 %t0, ptr %b.1, align 1");
})

// ---- enums -------------------------------------------------------------------------
// D3.9, D3.14

TEST(a_widening_cast_of_an_enum_sign_extends, {
    // An enum's underlying `i32` is the signed type, so a member may be
    // negative and a widening cast sign-extends.
    // D3.1, D3.9
    TEST_ASSERT_TRUE(emit("enum color { red = -1, green }\n"
                          "fn i32 main() {\n    color k = color.green;\n"
                          "    i64 n = cast(k, i64);\n    println(n);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("sext i32 %t0 to i64"), "sext i32 %t0 to i64");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_enum_to_i32_cast_emits_nothing, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    color k = color.green;\n"
                          "    return cast(k, i32);\n}\n"));
    TEST_ASSERT_EQ_STR(absent("to i32"), "absent");
})

TEST(a_widening_cast_into_an_enum_reads_the_sources_signedness, {
    // The target being an enum changes nothing: the source decides.
    // D3.14
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    u8 b = 1;\n    color k = cast(b, color);\n"
                          "    i8 c = -1;\n    color j = cast(c, color);\n"
                          "    println(k == j);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("zext i8 %t0 to i32"), "zext i8 %t0 to i32");
    TEST_ASSERT_EQ_STR(found("sext i8 %t2 to i32"), "sext i8 %t2 to i32");
})

TEST(a_narrowing_cast_into_an_enum_truncates, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    i64 n = 1;\n    color k = cast(n, color);\n"
                          "    return cast(k, i32);\n}\n"));
    TEST_ASSERT_EQ_STR(found("trunc i64 %t0 to i32"), "trunc i64 %t0 to i32");
})

// ---- pointers ----------------------------------------------------------------------
// D3.10, D3.11, D3.14

TEST(a_pointer_and_u64_round_trip_uses_ptrtoint_and_inttoptr, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 v = 1;\n    i32* p = &v;\n"
                                  "    u64 n = cast(p, u64);\n    void* q = cast(n, void*);\n")));
    TEST_ASSERT_EQ_STR(found("ptrtoint ptr %t0 to i64"), "ptrtoint ptr %t0 to i64");
    TEST_ASSERT_EQ_STR(found("inttoptr i64 %t2 to ptr"), "inttoptr i64 %t2 to ptr");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_pointer_to_pointer_cast_emits_nothing, {
    // Every pointer is the opaque `ptr`, so a pointer cast is a no-op whatever
    // it changes: the pointee type, `void*`, or a mark the source carries and
    // the target drops.
    // D3.14
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut v = 1;\n    i32 mut* p = &v;\n"
                                  "    void mut* q = cast(p, void mut*);\n"
                                  "    i32 mut* w = cast(q, i32 mut*);\n"
                                  "    u8* r = cast(q, u8*);\n    *w = 2;\n"
                                  "    println(v, \" \", r == null);\n")));
    TEST_ASSERT_EQ_STR(absent("bitcast"), "absent");
    TEST_ASSERT_EQ_STR(absent("addrspacecast"), "absent");
    TEST_ASSERT_EQ_STR(absent("inttoptr"), "absent");
    TEST_ASSERT_EQ_STR(absent("ptrtoint"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_function_pointer_and_void_pointer_cast_emits_nothing, {
    TEST_ASSERT_TRUE(emit("fn i32 twice(i32 x) { return x +% x; }\n"
                          "fn i32 main() {\n    fn i32(i32) f = twice;\n"
                          "    void* v = cast(f, void*);\n"
                          "    fn i32(i32) g = cast(v, fn i32(i32));\n"
                          "    return g(1);\n}\n"));
    TEST_ASSERT_EQ_STR(absent("bitcast"), "absent");
    TEST_ASSERT_EQ_STR(absent("ptrtoint"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- spans and strings -------------------------------------------------------------
// D3.5, D3.7, D3.14

TEST(a_span_cast_that_only_changes_marks_emits_no_conversion, {
    // A span cast never changes the element type, so the header is copied and
    // nothing else happens (item 12).
    // D3.14
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut@ own s = new(i32, 3);\n"
                                  "    s[0] = 7;\n"
                                  "    i32@ t = cast(s, i32@);\n"
                                  "    println(t[0], \" \", t.len);\n    del(s);\n")));
    TEST_ASSERT_EQ_STR(absent("bitcast"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_string_to_byte_span_cast_copies_the_header_alone, {
    // `string`, `char@` and `u8@` are one IR type, so a cast among them is a
    // copy of the two header fields (item 12).
    // D3.7, D3.14
    TEST_ASSERT_TRUE(emit(in_main("    string s = \"hi\";\n"
                                  "    u8@ b = cast(s, u8@);\n"
                                  "    println(b.len, \" \", b[0]);\n")));
    TEST_ASSERT_EQ_STR(absent("bitcast"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- ownership ---------------------------------------------------------------------
// D17.4, D17.5, D3.14

TEST(an_own_dropping_cast_emits_nothing, {
    // Dropping `own` is an implicit conversion, so the cast that spells it has
    // no instruction of its own.
    // D3.14, D17.4
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut* own p = new(i32);\n"
                                  "    i32* v = cast(p, i32*);\n"
                                  "    println(v == null);\n    del(p);\n")));
    TEST_ASSERT_EQ_STR(absent("bitcast"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_adopting_cast_emits_nothing, {
    // Adding `own` to a pointer is adoption, which is a mark and not a
    // conversion.
    // D3.14, D17.3
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut* own p = new(i32);\n"
                                  "    i32 mut* v = p;\n"
                                  "    i32 mut* own q = cast(v, i32 mut* own);\n"
                                  "    del(q);\n")));
    TEST_ASSERT_EQ_STR(absent("bitcast"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_cast", argc, argv);
    TEST_RUN(a_widening_cast_of_a_signed_source_is_a_sext);
    TEST_RUN(a_widening_cast_of_a_signed_source_into_an_unsigned_target_still_sexts);
    TEST_RUN(a_widening_cast_of_an_unsigned_source_is_a_zext);
    TEST_RUN(a_widening_cast_of_an_unsigned_source_into_a_signed_target_still_zexts);
    TEST_RUN(a_narrowing_cast_is_a_trunc);
    TEST_RUN(a_narrowing_cast_of_an_unsigned_source_is_a_trunc_too);
    TEST_RUN(a_same_width_sign_change_emits_nothing);
    TEST_RUN(a_bool_to_integer_cast_is_a_zext_of_i1);
    TEST_RUN(a_bool_widens_to_a_byte_because_its_value_is_one_bit);
    TEST_RUN(a_bool_to_bool_cast_emits_nothing);
    TEST_RUN(a_char_to_integer_cast_zero_extends);
    TEST_RUN(an_integer_to_char_cast_truncates);
    TEST_RUN(a_char_to_byte_cast_emits_nothing);
    TEST_RUN(a_widening_cast_of_an_enum_sign_extends);
    TEST_RUN(an_enum_to_i32_cast_emits_nothing);
    TEST_RUN(a_widening_cast_into_an_enum_reads_the_sources_signedness);
    TEST_RUN(a_narrowing_cast_into_an_enum_truncates);
    TEST_RUN(a_pointer_and_u64_round_trip_uses_ptrtoint_and_inttoptr);
    TEST_RUN(a_pointer_to_pointer_cast_emits_nothing);
    TEST_RUN(a_function_pointer_and_void_pointer_cast_emits_nothing);
    TEST_RUN(a_span_cast_that_only_changes_marks_emits_no_conversion);
    TEST_RUN(a_string_to_byte_span_cast_copies_the_header_alone);
    TEST_RUN(an_own_dropping_cast_emits_nothing);
    TEST_RUN(an_adopting_cast_emits_nothing);
    gen_done();
    TEST_EXIT();
}
