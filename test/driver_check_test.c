// Unit tests of the check mode: `--check`, which runs the front end and stops,
// and `--json`, whose one document on stdout is the compiler's whole editor
// interface (toolchain.md 4.1).
// D20.1, D20.2
//
// The suite asserts the shape of the document byte for byte, that it holds what
// the text form holds, and the contract a client reads it by: exit 0 or 1 with a
// complete document is a verdict, exit 2 with stdout empty is a crash. The
// sandbox and the captured diagnostics are driver_helpers.h; test/fake_cc.sh is
// the `--cc` a run under `--check` must never spawn.
// D14.2
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/wait.h>

#include "ast.h"
#include "check.h"
#include "diag.h"
#include "driver.h"
#include "driver_helpers.h"
#include "modules.h"
#include "str.h"
#include "sym.h"

#include "test.h"

// The document a clean module yields. Every closure holds `std.rt`, which the
// compiler loads as a root of its own before the entry file, so the runtime's
// file stands first in the list and the entry file second. The sandbox's runtime
// is empty, so it contributes no symbol of its own.
// D9.10
static const char CLEAN_DOCUMENT[] =
    "{\"version\":1,\"files\":[\"%s\",\"%s\"],\"diagnostics\":[],\"symbols\":[]}\n";

// A module that declares no `main`: under --check the file is a module under
// inspection and not a program, so the `main` rule is not applied.
// D8.6, D20.1
static const char NO_MAIN_SOURCE[] = "fn add(i32 a, i32 b) i32 { return a + b; }\n";

// A module whose import no root reaches: one diagnostic at the `import`
// keyword, with the note of module-system.md 13.
static const char BAD_IMPORT_SOURCE[] = "import nothere;\nfn main() i32 { return 0; }\n";

TEST(json_without_check_is_a_usage_error, {
    // Only the check mode can promise a complete document or nothing, since a
    // build spawns a `--cc` that inherits stdout, so --json alone is rejected.
    // D20.2
    const run_t run = RUN("--json", "main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err,
                       "fort: error: --json requires --check\n"
                       "usage: fort [options] entry.ft\n");
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
})

