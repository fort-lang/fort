// Tests implicit conversions, casts, qualified names, and conversion errors.
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- dropping mutability ------------------------------------------------------------

TEST(mutability_drops_at_level_one, {
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32 mut* p = &v;\n    i32* q = p;\n"
                                "    println(q);"));
})

TEST(mutability_is_never_added_implicitly, {
    TEST_ASSERT_FALSE(check_body("    i32 mut v = 1;\n    i32* p = &v;\n    i32 mut* q = p;\n"
                                 "    println(q);"));
    // Adding mutability requires a cast.
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut*, not i32*"));
})

TEST(a_drop_behind_a_mutable_level_is_refused, {
    TEST_ASSERT_TRUE(check_src("struct node {\n    i32 v;\n}\n"
                               "fn main() i32 {\n"
                               "    node mut* mut@ own a = new(node mut*, 1);\n"
                               "    node*@ b = a;\n    println(b.len);\n    del(a);\n"
                               "    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("struct node {\n    i32 v;\n}\n"
                                "fn main() i32 {\n"
                                "    node mut* mut@ own a = new(node mut*, 1);\n"
                                "    node* mut@ c = a;\n    println(c.len);\n    del(a);\n"
                                "    return 0;\n}\n"));
    // A mutable slot could then hold a pointer to what the source still sees
    // as mutable: the C `T** -> const T**` hole.
    TEST_ASSERT_TRUE(said("expects node* mut@, not node mut* mut@ own"));
})

TEST(the_drop_rule_applies_to_arguments_and_returns, {
    TEST_ASSERT_TRUE(check_src("fn look(i32* p) void {\n    println(p);\n}\n"
                               "fn view(i32 mut* p) i32* {\n    return p;\n}\n"
                               "fn main() i32 {\n    i32 mut v = 1;\n    look(&v);\n"
                               "    println(view(&v));\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("fn write(i32 mut* p) void {\n    println(p);\n}\n"
                                "fn main() i32 {\n    i32 v = 1;\n    write(&v);\n"
                                "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("the argument expects i32 mut*, not i32*"));
})

TEST(level_zero_is_unconstrained, {
    // Level 0 of the receiving binding is unconstrained.
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32* mut p = &v;\n    i32* q = p;\n"
                                "    p = q;\n    println(q);"));
})

// ---- dropping ownership -------------------------------------------------------------

TEST(ownership_drops_where_mutability_does, {
    TEST_ASSERT_TRUE(check_body("    i32 mut* own p = new(i32);\n    i32 mut* v = p;\n"
                                "    i32* w = p;\n    println(v, w);\n    del(p);"));
})

TEST(ownership_is_never_added_implicitly, {
    TEST_ASSERT_FALSE(check_body("    i32 mut v = 1;\n    i32 mut* own p = &v;\n    del(p);"));
    // `&` yields a borrowed pointer; adding `own` needs a cast.
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut* own, not i32 mut*"));
})

TEST(an_owning_span_lends_its_elements, {
    TEST_ASSERT_TRUE(check_body("    i32 mut@ own s = new(i32, 2);\n    i32 mut@ v = s;\n"
                                "    i32@ w = s;\n    println(v.len, w.len);\n    del(s);"));
})

// ---- the marker table ---------------------------------------------------------------

TEST(the_binding_marker_says_what_may_be_rebound, {
    // Every row of the table, left column: the marker before the name is the
    // binding's own storage.
    TEST_ASSERT_TRUE(check_node_body("    i32 mut x = 1;\n    x = 2;"));
    TEST_ASSERT_FALSE(check_node_body("    i32 x = 1;\n    x = 2;"));
    TEST_ASSERT_TRUE(check_node_body("    node mut q = {};\n    q = m;"));
    TEST_ASSERT_FALSE(check_node_body("    node q = {};\n    q = m;"));
    TEST_ASSERT_TRUE(check_node_body("    i32[4] mut a = {};\n    i32[4] b = {};\n    a = b;"));
    TEST_ASSERT_FALSE(check_node_body("    i32[4] a = {};\n    i32[4] b = {};\n    a = b;"));
    TEST_ASSERT_FALSE(check_node_body("    node* p = &k;\n    p = &m;"));
    TEST_ASSERT_TRUE(check_node_body("    node* mut p = &k;\n    p = &m;"));
    TEST_ASSERT_FALSE(check_node_body("    node mut* p = &m;\n    p = &m;"));
    TEST_ASSERT_TRUE(check_node_body("    node mut* mut p = &m;\n    p = &m;"));
    TEST_ASSERT_FALSE(check_node_body("    i32@ s = {};\n    i32@ t = {};\n    s = t;"));
    TEST_ASSERT_TRUE(check_node_body("    i32@ mut s = {};\n    i32@ t = {};\n    s = t;"));
    TEST_ASSERT_FALSE(check_node_body("    i32 mut@ s = {};\n    i32 mut@ t = {};\n    s = t;"));
    TEST_ASSERT_TRUE(check_node_body("    string mut s = \"a\";\n    s = \"b\";"));
    TEST_ASSERT_FALSE(check_node_body("    string s = \"a\";\n    s = \"b\";"));
})

