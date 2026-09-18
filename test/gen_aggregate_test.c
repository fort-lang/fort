// Tests the emitter memory model: places, assignment, literals, copies, and evaluation
// order the walk keeps. A struct, fixed array, span or `string` always occupies a place. Every one
// of these is a getelementptr, a memcpy or a memset and never an SSA aggregate.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// The declarations the struct tests share, before their `fn main() i32`.
static const char TYPES[] = "struct point { i32 x; i32 y; }\n"
                            "struct line { point a; point b; }\n"
                            "struct holder { i32[2] cells; u8 tag; }\n";

static char with_types[4096];

// Returns `TYPES` followed by a `fn main() i32` whose body is `body`.
// The next call invalidates the returned shared-buffer result.
static const char* in_typed_main(const char* body) {
    TEST_UNUSED(snprintf(
        with_types, sizeof with_types, "%sfn main() i32 {\n%s    return 0;\n}\n", TYPES, body));
    return with_types;
}

// ---- places ---------------------------------------------------------------

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

// ---- assignment --------------------------------------------------------------------

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
    TEST_ASSERT_TRUE(emit("fn idx() u64 { print(\"idx \"); return 1; }\n"
                          "fn val() i32 { print(\"val \"); return 3; }\n"
                          "fn main() i32 {\n    i32[3] mut a = {};\n"
                          "    a[idx()] = val();\n    return 0;\n}\n"));
    // The walk emits calls, loads and stores in source order.
    TEST_ASSERT_TRUE(before("call i64 @\"main.idx\"()", "call i32 @\"main.val\"()"));
    TEST_ASSERT_TRUE(before("call i32 @\"main.val\"()", "store i32 %t"));
})

TEST(an_element_assignment_is_checked_like_a_read, {
    TEST_ASSERT_TRUE(emit(in_main("    i32[2] mut a = {};\n    i64 i = 0;\n    a[i] = 5;\n")));
    TEST_ASSERT_EQ_STR(found("  %t1 = icmp uge i64 %t0, 2\n  br i1 %t1, label %L1, label %L0\n"),
                       "  %t1 = icmp uge i64 %t0, 2\n  br i1 %t1, label %L1, label %L0\n");
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_bounds\""), "@\"std.rt.fail_bounds\"");
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

// ---- literals ----------------------------------------------------------------------

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
    // Omitted fields are zeroed.
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
    TEST_ASSERT_TRUE(emit("fn f(i32 x) i32 { print(x); return x; }\n"
                          "fn main() i32 {\n    i32[2] a = {f(8), f(9)};\n    return a.len;\n}\n"));
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
    // evaluated, so nothing loads the array.
    TEST_ASSERT_EQ_STR(found("  %t0 = sext i32 8 to i64\n"
                             "  call void @\"std.rt.print_i64\"(i32 1, i64 %t0)\n"),
                       "  %t0 = sext i32 8 to i64\n"
                       "  call void @\"std.rt.print_i64\"(i32 1, i64 %t0)\n");
})

// ---- aggregates across calls ----------------------------------------------

TEST(an_aggregate_argument_gets_one_temporary_per_argument, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn sum(point a, point b) i32 { return a.x +% b.y; }\n"
                          "fn main() i32 {\n    point p = {1, 2};\n"
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
                          "fn bump(point mut p) i32 { p.x = 9; return p.x; }\n"
                          "fn main() i32 { point q = {1, 2}; return bump(q); }\n"));
    // The by-value rule is satisfied by the caller's copy.
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds %struct.main.point, ptr %p.in, i32 0, i32 0"),
                       "getelementptr inbounds %struct.main.point, ptr %p.in, i32 0, i32 0");
})

TEST(an_aggregate_result_is_written_straight_into_its_destination, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn make() point { point p = {1, 2}; return p; }\n"
                          "fn main() i32 { point q = make(); return q.x; }\n"));
    TEST_ASSERT_EQ_STR(found("call void @\"main.make\"(ptr sret(%struct.main.point) %q.0)"),
                       "call void @\"main.make\"(ptr sret(%struct.main.point) %q.0)");
    TEST_ASSERT_EQ_STR(absent("%tmp"), "absent");
})

