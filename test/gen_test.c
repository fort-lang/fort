// Unit tests of the LLVM IR emitter: the module skeleton, the type mapping,
// symbols, private data, locals, calls and the print family (toolchain.md 6
// items 1 to 13 and 19; D19.1 to D19.5). The runtime checks are the other
// half, in gen_check_test.c, the cast matrix of D3.14 is in gen_cast_test.c,
// the module-level data of D7.10 in gen_global_test.c, and the whole-module
// goldens are in gen_module_test.c.
#include "gen.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// ---- the module skeleton (item 1) --------------------------------------------------

TEST(the_module_begins_with_the_normalized_triple, {
    TEST_ASSERT_TRUE(emit(in_main("")));
    TEST_ASSERT_EQ_INT64(at("target triple = \"x86_64-unknown-linux-gnu\"\n"), (int64_t)0);
})

TEST(the_module_carries_no_datalayout_or_module_flags, {
    TEST_ASSERT_TRUE(emit(in_main("")));
    // No datalayout, module flags, ident or source_filename (D19.1).
    TEST_ASSERT_EQ_STR(absent("target datalayout"), "absent");
    TEST_ASSERT_EQ_STR(absent("!llvm.module.flags"), "absent");
    TEST_ASSERT_EQ_STR(absent("!llvm.ident"), "absent");
    TEST_ASSERT_EQ_STR(absent("source_filename"), "absent");
})

TEST(the_module_carries_no_comment, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"x\");\n")));
    TEST_ASSERT_EQ_STR(absent(";"), "absent");
})

TEST(both_named_types_are_emitted_used_or_not, {
    TEST_ASSERT_TRUE(emit(in_main("")));
    TEST_ASSERT_EQ_STR(
        found("%fort.span = type { ptr, i64 }\n%fort.enum_member = type { i32, ptr }"),
        "%fort.span = type { ptr, i64 }\n%fort.enum_member = type { i32, ptr }");
})

TEST(the_sections_appear_in_the_order_of_item_1, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    println(\"x\");\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(before("target triple", "%fort.span"));
    TEST_ASSERT_TRUE(before("%fort.span", "%struct.main.point"));
    TEST_ASSERT_TRUE(before("%struct.main.point", "define dso_local"));
    TEST_ASSERT_TRUE(before("define dso_local", "@.str.0"));
    TEST_ASSERT_TRUE(before("@.str.0", "declare void @fort_rt_print_str"));
    TEST_ASSERT_TRUE(before("declare void @fort_rt_print_str", "attributes #0"));
})

TEST(a_blank_line_stands_between_two_definitions, {
    TEST_ASSERT_TRUE(emit("fn i32 one() { return 1; }\nfn i32 main() { return one(); }\n"));
    TEST_ASSERT_EQ_STR(found("}\n\ndefine dso_local i32 @\"main.main\""),
                       "}\n\ndefine dso_local i32 @\"main.main\"");
})

// ---- the type mapping (item 2, D19.2) ----------------------------------------------

TEST(the_integer_types_map_to_their_widths, {
    TEST_ASSERT_TRUE(
        emit(in_main("    i8 a = 1;\n    i16 b = 1;\n    i32 c = 1;\n    i64 d = 1;\n")));
    TEST_ASSERT_EQ_STR(found("%a.0 = alloca i8, align 1"), "%a.0 = alloca i8, align 1");
    TEST_ASSERT_EQ_STR(found("%b.1 = alloca i16, align 2"), "%b.1 = alloca i16, align 2");
    TEST_ASSERT_EQ_STR(found("%c.2 = alloca i32, align 4"), "%c.2 = alloca i32, align 4");
    TEST_ASSERT_EQ_STR(found("%d.3 = alloca i64, align 8"), "%d.3 = alloca i64, align 8");
})

