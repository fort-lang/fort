// Unit tests of the checker's expressions (core-language.md 5.2, 5.5 to 5.10):
// the operand rules, the lvalues and their mutability, the postfix forms, the
// calls and the universe functions. The constants are in check_const_test.c
// and the statements in check_stmt_test.c.
// D3.2, D3.3, D3.13, D5.7, D5.8, D6.2, D6.7 to D6.11, D10.2, D12.2
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- operand rules ------------------------------------------------------------------
// D6.2

TEST(mixed_integer_types_are_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 a = 1;\n    i64 b = 2;\n    i64 s = a + b;\n"
                                 "    println(s);"));
    // There is no promotion, not even for u8 and i8.
    // D6.2
    TEST_ASSERT_TRUE(said("'+' takes two operands of the same type, not i32 and i64"));
})

TEST(char_has_no_arithmetic_and_no_bitwise, {
    TEST_ASSERT_FALSE(check_body("    char c = 'a';\n    char d = c + 1;\n    println(d);"));
    // `char` supports comparisons, switch and cast, and nothing else.
    // D3.2
    TEST_ASSERT_TRUE(said("'+' takes numeric operands, not char"));
    TEST_ASSERT_FALSE(check_body("    char c = 'a';\n    char e = c & 'b';\n    println(e);"));
    TEST_ASSERT_TRUE(said("'&' takes integer operands, not char"));
})

TEST(char_compares_and_switches,
     { TEST_ASSERT_TRUE(check_body("    char c = 'a';\n    bool b = c < 'b';\n    println(b);")); })

TEST(bool_has_no_ordering, {
    TEST_ASSERT_FALSE(check_body("    bool t = true;\n    bool b = t < false;\n    println(b);"));
    TEST_ASSERT_TRUE(said("'<' takes ordered operands, not bool"));
})

TEST(equality_is_refused_on_aggregates, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    point a = {1};\n    point b = {1};\n"
                                "    bool p = a == b;\n    println(p);\n    return 0;\n}\n"));
    // Equality is an error on structs, fixed arrays and spans.
    // D3.13
    TEST_ASSERT_TRUE(said("'==' takes comparable operands, not point"));
    TEST_ASSERT_FALSE(check_body("    i32[2] c = {1, 2};\n    i32[2] d = {1, 2};\n"
                                 "    bool q = c == d;\n    println(q);"));
    TEST_ASSERT_TRUE(said("'==' takes comparable operands, not i32[2]"));
    TEST_ASSERT_FALSE(check_body("    i32@ e = {};\n    i32@ f = {};\n    bool r = e == f;\n"
                                 "    println(r);"));
    TEST_ASSERT_TRUE(said("'==' takes comparable operands, not i32@"));
})

TEST(comparison_needs_identical_mutability, {
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32 mut* p = &x;\n    i32* q = &x;\n"
                                 "    bool same = p == q;\n    println(same);"));
    // "Same type" means identical, mutability levels included.
    // D6.2
    TEST_ASSERT_TRUE(said("not i32 mut* and i32*"));
})

TEST(a_void_pointer_compares_only_with_its_own_mutability, {
    // The level a `void*` reaches is part of its type, so `==` holds the two
    // operands to it as it holds a typed pointer's. The implicit drop does not
    // reach comparison, which is the last sentence of the decision that states
    // the drop, so a program that holds both casts one of them. This refusal is
    // new: before `void mut*` every `void*` compared with every other.
    // D3.11, D5.4, D6.2
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n"
                                 "    void mut* w = cast(&x, void mut*);\n"
                                 "    void* r = cast(&x, void*);\n"
                                 "    bool same = w == r;\n    println(same);"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("not void mut* and void*"));
    // Each compares with itself, with `null` and with the other one lent: the
    // cast that drops the mark is the way across, and it is the only way,
    // because no cast puts the mark back.
    // D3.14, D10.5
    TEST_ASSERT_TRUE(check_body("    i32 mut x = 1;\n"
                                "    void mut* w = cast(&x, void mut*);\n"
                                "    void* r = cast(&x, void*);\n"
                                "    bool lent = r == cast(w, void*);\n"
                                "    println(lent, w == null, r == null);"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)0);
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n"
                                 "    void mut* w = cast(&x, void mut*);\n"
                                 "    void* r = cast(&x, void*);\n"
                                 "    bool same = w == cast(r, void mut*);\n"
                                 "    println(same);"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("cannot cast void* to void mut*: a cast never adds 'mut'"));
})

