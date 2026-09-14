// Unit tests of `new` and `del` (toolchain.md 6 item 17): the call to
// std.rt.alloc with the element size and the count, the negative-count check,
// the span header the counted form writes, and the free that empties an lvalue
// operand and leaves an rvalue alone.
// D10.2, D10.3, D17.3, D17.9
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers)
// The literals below are the test data: the element sizes, counts, columns
// and temporary numbers of the module each program emits.

// A struct wider than the two words every aggregate assertion used before it,
// so that a size the emitter gets wrong only above 16 bytes is seen. Its 28
// bytes of fields are 32 bytes of storage, C/System V layout rounding the size
// up to the alignment.
// T-019, D3.8: the ticket that widened the aggregate assertions
#define WIDE "struct wide {\n    i64 a;\n    i64 b;\n    i64 c;\n    i32 d;\n}\n"

// ---- new(T) ------------------------------------------------------------------------
// D10.2, D17.3

TEST(new_of_one_object_is_the_element_size_and_a_count_of_one, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut* own p = new(i32);\n"
                          "    println(*p);\n    del(p);\n    return 0;\n}\n"));
    // The column of the check is the builtin's name. The declaration carries
    // the overwrite check, so the store stands in its continuation block (item
    // 18).
    // D11.4, D17.11
    TEST_ASSERT_EQ_STR(found("  %t0 = call ptr @\"std.rt.alloc\"(i64 4, i64 1, ptr @.file.0, "
                             "i32 2, i32 22)\n"
                             "  %t1 = load ptr, ptr %p.0, align 8\n"
                             "  %t2 = icmp ne ptr %t1, null\n"
                             "  br i1 %t2, label %L1, label %L0\n"
                             "\nL0:\n  store ptr %t0, ptr %p.0, align 8\n"),
                       "  %t0 = call ptr @\"std.rt.alloc\"(i64 4, i64 1, ptr @.file.0, "
                       "i32 2, i32 22)\n"
                       "  %t1 = load ptr, ptr %p.0, align 8\n"
                       "  %t2 = icmp ne ptr %t1, null\n"
                       "  br i1 %t2, label %L1, label %L0\n"
                       "\nL0:\n  store ptr %t0, ptr %p.0, align 8\n");
    // The runtime zeroes the storage, so the module writes nothing into it;
    // the slot of the owning local is a pointer, so its entry-block zeroing is
    // a `store ptr null` and not a memset.
    // D10.2, D17.11
    TEST_ASSERT_EQ_STR(absent("llvm.memset"), "absent");
    TEST_ASSERT_EQ_STR(found("entry:\n  %p.0 = alloca ptr, align 8\n"
                             "  store ptr null, ptr %p.0, align 8\n"),
                       "entry:\n  %p.0 = alloca ptr, align 8\n"
                       "  store ptr null, ptr %p.0, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(new_of_a_struct_asks_for_its_whole_size, {
    TEST_ASSERT_TRUE(emit("struct point {\n    i32 x;\n    i32 y;\n}\n"
                          "fn main() i32 {\n    point mut* own p = new(point);\n"
                          "    println(p->x);\n    del(p);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\"(i64 8, i64 1, "),
                       "call ptr @\"std.rt.alloc\"(i64 8, i64 1, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(new_of_a_wide_struct_asks_for_its_padded_size, {
    TEST_ASSERT_TRUE(emit(WIDE "fn main() i32 {\n    wide mut* own p = new(wide);\n"
                               "    println(p->d);\n    del(p);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\"(i64 32, i64 1, "),
                       "call ptr @\"std.rt.alloc\"(i64 32, i64 1, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(new_of_an_array_allocates_one_array_object, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u8[4] mut* own p = new(u8[4]);\n"
                          "    println((*p)[0]);\n    del(p);\n    return 0;\n}\n"));
    // `new(u8[4])` is one `u8[4]`, not four `u8`.
    // D10.2
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\"(i64 4, i64 1, "),
                       "call ptr @\"std.rt.alloc\"(i64 4, i64 1, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(new_of_a_pointer_allocates_one_slot, {
    TEST_ASSERT_TRUE(emit("struct node {\n    i32 value;\n}\n"
                          "fn main() i32 {\n    node mut* mut* own pp = new(node mut*);\n"
                          "    println(*pp == null);\n    del(pp);\n    return 0;\n}\n"));
    // `new(T*)` allocates one pointer slot and is legal.
    // D10.2, D17.3
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\"(i64 8, i64 1, "),
                       "call ptr @\"std.rt.alloc\"(i64 8, i64 1, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- new(T, n) ---------------------------------------------------------------------