TEST(the_unsigned_types_share_the_signed_ones_ir_types, {
    TEST_ASSERT_TRUE(
        emit(in_main("    u8 a = 1;\n    u16 b = 1;\n    u32 c = 1;\n    u64 d = 1;\n")));
    TEST_ASSERT_EQ_STR(found("%a.0 = alloca i8, align 1"), "%a.0 = alloca i8, align 1");
    TEST_ASSERT_EQ_STR(found("%b.1 = alloca i16, align 2"), "%b.1 = alloca i16, align 2");
    TEST_ASSERT_EQ_STR(found("%c.2 = alloca i32, align 4"), "%c.2 = alloca i32, align 4");
    TEST_ASSERT_EQ_STR(found("%d.3 = alloca i64, align 8"), "%d.3 = alloca i64, align 8");
})

TEST(a_bool_is_i8_in_memory_and_i1_as_a_value, {
    TEST_ASSERT_TRUE(emit(in_main("    bool b = true;\n    bool c = b;\n")));
    // Every store of a bool place is a zext and a store i8, a constant one
    // included, so the rule has no exception to get wrong (D19.2).
    TEST_ASSERT_EQ_STR(found("%b.0 = alloca i8, align 1"), "%b.0 = alloca i8, align 1");
    TEST_ASSERT_EQ_STR(found("%t0 = zext i1 true to i8"), "%t0 = zext i1 true to i8");
    TEST_ASSERT_EQ_STR(found("store i8 %t0, ptr %b.0, align 1"), "store i8 %t0, ptr %b.0, align 1");
    // Every load of one is a load i8 and a trunc.
    TEST_ASSERT_EQ_STR(found("%t1 = load i8, ptr %b.0, align 1"),
                       "%t1 = load i8, ptr %b.0, align 1");
    TEST_ASSERT_EQ_STR(found("%t2 = trunc i8 %t1 to i1"), "%t2 = trunc i8 %t1 to i1");
    TEST_ASSERT_EQ_STR(found("%t3 = zext i1 %t2 to i8"), "%t3 = zext i1 %t2 to i8");
})

TEST(a_char_is_an_unsigned_byte, {
    TEST_ASSERT_TRUE(emit(in_main("    char c = 'a';\n")));
    TEST_ASSERT_EQ_STR(found("%c.0 = alloca i8, align 1"), "%c.0 = alloca i8, align 1");
    TEST_ASSERT_EQ_STR(found("store i8 97, ptr %c.0, align 1"), "store i8 97, ptr %c.0, align 1");
})

TEST(a_pointer_is_the_opaque_ptr, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 v = 1;\n    i32* p = &v;\n    void* q = null;\n")));
    TEST_ASSERT_EQ_STR(found("%p.1 = alloca ptr, align 8"), "%p.1 = alloca ptr, align 8");
    TEST_ASSERT_EQ_STR(found("%q.2 = alloca ptr, align 8"), "%q.2 = alloca ptr, align 8");
    TEST_ASSERT_EQ_STR(found("store ptr null, ptr %q.2, align 8"),
                       "store ptr null, ptr %q.2, align 8");
})

TEST(a_fixed_array_is_a_bracketed_type, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[3] a = {};\n")));
    TEST_ASSERT_EQ_STR(found("%a.0 = alloca [3 x i32], align 4"),
                       "%a.0 = alloca [3 x i32], align 4");
})

TEST(a_string_and_a_span_share_one_named_type, {
    TEST_ASSERT_TRUE(emit(in_main("    string s = \"hi\";\n")));
    TEST_ASSERT_EQ_STR(found("%s.0 = alloca %fort.span, align 8"),
                       "%s.0 = alloca %fort.span, align 8");
})

TEST(an_enum_is_i32, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    color g = color.green;\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("%g.0 = alloca i32, align 4"), "%g.0 = alloca i32, align 4");
    TEST_ASSERT_EQ_STR(found("store i32 1, ptr %g.0, align 4"), "store i32 1, ptr %g.0, align 4");
})

