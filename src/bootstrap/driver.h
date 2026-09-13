// The compiler driver: the command line, the temporary directory and the
// invocation of clang (toolchain.md 1 and 2).
// D14.1, D14.3
//
// driver_main parses the options, hands the entry file to the front end, which
// writes one LLVM IR module, and runs `--cc` over that module once to compile
// and link it with the runtime object. The front end is the seam
// driver_front_end below.
// D19.1
//
// `--check` stops after the front end, with no module, no temporary and no
// `--cc`, and `--json` reports what it found as one JSON document on stdout
// instead of the text diagnostics (toolchain.md 4.1). `--index`
// implies both and fills that document's identifier index, which index.h
// builds from the trees the run leaves behind.
// D20.1, D20.2, D20.3
//
// `--tokens` stops one pass earlier still: it lexes the entry file alone,
// opening no import and parsing nothing, and writes the token dump of lexer.h
// to stdout (toolchain.md 1). It is what holds the two compilers' lexers
// against each other while src/fort is written.
// D14.1
//
// `--ast` stops between the two: it lexes and parses the entry file alone,
// opening no import and checking nothing, and writes the S-expression of
// ast_dump.h to stdout (toolchain.md 1). It is what holds the two compilers'
// parsers against each other the same way.
// D14.1
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, messages assembled with sb_t instead of printf formats.
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

/// The exit statuses; usage, toolchain (`--cc` failed) and internal errors
/// share FATAL_EXIT_STATUS of str.h.
/// D14.1
enum {
    FORT_EXIT_OK = 0,
    FORT_EXIT_COMPILE_ERROR = 1,
    FORT_EXIT_USAGE = FATAL_EXIT_STATUS,
};

// Defaults of toolchain.md 1: `--cc` is a clang because nothing else reads LLVM
// IR, the target triple is x86_64-linux-gnu, the output of a full build is a.out
// and the standard library sits in `std` beside the binary when $FORT_STD_DIR is
// unset.
// D14.1, D19.1
#define FORT_DEFAULT_CC "clang"
#define FORT_DEFAULT_TARGET "x86_64-linux-gnu"
#define FORT_DEFAULT_OUTPUT "a.out"
#define FORT_STD_DIR_NAME "std"

/// The command line after parsing (toolchain.md 1). Every pointer borrows an
/// argv string or a literal above; the three vectors hold `char*` argv
/// strings in command-line order and own only their slots.
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
    bool check;           // D20.1: --check: the front end alone
    bool json;            // D20.2: --json, the document on stdout
    bool index;           // D20.3: --index, the identifier index in it
    bool tokens;          // D14.1: --tokens: the entry file's tokens on stdout
    bool ast;             // D14.1: --ast: the entry file's syntax tree on stdout
    ptrvec_t includes;    // D9.2: -I roots, searched in command-line order
    ptrvec_t libs;        // -l<lib> as given, passed to the linker in order
    ptrvec_t cc_args;     // -Xcc arguments, passed verbatim after the rest
} driver_options_t;

/// Sets every option to its default; the vectors are empty.
void driver_options_init(driver_options_t* opts);

/// Releases the vectors' slots. The strings are borrowed and are untouched.
void driver_options_free(driver_options_t* opts);

/// The outcome of driver_parse.
enum {
    DRIVER_PARSE_OK = 0,    // *opts holds the command line
    DRIVER_PARSE_DONE = 1,  // --help or --version was printed; exit 0
    DRIVER_PARSE_ERROR = 2, // a usage error was reported; exit 2
};

/// Parses argv[1..argc-1] into *opts, which driver_options_init has prepared.
/// --help and --version print to `out`; a usage error prints
/// `fort: error: <message>` and the usage line to `err` (toolchain.md 1).
int driver_parse(driver_options_t* opts, int argc, char** argv, FILE* out, FILE* err);

/// The usage line that `fort` with no arguments and --help print.
const char* driver_usage_line(void);

/// The entry file's base name without its directory and its `.ft` suffix:
/// `main.ft` and `lib/main.ft` both give `main` (toolchain.md 1). The view
/// borrows the entry string.
str_t driver_entry_base(const char* entry);

/// The default output path (toolchain.md 1): `<base>.ll` with -S, `<base>.o`
/// with -c, else a.out, in the current directory as cc does. The result is
/// interned in `pool` and NUL-terminated.
str_t driver_default_output(const driver_options_t* opts, str_pool_t* pool);

