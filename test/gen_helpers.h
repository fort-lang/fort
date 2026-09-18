// Provides the shared code-generation test environment.
//
// A test runs with the sandbox as its working directory. The `@.file.N` constants hold the file
// name the compiler opened and nothing in the text depends on where the suite was built. It emits
// before check_free, because every annotation the emitter reads points into the checker (check.h).
//
// The helpers are static inline and the state is per suite, so a suite that
// uses only some of them still builds under -Werror.
#ifndef FORT_TEST_GEN_HELPERS_H
#define FORT_TEST_GEN_HELPERS_H

#include <dirent.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/wait.h>

#include "check.h"
#include "diag.h"
#include "gen.h"
#include "modules.h"
#include "runtime_sig.h"
#include "str.h"

#include "test.h"

enum { GEN_PATH_CAP = 512 };

// The verifier every emitted module must pass. CMake passes its path, and falls back to this name
// when it found none at configure time. A missing verifier surfaces here rather than at configure
// time.
#ifndef FORT_OPT
#define FORT_OPT "opt-18"
#endif

// The status a shell and posix_spawn's child both use when exec fails: the one exit status that
// means the tool is missing rather than unhappy.
enum { EXEC_FAILED_STATUS = 127 };

// The environment of the spawned verifier. POSIX declares `environ` in <unistd.h>, but glibc's is
// behind `#ifdef __USE_GNU`, which -std=c11 with _POSIX_C_SOURCE does not set. The suite declares
// it itself.
extern char** environ;

// Writing it, reading it or removing it would answer about the wrong module, or delete the wrong
// file. `written` is the snprintf result: using it is also what keeps gcc from warning that the
// call may truncate.
static inline void gen_path_fits(int written, size_t cap, const char* what) {
    if (written < 0 || (size_t)written >= cap) {
        TEST_UNUSED(fprintf(stderr, "gen: path too long for %zu bytes: %s\n", cap, what));
        exit(TEST_RESULT_ERR);
    }
}

// Writes `<dir>/<rel>` into `dst` and exits when either component does not fit.
static inline void gen_join_path(char* dst, size_t cap, const char* dir, const char* rel) {
    const int written = snprintf(dst, cap, "%s/%s", dir, rel);
    if (written < 0 || (size_t)written >= cap) {
        TEST_UNUSED(fprintf(stderr, "gen: path too long for %zu bytes: %s/%s\n", cap, dir, rel));
        exit(TEST_RESULT_ERR);
    }
}

static inline const char* gen_fitted(const char* report, int written, size_t cap) {
    if (written < 0 || (size_t)written >= cap) {
        return "the block report did not fit its buffer";
    }
    return report;
}

// Removes a directory and everything below it.
static inline void gen_remove_tree(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        TEST_UNUSED(unlink(path));
        return;
    }
    const struct dirent* entry = readdir(dir);
    while (entry != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char child[GEN_PATH_CAP];
            gen_join_path(child, sizeof child, path, entry->d_name);
            gen_remove_tree(child);
        }
        entry = readdir(dir);
    }
    TEST_UNUSED(closedir(dir));
    TEST_UNUSED(rmdir(path));
}

static char gen_sandbox[GEN_PATH_CAP];
static char gen_home[GEN_PATH_CAP];
static char gen_std[GEN_PATH_CAP];
static sb_t gen_module;
static sb_t gen_diags;
static bool gen_open = false;
static bool gen_ok = false;

// Releases the module text, the capture and the tree of the previous test.
static inline void gen_done(void) {
    if (!gen_open) {
        return;
    }
    diag_capture(NULL);
    sb_free(&gen_module);
    sb_free(&gen_diags);
    gen_remove_tree(gen_sandbox);
    gen_sandbox[0] = '\0';
    gen_std[0] = '\0';
    gen_open = false;
}

static inline void gen_no_verifier(const char* what) {
    TEST_UNUSED(fputs("gen: ", stderr));
    TEST_UNUSED(fputs(what, stderr));
    TEST_UNUSED(fputs(": ", stderr));
    TEST_UNUSED(fputs(FORT_OPT, stderr));
    TEST_UNUSED(fputs(" (llvm-18, see tools/provision.sh)\n", stderr));
    exit(TEST_RESULT_ERR);
}

