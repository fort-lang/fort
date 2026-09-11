// The compiler driver (toolchain.md 1 and 2, D14.1, D14.3); see driver.h.
#include "driver.h"

#include <errno.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#include "containers.h"
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
    "  --cc <path>        the clang that compiles and links the IR (default clang)",
    "  --target <triple>  pass --target=<triple> to --cc (default x86_64-linux-gnu)",
    "  -Xcc <arg>         pass <arg> to --cc verbatim; repeatable, in order",
    "  --help             print this help and exit",
    "  --version          print the compiler version and exit",
};

// The directory the temporary of toolchain.md 2 is created in when TMPDIR is
// not set, and the template mkdtemp fills in.
static const char TMP_DIR_DEFAULT[] = "/tmp";
static const char TMP_DIR_TEMPLATE[] = "/fort-XXXXXX";

// The symbolic link naming the running binary, and the buffer sizes the
// driver reads it with.
static const char PROC_SELF_EXE[] = "/proc/self/exe";
enum { EXE_PATH_FIRST_CAP = 256, EXE_PATH_MAX_CAP = 1 << 16 };

// ---- messages ------------------------------------------------------------------

// One `fort: error: <message>` line: usage errors, toolchain errors (a --cc
// that failed) and internal errors are reported that way and exit with 2
// (D14.1, toolchain.md 1).
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
    ptrvec_init(&opts->includes);
    ptrvec_init(&opts->libs);
    ptrvec_init(&opts->cc_args);
}