TEST(check_runs_the_front_end_and_stops, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    // No IR, no temporary and no --cc: the run ends after the front end, so -o
    // and --cc are unused.
    // D20.1
    const run_t run = RUN_CAPTURED("--check", "--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(access(box.out, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(check_beats_the_options_that_emit, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    // -S would write the module to -o; --check emits nothing at all, so the
    // options of a build are unused beside it.
    // D20.1
    const run_t run = RUN_CAPTURED("--check", "-S", "-c", "-o", box.out, "-lm", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(access(box.out, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(check_accepts_a_module_that_defines_no_main, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, NO_MAIN_SOURCE));
    // The `main` rule is not applied under --check.
    // D8.6, D20.1
    const run_t run = RUN_CAPTURED("--check", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(last_diags, "");
    sandbox_close(&box);
})

TEST(check_reports_a_broken_module_as_a_compile_error, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    // Every rule but the `main` rule holds under --check, the diagnostics
    // included.
    // D8.6, D14.2, D20.1
    const run_t run = RUN_CAPTURED("--check", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(last_diags, ":1:1: error: module 'nothere' not found"));
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
    sandbox_close(&box);
})

TEST(the_document_of_a_clean_module_is_one_line_with_four_keys, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    char want[CAPTURE_MAX];
    expect2(want, sizeof want, CLEAN_DOCUMENT, box.rt, box.entry);
    // Version, files, diagnostics and symbols, in that order, on one line ended
    // by a newline (toolchain.md 4.1).
    // D20.2
    TEST_ASSERT_EQ_STR(run.out, want);
    sandbox_close(&box);
})

TEST(the_document_lists_every_file_the_compiler_read, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn main() i32 { return 0; }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn add(i32 a, i32 b) i32 { return a + b; }\n"));
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // Every file read, in read order, so a client can clear stale diagnostics.
    // D20.2
    char want[CAPTURE_MAX];
    expect3(want, sizeof want, "\"files\":[\"%s\",\"%s\",\"%s\"]", box.rt, box.entry, util);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

// The path in is the path out, byte for byte. A client that names the entry
// relative to a directory of its own -- the VS Code extension names it relative
// to the workspace folder -- resolves every file of the answer against that same
// directory and needs no mapping of its own, which only holds while the compiler
// echoes what it was given (toolchain.md 2) rather than the absolute path the
// loader could just as well print. The run is made from inside the sandbox, so
// the argument is a bare file name; the working directory is restored before any
// assertion, since a failing one returns from the body at once.
// D14.2, D9.2
TEST(a_relative_entry_is_named_in_the_document_exactly_as_it_was_given, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    char home[PATH_CAP];
    const bool known = getcwd(home, sizeof home) != NULL;
    const bool moved = known && chdir(box.dir) == 0;
    run_t run;
    run.status = -1;
    run.out[0] = '\0';
    run.err[0] = '\0';
    if (moved) {
        // A relative standard library directory too, so that no absolute path
        // reaches the document from the sandbox's own environment.
        TEST_UNUSED(setenv("FORT_STD_DIR", "std", 1));
        run = RUN_CAPTURED("--check", "--json", "main.ft");
        TEST_UNUSED(chdir(home));
    }
    TEST_ASSERT_TRUE(moved);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    // The file list and the diagnostic both name it the way the command line
    // did, with no directory part added.
    TEST_ASSERT_NONNULL(strstr(run.out, "\"files\":[\"std/rt.ft\",\"main.ft\"]"));
    TEST_ASSERT_NONNULL(strstr(run.out, "\"file\":\"main.ft\",\"line\":1,\"col\":1"));
    TEST_ASSERT_NULL(strstr(run.out, box.dir));
    sandbox_close(&box);
})

// The same holds one directory down, where an absolute path would be the
// tempting thing to print: what comes back is the relative path as written.
TEST(a_relative_entry_below_the_directory_keeps_its_directory_part, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char sub[PATH_CAP];
    join(sub, sizeof sub, box.dir, "src");
    TEST_ASSERT_EQ_INT32(mkdir(sub, S_IRWXU), 0);
    char entry[PATH_CAP];
    join(entry, sizeof entry, sub, "main.ft");
    TEST_ASSERT_TRUE(write_source(entry, NO_MAIN_SOURCE));
    char home[PATH_CAP];
    const bool known = getcwd(home, sizeof home) != NULL;
    const bool moved = known && chdir(box.dir) == 0;
    run_t run;
    run.status = -1;
    run.out[0] = '\0';
    run.err[0] = '\0';
    if (moved) {
        TEST_UNUSED(setenv("FORT_STD_DIR", "std", 1));
        run = RUN_CAPTURED("--check", "--json", "src/main.ft");
        TEST_UNUSED(chdir(home));
    }
    TEST_ASSERT_TRUE(moved);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    char want[CAPTURE_MAX];
    expect2(want, sizeof want, CLEAN_DOCUMENT, "std/rt.ft", "src/main.ft");
    TEST_ASSERT_EQ_STR(run.out, want);
    sandbox_close(&box);
})

TEST(the_document_holds_the_diagnostic_the_text_form_holds, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    const run_t text = RUN_CAPTURED("--check", box.entry);
    char text_line[CAPTURE_MAX];
    TEST_UNUSED(snprintf(text_line, sizeof text_line, "%s", last_diags));
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, text.status);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    // The same file, position and message as the text form, with the end of the
    // range the text form does not print.
    // D20.2, D20.4
    char want[CAPTURE_MAX];
    expect1(want,
            sizeof want,
            "\"file\":\"%s\",\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":16,"
            "\"severity\":\"error\",\"message\":\"module 'nothere' not found\"",
            box.entry);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    TEST_ASSERT_NONNULL(strstr(text_line, ":1:1: error: module 'nothere' not found"));
    sandbox_close(&box);
})