static inline void gen_begin(void) {
    gen_done();
    char pattern[] = "/tmp/fort-gen-test-XXXXXX";
    if (mkdtemp(pattern) == NULL) {
        TEST_UNUSED(fputs("gen: cannot create the sandbox directory\n", stderr));
        exit(TEST_RESULT_ERR);
    }
    // Rejects a truncated copy of the path that `mkdtemp` writes.
    gen_path_fits(
        snprintf(gen_sandbox, sizeof gen_sandbox, "%s", pattern), sizeof gen_sandbox, pattern);
    sb_init(&gen_module);
    sb_init(&gen_diags);
    diag_capture(&gen_diags);
    diag_reset();
    gen_std[0] = '\0';
    gen_open = true;
    gen_ok = false;
}

// Writes `text` at `name` in the sandbox, making the one directory a nested module path needs
// (`util/chars.ft` is the module `util.chars`).
static inline void gen_write(const char* name, const char* text) {
    char path[GEN_PATH_CAP];
    const char* slash = strchr(name, '/');
    if (slash != NULL) {
        char dir[GEN_PATH_CAP];
        gen_path_fits(snprintf(dir, sizeof dir, "%s/%.*s", gen_sandbox, (int)(slash - name), name),
                      sizeof dir,
                      name);
        TEST_UNUSED(mkdir(dir, S_IRWXU));
    }
    gen_join_path(path, sizeof path, gen_sandbox, name);
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return;
    }
    TEST_UNUSED(fputs(text, file));
    TEST_UNUSED(fclose(file));
}

// Loads, checks and emits the module of the file `name` of the sandbox, with the sandbox as the
// working directory. Returns whether every phase succeeded; the module text is in `gen_module`
// either way.
static inline bool gen_emit_file(const char* name, gen_options_t opts) {
    if (getcwd(gen_home, sizeof gen_home) == NULL) {
        return false;
    }
    if (chdir(gen_sandbox) != 0) {
        return false;
    }
    module_set_t set;
    module_set_init(&set);
    if (gen_std[0] != '\0') {
        // A standard library directory makes the loader take `std.rt` into the closure as a root.
        // It is what a build does; without one the emitter's calls into the runtime reach a name
        // the module neither defines nor declares. It is what gen_runtime_declarations answers for.
        module_set_std_dir(&set, gen_std);
    }
    const bool loaded = module_set_load(&set, name);
    check_t ck;
    check_init(&ck);
    ck.require_main = true;
    const bool checked = check_program(&ck, &set);
    gen_t g;
    gen_init(&g, opts);
    bool emitted = false;
    if (loaded && checked) {
        emitted = gen_program(&g, &ck, &set);
        sb_clear(&gen_module);
        sb_append_str(&gen_module, gen_text(&g));
    }
    gen_free(&g);
    check_free(&ck);
    module_set_free(&set);
    TEST_UNUSED(chdir(gen_home));
    return loaded && checked && emitted;
}

