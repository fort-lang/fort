// Unit tests of what the emitter writes for ownership (toolchain.md 6 item 18):
// the read-then-zero of `move`, the implicit move a `return` of an `own` local
// performs, and the load, compare and branch of the overwrite check, which
// stands after the right-hand side and immediately before the store and which
// only `--release` removes.
// D17.5, D17.6, D17.11, D19.6
//
// The rules the checker enforces are in check_own_test.c; `new` and `del` are
// in gen_alloc_test.c.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers)
// The literals below are the test data: the sizes, alignments, columns and
// temporary numbers of the module each program emits.

// A struct with one owning field and one plain one, so that a move of the whole
// value is 24 bytes and a move of the field is 16.
// D3.8, D17.7
#define VEC "struct vec {\n    i32 mut@ own data;\n    u64 len;\n}\n"

// ---- move (item 18) ----------------------------------------------------------------
// D17.6

TEST(move_of_a_pointer_loads_before_it_stores_null, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut* own p = new(i32);\n"
                          "    i32 mut* own q = move(p);\n    del(q);\n    return 0;\n}\n"));
    // The value is read first, so the operand's own storage may be the
    // destination as well.
    // D17.6
    TEST_ASSERT_EQ_STR(found("  %t3 = load ptr, ptr %p.0, align 8\n"
                             "  store ptr null, ptr %p.0, align 8\n"),
                       "  %t3 = load ptr, ptr %p.0, align 8\n"
                       "  store ptr null, ptr %p.0, align 8\n");
    // The declaration that receives it carries the check, so the store stands
    // in its continuation block (item 18).
    // D17.11
    TEST_ASSERT_EQ_STR(found("\nL2:\n  store ptr %t3, ptr %q.1, align 8\n"),
                       "\nL2:\n  store ptr %t3, ptr %q.1, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(move_of_a_span_copies_the_header_and_zeroes_it, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut@ own s = new(i32, 2);\n"
                          "    i32 mut@ own t = move(s);\n    del(t);\n    return 0;\n}\n"));
    // Sixteen bytes copied, then sixteen zeroed: the span's whole header.
    // D3.5, D17.6
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp2, ptr align 8 %s.0, "
              "i64 16, i1 false)\n"
              "  call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp2, ptr align 8 %s.0, "
        "i64 16, i1 false)\n"
        "  call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(move_of_an_owning_aggregate_copies_the_whole_value, {
    TEST_ASSERT_TRUE(emit(VEC "fn main() i32 {\n    vec mut a = {};\n"
                              "    a.data = new(i32, 2);\n    vec b = move(a);\n"
                              "    del(b.data);\n    return 0;\n}\n"));
    // A `vec` is 24 bytes, so the move is 24 and not the 16 of its field.
    // D3.8, D17.7
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp1, ptr align 8 %a.0, "
              "i64 24, i1 false)\n"
              "  call void @llvm.memset.p0.i64(ptr align 8 %a.0, i8 0, i64 24, i1 false)\n"
              "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %b.1, ptr align 8 %tmp1, "
              "i64 24, i1 false)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp1, ptr align 8 %a.0, "
        "i64 24, i1 false)\n"
        "  call void @llvm.memset.p0.i64(ptr align 8 %a.0, i8 0, i64 24, i1 false)\n"
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %b.1, ptr align 8 %tmp1, "
        "i64 24, i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(move_out_of_a_field_empties_that_field_alone, {
    TEST_ASSERT_TRUE(emit(VEC "fn main() i32 {\n    vec mut a = {};\n"
                              "    a.data = new(i32, 2);\n"
                              "    i32 mut@ own d = move(a.data);\n"
                              "    del(d);\n    return 0;\n}\n"));
    // The field's address is the destination of the zeroing, so `a.len` is
    // untouched.
    // D17.6
    TEST_ASSERT_EQ_STR(found("  %t9 = getelementptr inbounds %struct.main.vec, ptr %a.0, "
                             "i32 0, i32 0\n"
                             "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp2, "
                             "ptr align 8 %t9, i64 16, i1 false)\n"
                             "  call void @llvm.memset.p0.i64(ptr align 8 %t9, i8 0, "
                             "i64 16, i1 false)\n"),
                       "  %t9 = getelementptr inbounds %struct.main.vec, ptr %a.0, "
                       "i32 0, i32 0\n"
                       "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp2, "
                       "ptr align 8 %t9, i64 16, i1 false)\n"
                       "  call void @llvm.memset.p0.i64(ptr align 8 %t9, i8 0, "
                       "i64 16, i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(move_is_the_same_in_both_build_modes, {
    static const char PROGRAM[] = "fn main() i32 {\n    i32 mut* own p = new(i32);\n"
                                  "    i32 mut* own q = move(p);\n    del(q);\n    return 0;\n}\n";
    // `move` zeroes its operand in both build modes (item 18).
    // D17.6
    TEST_ASSERT_TRUE(emit_release(PROGRAM));
    TEST_ASSERT_EQ_STR(found("  %t1 = load ptr, ptr %p.0, align 8\n"
                             "  store ptr null, ptr %p.0, align 8\n"
                             "  store ptr %t1, ptr %q.1, align 8\n"),
                       "  %t1 = load ptr, ptr %p.0, align 8\n"
                       "  store ptr null, ptr %p.0, align 8\n"
                       "  store ptr %t1, ptr %q.1, align 8\n");
    // Release mode neither zeroes the slots nor checks the declarations.
    // D11.1, D17.11
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_overwrite"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_of_a_moved_span_frees_the_copy_and_empties_the_source, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut@ own s = new(i32, 1);\n"
                          "    del(move(s));\n    return 0;\n}\n"));
    // The move lands in a temporary, whose pointer is freed; the operand is
    // emptied and nothing is stored back into the temporary.
    // D17.8, D17.9
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp2, ptr align 8 %s.0, "
              "i64 16, i1 false)\n"
              "  call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)\n"
              "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp1, ptr align 8 %tmp2, "
              "i64 16, i1 false)\n"
              "  %t8 = getelementptr inbounds %fort.span, ptr %tmp1, i32 0, i32 0\n"
              "  %t9 = load ptr, ptr %t8, align 8\n"
              "  call void @\"std.rt.free\"(ptr %t9)\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp2, ptr align 8 %s.0, "
        "i64 16, i1 false)\n"
        "  call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)\n"
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %tmp1, ptr align 8 %tmp2, "
        "i64 16, i1 false)\n"
        "  %t8 = getelementptr inbounds %fort.span, ptr %tmp1, i32 0, i32 0\n"
        "  %t9 = load ptr, ptr %t8, align 8\n"
        "  call void @\"std.rt.free\"(ptr %t9)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the implicit move of a `return` (item 18) -------------------------------------
