// Unit tests of the runtime entry-point table (runtime_sig.h, toolchain.md
// 5.1): the name lookup, the arity and result of every row, the `noreturn`
// set, the IR text of every form, the agreement of every row with the fort
// signature in `std/rt.ft`, and the form a fort type takes as a value.
//
// The table is the one description of an entry point the compiler holds, and
// `std/rt.ft` is the one thing that defines it. Nothing below the compiler
// holds the two together: opaque pointers make a call site's type independent
// of its callee's, so `opt -passes=verify` accepts a call whose arguments
// disagree with the definition it reaches, and the program then reads a
// register the caller never set. That is why this suite reads `std/rt.ft`
// with the compiler's own parser and checker rather than a copy of the
// signatures.
#include "runtime_sig.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "check.h"
#include "containers.h"
#include "diag.h"
#include "gen.h"
#include "modules.h"
#include "prim.h"
#include "str.h"
#include "types.h"

#include "test.h"

// The standard library sources, which CMake names. `std/rt.ft` is the
// definition of every entry point of section 5.1 that `std.rt` owns.
#ifndef FORT_STD_SOURCE_DIR
#define FORT_STD_SOURCE_DIR "std"
#endif

static type_table_t types;
static bool types_live = false;

static void types_begin(void) {
    if (types_live) {
        type_table_free(&types);
    }
    type_table_init(&types);
    types_live = true;
}

static void types_end(void) {
    if (types_live) {
        type_table_free(&types);
        types_live = false;
    }
}

static const type_t* prim(prim_kind_t k) {
    return type_prim(&types, k);
}

// The IR form of a fort type, as its text, so a failing assertion shows the
// spelling rather than a number.
static const char* form_of(const type_t* t) {
    return ir_param_text(ir_form_of_type(t));
}

// The parameter text the emitter would write for `t` at an extern call site
// and in an extern declaration: the value type of item 7 with the extension
// attribute after it. The comparison below holds ir_form_of_type against it,
// since a second mapping from a fort type to an IR type is what would let the
// check pass a call the emitter writes differently.
// D9.9, D9.8
static const char* emitted_param(const type_t* t) {
    static char text[64];
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    gen_t g;
    gen_init(&g, opts);
    const str_t ty = gen_value_type(&g, t);
    const char* attr = gen_ext_attr(t);
    uint64_t n = 0;
    while (n < ty.len && n + 1 < sizeof text) {
        text[n] = ty.ptr[n];
        n++;
    }
    text[n] = '\0';
    if (attr != NULL) {
        TEST_UNUSED(strncat(text, " ", sizeof text - strlen(text) - 1));
        TEST_UNUSED(strncat(text, attr, sizeof text - strlen(text) - 1));
    }
    gen_free(&g);
    return text;
}

// ---- the table (toolchain.md 5.1) --------------------------------------------------

TEST(a_mangled_name_finds_its_entry_point_and_nothing_else_does, {
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("std.rt.alloc")) == RT_ALLOC);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("std.rt.exit")) == RT_EXIT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("std.rt.print_enum")) == RT_PRINT_ENUM);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("std.rt_float.print_f64")) == RT_PRINT_F64);
    // The short name alone is not an entry point: the emitter matches the
    // mangled name, so a function of another module named `alloc` or `exit`
    // cannot take the attribute group of item 14.
    // D9.7
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("alloc")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("main.exit")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("std.rt.print")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("std.rt.allocs")) == RT_COUNT);
    // The C names the runtime used to have are ordinary C names now.
    // D9.8
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_rt_new")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("fort_entry")) == RT_COUNT);
    TEST_ASSERT_TRUE(rt_entry_of(str_from_cstr("write")) == RT_COUNT);
})

