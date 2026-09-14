// The toolchain conformance audit: the rules of toolchain.md 1 to 3 that only a
// run with a compiler behind it can answer for. The suite drives driver_main
// over real sources in the sandbox of driver_helpers.h and reads what came out:
// the module `-S` wrote, for the stack probing and the `--no-bounds-check`, and
// the exit status, the diagnostics and the leftovers of the runs that stop
// before `--cc`.
// D10.8, D10.6, D2.11, D14.1, D14.2, T-026: the ticket that audited them
//
// The command line itself, its error texts and the clang invocation are
// test/driver_test.c; the check mode is test/driver_check_test.c.
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "driver.h"
#include "driver_helpers.h"

#include "test.h"

// A module of this suite is a few hundred lines of IR at most, and the nesting
// source is one line of two brackets per level.
// D2.11
enum { MODULE_CAP = 1 << 16, SOURCE_CAP = 4096, NEST_LIMIT = 256 };

// The attribute the compiler puts on every fort definition, so that a module
// written by `-S` carries the guarantee by itself.
// D10.8
static const char PROBE_ATTRIBUTE[] = "\"probe-stack\"=\"inline-asm\"";

// The runtime entry point of a failed bounds check and the one of a failed
// overflow check (toolchain.md 5.1), which tell the branches `--no-bounds-check`
// removes from the ones it leaves alone.
// D10.6
static const char BOUNDS_FAILURE[] = "@\"std.rt.fail_bounds\"";
static const char OVERFLOW_FAILURE[] = "@\"std.rt.fail_overflow\"";

// A `getelementptr` of the indexed array below: `--no-bounds-check` keeps the
// `inbounds` of every one of them (toolchain.md 6).
// D10.6
static const char ARRAY_GEP[] = "getelementptr inbounds [3 x i32]";

// A program that indexes an array three times with an index the checker
// cannot fold, and adds to it, so its module holds both kinds of check.
static const char INDEXED_SOURCE[] = "fn first() i64 {\n"
                                     "    return 0;\n"
                                     "}\n"
                                     "\n"
                                     "fn main() i32 {\n"
                                     "    i32[3] a = {1, 3, 5};\n"
                                     "    i64 i = first();\n"
                                     "    println(a[i], \" \", a[i + 1], \" \", a[i + 2]);\n"
                                     "    return 0;\n"
                                     "}\n";

// A program with a frame larger than a page: 64 KiB of locals, which is the case
// the attribute is about.
// D10.8
static const char LARGE_FRAME_SOURCE[] = "fn first() i64 {\n"
                                         "    return 0;\n"
                                         "}\n"
                                         "\n"
                                         "fn main() i32 {\n"
                                         "    u8[65536] mut room = {};\n"
                                         "    i64 i = first();\n"
                                         "    room[i + 65535] = 7;\n"
                                         "    println(room[i + 65535]);\n"
                                         "    return 0;\n"
                                         "}\n";

// ---- the module a run wrote ------------------------------------------------------

// The text of the file at `path`, NUL-terminated, or the empty string when it
// cannot be read.
static void read_module(const char* path, char* buf, size_t size) {
    buf[0] = '\0';
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }
    const size_t got = fread(buf, 1, size - 1, file);
    buf[got] = '\0';
    TEST_UNUSED(fclose(file));
}

// Writes `source` into the sandbox's entry file and runs `fort -S` over it
// with the given extra flag (NULL for none), leaving the module in `buf`.
// Returns the run, so a caller can assert its status too.
static run_t emit_module(
    sandbox_t* box, const char* source, const char* flag, char* buf, size_t size) {
    run_t run;
    run.status = -1;
    run.out[0] = '\0';
    run.err[0] = '\0';
    buf[0] = '\0';
    if (!write_source(box->entry, source)) {
        return run;
    }
    char module[PATH_CAP];
    join(module, sizeof module, box->dir, "out.ll");
    if (flag == NULL) {
        run = RUN_CAPTURED("-S", "-o", module, box->entry);
    } else {
        // RUN_CAPTURED builds the `char*[]` posix_spawn takes, so the literal
        // the caller passed goes in without its const.
        run = RUN_CAPTURED("-S", (char*)flag, "-o", module, box->entry);
    }
    read_module(module, buf, size);
    return run;
}

// The number of times `needle` occurs in `text`.
static int count_of(const char* text, const char* needle) {
    int count = 0;
    const char* at = strstr(text, needle);
    while (at != NULL) {
        count++;
        at = strstr(at + 1, needle);
    }
    return count;
}