TEST(the_document_nests_a_note_under_its_error, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    // The `note:` lines of an error are nested in its "notes", each with its own
    // range and no severity of its own.
    // D20.2
    char want[CAPTURE_MAX];
    expect2(want,
            sizeof want,
            "\"notes\":[{\"file\":\"%s\",\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":16,"
            "\"message\":\"looked for %s/nothere.ft\"}]",
            box.entry,
            box.dir);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

// The whole document of a module with one error, note included: the shape a
// client parses (toolchain.md 4.1).
// D20.2
static const char BAD_IMPORT_DOCUMENT[] =
    "{\"version\":1,\"files\":[\"%s\",\"%s\"],\"diagnostics\":[{\"file\":\"%s\",\"line\":1,"
    "\"col\":1,\"end_line\":1,\"end_col\":16,\"severity\":\"error\",\"message\":"
    "\"module 'nothere' not found\",\"notes\":[{\"file\":\"%s\",\"line\":1,\"col\":1,"
    "\"end_line\":1,\"end_col\":16,\"message\":\"looked for %s/nothere.ft\"}]}],"
    "\"symbols\":[]}\n";

TEST(the_document_of_a_failing_module_is_exact, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    char want[CAPTURE_MAX];
    TEST_UNUSED(snprintf(
        want, sizeof want, BAD_IMPORT_DOCUMENT, box.rt, box.entry, box.entry, box.entry, box.dir));
    TEST_ASSERT_EQ_STR(run.out, want);
    sandbox_close(&box);
})

// The whole document of the sandbox entry under --index: one record for the one
// name the module declares (toolchain.md 9.1).
// D20.3
static const char INDEXED_DOCUMENT[] =
    "{\"version\":1,\"files\":[\"%s\",\"%s\"],\"diagnostics\":[],\"symbols\":[{\"file\":\"%s\","
    "\"line\":1,\"col\":4,\"end_line\":1,\"end_col\":8,\"name\":\"main\",\"kind\":\"fn\","
    "\"type\":\"fn () i32\",\"is_decl\":true,\"decl\":{\"file\":\"%s\",\"line\":1,\"col\":4,"
    "\"end_line\":1,\"end_col\":8}}]}\n";

// The whole document of a dotted entry name: the file is never read, so
// `"files"` is empty and the one diagnostic stands at 1:1. A diagnostic naming a
// file `"files"` does not list is published all the same: `"files"` is what a
// client may clear, not what it publishes (toolchain.md 9.2).
// D9.1, D14.2
static const char DOTTED_ENTRY_DOCUMENT[] =
    "{\"version\":1,\"files\":[],\"diagnostics\":[{\"file\":\"%s\",\"line\":1,"
    "\"col\":1,\"end_line\":1,\"end_col\":1,\"severity\":\"error\",\"message\":"
    "\"entry file name 'my.app' cannot contain '.'\",\"notes\":[]}],\"symbols\":[]}\n";

TEST(a_dotted_entry_under_check_is_rejected_too, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char entry[PATH_CAP];
    join(entry, sizeof entry, box.dir, "my.app.ft");
    TEST_ASSERT_TRUE(write_source(entry, "fn main() i32 { return 0; }\n"));
    // The rule is not the one about `main`, which --check skips: the name of the
    // file is wrong whatever the file holds.
    // D8.6, D9.1, D20.1
    const run_t run = RUN_CAPTURED("--check", entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(last_diags, "entry file name 'my.app' cannot contain '.'"));
    sandbox_close(&box);
})

TEST(the_document_of_a_dotted_entry_is_exact, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char entry[PATH_CAP];
    join(entry, sizeof entry, box.dir, "my.app.ft");
    TEST_ASSERT_TRUE(write_source(entry, "fn main() i32 { return 0; }\n"));
    // Exit 1 with a complete document is a verdict a client reads.
    // D20.2
    const run_t run = RUN_CAPTURED("--check", "--json", entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    char want[CAPTURE_MAX];
    TEST_UNUSED(snprintf(want, sizeof want, DOTTED_ENTRY_DOCUMENT, entry));
    TEST_ASSERT_EQ_STR(run.out, want);
    sandbox_close(&box);
})

