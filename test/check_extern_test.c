// Tests agreement between two `extern fn` declarations of one C symbol. The extern
// declarations that are held against the runtime's own prototypes are the other half, in
// check_conv_test.c.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "check_helpers.h"
#include "modules.h"
#include "str.h"

#include "test.h"

// Detects the note that recommends a shared type or fort wrapper.
// The checker adds it for incompatible struct or enum spellings.
static bool said_wrapper(void) {
    return said("call it through a fort function the others import");
}

// How many errors the capture holds, for the tests that pin a diagnostic that
// must not cascade into a second one.
static uint64_t errors_said(void) {
    uint64_t n = 0;
    for (const char* cursor = strstr(diags(), ": error: "); cursor != NULL;
         cursor = strstr(cursor + 1, ": error: ")) {
        n++;
    }
    return n;
}

// ---- declarations that agree --------------------------------------------------------

TEST(two_declarations_of_one_extern_with_one_signature_agree, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn write(i32 fd, void* buf, u64 n) i64;\n"
        "fn main() i32 { return cast(write(1, null, 0), i32); }\n");
    // Parameter names are required by the grammar and otherwise unused, so they are not part of the
    // signature.
    add("other.ft",
        "extern fn write(i32 d, void* b, u64 count) i64;\n"
        "fn go() i64 { return write(1, null, 0); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(a_binding_mut_is_not_part_of_a_signature, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn write(i32 fd, void* buf, u64 n) i64;\n"
        "fn main() i32 { return 0; }\n");
    // A binding-level `mut` makes the callee's copy assignable and is not part of a function type.
    // An extern has no body for it to mean anything in, and the two declarations emit the same
    // bytes.
    add("other.ft",
        "extern fn write(i32 mut fd, void* buf, u64 n) i64;\n"
        "fn go() i64 { return write(1, null, 0); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(char_and_u8_are_one_c_type_at_the_boundary, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn hash(char* s, u8 seed) u64;\n"
        "fn main() i32 { return 0; }\n");
    // fort `char` is C's `unsigned char` and an extern declaration maps a C `char*` to `char*` or
    // `u8*`. The two declarations are one C prototype and emit byte-identical IR.
    add("other.ft",
        "extern fn hash(u8* s, char seed) u64;\n"
        "fn go(u8* s) u64 { return hash(s, cast(0, char)); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(a_char_result_agrees_with_a_u8_result, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn pick(i32 n) char;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn pick(i32 n) u8;\n"
        "fn go() u8 { return pick(1); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(one_imported_enum_spelled_two_ways_is_one_type, {
    begin();
    // The importer's `shade.color` is the only other spelling there is. Both denote one
    // declaration, so the signatures are identical.
    add("main.ft",
        "import shade;\n"
        "extern fn paint(shade.color c) void;\n"
        "fn main() i32 { shade.paint_red(); paint(shade.color.green); return 0; }\n");
    add("shade.ft",
        "enum color { red, green }\n"
        "extern fn paint(color c) void;\n"
        "fn paint_red() void { paint(color.red); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(one_imported_struct_behind_a_pointer_is_one_type, {
    begin();
    add("main.ft",
        "import geom;\n"
        "extern fn move_it(geom.point mut* p) void;\n"
        "fn main() i32 { return 0; }\n");
    add("geom.ft",
        "struct point { i64 x; i64 y; }\n"
        "extern fn move_it(point mut* p) void;\n"
        "fn shift(point mut* p) void { move_it(p); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(a_module_may_declare_an_extern_no_other_module_declares, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn strlen(char* s) u64;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn free(void* own p) void;\n"
        "fn go(void* own p) void { free(cast(move(p), void* own)); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(a_variable_tail_accepts_its_fixed_prefix_and_c_types, {
    TEST_ASSERT_TRUE(check_src("extern fn tail(i32 fixed, ...) i32;\n"
                               "fn callback(i32 n) i32 { return n; }\n"
                               "fn pass(i32 a, u32 b, i64 c, u64 d, i32* p, void* q) void {\n"
                               "    tail(1, a, b, c, d, p, q, callback);\n"
                               "}\n"
                               "fn main() i32 { return 0; }\n"));
})

TEST(a_variable_tail_checks_its_fixed_prefix, {
    TEST_ASSERT_FALSE(check_src("extern fn tail(i32 fixed, ...) i32;\n"
                                "fn main() i32 {\n"
                                "    tail();\n"
                                "    tail(\"wrong\".ptr);\n"
                                "    return 0;\n"
                                "}\n"));
    TEST_ASSERT_TRUE(said("'tail' takes at least 1 argument, 0 given"));
    TEST_ASSERT_TRUE(said("the argument expects i32"));
})

TEST(a_variable_tail_rejects_types_without_c_default_promotions, {
    TEST_ASSERT_FALSE(check_src("extern fn tail(i32 fixed, ...) i32;\n"
                                "enum color { red }\n"
                                "struct pair { i32 n; }\n"
                                "fn pass(i8 a, u16 b, bool c, char d, pair e) void {\n"
                                "    tail(1, a);\n"
                                "    tail(1, b);\n"
                                "    tail(1, c);\n"
                                "    tail(1, d);\n"
                                "    tail(1, color.red);\n"
                                "    tail(1, e);\n"
                                "}\n"
                                "fn main() i32 { return 0; }\n"));
    TEST_ASSERT_TRUE(said("C variable tail cannot use type 'i8'"));
    TEST_ASSERT_TRUE(said("C variable tail cannot use type 'u16'"));
    TEST_ASSERT_TRUE(said("C variable tail cannot use type 'bool'"));
    TEST_ASSERT_TRUE(said("C variable tail cannot use type 'char'"));
    TEST_ASSERT_TRUE(said("C variable tail cannot use type 'color'"));
    TEST_ASSERT_TRUE(said("C variable tail cannot use type 'pair'"));
})

TEST(a_variable_tail_lends_an_owning_lvalue_and_rejects_an_owning_rvalue, {
    TEST_ASSERT_FALSE(check_src("extern fn tail(i32 fixed, ...) i32;\n"
                                "fn make() i32 mut* own { return new(i32); }\n"
                                "fn pass(i32 mut* own p) void {\n"
                                "    tail(1, p);\n"
                                "    tail(1, make());\n"
                                "    del(p);\n"
                                "}\n"
                                "fn main() i32 { return 0; }\n"));
    TEST_ASSERT_TRUE(said("owning temporary would leak: nothing here could free it"));
    TEST_ASSERT_FALSE(said("copying an owning value requires 'move'"));
})

TEST(variable_tail_marks_and_fixed_types_agree_across_modules, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn tail(i32 n, ...) i32;\n"
        "fn main() i32 { return tail(1, 2); }\n");
    add("other.ft",
        "extern fn tail(i32 n, ...) i32;\n"
        "fn go() i32 { return tail(1); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));

    begin();
    add("main.ft",
        "import other;\n"
        "extern fn tail(i64 n, ...) i32;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn tail(i32 n, ...) i32;\n"
        "fn go() i32 { return tail(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("parameter 1 differs"));

    begin();
    add("main.ft",
        "import other;\n"
        "extern fn tail(i32 n, ...) i32;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn tail(i32 n) i32;\n"
        "fn go() i32 { return tail(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("the variable-tail mark differs"));
})

// ---- declarations that conflict -------------------------------

TEST(a_parameter_type_that_differs_conflicts_and_names_the_parameter, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn write(i32 fd, void* buf, u64 n) i64;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn write(i64 fd, void* buf, u64 n) i64;\n"
        "fn go() i64 { return write(1, null, 0); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'write': parameter 1 differs"));
    TEST_ASSERT_TRUE(said("note: previous declaration of 'write' here"));
    // Both modules can spell one signature, so the wrapper is not the answer.
    TEST_ASSERT_FALSE(said_wrapper());
})

