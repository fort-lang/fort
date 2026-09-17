// The compiler driver (toolchain.md 1 and 2); see driver.h.
// D14.1, D14.3
#include "driver.h"

#include <errno.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "ast.h"
#include "ast_dump.h"
#include "check.h"
#include "containers.h"
#include "diag.h"
#include "gen.h"
#include "index.h"
#include "json.h"
#include "lexer.h"
#include "modules.h"
#include "parser.h"
#include "str.h"

// The environment handed to the spawned `--cc`. POSIX declares it in
// <unistd.h>, but glibc's is behind `#ifdef __USE_GNU`, which -std=c11 with
// _POSIX_C_SOURCE does not set, so the driver declares it itself.
extern char** environ;

static const char USAGE_LINE[] = "usage: fort [options] entry.ft";

// The option table of toolchain.md 1, printed by --help after the usage line.
static const char* const HELP_LINES[] = {
    "options:",
    "  -o <file>          write the output to <file> (default a.out)",
    "  -S                 stop after emitting the LLVM IR module <entry>.ll",
    "  -c                 stop after compiling that module to <entry>.o",
    "  -I <dir>           add a module search root; repeatable, in order",
    "  --std-dir <dir>    the standard library directory",
    "  --release          release mode",
    "  --no-bounds-check  remove the index and span checks; unsafe",
    "  -l<lib>            pass -l<lib> to the linker; repeatable, in order",
    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma) CMake sets the adjacent target text.
    "  --cc <path>        the clang that compiles and links the IR (default " FORT_DEFAULT_CC ")",
    // NOLINTNEXTLINE(bugprone-suspicious-missing-comma) CMake sets the adjacent target text.
    "  --target <triple>  pass --target=<triple> to --cc (default " FORT_DEFAULT_TARGET ")",
    "  -Xcc <arg>         pass <arg> to --cc verbatim; repeatable, in order",
    "  --check            run the front end only and stop; emit nothing",
    "  --json             write the check document to stdout; needs --check",
    "  --index            fill the document's identifier index; implies --check --json",
    "  --tokens           write the entry file's tokens to stdout and stop",
    "  --ast              write the entry file's syntax tree to stdout and stop",
    "  --help             print this help and exit",
    "  --version          print the compiler version and exit",
};

// The directory the temporary of toolchain.md 2 is created in when TMPDIR is
// not set, and the template mkdtemp fills in.
static const char TMP_DIR_DEFAULT[] = "/tmp";
static const char TMP_DIR_TEMPLATE[] = "/fort-XXXXXX";

// The bytes the entry file is read in under --tokens, as modules.c reads a
// module.
enum { ENTRY_READ_CHUNK = 4096 };

static const char MAC_TARGET_PREFIX[] = "arm64-apple-macosx";

// The symbolic link naming the running binary, and the buffer sizes the
// driver reads it with.
#if !defined(__APPLE__)
static const char PROC_SELF_EXE[] = "/proc/self/exe";
#endif
enum { EXE_PATH_FIRST_CAP = 256, EXE_PATH_MAX_CAP = 1 << 16 };

// ---- messages ------------------------------------------------------------------

// One `fort: error: <message>` line: usage errors, toolchain errors and internal
// errors are reported that way and exit with 2 (toolchain.md 1).
// D14.1
static void error_line(FILE* err, const char* message) {
    (void)fputs("fort: error: ", err);
    (void)fputs(message, err);
    (void)fputc('\n', err);
}

// `fort: error: <what> '<path>': <reason>`, the form of
// `cannot read 'x.ft': No such file or directory` (toolchain.md 1).
static void error_path(FILE* err, const char* what, const char* path, const char* reason) {
    sb_t m;
    sb_init(&m);
    sb_append(&m, what);
    sb_append(&m, " '");
    sb_append(&m, path);
    sb_append(&m, "': ");
    sb_append(&m, reason);
    error_line(err, sb_cstr(&m));
    sb_free(&m);
}

// A usage error, followed by the usage line (toolchain.md 1).
static void usage_error(FILE* err, const char* message, const char* detail) {
    sb_t m;
    sb_init(&m);
    sb_append(&m, message);
    if (detail != NULL) {
        sb_append(&m, " '");
        sb_append(&m, detail);
        sb_push(&m, '\'');
    }
    error_line(err, sb_cstr(&m));
    sb_free(&m);
    (void)fputs(USAGE_LINE, err);
    (void)fputc('\n', err);
}

const char* driver_usage_line(void) {
    return USAGE_LINE;
}

// ---- options -------------------------------------------------------------------

void driver_options_init(driver_options_t* opts) {
    opts->entry = NULL;
    opts->output = NULL;
    opts->std_dir = NULL;
    opts->cc = FORT_DEFAULT_CC;
    opts->target = FORT_DEFAULT_TARGET;
    opts->emit_ir = false;
    opts->compile_only = false;
    opts->release = false;
    opts->no_bounds_check = false;
    opts->check = false;
    opts->json = false;
    opts->index = false;
    opts->tokens = false;
    opts->ast = false;
    ptrvec_init(&opts->includes);
    ptrvec_init(&opts->libs);
    ptrvec_init(&opts->cc_args);
}