TEST(equality_lends_an_owning_operand, {
    TEST_ASSERT_TRUE(check_body("    i32 mut* own p = new(i32);\n    i32 mut* q = p;\n"
                                "    bool same = p == q;\n    println(same);\n    del(p);"));
})

TEST(logical_operators_need_bool, {
    TEST_ASSERT_FALSE(check_body("    i32 a = 1;\n    bool b = a && true;\n    println(b);"));
    TEST_ASSERT_TRUE(said("'&&' takes bool operands, not i32"));
})

TEST(unary_minus_needs_a_signed_operand, {
    TEST_ASSERT_FALSE(check_body("    u32 c = 1;\n    u32 n = -c;\n    println(n);"));
    // Unary `-` takes signed integers and floats only.
    // D6.2
    TEST_ASSERT_TRUE(said("'-' takes a signed operand, not u32"));
})

TEST(there_is_no_pointer_arithmetic, {
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    i32* q = p + 1;\n"
                                 "    println(q);"));
    // The only ways to obtain a pointer are null, &, new, .ptr, cast, a
    // function name and calls.
    // D10.4
    TEST_ASSERT_TRUE(said("there is no pointer arithmetic: '+' does not apply to i32*"));
})

TEST(an_enum_compares_but_does_not_order, {
    TEST_ASSERT_TRUE(
        check_src("enum color {\n    red,\n    green,\n}\n"
                  "fn main() i32 {\n    color c = color.red;\n"
                  "    bool b = c == color.green;\n    println(b);\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(
        check_src("enum color {\n    red,\n    green,\n}\n"
                  "fn main() i32 {\n    color c = color.red;\n"
                  "    bool b = c < color.green;\n    println(b);\n    return 0;\n}\n"));
    // Enums support `== !=`, switch and cast, with no ordering.
    // D3.9
    TEST_ASSERT_TRUE(said("'<' takes ordered operands, not color"));
})

TEST(strings_compare_by_contents, {
    TEST_ASSERT_TRUE(
        check_body("    string s = \"a\";\n    bool b = s == \"a\";\n    println(b);"));
})

// ---- lvalues and mutability ---------------------------------------------------------
// D5.7, D5.8, D6.7

TEST(address_of_takes_the_mutability_of_its_operand, {
    TEST_ASSERT_TRUE(check_body("    i32 mut x = 1;\n    i32 y = 2;\n    i32 mut* p = &x;\n"
                                "    i32* q = &y;\n    println(p, q);"));
    // `&e` yields `T*` whose level 1 is the mutability of `e`.
    // D5.8
    TEST_ASSERT_EQ_STR(init_type("p"), "i32 mut*");
    TEST_ASSERT_EQ_STR(init_type("q"), "i32*");
})

TEST(an_address_of_an_immutable_is_not_a_mut_pointer, {
    TEST_ASSERT_FALSE(check_body("    i32 y = 2;\n    i32 mut* p = &y;\n    println(p);"));
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut*, not i32*"));
})

TEST(address_of_requires_an_lvalue, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    i32* p = &(x + 1);\n    println(p);"));
    TEST_ASSERT_TRUE(said("'&' requires an lvalue"));
})

TEST(address_of_a_function_is_refused, {
    TEST_ASSERT_FALSE(
        check_src("fn inc(i32 n) i32 {\n    return n + 1;\n}\n"
                  "fn main() i32 {\n    fn (i32) i32 c = &inc;\n    return c(1);\n}\n"));
    // A function name is already a value.
    // D3.10
    TEST_ASSERT_TRUE(said("'&' on a function"));
})