TEST(a_marker_behind_an_indirection_says_what_may_be_written, {
    // In every table row, a `mut` after a reference suffix marks the reference.
    // A `mut` after the base type marks what the reference reaches.
    TEST_ASSERT_FALSE(check_node_body("    node* p = &k;\n    p->value = 1;"));
    TEST_ASSERT_FALSE(check_node_body("    node* mut p = &k;\n    p->value = 1;"));
    TEST_ASSERT_TRUE(check_node_body("    node mut* p = &m;\n    p->value = 1;"));
    TEST_ASSERT_TRUE(check_node_body("    node mut* mut p = &m;\n    p->value = 1;"));
    TEST_ASSERT_FALSE(check_node_body("    i32@ s = {};\n    s[0] = 1;"));
    TEST_ASSERT_FALSE(check_node_body("    i32@ mut s = {};\n    s[0] = 1;"));
    TEST_ASSERT_TRUE(check_node_body("    i32 mut@ s = {};\n    s[0] = 1;"));
    // `node* mut@ t`: the slots are writable and the nodes behind them are
    // not; `node mut*@ t` is the other way round.
    TEST_ASSERT_TRUE(check_node_body("    node* mut@ t = {};\n    t[0] = &k;"));
    TEST_ASSERT_FALSE(check_node_body("    node* mut@ t = {};\n    t[0]->value = 1;"));
    TEST_ASSERT_FALSE(check_node_body("    node mut*@ t = {};\n    t[0] = &m;"));
    TEST_ASSERT_TRUE(check_node_body("    node mut*@ t = {};\n    t[0]->value = 1;"));
    // `node* mut* pp`: `*pp` is writable, `**pp` is not.
    TEST_ASSERT_TRUE(check_node_body("    node* mut p = &k;\n    node* mut* pp = &p;\n"
                                     "    *pp = &k;"));
    TEST_ASSERT_FALSE(check_node_body("    node* mut p = &k;\n    node* mut* pp = &p;\n"
                                      "    (*pp)->value = 1;"));
    TEST_ASSERT_FALSE(check_node_body("    node mut* p = &m;\n    node mut** pp = &p;\n"
                                      "    *pp = &m;"));
    TEST_ASSERT_TRUE(check_node_body("    node mut* p = &m;\n    node mut** pp = &p;\n"
                                     "    (*pp)->value = 1;"));
    // The out-parameter shapes.
    TEST_ASSERT_TRUE(check_node_body("    u8 mut@ mut b = {};\n    u8 mut@ mut* out = &b;\n"
                                     "    (*out)[0] = 1;"));
    TEST_ASSERT_FALSE(check_node_body("    u8@ mut b = {};\n    u8@ mut* out = &b;\n"
                                      "    (*out)[0] = 1;"));
    // A fixed array of pointers: the slots follow the array, the pointees
    // their own marker.
    TEST_ASSERT_TRUE(check_node_body("    node*[4] mut t = {};\n    t[0] = &k;"));
    TEST_ASSERT_FALSE(check_node_body("    node*[4] mut t = {};\n    t[0]->value = 1;"));
    // A string's characters are never mutable.
    TEST_ASSERT_FALSE(check_node_body("    string mut s = \"a\";\n    s[0] = 'b';"));
})

// ---- the cast matrix ----------------------------------------------------------------

TEST(a_cast_converts_between_pointers_and_integers, {
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32 mut* p = &v;\n"
                                "    u8 mut* b = cast(p, u8 mut*);\n    u64 n = cast(p, u64);\n"
                                "    void* o = cast(p, void*);\n    println(b, n, o);"));
})

TEST(a_cast_adds_ownership_and_never_mutability, {
    // A cast never adds `mut`. A writable pointer needs a writable source.
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    i32* p = &v;\n"
                                 "    i32 mut* w = cast(p, i32 mut*);\n    println(w);"));
    TEST_ASSERT_TRUE(said("cannot cast i32* to i32 mut*: a cast never adds 'mut'"));
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32 mut* p = &v;\n"
                                "    i32 mut* w = cast(p, i32 mut*);\n    println(w);"));
    // Adding `own` is the adoption escape and it stays.
    TEST_ASSERT_TRUE(check_body("    u8 mut* p = null;\n"
                                "    u8 mut* own a = cast(p, u8 mut* own);\n    println(a);"));
    // The laundering route: a `void*` is a source like any other, and the
    // second cast is the one that fails.
    TEST_ASSERT_FALSE(check_body("    i32 v = 1;\n    i32* p = &v;\n"
                                 "    void* q = cast(p, void*);\n"
                                 "    i32 mut* w = cast(q, i32 mut*);\n    println(w);"));
    TEST_ASSERT_TRUE(said("cannot cast void* to i32 mut*: a cast never adds 'mut'"));
    // A `string` is immutable bytes, so no member of the family adds the mark.
    TEST_ASSERT_FALSE(check_body("    string s = \"ab\";\n"
                                 "    u8 mut@ b = cast(s, u8 mut@);\n    println(b.len);"));
    TEST_ASSERT_TRUE(said("cannot cast string to u8 mut@: a cast never adds 'mut'"));
    // An address in a `u64` names no level at all.
    TEST_ASSERT_FALSE(check_body("    u64 bits = 0;\n"
                                 "    i32 mut* w = cast(bits, i32 mut*);\n    println(w);"));
    TEST_ASSERT_TRUE(said("cannot cast u64 to i32 mut*: a cast never adds 'mut'"));
})

