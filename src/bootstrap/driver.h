/* The compiler driver: command line and exit statuses (toolchain.md 1, D14.1).
 *
 * This is the scaffold of ticket T-003: it recognizes every option of
 * toolchain.md section 1, handles --help and --version, reports usage errors
 * with exit status 2, and exits 2 with "not implemented" for every actual
 * compilation. Ticket T-013 turns it into the real driver. */
#ifndef FORT_DRIVER_H
#define FORT_DRIVER_H

#include <stdio.h>

#include "str.h"

/* The version printed by --version. */
#define FORT_VERSION_STRING "0.1.0"

/* Exit statuses of D14.1; usage, internal and C compiler failures share
 * FATAL_EXIT_STATUS of str.h. */
enum {
    FORT_EXIT_OK = 0,
    FORT_EXIT_COMPILE_ERROR = 1,
    FORT_EXIT_USAGE = FATAL_EXIT_STATUS,
};

/* The usage line that `fort` with no arguments and --help print. */
const char* driver_usage_line(void);

/* Runs the compiler with argv[1..argc-1]; out and err are where --help,
 * --version and the diagnostics go (stdout and stderr in main). Returns the
 * process exit status. */
int driver_main(int argc, char** argv, FILE* out, FILE* err);

#endif