TEST(a_dereference_yields_the_pointee_and_its_mutability, {
    TEST_ASSERT_TRUE(check_body("    i32 mut x = 1;\n    i32 mut* p = &x;\n    *p = 2;\n"
                                "    println(*p);"));
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    *p = 2;\n"
                                 "    println(*p);"));
    // `*p` has level 1 of `p`'s type.
    // D5.7
    TEST_ASSERT_TRUE(said("cannot assign to immutable"));
})

TEST(a_void_pointer_cannot_be_dereferenced, {
    TEST_ASSERT_FALSE(check_body("    void* v = null;\n    i32 d = *v;\n    println(d);"));
    // `void*` has no pointee type, and `void mut*` has none either: the mark
    // says the storage may be written and no expression reaches it.
    // D3.11
    TEST_ASSERT_TRUE(said("cannot dereference void*"));
})

TEST(a_field_of_an_immutable_struct_is_immutable, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    point p = {1};\n    p.x = 2;\n"
                                "    return p.x;\n}\n"));
    // Level 0 of a field comes from the access path.
    // D5.5, D5.7
    TEST_ASSERT_TRUE(said("cannot assign to immutable field 'x'"));
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n}\n"
                               "fn main() i32 {\n    point mut p = {1};\n    p.x = 2;\n"
                               "    return p.x;\n}\n"));
})

TEST(a_span_element_has_level_one_mutability, {
    TEST_ASSERT_TRUE(check_body("    i32 mut@ own s = new(i32, 4);\n    s[0] = 1;\n"
                                "    println(s[0]);\n    del(s);"));
    TEST_ASSERT_FALSE(check_body("    i32@ own s = new(i32, 4);\n    s[0] = 1;\n"
                                 "    println(s[0]);\n    del(s);"));
    TEST_ASSERT_TRUE(said("cannot assign to immutable"));
})

TEST(a_string_character_is_immutable, {
    TEST_ASSERT_FALSE(check_body("    string s = \"ab\";\n    s[0] = 'x';\n    println(s);"));
    // `str[i]` is never mutable.
    // D5.7
    TEST_ASSERT_TRUE(said("cannot assign to immutable"));
})

TEST(len_and_ptr_are_not_lvalues, {
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    s.len = 0;\n    println(s.len);"));
    TEST_ASSERT_TRUE(said("cannot assign to a value that is not an lvalue"));
})

TEST(a_span_ptr_carries_the_element_mutability, {
    TEST_ASSERT_TRUE(check_body("    u8 mut@ own b = new(u8, 2);\n    u8 mut* p = b.ptr;\n"
                                "    println(p);\n    del(b);"));
    // `.ptr` carries the element level's mutability and is never own.
    // D3.5, D17.3
    TEST_ASSERT_EQ_STR(init_type("p"), "u8 mut*");
})

TEST(a_fixed_array_has_no_ptr, {
    TEST_ASSERT_FALSE(check_body("    i32[2] a = {1, 2};\n    println(a.ptr);"));
    TEST_ASSERT_TRUE(said("a fixed array has no '.ptr'"));
})

// ---- indexing and span expressions --------------------------------------------------
// D6.8, D6.9

TEST(a_constant_index_out_of_range_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32 x = a[4];\n    println(x);"));
    TEST_ASSERT_TRUE(said("index 4 out of range for i32[4]"));
})

TEST(an_index_of_an_rvalue_array_is_not_an_lvalue, {
    // `e[i]` is an lvalue where `e` is an lvalue fixed array: the elements of
    // a returned array live in a temporary.
    // D6.7
    TEST_ASSERT_FALSE(check_src("fn make() i32[3] {\n    return i32[3]{1, 2, 3};\n}\n"
                                "fn main() i32 {\n    i32* p = &make()[0];\n"
                                "    return *p;\n}\n"));
    TEST_ASSERT_TRUE(said("'&' requires an lvalue"));
    // Reading one is fine, and a span or string expression is an lvalue
    // whatever its operand.
    TEST_ASSERT_TRUE(check_src("fn make() i32[3] {\n    return i32[3]{1, 2, 3};\n}\n"
                               "fn main() i32 {\n    return make()[0];\n}\n"));
    TEST_ASSERT_TRUE(check_body("    string s = \"ab\";\n    println(&s[0]);"));
})