// The `#<n>` group a `define` line references, copied into `group` as the
// `attributes #<n> = ` line it must match; false at the end of the module.
// `*from` walks the module and is left after the line that was read.
static bool next_definition_group(const char** from, char* group, size_t size) {
    const char* line = *from;
    if (strncmp(line, "define ", strlen("define ")) != 0) {
        line = strstr(line, "\ndefine ");
        if (line == NULL) {
            return false;
        }
        line++;
    }
    const char* end = strchr(line, '\n');
    *from = end == NULL ? line + strlen(line) : end;
    group[0] = '\0';
    if (end == NULL) {
        return true;
    }
    // The attribute group is the last `#<n>` of the line.
    const char* tag = NULL;
    for (const char* at = line; at < end; at++) {
        if (*at == '#') {
            tag = at;
        }
    }
    if (tag == NULL) {
        return true;
    }
    size_t len = 0;
    while (tag + 1 + len < end && tag[1 + len] >= '0' && tag[1 + len] <= '9') {
        len++;
    }
    TEST_UNUSED(snprintf(group, size, "attributes #%.*s = ", (int)len, tag + 1));
    return true;
}

// Whether every definition of `module` references an attribute group that
// carries `PROBE_ATTRIBUTE`, and there is at least one definition.
static bool every_definition_probes(const char* module) {
    const char* from = module;
    char group[PATH_CAP];
    int definitions = 0;
    while (next_definition_group(&from, group, sizeof group)) {
        definitions++;
        if (group[0] == '\0') {
            return false;
        }
        const char* line = strstr(module, group);
        if (line == NULL) {
            return false;
        }
        const char* end = strchr(line, '\n');
        if (end == NULL) {
            end = line + strlen(line);
        }
        const char* probe = strstr(line, PROBE_ATTRIBUTE);
        if (probe == NULL || probe > end) {
            return false;
        }
    }
    return definitions > 0;
}

// ---- stack probing ---------------------------------------------------------------
// D10.8

TEST(every_definition_carries_the_probe_attribute, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    const run_t run = emit_module(&box, INDEXED_SOURCE, NULL, module, sizeof module);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // Frames larger than a page are probed, and the request is the
    // "probe-stack"="inline-asm" attribute on every fort definition.
    // D10.8
    TEST_ASSERT_TRUE(every_definition_probes(module));
    sandbox_close(&box);
})

TEST(a_frame_larger_than_a_page_is_probed_too, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    const run_t run = emit_module(&box, LARGE_FRAME_SOURCE, NULL, module, sizeof module);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_NONNULL(strstr(module, "[65536 x i8]"));
    TEST_ASSERT_TRUE(every_definition_probes(module));
    sandbox_close(&box);
})

TEST(the_probe_attribute_survives_release_and_no_bounds_check, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    // The attribute is in the IR rather than on the `--cc` line, so no build
    // mode and no switch can take it off (toolchain.md 6 item 13).
    // D10.8
    const run_t released = emit_module(&box, INDEXED_SOURCE, "--release", module, sizeof module);
    TEST_ASSERT_EQ_INT32(released.status, FORT_EXIT_OK);
    TEST_ASSERT_TRUE(every_definition_probes(module));
    const run_t unchecked =
        emit_module(&box, INDEXED_SOURCE, "--no-bounds-check", module, sizeof module);
    TEST_ASSERT_EQ_INT32(unchecked.status, FORT_EXIT_OK);
    TEST_ASSERT_TRUE(every_definition_probes(module));
    sandbox_close(&box);
})

// ---- --no-bounds-check -----------------------------------------------------------
// D10.6

TEST(the_checks_of_a_module_are_the_bounds_and_the_overflow_ones, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    const run_t run = emit_module(&box, INDEXED_SOURCE, NULL, module, sizeof module);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // Three indexes and two additions, each checked in every build mode. Nothing
    // declares an entry point, so the name appears once per call and no more
    // (toolchain.md 6 item 8).
    // D10.6, D11.1
    TEST_ASSERT_EQ_INT32(count_of(module, BOUNDS_FAILURE), 3);
    TEST_ASSERT_EQ_INT32(count_of(module, OVERFLOW_FAILURE), 2);
    // Three stores of the array literal and three indexes, each addressed by
    // an `inbounds` getelementptr (toolchain.md 6).
    TEST_ASSERT_EQ_INT32(count_of(module, ARRAY_GEP), 3 + 3);
    sandbox_close(&box);
})

TEST(no_bounds_check_removes_every_bounds_branch, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    const run_t run = emit_module(&box, INDEXED_SOURCE, "--no-bounds-check", module, sizeof module);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // --no-bounds-check removes exactly the index and span branches, so neither
    // the call nor the declaration of the entry point is left.
    // D10.6, D19.6
    TEST_ASSERT_EQ_INT32(count_of(module, BOUNDS_FAILURE), 0);
    sandbox_close(&box);
})