TEST(a_struct_is_a_named_type_with_its_fields_in_order, {
    TEST_ASSERT_TRUE(emit("struct mixed { u8 tag; i64 value; }\n"
                          "fn i32 main() {\n    mixed m = {};\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("%struct.main.mixed = type { i8, i64 }"),
                       "%struct.main.mixed = type { i8, i64 }");
    // The struct is never packed, so LLVM lays it out exactly as C does
    // (D3.8).
    TEST_ASSERT_EQ_STR(absent("<{"), "absent");
    TEST_ASSERT_EQ_STR(found("%m.0 = alloca %struct.main.mixed, align 8"),
                       "%m.0 = alloca %struct.main.mixed, align 8");
})

TEST(a_void_function_has_the_void_result_type, {
    TEST_ASSERT_TRUE(emit("fn void nothing() { return; }\n"
                          "fn i32 main() { nothing(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"main.nothing\"() #0"),
                       "define dso_local void @\"main.nothing\"() #0");
    TEST_ASSERT_EQ_STR(found("  ret void"), "  ret void");
})

// ---- aggregates and the three getelementptr shapes (item 3) -----------------------

TEST(an_array_element_uses_the_array_gep_shape, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[3] a = {};\n    i64 i = 1;\n    println(a[i]);\n")));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0"),
                       "getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0");
})

TEST(a_struct_field_uses_the_field_gep_shape, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    point p = {1, 2};\n"
                          "    println(p.y);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 1"),
                       "getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 1");
})

TEST(a_span_header_field_uses_the_field_gep_shape, {
    TEST_ASSERT_TRUE(emit(in_main("    string s = \"hi\";\n    println(s.len);\n")));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1"),
                       "getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1");
})

TEST(a_string_element_uses_the_element_gep_shape, {
    TEST_ASSERT_TRUE(emit(in_main("    string s = \"hi\";\n    u64 i = 0;\n    println(s[i]);\n")));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds i8, ptr %t"),
                       "getelementptr inbounds i8, ptr %t");
})

TEST(every_getelementptr_carries_inbounds, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    point p = {1, 2};\n    i32[2] a = {};\n"
                          "    i64 i = 0;\n    println(p.x, a[i]);\n    return 0;\n}\n"));
    const char* cursor = ir();
    const char* hit = strstr(cursor, "getelementptr");
    while (hit != NULL) {
        TEST_ASSERT_EQ_INT32((int32_t)strncmp(hit, "getelementptr inbounds ", 23), 0);
        hit = strstr(hit + 1, "getelementptr");
    }
})

TEST(an_aggregate_copy_is_a_memcpy, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    point p = {1, 2};\n    point q = p;\n"
                          "    return q.x;\n}\n"));
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memcpy.p0.p0.i64(ptr align 4 %q.1, ptr align 4 %p.0, i64 8, i1 "
              "false)"),
        "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %q.1, ptr align 4 %p.0, i64 8, i1 false)");
})

TEST(a_brace_zero_initializer_is_a_memset, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[3] a = {};\n")));
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memset.p0.i64(ptr align 4 %a.0, i8 0, i64 12, i1 false)"),
        "call void @llvm.memset.p0.i64(ptr align 4 %a.0, i8 0, i64 12, i1 false)");
})

TEST(the_emitter_never_writes_insertvalue_on_an_aggregate, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    point p = {1, 2};\n    point q = p;\n"
                          "    return q.y;\n}\n"));
    // Only scalars are SSA values (D19.3).
    TEST_ASSERT_EQ_STR(absent("insertvalue"), "absent");
    TEST_ASSERT_EQ_STR(absent("extractvalue"), "absent");
})

// ---- symbols, linkage and visibility (item 4, D9.7) -------------------------------