void driver_options_free(driver_options_t* opts) {
    ptrvec_free(&opts->includes);
    ptrvec_free(&opts->libs);
    ptrvec_free(&opts->cc_args);
}

static bool target_is_mac(const char* target) {
    return strncmp(target, MAC_TARGET_PREFIX, sizeof MAC_TARGET_PREFIX - 1U) == 0;
}

// Whether the target has one of the two forms in toolchain.md 1.
// D14.1
static bool target_form(const char* target) {
    if (strcmp(target, "x86_64-linux-gnu") == 0) {
        return true;
    }
    if (!target_is_mac(target)) {
        return false;
    }
    uint64_t digits = 0;
    uint64_t dots = 0;
    for (uint64_t i = sizeof MAC_TARGET_PREFIX - 1U; target[i] != '\0'; i++) {
        const char c = target[i];
        if (c >= '0' && c <= '9') {
            digits++;
        } else if (c == '.' && digits > 0 && dots < 2) {
            dots++;
            digits = 0;
        } else {
            return false;
        }
    }
    return dots == 2 && digits > 0;
}

// Rejects a target that this IR mode cannot use before it creates output.
// The token and AST modes do not call this function.
// D14.1
static bool target_allowed(const driver_options_t* opts, FILE* err) {
    if (!target_form(opts->target)) {
        usage_error(err, "unsupported target", opts->target);
        return false;
    }
    if (strcmp(opts->target, FORT_DEFAULT_TARGET) == 0) {
        return true;
    }
    if (opts->emit_ir) {
        if (opts->std_dir != NULL) {
            return true;
        }
        usage_error(err, "--std-dir is required for cross-target -S", NULL);
    } else {
        sb_t message;
        sb_init(&message);
        sb_append(&message,
                  opts->compile_only ? "cannot compile object for target" : "cannot link target");
        sb_append(&message, " '");
        sb_append(&message, opts->target);
        sb_append(&message, "' with a '");
        sb_append(&message, FORT_DEFAULT_TARGET);
        sb_append(&message, "' compiler");
        usage_error(err, sb_cstr(&message), NULL);
        sb_free(&message);
    }
    return false;
}

// Appends an argument the driver borrows: an argv string, a literal of this
// file or a copy owned by a string pool. posix_spawnp takes char* const[],
// so the vector holds char* and the driver never writes through them.
static void push_arg(ptrvec_t* v, const char* arg) {
    ptrvec_push(v, (void*)arg);
}

// The i-th element of a vector filled by push_arg.
static char* arg_at(const ptrvec_t* v, uint64_t i) {
    return (char*)v->items[i];
}

// The argument of an option that takes the following one, or NULL after
// reporting the usage error; *i is left on the argument. -o, -I, --std-dir,
// --cc, --target and -Xcc take the following argument, whatever it looks like.
// D14.1
static const char* take_argument(int argc, char** argv, int* i, FILE* err) {
    if (*i + 1 >= argc) {
        usage_error(err, "missing argument for option", argv[*i]);
        return NULL;
    }
    *i += 1;
    return argv[*i];
}

// Whether `arg` is `-l<lib>`: the library is part of the option, so it is one
// argument, unlike every other option that carries a value.
// D14.1
static bool is_link_option(const char* arg) {
    return arg[0] == '-' && arg[1] == 'l' && arg[2] != '\0';
}

// The options that carry no value: -S, -c, --release, --no-bounds-check,
// --check, --json, --index, --tokens and --ast, each of which sets one flag.
// D14.1, D20.1, D20.2, D20.3
static bool parse_flag(driver_options_t* opts, const char* arg) {
    if (strcmp(arg, "-S") == 0) {
        opts->emit_ir = true;
    } else if (strcmp(arg, "-c") == 0) {
        opts->compile_only = true;
    } else if (strcmp(arg, "--release") == 0) {
        opts->release = true;
    } else if (strcmp(arg, "--no-bounds-check") == 0) {
        opts->no_bounds_check = true;
    } else if (strcmp(arg, "--check") == 0) {
        opts->check = true;
    } else if (strcmp(arg, "--json") == 0) {
        opts->json = true;
    } else if (strcmp(arg, "--index") == 0) {
        // D20.3: --index implies --check and --json
        opts->index = true;
        opts->check = true;
        opts->json = true;
    } else if (strcmp(arg, "--tokens") == 0) {
        // D14.1: --tokens implies nothing and combines with nothing
        opts->tokens = true;
    } else if (strcmp(arg, "--ast") == 0) {
        // D14.1: --ast implies nothing and combines with nothing either
        opts->ast = true;
    } else {
        return false;
    }
    return true;
}

