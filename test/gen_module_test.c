// Whole-module tests of the LLVM IR emitter: the two worked examples of
// toolchain.md 6 reproduced from their fort sources, the determinism D19.5
// asks of the text, the locals and blocks of item 10, the print family of
// item 19, fort_entry (item 22), and the LLVM verifier over every module the
// suites emit (D19.1).
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// The directory holding the worked examples; CMake passes its path.
#ifndef FORT_IR_DIR
#define FORT_IR_DIR "test/ir"
#endif

// The program of toolchain.md 6.1, which compiles to test/ir/hello.ll.
static const char HELLO_SOURCE[] = "fn i32 main() { println(\"hello, world!\"); return 0; }\n";

// The program of toolchain.md 6.2, which compiles to test/ir/abort.ll. That
// section fixes the `[` of `a[i]` at line 12, column 13, so the seven comment
// lines put the statement on line 12 and its four spaces of indentation put
// the `[` on column 13; nothing else about the source is free, since every
// instruction of the golden module comes from it.
static const char ABORT_SOURCE[] = "// The program of toolchain.md 6.2, whose bounds check fails\n"
                                   "// at run time. The lines above the body put the indexing on\n"
                                   "// line 12 and its indentation puts the `[` on column 13,\n"
                                   "// which is the position that section fixes and the module\n"
                                   "// records in its call to fort_rt_fail_bounds (D11.4).\n"
                                   "//\n"
                                   "//\n"
                                   "fn i32 main() {\n"
                                   "    println(\"before\");\n"
                                   "    i32[3] a = {};\n"
                                   "    i64 mut i = 5;\n"
                                   "    return a[i];\n"
                                   "}\n";

static sb_t golden_text;

// The golden module of `name` under test/ir, which toolchain.md 6 quotes byte
// for byte as its worked example (D19.1).
static const char* golden(const char* name) {
    char path[GEN_PATH_CAP];
    TEST_UNUSED(snprintf(path, sizeof path, "%s/%s", FORT_IR_DIR, name));
    const char* text = gen_read(path, &golden_text);
    if (text == NULL) {
        return "the golden module could not be read";
    }
    return text;
}

// ---- the worked examples (toolchain.md 6.1, 6.2) -----------------------------------