TEST(a_fort_function_is_a_quoted_dotted_name, {
    TEST_ASSERT_TRUE(emit("fn i32 add(i32 a, i32 b) { return a +% b; }\n"
                          "fn i32 main() { return add(1, 2); }\n"));
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"main.add\"(i32 %a.in, i32 %b.in) #0"),
                       "define dso_local i32 @\"main.add\"(i32 %a.in, i32 %b.in) #0");
    TEST_ASSERT_EQ_STR(found("call i32 @\"main.add\"(i32 1, i32 2)"),
                       "call i32 @\"main.add\"(i32 1, i32 2)");
})

TEST(an_extern_name_is_unmangled_and_not_dso_local, {
    TEST_ASSERT_TRUE(emit("extern fn i32 puts(char* s);\n"
                          "fn i32 main() { string s = \"x\"; return puts(s.ptr); }\n"));
    TEST_ASSERT_EQ_STR(found("declare i32 @puts(ptr, ...)"), "declare i32 @puts(ptr, ...)");
    TEST_ASSERT_EQ_STR(absent("dso_local i32 @puts"), "absent");
})

TEST(fort_entry_is_a_c_name_and_unquoted, {
    TEST_ASSERT_TRUE(emit(in_main("")));
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @fort_entry(ptr %args.in) #0"),
                       "define dso_local i32 @fort_entry(ptr %args.in) #0");
})

// ---- private data (item 5) ---------------------------------------------------------

TEST(a_string_literal_holds_the_trailing_nul_the_length_excludes, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"before\");\n")));
    TEST_ASSERT_EQ_STR(
        found("@.str.0 = private unnamed_addr constant [7 x i8] c\"before\\00\", align 1"),
        "@.str.0 = private unnamed_addr constant [7 x i8] c\"before\\00\", align 1");
    TEST_ASSERT_EQ_STR(found("ptr @.str.0, i64 6"), "ptr @.str.0, i64 6");
})

TEST(a_non_printable_byte_is_written_as_a_hex_pair, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"a\\nb\\\\c\\\"d\");\n")));
    TEST_ASSERT_EQ_STR(found("c\"a\\0Ab\\5Cc\\22d\\00\""), "c\"a\\0Ab\\5Cc\\22d\\00\"");
})

TEST(an_empty_string_literal_is_one_nul_byte, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"\");\n")));
    TEST_ASSERT_EQ_STR(found("[1 x i8] c\"\\00\""), "[1 x i8] c\"\\00\"");
    TEST_ASSERT_EQ_STR(found("ptr @.str.0, i64 0"), "ptr @.str.0, i64 0");
})

TEST(a_string_constant_is_not_deduplicated_by_content, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"x\", \"x\");\n")));
    // Module-level counters are assigned on first use and never deduplicated
    // by content (D19.5).
    TEST_ASSERT_EQ_STR(found("@.str.0 = private unnamed_addr constant [2 x i8] c\"x\\00\""),
                       "@.str.0 = private unnamed_addr constant [2 x i8] c\"x\\00\"");
    TEST_ASSERT_EQ_STR(found("@.str.1 = private unnamed_addr constant [2 x i8] c\"x\\00\""),
                       "@.str.1 = private unnamed_addr constant [2 x i8] c\"x\\00\"");
})

TEST(a_program_with_no_check_has_no_file_constant, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"hi\");\n")));
    TEST_ASSERT_EQ_STR(absent("@.file."), "absent");
})

TEST(one_file_constant_serves_every_check_of_that_file, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut a = 1;\n    a = a + 1;\n    a = a + 2;\n")));
    TEST_ASSERT_EQ_STR(found("@.file.0 = private unnamed_addr constant [8 x i8] c\"main.ft\\00\""),
                       "@.file.0 = private unnamed_addr constant [8 x i8] c\"main.ft\\00\"");
    TEST_ASSERT_EQ_STR(absent("@.file.1"), "absent");
})

TEST(the_file_constant_holds_the_name_the_compiler_opened, {
    TEST_ASSERT_TRUE(emit_as("abort.ft", in_main("    i32 mut a = 1;\n    a = a + 1;\n")));
    TEST_ASSERT_EQ_STR(found("c\"abort.ft\\00\""), "c\"abort.ft\\00\"");
})