TEST(no_bounds_check_keeps_the_inbounds_of_every_index, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    const run_t checked = emit_module(&box, INDEXED_SOURCE, NULL, module, sizeof module);
    TEST_ASSERT_EQ_INT32(checked.status, FORT_EXIT_OK);
    const int with_checks = count_of(module, ARRAY_GEP);
    const run_t unchecked =
        emit_module(&box, INDEXED_SOURCE, "--no-bounds-check", module, sizeof module);
    TEST_ASSERT_EQ_INT32(unchecked.status, FORT_EXIT_OK);
    // The `inbounds` of the getelementptr stays: it is the addressing, not a
    // check, so the two modules address alike (toolchain.md 6).
    // D6.8
    TEST_ASSERT_EQ_INT32(count_of(module, ARRAY_GEP), with_checks);
    sandbox_close(&box);
})

TEST(no_bounds_check_leaves_the_other_checks_alone, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    const run_t run = emit_module(&box, INDEXED_SOURCE, "--no-bounds-check", module, sizeof module);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // Only the index and span checks are removed; the arithmetic of the same
    // program keeps the traps of checked mode (toolchain.md 3).
    TEST_ASSERT_EQ_INT32(count_of(module, OVERFLOW_FAILURE), 2);
    sandbox_close(&box);
})

TEST(release_and_no_bounds_check_are_independent, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    // --release wraps the arithmetic and keeps the bounds checks; the two
    // switches are independent and may be combined (toolchain.md 1 and 3).
    const run_t released = emit_module(&box, INDEXED_SOURCE, "--release", module, sizeof module);
    TEST_ASSERT_EQ_INT32(released.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(count_of(module, BOUNDS_FAILURE), 3);
    TEST_ASSERT_EQ_INT32(count_of(module, OVERFLOW_FAILURE), 0);
    sandbox_close(&box);
})

// ---- the nesting limit -----------------------------------------------------------
// D2.11

// A `main` whose body nests `depth` parenthesized expressions around a
// constant, one construct per level. The brackets are written by hand, so
// the buffer bounds them: a depth the caller raised past it truncates the
// source rather than running past the end, and the test then fails on what
// it compiled.
static void nested_source(char* buf, size_t size, int depth) {
    const size_t tail = strlen(";\n    return x;\n}\n");
    size_t at = (size_t)snprintf(buf, size, "fn main() i32 {\n    i32 x = ");
    const size_t room = size - at - tail - 1;
    const size_t wanted = (size_t)depth * 2 + 1;
    const int levels = wanted <= room ? depth : (int)((room - 1) / 2);
    for (int i = 0; i < levels; i++) {
        buf[at++] = '(';
    }
    buf[at++] = '0';
    for (int i = 0; i < levels; i++) {
        buf[at++] = ')';
    }
    TEST_UNUSED(snprintf(buf + at, size - at, ";\n    return x;\n}\n"));
}

TEST(nesting_to_the_limit_compiles, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char source[SOURCE_CAP];
    // The function body is one of the 256 nesting levels, so 255 parenthesized
    // expressions inside it stand exactly at the limit.
    // D2.11
    nested_source(source, sizeof source, NEST_LIMIT - 1);
    static char module[MODULE_CAP];
    const run_t run = emit_module(&box, source, NULL, module, sizeof module);
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    sandbox_close(&box);
})

