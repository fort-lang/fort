// Unit tests of the memory model the emitter works in (toolchain.md 6 item 3,
// D19.3): places, assignment, literals, copies and the evaluation order the
// walk keeps (D6.3). A struct, fixed array, span or `string` always occupies
// a place, so every one of these is a getelementptr, a memcpy or a memset and
// never an SSA aggregate.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// The declarations the struct tests share, before their `fn i32 main`.
static const char TYPES[] = "struct point { i32 x; i32 y; }\n"
                            "struct line { point a; point b; }\n"
                            "struct holder { i32[2] cells; u8 tag; }\n";

static char with_types[4096];

// `TYPES` followed by a `fn i32 main` whose body is `body`.
static const char* in_typed_main(const char* body) {
    TEST_UNUSED(snprintf(
        with_types, sizeof with_types, "%sfn i32 main() {\n%s    return 0;\n}\n", TYPES, body));
    return with_types;
}

// ---- places (item 3) ---------------------------------------------------------------

TEST(a_local_place_is_its_entry_block_alloca, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut a = 1;\n    a = 2;\n")));
    TEST_ASSERT_EQ_STR(found("  %a.0 = alloca i32, align 4\n  store i32 1, ptr %a.0, align 4\n"
                             "  store i32 2, ptr %a.0, align 4\n"),
                       "  %a.0 = alloca i32, align 4\n  store i32 1, ptr %a.0, align 4\n"
                       "  store i32 2, ptr %a.0, align 4\n");
})

TEST(a_field_of_a_field_chains_two_geps, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    line mut l = {};\n    l.b.x = 3;\n")));
    TEST_ASSERT_EQ_STR(
        found("  %t0 = getelementptr inbounds %struct.main.line, ptr %l.0, i32 0, i32 1\n"
              "  %t1 = getelementptr inbounds %struct.main.point, ptr %t0, i32 0, i32 0\n"
              "  store i32 3, ptr %t1, align 4\n"),
        "  %t0 = getelementptr inbounds %struct.main.line, ptr %l.0, i32 0, i32 1\n"
        "  %t1 = getelementptr inbounds %struct.main.point, ptr %t0, i32 0, i32 0\n"
        "  store i32 3, ptr %t1, align 4\n");
})

TEST(an_arrow_dereferences_the_pointer_it_loads, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    point mut p = {1, 2};\n"
                                        "    point mut* r = &p;\n    r->y = 9;\n")));
    TEST_ASSERT_EQ_STR(
        found("  %t2 = load ptr, ptr %r.1, align 8\n"
              "  %t3 = getelementptr inbounds %struct.main.point, ptr %t2, i32 0, i32 1\n"
              "  store i32 9, ptr %t3, align 4\n"),
        "  %t2 = load ptr, ptr %r.1, align 8\n"
        "  %t3 = getelementptr inbounds %struct.main.point, ptr %t2, i32 0, i32 1\n"
        "  store i32 9, ptr %t3, align 4\n");
})

TEST(a_dereference_is_the_place_the_pointer_designates, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut v = 1;\n    i32 mut* p = &v;\n"
                                  "    *p = 2;\n    println(*p);\n")));
    TEST_ASSERT_EQ_STR(
        found("  %t0 = load ptr, ptr %p.1, align 8\n  store i32 2, ptr %t0, align 4\n"),
        "  %t0 = load ptr, ptr %p.1, align 8\n  store i32 2, ptr %t0, align 4\n");
    TEST_ASSERT_EQ_STR(found("  %t2 = load i32, ptr %t1, align 4\n"),
                       "  %t2 = load i32, ptr %t1, align 4\n");
})

TEST(the_address_of_an_element_is_its_gep, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[2] mut a = {};\n    i64 i = 0;\n"
                                  "    i32 mut* c = &a[i];\n    *c = 6;\n")));
    TEST_ASSERT_EQ_STR(found("  %t2 = getelementptr inbounds [2 x i32], ptr %a.0, i64 0, i64 %t0\n"
                             "  store ptr %t2, ptr %c.2, align 8\n"),
                       "  %t2 = getelementptr inbounds [2 x i32], ptr %a.0, i64 0, i64 %t0\n"
                       "  store ptr %t2, ptr %c.2, align 8\n");
})