TEST(the_program_of_section_6_1_compiles_to_hello_ll, {
    TEST_ASSERT_TRUE(emit(HELLO_SOURCE));
    TEST_ASSERT_EQ_STR(ir(), golden("hello.ll"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_program_of_section_6_2_compiles_to_abort_ll, {
    TEST_ASSERT_TRUE(emit_as("abort.ft", ABORT_SOURCE));
    // The whole module, the file constant and the 12:13 of the bounds check
    // included, which is why the source above is not free to change.
    TEST_ASSERT_EQ_STR(ir(), golden("abort.ll"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_checked_fragment_of_item_15_is_emitted_as_the_section_shows, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 a = 7;\n    i32 b = 3;\n"
                          "    return a + b;\n}\n"));
    TEST_ASSERT_EQ_STR(
        ir(),
        "target triple = \"x86_64-unknown-linux-gnu\"\n"
        "\n"
        "%fort.span = type { ptr, i64 }\n"
        "%fort.enum_member = type { i32, ptr }\n"
        "\n"
        "define dso_local i32 @\"main.main\"() #0 {\n"
        "entry:\n"
        "  %a.0 = alloca i32, align 4\n"
        "  %b.1 = alloca i32, align 4\n"
        "  store i32 7, ptr %a.0, align 4\n"
        "  store i32 3, ptr %b.1, align 4\n"
        "  %t0 = load i32, ptr %a.0, align 4\n"
        "  %t1 = load i32, ptr %b.1, align 4\n"
        "  %t2 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %t0, i32 %t1)\n"
        "  %t3 = extractvalue { i32, i1 } %t2, 0\n"
        "  %t4 = extractvalue { i32, i1 } %t2, 1\n"
        "  br i1 %t4, label %L1, label %L0\n"
        "\n"
        "L0:\n"
        "  ret i32 %t3\n"
        "\n"
        "L1:\n"
        "  call void @fort_rt_fail_overflow(ptr @.file.0, i32 4, i32 14)\n"
        "  unreachable\n"
        "}\n"
        "\n"
        "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
        "entry:\n"
        "  %t0 = call i32 @\"main.main\"()\n"
        "  ret i32 %t0\n"
        "}\n"
        "\n"
        "@.file.0 = private unnamed_addr constant [8 x i8] c\"main.ft\\00\", align 1\n"
        "\n"
        "declare void @fort_rt_fail_overflow(ptr, i32, i32) #2\n"
        "\n"
        "declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32) #4\n"
        "\n"
        "attributes #0 = { nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\" }\n"
        "attributes #2 = { cold noreturn nounwind }\n"
        "attributes #4 = { nocallback nofree nosync nounwind speculatable willreturn "
        "memory(none) }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_release_counterpart_of_that_fragment_is_a_plain_add, {
    TEST_ASSERT_TRUE(emit_release("fn i32 main() {\n    i32 a = 7;\n    i32 b = 3;\n"
                                  "    return a + b;\n}\n"));
    TEST_ASSERT_EQ_STR(ir(),
                       "target triple = \"x86_64-unknown-linux-gnu\"\n"
                       "\n"
                       "%fort.span = type { ptr, i64 }\n"
                       "%fort.enum_member = type { i32, ptr }\n"
                       "\n"
                       "define dso_local i32 @\"main.main\"() #0 {\n"
                       "entry:\n"
                       "  %a.0 = alloca i32, align 4\n"
                       "  %b.1 = alloca i32, align 4\n"
                       "  store i32 7, ptr %a.0, align 4\n"
                       "  store i32 3, ptr %b.1, align 4\n"
                       "  %t0 = load i32, ptr %a.0, align 4\n"
                       "  %t1 = load i32, ptr %b.1, align 4\n"
                       "  %t2 = add i32 %t0, %t1\n"
                       "  ret i32 %t2\n"
                       "}\n"
                       "\n"
                       "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
                       "entry:\n"
                       "  %t0 = call i32 @\"main.main\"()\n"
                       "  ret i32 %t0\n"
                       "}\n"
                       "\n"
                       "attributes #0 = { nounwind \"frame-pointer\"=\"all\" "
                       "\"probe-stack\"=\"inline-asm\" }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- determinism (D19.5) -----------------------------------------------------------

// A program that exercises every counter the emitter keeps: locals,
// temporaries, blocks, compiler-made places, string and file constants and an
// enum table.
static const char BUSY_SOURCE[] =
    "enum color { red, green }\n"
    "struct point { i32 x; i32 y; }\n"
    "fn u64 size(string s) { return s.len; }\n"
    "fn point make(i32 v) { point p = {v, v}; return p; }\n"
    "fn i32 main() {\n"
    "    i32 mut a = 7;\n"
    "    i32 b = 3;\n"
    "    a += b;\n"
    "    a /= b;\n"
    "    a <<= b;\n"
    "    bool t = a > 0 && b > 0;\n"
    "    string s = \"busy\";\n"
    "    color g = color.green;\n"
    "    i32[3] arr = {};\n"
    "    i64 i = 1;\n"
    "    point p = make(a);\n"
    "    assert(a != 0);\n"
    "    println(a, \" \", t, \" \", s, \" \", g, \" \", arr[i], \" \", p.x, \" \", size(s));\n"
    "    eprintln(\"done\");\n"
    "    fprint(2, 'x');\n"
    "    return 0;\n"
    "}\n";

TEST(two_runs_on_the_same_input_produce_byte_identical_text, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    static sb_t first;
    sb_clear(&first);
    sb_append(&first, ir());
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    // The emitted text is a function of the program alone, which is what lets
    // stage2 and stage3 reach a fixpoint (D19.5).
    TEST_ASSERT_EQ_STR(ir(), sb_cstr(&first));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_two_build_modes_differ_only_where_the_checks_are, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    static sb_t checked;
    sb_clear(&checked);
    sb_append(&checked, ir());
    TEST_ASSERT_TRUE(emit_release(BUSY_SOURCE));
    TEST_ASSERT_TRUE(strcmp(ir(), sb_cstr(&checked)) != 0);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_counters_are_reset_at_each_definition, {
    TEST_ASSERT_TRUE(emit("fn i32 one() { i32 a = 1; return a; }\n"
                          "fn i32 two() { i32 b = 2; return b; }\n"
                          "fn i32 main() { return one() + two(); }\n"));
    // Every counter is per function and reset at each definition (D19.5).
    TEST_ASSERT_EQ_STR(found("@\"main.one\"() #0 {\nentry:\n  %a.0 = alloca i32, align 4\n"
                             "  store i32 1, ptr %a.0, align 4\n  %t0 = load i32, ptr %a.0"),
                       "@\"main.one\"() #0 {\nentry:\n  %a.0 = alloca i32, align 4\n"
                       "  store i32 1, ptr %a.0, align 4\n  %t0 = load i32, ptr %a.0");
    TEST_ASSERT_EQ_STR(found("@\"main.two\"() #0 {\nentry:\n  %b.0 = alloca i32, align 4\n"
                             "  store i32 2, ptr %b.0, align 4\n  %t0 = load i32, ptr %b.0"),
                       "@\"main.two\"() #0 {\nentry:\n  %b.0 = alloca i32, align 4\n"
                       "  store i32 2, ptr %b.0, align 4\n  %t0 = load i32, ptr %b.0");
})

// ---- locals, parameters and blocks (item 10, D19.4) --------------------------------

TEST(every_local_is_an_entry_block_alloca_in_declaration_order, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 a = 1;\n    { i64 b = 2; }\n"
                          "    i16 c = 3;\n    return a;\n}\n"));
    // A local of a nested block is an entry-block alloca too, because LLVM's
    // promotion passes look only there (D19.4).
    TEST_ASSERT_EQ_STR(found("entry:\n  %a.0 = alloca i32, align 4\n"
                             "  %b.1 = alloca i64, align 8\n  %c.2 = alloca i16, align 2\n"),
                       "entry:\n  %a.0 = alloca i32, align 4\n"
                       "  %b.1 = alloca i64, align 8\n  %c.2 = alloca i16, align 2\n");
})

TEST(a_local_named_tmp_cannot_collide_with_a_compiler_made_place, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 tmp = 1;\n    i32 b = 2;\n"
                          "    bool t = tmp > 0 && b > 0;\n    println(t);\n    return 0;\n}\n"));
    // A name that embeds a fort identifier always contains a dot and an
    // invented one never does (D19.5).
    TEST_ASSERT_EQ_STR(found("%tmp.0 = alloca i32, align 4"), "%tmp.0 = alloca i32, align 4");
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca i8, align 1"), "%tmp0 = alloca i8, align 1");
})

TEST(a_shadowing_local_gets_a_slot_of_its_own, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 a = 1;\n    { i64 b = 2; }\n"
                          "    { i32 c = 3; }\n    return a;\n}\n"));
    TEST_ASSERT_EQ_STR(found("%b.1 = alloca i64"), "%b.1 = alloca i64");
    TEST_ASSERT_EQ_STR(found("%c.2 = alloca i32"), "%c.2 = alloca i32");
})