TEST(a_cast_converts_among_string_and_byte_spans, {
    TEST_ASSERT_TRUE(check_body("    string s = \"ab\";\n    u8@ b = cast(s, u8@);\n"
                                "    string t = cast(b, string);\n    println(b.len, t);"));
})

TEST(a_cast_never_changes_a_span_element_type, {
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    u32@ u = cast(s, u32@);\n"
                                 "    println(u.len);"));
    // The element type of a span never changes, because `len` counts elements.
    TEST_ASSERT_TRUE(said("cannot cast i32@ to u32@"));
    // The refusal has one reason and the message names it. A cast refused for
    // another reason carries no tail, so the tail says which rule refused.
    TEST_ASSERT_FALSE(said("a cast never adds 'mut'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    // The same pair with the mark on the target adds a mark and still carries no tail: the element
    // type refuses it as well. A reader who dropped the mark would meet a second error. The tail
    // stands only where dropping the marks of the target makes the cast legal.
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    u32 mut@ u = cast(s, u32 mut@);\n"
                                 "    println(u.len);"));
    TEST_ASSERT_TRUE(said("cannot cast i32@ to u32 mut@"));
    TEST_ASSERT_FALSE(said("a cast never adds 'mut'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    // The mark alone, over the same element type: the tail stands.
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    i32 mut@ u = cast(s, i32 mut@);\n"
                                 "    println(u.len);"));
    TEST_ASSERT_TRUE(said("cannot cast i32@ to i32 mut@: a cast never adds 'mut'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_cast_from_a_pointer_to_a_span_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 mut v = 1;\n    i32* p = &v;\n"
                                 "    i32@ s = cast(p, i32@);\n    println(s.len);"));
    TEST_ASSERT_TRUE(said("cannot cast i32* to i32@"));
})

TEST(a_cast_to_a_struct_or_an_array_is_refused, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn main() i32 {\n    i32 n = 1;\n    point p = cast(n, point);\n"
                                "    return p.x;\n}\n"));
    TEST_ASSERT_TRUE(said("cannot cast i32 to point"));
})

TEST(a_cast_between_an_enum_and_an_integer_is_allowed, {
    TEST_ASSERT_TRUE(check_src("enum color {\n    red,\n    green,\n}\n"
                               "fn main() i32 {\n    color c = cast(1, color);\n"
                               "    return cast(c, i32);\n}\n"));
})

TEST(a_cast_between_char_and_an_integer_is_allowed, {
    TEST_ASSERT_TRUE(check_body("    char c = cast(65, char);\n    i32 n = cast(c, i32);\n"
                                "    println(c, n);"));
})

TEST(a_cast_target_that_is_too_large_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 n = 1;\n"
                                 "    i32[4611686018427387904] a = "
                                 "cast(n, i32[4611686018427387904]);\n"
                                 "    println(a[0]);"));
    TEST_ASSERT_TRUE(said("type is too large"));
})

// ---- qualified names ----------------------------------------------------------------