TEST(the_address_of_a_field_is_its_gep, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    point mut p = {1, 2};\n    i32 mut* c = &p.y;\n"
                                        "    *c = 5;\n")));
    TEST_ASSERT_EQ_STR(
        found("  %t2 = getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 1\n"
              "  store ptr %t2, ptr %c.1, align 8\n"),
        "  %t2 = getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 1\n"
        "  store ptr %t2, ptr %c.1, align 8\n");
})

TEST(an_array_inside_a_struct_is_reached_through_two_shapes, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    holder mut h = {};\n    i64 i = 1;\n"
                                        "    h.cells[i] = 4;\n")));
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %struct.main.holder, ptr %h.0, i32 0, i32 0"),
                       "getelementptr inbounds %struct.main.holder, ptr %h.0, i32 0, i32 0");
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds [2 x i32], ptr %t"),
                       "getelementptr inbounds [2 x i32], ptr %t");
})

// ---- assignment (D7.2, D6.3) -------------------------------------------------------

TEST(an_aggregate_assignment_is_a_memcpy_into_the_targets_place, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    point p = {1, 2};\n    line mut l = {};\n"
                                        "    l.a = p;\n")));
    TEST_ASSERT_EQ_STR(
        found("  %t2 = getelementptr inbounds %struct.main.line, ptr %l.1, i32 0, i32 0\n"
              "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %t2, ptr align 4 %p.0, i64 8, "
              "i1 false)\n"),
        "  %t2 = getelementptr inbounds %struct.main.line, ptr %l.1, i32 0, i32 0\n"
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %t2, ptr align 4 %p.0, i64 8, i1 false)\n");
})

TEST(an_assignment_through_a_pointer_copies_into_the_storage_it_reaches, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    point q = {1, 2};\n    point mut p = {};\n"
                                        "    point mut* r = &p;\n    *r = q;\n")));
    TEST_ASSERT_EQ_STR(found("  %t2 = load ptr, ptr %r.2, align 8\n"
                             "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %t2, ptr align 4 "
                             "%q.0, i64 8, i1 false)\n"),
                       "  %t2 = load ptr, ptr %r.2, align 8\n"
                       "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %t2, ptr align 4 %q.0, "
                       "i64 8, i1 false)\n");
})

TEST(the_target_of_an_assignment_is_evaluated_before_its_value, {
    TEST_ASSERT_TRUE(emit("fn u64 idx() { print(\"idx \"); return 1; }\n"
                          "fn i32 val() { print(\"val \"); return 3; }\n"
                          "fn i32 main() {\n    i32[3] mut a = {};\n"
                          "    a[idx()] = val();\n    return 0;\n}\n"));
    // The walk emits calls, loads and stores in source order (D6.3).
    TEST_ASSERT_TRUE(before("call i64 @\"main.idx\"()", "call i32 @\"main.val\"()"));
    TEST_ASSERT_TRUE(before("call i32 @\"main.val\"()", "store i32 %t"));
})

TEST(an_element_assignment_is_checked_like_a_read, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[2] mut a = {};\n    i64 i = 0;\n    a[i] = 5;\n")));
    TEST_ASSERT_EQ_STR(found("  %t1 = icmp uge i64 %t0, 2\n  br i1 %t1, label %L1, label %L0\n"),
                       "  %t1 = icmp uge i64 %t0, 2\n  br i1 %t1, label %L1, label %L0\n");
    TEST_ASSERT_EQ_STR(found("@fort_rt_fail_bounds"), "@fort_rt_fail_bounds");
})

TEST(a_string_assignment_writes_both_header_fields, {
    TEST_ASSERT_TRUE(emit(in_main("    string mut s = \"hi\";\n    s = \"bye\";\n")));
    TEST_ASSERT_EQ_STR(found("  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  store ptr @.str.1, ptr %t2, align 8\n"
                             "  %t3 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                             "  store i64 3, ptr %t3, align 8\n"),
                       "  %t2 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  store ptr @.str.1, ptr %t2, align 8\n"
                       "  %t3 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 1\n"
                       "  store i64 3, ptr %t3, align 8\n");
})

TEST(a_string_copied_from_a_variable_is_a_sixteen_byte_memcpy, {
    TEST_ASSERT_TRUE(emit(in_main("    string s = \"hi\";\n    string t = s;\n")));
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memcpy.p0.p0.i64(ptr align 8 %t.1, ptr align 8 %s.0, i64 16, "
              "i1 false)"),
        "call void @llvm.memcpy.p0.p0.i64(ptr align 8 %t.1, ptr align 8 %s.0, i64 16, i1 false)");
})