TEST(an_aggregate_result_read_for_one_field_gets_a_temporary, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn make() point { point p = {1, 2}; return p; }\n"
                          "fn main() i32 { return make().y; }\n"));
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %struct.main.point, align 4"),
                       "%tmp0 = alloca %struct.main.point, align 4");
    TEST_ASSERT_EQ_STR(found("call void @\"main.make\"(ptr sret(%struct.main.point) %tmp0)"),
                       "call void @\"main.make\"(ptr sret(%struct.main.point) %tmp0)");
})

TEST(returning_an_aggregate_copies_it_through_the_sret_pointer, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn make() point { point p = {1, 2}; return p; }\n"
                          "fn main() i32 { point q = make(); return q.y; }\n"));
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ret.sret, ptr align 4 %p.0, "
              "i64 8, i1 false)\n  ret void\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ret.sret, ptr align 4 %p.0, i64 8, "
        "i1 false)\n  ret void\n");
})

TEST(a_string_literal_argument_is_built_in_a_temporary_and_copied, {
    TEST_ASSERT_TRUE(emit("fn size(string s) u64 { return s.len; }\n"
                          "fn main() i32 { println(size(\"hi\")); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %fort.span, align 8"),
                       "%tmp0 = alloca %fort.span, align 8");
    TEST_ASSERT_EQ_STR(found("store ptr @.str.0, ptr %t0, align 8"),
                       "store ptr @.str.0, ptr %t0, align 8");
    TEST_ASSERT_EQ_STR(found("call i64 @\"main.size\"(ptr %tmp0)"),
                       "call i64 @\"main.size\"(ptr %tmp0)");
})

TEST(a_discarded_call_still_runs, {
    TEST_ASSERT_TRUE(emit("fn side() i32 { print(\"s\"); return 1; }\n"
                          "fn main() i32 { side(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("%t0 = call i32 @\"main.side\"()"), "%t0 = call i32 @\"main.side\"()");
})

TEST(a_discarded_aggregate_call_gets_a_place_nothing_reads, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn make() point { point p = {1, 2}; return p; }\n"
                          "fn main() i32 { make(); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %struct.main.point, align 4"),
                       "%tmp0 = alloca %struct.main.point, align 4");
    TEST_ASSERT_EQ_STR(found("call void @\"main.make\"(ptr sret(%struct.main.point) %tmp0)"),
                       "call void @\"main.make\"(ptr sret(%struct.main.point) %tmp0)");
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