TEST(a_module_is_not_a_value_and_not_a_type, {
    begin();
    add("util.ft", "fn one() i32 {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn main() i32 {\n    i32 n = util;\n    return n;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // The diagnostics of the module contract.
    TEST_ASSERT_TRUE(said("'util' is a module, not a value"));
    begin();
    add("util.ft", "fn one() i32 {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn main() i32 {\n    util u = 1;\n    return 0;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("'util' is a module, not a type"));
})

TEST(a_module_without_the_declaration_is_reported, {
    begin();
    add("util.ft", "fn one() i32 {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn main() i32 {\n    return util.two();\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'util' has no declaration named 'two'"));
})

TEST(the_import_bindings_of_a_module_are_not_reachable_through_a_dot, {
    begin();
    add("deep.ft", "enum tone {\n    low,\n    high,\n}\nfn base() i32 {\n    return 1;\n}\n");
    add("mid.ft", "import deep;\nfn step() i32 {\n    return deep.base();\n}\n");
    add("main.ft", "import mid;\nfn main() i32 {\n    return mid.deep.base();\n}\n");
    // Qualified access sees the declarations of `mid`, not its imports: an import binding is not
    // re-exported.
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'mid' has no declaration named 'deep'"));
})

TEST(a_re_exported_enum_member_is_refused_at_the_module_binding, {
    begin();
    add("deep.ft", "enum tone {\n    low,\n    high,\n}\n");
    add("mid.ft", "import deep;\nfn step() i32 {\n    return 1;\n}\n");
    add("main.ft",
        "import mid;\nfn main() i32 {\n"
        "    return cast(mid.deep.tone.high, i32);\n}\n");
    // The leftmost dot fails first, so the member spelling never reaches an
    // enum of a module the file did not import.
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'mid' has no declaration named 'deep'"));
})

TEST(a_symbol_a_module_imported_is_not_one_of_its_declarations, {
    begin();
    add("deep.ft", "fn base() i32 {\n    return 1;\n}\n");
    add("mid.ft", "import deep.base;\nfn step() i32 {\n    return base();\n}\n");
    add("main.ft", "import mid;\nfn main() i32 {\n    return mid.base();\n}\n");
    // The other import binding, with the same answer.
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'mid' has no declaration named 'base'"));
})

TEST(a_module_reaches_the_declarations_of_the_modules_it_imports_itself, {
    begin();
    add("deep.ft", "fn base() i32 {\n    return 1;\n}\n");
    add("mid.ft", "import deep;\nfn step() i32 {\n    return deep.base();\n}\n");
    add("main.ft",
        "import mid;\nimport deep;\n"
        "fn main() i32 {\n    return mid.step() + deep.base();\n}\n");
    // An import does not re-export `deep`.
    // A file that imports `deep` directly can reach it.
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(an_imported_declaration_is_used_through_its_short_name, {
    begin();
    add("util.ft", "i32 SIZE = 3;\nstruct pair {\n    i32 a;\n    i32 b;\n}\n");
    add("main.ft",
        "import util.{SIZE, pair};\n"
        "fn main() i32 {\n    pair p = {1, 2};\n    i32[SIZE] t = {};\n"
        "    return p.a + t[2];\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    TEST_ASSERT_EQ_STR(decl_type("t"), "i32[3]");
})

TEST(a_qualified_enum_member_crosses_modules, {
    begin();
    add("palette.ft", "enum color {\n    red,\n    green,\n}\n");
    add("main.ft",
        "import palette;\n"
        "fn main() i32 {\n    palette.color c = palette.color.green;\n"
        "    return cast(c, i32);\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(an_imported_type_keeps_its_identity, {
    begin();
    add("geom.ft", "struct point {\n    i32 x;\n}\n");
    add("main.ft",
        "import geom;\nstruct point {\n    i32 x;\n}\n"
        "fn main() i32 {\n    geom.point a = {1};\n    point b = a;\n    return b.x;\n}\n");
    // Structs are nominal: two declarations are two types.
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("the initializer expects point, not point"));
})

// ---- functions and aggregates -------------------------------------------------------

TEST(an_aggregate_is_passed_and_returned_by_value, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "fn flip(point p) point {\n    return point{p.y, p.x};\n}\n"
                               "fn main() i32 {\n    point a = {1, 2};\n    point b = flip(a);\n"
                               "    return b.x;\n}\n"));
})

TEST(a_fixed_array_parameter_keeps_its_length, {
    TEST_ASSERT_FALSE(
        check_src("fn first(i32[3] a) i32 {\n    return a[0];\n}\n"
                  "fn main() i32 {\n    i32[2] t = {1, 2};\n    return first(t);\n}\n"));
    // Different `N` are different types.
    TEST_ASSERT_TRUE(said("the argument expects i32[3], not i32[2]"));
})

TEST(a_function_type_ignores_the_binding_mut_of_its_parameters, {
    TEST_ASSERT_TRUE(
        check_src("fn take(i32 mut n) i32 {\n    n = n + 1;\n    return n;\n}\n"
                  "fn main() i32 {\n    fn (i32) i32 f = take;\n    return f(1);\n}\n"));
    // Binding-level `mut` on parameters is not part of the type.
    TEST_ASSERT_EQ_STR(type_text(sym_main("take")->type), "fn (i32) i32");
})

TEST(a_void_parameter_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn f(void v) i32 {\n    return 1;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'void' is only a return type or the base of 'void*'"));
})

TEST(an_extern_signature_takes_scalars_and_pointers, {
    TEST_ASSERT_TRUE(check_src("extern fn write(i32 fd, void* buf, u64 n) i64;\n"
                               "fn main() i32 {\n    return cast(write(1, null, 0), i32);\n}\n"));
    TEST_ASSERT_FALSE(check_src("extern fn take(i32[2] a) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'i32[2]'"));
    // A pointer to the same struct is allowed, since that is how a struct crosses.
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "extern fn take(point p) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'point'"));
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n}\n"
                               "extern fn take(point mut* p) void;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("extern fn take(i32@ xs) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'i32@'"));
    TEST_ASSERT_FALSE(check_src("extern fn take(u8 mut@ own xs) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'u8 mut@ own'"));
    TEST_ASSERT_FALSE(check_src("extern fn grab() string own;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'string own'"));
})

TEST(an_extern_function_pointer_parameter_needs_an_extern_legal_signature, {
    // A `fn R(P...)` parameter is allowed exactly when its own signature is extern-legal: C calls
    // through it with the same convention. A struct or span in it would cross the boundary after
    // all.
    TEST_ASSERT_TRUE(check_src("extern fn sort(void* base, fn (void*, void*) i32 cmp) void;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "extern fn each(fn (point) void cb) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'fn (point) void'"));
})

TEST(an_extern_function_pointer_result_and_nesting_are_checked_too, {
    // Extern validation descends into function-pointer results and nested signatures.
    // C calls through each signature.
    TEST_ASSERT_FALSE(check_src("extern fn make(fn (i32) string f) i32;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'fn (i32) string'"));
    TEST_ASSERT_FALSE(check_src("extern fn nest(fn (fn (string) i32) void h) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'fn (fn (string) i32) void'"));
    // A result position is an extern signature like any other, so a function
    // pointer returned to C is checked the same way.
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "extern fn getter() fn (point) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'fn (point) void'"));
})