// ---- literals (D6.5) ---------------------------------------------------------------

TEST(a_positional_struct_literal_writes_its_fields_in_order, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    point p = {1, 2};\n")));
    TEST_ASSERT_EQ_STR(
        found("  %t0 = getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 0\n"
              "  store i32 1, ptr %t0, align 4\n"
              "  %t1 = getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 1\n"
              "  store i32 2, ptr %t1, align 4\n"),
        "  %t0 = getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 0\n"
        "  store i32 1, ptr %t0, align 4\n"
        "  %t1 = getelementptr inbounds %struct.main.point, ptr %p.0, i32 0, i32 1\n"
        "  store i32 2, ptr %t1, align 4\n");
    // A full positional literal needs no memset.
    TEST_ASSERT_EQ_STR(absent("llvm.memset"), "absent");
})

TEST(a_designated_literal_zeroes_the_place_first, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    point q = {.y = 7};\n")));
    // Omitted fields are zeroed (D6.5).
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memset.p0.i64(ptr align 4 %q.0, i8 0, i64 8, i1 false)\n"
              "  %t0 = getelementptr inbounds %struct.main.point, ptr %q.0, i32 0, i32 1\n"
              "  store i32 7, ptr %t0, align 4\n"),
        "  call void @llvm.memset.p0.i64(ptr align 4 %q.0, i8 0, i64 8, i1 false)\n"
        "  %t0 = getelementptr inbounds %struct.main.point, ptr %q.0, i32 0, i32 1\n"
        "  store i32 7, ptr %t0, align 4\n");
})

TEST(a_nested_literal_writes_through_the_outer_field, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    line l = {{1, 2}, {3, 4}};\n")));
    TEST_ASSERT_EQ_STR(
        found("  %t0 = getelementptr inbounds %struct.main.line, ptr %l.0, i32 0, i32 0\n"
              "  %t1 = getelementptr inbounds %struct.main.point, ptr %t0, i32 0, i32 0\n"
              "  store i32 1, ptr %t1, align 4\n"),
        "  %t0 = getelementptr inbounds %struct.main.line, ptr %l.0, i32 0, i32 0\n"
        "  %t1 = getelementptr inbounds %struct.main.point, ptr %t0, i32 0, i32 0\n"
        "  store i32 1, ptr %t1, align 4\n");
})

TEST(an_array_literal_writes_each_element_by_its_index, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[3] a = {7, 8, 9};\n")));
    TEST_ASSERT_EQ_STR(found("  %t0 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 0\n"
                             "  store i32 7, ptr %t0, align 4\n"
                             "  %t1 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 1\n"
                             "  store i32 8, ptr %t1, align 4\n"
                             "  %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 2\n"
                             "  store i32 9, ptr %t2, align 4\n"),
                       "  %t0 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 0\n"
                       "  store i32 7, ptr %t0, align 4\n"
                       "  %t1 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 1\n"
                       "  store i32 8, ptr %t1, align 4\n"
                       "  %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 2\n"
                       "  store i32 9, ptr %t2, align 4\n");
})

TEST(an_array_literal_of_calls_keeps_their_order, {
    TEST_ASSERT_TRUE(emit("fn i32 f(i32 x) { print(x); return x; }\n"
                          "fn i32 main() {\n    i32[2] a = {f(8), f(9)};\n    return a.len;\n}\n"));
    TEST_ASSERT_TRUE(before("call i32 @\"main.f\"(i32 8)", "call i32 @\"main.f\"(i32 9)"));
})

TEST(a_zero_literal_of_a_struct_with_an_array_is_one_memset, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    holder h = {};\n")));
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memset.p0.i64(ptr align 4 %h.0, i8 0, i64 12, i1 false)"),
        "call void @llvm.memset.p0.i64(ptr align 4 %h.0, i8 0, i64 12, i1 false)");
})

TEST(a_zero_literal_of_a_string_zeroes_its_header, {
    TEST_ASSERT_TRUE(emit(in_main("    string s = {};\n    println(s.len);\n")));
    TEST_ASSERT_EQ_STR(
        found("call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)"),
        "call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)");
})

