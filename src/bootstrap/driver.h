// The compiler driver: the command line, the temporary directory and the
// invocation of clang (toolchain.md 1 and 2, D14.1, D14.3).
//
// driver_main parses the options, hands the entry file to the front end,
// which writes one LLVM IR module (D19.1), and runs `--cc` over that module
// once to compile and link it with the runtime object. The front end is the
// seam driver_front_end below: tickets T-013 and T-015 implement it.
//
// `--check` stops after the front end, with no module, no temporary and no
// `--cc` (D20.1), and `--json` reports what it found as one JSON document on
// stdout instead of the text diagnostics of D14.2 (D20.2, toolchain.md 4.1).
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, messages assembled with sb_t instead of printf formats.
#ifndef FORT_DRIVER_H
#define FORT_DRIVER_H

#include <stdbool.h>
#include <stdio.h>

#include "containers.h"
#include "str.h"

// The version printed by --version.
#define FORT_VERSION_STRING "0.1.0"

// Exit statuses of D14.1; usage, toolchain (`--cc` failed) and internal
// errors share FATAL_EXIT_STATUS of str.h.
enum {
    FORT_EXIT_OK = 0,
    FORT_EXIT_COMPILE_ERROR = 1,
    FORT_EXIT_USAGE = FATAL_EXIT_STATUS,
};

// Defaults of toolchain.md 1: `--cc` is a clang because nothing else reads
// LLVM IR (D14.1, D19.1), the target triple is x86_64-linux-gnu, the output
// of a full build is a.out and the standard library sits in `std` beside the
// binary when $FORT_STD_DIR is unset.
#define FORT_DEFAULT_CC "clang"
#define FORT_DEFAULT_TARGET "x86_64-linux-gnu"
#define FORT_DEFAULT_OUTPUT "a.out"
#define FORT_STD_DIR_NAME "std"

// The C runtime object linked into every program, inside <std-dir>
// (toolchain.md 2).
#define FORT_RUNTIME_OBJECT "fort_rt.o"

// The command line after parsing (toolchain.md 1). Every pointer borrows an
// argv string or a literal above; the three vectors hold `char*` argv
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
    bool check;           // --check: the front end alone (D20.1)
    bool json;            // --json: the document of D20.2 on stdout
    ptrvec_t includes;    // -I roots, searched in command-line order (D9.2)
    ptrvec_t libs;        // -l<lib> as given, passed to the linker in order
    ptrvec_t cc_args;     // -Xcc arguments, passed verbatim after the rest
} driver_options_t;

// Sets every option to its default; the vectors are empty.
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
// --help and --version print to `out`; a usage error prints
// `fort: error: <message>` and the usage line to `err` (toolchain.md 1).
int driver_parse(driver_options_t* opts, int argc, char** argv, FILE* out, FILE* err);

// The usage line that `fort` with no arguments and --help print.
const char* driver_usage_line(void);

// The entry file's base name without its directory and its `.ft` suffix:
// `main.ft` and `lib/main.ft` both give `main` (toolchain.md 1). The view
// borrows the entry string.
str_t driver_entry_base(const char* entry);

// The default output path (toolchain.md 1): `<base>.ll` with -S, `<base>.o`
// with -c, else a.out, in the current directory as cc does. The result is
// interned in `pool` and NUL-terminated.
str_t driver_default_output(const driver_options_t* opts, str_pool_t* pool);

// `std` in the directory containing `program`, or `std` alone when `program`
// has no directory part (D14.1). Interned in `pool` and NUL-terminated.
str_t driver_std_dir_beside(const char* program, str_pool_t* pool);

// The standard library directory (toolchain.md 1): --std-dir, else
// $FORT_STD_DIR, else `std` beside the fort binary, which is /proc/self/exe
// when it can be read and `argv0` otherwise. Interned in `pool` and
// NUL-terminated.
str_t driver_std_dir(const driver_options_t* opts, const char* argv0, str_pool_t* pool);

// Builds the invocation of D14.3 and toolchain.md 2 into `argv`, which must
// be empty, as borrowed `char*` pointers followed by the NULL terminator
// posix_spawnp needs; the strings it must synthesize are interned in `pool`.
void driver_cc_argv(const driver_options_t* opts,
                    const char* ir_path,
                    const char* out_path,
                    const char* std_dir,
                    str_pool_t* pool,
                    ptrvec_t* argv);

// The `"version"` of the check mode's document: 1 for the form of D20.2.
enum { FORT_JSON_VERSION = 1 };

// The files of the import closure, in the order the compiler read them: the
// `"files"` array of the document (D20.2). The names are copies, since the
// module set that read them is released before the document is written.
// Zero-initialized storage is not one: driver_files_init prepares it.
typedef struct {
    str_pool_t pool; // owns every name
    ptrvec_t names;  // const char*, NUL-terminated, in read order
} driver_files_t;

void driver_files_init(driver_files_t* files);

// Releases the names and the pool; the list is empty and usable afterwards.
void driver_files_free(driver_files_t* files);

// The files, in read order. `i` past the end is an internal error.
uint64_t driver_files_count(const driver_files_t* files);
const char* driver_files_at(const driver_files_t* files, uint64_t i);

// The front end of toolchain.md 2, steps 1 to 4: read the entry file, parse
// the import closure (module-system.md 10), check every module in dependency
// order and write the program's LLVM IR module to `ir_path` (D19.1). The
// search roots are the entry file's directory, the `-I` options in order and
// the standard library directory driver_std_dir names (D9.2), which is why
// `argv0` is needed. Emission is the seam ticket T-015 fills in; until then a
// closure that resolves and checks writes an empty module, which is enough to
// exercise the options, the temporary and the clang invocation.
// Returns FORT_EXIT_OK, FORT_EXIT_COMPILE_ERROR, or FORT_EXIT_USAGE for an
// unreadable entry file (D14.1). Only the `fort: error:` line of a usage or
// toolchain error goes to `err`: the compile-time diagnostics of D14.2 go to
// stderr through diag.h, which main.c relies on and a test redirects with
// diag_capture.
//
// `ir_path` is NULL under `--check`, which emits no module (D20.1); `files`
// collects the closure's file names for the document of D20.2 and is NULL
// when the caller wants none. The diagnostics the run reported stay in the
// sink of diag.h, where diag_write_json reads them.
int driver_front_end(const driver_options_t* opts,
                     const char* argv0,
                     const char* ir_path,
                     driver_files_t* files,
                     FILE* err);

// Runs the compiler with argv[1..argc-1]; out and err are where --help,
// --version, the document of `--json` and the `fort: error:` lines go
// (stdout and stderr in main). Returns the process exit status.
int driver_main(int argc, char** argv, FILE* out, FILE* err);

#endif
