// Unit tests of the conversions the checker applies (core-language.md 3.4,
// 3.9, 5.8, 5.9; D5.4, D3.14, D17.4): where the implicit drops of mutability
// and ownership apply, what the cast matrix allows there, and the qualified
// names and poisoning that go with them. The operand rules are in
// check_expr_test.c.
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- dropping mutability (D5.4) -----------------------------------------------------

TEST(mutability_drops_at_level_one, {
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32 mut* p = &v;\n    i32* q = p;\n"
                                "    println(q);"));
})

TEST(mutability_is_never_added_implicitly, {
    TEST_ASSERT_FALSE(check_body("    i32 mut v = 1;\n    i32* p = &v;\n    i32 mut* q = p;\n"
                                 "    println(q);"));
    // Adding mutability requires a cast (D5.4, D3.14).
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut*, not i32*"));
})

TEST(a_drop_behind_a_mutable_level_is_refused, {
    TEST_ASSERT_TRUE(check_src("struct node {\n    i32 v;\n}\n"
                               "fn i32 main() {\n"
                               "    node mut* mut@ own a = new(node*, 1);\n"
                               "    node*@ b = a;\n    println(b.len);\n    del(a);\n"
                               "    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("struct node {\n    i32 v;\n}\n"
                                "fn i32 main() {\n"
                                "    node mut* mut@ own a = new(node*, 1);\n"
                                "    node* mut@ c = a;\n    println(c.len);\n    del(a);\n"
                                "    return 0;\n}\n"));
    // A mutable slot could then hold a pointer to what the source still sees
    // as mutable: the C `T** -> const T**` hole (D5.4).
    TEST_ASSERT_TRUE(said("expects node* mut@, not node mut* mut@ own"));
})

TEST(the_drop_rule_applies_to_arguments_and_returns, {
    TEST_ASSERT_TRUE(check_src("fn void look(i32* p) {\n    println(p);\n}\n"
                               "fn i32* view(i32 mut* p) {\n    return p;\n}\n"
                               "fn i32 main() {\n    i32 mut v = 1;\n    look(&v);\n"
                               "    println(view(&v));\n    return 0;\n}\n"));
    TEST_ASSERT_FALSE(check_src("fn void write(i32 mut* p) {\n    println(p);\n}\n"
                                "fn i32 main() {\n    i32 v = 1;\n    write(&v);\n"
                                "    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("the argument expects i32 mut*, not i32*"));
})

TEST(level_zero_is_unconstrained, {
    // Level 0 of the receiving binding is unconstrained (D5.4).
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32* mut p = &v;\n    i32* q = p;\n"
                                "    p = q;\n    println(q);"));
})

// ---- dropping ownership (D17.4) -----------------------------------------------------

TEST(ownership_drops_where_mutability_does, {
    TEST_ASSERT_TRUE(check_body("    i32 mut* own p = new(i32);\n    i32 mut* v = p;\n"
                                "    i32* w = p;\n    println(v, w);\n    del(p);"));
})

TEST(ownership_is_never_added_implicitly, {
    TEST_ASSERT_FALSE(check_body("    i32 mut v = 1;\n    i32 mut* own p = &v;\n    del(p);"));
    // `&` yields a borrowed pointer; adding `own` needs a cast (D17.3, D3.14).
    TEST_ASSERT_TRUE(said("the initializer expects i32 mut* own, not i32 mut*"));
})

TEST(an_owning_span_lends_its_elements, {
    TEST_ASSERT_TRUE(check_body("    i32 mut@ own s = new(i32, 2);\n    i32 mut@ v = s;\n"
                                "    i32@ w = s;\n    println(v.len, w.len);\n    del(s);"));
})

// ---- the marker table of D5.3 -------------------------------------------------------

TEST(the_binding_marker_says_what_may_be_rebound, {
    // Every row of the table of D5.3, left column: the marker before the name
    // is the binding's own storage.
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
    // Every row of the table of D5.3, right column: a `mut` after a `*` or an
    // `@` marks the reference, one after the base type marks what it reaches.
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
    // The out-parameter shapes of D3.6.
    TEST_ASSERT_TRUE(check_node_body("    u8 mut@ mut b = {};\n    u8 mut@ mut* out = &b;\n"
                                     "    (*out)[0] = 1;"));
    TEST_ASSERT_FALSE(check_node_body("    u8@ mut b = {};\n    u8@ mut* out = &b;\n"
                                      "    (*out)[0] = 1;"));
    // A fixed array of pointers: the slots follow the array, the pointees
    // their own marker.
    TEST_ASSERT_TRUE(check_node_body("    node*[4] mut t = {};\n    t[0] = &k;"));
    TEST_ASSERT_FALSE(check_node_body("    node*[4] mut t = {};\n    t[0]->value = 1;"));
    // A string's characters are never mutable (D3.7, D5.2).
    TEST_ASSERT_FALSE(check_node_body("    string mut s = \"a\";\n    s[0] = 'b';"));
})

