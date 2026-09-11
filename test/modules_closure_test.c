// Unit tests of the import closure (module-system.md 6, 10, 13;
// D9.5, D9.6, D9.8, D7.9): cycles, the identity of a module by the real path
// of its file, the module namespaces the bindings land in, extern
// declarations across modules, and the dependency order. The roots and the
// import readings are the other half, in modules_test.c.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "modules.h"
#include "modules_helpers.h"
#include "scope.h"
#include "str.h"

#include "test.h"

// ---- cycles (D9.5) ------------------------------------------------------------------

TEST(a_cycle_is_reported_at_the_import_that_closes_it, {
    begin();
    add("main.ft", "import other;\nfn i32 main() { return other.value(); }\n");
    add("other.ft", "import main;\nfn i32 value() { return 1; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("circular import: 'main' imports 'other' imports 'main'"));
    TEST_ASSERT_TRUE(said("other.ft:1:1: error:"));
})

TEST(a_module_importing_itself_is_a_cycle_of_length_one, {
    begin();
    add("main.ft", "import main;\nfn i32 main() { return 0; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("circular import: 'main' imports 'main'"));
})

TEST(a_longer_cycle_names_every_module_on_the_path, {
    begin();
    add("main.ft", "import a;\nfn i32 main() { return 0; }\n");
    add("a.ft", "import b;\nfn i32 f() { return 0; }\n");
    add("b.ft", "import a;\nfn i32 g() { return 0; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("circular import: 'a' imports 'b' imports 'a'"));
})

TEST(a_diamond_is_not_a_cycle, {
    begin();
    add("main.ft",
        "import a;\n"
        "import b;\n"
        "fn i32 main() { return 0; }\n");
    add("a.ft", "import c;\nfn i32 f() { return 0; }\n");
    add("b.ft", "import c;\nfn i32 g() { return 0; }\n");
    add("c.ft", "fn i32 h() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)4);
    TEST_ASSERT_EQ_STR(ordered(0), "c");
    TEST_ASSERT_EQ_STR(ordered(1), "a");
    TEST_ASSERT_EQ_STR(ordered(2), "b");
    TEST_ASSERT_EQ_STR(ordered(3), "main");
})

// ---- identity by the real path (D9.2) -----------------------------------------------

TEST(one_file_reached_through_two_paths_is_an_error, {
    begin();
    add("main.ft",
        "import sub::x;\n"
        "import x;\n"
        "fn i32 main() { return 0; }\n");
    add("sub/x.ft", "fn i32 f() { return 0; }\n");
    root("sub");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'x' is the same file as module 'sub::x'"));
})

TEST(a_symbolic_link_reaches_the_same_module, {
    begin();
    add("main.ft",
        "import util;\n"
        "import link;\n"
        "fn i32 main() { return 0; }\n");
    add("util.ft", src_add());
    char target[PATH_CAP];
    join_sandbox_path(target, sizeof target, sandbox, "util.ft");
    TEST_UNUSED(symlink(target, in_sandbox("link.ft")));
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("is the same file as module 'util'"));
})

TEST(one_module_may_be_imported_under_several_names, {
    begin();
    add("main.ft",
        "import util;\n"
        "import util as u;\n"
        "fn i32 main() { return u.add(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)2);
    TEST_ASSERT_TRUE(bound("main", "util")->module == bound("main", "u")->module);
})

TEST(a_module_and_one_of_its_declarations_may_be_imported_together, {
    begin();
    add("main.ft",
        "import util;\n"
        "import util::add;\n"
        "fn i32 main() { return add(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)2);
    TEST_ASSERT_TRUE(bound("main", "add")->to == bound("util", "add"));
})

// ---- the module namespace (D7.9) ----------------------------------------------------

TEST(every_kind_of_declaration_enters_the_namespace, {
    begin();
    add("main.ft",
        "extern fn i64 write(i32 fd, void* buf, u64 n);\n"
        "struct point { i32 x; i32 y; }\n"
        "enum color { red, green }\n"
        "i32 LIMIT = 4;\n"
        "i32 mut counter = 0;\n"
        "fn i32 main() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "write")->kind, (int32_t)BIND_EXTERN_FN);
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "point")->kind, (int32_t)BIND_STRUCT);
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "color")->kind, (int32_t)BIND_ENUM);
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "LIMIT")->kind, (int32_t)BIND_VAR);
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "counter")->kind, (int32_t)BIND_VAR);
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "main")->kind, (int32_t)BIND_FN);
})