TEST(the_conflict_names_the_first_parameter_that_differs, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn put(i32 fd, i64 a, i64 b) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn put(i32 fd, i64 a, i32 b) void;\n"
        "fn go() void { put(1, 2, 3); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'put': parameter 3 differs"));
})

TEST(the_error_stands_on_the_parameter_of_the_later_declaration, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn put(i32 fd) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn put(i64 fd) void;\n"
        "fn go() void { put(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // The imported module is checked first.
    TEST_ASSERT_TRUE(said("main.ft:2:15: error: conflicting declarations of extern 'put'"));
    TEST_ASSERT_TRUE(said("other.ft:1:11: note: previous declaration of 'put' here"));
})

TEST(an_own_that_differs_conflicts, {
    begin();
    add("main.ft",
        "import alloc;\n"
        "extern fn malloc(u64 n) void* own;\n"
        "fn main() i32 { return 0; }\n");
    // `own` is part of type identity and of signature identity, so the two
    // declarations do not describe one C function.
    add("alloc.ft",
        "extern fn malloc(u64 n) void*;\n"
        "fn grab(u64 n) void* { return malloc(n); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'malloc': the result type differs"));
    TEST_ASSERT_TRUE(said("note: previous declaration of 'malloc' here"));
})

TEST(a_mut_on_a_void_pointer_result_that_differs_conflicts, {
    begin();
    add("main.ft",
        "import alloc;\n"
        "extern fn malloc(u64 n) void mut* own;\n"
        "fn main() i32 { return 0; }\n");
    // The level a `void*` reaches is part of its type. It is part of signature identity as a typed
    // pointer's `mut` is: the two declarations below name one ELF symbol through two fort types.
    // This is the shape `std.libc` made real. Without it a program could declare `malloc` as
    // storage it may not write and call the library's.
    add("alloc.ft",
        "extern fn malloc(u64 n) void* own;\n"
        "fn grab(u64 n) void* { return malloc(n); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'malloc': the result type differs"));
    TEST_ASSERT_TRUE(said("note: previous declaration of 'malloc' here"));
})