// ---- the cast matrix (D3.14) --------------------------------------------------------

TEST(a_cast_converts_between_pointers_and_integers, {
    TEST_ASSERT_TRUE(check_body("    i32 mut v = 1;\n    i32 mut* p = &v;\n"
                                "    u8 mut* b = cast(p, u8 mut*);\n    u64 n = cast(p, u64);\n"
                                "    void* o = cast(p, void*);\n    println(b, n, o);"));
})

TEST(a_cast_adds_mutability_and_ownership, {
    TEST_ASSERT_TRUE(check_body("    i32 v = 1;\n    i32* p = &v;\n"
                                "    i32 mut* w = cast(p, i32 mut*);\n    println(w);"));
    TEST_ASSERT_TRUE(check_body("    u8 mut* p = null;\n"
                                "    u8 mut* own a = cast(p, u8 mut* own);\n    println(a);"));
})

TEST(a_cast_converts_among_string_and_byte_spans, {
    TEST_ASSERT_TRUE(check_body("    string s = \"ab\";\n    u8@ b = cast(s, u8@);\n"
                                "    string t = cast(b, string);\n    println(b.len, t);"));
})

TEST(a_cast_never_changes_a_span_element_type, {
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    u32@ u = cast(s, u32@);\n"
                                 "    println(u.len);"));
    // The element type of a span never changes, because `len` counts elements
    // (D3.14).
    TEST_ASSERT_TRUE(said("cannot cast i32@ to u32@"));
})

TEST(a_cast_from_a_pointer_to_a_span_is_refused, {
    TEST_ASSERT_FALSE(check_body("    i32 mut v = 1;\n    i32* p = &v;\n"
                                 "    i32@ s = cast(p, i32@);\n    println(s.len);"));
    TEST_ASSERT_TRUE(said("cannot cast i32* to i32@"));
})

TEST(a_cast_to_a_struct_or_an_array_is_refused, {
    TEST_ASSERT_FALSE(check_src("struct point {\n    i32 x;\n}\n"
                                "fn i32 main() {\n    i32 n = 1;\n    point p = cast(n, point);\n"
                                "    return p.x;\n}\n"));
    TEST_ASSERT_TRUE(said("cannot cast i32 to point"));
})