TEST(a_scalar_parameter_is_stored_into_its_slot_immediately, {
    TEST_ASSERT_TRUE(emit("fn i32 twice(i64 v) { return cast(v *% 2, i32); }\n"
                          "fn i32 main() { return twice(3); }\n"));
    TEST_ASSERT_EQ_STR(found("entry:\n  %v.0 = alloca i64, align 8\n"
                             "  store i64 %v.in, ptr %v.0, align 8\n"),
                       "entry:\n  %v.0 = alloca i64, align 8\n"
                       "  store i64 %v.in, ptr %v.0, align 8\n");
})

TEST(an_aggregate_parameter_is_not_copied_again, {
    TEST_ASSERT_TRUE(emit("fn u64 size(string s) { return s.len; }\n"
                          "fn i32 main() { string s = \"hi\"; println(size(s)); return 0; }\n"));
    // Its place is the caller-made copy the incoming pointer designates
    // (item 10).
    TEST_ASSERT_EQ_STR(
        found("@\"main.size\"(ptr %s.in) #0 {\nentry:\n"
              "  %t0 = getelementptr inbounds %fort.span, ptr %s.in, i32 0, i32 1\n"),
        "@\"main.size\"(ptr %s.in) #0 {\nentry:\n"
        "  %t0 = getelementptr inbounds %fort.span, ptr %s.in, i32 0, i32 1\n");
})