TEST(a_mut_on_a_void_pointer_parameter_that_differs_conflicts, {
    begin();
    add("main.ft",
        "import sink;\n"
        "extern fn take(void mut* p) void;\n"
        "fn main() i32 { return 0; }\n");
    // The same difference in a parameter. `extern_type_agrees` answers about a
    // `void*` in one branch of its own, so a case on the result holds nothing
    // about a parameter.
    add("sink.ft",
        "extern fn take(void* p) void;\n"
        "fn drop(void* p) void { take(p); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'take': parameter 1 differs"));
    TEST_ASSERT_TRUE(said("note: previous declaration of 'take' here"));
})

TEST(an_own_parameter_that_differs_conflicts, {
    begin();
    add("main.ft",
        "import alloc;\n"
        "extern fn free(void* own p) void;\n"
        "fn main() i32 { return 0; }\n");
    add("alloc.ft",
        "extern fn free(void* p) void;\n"
        "fn drop(void* p) void { free(p); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'free': parameter 1 differs"));
})

TEST(pointee_mutability_that_differs_conflicts, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn strlen(char* s) u64;\n"
        "fn main() i32 { return 0; }\n");
    // Mutability at a level below the binding is part of type identity. `char mut*` is another
    // signature and C is told the callee writes through the pointer.
    add("other.ft",
        "extern fn strlen(char mut* s) u64;\n"
        "fn go(char mut* s) u64 { return strlen(s); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'strlen': parameter 1 differs"));
})