TEST(a_fort_signature_may_still_carry_an_aggregate_function_pointer, {
    // The restriction applies only at the C boundary.
    // Inside fort, the calling convention passes the aggregate by a hidden pointer.
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n}\n"
                               "fn one(point p) void {\n    println(p.x);\n}\n"
                               "fn each(fn (point) void cb, point p) void {\n    cb(p);\n}\n"
                               "fn main() i32 {\n    point p = {2};\n"
                               "    each(one, p);\n    return 0;\n}\n"));
})

TEST(an_extern_may_not_declare_the_program_entry_point, {
    TEST_ASSERT_FALSE(check_src("extern fn fort_entry(i32 a, i32 b) i64;\n"
                                "fn main() i32 {\n    return cast(fort_entry(1, 2), i32);\n}\n"));
    // The compiler emits the definition of `fort_entry`. An `extern` declaring it is not a second
    // declaration of one C function: nothing can check the signature against that definition. The
    // call would go through the declared type.
    TEST_ASSERT_TRUE(said("'fort_entry' is reserved: the compiler emits it"));
    // The name is reserved for `extern` alone: a fort function's symbol
    // carries its module path, so it never collides.
    TEST_ASSERT_TRUE(check_src("fn fort_entry() i32 {\n    return 1;\n}\n"
                               "fn main() i32 {\n    return fort_entry();\n}\n"));
})