// The module of `text` as `main.ft`, in checked mode.
static inline bool emit(const char* text) {
    gen_begin();
    gen_write("main.ft", text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    opts.target = NULL;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of `text` for one selected target, in checked mode.
static inline bool emit_target(const char* text, const char* target) {
    gen_begin();
    gen_write("main.ft", text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    opts.target = target;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of `text` as `main.ft`, in release mode.
static inline bool emit_release(const char* text) {
    gen_begin();
    gen_write("main.ft", text);
    gen_options_t opts;
    opts.release = true;
    opts.no_bounds_check = false;
    opts.target = NULL;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of `text` as `main.ft`, with `--no-bounds-check`.
static inline bool emit_unchecked(const char* text) {
    gen_begin();
    gen_write("main.ft", text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = true;
    opts.target = NULL;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of a two-file program: `other` is written beside the entry file and reached by an
// `import`. The closure has two modules and the emitter walks them in dependency order.
static inline bool emit_two(const char* entry_name,
                            const char* entry_text,
                            const char* other_name,
                            const char* other_text) {
    gen_begin();
    gen_write(other_name, other_text);
    gen_write(entry_name, entry_text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    opts.target = NULL;
    gen_ok = gen_emit_file(entry_name, opts);
    return gen_ok;
}

// The module of a program of `count` files: `names[i]` holds `texts[i]` and `names[0]` is the entry
// file. A closure of any size is emitted from one call. A name holding a `/` is a nested module
// path.
static inline bool emit_files(const char* const* names, const char* const* texts, uint64_t count) {
    gen_begin();
    for (uint64_t i = count; i > 0; i--) {
        gen_write(names[i - 1], texts[i - 1]);
    }
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    opts.target = NULL;
    gen_ok = count > 0 && gen_emit_file(names[0], opts);
    return gen_ok;
}

// Emits a main module with runtime definitions and their attribute groups.
static inline bool emit_with_runtime(const char* runtime, const char* text) {
    gen_begin();
    gen_write("std/rt.ft", runtime);
    gen_write("main.ft", text);
    gen_join_path(gen_std, sizeof gen_std, gen_sandbox, "std");
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    opts.target = NULL;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of `text` and its test runtime for one selected target.
static inline bool emit_with_runtime_target(const char* runtime,
                                            const char* text,
                                            const char* target) {
    gen_begin();
    gen_write("std/rt.ft", runtime);
    gen_write("main.ft", text);
    gen_join_path(gen_std, sizeof gen_std, gen_sandbox, "std");
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    opts.target = target;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of `text` under the file name `name`, which the `@.file.N` constants of its checks
// hold.
static inline bool emit_as(const char* name, const char* text) {
    gen_begin();
    gen_write(name, text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    opts.target = NULL;
    gen_ok = gen_emit_file(name, opts);
    return gen_ok;
}

// The emitted module, valid until the next emission or `gen_done`.
static inline const char* ir(void) {
    return sb_cstr(&gen_module);
}

// The diagnostics the run reported, one line each.
// The next emission or `gen_done` invalidates the result.
static inline const char* gen_said(void) {
    return sb_cstr(&gen_diags);
}

// `fragment` when the module holds it, and the whole module otherwise, so a failing assertion shows
// what was emitted instead.
static inline const char* found(const char* fragment) {
    if (strstr(ir(), fragment) != NULL) {
        return fragment;
    }
    return ir();
}

// "absent" when the module does not hold `fragment`, and the whole module otherwise: the form a
// test asserting that nothing was emitted uses.
static inline const char* absent(const char* fragment) {
    if (strstr(ir(), fragment) == NULL) {
        return "absent";
    }
    return ir();
}

// How many times `fragment` occurs in the module: what a test asking that one definition,
// declaration or table was emitted once counts.
static inline uint64_t occurrences(const char* fragment) {
    uint64_t n = 0;
    const char* p = strstr(ir(), fragment);
    while (p != NULL) {
        n++;
        p = strstr(p + 1, fragment);
    }
    return n;
}

// The offset of `fragment` in the module, or -1: two of them in order say that one section precedes
// another.
static inline int64_t at(const char* fragment) {
    const char* hit = strstr(ir(), fragment);
    if (hit == NULL) {
        return -1;
    }
    return (int64_t)(hit - ir());
}

// Whether `first` appears before `second`, both being present.
static inline bool before(const char* first, const char* second) {
    const int64_t a = at(first);
    const int64_t b = at(second);
    return a >= 0 && b >= 0 && a < b;
}

// Whether `text` begins with the literal `prefix`, whose length the literal itself gives, so that
// no length is written twice.
static inline bool gen_starts_with(const char* text, const char* prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static inline bool gen_is_terminator(const char* p) {
    return gen_starts_with(p, "  br ") || gen_starts_with(p, "  ret ") ||
           gen_starts_with(p, "  unreachable") || gen_starts_with(p, "  switch ");
}

// Whether the line at `p` begins an instruction rather than continuing the one above. Every
// instruction is indented by exactly two spaces. A line indented further, or one holding the `]`
// that closes a `switch`, is a continuation of the `switch` above it. It is the one terminator LLVM
// prints over several lines.
static inline bool gen_is_instruction(const char* p) {
    return gen_starts_with(p, "  ") && p[2] != ' ' && p[2] != ']';
}

// Reports block terminator counts and instructions that follow a terminator.
// Returns "one terminator per block" when all closed definitions pass.
// A formatted report uses shared static storage until the next call.
static inline const char* gen_block_terminators_of(const char* text) {
    static char report[GEN_PATH_CAP];
    const char* p = text;
    bool in_function = false;
    bool open = false;
    bool seen = false;
    int64_t count = 0;
    char label[GEN_PATH_CAP];
    label[0] = '\0';
    while (*p != '\0') {
        const char* eol = strchr(p, '\n');
        if (eol == NULL) {
            break;
        }
        const size_t len = (size_t)(eol - p);
        if (!in_function) {
            in_function = gen_starts_with(p, "define ");
            seen = seen || in_function;
            open = false;
        } else if (len == 1 && *p == '}') {
            if (open && count != 1) {
                return gen_fitted(
                    report,
                    snprintf(
                        report, sizeof report, "%s has %lld terminators", label, (long long)count),
                    sizeof report);
            }
            in_function = false;
        } else if (len > 1 && *p != ' ' && p[len - 1] == ':') {
            if (open && count != 1) {
                return gen_fitted(
                    report,
                    snprintf(
                        report, sizeof report, "%s has %lld terminators", label, (long long)count),
                    sizeof report);
            }
            const size_t keep = len < sizeof label ? len : sizeof label - 1;
            TEST_UNUSED(memcpy(label, p, keep));
            label[keep] = '\0';
            open = true;
            count = 0;
        } else if (open && gen_is_instruction(p)) {
            if (gen_is_terminator(p)) {
                count++;
            } else if (count > 0) {
                return gen_fitted(
                    report,
                    snprintf(
                        report, sizeof report, "%s has an instruction after its terminator", label),
                    sizeof report);
            }
        }
        p = eol + 1;
    }
    if (!seen || in_function) {
        // A module with no definition, or one whose last definition never
        // closed, would otherwise pass without a block being looked at.
        return "the module holds no closed definition";
    }
    return "one terminator per block";
}

// The same over the module the last emission produced.
static inline const char* gen_block_terminators(void) {
    return gen_block_terminators_of(ir());
}

// Returns true when the emitted module defines `symbol`.
static inline bool gen_defines(const char* symbol) {
    const char* line = sb_cstr(&gen_module);
    while (line != NULL && *line != '\0') {
        const char* end_of_line = strchr(line, '\n');
        const char* hit = strstr(line, symbol);
        if (strncmp(line, "define ", strlen("define ")) == 0 && hit != NULL &&
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

// Adds missing runtime declarations to the verifier file.
// It does not change the emitted module that the tests inspect.
static inline void gen_runtime_declarations(FILE* file) {
    TEST_UNUSED(fputs("\n", file));
    for (uint64_t i = 0; i < (uint64_t)RT_COUNT; i++) {
        const rt_entry_t rt = (rt_entry_t)i;
        char symbol[GEN_PATH_CAP];
        TEST_UNUSED(snprintf(symbol, sizeof symbol, "@\"%s\"(", rt_entry_name(rt)));
        if (gen_defines(symbol)) {
            // The module defines it, so a declaration beside it would be the redefinition `opt`
            // rejects.
            continue;
        }
        TEST_UNUSED(fputs("declare ", file));
        TEST_UNUSED(fputs(ir_result_text(rt_entry_result(rt)), file));
        TEST_UNUSED(fputs(" @\"", file));
        TEST_UNUSED(fputs(rt_entry_name(rt), file));
        TEST_UNUSED(fputs("\"(", file));
        for (uint32_t p = 0; p < rt_entry_param_count(rt); p++) {
            if (p > 0) {
                TEST_UNUSED(fputs(", ", file));
            }
            TEST_UNUSED(fputs(ir_param_text(rt_entry_param(rt, p)), file));
        }
        TEST_UNUSED(fputs(")\n", file));
    }
}

// Runs the block scan before the LLVM verifier.
// The scan rejects multiple terminators, which `opt` can silently split.
// Returns "verified", a scan report, or the rejected module.
static inline const char* verified(void) {
    const char* blocks = gen_block_terminators();
    if (strcmp(blocks, "one terminator per block") != 0) {
        return blocks;
    }
    char path[GEN_PATH_CAP];
    gen_join_path(path, sizeof path, gen_sandbox, "module.ll");
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return "the module could not be written";
    }
    const str_t text = sb_view(&gen_module);
    TEST_UNUSED(fwrite(text.ptr, 1, (size_t)text.len, file));
    gen_runtime_declarations(file);
    TEST_UNUSED(fclose(file));
    char program[] = FORT_OPT;
    char passes[] = "-passes=verify";
    char quiet[] = "-disable-output";
    char* argv[] = {program, passes, quiet, path, NULL};
    pid_t child = 0;
    const int spawned = posix_spawnp(&child, argv[0], NULL, NULL, argv, environ);
    int status = 0;
    if (spawned == 0 && waitpid(child, &status, 0) != child) {
        gen_no_verifier("the IR verifier could not be waited for");
    }
    // posix_spawnp reports only the failures it can see before the fork. A failed exec is the child
    // exiting 127, and either way the environment is broken and not the module.
    if (spawned != 0 || !WIFEXITED(status) || WEXITSTATUS(status) == EXEC_FAILED_STATUS) {
        gen_no_verifier("the IR verifier could not be run");
    }
    if (WEXITSTATUS(status) != 0) {
        return ir();
    }
    return "verified";
}

// A program whose body is `body` inside `fn main() i32`.
// The next call invalidates the returned shared-buffer result.
static char gen_program_text[4096];
static inline const char* in_main(const char* body) {
    TEST_UNUSED(snprintf(
        gen_program_text, sizeof gen_program_text, "fn main() i32 {\n%s    return 0;\n}\n", body));
    return gen_program_text;
}

#endif