TEST(a_cast_between_an_enum_and_an_integer_is_allowed, {
    TEST_ASSERT_TRUE(check_src("enum color {\n    red,\n    green,\n}\n"
                               "fn i32 main() {\n    color c = cast(1, color);\n"
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

// ---- qualified names (D9.4) ---------------------------------------------------------

TEST(a_module_is_not_a_value_and_not_a_type, {
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn i32 main() {\n    i32 n = util;\n    return n;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // The diagnostics of module-system.md 13.
    TEST_ASSERT_TRUE(said("'util' is a module, not a value"));
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn i32 main() {\n    util u = 1;\n    return 0;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("'util' is a module, not a type"));
})

TEST(a_module_without_the_declaration_is_reported, {
    begin();
    add("util.ft", "fn i32 one() {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn i32 main() {\n    return util.two();\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'util' has no declaration named 'two'"));
})

TEST(the_import_bindings_of_a_module_are_not_reachable_through_a_dot, {
    begin();
    add("deep.ft", "enum tone {\n    low,\n    high,\n}\nfn i32 base() {\n    return 1;\n}\n");
    add("mid.ft", "import deep;\nfn i32 step() {\n    return deep.base();\n}\n");
    add("main.ft", "import mid;\nfn i32 main() {\n    return mid.deep.base();\n}\n");
    // Qualified access sees the declarations of `mid`, not its imports: an
    // import binding is not re-exported (D9.3, module-system.md 4).
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'mid' has no declaration named 'deep'"));
})

TEST(a_re_exported_enum_member_is_refused_at_the_module_binding, {
    begin();
    add("deep.ft", "enum tone {\n    low,\n    high,\n}\n");
    add("mid.ft", "import deep;\nfn i32 step() {\n    return 1;\n}\n");
    add("main.ft",
        "import mid;\nfn i32 main() {\n"
        "    return cast(mid.deep.tone.high, i32);\n}\n");
    // The leftmost dot fails first, so the member spelling of D3.9 never
    // reaches an enum of a module the file did not import.
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'mid' has no declaration named 'deep'"));
})

TEST(a_symbol_a_module_imported_is_not_one_of_its_declarations, {
    begin();
    add("deep.ft", "fn i32 base() {\n    return 1;\n}\n");
    add("mid.ft", "import deep::base;\nfn i32 step() {\n    return base();\n}\n");
    add("main.ft", "import mid;\nfn i32 main() {\n    return mid.base();\n}\n");
    // The other import binding of D9.3, with the same answer.
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("module 'mid' has no declaration named 'base'"));
})

TEST(a_module_reaches_the_declarations_of_the_modules_it_imports_itself, {
    begin();
    add("deep.ft", "fn i32 base() {\n    return 1;\n}\n");
    add("mid.ft", "import deep;\nfn i32 step() {\n    return deep.base();\n}\n");
    add("main.ft",
        "import mid;\nimport deep;\n"
        "fn i32 main() {\n    return mid.step() + deep.base();\n}\n");
    // The rule bars the re-export, not the module: a file that imports `deep`
    // itself reaches it (D9.3).
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(an_imported_declaration_is_used_through_its_short_name, {
    begin();
    add("util.ft", "i32 SIZE = 3;\nstruct pair {\n    i32 a;\n    i32 b;\n}\n");
    add("main.ft",
        "import util::{SIZE, pair};\n"
        "fn i32 main() {\n    pair p = {1, 2};\n    i32[SIZE] t = {};\n"
        "    return p.a + t[2];\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    TEST_ASSERT_EQ_STR(decl_type("t"), "i32[3]");
})

TEST(a_qualified_enum_member_crosses_modules, {
    begin();
    add("palette.ft", "enum color {\n    red,\n    green,\n}\n");
    add("main.ft",
        "import palette;\n"
        "fn i32 main() {\n    palette.color c = palette.color.green;\n"
        "    return cast(c, i32);\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
})

TEST(an_imported_type_keeps_its_identity, {
    begin();
    add("geom.ft", "struct point {\n    i32 x;\n}\n");
    add("main.ft",
        "import geom;\nstruct point {\n    i32 x;\n}\n"
        "fn i32 main() {\n    geom.point a = {1};\n    point b = a;\n    return b.x;\n}\n");
    // Structs are nominal: two declarations are two types (D3.8, D3.12).
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("the initializer expects point, not point"));
})

// ---- functions and aggregates (D8.2, D9.9) ------------------------------------------

TEST(an_aggregate_is_passed_and_returned_by_value, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "fn point flip(point p) {\n    return point{p.y, p.x};\n}\n"
                               "fn i32 main() {\n    point a = {1, 2};\n    point b = flip(a);\n"
                               "    return b.x;\n}\n"));
})

TEST(a_fixed_array_parameter_keeps_its_length, {
    TEST_ASSERT_FALSE(
        check_src("fn i32 first(i32[3] a) {\n    return a[0];\n}\n"
                  "fn i32 main() {\n    i32[2] t = {1, 2};\n    return first(t);\n}\n"));
    // Different `N` are different types (D3.4).
    TEST_ASSERT_TRUE(said("the argument expects i32[3], not i32[2]"));
})

TEST(a_function_type_ignores_the_binding_mut_of_its_parameters, {
    TEST_ASSERT_TRUE(
        check_src("fn i32 take(i32 mut n) {\n    n = n + 1;\n    return n;\n}\n"
                  "fn i32 main() {\n    fn i32(i32) f = take;\n    return f(1);\n}\n"));
    // Binding-level `mut` on parameters is not part of the type (D3.10).
    TEST_ASSERT_EQ_STR(type_text(sym_main("take")->type), "fn i32(i32)");
})

TEST(a_void_parameter_is_refused, {
    TEST_ASSERT_FALSE(check_src("fn i32 f(void v) {\n    return 1;\n}\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("'void' is only a return type or the base of 'void*'"));
})

TEST(an_extern_signature_takes_scalars_and_pointers, {
    TEST_ASSERT_TRUE(check_src("extern fn i64 write(i32 fd, void* buf, u64 n);\n"
                               "fn i32 main() {\n    return cast(write(1, null, 0), i32);\n}\n"));
    TEST_ASSERT_FALSE(check_src("extern fn void take(i32[2] a);\n"
                                "fn i32 main() {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("extern signature cannot use type 'i32[2]'"));
})

TEST(an_extern_may_not_declare_the_program_entry_point, {
    TEST_ASSERT_FALSE(check_src("extern fn i64 fort_entry(i32 a, i32 b);\n"
                                "fn i32 main() {\n    return cast(fort_entry(1, 2), i32);\n}\n"));
    // The compiler emits the definition of `fort_entry` (D11.6), so an
    // `extern` declaring it is not a second declaration of one C function:
    // nothing can check the signature against that definition, and the call
    // would go through the declared type (D9.7, module-system.md 13).
    TEST_ASSERT_TRUE(said("'fort_entry' is reserved: the compiler emits it"));
    // The name is reserved for `extern` alone: a fort function's symbol
    // carries its module path, so it never collides (D9.7).
    TEST_ASSERT_TRUE(check_src("fn i32 fort_entry() {\n    return 1;\n}\n"
                               "fn i32 main() {\n    return fort_entry();\n}\n"));
})

TEST(a_noreturn_function_pointer_keeps_its_type, {
    TEST_ASSERT_TRUE(check_src("fn noreturn die(string msg) {\n    panic(msg);\n}\n"
                               "fn i32 main() {\n    fn noreturn(string) f = die;\n"
                               "    println(f);\n    return 0;\n}\n"));
    // `noreturn` is part of a function type's identity (D3.10, D8.5), so the
    // written type of the binding is the function's own.
    TEST_ASSERT_EQ_STR(type_text(sym_main("die")->type), "fn noreturn(string)");
    TEST_ASSERT_EQ_STR(decl_type("f"), "fn noreturn(string)");
    TEST_ASSERT_FALSE(check_src("fn void die(string msg) {\n    println(msg);\n}\n"
                                "fn i32 main() {\n    fn noreturn(string) f = die;\n"
                                "    println(f);\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("expects fn noreturn(string), not fn void(string)"));
})

// ---- imports that failed (D14.2, D20.1) ----------------------------------------------

TEST(an_importer_is_checked_although_its_import_did_not_parse, {
    begin();
    add("util.ft", "fn i32 one( {\n    return 1;\n}\n");
    add("main.ft", "import util;\nfn i32 main() {\n    bool z = 1;\n    return util.one();\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // A file with a syntax error is not checked; every other module is, so
    // the importer reports its own error (D14.2).
    TEST_ASSERT_TRUE(said("an integer constant does not become bool"));
    // The loader reported the import, so the name it did not bind says
    // nothing further.
    TEST_ASSERT_FALSE(said("unknown name 'util'"));
})

TEST(a_failed_import_silences_every_use_of_its_name, {
    begin();
    add("main.ft",
        "import nothere;\nfn i32 main() {\n    nothere.point p = {1};\n"
        "    return nothere.one() + p.x;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    TEST_ASSERT_TRUE(said("not found"));
    TEST_ASSERT_FALSE(said("unknown name 'nothere'"));
    TEST_ASSERT_FALSE(said("unknown type 'nothere'"));
})

TEST(a_multi_segment_import_path_names_its_module, {
    begin();
    add("util/strings.ft", "i32 LEN = 3;\nfn i32 helper() {\n    return LEN;\n}\n");
    add("main.ft",
        "import util::strings;\nimport util::strings::helper;\n"
        "fn i32 main() {\n    return strings.LEN + helper();\n}\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    const sym_t* module = module_at("util::strings")->ast->sym;
    ast_node_t* mod = module_at("main")->ast;
    const ast_node_t* first = ast_child(mod, 0);
    const ast_node_t* second = ast_child(mod, 1);
    // The last segment of a module reading names the module; of a symbol
    // reading, the declaration, with the module on the segment before it
    // (D9.3). The segments before those name directories and carry nothing.
    TEST_ASSERT_TRUE(ast_child(first->a, 1)->sym == module);
    TEST_ASSERT_NULL(ast_child(first->a, 0)->sym);
    TEST_ASSERT_TRUE(ast_child(second->a, 2)->sym == sym_of("util::strings", "helper"));
    TEST_ASSERT_TRUE(ast_child(second->a, 1)->sym == module);
    TEST_ASSERT_NULL(unresolved_name(mod));
})

// ---- sizes and layout (D3.1, D3.8, D3.15) -------------------------------------------

TEST(sizeof_gives_the_size_of_every_kind, {
    TEST_ASSERT_TRUE(check_src("struct pad {\n    i8 a;\n    i64 b;\n    i8 c;\n}\n"
                               "enum color {\n    red,\n}\n"
                               "u64 BOOL_SIZE = sizeof(bool);\n"
                               "u64 CHAR_SIZE = sizeof(char);\n"
                               "u64 PTR_SIZE = sizeof(i32*);\n"
                               "u64 FN_SIZE = sizeof(fn i32(i32));\n"
                               "u64 SPAN_SIZE = sizeof(i32@);\n"
                               "u64 STRING_SIZE = sizeof(string);\n"
                               "u64 ENUM_SIZE = sizeof(color);\n"
                               "u64 ARRAY_SIZE = sizeof(i32[4]);\n"
                               "u64 STRUCT_SIZE = sizeof(pad);\n"
                               "fn i32 main() {\n    return 0;\n}\n"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("BOOL_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)1);
    TEST_ASSERT_TRUE(init_int("CHAR_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)1);
    TEST_ASSERT_TRUE(init_int("PTR_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)8);
    // A function pointer is 8, a span and a string 16, an enum 4 (D3.15).
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
    // alignment (D3.8).
    TEST_ASSERT_TRUE(init_int("STRUCT_SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)24);
})

TEST(a_field_offset_follows_the_c_layout, {
    TEST_ASSERT_TRUE(check_src("struct pad {\n    i8 a;\n    i64 b;\n    i8 c;\n}\n"
                               "fn i32 main() {\n    pad p = {1, 2, 3};\n"
                               "    return cast(p.a, i32);\n}\n"));
    // Fields in order, each at the next multiple of its alignment (D3.8).
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "a")->aux, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "b")->aux, (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "c")->aux, (uint64_t)16);
})

TEST(a_struct_of_a_struct_is_laid_out_once, {
    TEST_ASSERT_TRUE(check_src("struct point {\n    i32 x;\n    i32 y;\n}\n"
                               "struct line {\n    point a;\n    point b;\n}\n"
                               "u64 SIZE = sizeof(line);\n"
                               "fn i32 main() {\n    line l = {{1, 2}, {3, 4}};\n"
                               "    return l.b.y;\n}\n"));
    int64_t v = 0;
    TEST_ASSERT_TRUE(init_int("SIZE", &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)16);
    TEST_ASSERT_EQ_UINT64(node_in_main(AST_FIELD_DECL, "b")->aux, (uint64_t)8);
})

// ---- the print family (D12.2, 8.3) --------------------------------------------------

TEST(every_printable_type_prints, {
    TEST_ASSERT_TRUE(
        check_src("enum color {\n    red,\n}\n"
                  "fn i32 id(i32 n) {\n    return n;\n}\n"
                  "fn i32 main() {\n    i32 mut v = 1;\n"
                  "    println(1, 'a', true, \"s\", color.red, &v, id, cast(0, u64));\n"
                  "    void* o = null;\n    println(o);\n    return 0;\n}\n"));
})

TEST(an_array_and_a_span_are_not_printable, {
    TEST_ASSERT_FALSE(check_body("    i32[2] a = {1, 2};\n    println(a);"));
    TEST_ASSERT_TRUE(said("cannot print a value of type i32[2]"));
    TEST_ASSERT_FALSE(check_body("    i32@ s = {};\n    println(s);"));
    TEST_ASSERT_TRUE(said("cannot print a value of type i32@"));
})

// ---- poisoning (D14.2) --------------------------------------------------------------

TEST(a_failed_type_silences_the_declarations_that_use_it, {
    TEST_ASSERT_FALSE(check_src("struct bad {\n    nope x;\n}\n"
                                "fn i32 main() {\n    bad b = {1};\n    return b.x;\n}\n"));
    // The field failed, so the struct and every use of it are poisoned: one
    // diagnostic (D14.2).
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("unknown type 'nope'"));
})

TEST(a_failed_function_silences_its_calls, {
    TEST_ASSERT_FALSE(check_src("fn nope f() {\n    return 1;\n}\n"
                                "fn i32 main() {\n    return f() + 1;\n}\n"));
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
    add("main.ft", "import b;\nfn i32 main() {\n    return b.B;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    // Every module of the closure is checked, in dependency order (D9.10,
    // D14.2 as amended).
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
    TEST_RUN(a_cast_adds_mutability_and_ownership);
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
    TEST_RUN(an_extern_may_not_declare_the_program_entry_point);
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