TEST(index_implies_check_and_json_and_fills_the_symbols, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    // --index is the whole command line an editor gives the compiler: it turns
    // on --check and --json by itself.
    // D20.3
    const run_t run = RUN_CAPTURED("--index", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    char want[CAPTURE_MAX];
    TEST_UNUSED(
        snprintf(want, sizeof want, INDEXED_DOCUMENT, box.rt, box.entry, box.entry, box.entry));
    TEST_ASSERT_EQ_STR(run.out, want);
    sandbox_close(&box);
})

TEST(index_runs_the_front_end_alone, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    // The check mode emits nothing and spawns no `--cc`, which --index does not
    // change.
    // D20.1
    const run_t run = RUN_CAPTURED("--index", "--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(access(box.out, F_OK), -1);
    sandbox_close(&box);
})

TEST(an_indexed_use_points_at_the_declaration_in_the_other_file, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(
        write_source(box.entry, "import util;\nfn main() i32 { return util.add(1, 2); }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn add(i32 a, i32 b) i32 { return a + b; }\n"));
    const run_t run = RUN_CAPTURED("--index", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // The use of `add` in the entry carries the declaration's name range in the
    // imported file, which is what go-to-definition follows.
    // D20.3
    char want[CAPTURE_MAX];
    TEST_UNUSED(snprintf(want,
                         sizeof want,
                         "{\"file\":\"%s\",\"line\":2,\"col\":29,\"end_line\":2,"
                         "\"end_col\":32,\"name\":\"add\",\"kind\":\"fn\","
                         "\"type\":\"fn (i32, i32) i32\",\"is_decl\":false,"
                         "\"decl\":{\"file\":\"%s\",\"line\":1,\"col\":4,\"end_line\":1,"
                         "\"end_col\":7}}",
                         box.entry,
                         util));
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(an_indexed_run_with_an_error_still_indexes_what_resolved, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    const run_t run = RUN_CAPTURED("--index", box.entry);
    // A verdict is exit 0 or 1 with a document, and a file with errors still
    // indexes everything the checker resolved.
    // D20.2, D20.3
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(run.out, "\"message\":\"module 'nothere' not found\""));
    TEST_ASSERT_NONNULL(strstr(run.out, "\"name\":\"main\",\"kind\":\"fn\""));
    sandbox_close(&box);
})

TEST(an_indexed_run_that_cannot_read_the_entry_leaves_stdout_empty, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char missing[PATH_CAP];
    join(missing, sizeof missing, box.dir, "gone.ft");
    // An unreadable entry is a usage error: exit 2 with stdout empty, which is
    // how a client tells a crash from a verdict.
    // D14.1, D20.2
    const run_t run = RUN_CAPTURED("--index", missing);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
    sandbox_close(&box);
})

TEST(index_after_check_and_json_is_the_same_run, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    // --index implies the two options, so spelling them as well changes nothing.
    // D20.3
    const run_t implied = RUN_CAPTURED("--index", box.entry);
    char want[CAPTURE_MAX];
    TEST_UNUSED(snprintf(want, sizeof want, "%s", implied.out));
    const run_t spelled = RUN_CAPTURED("--check", "--json", "--index", box.entry);
    TEST_ASSERT_EQ_INT32(spelled.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(spelled.out, want);
    sandbox_close(&box);
})

TEST(an_indexed_alias_declares_its_name_and_points_elsewhere, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(
        box.entry, "import util.add as plus;\nfn main() i32 { return plus(1, 2); }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn add(i32 a, i32 b) i32 { return a + b; }\n"));
    const run_t run = RUN_CAPTURED("--index", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    // The alias declares `plus` in the entry and its declaration is the function
    // in the imported file.
    // D9.3, D20.3
    char want[CAPTURE_MAX];
    TEST_UNUSED(snprintf(want,
                         sizeof want,
                         "\"name\":\"plus\",\"kind\":\"fn\",\"type\":\"fn (i32, i32) i32\","
                         "\"is_decl\":true,\"decl\":{\"file\":\"%s\",\"line\":1,\"col\":4,"
                         "\"end_line\":1,\"end_col\":7}}",
                         util));
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(a_document_without_index_has_an_empty_symbols_array, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    // The index is a member of every document and is filled by --index alone.
    // D20.2, D20.3
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_NONNULL(strstr(run.out, "\"symbols\":[]}"));
    sandbox_close(&box);
})