TEST(noreturn_that_differs_conflicts, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn quit(i32 code) noreturn;\n"
        "fn main() i32 { return 0; }\n");
    // `noreturn` is part of a function's identity and it is what puts the trap after the call site.
    // The two declarations do not describe one C function.
    add("other.ft",
        "extern fn quit(i32 code) void;\n"
        "fn go() void { quit(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'quit': the result type differs"));
})

TEST(a_parameter_count_that_differs_conflicts, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn printf(char* fmt, i32 n) i32;\n"
        "fn main() i32 { return 0; }\n");
    // Each argument shape of a variadic C function is its own prototype. One C symbol carries only
    // one prototype.
    add("other.ft",
        "extern fn printf(char* fmt) i32;\n"
        "fn go(char* f) i32 { return printf(f); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(
        said("conflicting declarations of extern 'printf': the number of parameters differs"));
    TEST_ASSERT_TRUE(said("main.ft:2:11: error:"));
})

TEST(two_integer_types_of_one_size_are_not_one_c_type, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn take(u32 n) void;\n"
        "fn main() i32 { return 0; }\n");
    // `i32` and `u32` are both `i32` in IR, but an extern declaration asks for identical signatures
    // and both modules can write either. The conservative reading refuses the pair: C's `int` and
    // `unsigned int` are two types.
    add("other.ft",
        "extern fn take(i32 n) void;\n"
        "fn go() void { take(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'take': parameter 1 differs"));
})

TEST(a_function_pointer_parameter_is_compared_through_its_own_signature, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn each(fn (i32) void f) void;\n"
        "fn main() i32 { return 0; }\n");
    // A `fn R(P...)` in an extern signature is extern-legal through its own signature. The marks of
    // its parameters are part of its identity, so the comparison reaches inside it.
    add("other.ft",
        "extern fn each(fn (i64) void f) void;\n"
        "fn go(fn (i64) void f) void { each(f); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'each': parameter 1 differs"));
})

// ---- types of one spelling that are two types ---------------------------------------

TEST(two_local_enums_of_one_name_conflict, {
    begin();
    // Both modules write `paint(color c)` and the two `color`s are different enums: a nominal type
    // is identified by the declaration it comes from. The program has no one signature for the C
    // symbol.
    add("main.ft",
        "import other;\n"
        "enum color { red, green }\n"
        "extern fn paint(color c) void;\n"
        "fn main() i32 { paint(color.red); return 0; }\n");
    add("other.ft",
        "enum color { blue, gold }\n"
        "extern fn paint(color c) void;\n"
        "fn go() void { paint(color.blue); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'paint': parameter 1 differs"));
    // No spelling makes these agree, so the note says what does.
    TEST_ASSERT_TRUE(said_wrapper());
    TEST_ASSERT_TRUE(said("a struct or an enum stands here"));
})

TEST(two_local_structs_behind_a_pointer_conflict, {
    begin();
    // The same case one level down: `point mut*` in both modules, two
    // different `point`s. Every pointer is `ptr` in IR, so nothing below the
    // compiler could ever notice.
    add("main.ft",
        "import other;\n"
        "struct point { i64 x; i64 y; }\n"
        "extern fn move_it(point mut* p) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "struct point { i32 x; i32 y; }\n"
        "extern fn move_it(point mut* p) void;\n"
        "fn go(point mut* p) void { move_it(p); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'move_it': parameter 1 differs"));
    TEST_ASSERT_TRUE(said_wrapper());
})

TEST(two_local_enums_in_result_position_conflict, {
    begin();
    add("main.ft",
        "import other;\n"
        "enum color { red, green }\n"
        "extern fn pick() color;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "enum color { blue, gold }\n"
        "extern fn pick() color;\n"
        "fn go() void { println(pick()); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'pick': the result type differs"));
    TEST_ASSERT_TRUE(said_wrapper());
})