// D17.5

TEST(returning_an_own_local_empties_it_after_reading_it, {
    TEST_ASSERT_TRUE(emit("fn make() i32 mut* own {\n    i32 mut* own p = new(i32);\n"
                          "    return p;\n}\n"
                          "fn main() i32 {\n    i32 mut* own q = make();\n"
                          "    del(q);\n    return 0;\n}\n"));
    // The value is read, the local emptied, and only then does the function
    // return, which is what a `defer del(p)` above would see.
    // D7.8, D17.5
    TEST_ASSERT_EQ_STR(found("  %t3 = load ptr, ptr %p.0, align 8\n"
                             "  store ptr null, ptr %p.0, align 8\n"
                             "  ret ptr %t3\n"),
                       "  %t3 = load ptr, ptr %p.0, align 8\n"
                       "  store ptr null, ptr %p.0, align 8\n"
                       "  ret ptr %t3\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(returning_a_local_owning_aggregate_empties_it_after_the_sret_copy, {
    TEST_ASSERT_TRUE(emit(VEC "fn make() vec {\n    vec mut v = {};\n"
                              "    v.data = new(i32, 2);\n    return v;\n}\n"
                              "fn main() i32 {\n    vec a = make();\n"
                              "    del(a.data);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(
        found("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %ret.sret, ptr align 8 %v.0, "
              "i64 24, i1 false)\n"
              "  call void @llvm.memset.p0.i64(ptr align 8 %v.0, i8 0, i64 24, i1 false)\n"
              "  ret void\n"),
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %ret.sret, ptr align 8 %v.0, "
        "i64 24, i1 false)\n"
        "  call void @llvm.memset.p0.i64(ptr align 8 %v.0, i8 0, i64 24, i1 false)\n"
        "  ret void\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(returning_a_local_that_owns_nothing_empties_nothing, {
    TEST_ASSERT_TRUE(emit("fn pass(i32 mut* p) i32 mut* {\n    return p;\n}\n"
                          "fn main() i32 {\n    i32 mut v = 1;\n"
                          "    println(*pass(&v));\n    return 0;\n}\n"));
    // A borrowed pointer is returned as it is: no zeroing.
    // D17.5
    TEST_ASSERT_EQ_STR(absent("store ptr null"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_returned_own_rvalue_empties_nothing, {
    TEST_ASSERT_TRUE(emit("fn make(u64 n) i32 mut@ own {\n    return new(i32, n);\n}\n"
                          "fn main() i32 {\n    i32 mut@ own s = make(2);\n"
                          "    del(s);\n    return 0;\n}\n"));
    // There is no operand to empty, so the header is written into `sret` and
    // the function returns at once.
    // D17.5
    TEST_ASSERT_EQ_STR(found("  %t3 = getelementptr inbounds %fort.span, ptr %ret.sret, "
                             "i32 0, i32 1\n"
                             "  store i64 %t0, ptr %t3, align 8\n"
                             "  ret void\n"),
                       "  %t3 = getelementptr inbounds %fort.span, ptr %ret.sret, "
                       "i32 0, i32 1\n"
                       "  store i64 %t0, ptr %t3, align 8\n"
                       "  ret void\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the overwrite check (item 18) -------------------------------------------------
// D17.11

TEST(an_assignment_to_an_own_pointer_loads_compares_and_branches, {
    TEST_ASSERT_TRUE(emit("extern fn grab() i32 mut* own;\n"
                          "fn main() i32 {\n    i32 mut* own mut p = grab();\n"
                          "    p = grab();\n    del(p);\n    return 0;\n}\n"));
    // The right-hand side is evaluated first, then the check, then the store;
    // the continuation label is allocated before the failure one.
    // D19.6
    TEST_ASSERT_EQ_STR(found("  %t3 = call ptr @grab() #3\n"
                             "  %t4 = load ptr, ptr %p.0, align 8\n"
                             "  %t5 = icmp ne ptr %t4, null\n"
                             "  br i1 %t5, label %L3, label %L2\n"
                             "\nL2:\n"
                             "  store ptr %t3, ptr %p.0, align 8\n"),
                       "  %t3 = call ptr @grab() #3\n"
                       "  %t4 = load ptr, ptr %p.0, align 8\n"
                       "  %t5 = icmp ne ptr %t4, null\n"
                       "  br i1 %t5, label %L3, label %L2\n"
                       "\nL2:\n"
                       "  store ptr %t3, ptr %p.0, align 8\n");
    // The failure block calls the entry point at the `=` token and is followed
    // by `unreachable`.
    // D11.4, D19.6
    TEST_ASSERT_EQ_STR(found("\nL3:\n"
                             "  call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 4, i32 7)\n"
                             "  unreachable\n"),
                       "\nL3:\n"
                       "  call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 4, i32 7)\n"
                       "  unreachable\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_assignment_to_an_own_span_checks_the_pointer_field, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut@ own mut s = {};\n"
                          "    s = new(i32, 2);\n    del(s);\n    return 0;\n}\n"));
    // A span is produced into a place, so the value lands in a temporary and
    // the check reads field 0 of the target's header before the copy (item
    // 17, item 18).
    TEST_ASSERT_EQ_STR(found("  %t8 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  %t9 = load ptr, ptr %t8, align 8\n"
                             "  %t10 = icmp ne ptr %t9, null\n"
                             "  br i1 %t10, label %L5, label %L4\n"
                             "\nL4:\n"
                             "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %s.0, "
                             "ptr align 8 %tmp1, i64 16, i1 false)\n"),
                       "  %t8 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  %t9 = load ptr, ptr %t8, align 8\n"
                       "  %t10 = icmp ne ptr %t9, null\n"
                       "  br i1 %t10, label %L5, label %L4\n"
                       "\nL4:\n"
                       "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %s.0, "
                       "ptr align 8 %tmp1, i64 16, i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_assignment_through_an_indirection_checks_the_storage_it_reaches, {
    TEST_ASSERT_TRUE(emit("fn fill(i32 mut@ own mut* out, u64 n) void {\n"
                          "    *out = new(i32, n);\n}\n"
                          "fn main() i32 {\n    i32 mut@ own mut s = {};\n"
                          "    fill(&s, 2);\n    del(s);\n    return 0;\n}\n"));
    // The check reads the header the out-parameter points at, not the parameter
    // slot.
    // D17.11
    TEST_ASSERT_EQ_STR(found("  %t5 = getelementptr inbounds %fort.span, ptr %t0, i32 0, i32 0\n"
                             "  %t6 = load ptr, ptr %t5, align 8\n"
                             "  %t7 = icmp ne ptr %t6, null\n"),
                       "  %t5 = getelementptr inbounds %fort.span, ptr %t0, i32 0, i32 0\n"
                       "  %t6 = load ptr, ptr %t5, align 8\n"
                       "  %t7 = icmp ne ptr %t6, null\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_string_own_and_a_void_ptr_own_are_checked_by_their_own_shapes, {
    TEST_ASSERT_TRUE(emit("extern fn malloc(u64 n) void* own;\n"
                          "extern fn free(void* own p) void;\n"
                          "fn dup() string own {\n    u8 mut@ own b = new(u8, 2);\n"
                          "    return cast(move(b), string own);\n}\n"
                          "fn main() i32 {\n    string own mut s = {};\n"
                          "    s = dup();\n    del(s);\n"
                          "    void* own mut v = null;\n    v = malloc(4);\n"
                          "    free(move(v));\n    return 0;\n}\n"));
    // A `string` is a header like a span, so its check reads field 0; a `void*`
    // is a scalar, so its check loads the value itself.
    // D3.7, D3.11
    TEST_ASSERT_EQ_STR(found("  %t0 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  %t1 = load ptr, ptr %t0, align 8\n"
                             "  %t2 = icmp ne ptr %t1, null\n"
                             "  br i1 %t2, label %L1, label %L0\n"),
                       "  %t0 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  %t1 = load ptr, ptr %t0, align 8\n"
                       "  %t2 = icmp ne ptr %t1, null\n"
                       "  br i1 %t2, label %L1, label %L0\n");
    TEST_ASSERT_EQ_STR(found("  %t11 = load ptr, ptr %v.1, align 8\n"
                             "  %t12 = icmp ne ptr %t11, null\n"
                             "  br i1 %t12, label %L7, label %L6\n"),
                       "  %t11 = load ptr, ptr %v.1, align 8\n"
                       "  %t12 = icmp ne ptr %t11, null\n"
                       "  br i1 %t12, label %L7, label %L6\n");
    // `move` of a `void*` is the same load-then-null as any other pointer.
    // D17.6
    TEST_ASSERT_EQ_STR(found("  %t13 = load ptr, ptr %v.1, align 8\n"
                             "  store ptr null, ptr %v.1, align 8\n"),
                       "  %t13 = load ptr, ptr %v.1, align 8\n"
                       "  store ptr null, ptr %v.1, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_owning_locals_slot_is_zeroed_once_and_its_declaration_is_checked, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n"
                          "    for (i32 mut i = 0; i < 3; i++) {\n"
                          "        i32 mut* own p = new(i32);\n        *p = i;\n    }\n"
                          "    return 0;\n}\n"));
    // The slot is zeroed once in the entry block, so the first execution of the
    // declaration passes and the second, the body having no `del`, traps
    // instead of leaking one allocation per iteration.
    // D17.11, D19.4
    TEST_ASSERT_EQ_STR(found("entry:\n"
                             "  %i.0 = alloca i32, align 4\n"
                             "  %p.1 = alloca ptr, align 8\n"
                             "  store ptr null, ptr %p.1, align 8\n"),
                       "entry:\n"
                       "  %i.0 = alloca i32, align 4\n"
                       "  %p.1 = alloca ptr, align 8\n"
                       "  store ptr null, ptr %p.1, align 8\n");
    TEST_ASSERT_EQ_UINT64(occurrences("store ptr null, ptr %p.1"), (uint64_t)1);
    // One check, written once however often the loop runs, reported at the
    // declared name, the declaration having no operator token.
    // D11.4
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 3, i32 22)"),
                       "call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 3, i32 22)");
    TEST_ASSERT_EQ_UINT64(occurrences("@\"std.rt.fail_overwrite\"(ptr @.file"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_owning_aggregate_local_is_neither_zeroed_nor_checked, {
    TEST_ASSERT_TRUE(emit(VEC "fn main() i32 {\n"
                              "    for (i32 mut i = 0; i < 3; i++) {\n"
                              "        vec mut v = {};\n        v.len = 1;\n    }\n"
                              "    return 0;\n}\n"));
    // The overwrite check covers an `own` reference and nothing else, so an
    // owning aggregate's slot needs no zeroing of its own and its declaration
    // carries no check.
    // D17.11
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_overwrite"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_owning_aggregate_assignment_is_not_checked_field_by_field, {
    TEST_ASSERT_TRUE(emit(VEC "fn main() i32 {\n    vec mut a = {};\n"
                              "    a.data = new(i32, 2);\n    vec mut b = {};\n"
                              "    b = move(a);\n    del(b.data);\n    return 0;\n}\n"));
    // The assignment of the whole struct emits no check; the one to `a.data`
    // above it does, so exactly one is there.
    // D17.11
    TEST_ASSERT_EQ_UINT64(occurrences("@\"std.rt.fail_overwrite\"(ptr @.file"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_move_and_a_del_empty_their_operand_without_a_check, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut@ own mut s = new(i32, 2);\n"
                          "    del(s);\n    s = new(i32, 3);\n    del(s);\n"
                          "    return 0;\n}\n"));
    // Emptying is not an assignment: the `std.rt.free` is followed straight
    // by the zeroing with no branch between them, which is what makes
    // `del(v); v = new()` pass the check (item 17, item 18).
    TEST_ASSERT_EQ_STR(
        found("  call void @\"std.rt.free\"(ptr %t9)\n"
              "  call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)\n"
              "  %t10 = sext i32 3 to i64\n"),
        "  call void @\"std.rt.free\"(ptr %t9)\n"
        "  call void @llvm.memset.p0.i64(ptr align 8 %s.0, i8 0, i64 16, i1 false)\n"
        "  %t10 = sext i32 3 to i64\n");
    // One check for the declaration and one for the assignment, and none for
    // either `del`.
    // D17.11
    TEST_ASSERT_EQ_UINT64(occurrences("@\"std.rt.fail_overwrite\"(ptr @.file"), (uint64_t)2);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(release_mode_stores_over_an_owning_reference_without_a_check, {
    TEST_ASSERT_TRUE(emit_release("extern fn grab() i32 mut* own;\n"
                                  "fn main() i32 {\n    i32 mut* own mut p = grab();\n"
                                  "    p = null;\n    return 0;\n}\n"));
    // Release mode emits the plain store; the old allocation is leaked, which
    // is not tracked.
    // D11.1, D17.11, D17.14
    TEST_ASSERT_EQ_STR(found("store ptr null, ptr %p.0, align 8"),
                       "store ptr null, ptr %p.0, align 8");
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_overwrite"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(release_mode_stores_a_span_directly_with_no_temporary, {
    TEST_ASSERT_TRUE(emit_release("fn main() i32 {\n    i32 mut@ own mut s = {};\n"
                                  "    s = new(i32, 2);\n    del(s);\n    return 0;\n}\n"));
    // With no check to stand before, the header is written into the target
    // itself (item 18).
    // D19.3
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_overwrite"), "absent");
    TEST_ASSERT_EQ_STR(absent("%tmp0"), "absent");
    TEST_ASSERT_EQ_STR(found("  %t3 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                             "  store ptr %t2, ptr %t3, align 8\n"),
                       "  %t3 = getelementptr inbounds %fort.span, ptr %s.0, i32 0, i32 0\n"
                       "  store ptr %t2, ptr %t3, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(no_bounds_check_keeps_the_overwrite_check, {
    TEST_ASSERT_TRUE(emit_unchecked("extern fn grab() i32 mut* own;\n"
                                    "fn main() i32 {\n    i32 mut* own mut p = grab();\n"
                                    "    p = grab();\n    del(p);\n    return 0;\n}\n"));
    // `--no-bounds-check` removes the index and span branches and nothing else,
    // so the overwrite check stands.
    // D10.6, D17.11
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 4, i32 7)"),
                       "call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 4, i32 7)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_assignment_to_an_owned_slot_checks_the_element_it_reaches, {
    TEST_ASSERT_TRUE(emit("struct node {\n    i32 value;\n}\n"
                          "fn main() i32 {\n"
                          "    node mut* own mut@ own kids = new(node mut* own, 2);\n"
                          "    kids[0] = new(node);\n    del(kids[0]);\n    del(kids);\n"
                          "    return 0;\n}\n"));
    // The element address is computed first, which is the source order, then
    // the right-hand side runs, then the check reads that address and the store
    // follows it (item 18).
    // D6.3
    TEST_ASSERT_EQ_STR(found("  %t14 = getelementptr inbounds ptr, ptr %t13, i64 %t8\n"
                             "  %t15 = call ptr @\"std.rt.alloc\"(i64 4, i64 1, ptr @.file.0, "
                             "i32 6, i32 15)\n"
                             "  %t16 = load ptr, ptr %t14, align 8\n"
                             "  %t17 = icmp ne ptr %t16, null\n"
                             "  br i1 %t17, label %L7, label %L6\n"
                             "\nL6:\n"
                             "  store ptr %t15, ptr %t14, align 8\n"),
                       "  %t14 = getelementptr inbounds ptr, ptr %t13, i64 %t8\n"
                       "  %t15 = call ptr @\"std.rt.alloc\"(i64 4, i64 1, ptr @.file.0, "
                       "i32 6, i32 15)\n"
                       "  %t16 = load ptr, ptr %t14, align 8\n"
                       "  %t17 = icmp ne ptr %t16, null\n"
                       "  br i1 %t17, label %L7, label %L6\n"
                       "\nL6:\n"
                       "  store ptr %t15, ptr %t14, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_move_into_its_own_operand_leaves_the_value_where_it_was, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut* own mut p = new(i32);\n"
                          "    p = move(p);\n    del(p);\n    return 0;\n}\n"));
    // The move reads `p` and empties it, so the check that follows sees the
    // zero value and passes, and the store puts the value back.
    // D17.6, D17.11
    TEST_ASSERT_EQ_STR(found("  %t3 = load ptr, ptr %p.0, align 8\n"
                             "  store ptr null, ptr %p.0, align 8\n"
                             "  %t4 = load ptr, ptr %p.0, align 8\n"
                             "  %t5 = icmp ne ptr %t4, null\n"
                             "  br i1 %t5, label %L3, label %L2\n"
                             "\nL2:\n"
                             "  store ptr %t3, ptr %p.0, align 8\n"),
                       "  %t3 = load ptr, ptr %p.0, align 8\n"
                       "  store ptr null, ptr %p.0, align 8\n"
                       "  %t4 = load ptr, ptr %p.0, align 8\n"
                       "  %t5 = icmp ne ptr %t4, null\n"
                       "  br i1 %t5, label %L3, label %L2\n"
                       "\nL2:\n"
                       "  store ptr %t3, ptr %p.0, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_move_and_a_check_inside_a_loop_are_emitted_once, {
    TEST_ASSERT_TRUE(emit("struct node {\n    i32 value;\n    node mut* own next;\n}\n"
                          "fn main() i32 {\n    node mut* own mut head = null;\n"
                          "    for (i32 mut i = 0; i < 3; i++) {\n"
                          "        node mut* own n = new(node);\n"
                          "        n->next = move(head);\n        head = move(n);\n    }\n"
                          "    while (head != null) {\n"
                          "        node mut* own next = move(head->next);\n"
                          "        del(head);\n        head = move(next);\n    }\n"
                          "    return 0;\n}\n"));
    // One check per store into an `own` reference -- three assignments and
    // three declarations -- written once each however often the loop runs.
    // D17.11
    TEST_ASSERT_EQ_UINT64(occurrences("@\"std.rt.fail_overwrite\"(ptr @.file"), (uint64_t)6);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_failure_blocks_of_several_checks_stay_in_ascending_label_order, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut@ own mut s = {};\n"
                          "    s = new(i32, 2);\n    del(s);\n"
                          "    s = new(i32, 3);\n    del(s);\n    return 0;\n}\n"));
    // The failure buffer is appended after every normal block, in the order the
    // labels were allocated.
    // D19.5, D19.6
    TEST_ASSERT_TRUE(before("\nL1:\n", "\nL5:\n") && before("\nL5:\n", "\nL9:\n"));
    TEST_ASSERT_EQ_UINT64(occurrences("@\"std.rt.fail_overwrite\"(ptr @.file"), (uint64_t)3);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

// ---- what the emitter never sees ---------------------------------------------------
// D17.8

// `move(x);` as a statement: gen_builtin_call carries a path for it, and no
// program reaches that path. The checker leaves a `move` five ways before it
// succeeds: the arity, an operand that is not an lvalue, one that is not owning
// and one it cannot empty each report, and a poisoned operand was reported
// where it went wrong. What is left is the success path, which types the call
// as an owning rvalue, and a discarded owning rvalue is a leaking temporary.
// The two below are the cases a well-formed `move` reaches; check_own_test.c
// holds the others.
// D17.8
TEST(a_discarded_move_never_reaches_the_emitter, {
    // The whole sentence and not its first clause: the conversion path of
    // check.c reports the same prefix with another tail.
    TEST_ASSERT_FALSE(emit("fn main() i32 {\n    i32 mut* own p = new(i32);\n"
                           "    move(p);\n    del(p);\n    return 0;\n}\n"));
    TEST_ASSERT_NONNULL(strstr(gen_said(),
                               "owning temporary would leak: bind it to an 'own' place, "
                               "pass it on or 'del' it"));
    TEST_ASSERT_FALSE(emit("fn main() i32 {\n    i32 mut x = 1;\n"
                           "    move(x);\n    return 0;\n}\n"));
    TEST_ASSERT_NONNULL(strstr(gen_said(), "'move' needs an owning operand, not i32"));
})

// `std.rt.alloc`'s shape, twice: an extern that answers a block, a function
// that answers it `own`, and a caller that binds it, reads through it and
// releases it. The second source names `void mut*` where the first names
// `void*`, and nothing else differs. The caller reads and does not write,
// because a cast of the first block to a writable span would add the mark, and
// the two texts must differ in the mark alone. Each declared local stands on a
// line of its own, because the overwrite check of item 18 records the column of the
// name it guards and `void mut*` is four characters wider; with a name on the
// wider line the two texts would differ in two `fail_overwrite` columns and in
// nothing else. A function name needs no such line, because the result comes
// after the parameter list and cannot move the name.
// D3.11, D8.1, D17.11
static const char VOID_PTR_ALLOC[] = "extern fn take(u64 n, u64 size) void* own;\n"
                                     "extern fn drop(void* own p) void;\n"
                                     "fn grab(u64 n) void* own {\n"
                                     "    void* own\n"
                                     "        p = take(1, n);\n"
                                     "    return p;\n"
                                     "}\n"
                                     "fn main() i32 {\n"
                                     "    void* own\n"
                                     "        b = grab(8);\n"
                                     "    u8@ v = cast(b, u8*)[0..8];\n"
                                     "    println(v.len);\n"
                                     "    println(v[0]);\n"
                                     "    drop(move(b));\n"
                                     "    return 0;\n"
                                     "}\n";

static const char VOID_MUT_PTR_ALLOC[] = "extern fn take(u64 n, u64 size) void mut* own;\n"
                                         "extern fn drop(void* own p) void;\n"
                                         "fn grab(u64 n) void mut* own {\n"
                                         "    void mut* own\n"
                                         "        p = take(1, n);\n"
                                         "    return p;\n"
                                         "}\n"
                                         "fn main() i32 {\n"
                                         "    void mut* own\n"
                                         "        b = grab(8);\n"
                                         "    u8@ v = cast(b, u8*)[0..8];\n"
                                         "    println(v.len);\n"
                                         "    println(v[0]);\n"
                                         "    drop(move(b));\n"
                                         "    return 0;\n"
                                         "}\n";

TEST(the_allocator_shape_emits_one_text_with_and_without_the_mark, {
    // `std.rt.alloc` answers `void mut* own` and answered `void* own`. The
    // retyping is a rule of the type system and reaches no instruction, no
    // signature and no size, every pointer being one machine word, so the two
    // modules are one text byte for byte. A stronger argument for it exists --
    // `grep -- '->mut' src/bootstrap/gen*.c` returns one hit, the emitter's own
    // synthetic node, so the mark structurally cannot reach the IR -- and an
    // argument is not a test.
    // D3.1, D3.11, D19.2
    TEST_ASSERT_TRUE(strcmp(VOID_PTR_ALLOC, VOID_MUT_PTR_ALLOC) != 0);
    TEST_ASSERT_TRUE(emit(VOID_PTR_ALLOC));
    static char plain[32768];
    TEST_ASSERT_TRUE(strlen(ir()) < sizeof plain);
    TEST_UNUSED(snprintf(plain, sizeof plain, "%s", ir()));
    // The three records that carry a type or a source column of the allocator:
    // the call it makes, and the overwrite check of each `own` binding.
    static const char TAKE_CALL[] = "  %t1 = call ptr @take(i64 1, i64 %t0) #3\n";
    static const char GRAB_CHECK[] =
        "  call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 5, i32 9)\n";
    static const char MAIN_CHECK[] =
        "  call void @\"std.rt.fail_overwrite\"(ptr @.file.0, i32 10, i32 9)\n";
    TEST_ASSERT_EQ_STR(found(TAKE_CALL), TAKE_CALL);
    TEST_ASSERT_EQ_STR(found(GRAB_CHECK), GRAB_CHECK);
    TEST_ASSERT_EQ_STR(found(MAIN_CHECK), MAIN_CHECK);
    TEST_ASSERT_EQ_STR(verified(), "verified");
    TEST_ASSERT_TRUE(emit(VOID_MUT_PTR_ALLOC));
    TEST_ASSERT_EQ_STR(ir(), plain);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

int main(int argc, char** argv) {
    TEST_INIT("gen_own", argc, argv);
    TEST_RUN(move_of_a_pointer_loads_before_it_stores_null);
    TEST_RUN(move_of_a_span_copies_the_header_and_zeroes_it);
    TEST_RUN(move_of_an_owning_aggregate_copies_the_whole_value);
    TEST_RUN(move_out_of_a_field_empties_that_field_alone);
    TEST_RUN(move_is_the_same_in_both_build_modes);
    TEST_RUN(del_of_a_moved_span_frees_the_copy_and_empties_the_source);
    TEST_RUN(returning_an_own_local_empties_it_after_reading_it);
    TEST_RUN(returning_a_local_owning_aggregate_empties_it_after_the_sret_copy);
    TEST_RUN(returning_a_local_that_owns_nothing_empties_nothing);
    TEST_RUN(a_returned_own_rvalue_empties_nothing);
    TEST_RUN(an_assignment_to_an_own_pointer_loads_compares_and_branches);
    TEST_RUN(an_assignment_to_an_own_span_checks_the_pointer_field);
    TEST_RUN(an_assignment_through_an_indirection_checks_the_storage_it_reaches);
    TEST_RUN(a_string_own_and_a_void_ptr_own_are_checked_by_their_own_shapes);
    TEST_RUN(an_owning_locals_slot_is_zeroed_once_and_its_declaration_is_checked);
    TEST_RUN(an_owning_aggregate_local_is_neither_zeroed_nor_checked);
    TEST_RUN(an_owning_aggregate_assignment_is_not_checked_field_by_field);
    TEST_RUN(a_move_and_a_del_empty_their_operand_without_a_check);
    TEST_RUN(release_mode_stores_over_an_owning_reference_without_a_check);
    TEST_RUN(release_mode_stores_a_span_directly_with_no_temporary);
    TEST_RUN(no_bounds_check_keeps_the_overwrite_check);
    TEST_RUN(an_assignment_to_an_owned_slot_checks_the_element_it_reaches);
    TEST_RUN(a_move_into_its_own_operand_leaves_the_value_where_it_was);
    TEST_RUN(a_move_and_a_check_inside_a_loop_are_emitted_once);
    TEST_RUN(the_failure_blocks_of_several_checks_stay_in_ascending_label_order);
    TEST_RUN(a_discarded_move_never_reaches_the_emitter);
    TEST_RUN(the_allocator_shape_emits_one_text_with_and_without_the_mark);
    gen_done();
    TEST_EXIT();
}