TEST(json_writes_no_text_diagnostic, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    // --json replaces the text form with the document.
    // D14.2, D20.2
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_NONNULL(strstr(run.out, "\"message\":\"module 'nothere' not found\""));
    sandbox_close(&box);
})

TEST(a_run_after_json_writes_its_text_diagnostics_again, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, BAD_IMPORT_SOURCE));
    const run_t json = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(json.status, FORT_EXIT_COMPILE_ERROR);
    // The text form is a mode of the process, so a --json run must leave it
    // as it found it.
    const run_t run = RUN_CAPTURED("--check", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(last_diags, "error: module 'nothere' not found"));
    sandbox_close(&box);
})

TEST(an_unreadable_entry_under_json_leaves_stdout_empty, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char missing[PATH_CAP];
    join(missing, sizeof missing, box.dir, "gone.ft");
    // Exit 2 with stdout empty is the crash a client tells from a verdict; the
    // `fort: error:` line stays on stderr.
    // D20.2, D14.1
    const run_t run = RUN_CAPTURED("--check", "--json", missing);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
    TEST_ASSERT_NONNULL(strstr(run.err, "fort: error: cannot read '"));
    sandbox_close(&box);
})

// Two statements that fail in a row, so the parser reports both.
// D14.2
static const char TWO_ERRORS_SOURCE[] = "fn main() i32 { i32 x = 1 }\nfn g( { return 0; }\n";