TEST(enum_members_are_not_in_the_namespace, {
    begin();
    add("main.ft", "enum color { red, green }\nfn i32 main() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_NULL(bound("main", "red"));
})

TEST(two_declarations_of_one_name_collide, {
    begin();
    add("main.ft",
        "fn i32 add(i32 a) { return a; }\n"
        "struct add { i32 x; }\n"
        "fn i32 main() { return 0; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("redeclaration of 'add'"));
    TEST_ASSERT_TRUE(said("note: previous declaration of 'add' here"));
})

TEST(a_declaration_colliding_with_an_import_is_reported_at_the_declaration, {
    begin();
    add("main.ft",
        "import util;\n"
        "fn i32 util(i32 a) { return a; }\n"
        "fn i32 main() { return 0; }\n");
    add("util.ft", src_add());
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("main.ft:2:1: error: redeclaration of 'util'"));
    TEST_ASSERT_TRUE(said("main.ft:1:1: note:"));
})

TEST(two_imports_binding_one_name_collide, {
    begin();
    add("main.ft",
        "import util;\n"
        "import other as util;\n"
        "fn i32 main() { return 0; }\n");
    add("util.ft", src_add());
    add("other.ft", "fn i32 one() { return 1; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("main.ft:2:1: error: redeclaration of 'util'"));
})

// ---- extern declarations (D9.8) -----------------------------------------------------

TEST(one_c_symbol_may_be_declared_in_several_modules, {
    begin();
    add("main.ft",
        "import alloc;\n"
        "extern fn void* own malloc(u64 n);\n"
        "fn i32 main() { return 0; }\n");
    add("alloc.ft",
        "extern fn void* own malloc(u64 n);\n"
        "fn void* own grab(u64 n) { return malloc(n); }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
})

TEST(extern_declarations_that_differ_in_own_conflict, {
    begin();
    add("main.ft",
        "import alloc;\n"
        "extern fn void* own malloc(u64 n);\n"
        "fn i32 main() { return 0; }\n");
    add("alloc.ft",
        "extern fn void* malloc(u64 n);\n"
        "fn void* grab(u64 n) { return malloc(n); }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'malloc'"));
    TEST_ASSERT_TRUE(said("note: previous declaration of 'malloc' here"));
})

TEST(extern_declarations_that_differ_in_a_parameter_type_conflict, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn i64 write(i32 fd, void* buf, u64 n);\n"
        "fn i32 main() { return 0; }\n");
    add("other.ft",
        "extern fn i64 write(i64 fd, void* buf, u64 n);\n"
        "fn i64 go() { return write(1, null, 0); }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'write': parameter 1 differs"));
})

TEST(extern_declarations_that_differ_only_in_parameter_names_agree, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn i64 write(i32 fd, void* buf, u64 n);\n"
        "fn i32 main() { return 0; }\n");
    add("other.ft",
        "extern fn i64 write(i32 d, void* b, u64 count);\n"
        "fn i64 go() { return write(1, null, 0); }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
})

TEST(extern_declarations_that_differ_in_pointee_mutability_conflict, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn u64 strlen(char* s);\n"
        "fn i32 main() { return 0; }\n");
    // Mutability at a level below the binding is part of type identity
    // (D3.12), so `char mut*` is another signature and C is told the callee
    // writes through the pointer.
    add("other.ft",
        "extern fn u64 strlen(char mut* s);\n"
        "fn u64 go(char mut* s) { return strlen(s); }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'strlen': parameter 1 differs"));
})

TEST(extern_declarations_that_differ_in_noreturn_conflict, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn noreturn quit(i32 code);\n"
        "fn i32 main() { return 0; }\n");
    // `noreturn` is part of a function's identity (D3.10) and it is what puts
    // the trap of D8.5 after the call site, so the two declarations do not
    // describe one C function.
    add("other.ft",
        "extern fn void quit(i32 code);\n"
        "fn void go() { quit(1); }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'quit': the result type differs"));
})