TEST(every_row_names_the_entry_point_its_enumerator_does, {
    // The enum of runtime_sig.h is positional, so a row inserted in the middle
    // of section 5.1's order moves every later one: each name is held against
    // the enumerator it is indexed by.
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ALLOC), "std.rt.alloc");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FREE), "std.rt.free");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_STR_EQ), "std.rt.str_eq");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_BOUNDS), "std.rt.fail_bounds");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_SPAN), "std.rt.fail_span");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_OVERFLOW), "std.rt.fail_overflow");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_SHIFT), "std.rt.fail_shift");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_DIV_ZERO), "std.rt.fail_div_zero");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_DIV_OVERFLOW), "std.rt.fail_div_overflow");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_ALLOC_COUNT), "std.rt.fail_alloc_count");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_OVERWRITE), "std.rt.fail_overwrite");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FAIL_ENUM), "std.rt.fail_enum");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PANIC), "std.rt.panic");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ASSERT_FAIL), "std.rt.assert_fail");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_I64), "std.rt.print_i64");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_U64), "std.rt.print_u64");
    // The two float printers stand in `std.rt_float`, because a compiler
    // without floats cannot compile them.
    // D18.1
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_F32), "std.rt_float.print_f32");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_F64), "std.rt_float.print_f64");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_BOOL), "std.rt.print_bool");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_CHAR), "std.rt.print_char");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_PTR), "std.rt.print_ptr");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_STR), "std.rt.print_str");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_PRINT_ENUM), "std.rt.print_enum");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FLUSH), "std.rt.flush");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_FLUSH_ALL), "std.rt.flush_all");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ARGS_INIT), "std.rt.args_init");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_ARGS), "std.rt.args");
    TEST_ASSERT_EQ_STR(rt_entry_name(RT_EXIT), "std.rt.exit");
})

TEST(every_entry_point_is_a_mangled_fort_name_of_the_runtime, {
    // A call into the runtime is an ordinary fort-to-fort call by the mangled
    // name, so no row may hold a C name: an unmangled name would be a second
    // declaration of an ELF symbol a program may declare itself.
    // D9.7, D9.8
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        const char* name = rt_entry_name((rt_entry_t)i);
        const bool rt = strncmp(name, "std.rt.", strlen("std.rt.")) == 0;
        const bool floats = strncmp(name, "std.rt_float.", strlen("std.rt_float.")) == 0;
        TEST_ASSERT_TRUE(rt || floats);
        TEST_ASSERT_NULL(strstr(name, "fort_rt"));
    }
})

TEST(the_two_entry_points_that_return_a_value_are_the_only_ones, {
    // `alloc` and `str_eq` (section 5.1); `args` returns an aggregate, which
    // item 7 makes a `void` function with a leading `ptr`, and every other row
    // is `void`.
    uint64_t returning = 0;
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        if (rt_entry_result((rt_entry_t)i) != IR_VOID) {
            returning++;
        }
    }
    TEST_ASSERT_EQ_UINT64(returning, (uint64_t)2);
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_ALLOC)), "ptr");
    // A narrow result carries its extension attribute before the type, which
    // is what licenses eliding the caller's re-narrowing (item 7). A fort
    // `bool` result is `i1` and not `i8`: `i8` is its memory type alone (item
    // 2).
    // D9.9
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_STR_EQ)), "zeroext i1");
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_ARGS)), "void");
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_FREE)), "void");
})

TEST(the_noreturn_entry_points_are_the_ones_section_5_1_names, {
    // Every `fail_*` function, `panic`, `assert_fail` and `exit` is
    // `fn noreturn`, and nothing else is: that is what puts the attribute
    // group `#8` on the definition (item 14).
    uint64_t noreturn = 0;
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        const rt_entry_t rt = (rt_entry_t)i;
        const bool named = strncmp(rt_entry_name(rt), "std.rt.fail_", strlen("std.rt.fail_")) == 0;
        const bool listed = named || rt == RT_PANIC || rt == RT_ASSERT_FAIL || rt == RT_EXIT;
        TEST_ASSERT_TRUE(rt_entry_noreturn(rt) == listed);
        if (rt_entry_noreturn(rt)) {
            noreturn++;
        }
    }
    TEST_ASSERT_EQ_UINT64(noreturn, (uint64_t)12);
})

TEST(the_parameter_list_of_a_row_is_the_fort_signature_of_section_5_1, {
    // `loc` is the three parameters `ptr, i32, i32`, so the arity of a
    // failure entry point counts them.
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_ALLOC), (uint64_t)5);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_FREE), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_STR_EQ), (uint64_t)4);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_FAIL_SPAN), (uint64_t)6);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_FAIL_OVERFLOW), (uint64_t)3);
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_PRINT_ENUM), (uint64_t)4);
    // `args` takes no fort parameter: its one `ptr` is the hidden result
    // pointer of item 7.
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_ARGS), (uint64_t)1);
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_ARGS, 0)), "ptr");
    // The rows with no parameter at all: the arity is read off the row, so an
    // empty list is not a miscount of a padded one.
    TEST_ASSERT_EQ_UINT64((uint64_t)rt_entry_param_count(RT_FLUSH_ALL), (uint64_t)0);
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_ALLOC, 0)), "i64");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_ALLOC, 2)), "ptr");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_ALLOC, 4)), "i32");
    // A fort `bool` parameter is `i1 zeroext` and a `char` parameter `i8
    // zeroext`: the two are different forms (item 2).
    // D19.2
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_BOOL, 1)), "i1 zeroext");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_CHAR, 1)), "i8 zeroext");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_F32, 1)), "float");
    TEST_ASSERT_EQ_STR(ir_param_text(rt_entry_param(RT_PRINT_F64, 1)), "double");
})