TEST(a_struct_literal_read_for_one_field_is_built_in_a_temporary, {
    // Field access on an rvalue struct is allowed and copies it through a
    // temporary, which is the `%tmp<K>` counter.
    TEST_ASSERT_TRUE(emit(in_typed_main("    println(point{3, 4}.x);\n")));
    TEST_ASSERT_EQ_STR(found("  %tmp0 = alloca %struct.main.point, align 4\n"),
                       "  %tmp0 = alloca %struct.main.point, align 4\n");
    TEST_ASSERT_EQ_STR(
        found("  %t0 = getelementptr inbounds %struct.main.point, ptr %tmp0, i32 0, i32 0\n"
              "  store i32 3, ptr %t0, align 4\n"
              "  %t1 = getelementptr inbounds %struct.main.point, ptr %tmp0, i32 0, i32 1\n"
              "  store i32 4, ptr %t1, align 4\n"
              "  %t2 = getelementptr inbounds %struct.main.point, ptr %tmp0, i32 0, i32 0\n"
              "  %t3 = load i32, ptr %t2, align 4\n"),
        "  %t0 = getelementptr inbounds %struct.main.point, ptr %tmp0, i32 0, i32 0\n"
        "  store i32 3, ptr %t0, align 4\n"
        "  %t1 = getelementptr inbounds %struct.main.point, ptr %tmp0, i32 0, i32 1\n"
        "  store i32 4, ptr %t1, align 4\n"
        "  %t2 = getelementptr inbounds %struct.main.point, ptr %tmp0, i32 0, i32 0\n"
        "  %t3 = load i32, ptr %t2, align 4\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_array_literal_indexed_is_built_in_a_temporary_too, {
    // An rvalue array literal gets a temporary place before indexing.
    // The index reads the element from that place.
    TEST_ASSERT_TRUE(emit(in_main("    println(i32[2]{7, 8}[1]);\n")));
    TEST_ASSERT_EQ_STR(found("  %tmp0 = alloca [2 x i32], align 4\n"),
                       "  %tmp0 = alloca [2 x i32], align 4\n");
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds [2 x i32], ptr %tmp0, i64 0, i64 0\n"),
                       "getelementptr inbounds [2 x i32], ptr %tmp0, i64 0, i64 0\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_cast_of_an_aggregate_reaches_its_field_through_a_temporary, {
    // A cast between aggregates only drops marks and emits nothing of its own. However, its result
    // is an rvalue, so a field of it is read out of a copy.
    TEST_ASSERT_TRUE(emit(in_typed_main("    point p = {1, 2};\n"
                                        "    println(cast(p, point).y);\n")));
    TEST_ASSERT_EQ_STR(found("  %tmp0 = alloca %struct.main.point, align 4\n"),
                       "  %tmp0 = alloca %struct.main.point, align 4\n");
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %p.0, i64 8, "
              "i1 false)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %p.0, i64 8, "
        "i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_arrow_reaches_the_pseudo_fields_of_a_string, {
    // For a span or string pointer, `p->f` is `(*p).f`.
    // The emitter loads the pointer and then reads the header field.
    TEST_ASSERT_TRUE(emit(in_main("    string s = \"hi\";\n    string* p = &s;\n"
                                  "    println(p->len);\n")));
    TEST_ASSERT_EQ_STR(found("  %t2 = load ptr, ptr %p.1, align 8\n"
                             "  %t3 = getelementptr inbounds %fort.span, ptr %t2, i32 0, i32 1\n"
                             "  %t4 = load i64, ptr %t3, align 8\n"),
                       "  %t2 = load ptr, ptr %p.1, align 8\n"
                       "  %t3 = getelementptr inbounds %fort.span, ptr %t2, i32 0, i32 1\n"
                       "  %t4 = load i64, ptr %t3, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_named_struct_type_holds_the_memory_type_of_every_field_in_order, {
    // Fields keep declaration order and use their memory types.
    // The test also rejects packed layouts and incorrect pointer widths.
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "struct point { i32 x; i32 y; }\n"
                          "struct rec { u8 tag; i32 n; u16 k; i64 big; bool on; string s;"
                          " point p; i32[3] cells; color c; point* up; fn (i32) i32 f; }\n"
                          "fn main() i32 { rec r = {}; return r.n; }\n"));
    TEST_ASSERT_EQ_STR(found("%struct.main.rec = type { i8, i32, i16, i64, i8, %fort.span, "
                             "%struct.main.point, [3 x i32], i32, ptr, ptr }\n"),
                       "%struct.main.rec = type { i8, i32, i16, i64, i8, %fort.span, "
                       "%struct.main.point, [3 x i32], i32, ptr, ptr }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// The two declaration orders of a pair of mutually recursive structs, and the buffers their modules
// are held against each other in. LLVM's type definitions follow the order the file declares the
// structs in. What the two modules must agree on byte for byte is each definition on its own and
// the whole of the rest of the module.
static const char RECURSIVE_SPAN_FIRST[] =
    "struct vec { node mut* mut@ own items; }\n"
    "struct node { vec list; i32 tag; }\n"
    "fn main() i32 { node mut n = {}; n.tag = 3;\n"
    "    return cast(n.list.items.len, i32) + n.tag - 3; }\n";
static const char RECURSIVE_VALUE_FIRST[] =
    "struct node { vec list; i32 tag; }\n"
    "struct vec { node mut* mut@ own items; }\n"
    "fn main() i32 { node mut n = {}; n.tag = 3;\n"
    "    return cast(n.list.items.len, i32) + n.tag - 3; }\n";

enum { LINES_CAP = 8192 };
static char first_order[LINES_CAP];
static char second_order[LINES_CAP];

// Copies matching lines when `keep` is true and nonmatching lines otherwise.
// Returns "the module fitted" on success and a truncation status otherwise.
// The status lets the test distinguish truncation from unexpected module text.
static const char* module_lines(char* dst, size_t cap, const char* prefix, bool keep) {
    size_t used = 0;
    const char* p = ir();
    while (*p != '\0') {
        const char* eol = strchr(p, '\n');
        if (eol == NULL) {
            break;
        }
        const size_t len = (size_t)(eol - p) + 1;
        if (gen_starts_with(p, prefix) == keep) {
            if (used + len >= cap) {
                return "the module did not fit its buffer";
            }
            TEST_UNUSED(memcpy(dst + used, p, len));
            used += len;
        }
        p = eol + 1;
    }
    dst[used] = '\0';
    return "the module fitted";
}

TEST(mutually_recursive_structs_emit_one_module_in_either_order, {
    // A span of pointers to a struct that holds the span by value: neither size depends on the
    // other, so neither declaration order is special. The span is the two words of `%fort.span`
    // whichever order the file is written in.
    TEST_ASSERT_TRUE(emit(RECURSIVE_SPAN_FIRST));
    TEST_ASSERT_EQ_STR(found("%struct.main.vec = type { %fort.span }\n"),
                       "%struct.main.vec = type { %fort.span }\n");
    TEST_ASSERT_EQ_STR(found("%struct.main.node = type { %struct.main.vec, i32 }\n"),
                       "%struct.main.node = type { %struct.main.vec, i32 }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
    TEST_ASSERT_EQ_UINT64(occurrences("%struct.main.vec = type"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("%struct.main.node = type"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(module_lines(first_order, sizeof first_order, "%struct.", false),
                       "the module fitted");
    TEST_ASSERT_TRUE(emit(RECURSIVE_VALUE_FIRST));
    TEST_ASSERT_EQ_STR(found("%struct.main.vec = type { %fort.span }\n"),
                       "%struct.main.vec = type { %fort.span }\n");
    TEST_ASSERT_EQ_STR(found("%struct.main.node = type { %struct.main.vec, i32 }\n"),
                       "%struct.main.node = type { %struct.main.vec, i32 }\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
    // Only the two type definitions can differ.
    // Offsets, getelementptrs, memset width, and definition order must match.
    TEST_ASSERT_EQ_STR(module_lines(second_order, sizeof second_order, "%struct.", false),
                       "the module fitted");
    TEST_ASSERT_EQ_STR(second_order, first_order);
})

TEST(an_enum_field_is_four_bytes_and_moves_the_fields_after_it, {
    // An enum's underlying type is `i32` and its size 4.
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "struct tagged { u8 t; color c; i64 big; }\n"
                          "fn main() i32 { tagged mut v = {}; v.big = 1;"
                          " return cast(v.c, i32); }\n"));
    TEST_ASSERT_EQ_STR(found("%struct.main.tagged = type { i8, i32, i64 }\n"),
                       "%struct.main.tagged = type { i8, i32, i64 }\n");
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memset.p0.i64(ptr align 8 %v.0, i8 0, i64 16, i1 false)\n"),
        "  call void @llvm.memset.p0.i64(ptr align 8 %v.0, i8 0, i64 16, i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_struct_wider_than_two_words_is_returned_and_copied_whole, {
    // A struct wider than the two words the other tests use is asserted here.
    TEST_ASSERT_TRUE(emit("struct big { i64 a; i64 b; i64 c; i64 d; }\n"
                          "fn make() big { big b = {1, 2, 3, 4}; return b; }\n"
                          "fn last(big b) i64 { return b.d; }\n"
                          "fn main() i32 { big x = make(); big y = x;"
                          " println(last(y)); return 0; }\n"));
    TEST_ASSERT_EQ_STR(
        found("define dso_local void @\"main.make\"(ptr sret(%struct.main.big) %ret.sret) #0 {\n"),
        "define dso_local void @\"main.make\"(ptr sret(%struct.main.big) %ret.sret) #0 {\n");
    TEST_ASSERT_EQ_STR(found("define dso_local i64 @\"main.last\"(ptr %b.in) #0 {\n"),
                       "define dso_local i64 @\"main.last\"(ptr %b.in) #0 {\n");
    // The result goes straight into the destination, the copy and the
    // argument copy each move all thirty-two bytes.
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %ret.sret, ptr align 8 %b.0, "
              "i64 32, i1 false)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %ret.sret, ptr align 8 %b.0, "
        "i64 32, i1 false)\n");
    TEST_ASSERT_EQ_STR(
        found("  call void @\"main.make\"(ptr sret(%struct.main.big) %x.0)\n"
              "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %y.1, ptr align 8 %x.0, i64 32, "
              "i1 false)\n"
              "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp0, ptr align 8 %y.1, i64 32, "
              "i1 false)\n"
              "  %t0 = call i64 @\"main.last\"(ptr %tmp0)\n"),
        "  call void @\"main.make\"(ptr sret(%struct.main.big) %x.0)\n"
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %y.1, ptr align 8 %x.0, i64 32, "
        "i1 false)\n"
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp0, ptr align 8 %y.1, i64 32, "
        "i1 false)\n"
        "  %t0 = call i64 @\"main.last\"(ptr %tmp0)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_copy_and_a_zero_move_the_padded_size_of_the_struct, {
    // `struct wide { i64 big; u8 tail; }` is nine bytes of fields and sixteen
    // of storage: the size C rounds up to the alignment. A memcpy or a memset
    // of anything less would leave the tail of the destination behind.
    TEST_ASSERT_TRUE(emit("struct wide { i64 big; u8 tail; }\n"
                          "fn main() i32 { wide mut w = {}; wide v = w; w.tail = 1;"
                          " return cast(v.tail, i32); }\n"));
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memset.p0.i64(ptr align 8 %w.0, i8 0, i64 16, i1 false)\n"),
        "  call void @llvm.memset.p0.i64(ptr align 8 %w.0, i8 0, i64 16, i1 false)\n");
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %v.1, ptr align 8 %w.0, i64 16, "
              "i1 false)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %v.1, ptr align 8 %w.0, i64 16, "
        "i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_alignment_of_every_place_comes_from_its_type, {
    TEST_ASSERT_TRUE(emit(in_typed_main("    holder h = {};\n    i8 a = 1;\n    i16 b = 2;\n"
                                        "    i64 c = 3;\n    println(h.tag, a, b, c);\n")));
    // Every global, alloca, load and store carries an explicit alignment.
    TEST_ASSERT_EQ_STR(found("%a.1 = alloca i8, align 1"), "%a.1 = alloca i8, align 1");
    TEST_ASSERT_EQ_STR(found("%b.2 = alloca i16, align 2"), "%b.2 = alloca i16, align 2");
    TEST_ASSERT_EQ_STR(found("%c.3 = alloca i64, align 8"), "%c.3 = alloca i64, align 8");
    TEST_ASSERT_EQ_STR(found("load i8, ptr %t0, align 1"), "load i8, ptr %t0, align 1");
})

// ---- the element classes of a fixed array ---------------------------------

TEST(an_array_of_a_padded_struct_strides_by_the_padded_size, {
    // `pixel` is five bytes of fields and eight of storage.
    TEST_ASSERT_TRUE(emit("struct pixel { i32 code; char tag; }\n"
                          "fn main() i32 {\n    pixel[3] mut ps = {};\n"
                          "    ps[2] = pixel{3, 'c'};\n    return ps[2].code;\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %ps.0 = alloca [3 x %struct.main.pixel], align 4\n"),
                       "  %ps.0 = alloca [3 x %struct.main.pixel], align 4\n");
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memset.p0.i64(ptr align 4 %ps.0, i8 0, i64 24, i1 false)\n"),
        "  call void @llvm.memset.p0.i64(ptr align 4 %ps.0, i8 0, i64 24, i1 false)\n");
    TEST_ASSERT_EQ_STR(
        found("getelementptr inbounds [3 x %struct.main.pixel], ptr %ps.0, i64 0, i64 %t0\n"),
        "getelementptr inbounds [3 x %struct.main.pixel], ptr %ps.0, i64 0, i64 %t0\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_single_element_array_is_still_an_array_type, {
    // An array length is positive, and 1 is the smallest value.
    // The emitter must still use the array shape to reach its element.
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32[1] one = {7};\n"
                          "    i64 i = 0;\n    return one[i] +% cast(one.len, i32);\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %one.0 = alloca [1 x i32], align 4\n"),
                       "  %one.0 = alloca [1 x i32], align 4\n");
    TEST_ASSERT_EQ_STR(found("getelementptr inbounds [1 x i32], ptr %one.0, i64 0, i64 0\n"),
                       "getelementptr inbounds [1 x i32], ptr %one.0, i64 0, i64 0\n");
    // `.len` is a constant, so the bounds check of the read compares against the literal 1.
    TEST_ASSERT_EQ_STR(found("  %t2 = icmp uge i64 %t1, 1\n"), "  %t2 = icmp uge i64 %t1, 1\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_array_of_function_pointers_is_an_array_of_ptr, {
    // A function type carries no suffix of its own. An array of function pointers is an array of a
    // struct that holds one. Every pointer is the opaque `ptr`, so the struct is one word and the
    // array strides by it.
    TEST_ASSERT_TRUE(emit("struct slot { fn (i32) i32 f; }\n"
                          "fn twice(i32 n) i32 { return n *% 2; }\n"
                          "fn main() i32 {\n    slot[2] table = {{twice}, {twice}};\n"
                          "    i64 i = 1;\n    return table[i].f(5);\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %table.0 = alloca [2 x %struct.main.slot], align 8\n"),
                       "  %table.0 = alloca [2 x %struct.main.slot], align 8\n");
    const char* want = "%struct.main.slot = type { ptr }\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    // The element is loaded as a `ptr` and called with the function type written out, since an
    // opaque pointer carries none.
    TEST_ASSERT_EQ_STR(found("  %t9 = call i32 (i32) %t8(i32 5)\n"),
                       "  %t9 = call i32 (i32) %t8(i32 5)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_array_of_strings_strides_by_the_span_header, {
    // A `string` is the sixteen-byte `%fort.span`.
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    string[2] names = {\"a\", \"bc\"};\n"
                          "    i64 i = 1;\n    return cast(names[i].len, i32);\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %names.0 = alloca [2 x %fort.span], align 8\n"),
                       "  %names.0 = alloca [2 x %fort.span], align 8\n");
    const char* want = "getelementptr inbounds [2 x %fort.span], ptr %names.0, i64 0, i64 1\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_one_byte_element_and_an_eight_byte_one_keep_their_own_alignments, {
    // The array's alignment is its element's, so a `char[4]` is one-byte
    // aligned and an `i64[2]` eight.
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    char[4] mut cs = {};\n    i64[2] longs = {2, 1};\n"
                          "    cs[0] = 'a';\n    return cast(longs[0], i32);\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %cs.0 = alloca [4 x i8], align 1\n"),
                       "  %cs.0 = alloca [4 x i8], align 1\n");
    TEST_ASSERT_EQ_STR(found("  %longs.1 = alloca [2 x i64], align 8\n"),
                       "  %longs.1 = alloca [2 x i64], align 8\n");
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memset.p0.i64(ptr align 1 %cs.0, i8 0, i64 4, i1 false)\n"),
        "  call void @llvm.memset.p0.i64(ptr align 1 %cs.0, i8 0, i64 4, i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_array_copy_moves_every_element_at_once, {
    // A fixed array is a value type: assignment copies every element, which
    // is one `llvm.memcpy` of the whole array.
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i64[3] a = {1, 2, 3};\n    i64[3] b = a;\n"
                          "    return cast(b[0], i32);\n}\n"));
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %b.1, ptr align 8 %a.0, i64 24, "
              "i1 false)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %b.1, ptr align 8 %a.0, i64 24, "
        "i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
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
    TEST_RUN(a_struct_literal_read_for_one_field_is_built_in_a_temporary);
    TEST_RUN(an_array_literal_indexed_is_built_in_a_temporary_too);
    TEST_RUN(a_cast_of_an_aggregate_reaches_its_field_through_a_temporary);
    TEST_RUN(an_arrow_reaches_the_pseudo_fields_of_a_string);
    TEST_RUN(a_named_struct_type_holds_the_memory_type_of_every_field_in_order);
    TEST_RUN(mutually_recursive_structs_emit_one_module_in_either_order);
    TEST_RUN(an_enum_field_is_four_bytes_and_moves_the_fields_after_it);
    TEST_RUN(a_struct_wider_than_two_words_is_returned_and_copied_whole);
    TEST_RUN(a_copy_and_a_zero_move_the_padded_size_of_the_struct);
    TEST_RUN(the_alignment_of_every_place_comes_from_its_type);
    TEST_RUN(an_array_of_a_padded_struct_strides_by_the_padded_size);
    TEST_RUN(a_single_element_array_is_still_an_array_type);
    TEST_RUN(an_array_of_function_pointers_is_an_array_of_ptr);
    TEST_RUN(an_array_of_strings_strides_by_the_span_header);
    TEST_RUN(a_one_byte_element_and_an_eight_byte_one_keep_their_own_alignments);
    TEST_RUN(an_array_copy_moves_every_element_at_once);
    gen_done();
    TEST_EXIT();
}
