// The environment the emitter suites share (toolchain.md 6, D19): a sandbox
// directory under /tmp that one test writes its module into, the front end
// run over it, and the module text the emitter produced.
//
// A test runs with the sandbox as its working directory, so the `@.file.N`
// constants hold the file name the compiler opened and nothing in the text
// depends on where the suite was built (D19.5). It emits before check_free,
// because every annotation the emitter reads points into the checker
// (check.h).
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
#include "str.h"

#include "test.h"

enum { GEN_PATH_CAP = 512 };

// The verifier every emitted module must pass (D19.1); CMake passes its path.
#ifndef FORT_OPT
#define FORT_OPT "opt-18"
#endif

// The environment of the spawned verifier. POSIX declares `environ` in
// <unistd.h>, but glibc's is behind `#ifdef __USE_GNU`, which -std=c11 with
// _POSIX_C_SOURCE does not set, so the suite declares it itself.
extern char** environ;

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
            TEST_UNUSED(snprintf(child, sizeof child, "%s/%s", path, entry->d_name));
            gen_remove_tree(child);
        }
        entry = readdir(dir);
    }
    TEST_UNUSED(closedir(dir));
    TEST_UNUSED(rmdir(path));
}

static char gen_sandbox[GEN_PATH_CAP];
static char gen_home[GEN_PATH_CAP];
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
    gen_open = false;
}

static inline void gen_begin(void) {
    gen_done();
    char pattern[] = "/tmp/fort-gen-test-XXXXXX";
    if (mkdtemp(pattern) == NULL) {
        TEST_UNUSED(fputs("gen: cannot create the sandbox directory\n", stderr));
        exit(TEST_RESULT_ERR);
    }
    TEST_UNUSED(snprintf(gen_sandbox, sizeof gen_sandbox, "%s", pattern));
    sb_init(&gen_module);
    sb_init(&gen_diags);
    diag_capture(&gen_diags);
    diag_reset();
    gen_open = true;
    gen_ok = false;
}

// Writes `text` at `name` in the sandbox.
static inline void gen_write(const char* name, const char* text) {
    char path[GEN_PATH_CAP];
    TEST_UNUSED(snprintf(path, sizeof path, "%s/%s", gen_sandbox, name));
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return;
    }
    TEST_UNUSED(fputs(text, file));
    TEST_UNUSED(fclose(file));
}

// Loads, checks and emits the module of the file `name` of the sandbox, with
// the sandbox as the working directory. Returns whether every phase
// succeeded; the module text is in `gen_module` either way.
static inline bool gen_emit_file(const char* name, gen_options_t opts) {
    if (getcwd(gen_home, sizeof gen_home) == NULL) {
        return false;
    }
    if (chdir(gen_sandbox) != 0) {
        return false;
    }
    module_set_t set;
    module_set_init(&set);
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

// The module of `text` as `main.ft`, in checked mode (D11.1).
static inline bool emit(const char* text) {
    gen_begin();
    gen_write("main.ft", text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of `text` as `main.ft`, in release mode (D11.1).
static inline bool emit_release(const char* text) {
    gen_begin();
    gen_write("main.ft", text);
    gen_options_t opts;
    opts.release = true;
    opts.no_bounds_check = false;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of `text` as `main.ft`, with `--no-bounds-check` (D10.6).
static inline bool emit_unchecked(const char* text) {
    gen_begin();
    gen_write("main.ft", text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = true;
    gen_ok = gen_emit_file("main.ft", opts);
    return gen_ok;
}

// The module of a two-file program: `other` is written beside the entry file
// and reached by an `import`, so the closure has two modules and the emitter
// walks them in dependency order (D9.10, item 1).
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
    gen_ok = gen_emit_file(entry_name, opts);
    return gen_ok;
}

// The module of `text` under the file name `name`, which the `@.file.N`
// constants of its checks hold (D19.5).
static inline bool emit_as(const char* name, const char* text) {
    gen_begin();
    gen_write(name, text);
    gen_options_t opts;
    opts.release = false;
    opts.no_bounds_check = false;
    gen_ok = gen_emit_file(name, opts);
    return gen_ok;
}

// The emitted module.
static inline const char* ir(void) {
    return sb_cstr(&gen_module);
}

// The diagnostics the run reported, one line each.
static inline const char* gen_said(void) {
    return sb_cstr(&gen_diags);
}

// `fragment` when the module holds it, and the whole module otherwise, so a
// failing assertion shows what was emitted instead.
static inline const char* found(const char* fragment) {
    if (strstr(ir(), fragment) != NULL) {
        return fragment;
    }
    return ir();
}

// "absent" when the module does not hold `fragment`, and the whole module
// otherwise: the form a test asserting that nothing was emitted uses.
static inline const char* absent(const char* fragment) {
    if (strstr(ir(), fragment) == NULL) {
        return "absent";
    }
    return ir();
}

// The offset of `fragment` in the module, or -1: two of them in order say
// that one section precedes another (item 1).
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

// Runs the LLVM verifier over the emitted module: every module the compiler
// emits must pass `opt -passes=verify` (D19.1). Returns "verified", or the
// module when it does not, so a failing assertion shows the text.
static inline const char* verified(void) {
    char path[GEN_PATH_CAP];
    TEST_UNUSED(snprintf(path, sizeof path, "%s/module.ll", gen_sandbox));
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return "the module could not be written";
    }
    const str_t text = sb_view(&gen_module);
    TEST_UNUSED(fwrite(text.ptr, 1, (size_t)text.len, file));
    TEST_UNUSED(fclose(file));
    char program[] = FORT_OPT;
    char passes[] = "-passes=verify";
    char quiet[] = "-disable-output";
    char* argv[] = {program, passes, quiet, path, NULL};
    pid_t child = 0;
    if (posix_spawnp(&child, argv[0], NULL, NULL, argv, environ) != 0) {
        return "the IR verifier failed to start";
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) {
        return "the IR verifier could not be waited for";
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return ir();
    }
    return "verified";
}

// The contents of a file of the repository, as a NUL-terminated string owned
// by the caller-visible buffer, for the golden modules under test/ir.
static inline const char* gen_read(const char* path, sb_t* into) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    sb_clear(into);
    int byte = fgetc(file);
    while (byte != EOF) {
        sb_push(into, (char)byte);
        byte = fgetc(file);
    }
    TEST_UNUSED(fclose(file));
    return sb_cstr(into);
}

// A program whose body is `body` inside `fn i32 main()`, the shape most of
// the tests need.
static char gen_program_text[4096];
static inline const char* in_main(const char* body) {
    TEST_UNUSED(snprintf(
        gen_program_text, sizeof gen_program_text, "fn i32 main() {\n%s    return 0;\n}\n", body));
    return gen_program_text;
}

#endif