TEST(file_constants_precede_string_constants, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut a = 1;\n    println(\"x\");\n    a = a + 1;\n")));
    TEST_ASSERT_TRUE(before("@.file.0 =", "@.str.0 ="));
})

TEST(an_enum_table_is_emitted_only_when_a_print_reaches_it, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() {\n    color g = color.red;\n"
                          "    return cast(g, i32);\n}\n"));
    TEST_ASSERT_EQ_STR(absent("@.enum."), "absent");
})

TEST(an_enum_table_has_one_entry_per_member_in_declaration_order, {
    TEST_ASSERT_TRUE(emit("enum color { red, green = 5, blue }\n"
                          "fn i32 main() {\n    color g = color.blue;\n"
                          "    println(g);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(
        found(
            "@.enum.main.color = private unnamed_addr constant [3 x %fort.enum_member] "
            "[%fort.enum_member { i32 0, ptr @.str.0 }, %fort.enum_member { i32 5, ptr @.str.1 }, "
            "%fort.enum_member { i32 6, ptr @.str.2 }], align 8"),
        "@.enum.main.color = private unnamed_addr constant [3 x %fort.enum_member] "
        "[%fort.enum_member { i32 0, ptr @.str.0 }, %fort.enum_member { i32 5, ptr @.str.1 }, "
        "%fort.enum_member { i32 6, ptr @.str.2 }], align 8");
    TEST_ASSERT_TRUE(before("@.str.2 = ", "@.enum.main.color = "));
})

// ---- the calling convention (item 7, D9.9) ----------------------------------------

TEST(a_narrow_parameter_and_result_carry_the_extension_attribute, {
    TEST_ASSERT_TRUE(emit("fn i8 narrow(i8 a, i16 b, u8 c, u16 d, bool e, char f) { return a; }\n"
                          "fn i32 main() { return cast(narrow(1, 2, 3, 4, true, 'x'), i32); }\n"));
    TEST_ASSERT_EQ_STR(found("define dso_local signext i8 @\"main.narrow\"(i8 signext %a.in, "
                             "i16 signext %b.in, i8 zeroext %c.in, i16 zeroext %d.in, "
                             "i1 zeroext %e.in, i8 zeroext %f.in) #0"),
                       "define dso_local signext i8 @\"main.narrow\"(i8 signext %a.in, "
                       "i16 signext %b.in, i8 zeroext %c.in, i16 zeroext %d.in, "
                       "i1 zeroext %e.in, i8 zeroext %f.in) #0");
    TEST_ASSERT_EQ_STR(found("call signext i8 @\"main.narrow\"(i8 signext 1, i16 signext 2, "
                             "i8 zeroext 3, i16 zeroext 4, i1 zeroext true, i8 zeroext 120)"),
                       "call signext i8 @\"main.narrow\"(i8 signext 1, i16 signext 2, "
                       "i8 zeroext 3, i16 zeroext 4, i1 zeroext true, i8 zeroext 120)");
})

TEST(an_i32_parameter_carries_no_extension_attribute, {
    TEST_ASSERT_TRUE(emit("fn i32 wide(i32 a, i64 b) { return a; }\n"
                          "fn i32 main() { return wide(1, 2); }\n"));
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"main.wide\"(i32 %a.in, i64 %b.in) #0"),
                       "define dso_local i32 @\"main.wide\"(i32 %a.in, i64 %b.in) #0");
})