TEST(extern_declarations_that_differ_in_parameter_count_conflict, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn i32 printf(char* fmt, i32 n);\n"
        "fn i32 main() { return 0; }\n");
    // Each argument shape of a variadic C function is its own prototype
    // (module-system.md 8.4), and one C symbol carries one of them.
    add("other.ft",
        "extern fn i32 printf(char* fmt);\n"
        "fn i32 go(char* f) { return printf(f); }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(
        said("conflicting declarations of extern 'printf': the number of parameters differs"));
})

TEST(extern_declarations_that_differ_in_a_binding_mut_conflict, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn i64 write(i32 fd, void* buf, u64 n);\n"
        "fn i32 main() { return 0; }\n");
    // A binding-level `mut` is not part of a function type (D3.10) and an
    // extern has no body for it to mean anything in, but the comparison is of
    // what the two modules wrote (D9.8 as amended), so it is a difference.
    // The two spellings emit byte-identical IR, so the cost is the diagnostic
    // and the diagnostic has to say which parameter it means.
    add("other.ft",
        "extern fn i64 write(i32 mut fd, void* buf, u64 n);\n"
        "fn i64 go() { return write(1, null, 0); }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'write': parameter 1 differs"));
    TEST_ASSERT_TRUE(said("compared as written, parameter names excepted"));
})

TEST(extern_declarations_that_spell_one_enum_two_ways_conflict_wrongly, {
    begin();
    add("main.ft",
        "import other;\n"
        "import shade;\n"
        "extern fn void paint(shade.color c);\n"
        "fn i32 main() { return 0; }\n");
    // Wrong, and pinned so that fixing it changes this test: `color` and
    // `shade.color` are one type (D9.4), and the module that declares the
    // enum cannot qualify its own name or import itself, so no spelling
    // exists that both modules can write and the program cannot be written at
    // all. The comparison is of syntax because it runs before types exist;
    // comparing types means moving it to where they do, which is T-074. Until
    // then the note carries the workaround, which is real: one module
    // declares the symbol and exports a fort function the other imports.
    add("other.ft",
        "import shade;\n"
        "extern fn void paint(color c);\n"
        "fn void go() { paint(color.red); }\n");
    add("shade.ft", "enum color { red, green }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'paint': parameter 1 differs"));
    TEST_ASSERT_TRUE(said("call it through a fort function the others import"));
})