// The options that take the following argument. Returns false when `arg` is
// not one of them; *ok is false when the argument was missing. The last -o,
// --std-dir, --cc and --target win, while -I, -l and -Xcc accumulate in
// command-line order (toolchain.md 1).
// D14.1
static bool parse_valued(
    driver_options_t* opts, int argc, char** argv, int* i, FILE* err, bool* ok) {
    const char* arg = argv[*i];
    const bool is_output = strcmp(arg, "-o") == 0;
    const bool is_include = strcmp(arg, "-I") == 0;
    const bool is_std_dir = strcmp(arg, "--std-dir") == 0;
    const bool is_cc = strcmp(arg, "--cc") == 0;
    const bool is_target = strcmp(arg, "--target") == 0;
    const bool is_cc_arg = strcmp(arg, "-Xcc") == 0;
    if (!is_output && !is_include && !is_std_dir && !is_cc && !is_target && !is_cc_arg) {
        return false;
    }
    const char* value = take_argument(argc, argv, i, err);
    if (value == NULL) {
        *ok = false;
        return true;
    }
    if (is_output) {
        opts->output = value;
    } else if (is_include) {
        push_arg(&opts->includes, value);
    } else if (is_std_dir) {
        opts->std_dir = value;
    } else if (is_cc) {
        opts->cc = value;
    } else if (is_target) {
        opts->target = value;
    } else {
        push_arg(&opts->cc_args, value);
    }
    return true;
}

int driver_parse(driver_options_t* opts, int argc, char** argv, FILE* out, FILE* err) {
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (arg[0] != '-') {
            // D14.1: the first argument that does not start with '-'
            if (opts->entry != NULL) {
                usage_error(err, "unexpected argument", arg);
                return DRIVER_PARSE_ERROR;
            }
            opts->entry = arg;
        } else if (strcmp(arg, "--help") == 0) {
            (void)fprintf(out, "%s\n", USAGE_LINE);
            for (size_t line = 0; line < sizeof HELP_LINES / sizeof HELP_LINES[0]; line++) {
                (void)fprintf(out, "%s\n", HELP_LINES[line]);
            }
            return DRIVER_PARSE_DONE;
        } else if (strcmp(arg, "--version") == 0) {
            (void)fprintf(out, "fort %s\n", FORT_VERSION_STRING);
            return DRIVER_PARSE_DONE;
        } else if (is_link_option(arg)) {
            push_arg(&opts->libs, arg);
        } else if (!parse_flag(opts, arg)) {
            bool ok = true;
            if (!parse_valued(opts, argc, argv, &i, err, &ok)) {
                usage_error(err, "unknown option", arg);
                return DRIVER_PARSE_ERROR;
            }
            if (!ok) {
                return DRIVER_PARSE_ERROR;
            }
        }
    }
    if (opts->entry == NULL) {
        usage_error(err, "no entry file", NULL);
        return DRIVER_PARSE_ERROR;
    }
    if (opts->tokens && (opts->check || opts->json || opts->index)) {
        // D14.1: --tokens stops before the parser
        usage_error(err, "--tokens does not combine with --check, --json or --index", NULL);
        return DRIVER_PARSE_ERROR;
    }
    if (opts->ast && (opts->tokens || opts->check || opts->json || opts->index)) {
        // D14.1: --ast stops before the checker
        usage_error(err, "--ast does not combine with --tokens, --check, --json or --index", NULL);
        return DRIVER_PARSE_ERROR;
    }
    if (opts->json && !opts->check) {
        // D20.2: a build spawns a `--cc` that inherits stdout
        usage_error(err, "--json requires --check", NULL);
        return DRIVER_PARSE_ERROR;
    }
    return DRIVER_PARSE_OK;
}

// ---- paths ---------------------------------------------------------------------

// The position after the last '/' of `path`, that is the start of its last
// component.
static uint64_t last_component(str_t path) {
    uint64_t start = 0;
    for (uint64_t i = 0; i < path.len; i++) {
        if (path.ptr[i] == '/') {
            start = i + 1;
        }
    }
    return start;
}

str_t driver_entry_base(const char* entry) {
    str_t path = str_from_cstr(entry);
    const uint64_t start = last_component(path);
    str_t base = str_from_range(path.ptr + start, path.len - start);
    const str_t suffix = str_from_cstr(".ft");
    if (base.len > suffix.len) {
        const str_t tail = str_from_range(base.ptr + base.len - suffix.len, suffix.len);
        if (str_eq(tail, suffix)) {
            base = str_from_range(base.ptr, base.len - suffix.len);
        }
    }
    return base;
}

// The buffer's contents, interned in `pool` and NUL-terminated; the buffer
// is empty afterwards.
static str_t pool_take(str_pool_t* pool, sb_t* b) {
    const str_t s = str_pool_intern(pool, sb_view(b));
    sb_clear(b);
    return s;
}

// `<dir>/<name>`, interned in `pool`.
static str_t join_path(str_pool_t* pool, const char* dir, str_t name, const char* suffix) {
    sb_t b;
    sb_init(&b);
    if (dir != NULL) {
        sb_append(&b, dir);
        sb_push(&b, '/');
    }
    sb_append_str(&b, name);
    if (suffix != NULL) {
        sb_append(&b, suffix);
    }
    const str_t joined = pool_take(pool, &b);
    sb_free(&b);
    return joined;
}