TEST(an_aggregate_argument_is_a_pointer_to_a_caller_made_copy, {
    TEST_ASSERT_TRUE(emit("fn u64 size(string s) { return s.len; }\n"
                          "fn i32 main() { string s = \"hi\"; println(size(s)); return 0; }\n"));
    // byval is never used: the pointer goes in the integer slot (item 7).
    TEST_ASSERT_EQ_STR(absent("byval"), "absent");
    TEST_ASSERT_EQ_STR(found("define dso_local i64 @\"main.size\"(ptr %s.in) #0"),
                       "define dso_local i64 @\"main.size\"(ptr %s.in) #0");
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %fort.span, align 8"),
                       "%tmp0 = alloca %fort.span, align 8");
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp0, ptr align 8 %s.0, i64 16, "
              "i1 false)"),
        "call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp0, ptr align 8 %s.0, i64 16, i1 false)");
    TEST_ASSERT_EQ_STR(found("call i64 @\"main.size\"(ptr %tmp0)"),
                       "call i64 @\"main.size\"(ptr %tmp0)");
})

TEST(an_aggregate_result_is_a_leading_sret_pointer_on_a_void_function, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn point make() { point p = {1, 2}; return p; }\n"
                          "fn i32 main() { point q = make(); return q.x; }\n"));
    TEST_ASSERT_EQ_STR(
        found("define dso_local void @\"main.make\"(ptr sret(%struct.main.point) %ret.sret) #0"),
        "define dso_local void @\"main.make\"(ptr sret(%struct.main.point) %ret.sret) #0");
    TEST_ASSERT_EQ_STR(found("call void @\"main.make\"(ptr %q.0)"),
                       "call void @\"main.make\"(ptr %q.0)");
})

TEST(every_fort_definition_carries_the_attribute_group_of_item_7, {
    TEST_ASSERT_TRUE(emit(in_main("")));
    TEST_ASSERT_EQ_STR(
        found("attributes #0 = { nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\" "
              "}"),
        "attributes #0 = { nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\" }");
    // mustprogress is deliberately absent everywhere (toolchain.md 6).
    TEST_ASSERT_EQ_STR(absent("mustprogress"), "absent");
})

TEST(a_noreturn_definition_carries_its_own_group_and_ends_in_a_trap, {
    TEST_ASSERT_TRUE(emit("fn noreturn stop() { panic(\"x\"); }\n"
                          "fn i32 main() { stop(); }\n"));
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"main.stop\"() #1"),
                       "define dso_local void @\"main.stop\"() #1");
    TEST_ASSERT_EQ_STR(found("attributes #1 = { noreturn nounwind \"frame-pointer\"=\"all\" "
                             "\"probe-stack\"=\"inline-asm\" }"),
                       "attributes #1 = { noreturn nounwind \"frame-pointer\"=\"all\" "
                       "\"probe-stack\"=\"inline-asm\" }");
    // Every call site of a noreturn function traps too (D8.5, D19.7).
    TEST_ASSERT_EQ_STR(found("call void @\"main.stop\"()\n  call void @llvm.trap()\n  unreachable"),
                       "call void @\"main.stop\"()\n  call void @llvm.trap()\n  unreachable");
    TEST_ASSERT_EQ_STR(
        found("attributes #7 = { cold noreturn nounwind memory(inaccessiblemem: write) }"),
        "attributes #7 = { cold noreturn nounwind memory(inaccessiblemem: write) }");
})

// ---- normalization (item 9) ---------------------------------------------------------

TEST(a_narrow_value_keeps_its_own_width, {
    TEST_ASSERT_TRUE(emit(in_main("    i8 a = 1;\n    i8 b = 2;\n    println(a +% b);\n")));
    // A narrow value is not widened to 32 bits: its width is in its type
    // (item 9).
    TEST_ASSERT_EQ_STR(found("add i8 %t0, %t1"), "add i8 %t0, %t1");
})

// ---- one terminator per block (item 10) --------------------------------------------

TEST(the_blocks_of_a_short_circuit_and_a_check_each_end_in_one_terminator, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    i32 mut a = 7;\n    i32 b = 3;\n"
                          "    bool t = a > 0 && b > 0;\n    i32[2] arr = {a, b};\n"
                          "    point p = {a, b};\n    a /= b;\n    assert(a != 0);\n"
                          "    println(t, \" \", arr[1], \" \", p.x, \" \", color.green);\n"
                          "    return 0;\n}\n"));
    // Every block of every definition ends in exactly one terminator (item
    // 10), which `verified` checks before it runs `opt`, because `opt` exits
    // 0 on a block that holds two.
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