TEST(a_fort_rt_name_is_an_ordinary_extern_declaration, {
    // The fort runtime occupies no C name. A `fort_rt_` name is an ordinary C symbol and is checked
    // only against its other declarations.
    TEST_ASSERT_TRUE(check_src("extern fn fort_rt_del(i32 wrong) void;\n"
                               "fn main() i32 {\n    fort_rt_del(5);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(check_src("extern fn fort_rt_flush(i32 fd, i32 extra) void;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(check_src("extern fn fort_rt_flush(i32 fd) i32;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(check_src("extern fn fort_rt_exit(i32 status) noreturn;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    // What is refused of such a declaration is what is refused of any other: a
    // type an extern signature may not use.
    TEST_ASSERT_FALSE(check_src("extern fn fort_rt_print_str(i32 fd, string s) void;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'string'"));
})

TEST(main_is_reserved_like_fort_entry, {
    // The compiler emits `main(argc, argv)` in the entry module. An `extern` declaring the name is
    // not a second declaration of one C function but a signature nothing can check against that
    // definition.
    TEST_ASSERT_FALSE(check_src("extern fn main(i32 argc, char* mut* argv) i32;\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'main' is reserved: the compiler emits it"));
    // The name is reserved for `extern` alone: the entry module's own `fn i32
    // main` is the symbol `<entry>.main` and collides with nothing.
    TEST_ASSERT_TRUE(check_src("fn main() i32 {\n    return 0;\n}\n"));
})

TEST(a_declaration_of_a_libc_symbol_is_held_against_the_librarys, {
    // `std.rt` is a root of every closure and imports `std.libc`. A program that declares one of
    // those C symbols itself must agree with the library's declaration of it, `own` included. The
    // The test reads `std/libc.ft` directly, so it checks the current library declaration.
    TEST_ASSERT_FALSE(check_src_with_library("extern fn free(void* p) void;\n"
                                             "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'free'"));
    TEST_ASSERT_TRUE(said("previous declaration of 'free' here"));
    // An identical extern declaration is accepted.
    // Only a signature disagreement is an error.
    TEST_ASSERT_TRUE(check_src_with_library("extern fn free(void* own p) void;\n"
                                            "fn main() i32 {\n    return 0;\n}\n"));
    // A C symbol that the library does not declare has no conflicting declaration.
    TEST_ASSERT_TRUE(check_src_with_library("extern fn puts(char* s) i32;\n"
                                            "fn main() i32 {\n    return 0;\n}\n"));
})

TEST(an_immutable_buffer_is_refused_at_every_libc_entry_that_writes, {
    // The library reaches the real `std/libc.ft`. This is its signatures and not a copy of them:
    // `read`, `memset` and `memcpy` take a `u8 mut*` and `write` and `memcmp` a `u8*`. The count is
    // what the corpus cannot hold -- `judge_fail` groups by (file, line).
    // `fail/ffi/007_immutable_buffer_destination.ft` keeps the text and the position of each
    // message and this keeps the number.
    TEST_ASSERT_FALSE(
        check_src_with_library("import std.libc;\n"
                               "fn main() i32 {\n"
                               "    u8[8] frozen = {1, 2, 3, 4, 5, 6, 7, 8};\n"
                               "    u8@ view = frozen[..];\n"
                               "    i64 n = libc.read(0, view.ptr, view.len);\n"
                               "    libc.memset(view.ptr, 0, view.len);\n"
                               "    libc.memcpy(view.ptr, view.ptr, view.len);\n"
                               "    i64 w = libc.write(1, view.ptr, view.len);\n"
                               "    i32 c = libc.memcmp(view.ptr, view.ptr, view.len);\n"
                               "    return cast(n + w, i32) + c;\n"
                               "}\n"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)3);
    TEST_ASSERT_TRUE(said("the argument expects u8 mut*, not u8*"));
    // The two read-only functions accept the same span without a diagnostic.
    // The other three functions require a mutable buffer.
    TEST_ASSERT_TRUE(
        check_src_with_library("import std.libc;\n"
                               "fn main() i32 {\n"
                               "    u8[8] frozen = {1, 2, 3, 4, 5, 6, 7, 8};\n"
                               "    u8@ view = frozen[..];\n"
                               "    i64 w = libc.write(1, view.ptr, view.len);\n"
                               "    i32 c = libc.memcmp(view.ptr, view.ptr, view.len);\n"
                               "    return cast(w, i32) + c;\n"
                               "}\n"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)0);
})

TEST(the_runtime_allocator_hands_the_caller_storage_it_may_write, {
    // `std.rt.alloc` answers `void mut* own` (the runtime interface). The real `std/rt.ft` is read
    // here rather than a copy of its text. A callee that writes the block takes it as it comes: no
    // cast, and in particular no cast that adds `mut`.
    TEST_ASSERT_TRUE(
        check_src_with_library("import std.rt;\n"
                               "fn fill(void mut* p) void { }\n"
                               "fn read_only(void* p) void { }\n"
                               "fn main() i32 {\n"
                               "    void mut* own p = rt.alloc(1, 8, \"m\".ptr, 1, 1);\n"
                               "    fill(p);\n"
                               "    read_only(p);\n"
                               "    rt.free(move(p));\n"
                               "    return 0;\n"
                               "}\n"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)0);
    TEST_ASSERT_EQ_STR(decl_type("p"), "void mut* own");
    // The monotone drop runs one way. A caller that binds the result as `void* own` discards the
    // mark and cannot recover it. The count is what the corpus cannot hold: `judge_fail` groups by
    // (file, line).
    TEST_ASSERT_FALSE(check_src_with_library("import std.rt;\n"
                                             "fn fill(void mut* p) void { }\n"
                                             "fn main() i32 {\n"
                                             "    void* own p = rt.alloc(1, 8, \"m\".ptr, 1, 1);\n"
                                             "    fill(p);\n"
                                             "    rt.free(move(p));\n"
                                             "    return 0;\n"
                                             "}\n"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("the argument expects void mut*, not void* own"));
})

TEST(a_fort_rt_name_keeps_its_own_signature, {
    // No table holds it: a C symbol of that name is an ordinary extern,
    // whatever it is called.
    TEST_ASSERT_TRUE(check_src("extern fn fort_rt_helper(string* s) i32;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(check_src("extern fn write(i32 fd, void* buf, u64 n) i64;\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
})

TEST(a_noreturn_function_pointer_keeps_its_type, {
    TEST_ASSERT_TRUE(check_src("fn die(string msg) noreturn {\n    panic(msg);\n}\n"
                               "fn main() i32 {\n    fn (string) noreturn f = die;\n"
                               "    println(f);\n    return 0;\n}\n"));
    // `noreturn` is part of a function type's identity, so the written type of
    // the binding is the function's own.
    TEST_ASSERT_EQ_STR(type_text(sym_main("die")->type), "fn (string) noreturn");
    TEST_ASSERT_EQ_STR(decl_type("f"), "fn (string) noreturn");
    TEST_ASSERT_FALSE(check_src("fn die(string msg) void {\n    println(msg);\n}\n"
                                "fn main() i32 {\n    fn (string) noreturn f = die;\n"
                                "    println(f);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("expects fn (string) noreturn, not fn (string) void"));
})

// ---- imports that failed -------------------------------------------------------------

TEST(an_importer_is_checked_although_its_import_did_not_parse, {
    begin();
    add("util.ft", "fn one( {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn main() i32 {\n    bool z = 1;\n    return util.one();\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // A file with a syntax error is not checked; every other module is, so the
    // importer reports its own error.
    TEST_ASSERT_TRUE(said("an integer constant does not become bool"));
    // The loader reported the import, so the name it did not bind says
    // nothing further.
    TEST_ASSERT_FALSE(said("unknown name 'util'"));
})

TEST(a_failed_import_silences_every_use_of_its_name, {
    begin();
    add("main.ft",
        "import nothere;\nfn main() i32 {\n    nothere.point p = {1};\n"
        "    return nothere.one() + p.x;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("not found"));
    TEST_ASSERT_FALSE(said("unknown name 'nothere'"));
    TEST_ASSERT_FALSE(said("unknown type 'nothere'"));
})

TEST(a_multi_segment_import_path_names_its_module, {
    begin();
    add("util/strings.ft", "i32 LEN = 3;\nfn helper() i32 {\n    return LEN;\n}\n");
    add("main.ft",
        "import util.strings;\nimport util.strings.helper;\n"
        "fn main() i32 {\n    return strings.LEN + helper();\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    const sym_t* module = module_at("util.strings")->ast->sym;
    ast_node_t* mod = module_at("main")->ast;
    const ast_node_t* first = ast_child(mod, 0);
    const ast_node_t* second = ast_child(mod, 1);
    // The last segment of a module reading names the module; of a symbol
    // reading, the declaration, with the module on the segment before it. The
    // segments before those name directories and carry nothing.
    TEST_ASSERT_TRUE(ast_child(first->a, 1)->sym == module);
    TEST_ASSERT_NULL(ast_child(first->a, 0)->sym);
    TEST_ASSERT_TRUE(ast_child(second->a, 2)->sym == sym_of("util.strings", "helper"));
    TEST_ASSERT_TRUE(ast_child(second->a, 1)->sym == module);
    TEST_ASSERT_NULL(unresolved_name(mod));
})

// ---- sizes and layout ---------------------------------------------------------------

TEST(sizeof_gives_the_size_of_every_kind, {
    TEST_ASSERT_TRUE(check_src("struct pad {\n    i8 a;\n    i64 b;\n    i8 c;\n}\n"
                               "enum color {\n    red,\n}\n"
                               "u64 BOOL_SIZE = sizeof(bool);\n"
                               "u64 CHAR_SIZE = sizeof(char);\n"
                               "u64 PTR_SIZE = sizeof(i32*);\n"
                               "u64 FN_SIZE = sizeof(fn (i32) i32);\n"
                               "u64 SPAN_SIZE = sizeof(i32@);\n"
                               "u64 STRING_SIZE = sizeof(string);\n"
                               "u64 ENUM_SIZE = sizeof(color);\n"
                               "u64 ARRAY_SIZE = sizeof(i32[4]);\n"
                               "u64 STRUCT_SIZE = sizeof(pad);\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("BOOL_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)1);
    TEST_ASSERT_TRUE(init_int("CHAR_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)1);
    TEST_ASSERT_TRUE(init_int("PTR_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)8);
    // A function pointer is 8, a span and a string 16, an enum 4.
    TEST_ASSERT_TRUE(init_int("FN_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)8);
    TEST_ASSERT_TRUE(init_int("SPAN_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)16);
    TEST_ASSERT_TRUE(init_int("STRING_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)16);
    TEST_ASSERT_TRUE(init_int("ENUM_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)4);
    TEST_ASSERT_TRUE(init_int("ARRAY_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)16);
    // C/System V layout: 1 byte, 7 of padding, 8, 1 and 7 more to the
    // alignment.
    TEST_ASSERT_TRUE(init_int("STRUCT_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)24);
})

TEST(a_field_offset_follows_the_c_layout, {
    TEST_ASSERT_TRUE(check_src("struct pad {\n    i8 a;\n    i64 b;\n    i8 c;\n}\n"
                               "fn main() i32 {\n    pad p = {1, 2, 3};\n"
                               "    return cast(p.a, i32);\n}\n"));
    // Fields in order, each at the next multiple of its alignment.
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "a")->aux, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "b")->aux, (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "c")->aux, (uint64_t)16);
})

TEST(a_struct_of_a_struct_is_laid_out_once, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "struct line {\n    point a;\n    point b;\n}\n"
                               "u64 SIZE = sizeof(line);\n"
                               "fn main() i32 {\n    line l = {{1, 2}, {3, 4}};\n"
                               "    return l.b.y;\n}\n"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)16);
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "b")->aux, (uint64_t)8);
})

// ---- the print family (8.3) ---------------------------------------------------------

TEST(every_printable_type_prints, {
    TEST_ASSERT_TRUE(
        check_src("enum color {\n    red,\n}\n"
                  "fn id(i32 n) i32 {\n    return n;\n}\n"
                  "fn main() i32 {\n    i32 mut v = 1;\n"
                  "    println(1, 'a', true, \"s\", color.red, &v, id, cast(0, u64));\n"
                  "    void* o = null;\n    println(o);\n    return 0;\n}\n"));
})

TEST(an_array_and_a_span_are_not_printable, {
    TEST_ASSERT_FALSE(check_body("    i32[2] a = {1, 2};\n    println(a);"));
    TEST_ASSERT_TRUE(said("cannot print a value of type i32[2]"));
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    println(s);"));
    TEST_ASSERT_TRUE(said("cannot print a value of type i32@"));
})

// ---- poisoning ----------------------------------------------------------------------

TEST(a_failed_type_silences_the_declarations_that_use_it, {
    TEST_ASSERT_FALSE(check_src("struct bad {\n    nope x;\n}\n"
                                "fn main() i32 {\n    bad b = {1};\n    return b.x;\n}\n"));
    // The field failed, so the struct and every use of it are poisoned: one
    // diagnostic.
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("unknown type 'nope'"));
})

TEST(a_failed_function_silences_its_calls, {
    TEST_ASSERT_FALSE(check_src("fn f() nope {\n    return 1;\n}\n"
                                "fn main() i32 {\n    return f() + 1;\n}\n"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_failed_local_silences_its_uses, {
    TEST_ASSERT_FALSE(check_body("    i32 x = nope;\n    i32 y = x + 1;\n    println(y);"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(each_module_reports_its_own_errors, {
    begin();
    add("a.ft", "i32 A = nope;\n");
    add("b.ft", "import a;\ni32 B = bad;\n");
    add("main.ft", "import b;\nfn main() i32 {\n    return b.B;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // Every module of the closure is checked, in dependency order.
    TEST_ASSERT_TRUE(said("a.ft:1:9: error: unknown name 'nope'"));
    TEST_ASSERT_TRUE(said("b.ft:2:9: error: unknown name 'bad'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)2);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_conv", argc, argv);
    TEST_RUN(mutability_drops_at_level_one);
    TEST_RUN(mutability_is_never_added_implicitly);
    TEST_RUN(a_drop_behind_a_mutable_level_is_refused);
    TEST_RUN(the_drop_rule_applies_to_arguments_and_returns);
    TEST_RUN(level_zero_is_unconstrained);
    TEST_RUN(ownership_drops_where_mutability_does);
    TEST_RUN(ownership_is_never_added_implicitly);
    TEST_RUN(an_owning_span_lends_its_elements);
    TEST_RUN(the_binding_marker_says_what_may_be_rebound);
    TEST_RUN(a_marker_behind_an_indirection_says_what_may_be_written);
    TEST_RUN(a_cast_converts_between_pointers_and_integers);
    TEST_RUN(a_cast_adds_ownership_and_never_mutability);
    TEST_RUN(a_cast_converts_among_string_and_byte_spans);
    TEST_RUN(a_cast_never_changes_a_span_element_type);
    TEST_RUN(a_cast_from_a_pointer_to_a_span_is_refused);
    TEST_RUN(a_cast_to_a_struct_or_an_array_is_refused);
    TEST_RUN(a_cast_between_an_enum_and_an_integer_is_allowed);
    TEST_RUN(a_cast_between_char_and_an_integer_is_allowed);
    TEST_RUN(a_cast_target_that_is_too_large_is_refused);
    TEST_RUN(a_module_is_not_a_value_and_not_a_type);
    TEST_RUN(a_module_without_the_declaration_is_reported);
    TEST_RUN(the_import_bindings_of_a_module_are_not_reachable_through_a_dot);
    TEST_RUN(a_re_exported_enum_member_is_refused_at_the_module_binding);
    TEST_RUN(a_symbol_a_module_imported_is_not_one_of_its_declarations);
    TEST_RUN(a_module_reaches_the_declarations_of_the_modules_it_imports_itself);
    TEST_RUN(an_imported_declaration_is_used_through_its_short_name);
    TEST_RUN(a_qualified_enum_member_crosses_modules);
    TEST_RUN(an_imported_type_keeps_its_identity);
    TEST_RUN(an_aggregate_is_passed_and_returned_by_value);
    TEST_RUN(a_fixed_array_parameter_keeps_its_length);
    TEST_RUN(a_function_type_ignores_the_binding_mut_of_its_parameters);
    TEST_RUN(a_void_parameter_is_refused);
    TEST_RUN(an_extern_signature_takes_scalars_and_pointers);
    TEST_RUN(an_extern_function_pointer_parameter_needs_an_extern_legal_signature);
    TEST_RUN(an_extern_function_pointer_result_and_nesting_are_checked_too);
    TEST_RUN(a_fort_signature_may_still_carry_an_aggregate_function_pointer);
    TEST_RUN(an_extern_may_not_declare_the_program_entry_point);
    TEST_RUN(a_fort_rt_name_is_an_ordinary_extern_declaration);
    TEST_RUN(main_is_reserved_like_fort_entry);
    TEST_RUN(a_declaration_of_a_libc_symbol_is_held_against_the_librarys);
    TEST_RUN(an_immutable_buffer_is_refused_at_every_libc_entry_that_writes);
    TEST_RUN(the_runtime_allocator_hands_the_caller_storage_it_may_write);
    TEST_RUN(a_fort_rt_name_keeps_its_own_signature);
    TEST_RUN(a_noreturn_function_pointer_keeps_its_type);
    TEST_RUN(an_importer_is_checked_although_its_import_did_not_parse);
    TEST_RUN(a_failed_import_silences_every_use_of_its_name);
    TEST_RUN(a_multi_segment_import_path_names_its_module);
    TEST_RUN(sizeof_gives_the_size_of_every_kind);
    TEST_RUN(a_field_offset_follows_the_c_layout);
    TEST_RUN(a_struct_of_a_struct_is_laid_out_once);
    TEST_RUN(every_printable_type_prints);
    TEST_RUN(an_array_and_a_span_are_not_printable);
    TEST_RUN(a_failed_type_silences_the_declarations_that_use_it);
    TEST_RUN(a_failed_function_silences_its_calls);
    TEST_RUN(a_failed_local_silences_its_uses);
    TEST_RUN(each_module_reports_its_own_errors);
    check_reset();
    done();
    TEST_EXIT();
}