TEST(an_array_length_from_sizeof_is_a_constant, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[sizeof(i64)] a = {};\n    println(a.len);\n")));
    TEST_ASSERT_EQ_STR(found("%a.0 = alloca [8 x i32], align 4"),
                       "%a.0 = alloca [8 x i32], align 4");
    // `.len` of a fixed array is an untyped constant and its operand is not
    // evaluated, so nothing loads the array (D3.4, D4.6).
    TEST_ASSERT_EQ_STR(found("  %t0 = sext i32 8 to i64\n"
                             "  call void @fort_rt_print_i64(i32 1, i64 %t0)\n"),
                       "  %t0 = sext i32 8 to i64\n"
                       "  call void @fort_rt_print_i64(i32 1, i64 %t0)\n");
})

// ---- aggregates across calls (item 7) ----------------------------------------------

TEST(an_aggregate_argument_gets_one_temporary_per_argument, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 sum(point a, point b) { return a.x +% b.y; }\n"
                          "fn i32 main() {\n    point p = {1, 2};\n"
                          "    return sum(p, p);\n}\n"));
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %struct.main.point, align 4"),
                       "%tmp0 = alloca %struct.main.point, align 4");
    TEST_ASSERT_EQ_STR(found("%tmp1 = alloca %struct.main.point, align 4"),
                       "%tmp1 = alloca %struct.main.point, align 4");
    TEST_ASSERT_EQ_STR(found("call i32 @\"main.sum\"(ptr %tmp0, ptr %tmp1)"),
                       "call i32 @\"main.sum\"(ptr %tmp0, ptr %tmp1)");
})

TEST(a_callee_may_write_to_its_aggregate_parameter, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 bump(point mut p) { p.x = 9; return p.x; }\n"
                          "fn i32 main() { point q = {1, 2}; return bump(q); }\n"));
    // D8.2's by-value rule is satisfied by the caller's copy (item 10).
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %struct.main.point, ptr %p.in, i32 0, i32 0"),
                       "getelementptr inbounds %struct.main.point, ptr %p.in, i32 0, i32 0");
})

TEST(an_aggregate_result_is_written_straight_into_its_destination, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn point make() { point p = {1, 2}; return p; }\n"
                          "fn i32 main() { point q = make(); return q.x; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @\"main.make\"(ptr %q.0)"),
                       "call void @\"main.make\"(ptr %q.0)");
    TEST_ASSERT_EQ_STR(absent("%tmp"), "absent");
})

TEST(an_aggregate_result_read_for_one_field_gets_a_temporary, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn point make() { point p = {1, 2}; return p; }\n"
                          "fn i32 main() { return make().y; }\n"));
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %struct.main.point, align 4"),
                       "%tmp0 = alloca %struct.main.point, align 4");
    TEST_ASSERT_EQ_STR(found("call void @\"main.make\"(ptr %tmp0)"),
                       "call void @\"main.make\"(ptr %tmp0)");
})

TEST(returning_an_aggregate_copies_it_through_the_sret_pointer, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn point make() { point p = {1, 2}; return p; }\n"
                          "fn i32 main() { point q = make(); return q.y; }\n"));
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ret.sret, ptr align 4 %p.0, "
              "i64 8, i1 false)\n  ret void\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ret.sret, ptr align 4 %p.0, i64 8, "
        "i1 false)\n  ret void\n");
})

TEST(a_string_literal_argument_is_built_in_a_temporary_and_copied, {
    TEST_ASSERT_TRUE(emit("fn u64 size(string s) { return s.len; }\n"
                          "fn i32 main() { println(size(\"hi\")); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %fort.span, align 8"),
                       "%tmp0 = alloca %fort.span, align 8");
    TEST_ASSERT_EQ_STR(found("store ptr @.str.0, ptr %t0, align 8"),
                       "store ptr @.str.0, ptr %t0, align 8");
    TEST_ASSERT_EQ_STR(found("call i64 @\"main.size\"(ptr %tmp0)"),
                       "call i64 @\"main.size\"(ptr %tmp0)");
})

TEST(a_discarded_call_still_runs, {
    TEST_ASSERT_TRUE(emit("fn i32 side() { print(\"s\"); return 1; }\n"
                          "fn i32 main() { side(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("%t0 = call i32 @\"main.side\"()"), "%t0 = call i32 @\"main.side\"()");
})

TEST(a_discarded_aggregate_call_gets_a_place_nothing_reads, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn point make() { point p = {1, 2}; return p; }\n"
                          "fn i32 main() { make(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %struct.main.point, align 4"),
                       "%tmp0 = alloca %struct.main.point, align 4");
    TEST_ASSERT_EQ_STR(found("call void @\"main.make\"(ptr %tmp0)"),
                       "call void @\"main.make\"(ptr %tmp0)");
})