str_t driver_default_output(const driver_options_t* opts, str_pool_t* pool) {
    // -S and -c together stop at the IR, so -S decides the suffix
    // (toolchain.md 1).
    if (opts->emit_ir) {
        return join_path(pool, NULL, driver_entry_base(opts->entry), ".ll");
    }
    if (opts->compile_only) {
        return join_path(pool, NULL, driver_entry_base(opts->entry), ".o");
    }
    return str_pool_intern(pool, str_from_cstr(FORT_DEFAULT_OUTPUT));
}

// The value of `name` in the environment, or NULL when it is unset or empty.
// The only variables the compiler reads are FORT_STD_DIR and TMPDIR
// (toolchain.md 1); an empty value names no directory, so it counts as unset.
static const char* env_dir(const char* name) {
    const char* value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return NULL;
    }
    return value;
}

str_t driver_std_dir_beside(const char* program, str_pool_t* pool) {
    const str_t path = str_from_cstr(program);
    const uint64_t dir_len = last_component(path);
    sb_t b;
    sb_init(&b);
    sb_append_str(&b, str_from_range(path.ptr, dir_len));
    sb_append(&b, FORT_STD_DIR_NAME);
    const str_t dir = pool_take(pool, &b);
    sb_free(&b);
    return dir;
}

// The path of the running binary. Linux reads /proc/self/exe. Darwin reads
// dyld's path and resolves it with realpath when possible.
static str_t exe_path(str_pool_t* pool) {
#if defined(__APPLE__)
    uint32_t cap = EXE_PATH_FIRST_CAP;
    while ((uint64_t)cap <= EXE_PATH_MAX_CAP) {
        const uint32_t allocated = cap;
        sb_t b;
        sb_init(&b);
        sb_reserve(&b, (uint64_t)cap);
        if (_NSGetExecutablePath(b.data, &cap) == 0) {
            char* canonical = realpath(b.data, NULL);
            const char* selected = canonical != NULL ? canonical : b.data;
            const str_t path = str_pool_intern(pool, str_from_cstr(selected));
            free(canonical);
            sb_free(&b);
            return path;
        }
        sb_free(&b);
        if (cap <= allocated || (uint64_t)cap > EXE_PATH_MAX_CAP) {
            return str_from_range(NULL, 0);
        }
    }
    return str_from_range(NULL, 0);
#else
    uint64_t cap = EXE_PATH_FIRST_CAP;
    while (cap <= EXE_PATH_MAX_CAP) {
        sb_t b;
        sb_init(&b);
        sb_reserve(&b, cap);
        const ssize_t len = readlink(PROC_SELF_EXE, b.data, (size_t)cap);
        if (len < 0) {
            sb_free(&b);
            return str_from_range(NULL, 0);
        }
        if ((uint64_t)len < cap) {
            const str_t path = str_pool_intern(pool, str_from_range(b.data, (uint64_t)len));
            sb_free(&b);
            return path;
        }
        sb_free(&b);
        cap = mem_mul(cap, 2U);
    }
    return str_from_range(NULL, 0);
#endif
}

str_t driver_std_dir(const driver_options_t* opts, const char* argv0, str_pool_t* pool) {
    if (opts->std_dir != NULL) {
        return str_pool_intern(pool, str_from_cstr(opts->std_dir));
    }
    const char* from_env = env_dir("FORT_STD_DIR");
    if (from_env != NULL) {
        return str_pool_intern(pool, str_from_cstr(from_env));
    }
    // D14.1: else `std` beside the running fort binary
    const str_t running = exe_path(pool);
    if (running.ptr != NULL) {
        return driver_std_dir_beside(running.ptr, pool);
    }
    // Without /proc, argv[0] names the binary; one without a slash was found
    // through PATH, whose directory the driver cannot know, so `std` in the
    // current directory is the last resort.
    return driver_std_dir_beside(argv0, pool);
}

// ---- the temporary directory (toolchain.md 2) ------------------------------------

// Creates the temporary directory with mkdtemp under $TMPDIR, `/tmp` by
// default; the path is interned in `pool` and the directory it was created
// in is stored in *parent_out. Returns the zero view and the errno of the
// failure in *error_out, captured before anything else can overwrite it.
static str_t make_temp_dir(str_pool_t* pool, const char** parent_out, int* error_out) {
    const char* parent = env_dir("TMPDIR");
    if (parent == NULL) {
        parent = TMP_DIR_DEFAULT;
    }
    *parent_out = parent;
    sb_t b;
    sb_init(&b);
    sb_append(&b, parent);
    sb_append(&b, TMP_DIR_TEMPLATE);
    // sb_cstr terminates the template in the buffer's own storage, which is
    // where mkdtemp replaces the six X in place.
    (void)sb_cstr(&b);
    str_t dir = str_from_range(NULL, 0);
    *error_out = 0;
    if (mkdtemp(b.data) != NULL) {
        dir = str_pool_intern(pool, str_from_cstr(b.data));
    } else {
        *error_out = errno;
    }
    sb_free(&b);
    return dir;
}