void driver_options_free(driver_options_t* opts) {
    ptrvec_free(&opts->includes);
    ptrvec_free(&opts->libs);
    ptrvec_free(&opts->cc_args);
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
// --cc, --target and -Xcc take the following argument, whatever it looks
// like (D14.1).
static const char* take_argument(int argc, char** argv, int* i, FILE* err) {
    if (*i + 1 >= argc) {
        usage_error(err, "missing argument for option", argv[*i]);
        return NULL;
    }
    *i += 1;
    return argv[*i];
}

// Whether `arg` is `-l<lib>`: the library is part of the option, so it is one
// argument, unlike every other option that carries a value (D14.1).
static bool is_link_option(const char* arg) {
    return arg[0] == '-' && arg[1] == 'l' && arg[2] != '\0';
}

// The options that carry no value: -S, -c, --release and --no-bounds-check,
// each of which sets one flag (D14.1).
static bool parse_flag(driver_options_t* opts, const char* arg) {
    if (strcmp(arg, "-S") == 0) {
        opts->emit_ir = true;
    } else if (strcmp(arg, "-c") == 0) {
        opts->compile_only = true;
    } else if (strcmp(arg, "--release") == 0) {
        opts->release = true;
    } else if (strcmp(arg, "--no-bounds-check") == 0) {
        opts->no_bounds_check = true;
    } else {
        return false;
    }
    return true;
}

// The options that take the following argument. Returns false when `arg` is
// not one of them; *ok is false when the argument was missing. The last -o,
// --std-dir, --cc and --target win, while -I, -l and -Xcc accumulate in
// command-line order (D14.1, toolchain.md 1).
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
            // The first argument that does not start with '-' is the entry
            // file (D14.1); a second one is a usage error.
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

// The path of the running binary, read from /proc/self/exe; the zero view
// when it cannot be read. readlink does not terminate the path and reports
// truncation only by filling the buffer, so the buffer doubles until the
// result fits.
static str_t exe_path(str_pool_t* pool) {
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
}

str_t driver_std_dir(const driver_options_t* opts, const char* argv0, str_pool_t* pool) {
    if (opts->std_dir != NULL) {
        return str_pool_intern(pool, str_from_cstr(opts->std_dir));
    }
    const char* from_env = env_dir("FORT_STD_DIR");
    if (from_env != NULL) {
        return str_pool_intern(pool, str_from_cstr(from_env));
    }
    // Else `std` in the directory containing the fort binary (D14.1), which
    // is /proc/self/exe on the Linux the compiler runs on and targets.
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
// succeeded (toolchain.md 2). The compiler writes exactly one file there, the
// `<entry>.ll` of D19.1, so no directory walk is needed; a file already gone
// is not an error. A later ticket that writes a second temporary (T-015, if
// the emitter ever spills) must remove it here too, and a signal that kills
// the compiler while clang runs still leaves the directory behind.
static void remove_temp_dir(const char* dir, const char* ir_path) {
    if (ir_path != NULL) {
        (void)unlink(ir_path);
    }
    (void)rmdir(dir);
}

// ---- the clang invocation (D14.3) ------------------------------------------------

void driver_cc_argv(const driver_options_t* opts,
                    const char* ir_path,
                    const char* out_path,
                    const char* std_dir,
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
    // nothing (D14.3, toolchain.md 3).
    push_arg(argv, opts->release ? "-O2" : "-O1");
    push_arg(argv, "-fPIE");
    if (!opts->compile_only) {
        // The executable is position-independent; -c stops at the object, so
        // it takes no -pie, no runtime object and no -l (toolchain.md 2).
        push_arg(argv, "-pie");
    }
    // The module carries its own target triple, which clang would warn about
    // (toolchain.md 2).
    push_arg(argv, "-Wno-override-module");
    if (opts->compile_only) {
        push_arg(argv, "-c");
    }
    push_arg(argv, "-o");
    push_arg(argv, out_path);
    // No -x ir: -x is sticky and would also treat fort_rt.o as IR
    // (toolchain.md 2).
    push_arg(argv, ir_path);
    if (!opts->compile_only) {
        const str_t runtime = join_path(pool, std_dir, str_from_cstr(FORT_RUNTIME_OBJECT), NULL);
        push_arg(argv, runtime.ptr);
        for (uint64_t i = 0; i < opts->libs.len; i++) {
            push_arg(argv, arg_at(&opts->libs, i));
        }
    }
    // -Xcc arguments come last, verbatim and in command-line order (D14.1).
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

// Runs --cc once over the module: it compiles and links in one invocation
// (D14.3). Returns FORT_EXIT_OK, or FORT_EXIT_USAGE after reporting the
// failure (D14.1).
static int run_cc(const driver_options_t* opts,
                  const char* argv0,
                  const char* ir_path,
                  const char* out_path,
                  FILE* err) {
    str_pool_t pool;
    str_pool_init(&pool);
    const str_t std_dir = driver_std_dir(opts, argv0, &pool);
    ptrvec_t argv;
    ptrvec_init(&argv);
    driver_cc_argv(opts, ir_path, out_path, std_dir.ptr, &pool, &argv);
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

// ---- the front end (the seam of T-013 and T-015) ----------------------------------

int driver_front_end(const driver_options_t* opts, const char* ir_path, FILE* err) {
    // Step 1 of toolchain.md 2: an unreadable entry file is exit 2 (D14.1).
    FILE* entry = fopen(opts->entry, "rb");
    if (entry == NULL) {
        error_path(err, "cannot read", opts->entry, strerror(errno));
        return FORT_EXIT_USAGE;
    }
    (void)fclose(entry);
    // Steps 2 to 4 belong to T-013 (parse and check the import closure) and
    // T-015 (emit the module). Until they land the module is empty: the
    // driver is complete, the compiler behind it is not.
    FILE* module = fopen(ir_path, "wb");
    if (module == NULL) {
        error_path(err, "cannot write", ir_path, strerror(errno));
        return FORT_EXIT_USAGE;
    }
    (void)fclose(module);
    return FORT_EXIT_OK;
}

// ---- the pipeline (toolchain.md 2) ------------------------------------------------

// Emits the module and, unless -S stops there, compiles and links it,
// removing the temporary directory on every path out.
static int compile_entry(const driver_options_t* opts, const char* argv0, FILE* err) {
    str_pool_t pool;
    str_pool_init(&pool);
    const char* out_path = opts->output;
    if (out_path == NULL) {
        out_path = driver_default_output(opts, &pool).ptr;
    }
    if (opts->emit_ir) {
        // -S writes the module to the output and stops, so it needs no
        // temporary (toolchain.md 2).
        const int status = driver_front_end(opts, out_path, err);
        str_pool_free(&pool);
        return status;
    }
    const char* parent = TMP_DIR_DEFAULT;
    int failure = 0;
    const str_t dir = make_temp_dir(&pool, &parent, &failure);
    if (dir.ptr == NULL) {
        error_path(err, "cannot create a temporary directory in", parent, strerror(failure));
        str_pool_free(&pool);
        return FORT_EXIT_USAGE;
    }
    const str_t ir_path = join_path(&pool, dir.ptr, driver_entry_base(opts->entry), ".ll");
    int status = driver_front_end(opts, ir_path.ptr, err);
    if (status == FORT_EXIT_OK) {
        status = run_cc(opts, argv0, ir_path.ptr, out_path, err);
    }
    remove_temp_dir(dir.ptr, ir_path.ptr);
    str_pool_free(&pool);
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
        status = compile_entry(&opts, argv[0], err);
    }
    driver_options_free(&opts);
    return status;
}