TEST(a_local_enum_against_an_imported_one_conflicts, {
    begin();
    // The importer declares its own `color` rather than using the one it
    // imports: the two are different enums however alike they look.
    add("main.ft",
        "import shade;\n"
        "enum color { red, green }\n"
        "extern fn paint(color c) void;\n"
        "fn main() i32 { shade.paint_red(); return 0; }\n");
    add("shade.ft",
        "enum color { red, green }\n"
        "extern fn paint(color c) void;\n"
        "fn paint_red() void { paint(color.red); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'paint': parameter 1 differs"));
    TEST_ASSERT_TRUE(said_wrapper());
})

TEST(a_nominal_against_a_plain_type_offers_the_type_too, {
    begin();
    // One side names the enum and the other writes the `i32` it crosses as: the note fires although
    // only one module named a type. This is because the fix is the same one -- import the enum and
    // write it -- and no rewording of `i32` reaches it.
    add("main.ft",
        "import shade;\n"
        "extern fn paint(i32 c) void;\n"
        "fn main() i32 { return 0; }\n");
    add("shade.ft",
        "enum color { red, green }\n"
        "extern fn paint(color c) void;\n"
        "fn paint_red() void { paint(color.red); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'paint': parameter 1 differs"));
    TEST_ASSERT_TRUE(said("a struct or an enum stands here"));
    TEST_ASSERT_TRUE(said_wrapper());
})

TEST(the_note_fires_whichever_declaration_names_the_type, {
    begin();
    // The mirror of the case above: the earlier declaration is the plain one
    // and the later names the enum. The note is about the pair and not about
    // which module the error landed in.
    add("main.ft",
        "import plain;\n"
        "enum color { red, green }\n"
        "extern fn paint(color c) void;\n"
        "fn main() i32 { return 0; }\n");
    add("plain.ft",
        "extern fn paint(i32 c) void;\n"
        "fn go() void { paint(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'paint': parameter 1 differs"));
    TEST_ASSERT_TRUE(said("a struct or an enum stands here"));
})

TEST(two_runtime_declarations_may_agree_with_the_runtime_and_not_with_each_other, {
    begin();
    // The two comparisons are not the same comparison. `own` is erased at run time and the
    // runtime's C prototype has no notion of it. Each of these declarations satisfies the runtime
    // interface on its own. `own` is part of signature identity between two fort declarations of
    // one C symbol, so together they conflict. The error is the extern-versus-extern one: it points
    // at the other module and not at the compiler's own declaration.
    add("main.ft",
        "import other;\n"
        "extern fn fort_rt_del(u8 mut* own p) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn fort_rt_del(u8 mut* p) void;\n"
        "fn drop(u8 mut* p) void { fort_rt_del(p); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'fort_rt_del': parameter 1 differs"));
    TEST_ASSERT_TRUE(said("note: previous declaration of 'fort_rt_del' here"));
    TEST_ASSERT_FALSE(said("from the runtime's"));
    TEST_ASSERT_FALSE(said("the compiler declares it as"));
})

TEST(a_runtime_declaration_that_adds_own_is_accepted_alone, {
    // The other half of that asymmetry. A change that made the runtime comparison read `own` would
    // fail here rather than silently refuse the standard library's own declarations.
    TEST_ASSERT_TRUE(check_src("extern fn fort_rt_del(u8 mut* own p) void;\n"
                               "fn main() i32 { return 0; }\n"));
})

TEST(a_conflicting_declaration_is_poisoned_and_does_not_cascade, {
    begin();
    // The later declaration keeps no usable type: every call to it in its own
    // module is silent, so one disagreement is one diagnostic. The call below
    // passes an integer constant where the enum stands, which is an error of
    // its own the moment the symbol is not poisoned.
    add("main.ft",
        "import other;\n"
        "enum color { red, green }\n"
        "extern fn paint(color c) void;\n"
        "fn main() i32 { paint(1); return 0; }\n");
    add("other.ft",
        "enum color { blue, gold }\n"
        "extern fn paint(color c) void;\n"
        "fn go() void { paint(color.blue); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'paint': parameter 1 differs"));
    TEST_ASSERT_FALSE(said("an integer constant does not become an enum"));
    TEST_ASSERT_EQ_UINT64(errors_said(), (uint64_t)1);
})