int main(int argc, char** argv) {
    TEST_INIT("gen", argc, argv);
    TEST_RUN(the_module_begins_with_the_normalized_triple);
    TEST_RUN(the_module_carries_no_datalayout_or_module_flags);
    TEST_RUN(the_module_carries_no_comment);
    TEST_RUN(both_named_types_are_emitted_used_or_not);
    TEST_RUN(the_sections_appear_in_the_order_of_item_1);
    TEST_RUN(a_blank_line_stands_between_two_definitions);
    TEST_RUN(the_integer_types_map_to_their_widths);
    TEST_RUN(the_unsigned_types_share_the_signed_ones_ir_types);
    TEST_RUN(a_bool_is_i8_in_memory_and_i1_as_a_value);
    TEST_RUN(a_char_is_an_unsigned_byte);
    TEST_RUN(a_pointer_is_the_opaque_ptr);
    TEST_RUN(a_fixed_array_is_a_bracketed_type);
    TEST_RUN(a_string_and_a_span_share_one_named_type);
    TEST_RUN(an_enum_is_i32);
    TEST_RUN(a_struct_is_a_named_type_with_its_fields_in_order);
    TEST_RUN(a_void_function_has_the_void_result_type);
    TEST_RUN(an_array_element_uses_the_array_gep_shape);
    TEST_RUN(a_struct_field_uses_the_field_gep_shape);
    TEST_RUN(a_span_header_field_uses_the_field_gep_shape);
    TEST_RUN(a_string_element_uses_the_element_gep_shape);
    TEST_RUN(every_getelementptr_carries_inbounds);
    TEST_RUN(an_aggregate_copy_is_a_memcpy);
    TEST_RUN(a_brace_zero_initializer_is_a_memset);
    TEST_RUN(the_emitter_never_writes_insertvalue_on_an_aggregate);
    TEST_RUN(a_fort_function_is_a_quoted_dotted_name);
    TEST_RUN(an_extern_name_is_unmangled_and_not_dso_local);
    TEST_RUN(fort_entry_is_a_c_name_and_unquoted);
    TEST_RUN(a_string_literal_holds_the_trailing_nul_the_length_excludes);
    TEST_RUN(a_non_printable_byte_is_written_as_a_hex_pair);
    TEST_RUN(an_empty_string_literal_is_one_nul_byte);
    TEST_RUN(a_string_constant_is_not_deduplicated_by_content);
    TEST_RUN(a_program_with_no_check_has_no_file_constant);
    TEST_RUN(one_file_constant_serves_every_check_of_that_file);
    TEST_RUN(the_file_constant_holds_the_name_the_compiler_opened);
    TEST_RUN(file_constants_precede_string_constants);
    TEST_RUN(an_enum_table_is_emitted_only_when_a_print_reaches_it);
    TEST_RUN(an_enum_table_has_one_entry_per_member_in_declaration_order);
    TEST_RUN(a_narrow_parameter_and_result_carry_the_extension_attribute);
    TEST_RUN(an_i32_parameter_carries_no_extension_attribute);
    TEST_RUN(an_aggregate_argument_is_a_pointer_to_a_caller_made_copy);
    TEST_RUN(an_aggregate_result_is_a_leading_sret_pointer_on_a_void_function);
    TEST_RUN(every_fort_definition_carries_the_attribute_group_of_item_7);
    TEST_RUN(a_noreturn_definition_carries_its_own_group_and_ends_in_a_trap);
    TEST_RUN(a_narrow_value_keeps_its_own_width);
    TEST_RUN(the_blocks_of_a_short_circuit_and_a_check_each_end_in_one_terminator);
    gen_done();
    TEST_EXIT();
}