TEST(an_aggregate_never_becomes_an_ssa_value, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    line l = {{1, 2}, {3, 4}};\n    line m = l;\n"
                                        "    holder h = {};\n    string s = \"x\";\n"
                                        "    println(m.a.x, h.tag, s.len);\n")));
    TEST_ASSERT_EQ_STR(absent("insertvalue"), "absent");
    TEST_ASSERT_EQ_STR(absent("extractvalue"), "absent");
    TEST_ASSERT_EQ_STR(absent("load %struct."), "absent");
    TEST_ASSERT_EQ_STR(absent("store %struct."), "absent");
    TEST_ASSERT_EQ_STR(absent("load %fort.span"), "absent");
    TEST_ASSERT_EQ_STR(absent("load [2 x i32]"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_alignment_of_every_place_comes_from_its_type, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    holder h = {};\n    i8 a = 1;\n    i16 b = 2;\n"
                                        "    i64 c = 3;\n    println(h.tag, a, b, c);\n")));
    // Every global, alloca, load and store carries an explicit alignment
    // (item 5).
    TEST_ASSERT_EQ_STR(found("%a.1 = alloca i8, align 1"), "%a.1 = alloca i8, align 1");
    TEST_ASSERT_EQ_STR(found("%b.2 = alloca i16, align 2"), "%b.2 = alloca i16, align 2");
    TEST_ASSERT_EQ_STR(found("%c.3 = alloca i64, align 8"), "%c.3 = alloca i64, align 8");
    TEST_ASSERT_EQ_STR(found("load i8, ptr %t0, align 1"), "load i8, ptr %t0, align 1");
})

int main(int argc, char** argv) {
    TEST_INIT("gen_aggregate", argc, argv);
    TEST_RUN(a_local_place_is_its_entry_block_alloca);
    TEST_RUN(a_field_of_a_field_chains_two_geps);
    TEST_RUN(an_arrow_dereferences_the_pointer_it_loads);
    TEST_RUN(a_dereference_is_the_place_the_pointer_designates);
    TEST_RUN(the_address_of_an_element_is_its_gep);
    TEST_RUN(the_address_of_a_field_is_its_gep);
    TEST_RUN(an_array_inside_a_struct_is_reached_through_two_shapes);
    TEST_RUN(an_aggregate_assignment_is_a_memcpy_into_the_targets_place);
    TEST_RUN(an_assignment_through_a_pointer_copies_into_the_storage_it_reaches);
    TEST_RUN(the_target_of_an_assignment_is_evaluated_before_its_value);
    TEST_RUN(an_element_assignment_is_checked_like_a_read);
    TEST_RUN(a_string_assignment_writes_both_header_fields);
    TEST_RUN(a_string_copied_from_a_variable_is_a_sixteen_byte_memcpy);
    TEST_RUN(a_positional_struct_literal_writes_its_fields_in_order);
    TEST_RUN(a_designated_literal_zeroes_the_place_first);
    TEST_RUN(a_nested_literal_writes_through_the_outer_field);
    TEST_RUN(an_array_literal_writes_each_element_by_its_index);
    TEST_RUN(an_array_literal_of_calls_keeps_their_order);
    TEST_RUN(a_zero_literal_of_a_struct_with_an_array_is_one_memset);
    TEST_RUN(a_zero_literal_of_a_string_zeroes_its_header);
    TEST_RUN(an_array_length_from_sizeof_is_a_constant);
    TEST_RUN(an_aggregate_argument_gets_one_temporary_per_argument);
    TEST_RUN(a_callee_may_write_to_its_aggregate_parameter);
    TEST_RUN(an_aggregate_result_is_written_straight_into_its_destination);
    TEST_RUN(an_aggregate_result_read_for_one_field_gets_a_temporary);
    TEST_RUN(returning_an_aggregate_copies_it_through_the_sret_pointer);
    TEST_RUN(a_string_literal_argument_is_built_in_a_temporary_and_copied);
    TEST_RUN(a_discarded_call_still_runs);
    TEST_RUN(a_discarded_aggregate_call_gets_a_place_nothing_reads);
    TEST_RUN(an_aggregate_never_becomes_an_ssa_value);
    TEST_RUN(the_alignment_of_every_place_comes_from_its_type);
    gen_done();
    TEST_EXIT();
}