TEST(a_negative_constant_index_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32 x = a[-1];\n    println(x);"));
    TEST_ASSERT_TRUE(said("a negative index: -1"));
})

TEST(a_negative_span_bound_or_count_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32@ s = a[-1..];\n    println(s.len);"));
    TEST_ASSERT_TRUE(said("a negative span bound: -1"));
    TEST_ASSERT_FALSE(check_body("    del(new(i32, -1));"));
    TEST_ASSERT_TRUE(said("a negative count: -1"));
})

TEST(a_pointer_cannot_be_indexed, {
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    i32 v = p[0];\n"
                                 "    println(v);"));
    // Pointers cannot be indexed, not even pointers to arrays.
    // D6.8, D10.4
    TEST_ASSERT_TRUE(said("i32* cannot be indexed: write '(*p)[i]'"));
})

TEST(a_span_of_a_string_is_a_string, {
    TEST_ASSERT_TRUE(check_body("    string s = \"hello\";\n    string t = s[1..3];\n"
                                "    println(t);"));
    TEST_ASSERT_EQ_STR(init_type("t"), "string");
})

TEST(a_span_of_an_array_takes_its_element_mutability, {
    TEST_ASSERT_TRUE(check_body("    i32[4] mut a = {};\n    i32 mut@ s = a[1..3];\n"
                                "    i32@ t = a[..];\n    println(s.len, t.len);"));
    TEST_ASSERT_EQ_STR(init_type("s"), "i32 mut@");
    TEST_ASSERT_FALSE(check_body("    i32[4] a = {};\n    i32 mut@ s = a[1..3];\n"
                                 "    println(s.len);"));
    // A span of an immutable array cannot add mutability.
    // D6.9
    TEST_ASSERT_TRUE(said("expects i32 mut@, not i32@"));
})

TEST(a_span_of_a_pointer_needs_both_bounds, {
    TEST_ASSERT_TRUE(
        check_body("    i32 mut x = 1;\n    i32 mut* p = &x;\n    i32 mut@ s = p[0..1];\n"
                   "    println(s.len);"));
    TEST_ASSERT_FALSE(check_body("    i32 mut x = 1;\n    i32* p = &x;\n    i32@ s = p[0..];\n"
                                 "    println(s.len);"));
    // A pointer has no length, so only the two-bound form exists.
    // D6.9
    TEST_ASSERT_TRUE(said("a pointer has no length"));
})

TEST(a_span_of_a_void_pointer_is_refused, {
    TEST_ASSERT_FALSE(check_body("    void* v = null;\n    i32 n = 2;\n    i32@ s = v[0..n];\n"
                                 "    println(s.len);"));
    TEST_ASSERT_TRUE(said("cannot take a span of void*"));
})

// ---- field access -------------------------------------------------------------------
// D6.10

TEST(a_dot_on_a_pointer_says_to_use_an_arrow, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    point mut p = {1};\n    point mut* q = &p;\n"
                                "    return q.x;\n}\n"));
    TEST_ASSERT_TRUE(said("'.' on a pointer of type point mut*: use '->'"));
})

TEST(an_arrow_on_a_value_says_to_use_a_dot, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    point p = {1};\n    return p->x;\n}\n"));
    TEST_ASSERT_TRUE(said("'->' needs a pointer, not point: use '.'"));
})

TEST(an_arrow_reaches_the_span_pseudo_fields, {
    TEST_ASSERT_TRUE(check_src("fn size(u8@ mut* out) u64 {\n    return out->len;\n}\n"
                               "fn main() i32 {\n    u8@ mut s = {};\n"
                               "    return cast(size(&s), i32);\n}\n"));
})