// Every scalar an extern signature may use, as a file-scope table: a brace
// initializer inside a TEST body would split the one macro argument.
// D9.8
static const prim_kind_t SCALARS[] = {PRIM_I8,
                                      PRIM_I16,
                                      PRIM_I32,
                                      PRIM_I64,
                                      PRIM_U8,
                                      PRIM_U16,
                                      PRIM_U32,
                                      PRIM_U64,
                                      PRIM_BOOL,
                                      PRIM_CHAR,
                                      PRIM_F32,
                                      PRIM_F64};

// ---- the fort signatures of std/rt.ft ----------------------------------------------
// D13.1

// The runtime's module, parsed and checked by the compiler's own front end. It
// is loaded as the entry file, so its module path is the base name `rt` and
// the runtime root is not loaded beside it; what this suite reads off it is
// the signature of each declaration, which the path does not change.
// D9.10
static module_set_t rt_set;
static check_t rt_check;
static bool rt_live = false;
static sb_t rt_diags;

// Loads `std/rt.ft`. Answers with the module, or NULL when the front end
// reported anything, which is a broken standard library rather than a failing
// row.
static const module_t* rt_open(void) {
    sb_init(&rt_diags);
    diag_capture(&rt_diags);
    diag_reset();
    module_set_init(&rt_set);
    check_init(&rt_check);
    rt_check.require_main = false;
    rt_live = true;
    module_set_std_dir(&rt_set, FORT_STD_SOURCE_DIR);
    if (!module_set_load(&rt_set, FORT_STD_SOURCE_DIR "/rt.ft")) {
        return NULL;
    }
    if (!check_program(&rt_check, &rt_set)) {
        return NULL;
    }
    return module_set_entry(&rt_set);
}

static void rt_close(void) {
    if (!rt_live) {
        return;
    }
    check_free(&rt_check);
    module_set_free(&rt_set);
    diag_capture(NULL);
    sb_free(&rt_diags);
    rt_live = false;
}

// The declaration of `name` at module level, or NULL.
static const ast_node_t* rt_decl(const module_t* m, str_t name) {
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        const ast_node_t* decl = ast_child(m->ast, i);
        if (decl->kind == AST_FN_DECL && str_eq(decl->name, name)) {
            return decl;
        }
    }
    return NULL;
}

// The part of a mangled name after its last dot, which the mangling makes the
// declaration name.
// D9.7
static str_t short_name(const char* mangled) {
    const char* last = strrchr(mangled, '.');
    return str_from_cstr(last != NULL ? last + 1 : mangled);
}

// The module path a mangled name carries, `std.rt` or `std.rt_float`.
static bool in_module(const char* mangled, const char* path) {
    const size_t n = strlen(path);
    return strncmp(mangled, path, n) == 0 && mangled[n] == '.';
}

// The widest text either helper below builds: `zeroext i16` and the widest
// row report, `std.rt.fail_div_overflow: parameter 6 is i16 signext,
// std/rt.ft's i16 zeroext`.
enum { RESULT_CAP = 64, REPORT_CAP = 160 };

// The result text the emitter writes on the definition of `t`, which is what a
// call site must state for the two to agree (item 7).
// D9.9
static const char* emitted_result(const type_t* t) {
    static char text[RESULT_CAP];
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    gen_t g;
    gen_init(&g, opts);
    const char* attr = gen_ext_attr(t);
    const str_t ty = gen_result_type(&g, t);
    // One snprintf rather than two strncat calls: the attribute and the type
    // are one text and the truncation rule is then the one the buffer states.
    const int written = snprintf(text,
                                 sizeof text,
                                 "%s%s%.*s",
                                 attr != NULL ? attr : "",
                                 attr != NULL ? " " : "",
                                 (int)ty.len,
                                 ty.ptr);
    gen_free(&g);
    if (written < 0 || (size_t)written >= sizeof text) {
        return "the result text did not fit its buffer";
    }
    return text;
}

