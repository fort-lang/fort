// Unit tests of the declaration group the emitter writes above its
// definitions (toolchain.md 6 item 8): the `extern` C functions of D9.8, each
// declared and called through a variadic LLVM function type so that a fixed
// prototype of a variadic C function is safe, the runtime entry points of
// section 5.1 with their own prototypes, the intrinsics, and the order and
// spacing of the three groups. The rest of the emitter's module skeleton is
// in gen_test.c, which shares gen_helpers.h with this suite.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen_helpers.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- declarations (item 8) ---------------------------------------------------------

enum { DECL_NAME_CAP = 64 };

// The name of the first `declare` in the emitted module that is neither a
// runtime entry point nor an intrinsic, or "" when there is none. The
// compiler never emits a call to a C library symbol of its own accord: that
// would be a second declaration of an ELF symbol a program may also declare
// `extern`, against D9.7's one entity per symbol (D9.8).
static const char* first_foreign_declaration(void) {
    static char name[DECL_NAME_CAP];
    const char* line = ir();
    while (line != NULL && *line != '\0') {
        if (gen_starts_with(line, "declare")) {
            const char* end_of_line = strchr(line, '\n');
            const char* at = strchr(line, '@');
            if (at == NULL || (end_of_line != NULL && at > end_of_line)) {
                // The `@` must be this declaration's own, not one further
                // down the module.
                return "declare without a symbol";
            }
            uint64_t n = 0;
            while (at[n] != '\0' && at[n] != '(' && at[n] != '\n' && n + 1 < DECL_NAME_CAP) {
                n++;
            }
            if (!gen_starts_with(at, "@fort_rt_") && !gen_starts_with(at, "@llvm.")) {
                for (uint64_t i = 0; i < n; i++) {
                    name[i] = at[i];
                }
                name[n] = '\0';
                return name;
            }
        }
        line = strchr(line, '\n');
        if (line != NULL) {
            line++;
        }
    }
    return "";
}