TEST(a_pointer_that_erases_its_pointee_is_another_type, {
    // `void*`, `u8*` and a function pointer are all `ptr` in IR, and an extern declaration blesses
    // no spelling difference but `char*` for `u8*`. Each of these pairs is two types and every
    // module can write either name.
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn take(void* p) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn take(u8* p) void;\n"
        "fn go(u8* p) void { take(p); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'take': parameter 1 differs"));
    // A `void*` hides no struct or enum, so the note about one is absent.
    TEST_ASSERT_FALSE(said_wrapper());
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn take(char* p) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn take(void* p) void;\n"
        "fn go(void* p) void { take(p); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'take': parameter 1 differs"));
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn take(fn (i32) void f) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn take(void* f) void;\n"
        "fn go(void* f) void { take(f); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'take': parameter 1 differs"));
})

// ---- the closure --------------------------------------------------------------------

TEST(every_later_declaration_is_held_against_the_first, {
    begin();
    add("main.ft",
        "import a;\n"
        "import b;\n"
        "extern fn put(i32 n) void;\n"
        "fn main() i32 { a.go(); b.go(); return 0; }\n");
    // Three modules, one C symbol: the first declaration of the dependency order is the reference.
    // Both disagreeing modules are reported and each note points at the same earlier declaration.
    add("a.ft",
        "extern fn put(i32 n) void;\n"
        "fn go() void { put(1); }\n");
    add("b.ft",
        "extern fn put(i64 n) void;\n"
        "fn go() void { put(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("b.ft:1:15: error: conflicting declarations of extern 'put'"));
    TEST_ASSERT_TRUE(said("a.ft:1:11: note: previous declaration of 'put' here"));
    TEST_ASSERT_FALSE(said("main.ft:3:"));
})

TEST(a_conflict_in_an_imported_module_is_reported_from_any_entry, {
    begin();
    // Neither disagreeing declaration is in the entry module, and the closure
    // is still checked end to end, so the conflict is found.
    add("main.ft",
        "import a;\n"
        "fn main() i32 { a.go(); return 0; }\n");
    add("a.ft",
        "import b;\n"
        "extern fn put(i32 n) void;\n"
        "fn go() void { put(1); b.go(); }\n");
    add("b.ft",
        "extern fn put(i64 n) void;\n"
        "fn go() void { put(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'put': parameter 1 differs"));
})

TEST(a_declaration_that_failed_to_check_is_not_the_reference, {
    begin();
    // The first module's declaration is not extern-legal and reports its own error. It never
    // becomes the signature the others are held against and the two well-formed declarations agree.
    add("main.ft",
        "import a;\n"
        "import b;\n"
        "extern fn put(i32 n) void;\n"
        "fn main() i32 { return 0; }\n");
    add("a.ft",
        "extern fn put(i32@ n) void;\n"
        "fn go() void { }\n");
    add("b.ft",
        "extern fn put(i32 n) void;\n"
        "fn go() void { put(1); }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'i32@'"));
    TEST_ASSERT_FALSE(said("conflicting declarations"));
})

TEST(checking_one_module_twice_does_not_conflict_with_itself, {
    begin();
    add("main.ft",
        "extern fn put(i32 n) void;\n"
        "fn main() i32 { put(1); return 0; }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    // An editor can check one file twice with the same checker.
    // The second pass creates new symbols for the same tree.
    // These symbols must not conflict with the first pass.
    const module_t* m = module_at("main");
    TEST_ASSERT_NONNULL(m);
    TEST_ASSERT_TRUE(check_module(&checker, m));
    TEST_ASSERT_FALSE(said("conflicting declarations"));
})

TEST(a_muted_checker_counts_the_conflict_and_reports_nothing, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn put(i32 n) void;\n"
        "fn main() i32 { return 0; }\n");
    add("other.ft",
        "extern fn put(i64 n) void;\n"
        "fn go() void { put(1); }\n");
    want_mute = true;
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // A muted checker annotates the tree without reporting, so neither the
    // error nor its notes reach the sink.
    TEST_ASSERT_FALSE(said("conflicting declarations"));
    TEST_ASSERT_FALSE(said("previous declaration"));
})

