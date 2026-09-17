// Unit tests of the calls the emitter writes (toolchain.md 6 items 7, 8, 10
// and 20): the signature of a definition, the call of a name and the call of
// a function pointer, the aggregate convention on both sides of the boundary,
// the extension attributes of narrow parameters and results, recursion,
// `noreturn` and the identity of a function pointer.
// D3.10, D3.13, D6.11, D8.2, D8.5, D9.9, D19.7
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen.h"
#include "gen_helpers.h"
#include "str.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the literals are the test data: the
// fort programs and the IR text each one must produce.

// A program whose every call form appears: a name, a recursive name, a
// pointer of every shape and an aggregate in both directions. The structural
// tests answer about the whole module rather than about one snippet.
static const char BUSY_SOURCE[] =
    "struct point { i32 x; i32 y; }\n"
    "struct slot { fn (i32, i32) i32 f; }\n"
    "fn add(i32 a, i32 b) i32 { return a +% b; }\n"
    "fn depth(i32 n) i32 { if (n == 0) { return 0; } return depth(n -% 1) +% 1; }\n"
    "fn grow(point p, i32 by) point { point q = {p.x +% by, p.y +% by}; return q; }\n"
    "fn narrow(i8 a, u16 b, bool c, char d) i8 { if (c) { return a; } return cast(d, i8); }\n"
    "fn stop(string m) noreturn { panic(m); }\n"
    "fn choose(bool p) fn (i32, i32) i32 { if (p) { return add; } return add; }\n"
    "fn main() i32 {\n"
    "    fn (i32, i32) i32 op = add;\n"
    "    fn (point, i32) point g = grow;\n"
    "    fn (i8, u16, bool, char) i8 n = narrow;\n"
    "    fn (string) noreturn f = stop;\n"
    "    slot[2] ops = {{add}, {add}};\n"
    "    point p = {1, 2};\n"
    "    point q = g(p, depth(3));\n"
    "    println(op(q.x, q.y), \" \", cast(n(1, 2, true, 'x'), i32), \" \", choose(true)(1, 2));\n"
    "    println(ops[1].f(q.x, q.y));\n"
    "    println(op == add, \" \", f == null);\n"
    "    f(\"bye\");\n"
    "}\n";

// ---- a function name as a value ----------------------------------------------------
// D3.10