TEST(every_diagnostic_of_a_file_is_in_the_document_in_report_order, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, TWO_ERRORS_SOURCE));
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    // The order of the array is the order the diagnostics were reported, the
    // order of the text form.
    // D20.2
    char want[CAPTURE_MAX];
    expect2(want,
            sizeof want,
            "\"diagnostics\":[{\"file\":\"%s\",\"line\":1,\"col\":27,\"end_line\":1,"
            "\"end_col\":28,\"severity\":\"error\",\"message\":\"expected ';', found '}'\","
            "\"notes\":[]},{\"file\":\"%s\",\"line\":2,\"col\":7,\"end_line\":2,\"end_col\":8,"
            "\"severity\":\"error\",\"message\":\"expected a type, found '{'\",\"notes\":[]}]",
            box.entry,
            box.entry);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(a_lexical_error_is_one_diagnostic_at_an_empty_range, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn main() i32 { return 'abc; }\n"));
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    // A lexical error stops the file after one diagnostic, and the lexer reports
    // a position, which is the empty range there.
    // D14.2, D20.4
    char want[CAPTURE_MAX];
    expect1(want,
            sizeof want,
            "\"diagnostics\":[{\"file\":\"%s\",\"line\":1,\"col\":24,\"end_line\":1,"
            "\"end_col\":24,\"severity\":\"error\",",
            box.entry);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(a_diagnostic_of_an_imported_module_names_that_module, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn main() i32 { return 0; }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn add(i32 a { return a; }\n"));
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    // Every module of the closure is read and reported on, each diagnostic
    // naming its own file (module-system.md 10).
    // D14.2
    char want[CAPTURE_MAX];
    expect3(want, sizeof want, "\"files\":[\"%s\",\"%s\",\"%s\"]", box.rt, box.entry, util);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    expect1(want, sizeof want, "\"file\":\"%s\",\"line\":1,\"col\":14", util);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(an_import_cycle_is_one_diagnostic_of_the_document, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn main() i32 { return 0; }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "import main;\nfn add(i32 a) i32 { return a; }\n"));
    // A cycle is an error at the import that closes it (module-system.md 13),
    // reported like every other error under --check.
    // D9.5
    const run_t run = RUN_CAPTURED("--check", "--json", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(run.out,
                               "\"message\":\"circular import: 'main' imports 'util' "
                               "imports 'main'\""));
    sandbox_close(&box);
})

TEST(an_include_root_is_searched_under_check, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn main() i32 { return 0; }\n"));
    char lib[PATH_CAP];
    join(lib, sizeof lib, box.dir, "lib");
    TEST_ASSERT_EQ_INT32(mkdir(lib, S_IRWXU), 0);
    char util[PATH_CAP];
    join(util, sizeof util, lib, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn add(i32 a, i32 b) i32 { return a + b; }\n"));
    // The search roots are the same under --check, and the document names a file
    // as the compiler opened it.
    // D9.2, D20.1, D20.2
    const run_t run = RUN_CAPTURED("--check", "--json", "-I", lib, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    char want[CAPTURE_MAX];
    expect3(want, sizeof want, "\"files\":[\"%s\",\"%s\",\"%s\"]", box.rt, box.entry, util);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(a_quote_in_a_file_name_is_escaped_in_the_document, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char quoted[PATH_CAP];
    join(quoted, sizeof quoted, box.dir, "q\".ft");
    TEST_ASSERT_TRUE(write_source(quoted, NO_MAIN_SOURCE));
    // The document is JSON, so a name is escaped rather than ending the string
    // it is in.
    // D20.2
    const run_t run = RUN_CAPTURED("--check", "--json", quoted);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    char want[CAPTURE_MAX];
    expect2(want, sizeof want, "\"files\":[\"%s\",\"%s/q\\\".ft\"]", box.rt, box.dir);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(a_non_ascii_file_name_passes_through_the_document, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char accented[PATH_CAP];
    join(accented, sizeof accented, box.dir, "h\xC3\xA9llo.ft");
    TEST_ASSERT_TRUE(write_source(accented, NO_MAIN_SOURCE));
    // A well-formed UTF-8 sequence is written verbatim.
    // D20.2, D2.1
    const run_t run = RUN_CAPTURED("--check", "--json", accented);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    char want[CAPTURE_MAX];
    expect2(want, sizeof want, "\"files\":[\"%s\",\"%s\"]", box.rt, accented);
    TEST_ASSERT_NONNULL(strstr(run.out, want));
    sandbox_close(&box);
})

TEST(the_front_end_hands_out_the_files_it_read, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn main() i32 { return 0; }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn add(i32 a, i32 b) i32 { return a + b; }\n"));
    driver_options_t opts;
    driver_options_init(&opts);
    opts.entry = box.entry;
    opts.check = true;
    driver_files_t files;
    driver_files_init(&files);
    driver_analysis_t an;
    driver_analysis_init(&an);
    sb_t sink;
    sb_init(&sink);
    diag_capture(&sink);
    // The seam itself: no module is written when `ir_path` is NULL, and the
    // files come back in read order.
    // D20.1, D20.2
    const int status = driver_front_end(&opts, "fort", NULL, &files, &an, stderr);
    diag_capture(NULL);
    sb_free(&sink);
    TEST_ASSERT_EQ_INT32(status, FORT_EXIT_OK);
    // The runtime is read first, being a root of the closure.
    // D9.10
    TEST_ASSERT_EQ_UINT64(driver_files_count(&files), (uint64_t)3);
    TEST_ASSERT_EQ_STR(driver_files_at(&files, 0), box.rt);
    TEST_ASSERT_EQ_STR(driver_files_at(&files, 1), box.entry);
    TEST_ASSERT_EQ_STR(driver_files_at(&files, 2), util);
    driver_analysis_free(&an);
    driver_files_free(&files);
    driver_options_free(&opts);
    sandbox_close(&box);
})

TEST(the_analysis_outlives_the_front_end, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn main() i32 { return 0; }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn add(i32 a, i32 b) i32 { return a + b; }\n"));
    driver_options_t opts;
    driver_options_init(&opts);
    opts.entry = box.entry;
    opts.check = true;
    driver_analysis_t an;
    driver_analysis_init(&an);
    sb_t sink;
    sb_init(&sink);
    diag_capture(&sink);
    const int status = driver_front_end(&opts, "fort", NULL, NULL, &an, stderr);
    diag_capture(NULL);
    sb_free(&sink);
    TEST_ASSERT_EQ_INT32(status, FORT_EXIT_OK);
    // The trees and every annotation on them are readable after the run: the
    // symbols live until the caller frees the analysis (sym.h).
    // D20.3
    TEST_ASSERT_EQ_UINT64(module_set_count(&an.set), (uint64_t)3);
    const module_t* entry = module_set_entry(&an.set);
    TEST_ASSERT_NONNULL(entry);
    TEST_ASSERT_NONNULL(entry->ast->sym);
    TEST_ASSERT_EQ_INT32((int32_t)entry->ast->sym->kind, (int32_t)SYM_MODULE);
    TEST_ASSERT_TRUE(str_eq(entry->ast->sym->name, str_from_cstr("main")));
    // The module's one declaration is `fn main() i32`, whose symbol the
    // checker left on its node.
    const ast_node_t* decl = ast_child(entry->ast, ast_len(entry->ast) - 1);
    TEST_ASSERT_NONNULL(decl->sym);
    TEST_ASSERT_EQ_INT32((int32_t)decl->sym->kind, (int32_t)SYM_FN);
    TEST_ASSERT_TRUE(check_sym_count(&an.ck) > 0);
    driver_analysis_free(&an);
    driver_options_free(&opts);
    sandbox_close(&box);
})

TEST(a_front_end_that_wants_no_files_gets_none, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    driver_options_t opts;
    driver_options_init(&opts);
    opts.entry = box.entry;
    opts.check = true;
    // A caller that writes no document passes NULL and nothing is collected.
    // D20.2
    driver_analysis_t an;
    driver_analysis_init(&an);
    const int status = driver_front_end(&opts, "fort", NULL, NULL, &an, stderr);
    TEST_ASSERT_EQ_INT32(status, FORT_EXIT_OK);
    driver_analysis_free(&an);
    driver_options_free(&opts);
    sandbox_close(&box);
})

TEST(the_files_of_a_run_are_forgotten_when_the_list_is_freed, {
    driver_files_t files;
    driver_files_init(&files);
    TEST_ASSERT_EQ_UINT64(driver_files_count(&files), (uint64_t)0);
    driver_files_free(&files);
    // Freeing leaves the list usable, as every container of the compiler does.
    TEST_ASSERT_EQ_UINT64(driver_files_count(&files), (uint64_t)0);
    driver_files_free(&files);
})

// The entry file and the stdout file of the forked run below; a forked
// function takes no argument, so the test leaves them here.
static char forked_entry[PATH_CAP];
static char forked_stdout[PATH_CAP];

// A child that dies as an internal error does, in the middle of a check run: the
// front end runs, then the compiler exits 2 without the document ever being
// built, which is what leaves stdout empty. The locals stay live across the
// call, so their blocks are reachable for LeakSanitizer.
// D14.1, D20.2
static void check_run_then_internal_error(void) {
    driver_options_t opts;
    driver_options_init(&opts);
    opts.entry = forked_entry;
    opts.check = true;
    opts.json = true;
    driver_files_t files;
    driver_files_init(&files);
    driver_analysis_t an;
    driver_analysis_init(&an);
    TEST_UNUSED(driver_front_end(&opts, "fort", NULL, &files, &an, stderr));
    fatal_internal("simulated");
}

// Runs `fn` in a forked child whose stdout is the file at `path` and whose
// stderr is dropped, and returns its exit status or FORKED_ABNORMAL. test/fork.h
// captures a child's stderr; the contract is about its stdout, hence this one.
// D20.2
static int run_forked_stdout(void (*fn)(void), const char* path) {
    enum { FORKED_ABNORMAL = -1, FORKED_SETUP_FAILED = 127 };
    TEST_UNUSED(fflush(NULL));
    const pid_t pid = fork();
    if (pid < 0) {
        return FORKED_ABNORMAL;
    }
    if (pid == 0) {
        if (freopen(path, "wb", stdout) == NULL) {
            _exit(FORKED_SETUP_FAILED);
        }
        // The `fort: error:` line of the internal error is the run's, not the
        // suite's output.
        // D14.1
        if (freopen("/dev/null", "wb", stderr) == NULL) {
            _exit(FORKED_SETUP_FAILED);
        }
        fn();
        TEST_UNUSED(fflush(stdout));
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status)) {
        return FORKED_ABNORMAL;
    }
    return WEXITSTATUS(status);
}