TEST(an_unknown_field_is_reported_at_its_name, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    point p = {1};\n    return p.z;\n}\n"));
    TEST_ASSERT_TRUE(said("main.ft:6:14: error: struct point has no field 'z'"));
})

// ---- calls and the universe functions -----------------------------------------------
// D6.11, D12.2

TEST(a_call_checks_its_arity, {
    TEST_ASSERT_FALSE(check_src("fn add(i32 a, i32 b) i32 {\n    return a + b;\n}\n"
                                "fn main() i32 {\n    return add(1);\n}\n"));
    TEST_ASSERT_TRUE(said("'add' takes 2 arguments, 1 given"));
})

TEST(a_call_converts_its_arguments, {
    TEST_ASSERT_FALSE(check_src("fn add(i32 a, i32 b) i32 {\n    return a + b;\n}\n"
                                "fn main() i32 {\n    i64 w = 1;\n    return add(w, 2);\n}\n"));
    TEST_ASSERT_TRUE(said("the argument expects i32, not i64"));
})

TEST(a_function_name_is_a_value_of_its_type, {
    TEST_ASSERT_TRUE(
        check_src("fn inc(i32 n) i32 {\n    return n + 1;\n}\n"
                  "fn main() i32 {\n    fn (i32) i32 f = inc;\n    return f(1);\n}\n"));
    TEST_ASSERT_EQ_STR(init_type("f"), "fn (i32) i32");
})

TEST(an_extern_function_is_callable_and_not_a_value, {
    // An `extern fn` in value position is an error: an extern is called
    // through the LLVM type its declaration supplies, which an
    // indirect call site has no callee to take.
    // D3.10, D9.8
    TEST_ASSERT_TRUE(check_src("extern fn abs(i32 n) i32;\n"
                               "fn main() i32 {\n    return abs(-1);\n}\n"));
    TEST_ASSERT_FALSE(check_src("extern fn abs(i32 n) i32;\n"
                                "fn main() i32 {\n    fn (i32) i32 f = abs;\n"
                                "    return f(-1);\n}\n"));
    TEST_ASSERT_TRUE(said("'abs' is an extern function, which is not a value"));
    TEST_ASSERT_FALSE(check_src("extern fn abs(i32 n) i32;\n"
                                "fn main() i32 {\n    println(cast(abs, void*));\n"
                                "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("wrap it in a fort function to take a function pointer"));
})

TEST(an_extern_of_another_module_is_callable_and_not_a_value, {
    begin();
    add("libc.ft", "extern fn abs(i32 n) i32;\n");
    add("main.ft", "import libc;\nfn main() i32 {\n    return libc.abs(-1);\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    begin();
    add("libc.ft", "extern fn abs(i32 n) i32;\n");
    add("main.ft",
        "import libc;\nfn main() i32 {\n    fn (i32) i32 f = libc.abs;\n    return f(-1);\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("'abs' is an extern function, which is not a value"));
})

TEST(a_value_that_is_not_callable_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n    println(x(1));"));
    TEST_ASSERT_TRUE(said("cannot call a value of type i32"));
})

TEST(a_builtin_cannot_be_used_as_a_value, {
    TEST_ASSERT_FALSE(check_body("    fn (i32* own) void f = del;\n    println(f);"));
    // A universe function is callable and nothing else.
    // D12.2
    TEST_ASSERT_TRUE(said("'del' cannot be used as a value"));
})

TEST(a_builtin_that_yields_nothing_has_no_value, {
    TEST_ASSERT_FALSE(check_body("    i32 mut* own p = new(i32);\n    i32 r = del(p);\n"
                                 "    println(r);"));
    TEST_ASSERT_TRUE(said("'del' has no value"));
})

TEST(a_void_call_has_no_value, {
    TEST_ASSERT_FALSE(check_src("fn nothing() void {\n}\n"
                                "fn main() i32 {\n    i32 x = nothing();\n    return x;\n}\n"));
    TEST_ASSERT_TRUE(said("'nothing' has no value"));
})

TEST(a_local_shadows_a_builtin, {
    TEST_ASSERT_FALSE(check_body("    i32 print = 0;\n    print(1);"));
    // A module-level or local declaration shadows a universe name, which is
    // then inaccessible.
    // D7.9
    TEST_ASSERT_TRUE(said("cannot call a value of type i32"));
})

TEST(print_refuses_an_unprintable_value, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    point p = {1};\n    println(p);\n"
                                "    return 0;\n}\n"));
    // Structs, arrays and spans are not printable.
    // D12.2
    TEST_ASSERT_TRUE(said("cannot print a value of type point"));
})