// The first disagreement between the row `e` of RT_SIG and the fort signature
// `sig`, or "": the result form, the arity and each parameter form, all read
// through the one map of item 7.
static const char* row_mismatch(rt_entry_t e, const char* name, const type_t* sig) {
    static char text[REPORT_CAP];
    ir_form_t result = IR_NONE;
    ir_form_t params[RT_MAX_PARAMS];
    uint32_t nparams = 0;
    if (!rt_signature_of_type(sig, &result, params, &nparams)) {
        TEST_UNUSED(snprintf(text, sizeof text, "%s: the signature has no IR call shape", name));
        return text;
    }
    if (result == IR_NONE || rt_entry_result(e) != result) {
        TEST_UNUSED(snprintf(text,
                             sizeof text,
                             "%s: the row's result is %s, std/rt.ft's %s",
                             name,
                             ir_result_text(rt_entry_result(e)),
                             ir_result_text(result)));
        return text;
    }
    if (rt_entry_param_count(e) != nparams) {
        TEST_UNUSED(snprintf(text,
                             sizeof text,
                             "%s: the row takes %u parameters, std/rt.ft's %u",
                             name,
                             rt_entry_param_count(e),
                             nparams));
        return text;
    }
    for (uint32_t i = 0; i < nparams; i++) {
        if (params[i] != IR_NONE && rt_entry_param(e, i) == params[i]) {
            continue;
        }
        TEST_UNUSED(snprintf(text,
                             sizeof text,
                             "%s: parameter %u is %s, std/rt.ft's %s",
                             name,
                             i + 1,
                             ir_param_text(rt_entry_param(e, i)),
                             ir_param_text(params[i])));
        return text;
    }
    return "";
}

// The canonical spelling of `t`, valid until the next call. The rows above are
// about IR forms; the two tests below are about what `std/rt.ft` declares, so
// they read the fort type itself.
// D5.3
static const char* type_spelling(const type_t* t) {
    static sb_t out;
    static bool ready = false;
    if (!ready) {
        sb_init(&out);
        ready = true;
    }
    sb_clear(&out);
    if (t == NULL) {
        sb_append(&out, "<null>");
    } else {
        type_to_str(t, &out);
    }
    return sb_cstr(&out);
}

TEST(every_row_is_the_fort_signature_of_std_rt, {
    // The one place the rows are held against the thing that defines them.
    // Nothing below the compiler does it: a call site's argument types are
    // independent of its callee's under opaque pointers, so a wrong row emits
    // a call `opt -passes=verify` accepts and the callee then reads a
    // register the caller never set.
    const module_t* m = rt_open();
    TEST_ASSERT_NONNULL(m);
    TEST_ASSERT_EQ_STR(sb_cstr(&rt_diags), "");
    uint64_t checked = 0;
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        const rt_entry_t e = (rt_entry_t)i;
        const char* name = rt_entry_name(e);
        if (!in_module(name, "std.rt")) {
            continue;
        }
        const ast_node_t* decl = rt_decl(m, short_name(name));
        TEST_ASSERT_NONNULL(decl);
        TEST_ASSERT_NONNULL(decl->sym);
        TEST_ASSERT_EQ_STR(row_mismatch(e, name, decl->sym->type), "");
        // The call site states the result the definition states, attribute
        // included: `opt` accepts a disagreement and LLVM then falls back to
        // the callee's, which is an assumption the caller never made.
        // D9.9
        TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(e)),
                           emitted_result(decl->sym->type->elem));
        // `noreturn` is part of the signature: it is what the attribute group
        // of item 14 states of the definition.
        // D8.5
        TEST_ASSERT_TRUE(decl->sym->type->noreturn == rt_entry_noreturn(e));
        checked++;
    }
    // Every row but the two float printers, which stand in `std.rt_float` and
    // arrive with the float work.
    // D18.1
    TEST_ASSERT_EQ_UINT64(checked, (uint64_t)RT_COUNT - 2);
    rt_close();
})