// Removes the temporary directory and the module in it, whether or not --cc
// succeeded (toolchain.md 2). The compiler writes exactly one file there, one
// `<entry>.ll`, so no directory walk is needed; a file already gone is not an
// error. A later ticket that writes a second temporary must remove it here too,
// and a signal that kills the compiler while clang runs still leaves the
// directory behind.
// D19.1
static void remove_temp_dir(const char* dir, const char* ir_path) {
    if (ir_path != NULL) {
        (void)unlink(ir_path);
    }
    (void)rmdir(dir);
}

// ---- the clang invocation --------------------------------------------------------
// D14.3

void driver_cc_argv(const driver_options_t* opts,
                    const char* ir_path,
                    const char* out_path,
                    str_pool_t* pool,
                    ptrvec_t* argv) {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "--target=");
    sb_append(&b, opts->target);
    const str_t target = pool_take(pool, &b);
    sb_free(&b);

    push_arg(argv, opts->cc);
    push_arg(argv, target.ptr);
    // -O2 in place of -O1 under --release; the compiler itself optimizes
    // nothing (toolchain.md 3).
    // D14.3: -O2 under --release; the compiler optimizes nothing
    push_arg(argv, opts->release ? "-O2" : "-O1");
    push_arg(argv, "-fPIE");
    if (!opts->compile_only) {
        // The executable is position-independent; -c stops at the object, so
        // it takes no -pie and no -l (toolchain.md 2).
        push_arg(argv, target_is_mac(FORT_DEFAULT_TARGET) ? "-Wl,-pie" : "-pie");
    }
    // The module carries its own target triple, which clang would warn about
    // (toolchain.md 2).
    push_arg(argv, "-Wno-override-module");
    if (opts->compile_only) {
        push_arg(argv, "-c");
    }
    push_arg(argv, "-o");
    push_arg(argv, out_path);
    // The one input is the module, which holds the whole program, the runtime
    // included. No -x ir: clang reads a `.ll` by its suffix and -x is sticky
    // (toolchain.md 2).
    // D9.10, D13.1
    push_arg(argv, ir_path);
    if (!opts->compile_only) {
        for (uint64_t i = 0; i < opts->libs.len; i++) {
            push_arg(argv, arg_at(&opts->libs, i));
        }
    }
    // D14.1: -Xcc arguments come last, verbatim and in order
    for (uint64_t i = 0; i < opts->cc_args.len; i++) {
        push_arg(argv, arg_at(&opts->cc_args, i));
    }
    ptrvec_push(argv, NULL);
}

// `cc failed with status N`, or `cc failed with signal N` for a --cc that
// died by a signal, which toolchain.md 1 does not name.
static void error_cc_status(FILE* err, const char* what, int64_t value) {
    sb_t m;
    sb_init(&m);
    sb_append(&m, "cc failed with ");
    sb_append(&m, what);
    sb_push(&m, ' ');
    sb_append_i64(&m, value);
    error_line(err, sb_cstr(&m));
    sb_free(&m);
}

// Runs --cc once over the module: it compiles and links in one invocation.
// Returns FORT_EXIT_OK, or FORT_EXIT_USAGE after reporting the failure.
// D14.1, D14.3
static int run_cc(const driver_options_t* opts,
                  const char* ir_path,
                  const char* out_path,
                  FILE* err) {
    str_pool_t pool;
    str_pool_init(&pool);
    ptrvec_t argv;
    ptrvec_init(&argv);
    driver_cc_argv(opts, ir_path, out_path, &pool, &argv);
    pid_t child = 0;
    const int spawned =
        posix_spawnp(&child, opts->cc, NULL, NULL, (char* const*)argv.items, environ);
    ptrvec_free(&argv);
    str_pool_free(&pool);
    if (spawned != 0) {
        error_path(err, "cannot run", opts->cc, strerror(spawned));
        return FORT_EXIT_USAGE;
    }
    int wait_status = 0;
    // A signal caught while clang runs interrupts the wait, not the child.
    pid_t waited = waitpid(child, &wait_status, 0);
    while (waited < 0 && errno == EINTR) {
        waited = waitpid(child, &wait_status, 0);
    }
    if (waited < 0) {
        error_path(err, "cannot wait for", opts->cc, strerror(errno));
        return FORT_EXIT_USAGE;
    }
    if (WIFSIGNALED(wait_status)) {
        error_cc_status(err, "signal", WTERMSIG(wait_status));
        return FORT_EXIT_USAGE;
    }
    const int status = WEXITSTATUS(wait_status);
    if (status != 0) {
        error_cc_status(err, "status", status);
        return FORT_EXIT_USAGE;
    }
    return FORT_EXIT_OK;
}

// ---- the closure's files ----------------------------------------------------------
// D20.2

void driver_files_init(driver_files_t* files) {
    str_pool_init(&files->pool);
    ptrvec_init(&files->names);
}

void driver_files_free(driver_files_t* files) {
    ptrvec_free(&files->names);
    str_pool_free(&files->pool);
}

uint64_t driver_files_count(const driver_files_t* files) {
    return files->names.len;
}