TEST(the_print_family_takes_any_number_of_arguments, {
    TEST_ASSERT_TRUE(check_body("    println();\n    print(1, 'a', true, \"s\");\n"
                                "    eprintln(1);\n    fprintln(2, \"e\");"));
})

TEST(fprint_takes_a_descriptor_first, {
    TEST_ASSERT_FALSE(check_body("    fprint(\"x\", 1);"));
    TEST_ASSERT_TRUE(said("a descriptor expects i32, not string"));
    TEST_ASSERT_FALSE(check_body("    fprint();"));
    TEST_ASSERT_TRUE(said("'fprint' takes a descriptor and then its arguments"));
})

TEST(assert_takes_a_bool_and_panic_a_string, {
    TEST_ASSERT_FALSE(check_body("    i32 k = 1;\n    assert(k);"));
    TEST_ASSERT_TRUE(said("'assert' must be bool, not i32"));
    TEST_ASSERT_FALSE(check_body("    panic(1);"));
    TEST_ASSERT_TRUE(said("'panic' expects string, not a constant"));
    TEST_ASSERT_TRUE(check_body("    assert(1 < 2);\n    panic(\"stop\");"));
})

// ---- new and del --------------------------------------------------------------------
// D10.2, D12.2

TEST(new_yields_an_owning_pointer_or_span, {
    TEST_ASSERT_TRUE(
        check_body("    i32 mut* own p = new(i32);\n    i32 mut@ own s = new(i32, 4);\n"
                   "    println(s.len);\n    del(p);\n    del(s);"));
    // `new(T)` is `T mut* own` and `new(T, n)` is `T mut@ own`.
    // D10.2, D17.3
    TEST_ASSERT_EQ_STR(init_type("p"), "i32 mut* own");
    TEST_ASSERT_EQ_STR(init_type("s"), "i32 mut@ own");
})

TEST(new_marks_the_storage_it_allocates_and_no_position_below_it, {
    TEST_ASSERT_TRUE(check_body("    i32* mut* own pp = new(i32*);\n"
                                "    i32 mut* mut* own qq = new(i32 mut*);\n"
                                "    i32[4] mut* own r = new(i32[4]);\n"
                                "    println(pp, qq, r);\n"
                                "    del(pp);\n    del(qq);\n    del(r);"));
    // The slot `new` allocates is writable; what the slot reaches is the
    // program's to write, and a fresh slot reaches nothing.
    // D5.8, D10.2
    TEST_ASSERT_EQ_STR(init_type("pp"), "i32* mut* own");
    TEST_ASSERT_EQ_STR(init_type("qq"), "i32 mut* mut* own");
    // The elements of a fixed array share its storage, so one mark covers
    // both and the array is the outermost position.
    // D5.2, D5.3
    TEST_ASSERT_EQ_STR(init_type("r"), "i32[4] mut* own");
})