/// `std` in the directory containing `program`, or `std` alone when `program` has
/// no directory part. Interned in `pool` and NUL-terminated.
/// D14.1
str_t driver_std_dir_beside(const char* program, str_pool_t* pool);

/// The standard library directory (toolchain.md 1): --std-dir, else
/// $FORT_STD_DIR, else `std` beside the fort binary, which is /proc/self/exe
/// when it can be read and `argv0` otherwise. Interned in `pool` and
/// NUL-terminated.
str_t driver_std_dir(const driver_options_t* opts, const char* argv0, str_pool_t* pool);

/// Builds the invocation of toolchain.md 2 into `argv`, which must be empty, as
/// borrowed `char*` pointers followed by the NULL terminator posix_spawnp needs;
/// the strings it must synthesize are interned in `pool`.
/// D14.3
void driver_cc_argv(const driver_options_t* opts,
                    const char* ir_path,
                    const char* out_path,
                    str_pool_t* pool,
                    ptrvec_t* argv);

/// The `"version"` of the check mode's document: 1 for this form of it.
/// D20.2
enum { FORT_JSON_VERSION = 1 };

/// The files of the import closure, in the order the compiler read them: the
/// `"files"` array of the document. The names are copies, since the module set
/// that read them is released before the document is written. Zero-initialized
/// storage is not one: driver_files_init prepares it.
/// D20.2
typedef struct {
    str_pool_t pool; // owns every name
    ptrvec_t names;  // const char*, NUL-terminated, in read order
} driver_files_t;

void driver_files_init(driver_files_t* files);

/// Releases the names and the pool; the list is empty and usable afterwards.
void driver_files_free(driver_files_t* files);

/// The files, in read order. `i` past the end is an internal error.
uint64_t driver_files_count(const driver_files_t* files);
const char* driver_files_at(const driver_files_t* files, uint64_t i);

/// What a front-end run leaves behind: the modules it read, which own every
/// syntax tree, and the checker, which owns every symbol and type the annotations
/// on those trees point to. Every `sym` and `type` slot of a tree dangles once the
/// checker is freed (sym.h), so the caller owns the analysis and releases it only
/// after the last pass that reads a tree, which is the index walk running after
/// the front end returned. Zero-initialized storage is not one:
/// driver_analysis_init prepares it, and one analysis serves one run.
/// D20.3
typedef struct {
    module_set_t set; // the import closure and the arena that owns every tree
    check_t ck;       // the symbols and types the annotations point into
} driver_analysis_t;

void driver_analysis_init(driver_analysis_t* an);

/// Releases the checker and then the modules, in that order because an
/// annotation points into the checker and a symbol's name points into a
/// module's source; the analysis is empty and no tree may be read afterwards.
void driver_analysis_free(driver_analysis_t* an);

/// The front end of toolchain.md 2, steps 1 to 4: read the entry file, parse the
/// import closure (module-system.md 10), check every module in dependency order
/// and write the program's LLVM IR module to `ir_path`. The search roots are the
/// entry file's directory, the `-I` options in order and the standard library
/// directory driver_std_dir names, which is why `argv0` is needed. The module is
/// emitted by gen.h while the checker's annotations are still alive, so a
/// construct the emitter cannot lower yet is one more compile error and never a
/// half-written module. Returns FORT_EXIT_OK, FORT_EXIT_COMPILE_ERROR, or
/// FORT_EXIT_USAGE for an unreadable entry file. Only the `fort: error:` line of
/// a usage or toolchain error goes to `err`: the compile-time diagnostics go to
/// stderr through diag.h, which main.c relies on and a test redirects with
/// diag_capture.
///
/// `ir_path` is NULL under `--check`, which emits no module; `files` collects the
/// closure's file names for the document and is NULL when the caller wants none.
/// The diagnostics the run reported stay in the sink of diag.h, where
/// diag_write_json reads them.
///
/// `an` is the caller's, prepared by driver_analysis_init and never freed here: it
/// holds the trees and the annotations the run produced, so a caller that indexes
/// them reads them after this returns and frees the analysis when it is done
/// (sym.h).
/// D9.2, D14.1, D14.2, D19.1, D20.1, D20.2, D20.3
int driver_front_end(const driver_options_t* opts,
                     const char* argv0,
                     const char* ir_path,
                     driver_files_t* files,
                     driver_analysis_t* an,
                     FILE* err);

/// Runs the compiler with argv[1..argc-1]; out and err are where --help,
/// --version, the document of `--json` and the `fort: error:` lines go
/// (stdout and stderr in main). Returns the process exit status.
int driver_main(int argc, char** argv, FILE* out, FILE* err);

#endif
