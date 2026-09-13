// Whole-module tests of the LLVM IR emitter: the two worked examples of
// toolchain.md 6 reproduced from their fort sources, the determinism the
// emitter owes the text, the locals and blocks of item 10, the print family
// of item 19, fort_entry (item 22), and the LLVM verifier over every module
// the suites emit.
// D19.5, D19.1
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// The program of toolchain.md 6.1, whose module that section shows.
static const char HELLO_SOURCE[] = "fn i32 main() { println(\"hello, world!\"); return 0; }\n";

// The program of toolchain.md 6.2, whose module that section shows. It fixes
// the `[` of `a[i]` at line 12, column 13, so the seven comment lines put the
// statement on line 12 and its four spaces of indentation put the `[` on
// column 13; nothing else about the source is free, since every instruction
// of the expected module comes from it.
static const char ABORT_SOURCE[] = "// The program of toolchain.md 6.2, whose bounds check fails\n"
                                   "// at run time. The lines above the body put the indexing on\n"
                                   "// line 12 and its indentation puts the `[` on column 13,\n"
                                   "// which is the position that section fixes and the module\n"
                                   "// records in its call to std.rt.fail_bounds (D11.4).\n"
                                   "//\n"
                                   "//\n"
                                   "fn i32 main() {\n"
                                   "    println(\"before\");\n"
                                   "    i32[3] a = {};\n"
                                   "    i64 mut i = 5;\n"
                                   "    return a[i];\n"
                                   "}\n";

// ---- the worked examples (toolchain.md 6.1, 6.2) -----------------------------------