// D10.2, D17.3

TEST(a_counted_new_writes_the_header_field_by_field, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u64 n = 3;\n"
                          "    i32 mut@ own s = new(i32, n);\n"
                          "    println(s.len);\n    del(s);\n    return 0;\n}\n"));
    // An unsigned count takes no branch, and the header is the pointer then
    // the length (item 17).
    // The declaration is checked, so the header is built in the temporary of
    // item 18 and copied into the slot after the check.
    TEST_ASSERT_EQ_STR(
        found("  %t0 = load i64, ptr %n.0, align 8\n"
              "  %t1 = call ptr @\"std.rt.alloc\"(i64 4, i64 %t0, ptr @.file.0, i32 3, i32 22)\n"
              "  %t2 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
              "  store ptr %t1, ptr %t2, align 8\n"
              "  %t3 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 1\n"
              "  store i64 %t0, ptr %t3, align 8\n"),
        "  %t0 = load i64, ptr %n.0, align 8\n"
        "  %t1 = call ptr @\"std.rt.alloc\"(i64 4, i64 %t0, ptr @.file.0, i32 3, i32 22)\n"
        "  %t2 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
        "  store ptr %t1, ptr %t2, align 8\n"
        "  %t3 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 1\n"
        "  store i64 %t0, ptr %t3, align 8\n");
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_alloc_count"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_signed_count_is_checked_against_zero, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 n = 3;\n"
                          "    i32 mut@ own s = new(i32, n);\n"
                          "    println(s.len);\n    del(s);\n    return 0;\n}\n"));
    // A negative count is a runtime error reported with its signed value.
    // D10.2, D11.4
    TEST_ASSERT_EQ_STR(found("  %t0 = load i32, ptr %n.0, align 4\n"
                             "  %t1 = sext i32 %t0 to i64\n"
                             "  %t2 = icmp slt i64 %t1, 0\n"
                             "  br i1 %t2, label %L1, label %L0\n"),
                       "  %t0 = load i32, ptr %n.0, align 4\n"
                       "  %t1 = sext i32 %t0 to i64\n"
                       "  %t2 = icmp slt i64 %t1, 0\n"
                       "  br i1 %t2, label %L1, label %L0\n");
    TEST_ASSERT_EQ_STR(
        found("\nL1:\n  call void @\"std.rt.fail_alloc_count\"(i64 %t1, ptr @.file.0, "
              "i32 3, i32 22)\n  unreachable\n"),
        "\nL1:\n  call void @\"std.rt.fail_alloc_count\"(i64 %t1, ptr @.file.0, "
        "i32 3, i32 22)\n  unreachable\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_narrow_count_is_extended_by_its_own_signedness, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u8 n = 3;\n"
                          "    i32 mut@ own s = new(i32, n);\n"
                          "    println(s.len);\n    del(s);\n    return 0;\n}\n"));
    // An unsigned count is zero-extended and never negative (item 16).
    TEST_ASSERT_EQ_STR(found("zext i8 %t0 to i64"), "zext i8 %t0 to i64");
    TEST_ASSERT_EQ_STR(absent("std.rt.fail_alloc_count"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_counted_new_of_a_wide_element_asks_for_its_stride, {
    TEST_ASSERT_TRUE(emit(WIDE "fn main() i32 {\n    u64 n = 2;\n"
                               "    wide mut@ own s = new(wide, n);\n"
                               "    println(s.len);\n    del(s);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\"(i64 32, i64 %t0, "),
                       "call ptr @\"std.rt.alloc\"(i64 32, i64 %t0, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_count_of_zero_is_allocated_like_any_other, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u64 n = 0;\n"
                          "    i32 mut@ own s = new(i32, n);\n"
                          "    println(s.len, s.ptr != null);\n    del(s);\n    return 0;\n}\n"));
    // `n == 0` is allowed and yields a non-null pointer, which the runtime
    // guarantees, so the module tests nothing.
    // D10.2
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\"(i64 4, i64 %t0, "),
                       "call ptr @\"std.rt.alloc\"(i64 4, i64 %t0, ");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_allocation_count_check_survives_both_switches, {
    static const char PROGRAM[] = "fn main() i32 {\n    i32 n = 3;\n"
                                  "    i32 mut@ own s = new(i32, n);\n"
                                  "    println(s.len);\n    del(s);\n    return 0;\n}\n";
    // A negative count is a runtime error of the new builtin and not a bounds
    // check, so neither `--release` nor `--no-bounds-check` removes it.
    // D10.2, D10.6, D11.1
    TEST_ASSERT_TRUE(emit_release(PROGRAM));
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_alloc_count\""), "@\"std.rt.fail_alloc_count\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
    TEST_ASSERT_TRUE(emit_unchecked(PROGRAM));
    TEST_ASSERT_EQ_STR(found("@\"std.rt.fail_alloc_count\""), "@\"std.rt.fail_alloc_count\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- del ---------------------------------------------------------------------------
// D10.3, D17.9

TEST(del_of_a_pointer_lvalue_frees_it_and_stores_null, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    i32 mut* own p = new(i32);\n"
                          "    del(p);\n    println(p == null);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("  %t3 = load ptr, ptr %p.0, align 8\n"
                             "  call void @\"std.rt.free\"(ptr %t3)\n"
                             "  store ptr null, ptr %p.0, align 8\n"),
                       "  %t3 = load ptr, ptr %p.0, align 8\n"
                       "  call void @\"std.rt.free\"(ptr %t3)\n"
                       "  store ptr null, ptr %p.0, align 8\n");
    // Nothing declares the entry point: `std.rt` is in the closure, so the
    // module that holds the call holds the definition (item 8).
    TEST_ASSERT_EQ_STR(absent("declare void @\"std.rt.free\""), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_of_a_span_lvalue_frees_the_pointer_field_and_zeroes_the_header, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    u64 n = 3;\n"
                          "    i32 mut@ own s = new(i32, n);\n"
                          "    del(s);\n    println(s.len);\n    return 0;\n}\n"));
    // The pointer is field 0, and the whole 16-byte header is zeroed (item
    // 17).
    // D17.9
    TEST_ASSERT_EQ_STR(
        found("  %t7 = getelementptr inbounds %fort.span, ptr %s.1, i32 0, i32 0\n"
              "  %t8 = load ptr, ptr %t7, align 8\n"
              "  call void @\"std.rt.free\"(ptr %t8)\n"
              "  call void @llvm.memset.p0.i64(ptr align 8 %s.1, i8 0, i64 16, i1 false)\n"),
        "  %t7 = getelementptr inbounds %fort.span, ptr %s.1, i32 0, i32 0\n"
        "  %t8 = load ptr, ptr %t7, align 8\n"
        "  call void @\"std.rt.free\"(ptr %t8)\n"
        "  call void @llvm.memset.p0.i64(ptr align 8 %s.1, i8 0, i64 16, i1 false)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_of_an_rvalue_frees_and_stores_nothing, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    del(new(i32));\n    return 0;\n}\n"));
    // An `own` rvalue may be `del`ed and has no place to empty; the uncounted
    // `new` is a scalar, so it needs no place at all.
    // D17.8, D17.9
    TEST_ASSERT_EQ_STR(found("  %t0 = call ptr @\"std.rt.alloc\"(i64 4, i64 1, ptr @.file.0, "
                             "i32 2, i32 9)\n  call void @\"std.rt.free\"(ptr %t0)\n  ret i32 0\n"),
                       "  %t0 = call ptr @\"std.rt.alloc\"(i64 4, i64 1, ptr @.file.0, "
                       "i32 2, i32 9)\n  call void @\"std.rt.free\"(ptr %t0)\n  ret i32 0\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_of_a_counted_new_frees_the_allocation_it_just_made, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    del(new(u8, 4));\n    return 0;\n}\n"));
    // An `own` rvalue may be bound, passed to an `own` parameter or `del`ed,
    // so `new(T, n)` has a place like any other aggregate rvalue: its header
    // lands in a temporary whose pointer is freed, and nothing is stored back.
    // D17.8, D17.9
    TEST_ASSERT_EQ_STR(found("  %t2 = call ptr @\"std.rt.alloc\"(i64 1, i64 %t0, ptr @.file.0, "
                             "i32 2, i32 9)\n"
                             "  %t3 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
                             "  store ptr %t2, ptr %t3, align 8\n"
                             "  %t4 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 1\n"
                             "  store i64 %t0, ptr %t4, align 8\n"
                             "  %t5 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
                             "  %t6 = load ptr, ptr %t5, align 8\n"
                             "  call void @\"std.rt.free\"(ptr %t6)\n"),
                       "  %t2 = call ptr @\"std.rt.alloc\"(i64 1, i64 %t0, ptr @.file.0, "
                       "i32 2, i32 9)\n"
                       "  %t3 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
                       "  store ptr %t2, ptr %t3, align 8\n"
                       "  %t4 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 1\n"
                       "  store i64 %t0, ptr %t4, align 8\n"
                       "  %t5 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
                       "  %t6 = load ptr, ptr %t5, align 8\n"
                       "  call void @\"std.rt.free\"(ptr %t6)\n");
    TEST_ASSERT_EQ_STR(absent("llvm.memset"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_of_a_span_rvalue_frees_the_pointer_of_its_temporary, {
    TEST_ASSERT_TRUE(emit("fn make(u64 n) u8 mut@ own {\n    return new(u8, n);\n}\n"
                          "fn main() i32 {\n    del(make(3));\n    return 0;\n}\n"));
    // An aggregate result arrives in a place the caller makes (item 7), so
    // `del` of one reads field 0 of that temporary and empties nothing.
    // D17.9
    TEST_ASSERT_EQ_STR(found("  call void @\"main.make\"(ptr %tmp0, i64 3)\n"
                             "  %t0 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
                             "  %t1 = load ptr, ptr %t0, align 8\n"
                             "  call void @\"std.rt.free\"(ptr %t1)\n  ret i32 0\n"),
                       "  call void @\"main.make\"(ptr %tmp0, i64 3)\n"
                       "  %t0 = getelementptr inbounds %fort.span, ptr %tmp0, i32 0, i32 0\n"
                       "  %t1 = load ptr, ptr %t0, align 8\n"
                       "  call void @\"std.rt.free\"(ptr %t1)\n  ret i32 0\n");
    TEST_ASSERT_EQ_STR(absent("llvm.memset"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_of_a_null_literal_is_one_call_with_null, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    del(null);\n    return 0;\n}\n"));
    // `del(null)` is a no-op the runtime absorbs.
    // D17.9
    TEST_ASSERT_EQ_STR(found("  call void @\"std.rt.free\"(ptr null)\n"),
                       "  call void @\"std.rt.free\"(ptr null)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_through_an_indirection_empties_the_storage_it_reaches, {
    TEST_ASSERT_TRUE(emit("fn drop(i32 mut* own mut* p) void {\n    del(*p);\n}\n"
                          "fn main() i32 {\n    i32 mut* own mut q = new(i32);\n"
                          "    drop(&q);\n    println(q == null);\n    return 0;\n}\n"));
    // `*p` designates the storage the pointer reaches, so the store empties
    // the caller's slot.
    // D6.7, D17.9
    TEST_ASSERT_EQ_STR(found("  %t0 = load ptr, ptr %p.0, align 8\n"
                             "  %t1 = load ptr, ptr %t0, align 8\n"
                             "  call void @\"std.rt.free\"(ptr %t1)\n"
                             "  store ptr null, ptr %t0, align 8\n"),
                       "  %t0 = load ptr, ptr %p.0, align 8\n"
                       "  %t1 = load ptr, ptr %t0, align 8\n"
                       "  call void @\"std.rt.free\"(ptr %t1)\n"
                       "  store ptr null, ptr %t0, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_of_a_field_empties_that_field_alone, {
    TEST_ASSERT_TRUE(emit("struct box {\n    i32 mut* own p;\n    i32 tag;\n}\n"
                          "fn main() i32 {\n    box mut b = {new(i32), 7};\n"
                          "    del(b.p);\n"
                          "    println(b.p == null, b.tag);\n    return 0;\n}\n"));
    // An `own` rvalue flows into an `own` field of a literal with no `move`,
    // and `del` empties that field alone.
    // D17.5, D17.9
    TEST_ASSERT_EQ_STR(found("  %t4 = load ptr, ptr %t3, align 8\n"
                             "  call void @\"std.rt.free\"(ptr %t4)\n"
                             "  store ptr null, ptr %t3, align 8\n"),
                       "  %t4 = load ptr, ptr %t3, align 8\n"
                       "  call void @\"std.rt.free\"(ptr %t4)\n"
                       "  store ptr null, ptr %t3, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(del_is_the_same_in_both_build_modes, {
    static const char PROGRAM[] = "fn main() i32 {\n    i32 mut* own p = new(i32);\n"
                                  "    del(p);\n    return 0;\n}\n";
    // `del` empties its operand in both build modes (item 18).
    // D17.6
    TEST_ASSERT_TRUE(emit_release(PROGRAM));
    TEST_ASSERT_EQ_STR(found("  call void @\"std.rt.free\"(ptr %t1)\n"
                             "  store ptr null, ptr %p.0, align 8\n"),
                       "  call void @\"std.rt.free\"(ptr %t1)\n"
                       "  store ptr null, ptr %p.0, align 8\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_alloc", argc, argv);
    TEST_RUN(new_of_one_object_is_the_element_size_and_a_count_of_one);
    TEST_RUN(new_of_a_struct_asks_for_its_whole_size);
    TEST_RUN(new_of_a_wide_struct_asks_for_its_padded_size);
    TEST_RUN(new_of_an_array_allocates_one_array_object);
    TEST_RUN(new_of_a_pointer_allocates_one_slot);
    TEST_RUN(a_counted_new_writes_the_header_field_by_field);
    TEST_RUN(a_signed_count_is_checked_against_zero);
    TEST_RUN(a_narrow_count_is_extended_by_its_own_signedness);
    TEST_RUN(a_counted_new_of_a_wide_element_asks_for_its_stride);
    TEST_RUN(a_count_of_zero_is_allocated_like_any_other);
    TEST_RUN(the_allocation_count_check_survives_both_switches);
    TEST_RUN(del_of_a_pointer_lvalue_frees_it_and_stores_null);
    TEST_RUN(del_of_a_span_lvalue_frees_the_pointer_field_and_zeroes_the_header);
    TEST_RUN(del_of_an_rvalue_frees_and_stores_nothing);
    TEST_RUN(del_of_a_counted_new_frees_the_allocation_it_just_made);
    TEST_RUN(del_of_a_span_rvalue_frees_the_pointer_of_its_temporary);
    TEST_RUN(del_of_a_null_literal_is_one_call_with_null);
    TEST_RUN(del_through_an_indirection_empties_the_storage_it_reaches);
    TEST_RUN(del_of_a_field_empties_that_field_alone);
    TEST_RUN(del_is_the_same_in_both_build_modes);
    gen_done();
    TEST_EXIT();
}