int main(int argc, char** argv) {
    TEST_INIT("check_extern", argc, argv);
    TEST_RUN(two_declarations_of_one_extern_with_one_signature_agree);
    TEST_RUN(a_binding_mut_is_not_part_of_a_signature);
    TEST_RUN(char_and_u8_are_one_c_type_at_the_boundary);
    TEST_RUN(a_char_result_agrees_with_a_u8_result);
    TEST_RUN(one_imported_enum_spelled_two_ways_is_one_type);
    TEST_RUN(one_imported_struct_behind_a_pointer_is_one_type);
    TEST_RUN(a_module_may_declare_an_extern_no_other_module_declares);
    TEST_RUN(a_variable_tail_accepts_its_fixed_prefix_and_c_types);
    TEST_RUN(a_variable_tail_checks_its_fixed_prefix);
    TEST_RUN(a_variable_tail_rejects_types_without_c_default_promotions);
    TEST_RUN(a_variable_tail_lends_an_owning_lvalue_and_rejects_an_owning_rvalue);
    TEST_RUN(variable_tail_marks_and_fixed_types_agree_across_modules);
    TEST_RUN(a_parameter_type_that_differs_conflicts_and_names_the_parameter);
    TEST_RUN(the_conflict_names_the_first_parameter_that_differs);
    TEST_RUN(the_error_stands_on_the_parameter_of_the_later_declaration);
    TEST_RUN(an_own_that_differs_conflicts);
    TEST_RUN(a_mut_on_a_void_pointer_result_that_differs_conflicts);
    TEST_RUN(a_mut_on_a_void_pointer_parameter_that_differs_conflicts);
    TEST_RUN(an_own_parameter_that_differs_conflicts);
    TEST_RUN(pointee_mutability_that_differs_conflicts);
    TEST_RUN(noreturn_that_differs_conflicts);
    TEST_RUN(a_parameter_count_that_differs_conflicts);
    TEST_RUN(two_integer_types_of_one_size_are_not_one_c_type);
    TEST_RUN(a_function_pointer_parameter_is_compared_through_its_own_signature);
    TEST_RUN(two_local_enums_of_one_name_conflict);
    TEST_RUN(two_local_structs_behind_a_pointer_conflict);
    TEST_RUN(two_local_enums_in_result_position_conflict);
    TEST_RUN(a_local_enum_against_an_imported_one_conflicts);
    TEST_RUN(a_nominal_against_a_plain_type_offers_the_type_too);
    TEST_RUN(the_note_fires_whichever_declaration_names_the_type);
    TEST_RUN(two_runtime_declarations_may_agree_with_the_runtime_and_not_with_each_other);
    TEST_RUN(a_runtime_declaration_that_adds_own_is_accepted_alone);
    TEST_RUN(a_conflicting_declaration_is_poisoned_and_does_not_cascade);
    TEST_RUN(a_pointer_that_erases_its_pointee_is_another_type);
    TEST_RUN(every_later_declaration_is_held_against_the_first);
    TEST_RUN(a_conflict_in_an_imported_module_is_reported_from_any_entry);
    TEST_RUN(a_declaration_that_failed_to_check_is_not_the_reference);
    TEST_RUN(checking_one_module_twice_does_not_conflict_with_itself);
    TEST_RUN(a_muted_checker_counts_the_conflict_and_reports_nothing);
    check_reset();
    done();
    TEST_EXIT();
}