TEST(new_of_a_borrowed_element_needs_no_cast_to_reach_a_borrowed_span, {
    // A span of slots that borrow, which `src/fort/gen.ft` reached with a cast
    // until this rule changed. `new` answers it directly now, and each
    // spelling refuses the conversion to the other.
    // D5.4, D10.2
    TEST_ASSERT_TRUE(check_body("    i32* mut@ own s = new(i32*, 2);\n"
                                "    println(s.len);\n    del(s);"));
    TEST_ASSERT_EQ_STR(init_type("s"), "i32* mut@ own");
    TEST_ASSERT_FALSE(check_body("    i32 mut* mut@ own s = new(i32*, 2);\n"
                                 "    println(s.len);\n    del(s);"));
    TEST_ASSERT_TRUE(said("expects i32 mut* mut@ own, not i32* mut@ own"));
    TEST_ASSERT_FALSE(check_body("    i32* mut@ own s = new(i32 mut*, 2);\n"
                                 "    println(s.len);\n    del(s);"));
    TEST_ASSERT_TRUE(said("expects i32* mut@ own, not i32 mut* mut@ own"));
})

TEST(new_of_an_own_element_owns_each_slot, {
    TEST_ASSERT_TRUE(
        check_src("struct node {\n    i32 v;\n}\n"
                  "fn main() i32 {\n    node mut* own mut@ own k = new(node mut* own, 4);\n"
                  "    println(k.len);\n    del(k);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(init_type("k"), "node mut* own mut@ own");
})

TEST(new_of_a_void_pointer_allocates_a_slot, {
    TEST_ASSERT_TRUE(check_body("    void mut* mut* own p = new(void mut*);\n"
                                "    void mut* mut@ own s = new(void mut*, 4);\n"
                                "    void* mut@ own b = new(void*, 4);\n"
                                "    println(s.len, b.len);\n"
                                "    del(p);\n    del(s);\n    del(b);"));
    // `new(void*)` is legal, one pointer slot. The slot is writable because
    // `new` allocates it; the level a `void*` reaches is the program's to
    // mark, and both spellings are types.
    // D3.11, D5.8, D10.2, D17.3
    TEST_ASSERT_EQ_STR(init_type("p"), "void mut* mut* own");
    TEST_ASSERT_EQ_STR(init_type("s"), "void mut* mut@ own");
    TEST_ASSERT_EQ_STR(init_type("b"), "void* mut@ own");
    // The slot takes a writable pointer and refuses a read-only one, as a
    // `node mut*` slot does.
    // D5.4
    TEST_ASSERT_FALSE(check_body("    i32 x = 1;\n"
                                 "    void mut* mut@ own s = new(void mut*, 1);\n"
                                 "    s[0] = cast(&x, void*);\n    del(s);"));
    TEST_ASSERT_TRUE(said("expects void mut*, not void*"));
})

TEST(new_of_void_is_refused, {
    // It is `new(void)` that the new builtin rejects, `void` having no size.
    // D10.2
    TEST_ASSERT_FALSE(check_body("    void* own p = new(void);\n    del(p);"));
    TEST_ASSERT_TRUE(said("'new' needs a sized type, not void"));
})

TEST(del_needs_an_owning_operand, {
    TEST_ASSERT_FALSE(check_body("    string s = \"a\";\n    del(s);"));
    TEST_ASSERT_TRUE(said("'del' needs an owning operand, not string"));
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    point p = {1};\n    del(p);\n"
                                "    return 0;\n}\n"));
    // `del` of a struct or array is an error.
    // D12.2
    TEST_ASSERT_TRUE(said("'del' takes a reference, not point"));
})

TEST(del_of_null_is_allowed, { TEST_ASSERT_TRUE(check_body("    del(null);")); })