TEST(the_compiler_declares_no_c_library_symbol_of_its_own_accord, {
    // String equality, printing, allocation, a bounds check and an aggregate
    // copy between them reach every helper the emitter calls on its own; each
    // is a `fort_rt_` entry point, in the space no C library occupies, or an
    // LLVM intrinsic. A `@memcmp` here would be the library-call rewriting
    // that `nobuiltin` exists to prevent, arriving from the other side (D9.8).
    TEST_ASSERT_TRUE(emit("struct point {\n    i32 x;\n    i32 y;\n}\n"
                          "fn i32 main() {\n"
                          "    string a = \"ab\";\n"
                          "    println(a == \"cd\");\n"
                          "    point p = {1, 2};\n"
                          "    point q = p;\n"
                          "    i32 mut@ own xs = new(i32, 3);\n"
                          "    xs[q.x] = q.y;\n"
                          "    println(xs[0], xs.len);\n"
                          "    del(xs);\n"
                          "    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(first_foreign_declaration(), "");
    // And the one C symbol LLVM most wants to introduce for `string ==` is
    // named, so the assertion above cannot pass by looking at nothing.
    TEST_ASSERT_EQ_STR(absent("@memcmp"), "absent");
    TEST_ASSERT_EQ_STR(found("@fort_rt_str_eq"), "@fort_rt_str_eq");
    TEST_ASSERT_EQ_STR(verified(), "verified");
    // The scanner finds a foreign declaration when there is one, so the empty
    // answer above is an answer and not an empty walk.
    TEST_ASSERT_TRUE(emit("extern fn i32 puts(char* s);\n"
                          "fn i32 main() { return puts(\"x\".ptr); }\n"));
    TEST_ASSERT_EQ_STR(first_foreign_declaration(), "@puts");
})

TEST(an_extern_is_declared_and_called_through_a_variadic_type, {
    TEST_ASSERT_TRUE(emit("extern fn i32 printf(char* fmt);\n"
                          "fn i32 main() { string s = \"x\"; return printf(s.ptr); }\n"));
    TEST_ASSERT_EQ_STR(found("declare i32 @printf(ptr, ...)"), "declare i32 @printf(ptr, ...)");
    TEST_ASSERT_EQ_STR(found("call i32 (ptr, ...) @printf(ptr %t"),
                       "call i32 (ptr, ...) @printf(ptr %t");
    // nobuiltin on every extern call site keeps LLVM from rewriting the call
    // (item 8).
    TEST_ASSERT_EQ_STR(found(") #3\n"), ") #3\n");
    TEST_ASSERT_EQ_STR(found("attributes #3 = { nobuiltin }"), "attributes #3 = { nobuiltin }");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_extern_with_no_parameter_is_still_variadic, {
    TEST_ASSERT_TRUE(emit("extern fn i32 rand();\nfn i32 main() { return rand(); }\n"));
    TEST_ASSERT_EQ_STR(found("declare i32 @rand(...)"), "declare i32 @rand(...)");
    TEST_ASSERT_EQ_STR(found("call i32 (...) @rand() #3"), "call i32 (...) @rand() #3");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_extern_narrow_signature_carries_the_c_attributes, {
    TEST_ASSERT_TRUE(emit("extern fn i8 narrow(i8 a, u16 b, bool c, char d);\n"
                          "fn i32 main() { return cast(narrow(1, 2, true, \'x\'), i32); }\n"));
    // `bool`, `char`, `u8` and `u16` carry `zeroext` and `i8` and `i16`
    // `signext` in an extern signature as in a fort one (D9.9, item 7), and
    // `char` is C's `unsigned char`, so it is `i8 zeroext` (D9.8).
    const char* decl = "declare signext i8 @narrow(i8 signext, i16 zeroext, i1 zeroext,"
                       " i8 zeroext, ...)";
    TEST_ASSERT_EQ_STR(found(decl), decl);
    // The call site carries them too, which is the half that extends the
    // argument the callee then trusts (module-system.md 8.3): the declaration
    // alone would leave the caller free to pass unnormalized bits.
    const char* call = "call signext i8 (i8, i16, i1, i8, ...) @narrow"
                       "(i8 signext 1, i16 zeroext 2, i1 zeroext true, i8 zeroext 120) #3";
    TEST_ASSERT_EQ_STR(found(call), call);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_wide_extern_signature_carries_no_extension_attribute, {
    // Nothing wider than 16 bits carries an extension attribute (D9.9), and an
    // enum crosses as a plain `i32` (D9.8), so an attribute appearing there
    // would be one LLVM applies to a value C never extends.
    TEST_ASSERT_TRUE(emit("enum color { red, green }\n"
                          "extern fn u64 wide(i32 a, u32 b, i64 c, color d, void* e);\n"
                          "fn i32 main() { return cast(wide(1, 2, 3, color.red, null), i32); }\n"));
    const char* decl = "declare i64 @wide(i32, i32, i64, i32, ptr, ...)";
    TEST_ASSERT_EQ_STR(found(decl), decl);
    const char* call = "call i64 (i32, i32, i64, i32, ptr, ...) @wide(i32 1, i32 2, i64 3,"
                       " i32 0, ptr null) #3";
    TEST_ASSERT_EQ_STR(found(call), call);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_extern_naming_a_runtime_entry_point_is_declared_once, {
    TEST_ASSERT_TRUE(emit("extern fn void fort_rt_flush(i32 fd);\n"
                          "fn i32 main() { println(\"x\"); fort_rt_flush(1); return 0; }\n"));
    // It is emitted in the runtime group with that group's prototype and left
    // out of the extern group, variadic tail included (item 8).
    TEST_ASSERT_EQ_STR(found("declare void @fort_rt_flush(i32)\n"),
                       "declare void @fort_rt_flush(i32)\n");
    TEST_ASSERT_EQ_STR(absent("@fort_rt_flush(i32, ...)"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_runtime_entry_point_an_extern_declares_is_called_through_its_prototype, {
    // An `extern fn` naming a runtime entry point takes that group's
    // prototype rather than the variadic type of an extern, and its call site
    // carries no `#3`, since no C library occupies the `fort_rt_` space
    // (item 8, D13.1). The checker has held the declaration against the
    // canonical signature first (check_conv_test.c), so the two agree.
    TEST_ASSERT_TRUE(emit("extern fn void fort_rt_del(void* p);\n"
                          "fn i32 main() { fort_rt_del(null); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("declare void @fort_rt_del(ptr)"), "declare void @fort_rt_del(ptr)");
    TEST_ASSERT_EQ_STR(found("call void @fort_rt_del(ptr null)"),
                       "call void @fort_rt_del(ptr null)");
    TEST_ASSERT_EQ_STR(absent("@fort_rt_del(ptr, ...)"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_fort_rt_name_no_entry_point_takes_is_an_ordinary_extern, {
    // The runtime group is the table of section 5.1 and not the `fort_rt_`
    // prefix: a C symbol the runtime does not declare is declared and called
    // through the variadic type of an extern, with the `#3` of item 8.
    TEST_ASSERT_TRUE(emit("extern fn i32 fort_rt_helper(i32 n);\n"
                          "fn i32 main() { return fort_rt_helper(1); }\n"));
    TEST_ASSERT_EQ_STR(found("declare i32 @fort_rt_helper(i32, ...)"),
                       "declare i32 @fort_rt_helper(i32, ...)");
    TEST_ASSERT_EQ_STR(found("call i32 (i32, ...) @fort_rt_helper(i32 1) #3"),
                       "call i32 (i32, ...) @fort_rt_helper(i32 1) #3");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_runtime_entry_point_declared_with_the_wrong_signature_is_refused, {
    // The hole T-072 closed. The emitter replaces the declaration with the
    // runtime's own prototype, so a call written against a wrong signature
    // reaches no tool below the compiler: opaque pointers make a call site's
    // type independent of its callee's, `opt` accepts the pair and the
    // program dies at run time. The front end takes the rejection over, and
    // nothing is emitted at all.
    TEST_ASSERT_FALSE(emit("extern fn void fort_rt_del(i32 wrong);\n"
                           "fn i32 main() { fort_rt_del(5); return 0; }\n"));
    // Nothing is emitted at all: the emitter never ran.
    TEST_ASSERT_EQ_STR(ir(), "");
})

TEST(the_runtime_declarations_follow_the_order_of_section_5_1, {
    TEST_ASSERT_TRUE(emit(in_main("    i32 mut a = 1;\n    a = a + 1;\n    println(1, 'c');\n")));
    TEST_ASSERT_TRUE(
        before("declare void @fort_rt_fail_overflow", "declare void @fort_rt_print_i64"));
    TEST_ASSERT_TRUE(before("declare void @fort_rt_print_i64", "declare void @fort_rt_print_char"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_intrinsics_come_last_in_their_table_order, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    point p = {};\n    point q = p;\n"
                          "    i32 mut a = 1;\n    a = a + 1;\n    return q.x;\n}\n"));
    TEST_ASSERT_TRUE(before("declare void @fort_rt_fail_overflow", "declare void @llvm.memcpy"));
    TEST_ASSERT_TRUE(before("declare void @llvm.memcpy", "declare void @llvm.memset"));
    TEST_ASSERT_TRUE(before("declare void @llvm.memset", "declare { i32, i1 } @llvm.sadd"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(only_referenced_declarations_are_emitted, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"x\");\n")));
    TEST_ASSERT_EQ_STR(absent("@fort_rt_new"), "absent");
    TEST_ASSERT_EQ_STR(absent("@fort_rt_print_i64"), "absent");
    TEST_ASSERT_EQ_STR(absent("@llvm."), "absent");
    TEST_ASSERT_EQ_STR(absent("attributes #2"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(each_declaration_group_is_separated_by_a_blank_line, {
    TEST_ASSERT_TRUE(emit("extern fn i32 rand();\n"
                          "fn i32 main() {\n    i32[2] a = {};\n    i64 i = 0;\n"
                          "    println(a[i], rand());\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(found("declare i32 @rand(...)\n\ndeclare void @fort_rt_fail_bounds"),
                       "declare i32 @rand(...)\n\ndeclare void @fort_rt_fail_bounds");
    TEST_ASSERT_EQ_STR(found("\n\ndeclare void @llvm.memset"), "\n\ndeclare void @llvm.memset");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_decl", argc, argv);
    TEST_RUN(an_extern_is_declared_and_called_through_a_variadic_type);
    TEST_RUN(an_extern_with_no_parameter_is_still_variadic);
    TEST_RUN(an_extern_narrow_signature_carries_the_c_attributes);
    TEST_RUN(a_wide_extern_signature_carries_no_extension_attribute);
    TEST_RUN(an_extern_naming_a_runtime_entry_point_is_declared_once);
    TEST_RUN(a_runtime_entry_point_an_extern_declares_is_called_through_its_prototype);
    TEST_RUN(a_fort_rt_name_no_entry_point_takes_is_an_ordinary_extern);
    TEST_RUN(a_runtime_entry_point_declared_with_the_wrong_signature_is_refused);
    TEST_RUN(the_runtime_declarations_follow_the_order_of_section_5_1);
    TEST_RUN(the_intrinsics_come_last_in_their_table_order);
    TEST_RUN(only_referenced_declarations_are_emitted);
    TEST_RUN(each_declaration_group_is_separated_by_a_blank_line);
    TEST_RUN(the_compiler_declares_no_c_library_symbol_of_its_own_accord);
    gen_done();
    TEST_EXIT();
}