TEST(the_program_of_section_6_1_is_emitted_as_that_section_shows, {
    TEST_ASSERT_TRUE(emit(HELLO_SOURCE));
    TEST_ASSERT_EQ_STR(
        ir(),
        "target triple = \"x86_64-unknown-linux-gnu\"\n"
        "\n"
        "%fort.span = type { ptr, i64 }\n"
        "%fort.enum_member = type { i32, ptr }\n"
        "\n"
        "define dso_local i32 @\"main.main\"() #0 {\n"
        "entry:\n"
        "  call void @\"std.rt.print_str\"(i32 1, ptr @.str.0, i64 13)\n"
        "  call void @\"std.rt.print_char\"(i32 1, i8 zeroext 10)\n"
        "  ret i32 0\n"
        "}\n"
        "\n"
        "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
        "entry:\n"
        "  %t0 = call i32 @\"main.main\"()\n"
        "  ret i32 %t0\n"
        "}\n"
        "\n"
        "define dso_local i32 @main(i32 %argc, ptr %argv) #0 {\n"
        "entry:\n"
        "  %args = alloca %fort.span, align 8\n"
        "  call void @\"std.rt.args_init\"(i32 %argc, ptr %argv)\n"
        "  call void @\"std.rt.args\"(ptr %args)\n"
        "  %t0 = call i32 @fort_entry(ptr %args)\n"
        "  call void @\"std.rt.flush_all\"()\n"
        "  %t1 = and i32 %t0, 255\n"
        "  ret i32 %t1\n"
        "}\n"
        "\n"
        "@.str.0 = private unnamed_addr constant [14 x i8] c\"hello, world!\\00\", align 1\n"
        "\n"
        "attributes #0 = { nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\" }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_program_of_section_6_2_is_emitted_as_that_section_shows, {
    TEST_ASSERT_TRUE(emit_as("abort.ft", ABORT_SOURCE));
    // The whole module, the file constant and the 12:13 of the bounds check
    // included, which is why the source above is not free to change.
    TEST_ASSERT_EQ_STR(
        ir(),
        "target triple = \"x86_64-unknown-linux-gnu\"\n"
        "\n"
        "%fort.span = type { ptr, i64 }\n"
        "%fort.enum_member = type { i32, ptr }\n"
        "\n"
        "define dso_local i32 @\"abort.main\"() #0 {\n"
        "entry:\n"
        "  %a.0 = alloca [3 x i32], align 4\n"
        "  %i.1 = alloca i64, align 8\n"
        "  call void @\"std.rt.print_str\"(i32 1, ptr @.str.0, i64 6)\n"
        "  call void @\"std.rt.print_char\"(i32 1, i8 zeroext 10)\n"
        "  call void @llvm.memset.p0.i64(ptr align 4 %a.0, i8 0, i64 12, i1 false)\n"
        "  store i64 5, ptr %i.1, align 8\n"
        "  %t0 = load i64, ptr %i.1, align 8\n"
        "  %t1 = icmp uge i64 %t0, 3\n"
        "  br i1 %t1, label %L1, label %L0\n"
        "\n"
        "L0:\n"
        "  %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0\n"
        "  %t3 = load i32, ptr %t2, align 4\n"
        "  ret i32 %t3\n"
        "\n"
        "L1:\n"
        "  call void @\"std.rt.fail_bounds\"(i64 %t0, i64 3, ptr @.file.0, i32 12, i32 13)\n"
        "  unreachable\n"
        "}\n"
        "\n"
        "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
        "entry:\n"
        "  %t0 = call i32 @\"abort.main\"()\n"
        "  ret i32 %t0\n"
        "}\n"
        "\n"
        "define dso_local i32 @main(i32 %argc, ptr %argv) #0 {\n"
        "entry:\n"
        "  %args = alloca %fort.span, align 8\n"
        "  call void @\"std.rt.args_init\"(i32 %argc, ptr %argv)\n"
        "  call void @\"std.rt.args\"(ptr %args)\n"
        "  %t0 = call i32 @fort_entry(ptr %args)\n"
        "  call void @\"std.rt.flush_all\"()\n"
        "  %t1 = and i32 %t0, 255\n"
        "  ret i32 %t1\n"
        "}\n"
        "\n"
        "@.file.0 = private unnamed_addr constant [9 x i8] c\"abort.ft\\00\", align 1\n"
        "@.str.0 = private unnamed_addr constant [7 x i8] c\"before\\00\", align 1\n"
        "\n"
        "declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #6\n"
        "\n"
        "attributes #0 = { nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\" }\n"
        "attributes #6 = { nocallback nofree nounwind willreturn memory(argmem: write) }\n");
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
        "  call void @\"std.rt.fail_overflow\"(ptr @.file.0, i32 4, i32 14)\n"
        "  unreachable\n"
        "}\n"
        "\n"
        "define dso_local i32 @fort_entry(ptr %args.in) #0 {\n"
        "entry:\n"
        "  %t0 = call i32 @\"main.main\"()\n"
        "  ret i32 %t0\n"
        "}\n"
        "\n"
        "define dso_local i32 @main(i32 %argc, ptr %argv) #0 {\n"
        "entry:\n"
        "  %args = alloca %fort.span, align 8\n"
        "  call void @\"std.rt.args_init\"(i32 %argc, ptr %argv)\n"
        "  call void @\"std.rt.args\"(ptr %args)\n"
        "  %t0 = call i32 @fort_entry(ptr %args)\n"
        "  call void @\"std.rt.flush_all\"()\n"
        "  %t1 = and i32 %t0, 255\n"
        "  ret i32 %t1\n"
        "}\n"
        "\n"
        "@.file.0 = private unnamed_addr constant [8 x i8] c\"main.ft\\00\", align 1\n"
        "\n"
        "declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32) #4\n"
        "\n"
        "attributes #0 = { nounwind \"frame-pointer\"=\"all\" \"probe-stack\"=\"inline-asm\" }\n"
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
                       "define dso_local i32 @main(i32 %argc, ptr %argv) #0 {\n"
                       "entry:\n"
                       "  %args = alloca %fort.span, align 8\n"
                       "  call void @\"std.rt.args_init\"(i32 %argc, ptr %argv)\n"
                       "  call void @\"std.rt.args\"(ptr %args)\n"
                       "  %t0 = call i32 @fort_entry(ptr %args)\n"
                       "  call void @\"std.rt.flush_all\"()\n"
                       "  %t1 = and i32 %t0, 255\n"
                       "  ret i32 %t1\n"
                       "}\n"
                       "\n"
                       "attributes #0 = { nounwind \"frame-pointer\"=\"all\" "
                       "\"probe-stack\"=\"inline-asm\" }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- determinism -------------------------------------------------------------------
// D19.5

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
    // stage2 and stage3 reach a fixpoint.
    // D19.5
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
    // Every counter is per function and reset at each definition.
    // D19.5
    TEST_ASSERT_EQ_STR(found("@\"main.one\"() #0 {\nentry:\n  %a.0 = alloca i32, align 4\n"
                             "  store i32 1, ptr %a.0, align 4\n  %t0 = load i32, ptr %a.0"),
                       "@\"main.one\"() #0 {\nentry:\n  %a.0 = alloca i32, align 4\n"
                       "  store i32 1, ptr %a.0, align 4\n  %t0 = load i32, ptr %a.0");
    TEST_ASSERT_EQ_STR(found("@\"main.two\"() #0 {\nentry:\n  %b.0 = alloca i32, align 4\n"
                             "  store i32 2, ptr %b.0, align 4\n  %t0 = load i32, ptr %b.0"),
                       "@\"main.two\"() #0 {\nentry:\n  %b.0 = alloca i32, align 4\n"
                       "  store i32 2, ptr %b.0, align 4\n  %t0 = load i32, ptr %b.0");
})

// ---- locals, parameters and blocks (item 10) ---------------------------------------
// D19.4

TEST(every_local_is_an_entry_block_alloca_in_declaration_order, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 a = 1;\n    { i64 b = 2; }\n"
                          "    i16 c = 3;\n    return a;\n}\n"));
    // A local of a nested block is an entry-block alloca too, because LLVM's
    // promotion passes look only there.
    // D19.4
    TEST_ASSERT_EQ_STR(found("entry:\n  %a.0 = alloca i32, align 4\n"
                             "  %b.1 = alloca i64, align 8\n  %c.2 = alloca i16, align 2\n"),
                       "entry:\n  %a.0 = alloca i32, align 4\n"
                       "  %b.1 = alloca i64, align 8\n  %c.2 = alloca i16, align 2\n");
})

