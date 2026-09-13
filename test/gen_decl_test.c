// Unit tests of the declaration group the emitter writes above its definitions
// (toolchain.md 6 item 8): the `extern` C functions, each declared and called
// through a variadic LLVM function type so that a fixed prototype of a
// variadic C function is safe, the intrinsics, and the order and spacing of
// the two groups. There are two groups and no third: the runtime is `std.rt`,
// whose definitions the module holds, so nothing declares it. The rest of the
// emitter's module skeleton is in gen_test.c, which shares gen_helpers.h with
// this suite.
// D9.8, D13.1
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gen_helpers.h"
#include "runtime_sig.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- declarations (item 8) ---------------------------------------------------------

enum { DECL_NAME_CAP = 64 };

// The name of the first `declare` in the emitted module that is not an
// intrinsic, or "" when there is none. The compiler never emits a call to a C
// library symbol of its own accord: that would be a second declaration of an
// ELF symbol a program may also declare `extern`, against the rule of one
// entity per symbol.
// D9.7, D9.8
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
            if (!gen_starts_with(at, "@llvm.")) {
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

// Whether any `declare` line of the module names `symbol`.
static bool declares(const char* symbol) {
    const char* line = ir();
    while (line != NULL && *line != '\0') {
        const char* end_of_line = strchr(line, '\n');
        const char* hit = strstr(line, symbol);
        if (gen_starts_with(line, "declare") && hit != NULL &&
            (end_of_line == NULL || hit < end_of_line)) {
            return true;
        }
        line = end_of_line;
        if (line != NULL) {
            line++;
        }
    }
    return false;
}

TEST(the_compiler_declares_no_c_library_symbol_of_its_own_accord, {
    // String equality, printing, allocation, a bounds check and an aggregate
    // copy between them reach every helper the emitter calls on its own; each
    // is a function of `std.rt`, reached by its mangled name, or an LLVM
    // intrinsic. A `@memcmp` here would be the library-call rewriting that
    // `nobuiltin` exists to prevent, arriving from the other side.
    // D9.8
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
    TEST_ASSERT_EQ_STR(found("@\"std.rt.str_eq\""), "@\"std.rt.str_eq\"");
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
    // `signext` in an extern signature as in a fort one (item 7), and `char`
    // is C's `unsigned char`, so it is `i8 zeroext`.
    // D9.9, D9.8
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
    // Nothing wider than 16 bits carries an extension attribute, and an enum
    // crosses as a plain `i32`, so an attribute appearing there would be one
    // LLVM applies to a value C never extends.
    // D9.9, D9.8
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

TEST(no_entry_point_of_section_5_1_is_ever_declared, {
    // The module that holds a call into the runtime holds its definition, so a
    // `declare` beside it would be the redefinition `opt` rejected. The
    // program below reaches printing, string equality, allocation, a bounds
    // check and a failure path (item 8).
    // T-018, D9.10: the ticket whose `opt` run rejected the redefinition
    TEST_ASSERT_TRUE(emit("fn i32 main() {\n"
                          "    string a = \"ab\";\n"
                          "    println(a == \"cd\");\n"
                          "    i32 mut@ own xs = new(i32, 3);\n"
                          "    u64 i = 0;\n"
                          "    println(xs[i]);\n"
                          "    del(xs);\n"
                          "    return 0;\n}\n"));
    for (uint64_t k = 0; k < (uint64_t)RT_COUNT; k++) {
        char text[DECL_NAME_CAP + 16];
        TEST_UNUSED(snprintf(text, sizeof text, "@\"%s\"", rt_entry_name((rt_entry_t)k)));
        TEST_ASSERT_FALSE(declares(text));
    }
    // The calls are there, so the loop above answers about a module that
    // reaches the runtime rather than about an empty one.
    TEST_ASSERT_EQ_STR(found("call zeroext i1 @\"std.rt.str_eq\""),
                       "call zeroext i1 @\"std.rt.str_eq\"");
    TEST_ASSERT_EQ_STR(found("call ptr @\"std.rt.alloc\""), "call ptr @\"std.rt.alloc\"");
    TEST_ASSERT_EQ_STR(found("call void @\"std.rt.fail_bounds\""),
                       "call void @\"std.rt.fail_bounds\"");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_fort_rt_name_is_an_ordinary_extern, {
    // The `fort_rt_` space is reserved for nothing now: the runtime is fort
    // and occupies no C name, so an `extern` naming what used to be an entry
    // point is an ordinary declaration of an ordinary C symbol, declared and
    // called through the variadic type of item 8 with its `#3`.
    // D9.8
    TEST_ASSERT_TRUE(emit("extern fn void fort_rt_del(void* p);\n"
                          "fn i32 main() { fort_rt_del(null); return 0; }\n"));
    TEST_ASSERT_EQ_STR(found("declare void @fort_rt_del(ptr, ...)"),
                       "declare void @fort_rt_del(ptr, ...)");
    TEST_ASSERT_EQ_STR(found("call void (ptr, ...) @fort_rt_del(ptr null) #3"),
                       "call void (ptr, ...) @fort_rt_del(ptr null) #3");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(the_intrinsics_come_last_in_their_table_order, {
    TEST_ASSERT_TRUE(emit("struct point { i32 x; i32 y; }\n"
                          "fn i32 main() {\n    point p = {};\n    point q = p;\n"
                          "    i32 mut a = 1;\n    a = a + 1;\n    return q.x;\n}\n"));
    TEST_ASSERT_TRUE(before("declare void @llvm.memcpy", "declare void @llvm.memset"));
    TEST_ASSERT_TRUE(before("declare void @llvm.memset", "declare { i32, i1 } @llvm.sadd"));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(only_referenced_declarations_are_emitted, {
    TEST_ASSERT_TRUE(emit(in_main("    println(\"x\");\n")));
    TEST_ASSERT_EQ_STR(absent("@\"std.rt.alloc\""), "absent");
    TEST_ASSERT_EQ_STR(absent("@\"std.rt.print_i64\""), "absent");
    TEST_ASSERT_EQ_STR(absent("@llvm."), "absent");
    TEST_ASSERT_EQ_STR(absent("attributes #2"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(each_declaration_group_is_separated_by_a_blank_line, {
    TEST_ASSERT_TRUE(emit("extern fn i32 rand();\n"
                          "fn i32 main() {\n    i32[2] a = {};\n    i64 i = 0;\n"
                          "    println(a[i], rand());\n    return 0;\n}\n"));
    // The two groups, in order and separated by one blank line (item 8).
    TEST_ASSERT_EQ_STR(found("declare i32 @rand(...)\n\ndeclare void @llvm.memset"),
                       "declare i32 @rand(...)\n\ndeclare void @llvm.memset");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_decl", argc, argv);
    TEST_RUN(an_extern_is_declared_and_called_through_a_variadic_type);
    TEST_RUN(an_extern_with_no_parameter_is_still_variadic);
    TEST_RUN(an_extern_narrow_signature_carries_the_c_attributes);
    TEST_RUN(a_wide_extern_signature_carries_no_extension_attribute);
    TEST_RUN(no_entry_point_of_section_5_1_is_ever_declared);
    TEST_RUN(a_fort_rt_name_is_an_ordinary_extern);
    TEST_RUN(the_intrinsics_come_last_in_their_table_order);
    TEST_RUN(only_referenced_declarations_are_emitted);
    TEST_RUN(each_declaration_group_is_separated_by_a_blank_line);
    TEST_RUN(the_compiler_declares_no_c_library_symbol_of_its_own_accord);
    gen_done();
    TEST_EXIT();
}