TEST(a_fresh_block_opens_after_a_terminating_statement, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    { return 1; println(\"after\"); }\n"
                          "    return 0;\n}\n"));
    // D14.2 allows the statements after a terminating one, so the emitter
    // opens a block for them (item 10).
    TEST_ASSERT_EQ_STR(found("  ret i32 1\n\nL0:\n  call void @fort_rt_print_str"),
                       "  ret i32 1\n\nL0:\n  call void @fort_rt_print_str");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_void_body_that_falls_off_the_end_returns_void, {
    TEST_ASSERT_TRUE(emit("fn void noop() { }\nfn i32 main() { noop(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.noop\"() #0 {\nentry:\n  ret void\n}"),
                       "@\"main.noop\"() #0 {\nentry:\n  ret void\n}");
})

// ---- fort_entry (item 22, D11.6) ---------------------------------------------------

TEST(fort_entry_calls_main_directly_when_it_takes_no_parameter, {
    TEST_ASSERT_TRUE(emit(HELLO_SOURCE));
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
                             "entry:\n"
                             "  %t0 = call i32 @\"main.main\"()\n"
                             "  ret i32 %t0\n"
                             "}\n"),
                       "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
                       "entry:\n"
                       "  %t0 = call i32 @\"main.main\"()\n"
                       "  ret i32 %t0\n"
                       "}\n");
})

TEST(fort_entry_copies_the_argument_span_when_main_declares_it, {
    TEST_ASSERT_TRUE(emit("fn i32 main(string@ args) { return cast(args.len, i32); }\n"));
    // Its caller is the C runtime rather than fort code, so the span is
    // copied into its own frame (item 22).
    TEST_ASSERT_EQ_STR(
        found("define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
              "entry:\n"
              "  %args.0 = alloca %fort.span, align 8\n"
              "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %args.0, ptr align 8 %args.in, "
              "i64 16, i1 false)\n"
              "  %t0 = call i32 @\"main.main\"(ptr %args.0)\n"
              "  ret i32 %t0\n"
              "}\n"),
        "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
        "entry:\n"
        "  %args.0 = alloca %fort.span, align 8\n"
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %args.0, ptr align 8 %args.in, "
        "i64 16, i1 false)\n"
        "  %t0 = call i32 @\"main.main\"(ptr %args.0)\n"
        "  ret i32 %t0\n"
        "}\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(fort_entry_is_the_last_definition_of_the_module, {
    TEST_ASSERT_TRUE(emit("fn i32 one() { return 1; }\nfn i32 main() { return one(); }\n"));
    TEST_ASSERT_TRUE(before("@\"main.one\"", "@\"main.main\""));
    TEST_ASSERT_TRUE(before("@\"main.main\"() #0 {", "@fort_entry"));
})

// ---- the print family (item 19, D12.2) ---------------------------------------------

TEST(each_integer_type_reaches_its_own_printer, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i8 a = 1;\n    u8 b = 2;\n    i64 c = 3;\n"
                          "    u64 d = 4;\n    println(a, b, c, d);\n    return 0;\n}\n"));
    // i8 i16 i32 i64 are sign-extended to i64 and u8 u16 u32 u64
    // zero-extended to it (item 19).
    TEST_ASSERT_EQ_STR(found("sext i8 %t0 to i64"), "sext i8 %t0 to i64");
    TEST_ASSERT_EQ_STR(found("@fort_rt_print_i64(i32 1, i64 %t1)"),
                       "@fort_rt_print_i64(i32 1, i64 %t1)");
    TEST_ASSERT_EQ_STR(found("zext i8 %t2 to i64"), "zext i8 %t2 to i64");
    TEST_ASSERT_EQ_STR(found("@fort_rt_print_u64(i32 1, i64 %t3)"),
                       "@fort_rt_print_u64(i32 1, i64 %t3)");
    TEST_ASSERT_EQ_STR(found("@fort_rt_print_i64(i32 1, i64 %t4)"),
                       "@fort_rt_print_i64(i32 1, i64 %t4)");
    TEST_ASSERT_EQ_STR(found("@fort_rt_print_u64(i32 1, i64 %t5)"),
                       "@fort_rt_print_u64(i32 1, i64 %t5)");
})

TEST(a_bool_a_char_and_a_pointer_reach_their_printers, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    bool t = true;\n    char c = 'a';\n"
                          "    void* p = null;\n    println(t, c, p);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@fort_rt_print_bool(i32 1, i8 zeroext %t"),
                       "@fort_rt_print_bool(i32 1, i8 zeroext %t");
    TEST_ASSERT_EQ_STR(found("@fort_rt_print_char(i32 1, i8 zeroext %t"),
                       "@fort_rt_print_char(i32 1, i8 zeroext %t");
    TEST_ASSERT_EQ_STR(found("@fort_rt_print_ptr(i32 1, ptr %t"),
                       "@fort_rt_print_ptr(i32 1, ptr %t");
})