TEST(a_local_named_tmp_cannot_collide_with_a_compiler_made_place, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i32 tmp = 1;\n    i32 b = 2;\n"
                          "    bool t = tmp > 0 && b > 0;\n    println(t);\n    return 0;\n}\n"));
    // A name that embeds a fort identifier always contains a dot and an
    // invented one never does.
    // D19.5
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
    // The parser allows the statements after a terminating one, so the
    // emitter opens a block for them (item 10).
    // D14.2
    TEST_ASSERT_EQ_STR(found("  ret i32 1\n\nL0:\n  call void @\"std.rt.print_str\""),
                       "  ret i32 1\n\nL0:\n  call void @\"std.rt.print_str\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_void_body_that_falls_off_the_end_returns_void, {
    TEST_ASSERT_TRUE(emit("fn void noop() { }\nfn i32 main() { noop(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("@\"main.noop\"() #0 {\nentry:\n  ret void\n}"),
                       "@\"main.noop\"() #0 {\nentry:\n  ret void\n}");
})

// ---- fort_entry (item 22) ----------------------------------------------------------
// D11.6

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
    // Its caller is the `main` of item 22 rather than the body of a fort
    // function, so the span is copied into its own frame.
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

// ---- the print family (item 19) ----------------------------------------------------
// D12.2

TEST(each_integer_type_reaches_its_own_printer, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    i8 a = 1;\n    u8 b = 2;\n    i64 c = 3;\n"
                          "    u64 d = 4;\n    println(a, b, c, d);\n    return 0;\n}\n"));
    // i8 i16 i32 i64 are sign-extended to i64 and u8 u16 u32 u64
    // zero-extended to it (item 19).
    TEST_ASSERT_EQ_STR(found("sext i8 %t0 to i64"), "sext i8 %t0 to i64");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_i64\"(i32 1, i64 %t1)"),
                       "@\"std.rt.print_i64\"(i32 1, i64 %t1)");
    TEST_ASSERT_EQ_STR(found("zext i8 %t2 to i64"), "zext i8 %t2 to i64");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_u64\"(i32 1, i64 %t3)"),
                       "@\"std.rt.print_u64\"(i32 1, i64 %t3)");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_i64\"(i32 1, i64 %t4)"),
                       "@\"std.rt.print_i64\"(i32 1, i64 %t4)");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_u64\"(i32 1, i64 %t5)"),
                       "@\"std.rt.print_u64\"(i32 1, i64 %t5)");
})