TEST(a_function_name_used_as_a_value_is_its_address, {
    TEST_ASSERT_TRUE(emit("fn five() i32 { return 5; }\n"
                          "fn main() i32 { fn () i32 f = five; return f(); }\n"));
    // The name denotes the function's address and has no storage to load
    // from, so nothing is dereferenced to reach it.
    // D3.10
    TEST_ASSERT_EQ_STR(found("store ptr @\"main.five\", ptr %f.0, align 8"),
                       "store ptr @\"main.five\", ptr %f.0, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_function_pointer_local_is_a_pointer_sized_slot, {
    TEST_ASSERT_TRUE(emit("fn five() i32 { return 5; }\n"
                          "fn main() i32 { fn () i32 f = five; return f(); }\n"));
    // A function pointer is an ordinary `ptr` value (item 2).
    // D3.10
    TEST_ASSERT_EQ_STR(found("%f.0 = alloca ptr, align 8"), "%f.0 = alloca ptr, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// A function type carries no suffix of its own, so an array of function
// pointers is an array of a struct that holds one.
// D8.1
TEST(a_function_name_in_an_array_literal_is_stored_element_by_element, {
    TEST_ASSERT_TRUE(emit("struct slot { fn (i32) i32 f; }\n"
                          "fn a(i32 x) i32 { return x; }\n"
                          "fn b(i32 x) i32 { return x; }\n"
                          "fn main() i32 { slot[2] t = {{a}, {b}}; return t[0].f(1); }\n"));
    const char* want =
        "  %t0 = getelementptr inbounds [2 x %struct.main.slot], ptr %t.0, i64 0, i64 0\n"
        "  %t1 = getelementptr inbounds %struct.main.slot, ptr %t0, i32 0, i32 0\n"
        "  store ptr @\"main.a\", ptr %t1, align 8\n"
        "  %t2 = getelementptr inbounds [2 x %struct.main.slot], ptr %t.0, i64 0, i64 1\n"
        "  %t3 = getelementptr inbounds %struct.main.slot, ptr %t2, i32 0, i32 0\n"
        "  store ptr @\"main.b\", ptr %t3, align 8\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_null_function_pointer_is_the_null_constant, {
    TEST_ASSERT_TRUE(emit("fn main() i32 { fn () void f = null; return 0; }\n"));
    // `null` is a valid function-pointer value.
    // D3.10
    TEST_ASSERT_EQ_STR(found("store ptr null, ptr %f.0, align 8"),
                       "store ptr null, ptr %f.0, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_extern_reaches_a_function_pointer_through_a_fort_wrapper, {
    TEST_ASSERT_TRUE(emit("extern fn abs(i32 n) i32;\n"
                          "fn magnitude(i32 n) i32 { return abs(n); }\n"
                          "fn main() i32 { fn (i32) i32 f = magnitude; return f(-1); }\n"));
    // An `extern fn` in value position is a checker error, so the pointer is
    // the wrapper's and the extern is still called through the fixed type
    // its declaration supplies (item 8).
    // D3.10, D9.8
    TEST_ASSERT_EQ_STR(found("declare i32 @abs(i32) nobuiltin\n"),
                       "declare i32 @abs(i32) nobuiltin\n");
    TEST_ASSERT_EQ_STR(absent("ptr @abs"), "absent");
    TEST_ASSERT_EQ_STR(found("call i32 @abs(i32 %t0) #3"), "call i32 @abs(i32 %t0) #3");
    TEST_ASSERT_EQ_STR(found("store ptr @\"main.magnitude\", ptr %f.0, align 8"),
                       "store ptr @\"main.magnitude\", ptr %f.0, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_qualified_function_name_of_another_module_is_its_address, {
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import util;\n"
                              "fn main() i32 { fn (i32) i32 f = util.twice; return f(2); }\n",
                              "util.ft",
                              "fn twice(i32 x) i32 { return x +% x; }\n"));
    // A qualified name `m.f` is the function's address and not a field of a
    // value.
    // D3.10, D9.4
    TEST_ASSERT_EQ_STR(found("store ptr @\"util.twice\", ptr %f.0, align 8"),
                       "store ptr @\"util.twice\", ptr %f.0, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_main_in_another_module_is_an_ordinary_function, {
    TEST_ASSERT_TRUE(emit_two("app.ft",
                              "import util;\n"
                              "fn main() i32 { fn () i32 f = util.main; return f() -% 7; }\n",
                              "util.ft",
                              "fn main() i32 { return 7; }\n"));
    // `fort_entry` is emitted for the entry module alone; a `main` in any
    // other module is an ordinary function (item 22).
    // D8.6
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"util.main\"() #0"),
                       "define dso_local i32 @\"util.main\"() #0");
    TEST_ASSERT_EQ_STR(found("  %t0 = call i32 @\"app.main\"()\n"),
                       "  %t0 = call i32 @\"app.main\"()\n");
    TEST_ASSERT_EQ_STR(found("store ptr @\"util.main\", ptr %f.0, align 8"),
                       "store ptr @\"util.main\", ptr %f.0, align 8");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the call of a function pointer (item 7) ---------------------------------------
// D6.11

TEST(an_indirect_call_carries_the_function_type_of_its_callee, {
    TEST_ASSERT_TRUE(emit("fn add(i32 a, i32 b) i32 { return a +% b; }\n"
                          "fn main() i32 { fn (i32, i32) i32 f = add; return f(1, 2); }\n"));
    // An opaque pointer carries no signature, so the call site names the
    // function type.
    // D3.10, D19.2
    const char* want = "  %t0 = load ptr, ptr %f.0, align 8\n"
                       "  %t1 = call i32 (i32, i32) %t0(i32 1, i32 2)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_indirect_call_with_no_parameter_names_an_empty_type_list, {
    TEST_ASSERT_TRUE(emit("fn five() i32 { return 5; }\n"
                          "fn main() i32 { fn () i32 f = five; return f(); }\n"));
    TEST_ASSERT_EQ_STR(found("%t1 = call i32 () %t0()"), "%t1 = call i32 () %t0()");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_indirect_call_of_a_void_function_produces_no_temporary, {
    TEST_ASSERT_TRUE(emit("fn note(i32 x) void { println(x); }\n"
                          "fn main() i32 { fn (i32) void v = note; v(7); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("  call void (i32) %t0(i32 7)\n"), "  call void (i32) %t0(i32 7)\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_callee_of_an_indirect_call_is_evaluated_before_the_arguments, {
    TEST_ASSERT_TRUE(
        emit("fn add(i32 a, i32 b) i32 { return a +% b; }\n"
             "fn five() i32 { return 5; }\n"
             "fn main() i32 { fn (i32, i32) i32 f = add; return f(five(), five()); }\n"));
    // The walk emits calls and loads in source order, and the callee stands
    // before its arguments.
    // D6.3
    const char* want = "  %t0 = load ptr, ptr %f.0, align 8\n"
                       "  %t1 = call i32 @\"main.five\"()\n"
                       "  %t2 = call i32 @\"main.five\"()\n"
                       "  %t3 = call i32 (i32, i32) %t0(i32 %t1, i32 %t2)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_indirect_call_through_an_array_element_loads_the_element_first, {
    TEST_ASSERT_TRUE(emit("struct slot { fn (i32) i32 f; }\n"
                          "fn a(i32 x) i32 { return x; }\n"
                          "fn main() i32 { slot[1] t = {{a}}; return t[0].f(3); }\n"));
    const char* want = "  %t6 = load ptr, ptr %t5, align 8\n"
                       "  %t7 = call i32 (i32) %t6(i32 3)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_indirect_call_through_a_returned_pointer_calls_the_value_it_returns, {
    TEST_ASSERT_TRUE(emit("fn add(i32 a, i32 b) i32 { return a +% b; }\n"
                          "fn choose(bool p) fn (i32, i32) i32 { if (p) { return add; }\n"
                          "    return add; }\n"
                          "fn main() i32 { return choose(true)(4, 2); }\n"));
    const char* want = "  %t0 = call ptr @\"main.choose\"(i1 zeroext true)\n"
                       "  %t1 = call i32 (i32, i32) %t0(i32 4, i32 2)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_function_pointer_parameter_and_result_are_plain_pointers, {
    TEST_ASSERT_TRUE(emit("fn add(i32 a, i32 b) i32 { return a +% b; }\n"
                          "fn pick(fn (i32, i32) i32 f) fn (i32, i32) i32 { return f; }\n"
                          "fn main() i32 { return pick(add)(1, 2); }\n"));
    // A function pointer is passed and returned in a register like any other
    // pointer.
    // D9.9
    TEST_ASSERT_EQ_STR(found("define dso_local ptr @\"main.pick\"(ptr %f.in) #0"),
                       "define dso_local ptr @\"main.pick\"(ptr %f.in) #0");
    TEST_ASSERT_EQ_STR(found("call ptr @\"main.pick\"(ptr @\"main.add\")"),
                       "call ptr @\"main.pick\"(ptr @\"main.add\")");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_indirect_call_normalizes_its_narrow_arguments_and_result, {
    TEST_ASSERT_TRUE(emit("fn narrow(i8 a, u16 b, bool c, char d) i8 {\n"
                          "    if (c) { return a; }\n    return cast(d, i8);\n}\n"
                          "fn main() i32 { fn (i8, u16, bool, char) i8 f = narrow;\n"
                          "    return cast(f(1, 2, true, 'x'), i32); }\n"));
    // `bool`, `char`, `u8` and `u16` carry `zeroext` and `i8` and `i16`
    // `signext`, at an indirect call site as at a definition (item 7).
    // D9.9
    const char* want = "call signext i8 (i8, i16, i1, i8) %t0"
                       "(i8 signext 1, i16 zeroext 2, i1 zeroext true, i8 zeroext 120)";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_indirect_call_is_not_variadic_and_carries_no_nobuiltin, {
    TEST_ASSERT_TRUE(emit("fn add(i32 a, i32 b) i32 { return a +% b; }\n"
                          "fn main() i32 { fn (i32, i32) i32 f = add; return f(1, 2); }\n"));
    // A function type has no variadic form, so the call site names no
    // variadic tail, and `nobuiltin` belongs to extern call sites (item 8).
    // D3.10
    TEST_ASSERT_EQ_STR(absent("..."), "absent");
    TEST_ASSERT_EQ_STR(absent(" #3"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the aggregate convention through a pointer (item 7) ---------------------------
// D9.9

TEST(an_indirect_call_passes_an_aggregate_as_a_pointer_to_a_copy, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn first(point p) i32 { return p.x; }\n"
                          "fn main() i32 { fn (point) i32 f = first; point p = {1, 2};\n"
                          "    return f(p); }\n"));
    // The caller allocates the copy in its entry block and passes its
    // address; `byval` is never used (item 7).
    TEST_ASSERT_EQ_STR(absent("byval"), "absent");
    TEST_ASSERT_EQ_STR(found("%tmp0 = alloca %struct.main.point, align 4"),
                       "%tmp0 = alloca %struct.main.point, align 4");
    const char* want =
        "  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %p.1, i64 8, "
        "i1 false)\n"
        "  %t3 = call i32 (ptr) %t2(ptr %tmp0)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_indirect_call_writes_an_aggregate_result_through_the_leading_pointer, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn make(i32 v) point { point q = {v, v}; return q; }\n"
                          "fn main() i32 { fn (i32) point f = make; point r = f(3);\n"
                          "    return r.x; }\n"));
    // An aggregate result is a leading pointer on a `void` function, and the
    // call-site type lists it first (item 7).
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"main.make\"(ptr sret(%struct.main.point) "
                             "%ret.sret, i32 %v.in) #0"),
                       "define dso_local void @\"main.make\"(ptr sret(%struct.main.point) "
                       "%ret.sret, i32 %v.in) #0");
    // The leading pointer carries `sret` at the call site too.
    const char* call = "call void (ptr, i32) %t0(ptr sret(%struct.main.point) %r.1, i32 3)";
    TEST_ASSERT_EQ_STR(found(call), call);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_darwin_indirect_call_marks_its_aggregate_result_pointer, {
    TEST_ASSERT_TRUE(emit_target("struct point { i32 x; i32 y; }\n"
                                 "fn make(i32 v) point { point q = {v, v}; return q; }\n"
                                 "fn main() i32 { fn (i32) point f = make; point r = f(3);\n"
                                 "    return r.x; }\n",
                                 "arm64-apple-macosx26.6.2"));
    const char* call = "call void (ptr, i32) %t0(ptr sret(%struct.main.point) %r.1, i32 3)";
    TEST_ASSERT_EQ_STR(found(call), call);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_callee_may_write_to_the_aggregate_parameter_the_caller_copied, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn clobber(point mut p) i32 { p.x = 100; return p.x; }\n"
                          "fn main() i32 { fn (point) i32 f = clobber; point p = {1, 2};\n"
                          "    return f(p) +% p.x; }\n"));
    // An aggregate parameter is not copied again: its place is the
    // caller-made copy the incoming pointer designates, which the callee may
    // write to (item 10). Binding-level `mut` is not part of the function's
    // type, so the pointer's type still matches.
    // D8.2, D3.10, D5.6
    TEST_ASSERT_EQ_STR(absent("%p.0 = alloca"), "absent");
    const char* want =
        "  %t0 = getelementptr inbounds %struct.main.point, ptr %p.in, i32 0, i32 0\n"
        "  store i32 100, ptr %t0, align 4\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- many arguments ----------------------------------------------------------------
// D8.2

TEST(nine_arguments_are_written_in_source_order, {
    TEST_ASSERT_TRUE(
        emit("fn nine(i32 a, i32 b, i32 c, i32 d, i32 e, i32 f, i32 g, i32 h, i32 i) i32 {\n"
             "    return a; }\n"
             "fn main() i32 { return nine(1, 2, 3, 4, 5, 6, 7, 8, 9); }\n"));
    // LLVM places the arguments by System V; the compiler only fixes the
    // signature and the order.
    // D9.9
    TEST_ASSERT_EQ_STR(
        found("define dso_local i32 @\"main.nine\"(i32 %a.in, i32 %b.in, i32 %c.in, i32 %d.in, "
              "i32 %e.in, i32 %f.in, i32 %g.in, i32 %h.in, i32 %i.in) #0"),
        "define dso_local i32 @\"main.nine\"(i32 %a.in, i32 %b.in, i32 %c.in, i32 %d.in, "
        "i32 %e.in, i32 %f.in, i32 %g.in, i32 %h.in, i32 %i.in) #0");
    TEST_ASSERT_EQ_STR(found("call i32 @\"main.nine\"(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, "
                             "i32 7, i32 8, i32 9)"),
                       "call i32 @\"main.nine\"(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, "
                       "i32 7, i32 8, i32 9)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(nine_arguments_go_through_a_function_pointer_the_same_way, {
    TEST_ASSERT_TRUE(
        emit("fn nine(i32 a, i32 b, i32 c, i32 d, i32 e, i32 f, i32 g, i32 h, i32 i) i32 {\n"
             "    return a; }\n"
             "fn main() i32 {\n"
             "    fn (i32, i32, i32, i32, i32, i32, i32, i32, i32) i32 f = nine;\n"
             "    return f(1, 2, 3, 4, 5, 6, 7, 8, 9); }\n"));
    TEST_ASSERT_EQ_STR(
        found("call i32 (i32, i32, i32, i32, i32, i32, i32, i32, i32) %t0(i32 1, i32 2, i32 3, "
              "i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)"),
        "call i32 (i32, i32, i32, i32, i32, i32, i32, i32, i32) %t0(i32 1, i32 2, i32 3, "
        "i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_arguments_of_a_call_are_evaluated_left_to_right, {
    TEST_ASSERT_TRUE(emit("fn one() i32 { return 1; }\n"
                          "fn two() i32 { return 2; }\n"
                          "fn both(i32 a, i32 b) i32 { return a +% b; }\n"
                          "fn main() i32 { return both(one(), two()); }\n"));
    const char* want = "  %t0 = call i32 @\"main.one\"()\n"
                       "  %t1 = call i32 @\"main.two\"()\n"
                       "  %t2 = call i32 @\"main.both\"(i32 %t0, i32 %t1)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- recursion ---------------------------------------------------------------------
// D8.3

TEST(a_recursive_call_names_the_function_being_defined, {
    TEST_ASSERT_TRUE(emit("fn depth(i32 n) i32 { if (n == 0) { return 0; }\n"
                          "    return depth(n -% 1) +% 1; }\n"
                          "fn main() i32 { return depth(3); }\n"));
    const char* want = "  %t3 = sub i32 %t2, 1\n"
                       "  %t4 = call i32 @\"main.depth\"(i32 %t3)\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_functions_may_call_each_other, {
    TEST_ASSERT_TRUE(emit("fn even(i32 n) bool { if (n == 0) { return true; }\n"
                          "    return odd(n -% 1); }\n"
                          "fn odd(i32 n) bool { if (n == 0) { return false; }\n"
                          "    return even(n -% 1); }\n"
                          "fn main() i32 { if (even(2)) { return 0; } return 1; }\n"));
    // Forward references are legal in `.ll`, so one pass emits a call to a
    // definition that follows it (item 1).
    TEST_ASSERT_TRUE(before("@\"main.even\"(", "call zeroext i1 @\"main.even\""));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- identity ----------------------------------------------------------------------
// D3.13

TEST(comparing_two_function_pointers_is_an_icmp_on_ptr, {
    TEST_ASSERT_TRUE(emit("fn a(i32 x) i32 { return x; }\n"
                          "fn main() i32 { fn (i32) i32 f = a; if (f == a) { return 0; }\n"
                          "    return 1; }\n"));
    // `==` on function pointers compares identity.
    // D3.13
    TEST_ASSERT_EQ_STR(found("icmp eq ptr %t0, @\"main.a\""), "icmp eq ptr %t0, @\"main.a\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(comparing_a_function_pointer_with_null_names_the_null_constant, {
    TEST_ASSERT_TRUE(emit("fn main() i32 { fn () void f = null; if (f != null) { return 1; }\n"
                          "    return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("icmp ne ptr %t0, null"), "icmp ne ptr %t0, null");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(printing_a_function_pointer_reaches_the_pointer_printer, {
    TEST_ASSERT_TRUE(emit("fn a(i32 x) i32 { return x; }\n"
                          "fn main() i32 { fn (i32) i32 f = a; println(f); return 0; }\n"));
    // A pointer, `void*` or function pointer goes to `_ptr` (item 19).
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.print_ptr\"(i32 1, ptr %t0)"),
                       "call void @\"std.rt.print_ptr\"(i32 1, ptr %t0)");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- noreturn ----------------------------------------------------------------------
// D8.5, D19.7

TEST(an_indirect_call_of_a_noreturn_pointer_traps_at_the_call_site, {
    TEST_ASSERT_TRUE(emit("fn stop(string m) noreturn { panic(m); }\n"
                          "fn main() i32 { fn (string) noreturn f = stop; f(\"bye\"); }\n"));
    // Every call site of a `noreturn` function ends with the trap the rules
    // require, a call through a pointer included (item 20).
    // D8.5, D19.7
    const char* want = "  call void (ptr) %t0(ptr %tmp0)\n"
                       "  call void @llvm.trap()\n"
                       "  unreachable\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_statement_after_a_noreturn_call_stands_in_a_block_of_its_own, {
    TEST_ASSERT_TRUE(emit("fn stop() noreturn { panic(\"x\"); }\n"
                          "fn main() i32 { stop(); println(1); return 0; }\n"));
    // After a terminating statement the emitter opens a fresh block for the
    // unreachable statements the parser allows (item 10).
    // D14.2
    const char* want = "  call void @\"main.stop\"()\n"
                       "  call void @llvm.trap()\n"
                       "  unreachable\n"
                       "\nL0:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_noreturn_call_in_a_branch_leaves_the_edge_to_the_continuation_out, {
    TEST_ASSERT_TRUE(emit("fn stop() noreturn { panic(\"x\"); }\n"
                          "fn main() i32 { i32 n = 1; if (n == 0) { stop(); } return n; }\n"));
    // The branch terminated, so no `br` follows its trap (item 10).
    const char* want = "  call void @llvm.trap()\n"
                       "  unreachable\n"
                       "\nL1:\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_noreturn_body_that_falls_off_its_end_traps, {
    // `spin` is never called, so the only trap the module can hold is the one
    // after its body, which stands there as well as at every call site
    // because an optimizer that believed the callee could delete that one
    // (item 20).
    // D8.5
    TEST_ASSERT_TRUE(emit("fn spin() noreturn { while (true) { } }\n"
                          "fn main() i32 { return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("define dso_local void @\"main.spin\"() #1"),
                       "define dso_local void @\"main.spin\"() #1");
    // The call and not the `unreachable` alone is the trap the emitter owes,
    // since LLVM may let control fall through an `unreachable`; `verified()`
    // accepts a bare one, so the text is asserted here.
    // D19.7
    const char* want = "  call void @llvm.trap()\n  unreachable\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_extern_noreturn_declaration_stays_unadorned, {
    TEST_ASSERT_TRUE(emit("extern fn die(i32 code) noreturn;\n"
                          "fn main() i32 { die(1); }\n"));
    // `noreturn` is never put on the declaration of a C function, so the
    // optimizer cannot delete the trap that catches an extern returning
    // anyway (item 20). The declaration carries `nobuiltin` alone.
    TEST_ASSERT_EQ_STR(found("declare void @die(i32) nobuiltin\n"),
                       "declare void @die(i32) nobuiltin\n");
    const char* want = "  call void @die(i32 1) #3\n"
                       "  call void @llvm.trap()\n"
                       "  unreachable\n";
    TEST_ASSERT_EQ_STR(found(want), want);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- the module as a whole ---------------------------------------------------------

TEST(every_block_of_a_call_module_ends_in_exactly_one_terminator, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    TEST_ASSERT_EQ_STR(gen_block_terminators(), "one terminator per block");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_call_module_verifies_in_release_mode_too, {
    TEST_ASSERT_TRUE(emit_release(BUSY_SOURCE));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_runs_over_a_call_module_produce_byte_identical_text, {
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    // The text is a function of the program alone.
    // D19.5
    static char first[32768];
    TEST_ASSERT_TRUE(strlen(ir()) < sizeof first);
    TEST_UNUSED(snprintf(first, sizeof first, "%s", ir()));
    TEST_ASSERT_TRUE(emit(BUSY_SOURCE));
    TEST_ASSERT_EQ_STR(ir(), first);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// The same module twice, with `void mut*` in the place of every `void*`: an
// extern declaration, a parameter, a result, a span element, a local and the
// target of an out-parameter. The two sources differ in nothing else.
// D3.11
static const char VOID_PTR_SOURCE[] = "extern fn blit(i32 fd, void* buf, u64 n) i64;\n"
                                      "fn give(void* p) void { }\n"
                                      "fn pass(void* p, void*@ s, void* mut* out) void* {\n"
                                      "    *out = p;\n"
                                      "    give(s[0]);\n"
                                      "    return p;\n"
                                      "}\n"
                                      "fn main() i32 {\n"
                                      "    i32 mut x = 1;\n"
                                      "    void* p = cast(&x, void*);\n"
                                      "    void* mut slot = p;\n"
                                      "    void*[2] mut many = {};\n"
                                      "    void*@ mut all = {};\n"
                                      "    many[0] = p;\n"
                                      "    all = many[..];\n"
                                      "    void* back = pass(p, all, &slot);\n"
                                      "    i64 n = blit(1, back, 0);\n"
                                      "    println(back == p, \" \", slot == p, \" \", n);\n"
                                      "    return 0;\n"
                                      "}\n";

static const char VOID_MUT_PTR_SOURCE[] =
    "extern fn blit(i32 fd, void mut* buf, u64 n) i64;\n"
    "fn give(void mut* p) void { }\n"
    "fn pass(void mut* p, void mut*@ s, void mut* mut* out) void mut* {\n"
    "    *out = p;\n"
    "    give(s[0]);\n"
    "    return p;\n"
    "}\n"
    "fn main() i32 {\n"
    "    i32 mut x = 1;\n"
    "    void mut* p = cast(&x, void mut*);\n"
    "    void mut* mut slot = p;\n"
    "    void mut*[2] mut many = {};\n"
    "    void mut*@ mut all = {};\n"
    "    many[0] = p;\n"
    "    all = many[..];\n"
    "    void mut* back = pass(p, all, &slot);\n"
    "    i64 n = blit(1, back, 0);\n"
    "    println(back == p, \" \", slot == p, \" \", n);\n"
    "    return 0;\n"
    "}\n";

TEST(a_void_mut_pointer_emits_the_text_a_void_pointer_emits, {
    // Every pointer is one machine word, so the mark a `void*` carries is a
    // rule of the type system and reaches no instruction, no signature and no
    // size. The two modules are one text, byte for byte.
    // D3.1, D3.11, D19.2
    TEST_ASSERT_TRUE(strcmp(VOID_PTR_SOURCE, VOID_MUT_PTR_SOURCE) != 0);
    TEST_ASSERT_TRUE(emit(VOID_PTR_SOURCE));
    static char plain[32768];
    TEST_ASSERT_TRUE(strlen(ir()) < sizeof plain);
    TEST_UNUSED(snprintf(plain, sizeof plain, "%s", ir()));
    TEST_ASSERT_EQ_STR(
        found("define dso_local ptr @\"main.pass\"(ptr %p.in, ptr %s.in, ptr %out.in)"),
        "define dso_local ptr @\"main.pass\"(ptr %p.in, ptr %s.in, ptr %out.in)");
    TEST_ASSERT_EQ_STR(found("declare i64 @blit(i32, ptr, i64) nobuiltin\n"),
                       "declare i64 @blit(i32, ptr, i64) nobuiltin\n");
    TEST_ASSERT_EQ_STR(verified(), "verified");
    TEST_ASSERT_TRUE(emit(VOID_MUT_PTR_SOURCE));
    TEST_ASSERT_EQ_STR(ir(), plain);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_call", argc, argv);
    TEST_RUN(a_function_name_used_as_a_value_is_its_address);
    TEST_RUN(a_function_pointer_local_is_a_pointer_sized_slot);
    TEST_RUN(a_function_name_in_an_array_literal_is_stored_element_by_element);
    TEST_RUN(a_null_function_pointer_is_the_null_constant);
    TEST_RUN(an_extern_reaches_a_function_pointer_through_a_fort_wrapper);
    TEST_RUN(a_qualified_function_name_of_another_module_is_its_address);
    TEST_RUN(a_main_in_another_module_is_an_ordinary_function);
    TEST_RUN(an_indirect_call_carries_the_function_type_of_its_callee);
    TEST_RUN(an_indirect_call_with_no_parameter_names_an_empty_type_list);
    TEST_RUN(an_indirect_call_of_a_void_function_produces_no_temporary);
    TEST_RUN(the_callee_of_an_indirect_call_is_evaluated_before_the_arguments);
    TEST_RUN(an_indirect_call_through_an_array_element_loads_the_element_first);
    TEST_RUN(an_indirect_call_through_a_returned_pointer_calls_the_value_it_returns);
    TEST_RUN(a_function_pointer_parameter_and_result_are_plain_pointers);
    TEST_RUN(an_indirect_call_normalizes_its_narrow_arguments_and_result);
    TEST_RUN(an_indirect_call_is_not_variadic_and_carries_no_nobuiltin);
    TEST_RUN(an_indirect_call_passes_an_aggregate_as_a_pointer_to_a_copy);
    TEST_RUN(an_indirect_call_writes_an_aggregate_result_through_the_leading_pointer);
    TEST_RUN(a_darwin_indirect_call_marks_its_aggregate_result_pointer);
    TEST_RUN(a_callee_may_write_to_the_aggregate_parameter_the_caller_copied);
    TEST_RUN(nine_arguments_are_written_in_source_order);
    TEST_RUN(nine_arguments_go_through_a_function_pointer_the_same_way);
    TEST_RUN(the_arguments_of_a_call_are_evaluated_left_to_right);
    TEST_RUN(a_recursive_call_names_the_function_being_defined);
    TEST_RUN(two_functions_may_call_each_other);
    TEST_RUN(comparing_two_function_pointers_is_an_icmp_on_ptr);
    TEST_RUN(comparing_a_function_pointer_with_null_names_the_null_constant);
    TEST_RUN(printing_a_function_pointer_reaches_the_pointer_printer);
    TEST_RUN(an_indirect_call_of_a_noreturn_pointer_traps_at_the_call_site);
    TEST_RUN(a_statement_after_a_noreturn_call_stands_in_a_block_of_its_own);
    TEST_RUN(a_noreturn_call_in_a_branch_leaves_the_edge_to_the_continuation_out);
    TEST_RUN(a_noreturn_body_that_falls_off_its_end_traps);
    TEST_RUN(an_extern_noreturn_declaration_stays_unadorned);
    TEST_RUN(every_block_of_a_call_module_ends_in_exactly_one_terminator);
    TEST_RUN(a_call_module_verifies_in_release_mode_too);
    TEST_RUN(two_runs_over_a_call_module_produce_byte_identical_text);
    TEST_RUN(a_void_mut_pointer_emits_the_text_a_void_pointer_emits);
    gen_done();
    TEST_EXIT();
}