TEST(a_string_literal_prints_as_its_constant_and_length, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { print(\"hi\"); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @fort_rt_print_str(i32 1, ptr @.str.0, i64 2)"),
                       "call void @fort_rt_print_str(i32 1, ptr @.str.0, i64 2)");
    // print writes no newline (D12.2).
    TEST_ASSERT_EQ_STR(absent("i8 zeroext 10"), "absent");
})

TEST(println_ends_with_the_newline_byte, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { println(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @fort_rt_print_char(i32 1, i8 zeroext 10)"),
                       "call void @fort_rt_print_char(i32 1, i8 zeroext 10)");
})

TEST(eprint_writes_to_the_second_descriptor, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { eprintln(\"warn\"); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @fort_rt_print_str(i32 2, ptr @.str.0, i64 4)"),
                       "call void @fort_rt_print_str(i32 2, ptr @.str.0, i64 4)");
    TEST_ASSERT_EQ_STR(found("call void @fort_rt_print_char(i32 2, i8 zeroext 10)"),
                       "call void @fort_rt_print_char(i32 2, i8 zeroext 10)");
})

TEST(fprint_evaluates_its_descriptor_once, {
    TEST_ASSERT_TRUE(emit("fn i32 fd() { return 1; }\n"
                          "fn i32 main() { fprintln(fd(), \"a\", \"b\"); return 0; }\n"));
    // `fd` is evaluated once and then each argument left to right (D6.3,
    // D12.2).
    TEST_ASSERT_EQ_STR(found("  %t0 = call i32 @\"main.fd\"()\n"
                             "  call void @fort_rt_print_str(i32 %t0, ptr @.str.0, i64 1)\n"
                             "  call void @fort_rt_print_str(i32 %t0, ptr @.str.1, i64 1)\n"
                             "  call void @fort_rt_print_char(i32 %t0, i8 zeroext 10)\n"),
                       "  %t0 = call i32 @\"main.fd\"()\n"
                       "  call void @fort_rt_print_str(i32 %t0, ptr @.str.0, i64 1)\n"
                       "  call void @fort_rt_print_str(i32 %t0, ptr @.str.1, i64 1)\n"
                       "  call void @fort_rt_print_char(i32 %t0, i8 zeroext 10)\n");
})

TEST(a_string_variable_prints_as_its_two_header_fields, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { string s = \"hi\"; print(s); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  %t3 = load ptr, ptr %t2, align 8\n"
                             "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                             "  %t5 = load i64, ptr %t4, align 8\n"
                             "  call void @fort_rt_print_str(i32 1, ptr %t3, i64 %t5)\n"),
                       "  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  %t3 = load ptr, ptr %t2, align 8\n"
                       "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                       "  %t5 = load i64, ptr %t4, align 8\n"
                       "  call void @fort_rt_print_str(i32 1, ptr %t3, i64 %t5)\n");
})

TEST(an_enum_prints_with_its_table_and_member_count, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() { color g = color.green; print(g); return 0; }\n"));
    TEST_ASSERT_EQ_STR(
        found("call void @fort_rt_print_enum(i32 1, i32 %t0, ptr @.enum.main.color, i64 2)"),
        "call void @fort_rt_print_enum(i32 1, i32 %t0, ptr @.enum.main.color, i64 2)");
})

// ---- the verifier over a corpus (D19.1) --------------------------------------------

