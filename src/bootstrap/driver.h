// Parses compiler options, runs the front end, and invokes the selected C compiler.
#ifndef FORT_DRIVER_H
#define FORT_DRIVER_H

#include <stdbool.h>
#include <stdio.h>

#include "check.h"
#include "containers.h"
#include "modules.h"
#include "str.h"

// The version printed by --version.
#define FORT_VERSION_STRING "0.1.0"

// The exit statuses; usage, toolchain (`--cc` failed) and internal errors
// share FATAL_EXIT_STATUS of str.h.
enum {
    FORT_EXIT_OK = 0,
    FORT_EXIT_COMPILE_ERROR = 1,
    FORT_EXIT_USAGE = FATAL_EXIT_STATUS,
};

// CMake sets the host defaults for production. The fallback values keep a
// direct Linux C build usable.
#ifndef FORT_DEFAULT_CC
#define FORT_DEFAULT_CC "clang"
#endif
#ifndef FORT_DEFAULT_TARGET
#define FORT_DEFAULT_TARGET "x86_64-linux-gnu"
#endif
#define FORT_DEFAULT_OUTPUT "a.out"
#define FORT_STD_DIR_NAME "std"

// The command line after parsing. Every pointer borrows an
// argv string or a literal above. The three vectors hold `char*` argv
// strings in command-line order and own only their slots.
typedef struct {
    const char* entry;    // the entry file, NULL until one is seen
    const char* output;   // -o, NULL for the default of driver_default_output
    const char* std_dir;  // --std-dir, NULL for $FORT_STD_DIR or std beside the binary
    const char* cc;       // --cc
    const char* target;   // --target
    bool emit_ir;         // -S
    bool compile_only;    // -c
    bool release;         // --release
    bool no_bounds_check; // --no-bounds-check
    bool check;           // --check: the front end alone
    bool json;            // --json, the document on stdout
    bool index;           // --index, the identifier index in it
    bool tokens;          // --tokens: the entry file's tokens on stdout
    bool ast;             // --ast: the entry file's syntax tree on stdout
    ptrvec_t includes;    // -I roots, searched in command-line order
    ptrvec_t libs;        // -l<lib> as given, passed to the linker in order
    ptrvec_t cc_args;     // -Xcc arguments, passed verbatim after the rest
} driver_options_t;

// Sets every option to its default. The vectors are empty.
void driver_options_init(driver_options_t* opts);

// Releases the vectors' slots. The strings are borrowed and are untouched.
void driver_options_free(driver_options_t* opts);

// The outcome of driver_parse.
enum {
    DRIVER_PARSE_OK = 0,    // *opts holds the command line
    DRIVER_PARSE_DONE = 1,  // --help or --version was printed; exit 0
    DRIVER_PARSE_ERROR = 2, // a usage error was reported; exit 2
};

// Parses argv[1..argc-1] into *opts, which driver_options_init has prepared.
// --help and --version print to `out`. A usage error prints
// `fort: error: <message>` and the usage line to `err`.
int driver_parse(driver_options_t* opts, int argc, char** argv, FILE* out, FILE* err);

// The usage line that `fort` with no arguments and --help print.
const char* driver_usage_line(void);

// The entry file's base name without its directory and its `.ft` suffix:
// `main.ft` and `lib/main.ft` both give `main`. The view
// borrows the entry string.
str_t driver_entry_base(const char* entry);

// The default output path: `<base>.ll` with -S, `<base>.o`
// with -c, else a.out, in the current directory as cc does. The result is
// interned in `pool` and NUL-terminated.
str_t driver_default_output(const driver_options_t* opts, str_pool_t* pool);

// `std` in the directory containing `program`, or `std` alone when `program` has
// no directory part. Interned in `pool` and NUL-terminated.
str_t driver_std_dir_beside(const char* program, str_pool_t* pool);

// Returns the standard library directory. It uses --std-dir, then $FORT_STD_DIR, then `std` beside
// the fort binary. The binary path comes from /proc/self/exe or `argv0`. Interned in `pool` and
// NUL-terminated.
str_t driver_std_dir(const driver_options_t* opts, const char* argv0, str_pool_t* pool);

// Builds the C compiler invocation in empty `argv`. It appends borrowed `char*` pointers and the
// NULL terminator required by posix_spawnp. `pool` holds synthesized strings.
void driver_cc_argv(const driver_options_t* opts,
                    const char* ir_path,
                    const char* out_path,
                    str_pool_t* pool,
                    ptrvec_t* argv);

// The `"version"` of the check mode's document: 1 for this form of it.
enum { FORT_JSON_VERSION = 1 };

// The files of the import closure, in the order the compiler read them: the
// `"files"` array of the document. The names are copies, since the module set
// that read them is released before the document is written. Zero-initialized
// storage is not one: driver_files_init prepares it.
typedef struct {
    str_pool_t pool; // owns every name
    ptrvec_t names;  // const char*, NUL-terminated, in read order
} driver_files_t;

void driver_files_init(driver_files_t* files);

// Releases the names and the pool. The list is empty and usable afterwards.
void driver_files_free(driver_files_t* files);

// The files, in read order. `i` past the end is an internal error.
uint64_t driver_files_count(const driver_files_t* files);
const char* driver_files_at(const driver_files_t* files, uint64_t i);

// Holds the state from one front-end run. The modules own each syntax tree. The checker owns the
// symbols and types referenced by tree annotations. A tree's `sym` and `type` slots become invalid
// after checker release (sym.h). The caller releases the analysis after the final tree pass. The
// index walk is that pass. Zero-initialized storage is not one: driver_analysis_init prepares it,
// and one analysis serves one run.
typedef struct {
    module_set_t set; // the import closure and the arena that owns every tree
    check_t ck;       // the symbols and types the annotations point into
} driver_analysis_t;

void driver_analysis_init(driver_analysis_t* an);

// Releases the checker, then the modules. An annotation points into the checker. A symbol name
// points into module source. The analysis is empty and no tree may be read afterwards.
void driver_analysis_free(driver_analysis_t* an);

// The front end reads the entry file and parses the import closure. It checks each module in
// dependency order and writes the program's LLVM IR module to `ir_path`. It uses the entry
// directory, ordered `-I` options, and the standard library directory as search roots. `argv0`
// helps driver_std_dir find the last root. gen.h emits the module while checker annotations remain
// valid. An unsupported construct adds a compile error. The driver does not keep a partial module.
// Returns FORT_EXIT_OK, FORT_EXIT_COMPILE_ERROR, or FORT_EXIT_USAGE for an unreadable entry file.
// Only the `fort: error:` line of a usage or toolchain error goes to `err`: the compile-time
// diagnostics go to stderr through diag.h. `ir_path` is NULL under `--check`, which emits no
// module. `files` collects closure file names for the document. It is NULL when unneeded. The
// diagnostics the run reported stay in the sink of diag.h, where diag_write_json reads them.
// driver_analysis_init prepares caller-owned `an`. This function does not release it. The caller
// can index its trees after return, then release the analysis (sym.h).
int driver_front_end(const driver_options_t* opts,
                     const char* argv0,
                     const char* ir_path,
                     driver_files_t* files,
                     driver_analysis_t* an,
                     FILE* err);

// Runs the compiler with argv[1..argc-1]. `out` receives --help, --version, and --json output.
// `err` receives `fort: error:` lines. main passes stdout and stderr. Returns the process exit
// status.
int driver_main(int argc, char** argv, FILE* out, FILE* err);

#endif