TEST(a_bool_a_char_and_a_pointer_reach_their_printers, {
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n    bool t = true;\n    char c = 'a';\n"
                          "    void* p = null;\n    println(t, c, p);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_bool\"(i32 1, i1 zeroext %t"),
                       "@\"std.rt.print_bool\"(i32 1, i1 zeroext %t");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_char\"(i32 1, i8 zeroext %t"),
                       "@\"std.rt.print_char\"(i32 1, i8 zeroext %t");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.print_ptr\"(i32 1, ptr %t"),
                       "@\"std.rt.print_ptr\"(i32 1, ptr %t");
})

TEST(a_string_literal_prints_as_its_constant_and_length, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { print(\"hi\"); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.print_str\"(i32 1, ptr @.str.0, i64 2)"),
                       "call void @\"std.rt.print_str\"(i32 1, ptr @.str.0, i64 2)");
    // print writes no newline.
    // D12.2
    TEST_ASSERT_EQ_STR(absent("i8 zeroext 10"), "absent");
})

TEST(println_ends_with_the_newline_byte, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { println(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.print_char\"(i32 1, i8 zeroext 10)"),
                       "call void @\"std.rt.print_char\"(i32 1, i8 zeroext 10)");
})

TEST(eprint_writes_to_the_second_descriptor, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { eprintln(\"warn\"); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.print_str\"(i32 2, ptr @.str.0, i64 4)"),
                       "call void @\"std.rt.print_str\"(i32 2, ptr @.str.0, i64 4)");
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.print_char\"(i32 2, i8 zeroext 10)"),
                       "call void @\"std.rt.print_char\"(i32 2, i8 zeroext 10)");
})

TEST(fprint_evaluates_its_descriptor_once, {
    TEST_ASSERT_TRUE(emit("fn i32 fd() { return 1; }\n"
                          "fn i32 main() { fprintln(fd(), \"a\", \"b\"); return 0; }\n"));
    // `fd` is evaluated once and then each argument left to right.
    // D6.3, D12.2
    TEST_ASSERT_EQ_STR(found("  %t0 = call i32 @\"main.fd\"()\n"
                             "  call void @\"std.rt.print_str\"(i32 %t0, ptr @.str.0, i64 1)\n"
                             "  call void @\"std.rt.print_str\"(i32 %t0, ptr @.str.1, i64 1)\n"
                             "  call void @\"std.rt.print_char\"(i32 %t0, i8 zeroext 10)\n"),
                       "  %t0 = call i32 @\"main.fd\"()\n"
                       "  call void @\"std.rt.print_str\"(i32 %t0, ptr @.str.0, i64 1)\n"
                       "  call void @\"std.rt.print_str\"(i32 %t0, ptr @.str.1, i64 1)\n"
                       "  call void @\"std.rt.print_char\"(i32 %t0, i8 zeroext 10)\n");
})

TEST(a_string_variable_prints_as_its_two_header_fields, {
    TEST_ASSERT_TRUE(emit("fn i32 main() { string s = \"hi\"; print(s); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  %t3 = load ptr, ptr %t2, align 8\n"
                             "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                             "  %t5 = load i64, ptr %t4, align 8\n"
                             "  call void @\"std.rt.print_str\"(i32 1, ptr %t3, i64 %t5)\n"),
                       "  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  %t3 = load ptr, ptr %t2, align 8\n"
                       "  %t4 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                       "  %t5 = load i64, ptr %t4, align 8\n"
                       "  call void @\"std.rt.print_str\"(i32 1, ptr %t3, i64 %t5)\n");
})

TEST(an_enum_prints_with_its_table_and_member_count, {
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "fn i32 main() { color g = color.green; print(g); return 0; }\n"));
    TEST_ASSERT_EQ_STR(
        found("call void @\"std.rt.print_enum\"(i32 1, i32 %t0, ptr @.enum.main.color, i64 2)"),
        "call void @\"std.rt.print_enum\"(i32 1, i32 %t0, ptr @.enum.main.color, i64 2)");
})

// ---- the verifier over a corpus ----------------------------------------------------
// D19.1

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

// ---- several modules in one program (item 1) ---------------------------------------
// D9.10

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
    // An imported module stands before its importer, and each symbol is its
    // own module path plus its name.
    // D9.10, D19.5, D9.7
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