TEST(move_needs_an_owning_lvalue, {
    TEST_ASSERT_FALSE(check_body("    i32 k = 1;\n    i32 m = move(k);\n    println(m);"));
    TEST_ASSERT_TRUE(said("'move' needs an owning operand, not i32"));
    TEST_ASSERT_TRUE(check_body("    i32 mut* own p = new(i32);\n    i32 mut* own q = move(p);\n"
                                "    del(q);"));
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_expr", argc, argv);
    TEST_RUN(mixed_integer_types_are_refused);
    TEST_RUN(char_has_no_arithmetic_and_no_bitwise);
    TEST_RUN(char_compares_and_switches);
    TEST_RUN(bool_has_no_ordering);
    TEST_RUN(equality_is_refused_on_aggregates);
    TEST_RUN(comparison_needs_identical_mutability);
    TEST_RUN(a_void_pointer_compares_only_with_its_own_mutability);
    TEST_RUN(equality_lends_an_owning_operand);
    TEST_RUN(logical_operators_need_bool);
    TEST_RUN(unary_minus_needs_a_signed_operand);
    TEST_RUN(there_is_no_pointer_arithmetic);
    TEST_RUN(an_enum_compares_but_does_not_order);
    TEST_RUN(strings_compare_by_contents);
    TEST_RUN(address_of_takes_the_mutability_of_its_operand);
    TEST_RUN(an_address_of_an_immutable_is_not_a_mut_pointer);
    TEST_RUN(address_of_requires_an_lvalue);
    TEST_RUN(address_of_a_function_is_refused);
    TEST_RUN(a_dereference_yields_the_pointee_and_its_mutability);
    TEST_RUN(a_void_pointer_cannot_be_dereferenced);
    TEST_RUN(a_field_of_an_immutable_struct_is_immutable);
    TEST_RUN(a_span_element_has_level_one_mutability);
    TEST_RUN(a_string_character_is_immutable);
    TEST_RUN(len_and_ptr_are_not_lvalues);
    TEST_RUN(a_span_ptr_carries_the_element_mutability);
    TEST_RUN(a_fixed_array_has_no_ptr);
    TEST_RUN(a_constant_index_out_of_range_is_refused);
    TEST_RUN(an_index_of_an_rvalue_array_is_not_an_lvalue);
    TEST_RUN(a_negative_constant_index_is_refused);
    TEST_RUN(a_negative_span_bound_or_count_is_refused);
    TEST_RUN(a_pointer_cannot_be_indexed);
    TEST_RUN(a_span_of_a_string_is_a_string);
    TEST_RUN(a_span_of_an_array_takes_its_element_mutability);
    TEST_RUN(a_span_of_a_pointer_needs_both_bounds);
    TEST_RUN(a_span_of_a_void_pointer_is_refused);
    TEST_RUN(a_dot_on_a_pointer_says_to_use_an_arrow);
    TEST_RUN(an_arrow_on_a_value_says_to_use_a_dot);
    TEST_RUN(an_arrow_reaches_the_span_pseudo_fields);
    TEST_RUN(an_unknown_field_is_reported_at_its_name);
    TEST_RUN(a_call_checks_its_arity);
    TEST_RUN(a_call_converts_its_arguments);
    TEST_RUN(a_function_name_is_a_value_of_its_type);
    TEST_RUN(an_extern_function_is_callable_and_not_a_value);
    TEST_RUN(an_extern_of_another_module_is_callable_and_not_a_value);
    TEST_RUN(a_value_that_is_not_callable_is_refused);
    TEST_RUN(a_builtin_cannot_be_used_as_a_value);
    TEST_RUN(a_builtin_that_yields_nothing_has_no_value);
    TEST_RUN(a_void_call_has_no_value);
    TEST_RUN(a_local_shadows_a_builtin);
    TEST_RUN(print_refuses_an_unprintable_value);
    TEST_RUN(the_print_family_takes_any_number_of_arguments);
    TEST_RUN(fprint_takes_a_descriptor_first);
    TEST_RUN(assert_takes_a_bool_and_panic_a_string);
    TEST_RUN(new_yields_an_owning_pointer_or_span);
    TEST_RUN(new_marks_the_storage_it_allocates_and_no_position_below_it);
    TEST_RUN(new_of_a_borrowed_element_needs_no_cast_to_reach_a_borrowed_span);
    TEST_RUN(new_of_an_own_element_owns_each_slot);
    TEST_RUN(new_of_a_void_pointer_allocates_a_slot);
    TEST_RUN(new_of_void_is_refused);
    TEST_RUN(del_needs_an_owning_operand);
    TEST_RUN(del_of_null_is_allowed);
    TEST_RUN(move_needs_an_owning_lvalue);
    check_reset();
    done();
    TEST_EXIT();
}