TEST(the_allocator_answers_storage_the_caller_owns_and_may_write, {
    // `alloc` answers `void mut* own`. The storage is fresh, so the caller
    // owns it and may write it, and that is what an allocation is for; the
    // mark comes from `libc.calloc`, which says the same of what C returns,
    // and `alloc` carries it out rather than dropping it. Without it a caller
    // of this entry point allocates memory it may never write, and the only
    // way back is a cast that adds a `mut`. `toolchain.md` 5.1 fixes the
    // signature, that section being the list of entry points the compiler
    // holds.
    // D3.11, D5.4, D12.2
    const module_t* m = rt_open();
    TEST_ASSERT_NONNULL(m);
    TEST_ASSERT_EQ_STR(sb_cstr(&rt_diags), "");
    const ast_node_t* alloc = rt_decl(m, str_from_cstr("alloc"));
    TEST_ASSERT_NONNULL(alloc);
    TEST_ASSERT_NONNULL(alloc->sym);
    TEST_ASSERT_EQ_STR(type_spelling(alloc->sym->type->elem), "void mut* own");
    // `free` takes the `void* own` every result of `alloc` reaches by the
    // monotone drop, so the pair still fits and the drop runs one way.
    // D5.4
    const ast_node_t* release = rt_decl(m, str_from_cstr("free"));
    TEST_ASSERT_NONNULL(release);
    TEST_ASSERT_NONNULL(release->sym);
    TEST_ASSERT_EQ_UINT64((uint64_t)release->sym->type->nparams, (uint64_t)1);
    TEST_ASSERT_EQ_STR(type_spelling(release->sym->type->params[0]), "void* own");
    // The mark costs nothing at the boundary: the result the emitter writes on
    // the call of item 17 is `ptr`, which is the row the table already holds.
    // D9.9, D19.2
    TEST_ASSERT_EQ_STR(emitted_result(alloc->sym->type->elem), "ptr");
    TEST_ASSERT_EQ_STR(ir_result_text(rt_entry_result(RT_ALLOC)), "ptr");
    rt_close();
})

TEST(a_void_pointer_takes_one_ir_form_whatever_it_is_marked, {
    // Every pointer is one machine word, so `mut` and `own` on a `void*` are
    // rules of the type system and reach no IR type. The four spellings the
    // runtime boundary meets take the one form, which is why the retyping of
    // `alloc` changed no emitted text.
    // D3.11, D19.2
    types_begin();
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false, false)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false, true)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, true, false)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, true, true)), "ptr");
    TEST_ASSERT_EQ_STR(emitted_param(type_voidptr(&types, true, true)), "ptr");
    TEST_ASSERT_EQ_STR(emitted_result(type_voidptr(&types, true, true)), "ptr");
    types_end();
})

TEST(std_rt_declares_no_runtime_c_symbol, {
    // `std.rt` is the runtime, so it declares no `fort_rt_` symbol. Its own
    // `extern` declarations are `std.libc`'s and it has none of its own.
    // D13.2
    const module_t* m = rt_open();
    TEST_ASSERT_NONNULL(m);
    uint64_t externs = 0;
    for (uint64_t i = 0; i < ast_len(m->ast); i++) {
        const ast_node_t* decl = ast_child(m->ast, i);
        if (decl->kind != AST_FN_DECL || (decl->flags & AST_FLAG_EXTERN) == 0) {
            continue;
        }
        externs++;
    }
    TEST_ASSERT_EQ_UINT64(externs, (uint64_t)0);
    rt_close();
})

TEST(std_rt_defines_the_float_printers_nowhere, {
    // The two float rows name `std.rt_float`, and `std.rt` neither defines
    // them nor is loaded with them: a compiler that rejects floats cannot
    // compile them.
    // D18.1, D18.4
    const module_t* m = rt_open();
    TEST_ASSERT_NONNULL(m);
    TEST_ASSERT_TRUE(in_module(rt_entry_name(RT_PRINT_F32), "std.rt_float"));
    TEST_ASSERT_TRUE(in_module(rt_entry_name(RT_PRINT_F64), "std.rt_float"));
    TEST_ASSERT_NULL(rt_decl(m, str_from_cstr("print_f32")));
    TEST_ASSERT_NULL(rt_decl(m, str_from_cstr("print_f64")));
    rt_close();
})

// ---- the form of a fort type at the boundary ---------------------------------------
// D9.8, D9.9

TEST(a_fort_type_takes_the_ir_form_of_its_c_counterpart, {
    types_begin();
    // The mapping: `int` is `i32`, `long` and `size_t` are 64-bit, and a
    // narrow value carries an extension attribute on both sides.
    // D9.8
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I32)), "i32");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U32)), "i32");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I64)), "i64");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U64)), "i64");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I8)), "i8 signext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U8)), "i8 zeroext");
    // Fort `char` is C's `unsigned char` at the boundary, so it is the form a
    // `uint8_t` parameter takes.
    // D3.2
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_CHAR)), "i8 zeroext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_I16)), "i16 signext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_U16)), "i16 zeroext");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_F32)), "float");
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_F64)), "double");
    // `bool` is `i1` as a value and `i8` in memory, so it is a form of its own
    // and not the form of a `uint8_t`.
    // D19.2
    TEST_ASSERT_EQ_STR(form_of(prim(PRIM_BOOL)), "i1 zeroext");
    TEST_ASSERT_EQ_STR(form_of(type_void(&types)), "void");
    types_end();
})