const char* driver_files_at(const driver_files_t* files, uint64_t i) {
    if (i >= files->names.len) {
        fatal_internal("driver_files_at: index out of range");
    }
    return (const char*)files->names.items[i];
}

// Copies the file of every module the loader read, in that order: the names
// the module set hands out die with it, and the document is written after it
// is released.
// D20.2
static void collect_files(const module_set_t* set, driver_files_t* files) {
    for (uint64_t i = 0; i < module_set_file_count(set); i++) {
        const str_t file = str_pool_intern(&files->pool, module_set_file_at(set, i));
        ptrvec_push(&files->names, (void*)file.ptr);
    }
}

// ---- the front end ----------------------------------------------------------------
// T-013, T-015: the front end is the seam of these two tickets

// Steps 1 and 2 of toolchain.md 2: the entry file's module path and root, then
// the import closure, with the search roots in order. Returns false after the
// diagnostics were reported.
// D9.2, D14.2
static bool load_closure(const driver_options_t* opts, const char* argv0, module_set_t* set) {
    str_pool_t pool;
    str_pool_init(&pool);
    for (uint64_t i = 0; i < opts->includes.len; i++) {
        // D9.2: the `-I` roots follow the entry file's directory
        module_set_add_root(set, arg_at(&opts->includes, i));
    }
    // D9.2, D14.1: `std` is looked up in the standard library directory
    const str_t std_dir = driver_std_dir(opts, argv0, &pool);
    module_set_std_dir(set, std_dir.ptr);
    str_pool_free(&pool);
    return module_set_load(set, opts->entry);
}

void driver_analysis_init(driver_analysis_t* an) {
    module_set_init(&an->set);
    check_init(&an->ck);
}

void driver_analysis_free(driver_analysis_t* an) {
    // The checker first: every annotation on a tree points into it, and a
    // symbol's name points into the source the module set owns (sym.h).
    check_free(&an->ck);
    module_set_free(&an->set);
}

// Step 4 of toolchain.md 2: the emitter writes one module for the whole
// closure (toolchain.md 6). An unwritable path is a toolchain error (exit 2)
// and a construct the emitter cannot lower yet is a compile error (exit 1), so
// a half-emitted module never reaches `--cc`.
// D19.1
static int emit_module(const driver_options_t* opts,
                       const check_t* ck,
                       const module_set_t* set,
                       const char* ir_path,
                       FILE* err) {
    gen_options_t gopts;
    gopts.release = opts->release;
    gopts.no_bounds_check = opts->no_bounds_check;
    gopts.target = opts->target;
    gen_t g;
    gen_init(&g, gopts);
    const bool ok = gen_program(&g, ck, set);
    int status = FORT_EXIT_OK;
    if (!ok) {
        status = FORT_EXIT_COMPILE_ERROR;
    } else {
        FILE* module = fopen(ir_path, "wb");
        if (module == NULL) {
            error_path(err, "cannot write", ir_path, strerror(errno));
            status = FORT_EXIT_USAGE;
        } else {
            const str_t text = gen_text(&g);
            (void)fwrite(text.ptr, 1, (size_t)text.len, module);
            if (fclose(module) != 0) {
                error_path(err, "cannot write", ir_path, strerror(errno));
                status = FORT_EXIT_USAGE;
            }
        }
    }
    gen_free(&g);
    return status;
}

int driver_front_end(const driver_options_t* opts,
                     const char* argv0,
                     const char* ir_path,
                     driver_files_t* files,
                     driver_analysis_t* an,
                     FILE* err) {
    // Step 1 of toolchain.md 2: an unreadable entry file is exit 2.
    // D14.1: an unreadable entry file is exit 2
    FILE* entry = fopen(opts->entry, "rb");
    if (entry == NULL) {
        error_path(err, "cannot read", opts->entry, strerror(errno));
        return FORT_EXIT_USAGE;
    }
    (void)fclose(entry);
    // Each run starts from an empty sink; the records of this one outlive it on
    // purpose, so that a caller can write them after the front end returns, and the
    // next diag_reset releases them.
    // D14.2
    diag_reset();
    const bool loaded = load_closure(opts, argv0, &an->set);
    if (files != NULL) {
        collect_files(&an->set, files);
    }
    // Step 3: every module that parsed is checked, the dependency order first, so
    // one file that did not parse never hides the errors of the others.
    //
    // Neither the modules nor the checker is released here: the annotations they own
    // live until the caller frees the analysis, which is what lets the index walk
    // read the trees afterwards (sym.h). A compilation builds a program, so the entry
    // module defines main; --check inspects one module instead, and the entry rule
    // does not apply to it.
    // D8.6, D9.10, D14.2, D20.1, D20.3
    an->ck.require_main = ir_path != NULL;
    const bool checked = check_program(&an->ck, &an->set);
    if (!loaded || !checked) {
        // D14.1
        return FORT_EXIT_COMPILE_ERROR;
    }
    if (ir_path == NULL) {
        // D20.1: --check stops after the front end, no module emitted
        return FORT_EXIT_OK;
    }
    // Step 4: the program's LLVM IR module. It is emitted while the analysis is
    // alive, since every annotation the emitter reads points into it (check.h).
    // D19.1
    return emit_module(opts, &an->ck, &an->set, ir_path, err);
}

