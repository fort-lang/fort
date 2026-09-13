// Unit tests of the import closure (module-system.md 6, 10, 13): cycles, the
// identity of a module by the real path of its file, the module namespaces
// the bindings land in, and the dependency order. The roots and the import
// readings are the other half, in modules_test.c; two declarations of one C
// symbol are compared where types exist, so they are in check_extern_test.c.
// D9.5, D9.6, D7.9, D9.8
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "modules.h"
#include "modules_helpers.h"
#include "scope.h"
#include "str.h"

#include "test.h"

// ---- cycles -------------------------------------------------------------------------
// D9.5

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

// ---- identity by the real path ------------------------------------------------------
// D9.2

TEST(one_file_reached_through_two_paths_is_an_error, {
    begin();
    add("main.ft",
        "import sub.x;\n"
        "import x;\n"
        "fn i32 main() { return 0; }\n");
    add("sub/x.ft", "fn i32 f() { return 0; }\n");
    root("sub");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'x' is the same file as module 'sub.x'"));
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
        "import util.add;\n"
        "fn i32 main() { return add(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)2);
    TEST_ASSERT_TRUE(bound("main", "add")->to == bound("util", "add"));
})

// ---- the module namespace -----------------------------------------------------------
// D7.9

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
    // A closure that resolved is ordered end to end, so the pass order is the
    // dependency order and nothing more.
    // D9.10
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
    // imported module first.
    // D14.2, D20.1
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
    // A file with a syntax error is not checked, so no pass visits it; the
    // importer that parsed is visited.
    // D14.2
    TEST_ASSERT_EQ_UINT64(module_set_pass_count(&set), (uint64_t)1);
    TEST_ASSERT_EQ_STR(passed(0), "main");
})

TEST(the_symbol_reading_works_at_any_depth, {
    begin();
    add("main.ft", "import a.b.c.d;\nfn i32 main() { return d(1); }\n");
    add("a/b/c.ft", "fn i32 d(i32 x) { return x; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "a.b.c");
    TEST_ASSERT_EQ_INT32((int32_t)bound("main", "d")->kind, (int32_t)BIND_SYMBOL);
})

TEST(the_file_of_a_module_is_its_root_joined_path, {
    begin();
    add("src/main.ft", "import geom.vec;\nfn i32 main() { return 0; }\n");
    add("src/geom/vec.ft", "struct vec2 { i64 x; i64 y; }\n");
    TEST_ASSERT_TRUE(load("src/main.ft"));
    const module_t* vec = module_set_find(&set, str_from_cstr("geom.vec"));
    TEST_ASSERT_NONNULL(vec);
    // The file is the root as given followed by the path (toolchain.md 4).
    TEST_ASSERT_NONNULL(strstr(vec->file.ptr, "/src/geom/vec.ft"));
})

TEST(a_root_spelled_with_dot_dot_reaches_the_same_module, {
    begin();
    add("main.ft",
        "import sub.x;\n"
        "import x;\n"
        "fn i32 main() { return 0; }\n");
    add("sub/x.ft", "fn i32 f() { return 0; }\n");
    // <sandbox>/sub/../sub/x.ft and <sandbox>/sub/x.ft are one file, since a
    // module's identity is the real path of its file, `..` resolved.
    // D9.2
    root("sub/../sub");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'x' is the same file as module 'sub.x'"));
})

TEST(the_notes_of_a_missing_module_name_every_root_and_reading, {
    begin();
    add("main.ft", "import util.strings;\nfn i32 main() { return 0; }\n");
    root("lib");
    TEST_ASSERT_FALSE(load("main.ft"));
    // One note per root and reading: two roots, two readings
    // (module-system.md 13).
    // D9.2
    uint64_t notes = 0;
    for (const char* cursor = strstr(diags(), "note:"); cursor != NULL;
         cursor = strstr(cursor + 1, "note:")) {
        notes++;
    }
    TEST_ASSERT_EQ_UINT64(notes, (uint64_t)4);
})

TEST(a_namespace_holds_the_files_imports_and_its_declarations, {
    begin();
    add("main.ft",
        "import util;\n"
        "import util.add as plus;\n"
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
    TEST_RUN(a_namespace_holds_the_files_imports_and_its_declarations);
    done();
    TEST_EXIT();
}