// One program per area the emitter covers; every module it produces must pass
// opt -passes=verify, which is the one property the whole contract rests on.
static const char* const CORPUS[] = {
    "fn i32 main() { return 0; }\n",
    "fn i32 main() { println(); return 0; }\n",
    "fn i32 main() {\n    i8 a = -128;\n    u64 b = 18446744073709551615;\n"
    "    println(a, b, -a +% 1, b +% 1);\n    return 0;\n}\n",
    "fn i32 main() {\n    i32 a = 7;\n    i32 b = 3;\n"
    "    println(a + b, a - b, a * b, a / b, a % b, a << b, a >> b, a & b, a | b, a ^ b, ~a);\n"
    "    return 0;\n}\n",
    "fn i32 main() {\n    u8 a = 7;\n    u8 b = 3;\n"
    "    println(a + b, a - b, a * b, a / b, a % b, a << b, a >> b);\n    return 0;\n}\n",
    "fn i32 main() {\n    i32 a = 1;\n    i32 b = 2;\n"
    "    println(a > b && b > 0 || a == 1, !(a < b));\n    return 0;\n}\n",
    "struct point { i32 x; i32 y; }\n"
    "struct line { point a; point b; }\n"
    "fn i32 main() {\n    line l = {};\n    line m = l;\n"
    "    println(m.a.x, m.b.y);\n    return 0;\n}\n",
    "struct point { i32 x; i32 y; }\n"
    "fn point make() { point p = {.y = 2}; return p; }\n"
    "fn i32 main() { point q = make(); println(q.x, q.y); return 0; }\n",
    "fn i32 main() {\n    i32[4] a = {1, 2, 3, 4};\n    i64 i = 3;\n"
    "    println(a[i], a.len);\n    return 0;\n}\n",
    "fn i32 main() {\n    string s = \"hello\";\n    u64 i = 1;\n"
    "    println(s, s.len, s[i]);\n    return 0;\n}\n",
    "enum color { red, green = 5, blue }\n"
    "fn i32 main() {\n    color g = color.blue;\n"
    "    println(g, cast(g, i32), cast(9, color));\n    return 0;\n}\n",
    "fn i32 main() {\n    i32 v = 1;\n    i32* p = &v;\n    i32** q = &p;\n"
    "    println(*p, **q, p == null);\n    return 0;\n}\n",
    "extern fn i32 puts(char* s);\n"
    "fn i32 main() { string s = \"x\"; return puts(s.ptr); }\n",
    "fn noreturn stop() { panic(\"stop\"); }\n"
    "fn i32 main() { println(\"before\"); stop(); }\n",
    "fn i32 main() { assert(1 == 1); assert(sizeof(i64) == 8); return 0; }\n",
    "fn i32 main(string@ args) { println(args.len); return 0; }\n",
};

TEST(every_module_of_the_corpus_verifies_in_the_checked_mode, {
    for (uint64_t i = 0; i < sizeof CORPUS / sizeof CORPUS[0]; i++) {
        TEST_ASSERT_TRUE(emit(CORPUS[i]));
        TEST_ASSERT_EQ_STR(verified(), "verified");
    }
})

TEST(every_module_of_the_corpus_verifies_in_release_mode, {
    for (uint64_t i = 0; i < sizeof CORPUS / sizeof CORPUS[0]; i++) {
        TEST_ASSERT_TRUE(emit_release(CORPUS[i]));
        TEST_ASSERT_EQ_STR(verified(), "verified");
    }
})

TEST(every_module_of_the_corpus_verifies_without_bounds_checks, {
    for (uint64_t i = 0; i < sizeof CORPUS / sizeof CORPUS[0]; i++) {
        TEST_ASSERT_TRUE(emit_unchecked(CORPUS[i]));
        TEST_ASSERT_EQ_STR(verified(), "verified");
    }
})

TEST(every_module_of_the_corpus_is_reproduced_byte_for_byte, {
    static sb_t first;
    for (uint64_t i = 0; i < sizeof CORPUS / sizeof CORPUS[0]; i++) {
        TEST_ASSERT_TRUE(emit(CORPUS[i]));
        sb_clear(&first);
        sb_append(&first, ir());
        TEST_ASSERT_TRUE(emit(CORPUS[i]));
        TEST_ASSERT_EQ_STR(ir(), sb_cstr(&first));
    }
    sb_free(&first);
})

// ---- several modules in one program (item 1, D9.10) --------------------------------

// The imported module: a struct, a function and an `extern` the importer
// declares as well.
static const char UTIL_SOURCE[] = "extern fn i32 puts(char* s);\n"
                                  "struct point { i32 x; i32 y; }\n"
                                  "fn i32 shout(char* s) { return puts(s); }\n";

static const char APP_SOURCE[] = "import util;\n"
                                 "extern fn i32 puts(char* s);\n"
                                 "fn i32 main() {\n"
                                 "    util.point p = {1, 2};\n"
                                 "    string s = \"hi\";\n"
                                 "    i32 a = puts(s.ptr);\n"
                                 "    i32 b = util.shout(s.ptr);\n"
                                 "    println(p.x +% p.y +% a +% b);\n"
                                 "    return 0;\n"
                                 "}\n";