// ---- the document of the check mode (toolchain.md 4.1) ----------------------------
// D20.2

// Builds the whole document into `doc`: the version, the files of the closure,
// the diagnostics of the run and the identifier index.
// D20.2
static void build_document(sb_t* doc, const driver_files_t* files, const index_t* ix) {
    json_t j;
    json_init(&j, doc);
    json_object_begin(&j);
    // D20.2
    json_key(&j, "version");
    json_uint(&j, FORT_JSON_VERSION);
    // D20.2: a client clears the stale diagnostics of a file
    json_key(&j, "files");
    json_array_begin(&j);
    for (uint64_t i = 0; i < driver_files_count(files); i++) {
        json_cstr(&j, driver_files_at(files, i));
    }
    json_array_end(&j);
    json_key(&j, "diagnostics");
    diag_write_json(&j);
    // D20.3, D20.2: the empty array without --index, always a member
    json_key(&j, "symbols");
    index_write_json(ix, &j);
    json_object_end(&j);
}

// Writes the document with one fwrite, so stdout holds a complete document or
// nothing and a client tells a crash from a verdict. The document is one line
// ended by a newline (toolchain.md 4.1).
// D20.2
static void write_document(FILE* out, const driver_files_t* files, const index_t* ix) {
    sb_t doc;
    sb_init(&doc);
    build_document(&doc, files, ix);
    sb_push(&doc, '\n');
    const str_t text = sb_view(&doc);
    (void)fwrite(text.ptr, 1, (size_t)text.len, out);
    (void)fflush(out);
    sb_free(&doc);
}

// ---- the token dump --------------------------------------------------------------
// D14.1

// The whole of `path` into `b`; false when it could not be opened or read,
// with the failure reported as `cannot read '<path>': <reason>`, the same
// line an unreadable entry file gets from the pipeline (toolchain.md 1).
static bool read_entry(const char* path, sb_t* b, FILE* err) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        error_path(err, "cannot read", path, strerror(errno));
        return false;
    }
    for (;;) {
        sb_reserve(b, ENTRY_READ_CHUNK);
        const size_t got = fread(b->data + b->len, 1, ENTRY_READ_CHUNK, file);
        b->len += (uint64_t)got;
        if (got < ENTRY_READ_CHUNK) {
            break;
        }
    }
    const bool ok = ferror(file) == 0;
    if (!ok) {
        error_path(err, "cannot read", path, strerror(errno));
    }
    (void)fclose(file);
    return ok;
}

// --tokens lexes the entry file alone -- no import is resolved and nothing is
// parsed -- and writes one line per token to `out` in the form of toolchain.md 1.
// A lexical error is reported as usual and lexing goes on, so the dump covers the
// whole file either way; the status is then a compile error.
// D14.1, D14.2
static int tokens_entry(const driver_options_t* opts, FILE* out, FILE* err) {
    sb_t source;
    sb_init(&source);
    if (!read_entry(opts->entry, &source, err)) {
        sb_free(&source);
        return FORT_EXIT_USAGE;
    }
    // Each run starts from an empty sink, as the front end does.
    diag_reset();
    str_pool_t pool;
    str_pool_init(&pool);
    tokvec_t toks;
    tokvec_init(&toks);
    const bool clean = lex_file(opts->entry, sb_view(&source), &pool, &toks);
    sb_t dump;
    sb_init(&dump);
    tok_dump(&toks, &dump);
    const str_t text = sb_view(&dump);
    if (text.len > 0) {
        (void)fwrite(text.ptr, 1, (size_t)text.len, out);
    }
    (void)fflush(out);
    sb_free(&dump);
    tokvec_free(&toks);
    str_pool_free(&pool);
    sb_free(&source);
    return clean ? FORT_EXIT_OK : FORT_EXIT_COMPILE_ERROR;
}

// ---- the syntax tree dump --------------------------------------------------------
// D14.1

// --ast lexes and parses the entry file alone -- no import is resolved and
// nothing is checked -- and writes the S-expression of ast_dump.h to `out`,
// followed by one newline. The parser recovers from a syntax error and the lexer
// from a lexical one, so a tree covering the whole file is dumped either way and
// the status is then a compile error.
// D14.1, D14.2
static int ast_entry(const driver_options_t* opts, FILE* out, FILE* err) {
    sb_t source;
    sb_init(&source);
    if (!read_entry(opts->entry, &source, err)) {
        sb_free(&source);
        return FORT_EXIT_USAGE;
    }
    // Each run starts from an empty sink, as the front end does.
    diag_reset();
    str_pool_t pool;
    str_pool_init(&pool);
    tokvec_t toks;
    tokvec_init(&toks);
    (void)lex_file(opts->entry, sb_view(&source), &pool, &toks);
    // D14.2: the tokens cover the whole file, so the parse runs
    ast_arena_t arena;
    ast_arena_init(&arena);
    const ast_node_t* mod = parse_module(opts->entry, toks.items, toks.len, &arena);
    sb_t dump;
    sb_init(&dump);
    ast_dump(mod, &dump);
    sb_push(&dump, '\n');
    const str_t text = sb_view(&dump);
    (void)fwrite(text.ptr, 1, (size_t)text.len, out);
    (void)fflush(out);
    sb_free(&dump);
    ast_arena_free(&arena);
    tokvec_free(&toks);
    str_pool_free(&pool);
    sb_free(&source);
    return diag_count() == 0 ? FORT_EXIT_OK : FORT_EXIT_COMPILE_ERROR;
}