// The size of a file, or -1 when it cannot be read.
static int64_t file_size(const char* path) {
    struct stat info;
    if (stat(path, &info) != 0) {
        return -1;
    }
    return (int64_t)info.st_size;
}

TEST(an_internal_error_in_a_check_run_leaves_stdout_empty_and_exits_2, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_UNUSED(snprintf(forked_entry, sizeof forked_entry, "%s", box.entry));
    join(forked_stdout, sizeof forked_stdout, box.dir, "stdout.txt");
    const int status = run_forked_stdout(check_run_then_internal_error, forked_stdout);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_INT64(file_size(forked_stdout), (int64_t)0);
    sandbox_close(&box);
})

int main(int argc, char** argv) {
    TEST_INIT("driver_check", argc, argv);
    TEST_RUN(json_without_check_is_a_usage_error);
    TEST_RUN(check_runs_the_front_end_and_stops);
    TEST_RUN(check_beats_the_options_that_emit);
    TEST_RUN(check_accepts_a_module_that_defines_no_main);
    TEST_RUN(check_reports_a_broken_module_as_a_compile_error);
    TEST_RUN(the_document_of_a_clean_module_is_one_line_with_four_keys);
    TEST_RUN(the_document_lists_every_file_the_compiler_read);
    TEST_RUN(a_relative_entry_is_named_in_the_document_exactly_as_it_was_given);
    TEST_RUN(a_relative_entry_below_the_directory_keeps_its_directory_part);
    TEST_RUN(the_document_holds_the_diagnostic_the_text_form_holds);
    TEST_RUN(the_document_nests_a_note_under_its_error);
    TEST_RUN(the_document_of_a_failing_module_is_exact);
    TEST_RUN(json_writes_no_text_diagnostic);
    TEST_RUN(a_run_after_json_writes_its_text_diagnostics_again);
    TEST_RUN(every_diagnostic_of_a_file_is_in_the_document_in_report_order);
    TEST_RUN(a_lexical_error_is_one_diagnostic_at_an_empty_range);
    TEST_RUN(a_diagnostic_of_an_imported_module_names_that_module);
    TEST_RUN(an_import_cycle_is_one_diagnostic_of_the_document);
    TEST_RUN(an_include_root_is_searched_under_check);
    TEST_RUN(a_quote_in_a_file_name_is_escaped_in_the_document);
    TEST_RUN(a_non_ascii_file_name_passes_through_the_document);
    TEST_RUN(a_dotted_entry_under_check_is_rejected_too);
    TEST_RUN(the_document_of_a_dotted_entry_is_exact);
    TEST_RUN(index_implies_check_and_json_and_fills_the_symbols);
    TEST_RUN(index_runs_the_front_end_alone);
    TEST_RUN(an_indexed_use_points_at_the_declaration_in_the_other_file);
    TEST_RUN(an_indexed_run_with_an_error_still_indexes_what_resolved);
    TEST_RUN(an_indexed_run_that_cannot_read_the_entry_leaves_stdout_empty);
    TEST_RUN(index_after_check_and_json_is_the_same_run);
    TEST_RUN(an_indexed_alias_declares_its_name_and_points_elsewhere);
    TEST_RUN(a_document_without_index_has_an_empty_symbols_array);
    TEST_RUN(the_front_end_hands_out_the_files_it_read);
    TEST_RUN(the_analysis_outlives_the_front_end);
    TEST_RUN(a_front_end_that_wants_no_files_gets_none);
    TEST_RUN(the_files_of_a_run_are_forgotten_when_the_list_is_freed);
    TEST_RUN(an_unreadable_entry_under_json_leaves_stdout_empty);
    TEST_RUN(an_internal_error_in_a_check_run_leaves_stdout_empty_and_exits_2);
    TEST_EXIT();
}