TEST(every_pointer_takes_the_opaque_ptr_form, {
    types_begin();
    const type_t* i32t = prim(PRIM_I32);
    TEST_ASSERT_EQ_STR(form_of(type_ptr(&types, i32t, false, false)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_ptr(&types, i32t, true, true)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false, false)), "ptr");
    // `void mut*` is one machine word like every other pointer, so it crosses
    // an extern boundary in the same form.
    // D3.11
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false, true)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, true, true)), "ptr");
    // A function pointer is a pointer and an enum crosses as `i32`.
    // D3.10, D9.8, D3.9
    TEST_ASSERT_EQ_STR(form_of(type_fn(&types, i32t, &i32t, 1, false)), "ptr");
    TEST_ASSERT_EQ_STR(form_of(type_enum(&types, str_from_cstr("color"), &types)), "i32");
    types_end();
})

TEST(a_type_no_extern_signature_may_use_has_no_form, {
    types_begin();
    const type_t* i32t = prim(PRIM_I32);
    // Spans, strings, structs and fixed arrays are out of extern signatures,
    // and the checker refuses them before this is asked; a form of IR_NONE
    // never matches a row, so a type that slipped through cannot silently
    // agree with one.
    // D9.8
    TEST_ASSERT_EQ_STR(form_of(type_span(&types, i32t, false, false)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(type_string(&types, false)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(type_array(&types, i32t, 2)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(type_struct(&types, str_from_cstr("point"), &types)), "<none>");
    TEST_ASSERT_EQ_STR(form_of(NULL), "<none>");
    types_end();
})

TEST(the_form_of_a_type_is_the_text_the_emitter_writes_for_it, {
    types_begin();
    // One map from a fort type to an IR type, or the check would compare a
    // signature against a call the emitter writes differently.
    // D9.8
    for (uint64_t i = 0; i < sizeof SCALARS / sizeof SCALARS[0]; i++) {
        const type_t* t = prim(SCALARS[i]);
        TEST_ASSERT_EQ_STR(form_of(t), emitted_param(t));
    }
    const type_t* i32t = prim(PRIM_I32);
    TEST_ASSERT_EQ_STR(form_of(type_ptr(&types, i32t, false, false)),
                       emitted_param(type_ptr(&types, i32t, false, false)));
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false, false)),
                       emitted_param(type_voidptr(&types, false, false)));
    TEST_ASSERT_EQ_STR(form_of(type_voidptr(&types, false, true)),
                       emitted_param(type_voidptr(&types, false, true)));
    TEST_ASSERT_EQ_STR(form_of(type_enum(&types, str_from_cstr("color"), &types)),
                       emitted_param(type_enum(&types, str_from_cstr("color"), &types)));
    types_end();
})

int main(int argc, char** argv) {
    TEST_INIT("runtime_sig", argc, argv);
    TEST_RUN(a_mangled_name_finds_its_entry_point_and_nothing_else_does);
    TEST_RUN(every_row_names_the_entry_point_its_enumerator_does);
    TEST_RUN(every_entry_point_is_a_mangled_fort_name_of_the_runtime);
    TEST_RUN(the_two_entry_points_that_return_a_value_are_the_only_ones);
    TEST_RUN(the_noreturn_entry_points_are_the_ones_section_5_1_names);
    TEST_RUN(the_parameter_list_of_a_row_is_the_fort_signature_of_section_5_1);
    TEST_RUN(every_row_is_the_fort_signature_of_std_rt);
    TEST_RUN(the_allocator_answers_storage_the_caller_owns_and_may_write);
    TEST_RUN(a_void_pointer_takes_one_ir_form_whatever_it_is_marked);
    TEST_RUN(std_rt_declares_no_runtime_c_symbol);
    TEST_RUN(std_rt_defines_the_float_printers_nowhere);
    TEST_RUN(a_fort_type_takes_the_ir_form_of_its_c_counterpart);
    TEST_RUN(every_pointer_takes_the_opaque_ptr_form);
    TEST_RUN(a_type_no_extern_signature_may_use_has_no_form);
    TEST_RUN(the_form_of_a_type_is_the_text_the_emitter_writes_for_it);
    types_end();
    rt_close();
    TEST_EXIT();
}