TEST(the_workaround_for_a_spelling_conflict_compiles, {
    begin();
    // The note's advice, checked: the module that owns the enum declares the
    // C symbol and exports a fort function, and the other module imports that
    // instead of declaring the symbol a second time.
    add("main.ft",
        "import shade;\n"
        "fn i32 main() { shade.paint_red(); return 0; }\n");
    add("shade.ft",
        "enum color { red, green }\n"
        "extern fn void paint(color c);\n"
        "fn void paint_red() { paint(color.red); }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
})

// ---- the closure (module-system.md 10) ----------------------------------------------

TEST(a_module_with_nothing_in_it_loads_with_an_empty_namespace, {
    begin();
    add("main.ft", "import empty;\nfn i32 main() { return 0; }\n");
    add("empty.ft", "");
    TEST_ASSERT_TRUE(load("main.ft"));
    const module_t* empty = module_set_find(&set, str_from_cstr("empty"));
    TEST_ASSERT_NONNULL(empty);
    TEST_ASSERT_EQ_UINT64(scope_count(&empty->names), (uint64_t)0);
})

TEST(a_module_is_read_once_however_many_modules_import_it, {
    begin();
    add("main.ft",
        "import a;\n"
        "import util;\n"
        "fn i32 main() { return 0; }\n");
    add("a.ft", "import util;\nfn i32 f() { return util.add(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)3);
    TEST_ASSERT_EQ_STR(ordered(0), "util");
})

TEST(a_chain_is_ordered_from_the_deepest_module_up, {
    begin();
    add("main.ft", "import a;\nfn i32 main() { return 0; }\n");
    add("a.ft", "import b;\nfn i32 f() { return 0; }\n");
    add("b.ft", "import c;\nfn i32 g() { return 0; }\n");
    add("c.ft", "fn i32 h() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "c");
    TEST_ASSERT_EQ_STR(ordered(1), "b");
    TEST_ASSERT_EQ_STR(ordered(2), "a");
    TEST_ASSERT_EQ_STR(ordered(3), "main");
})

TEST(the_pass_order_is_the_dependency_order_when_every_module_loaded, {
    begin();
    add("main.ft", "import a;\nfn i32 main() { return 0; }\n");
    add("a.ft", "fn i32 f() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    // A closure that resolved is ordered end to end, so the pass order of
    // D9.10 is the dependency order and nothing more.
    TEST_ASSERT_EQ_UINT64(module_set_pass_count(&set), module_set_count(&set));
    TEST_ASSERT_EQ_STR(passed(0), "a");
    TEST_ASSERT_EQ_STR(passed(1), "main");
})

TEST(the_pass_order_ends_with_the_modules_the_loader_never_ordered, {
    begin();
    add("main.ft", "import a;\nfn i32 main() { return 0; }\n");
    add("a.ft", "import nothere;\nfn i32 f() { return 0; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    // `a` never resolved and `main` was never reached, so neither is in the
    // dependency order; both parsed, so a pass still visits them, the
    // imported module first (D14.2, D20.1).
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(module_set_pass_count(&set), (uint64_t)2);
    TEST_ASSERT_EQ_STR(passed(0), "a");
    TEST_ASSERT_EQ_STR(passed(1), "main");
    TEST_ASSERT_FALSE(module_set_is_ordered(&set, module_set_find(&set, str_from_cstr("main"))));
})

TEST(a_module_that_did_not_parse_is_not_in_the_pass_order, {
    begin();
    add("main.ft", "import a;\nfn i32 main() { return 0; }\n");
    add("a.ft", "fn i32 f() { return 0\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    // A file with a syntax error is not checked, so no pass visits it
    // (D14.2); the importer that parsed is visited.
    TEST_ASSERT_EQ_UINT64(module_set_pass_count(&set), (uint64_t)1);
    TEST_ASSERT_EQ_STR(passed(0), "main");
})

TEST(the_symbol_reading_works_at_any_depth, {
    begin();
    add("main.ft", "import a::b::c::d;\nfn i32 main() { return d(1); }\n");
    add("a/b/c.ft", "fn i32 d(i32 x) { return x; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "a::b::c");
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "d")->kind, (int32_t)BIND_SYMBOL);
})

TEST(the_file_of_a_module_is_its_root_joined_path, {
    begin();
    add("src/main.ft", "import geom::vec;\nfn i32 main() { return 0; }\n");
    add("src/geom/vec.ft", "struct vec2 { i64 x; i64 y; }\n");
    TEST_ASSERT_TRUE(load("src/main.ft"));
    const module_t* vec = module_set_find(&set, str_from_cstr("geom::vec"));
    TEST_ASSERT_NONNULL(vec);
    // The file is the root as given followed by the path (toolchain.md 4).
    TEST_ASSERT_NONNULL(strstr(vec->file.ptr, "/src/geom/vec.ft"));
})

TEST(a_root_spelled_with_dot_dot_reaches_the_same_module, {
    begin();
    add("main.ft",
        "import sub::x;\n"
        "import x;\n"
        "fn i32 main() { return 0; }\n");
    add("sub/x.ft", "fn i32 f() { return 0; }\n");
    // <sandbox>/sub/../sub/x.ft and <sandbox>/sub/x.ft are one file, since a
    // module's identity is the real path of its file, `..` resolved (D9.2).
    root("sub/../sub");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'x' is the same file as module 'sub::x'"));
})

TEST(the_notes_of_a_missing_module_name_every_root_and_reading, {
    begin();
    add("main.ft", "import util::strings;\nfn i32 main() { return 0; }\n");
    root("lib");
    TEST_ASSERT_FALSE(load("main.ft"));
    // One note per root and reading: two roots, two readings (D9.2,
    // module-system.md 13).
    uint64_t notes = 0;
    for (const char* cursor = strstr(diags(), "note:"); cursor != NULL;
         cursor = strstr(cursor + 1, "note:")) {
        notes++;
    }
    TEST_ASSERT_EQ_UINT64(notes, (uint64_t)4);
})

TEST(extern_declarations_that_differ_in_the_result_type_conflict, {
    begin();
    add("main.ft",
        "import other;\n"
        "extern fn i64 write(i32 fd, void* buf, u64 n);\n"
        "fn i32 main() { return 0; }\n");
    add("other.ft",
        "extern fn i32 write(i32 fd, void* buf, u64 n);\n"
        "fn i32 go() { return write(1, null, 0); }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("conflicting declarations of extern 'write': the result type differs"));
})

TEST(a_namespace_holds_the_files_imports_and_its_declarations, {
    begin();
    add("main.ft",
        "import util;\n"
        "import util::add as plus;\n"
        "fn i32 main() { return plus(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    const module_t* m = module_set_find(&set, str_from_cstr("main"));
    TEST_ASSERT_NONNULL(m);
    TEST_ASSERT_EQ_UINT64(scope_count(&m->names), (uint64_t)3);
})

int main(int argc, char** argv) {
    TEST_INIT("modules-closure", argc, argv);
    TEST_RUN(a_cycle_is_reported_at_the_import_that_closes_it);
    TEST_RUN(a_module_importing_itself_is_a_cycle_of_length_one);
    TEST_RUN(a_longer_cycle_names_every_module_on_the_path);
    TEST_RUN(a_diamond_is_not_a_cycle);
    TEST_RUN(one_file_reached_through_two_paths_is_an_error);
    TEST_RUN(a_symbolic_link_reaches_the_same_module);
    TEST_RUN(one_module_may_be_imported_under_several_names);
    TEST_RUN(a_module_and_one_of_its_declarations_may_be_imported_together);
    TEST_RUN(every_kind_of_declaration_enters_the_namespace);
    TEST_RUN(enum_members_are_not_in_the_namespace);
    TEST_RUN(two_declarations_of_one_name_collide);
    TEST_RUN(a_declaration_colliding_with_an_import_is_reported_at_the_declaration);
    TEST_RUN(two_imports_binding_one_name_collide);
    TEST_RUN(one_c_symbol_may_be_declared_in_several_modules);
    TEST_RUN(extern_declarations_that_differ_in_own_conflict);
    TEST_RUN(extern_declarations_that_differ_in_a_parameter_type_conflict);
    TEST_RUN(extern_declarations_that_differ_only_in_parameter_names_agree);
    TEST_RUN(extern_declarations_that_differ_in_pointee_mutability_conflict);
    TEST_RUN(extern_declarations_that_differ_in_noreturn_conflict);
    TEST_RUN(extern_declarations_that_differ_in_parameter_count_conflict);
    TEST_RUN(extern_declarations_that_differ_in_a_binding_mut_conflict);
    TEST_RUN(extern_declarations_that_spell_one_enum_two_ways_conflict_wrongly);
    TEST_RUN(the_workaround_for_a_spelling_conflict_compiles);
    TEST_RUN(a_module_with_nothing_in_it_loads_with_an_empty_namespace);
    TEST_RUN(a_module_is_read_once_however_many_modules_import_it);
    TEST_RUN(a_chain_is_ordered_from_the_deepest_module_up);
    TEST_RUN(the_pass_order_is_the_dependency_order_when_every_module_loaded);
    TEST_RUN(the_pass_order_ends_with_the_modules_the_loader_never_ordered);
    TEST_RUN(a_module_that_did_not_parse_is_not_in_the_pass_order);
    TEST_RUN(the_symbol_reading_works_at_any_depth);
    TEST_RUN(the_file_of_a_module_is_its_root_joined_path);
    TEST_RUN(a_root_spelled_with_dot_dot_reaches_the_same_module);
    TEST_RUN(the_notes_of_a_missing_module_name_every_root_and_reading);
    TEST_RUN(extern_declarations_that_differ_in_the_result_type_conflict);
    TEST_RUN(a_namespace_holds_the_files_imports_and_its_declarations);
    done();
    TEST_EXIT();
}