// ---- the pipeline (toolchain.md 2) ------------------------------------------------

// Emits the module and, unless -S stops there, compiles and links it,
// removing the temporary directory on every path out.
static int compile_entry(const driver_options_t* opts, const char* argv0, FILE* err) {
    if (!target_allowed(opts, err)) {
        return FORT_EXIT_USAGE;
    }
    str_pool_t pool;
    str_pool_init(&pool);
    // A build reads no tree after the front end returned, so its analysis is
    // released on every path out of this function (sym.h).
    driver_analysis_t an;
    driver_analysis_init(&an);
    const char* out_path = opts->output;
    if (out_path == NULL) {
        out_path = driver_default_output(opts, &pool).ptr;
    }
    if (opts->emit_ir) {
        // -S writes the module to the output and stops, so it needs no
        // temporary (toolchain.md 2).
        const int status = driver_front_end(opts, argv0, out_path, NULL, &an, err);
        driver_analysis_free(&an);
        str_pool_free(&pool);
        return status;
    }
    const char* parent = TMP_DIR_DEFAULT;
    int failure = 0;
    const str_t dir = make_temp_dir(&pool, &parent, &failure);
    if (dir.ptr == NULL) {
        error_path(err, "cannot create a temporary directory in", parent, strerror(failure));
        driver_analysis_free(&an);
        str_pool_free(&pool);
        return FORT_EXIT_USAGE;
    }
    const str_t ir_path = join_path(&pool, dir.ptr, driver_entry_base(opts->entry), ".ll");
    int status = driver_front_end(opts, argv0, ir_path.ptr, NULL, &an, err);
    driver_analysis_free(&an);
    if (status == FORT_EXIT_OK) {
        status = run_cc(opts, ir_path.ptr, out_path, err);
    }
    remove_temp_dir(dir.ptr, ir_path.ptr);
    str_pool_free(&pool);
    return status;
}

// --check runs the front end and stops: no IR, no temporary, no `--cc`, and
// --json reports what it found as the document on `out` instead of the text
// diagnostics.
// D14.2, D20.1, D20.2
static int check_entry(const driver_options_t* opts, const char* argv0, FILE* out, FILE* err) {
    if (!target_form(opts->target)) {
        usage_error(err, "unsupported target", opts->target);
        return FORT_EXIT_USAGE;
    }
    driver_files_t files;
    driver_files_init(&files);
    // D20.3: freed only after the last byte of the document (sym.h)
    driver_analysis_t an;
    driver_analysis_init(&an);
    if (opts->json) {
        // D20.2: the records stay in the sink, where the document reads them
        diag_set_text(false);
    }
    const int status = driver_front_end(opts, argv0, NULL, &files, &an, err);
    if (opts->json) {
        // D20.2, D14.1: a usage or internal error is exit 2, stdout empty
        if (status == FORT_EXIT_OK || status == FORT_EXIT_COMPILE_ERROR) {
            index_t ix;
            index_init(&ix);
            if (opts->index) {
                // D20.3: the analysis is freed below, after the document (sym.h)
                index_build(&ix, &an.set);
            }
            write_document(out, &files, &ix);
            index_free(&ix);
        }
        // The text form is a process-wide mode, so it is restored: one run
        // must not change the next in a process that makes several.
        diag_set_text(true);
    }
    driver_analysis_free(&an);
    driver_files_free(&files);
    return status;
}

int driver_main(int argc, char** argv, FILE* out, FILE* err) {
    if (argc <= 1) {
        (void)fprintf(err, "%s\n", USAGE_LINE);
        return FORT_EXIT_USAGE;
    }
    driver_options_t opts;
    driver_options_init(&opts);
    const int parsed = driver_parse(&opts, argc, argv, out, err);
    int status = FORT_EXIT_OK;
    if (parsed == DRIVER_PARSE_ERROR) {
        status = FORT_EXIT_USAGE;
    } else if (parsed == DRIVER_PARSE_OK) {
        // --tokens stops after the lexer, --ast after the parser and --check
        // after the front end; every other run goes through the whole pipeline
        // of toolchain.md 2.
        // D14.1, D20.1: --tokens, --ast and --check each stop earlier
        if (opts.tokens) {
            status = tokens_entry(&opts, out, err);
        } else if (opts.ast) {
            status = ast_entry(&opts, out, err);
        } else if (opts.check) {
            status = check_entry(&opts, argv[0], out, err);
        } else {
            status = compile_entry(&opts, argv[0], err);
        }
    }
    driver_options_free(&opts);
    return status;
}