TEST(the_modules_of_a_closure_are_emitted_in_dependency_order, {
    TEST_ASSERT_TRUE(emit_two("app.ft", APP_SOURCE, "util.ft", UTIL_SOURCE));
    // An imported module stands before its importer (D9.10, D19.5), and each
    // symbol is its own module path plus its name (D9.7).
    TEST_ASSERT_TRUE(
        before("define dso_local i32 @\"util.shout\"", "define dso_local i32 @\"app.main\""));
    TEST_ASSERT_TRUE(before("define dso_local i32 @\"app.main\"", "@fort_entry"));
    TEST_ASSERT_EQ_STR(found("%struct.util.point = type { i32, i32 }"),
                       "%struct.util.point = type { i32, i32 }");
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %struct.util.point, ptr %p.0, i32 0, i32 0"),
                       "getelementptr inbounds %struct.util.point, ptr %p.0, i32 0, i32 0");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_c_function_two_modules_declare_is_declared_once, {
    TEST_ASSERT_TRUE(emit_two("app.ft", APP_SOURCE, "util.ft", UTIL_SOURCE));
    // A symbol is declared exactly once (item 8): the two `extern fn puts`
    // are two symbols and one ELF symbol, and a second declaration is a
    // redefinition the verifier rejects.
    TEST_ASSERT_EQ_STR(found("declare i32 @puts(ptr, ...)\n"), "declare i32 @puts(ptr, ...)\n");
    TEST_ASSERT_NULL(
        strstr(strstr(ir(), "declare i32 @puts(ptr, ...)") + 1, "declare i32 @puts(ptr, ...)"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_declarations_of_one_c_name_never_reach_the_emitter_disagreeing, {
    // The dedup above keeps the first symbol of a C name, which is only safe
    // because the loader refuses two declarations of one extern that disagree
    // (module-system.md 13); the emitter never chooses between them.
    TEST_ASSERT_FALSE(emit_two("app.ft",
                               "import util;\n"
                               "extern fn i64 puts(i32 n);\n"
                               "fn i32 main() { return cast(puts(7), i32); }\n",
                               "util.ft",
                               "extern fn i32 puts(char* s);\n"
                               "fn i32 shout(char* s) { return puts(s); }\n"));
    TEST_ASSERT_NONNULL(strstr(gen_said(), "conflicting declarations of extern 'puts'"));
})

// ---- the runtime group reached through an extern fn (item 8, D13.1) ----------------

TEST(an_extern_naming_a_runtime_entry_point_takes_that_groups_prototype, {
    TEST_ASSERT_TRUE(emit("extern fn noreturn fort_rt_exit(i32 status);\n"
                          "fn i32 main() { println(\"bye\"); fort_rt_exit(3); }\n"));
    // It is emitted in the runtime group with that group's prototype and
    // attributes and left out of the extern group, variadic tail included,
    // so the call site goes through that prototype too (item 8).
    TEST_ASSERT_EQ_STR(found("declare void @fort_rt_exit(i32) #2\n"),
                       "declare void @fort_rt_exit(i32) #2\n");
    TEST_ASSERT_EQ_STR(absent("@fort_rt_exit(i32, ...)"), "absent");
    TEST_ASSERT_EQ_STR(found("  call void @fort_rt_exit(i32 3)\n"),
                       "  call void @fort_rt_exit(i32 3)\n");
    TEST_ASSERT_EQ_STR(absent("@fort_rt_exit(i32 3) #3"), "absent");
    // `_Noreturn` is the runtime's own guarantee, so the declaration carries
    // it (item 20), and the call site still ends in the trap of D8.5.
    TEST_ASSERT_EQ_STR(found("attributes #2 = { cold noreturn nounwind }"),
                       "attributes #2 = { cold noreturn nounwind }");
    TEST_ASSERT_EQ_STR(found("  call void @llvm.trap()\n  unreachable\n"),
                       "  call void @llvm.trap()\n  unreachable\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_argument_entry_points_are_declared_from_section_5_1, {
    TEST_ASSERT_TRUE(
        emit("extern fn u64 fort_rt_args_len();\n"
             "extern fn void fort_rt_flush(i32 fd);\n"
             "fn i32 main() { fort_rt_flush(1); return cast(fort_rt_args_len(), i32); }\n"));
    TEST_ASSERT_EQ_STR(found("declare void @fort_rt_flush(i32)\n"),
                       "declare void @fort_rt_flush(i32)\n");
    TEST_ASSERT_EQ_STR(found("declare i64 @fort_rt_args_len()\n"),
                       "declare i64 @fort_rt_args_len()\n");
    // The runtime group keeps the order of toolchain.md 5.1, where flush
    // comes before the argument entry points.
    TEST_ASSERT_TRUE(before("declare void @fort_rt_flush(i32)", "declare i64 @fort_rt_args_len()"));
    TEST_ASSERT_EQ_STR(found("%t0 = call i64 @fort_rt_args_len()"),
                       "%t0 = call i64 @fort_rt_args_len()");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- unfinished constructs are refused, never miscompiled --------------------------

TEST(a_construct_the_emitter_cannot_lower_yet_is_a_diagnostic, {
    // The tickets after T-015 remove these one by one; until then a program
    // that uses one is refused rather than emitted wrongly.
    TEST_ASSERT_FALSE(emit("fn i32 main() {\n    if (true) { return 1; }\n    return 0;\n}\n"));
    TEST_ASSERT_NONNULL(strstr(gen_said(), "cannot generate code yet for control flow"));
})

int main(int argc, char** argv) {
    TEST_INIT("gen_module", argc, argv);
    TEST_RUN(the_program_of_section_6_1_compiles_to_hello_ll);
    TEST_RUN(the_program_of_section_6_2_compiles_to_abort_ll);
    TEST_RUN(the_checked_fragment_of_item_15_is_emitted_as_the_section_shows);
    TEST_RUN(the_release_counterpart_of_that_fragment_is_a_plain_add);
    TEST_RUN(two_runs_on_the_same_input_produce_byte_identical_text);
    TEST_RUN(the_two_build_modes_differ_only_where_the_checks_are);
    TEST_RUN(the_counters_are_reset_at_each_definition);
    TEST_RUN(every_local_is_an_entry_block_alloca_in_declaration_order);
    TEST_RUN(a_local_named_tmp_cannot_collide_with_a_compiler_made_place);
    TEST_RUN(a_shadowing_local_gets_a_slot_of_its_own);
    TEST_RUN(a_scalar_parameter_is_stored_into_its_slot_immediately);
    TEST_RUN(an_aggregate_parameter_is_not_copied_again);
    TEST_RUN(a_fresh_block_opens_after_a_terminating_statement);
    TEST_RUN(a_void_body_that_falls_off_the_end_returns_void);
    TEST_RUN(fort_entry_calls_main_directly_when_it_takes_no_parameter);
    TEST_RUN(fort_entry_copies_the_argument_span_when_main_declares_it);
    TEST_RUN(fort_entry_is_the_last_definition_of_the_module);
    TEST_RUN(each_integer_type_reaches_its_own_printer);
    TEST_RUN(a_bool_a_char_and_a_pointer_reach_their_printers);
    TEST_RUN(a_string_literal_prints_as_its_constant_and_length);
    TEST_RUN(println_ends_with_the_newline_byte);
    TEST_RUN(eprint_writes_to_the_second_descriptor);
    TEST_RUN(fprint_evaluates_its_descriptor_once);
    TEST_RUN(a_string_variable_prints_as_its_two_header_fields);
    TEST_RUN(an_enum_prints_with_its_table_and_member_count);
    TEST_RUN(every_module_of_the_corpus_verifies_in_the_checked_mode);
    TEST_RUN(every_module_of_the_corpus_verifies_in_release_mode);
    TEST_RUN(every_module_of_the_corpus_verifies_without_bounds_checks);
    TEST_RUN(every_module_of_the_corpus_is_reproduced_byte_for_byte);
    TEST_RUN(the_modules_of_a_closure_are_emitted_in_dependency_order);
    TEST_RUN(a_c_function_two_modules_declare_is_declared_once);
    TEST_RUN(two_declarations_of_one_c_name_never_reach_the_emitter_disagreeing);
    TEST_RUN(an_extern_naming_a_runtime_entry_point_takes_that_groups_prototype);
    TEST_RUN(the_argument_entry_points_are_declared_from_section_5_1);
    TEST_RUN(a_construct_the_emitter_cannot_lower_yet_is_a_diagnostic);
    sb_free(&golden_text);
    gen_done();
    TEST_EXIT();
}