// ---- the runtime in the closure (item 14, item 22) ---------------------------------
// D9.10, D13.1

// A runtime for the tests below: the entry points these programs reach, with
// bodies that do nothing. A program the compiler builds holds the whole of
// `std/rt.ft`; what these tests ask about is the shape the emitter gives a
// definition of `std.rt`, which does not depend on the body.
static const char RUNTIME_SOURCE[] =
    "struct enum_member { i32 value; char* name; }\n"
    "string mut@ own mut args_store = {};\n"
    "fn void args_init(i32 argc, char* mut* argv) { }\n"
    "fn string@ args() { return args_store; }\n"
    "fn void flush_all() { }\n"
    "fn void print_i64(i32 fd, i64 v) { }\n"
    "fn void print_str(i32 fd, char* ptr, u64 len) { }\n"
    "fn void print_char(i32 fd, char c) { }\n"
    "fn void print_enum(i32 fd, i32 v, enum_member* m, u64 n) { }\n"
    "fn noreturn fail_div_zero(char* file, u32 line, u32 col) { while (true) { } }\n"
    "fn noreturn fail_div_overflow(char* file, u32 line, u32 col) { while (true) { } }\n";

TEST(a_noreturn_entry_point_of_section_5_1_carries_the_attribute_group_8, {
    TEST_ASSERT_TRUE(emit_with_runtime(
        RUNTIME_SOURCE, "fn i32 main() {\n    i32 a = 7;\n    i32 b = 0;\n    return a / b;\n}\n"));
    // `#8` is `#1` plus `cold`, on the definitions of the `noreturn` entry
    // points of section 5.1 and on no other fort function (item 14).
    TEST_ASSERT_EQ_STR(
        found("define dso_local void @\"std.rt.fail_div_zero\"(ptr %file.in, i32 %line.in,"
              " i32 %col.in) #8 {"),
        "define dso_local void @\"std.rt.fail_div_zero\"(ptr %file.in, i32 %line.in,"
        " i32 %col.in) #8 {");
    TEST_ASSERT_EQ_STR(found("attributes #8 = { cold noreturn nounwind \"frame-pointer\"=\"all\""
                             " \"probe-stack\"=\"inline-asm\" }"),
                       "attributes #8 = { cold noreturn nounwind \"frame-pointer\"=\"all\""
                       " \"probe-stack\"=\"inline-asm\" }");
    // `#2` held that set on the C runtime's declarations and is never
    // emitted; its index stays so that `#3` to `#7` keep their numbers (item
    // 14).
    // D19.5
    TEST_ASSERT_EQ_STR(absent("attributes #2 ="), "absent");
    // The module that holds the call holds the definition, so nothing is
    // declared (item 8).
    TEST_ASSERT_EQ_STR(absent("declare void @\"std.rt."), "absent");
    TEST_ASSERT_EQ_STR(found("  call void @\"std.rt.fail_div_zero\"(ptr @.file.0,"),
                       "  call void @\"std.rt.fail_div_zero\"(ptr @.file.0,");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_entry_point_that_returns_carries_the_ordinary_group, {
    TEST_ASSERT_TRUE(emit_with_runtime(RUNTIME_SOURCE, HELLO_SOURCE));
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"std.rt.print_char\"(i32 %fd.in,"
                             " i8 zeroext %c.in) #0 {"),
                       "define dso_local void @\"std.rt.print_char\"(i32 %fd.in,"
                       " i8 zeroext %c.in) #0 {");
    // An entry point that returns takes `#0` like any fort definition
    // (item 7), which is what the `#8` of the `noreturn` ones beside it in
    // the same module is held against (item 14).
    TEST_ASSERT_EQ_STR(absent("@\"std.rt.print_char\"(i32 %fd.in, i8 zeroext %c.in) #8"), "absent");
    // An aggregate result is the leading `ptr sret(%T)` of item 7, which is
    // what `main` passes its own place to (item 22).
    TEST_ASSERT_EQ_STR(
        found("define dso_local void @\"std.rt.args\"(ptr sret(%fort.span) %ret.sret) #0 {"),
        "define dso_local void @\"std.rt.args\"(ptr sret(%fort.span) %ret.sret) #0 {");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_noreturn_function_outside_the_runtime_keeps_the_group_of_item_20, {
    // The emitter matches the mangled name, so a function of another module
    // whose short name is an entry point's takes `#1` and not `#8`.
    // D9.7
    TEST_ASSERT_TRUE(emit_with_runtime(
        RUNTIME_SOURCE,
        "fn noreturn fail_div_zero(char* file, u32 line, u32 col) { while (true) { } }\n"
        "fn i32 main() {\n    i32 a = 7;\n    i32 b = 0;\n    return a / b;\n}\n"));
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"main.fail_div_zero\"(ptr %file.in,"
                             " i32 %line.in, i32 %col.in) #1 {"),
                       "define dso_local void @\"main.fail_div_zero\"(ptr %file.in,"
                       " i32 %line.in, i32 %col.in) #1 {");
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"std.rt.fail_div_zero\"(ptr %file.in,"
                             " i32 %line.in, i32 %col.in) #8 {"),
                       "define dso_local void @\"std.rt.fail_div_zero\"(ptr %file.in,"
                       " i32 %line.in, i32 %col.in) #8 {");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_enum_member_of_std_rt_is_the_named_type_every_module_carries, {
    // `%fort.enum_member` is the IR of `std.rt`'s own `struct enum_member`
    // and is emitted under that name rather than as a `%struct.` of its own:
    // one named type for one layout (item 2, item 21).
    TEST_ASSERT_TRUE(emit_with_runtime(RUNTIME_SOURCE,
                                       "enum color { red, green }\n"
                                       "fn i32 main() { println(color.green); return 0; }\n"));
    TEST_ASSERT_EQ_STR(absent("%struct.std.rt.enum_member"), "absent");
    TEST_ASSERT_EQ_SIZE(occurrences("%fort.enum_member = type { i32, ptr }"), (size_t)1);
    TEST_ASSERT_EQ_STR(found("[2 x %fort.enum_member]"), "[2 x %fort.enum_member]");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_runtime_stands_before_the_program_in_the_module, {
    // `std.rt` is a root of the closure and the program imports nothing, so
    // the dependency order puts the runtime first.
    // D9.10, D19.5
    TEST_ASSERT_TRUE(emit_with_runtime(RUNTIME_SOURCE, HELLO_SOURCE));
    TEST_ASSERT_TRUE(before("define dso_local void @\"std.rt.args_init\"",
                            "define dso_local i32 @\"main.main\""));
    TEST_ASSERT_TRUE(before("define dso_local i32 @\"main.main\"", "define dso_local i32 @main("));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- unfinished constructs are refused, never miscompiled --------------------------

TEST(no_construct_the_front_end_admits_reaches_the_emitters_refusal, {
    // The tickets that followed removed the emitter's `gen_todo` paths one by
    // one, and the one that lowered the module-level variable this test used
    // to be refused for removed the last one a program could reach: every
    // feature the bootstrap subset lacks is refused by the parser or the
    // checker first (floats, `?:`, do-while, multi-dimensional arrays), so a
    // program that checks is a program that is lowered. What the remaining
    // paths stand for is unchanged -- an unfinished path is a diagnostic and
    // never wrong code -- so a ticket that adds one puts its case here.
    // T-015, T-024: the tickets that emptied the `gen_todo` paths
    TEST_ASSERT_TRUE(emit("i32 mut counter = 0;\nfn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_NULL(strstr(gen_said(), "cannot generate code yet"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

int main(int argc, char** argv) {
    TEST_INIT("gen_module", argc, argv);
    TEST_RUN(the_program_of_section_6_1_is_emitted_as_that_section_shows);
    TEST_RUN(the_program_of_section_6_2_is_emitted_as_that_section_shows);
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
    TEST_RUN(a_noreturn_entry_point_of_section_5_1_carries_the_attribute_group_8);
    TEST_RUN(an_entry_point_that_returns_carries_the_ordinary_group);
    TEST_RUN(a_noreturn_function_outside_the_runtime_keeps_the_group_of_item_20);
    TEST_RUN(the_enum_member_of_std_rt_is_the_named_type_every_module_carries);
    TEST_RUN(the_runtime_stands_before_the_program_in_the_module);
    TEST_RUN(no_construct_the_front_end_admits_reaches_the_emitters_refusal);
    gen_done();
    TEST_EXIT();
}