TEST(nesting_past_the_limit_is_a_compile_error_and_runs_no_compiler, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char source[SOURCE_CAP];
    // One more than the test above, which is the first depth the limit rejects:
    // nesting deeper than 256 is a compile error, so a recursive-descent
    // compiler written in fort never needs an unbounded stack.
    // D2.11
    nested_source(source, sizeof source, NEST_LIMIT);
    TEST_ASSERT_TRUE(write_source(box.entry, source));
    const run_t run = RUN_CAPTURED("--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(last_diags, "nesting deeper than 256"));
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

// ---- the runs that stop before `--cc` (toolchain.md 2) ---------------------------
// D14.1

TEST(a_syntax_error_is_exit_1_and_leaves_no_temporary, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn main() i32 { return 0\n"));
    // At least one compile error was reported, which is exit 1, and the
    // temporary directory is removed whether or not `--cc` ran (toolchain.md 2).
    // D14.1
    const run_t run = RUN_CAPTURED("--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(last_diags, "error:"));
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(a_module_without_main_is_a_compile_error_of_a_build, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn add(i32 a, i32 b) i32 { return a + b; }\n"));
    // A compilation builds a program, so the entry module defines `main`; the
    // error has no position in the file and is reported at 1:1.
    // D8.6, D14.2
    const run_t run = RUN_CAPTURED("--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(last_diags, ":1:1: error:"));
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(a_failed_emission_writes_no_module_at_all, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn main() i32 { return 0\n"));
    char module[PATH_CAP];
    join(module, sizeof module, box.dir, "out.ll");
    // A half-emitted module never reaches `--cc`: the front end stops at the
    // compile error and the output file is never created (toolchain.md 2).
    const run_t run = RUN_CAPTURED("-S", "-o", module, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_EQ_INT32(access(module, F_OK), -1);
    sandbox_close(&box);
})

TEST(the_module_of_s_is_the_one_a_build_hands_to_the_compiler, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    static char module[MODULE_CAP];
    const run_t run = emit_module(&box, INDEXED_SOURCE, NULL, module, sizeof module);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // The module names the entry module's functions and ends with the
    // attribute groups of toolchain.md 6; `-S` stops there (toolchain.md 2).
    TEST_ASSERT_NONNULL(strstr(module, "define dso_local i32 @\"main.main\"()"));
    TEST_ASSERT_NONNULL(strstr(module, "define dso_local i32 @fort_entry("));
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

// ---- the entry file and the search roots ----------------------------------------
// D9.1, D9.2

TEST(an_entry_whose_base_name_is_not_an_identifier_compiles, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char entry[PATH_CAP];
    join(entry, sizeof entry, box.dir, "007_case.ft");
    TEST_ASSERT_TRUE(write_source(entry, "fn main() i32 { return 0; }\n"));
    char module[PATH_CAP];
    join(module, sizeof module, box.dir, "out.ll");
    // The entry file is named on the command line rather than reached by an
    // import path, so its base name need not be an identifier (toolchain.md 2
    // step 1).
    // D9.1
    const run_t run = RUN_CAPTURED("-S", "-o", module, entry);
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(access(module, F_OK), 0);
    sandbox_close(&box);
})

TEST(the_first_include_root_that_holds_a_module_wins, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry,
                                  "import util;\n"
                                  "fn main() i32 { return util.answer(); }\n"));
    char first[PATH_CAP];
    char second[PATH_CAP];
    join(first, sizeof first, box.dir, "first");
    join(second, sizeof second, box.dir, "second");
    TEST_ASSERT_EQ_INT32(mkdir(first, S_IRWXU), 0);
    TEST_ASSERT_EQ_INT32(mkdir(second, S_IRWXU), 0);
    char module[PATH_CAP];
    join(module, sizeof module, first, "util.ft");
    TEST_ASSERT_TRUE(write_source(module, "fn answer() i32 { return 1; }\n"));
    join(module, sizeof module, second, "util.ft");
    TEST_ASSERT_TRUE(write_source(module, "fn answer() i32 { return 2; }\n"));
    char out[PATH_CAP];
    join(out, sizeof out, box.dir, "out.ll");
    // `-I` roots are searched in command-line order, so the module of the first
    // root is the one the program is built from.
    // D9.2
    const run_t run = RUN_CAPTURED("-S", "-I", first, "-I", second, "-o", out, box.entry);
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    static char text[MODULE_CAP];
    read_module(out, text, sizeof text);
    TEST_ASSERT_NONNULL(strstr(text, "ret i32 1"));
    TEST_ASSERT_NULL(strstr(text, "ret i32 2"));
    sandbox_close(&box);
})

int main(int argc, char** argv) {
    TEST_INIT("driver_conformance", argc, argv);
    TEST_RUN(every_definition_carries_the_probe_attribute);
    TEST_RUN(a_frame_larger_than_a_page_is_probed_too);
    TEST_RUN(the_probe_attribute_survives_release_and_no_bounds_check);
    TEST_RUN(the_checks_of_a_module_are_the_bounds_and_the_overflow_ones);
    TEST_RUN(no_bounds_check_removes_every_bounds_branch);
    TEST_RUN(no_bounds_check_keeps_the_inbounds_of_every_index);
    TEST_RUN(no_bounds_check_leaves_the_other_checks_alone);
    TEST_RUN(release_and_no_bounds_check_are_independent);
    TEST_RUN(nesting_to_the_limit_compiles);
    TEST_RUN(nesting_past_the_limit_is_a_compile_error_and_runs_no_compiler);
    TEST_RUN(a_syntax_error_is_exit_1_and_leaves_no_temporary);
    TEST_RUN(a_module_without_main_is_a_compile_error_of_a_build);
    TEST_RUN(a_failed_emission_writes_no_module_at_all);
    TEST_RUN(the_module_of_s_is_the_one_a_build_hands_to_the_compiler);
    TEST_RUN(an_entry_whose_base_name_is_not_an_identifier_compiles);
    TEST_RUN(the_first_include_root_that_holds_a_module_wins);
    TEST_EXIT();
}
