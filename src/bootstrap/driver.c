/* The compiler driver scaffold (toolchain.md 1, D14.1); see driver.h. */
#include "driver.h"

#include <stdbool.h>
#include <string.h>

static const char USAGE_LINE[] = "usage: fort [options] entry.ft";

/* Options that take the following argument (toolchain.md 1). */
static const char* const OPTIONS_WITH_ARGUMENT[] = {"-o", "-I", "--std-dir", "--cc"};

/* Options that stand alone. -l<lib> is one argument and is checked apart. */
static const char* const OPTIONS_ALONE[] = {"-S", "-c", "--release", "--no-bounds-check"};

static bool in_list(const char* arg, const char* const* list, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(arg, list[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool takes_argument(const char* arg) {
    return in_list(
        arg, OPTIONS_WITH_ARGUMENT, sizeof OPTIONS_WITH_ARGUMENT / sizeof OPTIONS_WITH_ARGUMENT[0]);
}

static bool stands_alone(const char* arg) {
    return in_list(arg, OPTIONS_ALONE, sizeof OPTIONS_ALONE / sizeof OPTIONS_ALONE[0]) ||
           (arg[0] == '-' && arg[1] == 'l' && arg[2] != '\0');
}

static int usage_error(FILE* err, const char* message, const char* detail) {
    (void)fprintf(err, "fort: error: %s '%s'\n%s\n", message, detail, USAGE_LINE);
    return FORT_EXIT_USAGE;
}

const char* driver_usage_line(void) {
    return USAGE_LINE;
}

int driver_main(int argc, char** argv, FILE* out, FILE* err) {
    if (argc <= 1) {
        (void)fprintf(err, "%s\n", USAGE_LINE);
        return FORT_EXIT_USAGE;
    }
    const char* entry = NULL;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (arg[0] != '-') {
            /* The first argument that does not start with '-' is the entry
             * file (D14.1); a second one is a usage error. */
            if (entry != NULL) {
                return usage_error(err, "unexpected argument", arg);
            }
            entry = arg;
        } else if (strcmp(arg, "--help") == 0) {
            (void)fprintf(out, "%s\n", USAGE_LINE);
            return FORT_EXIT_OK;
        } else if (strcmp(arg, "--version") == 0) {
            (void)fprintf(out, "fort %s\n", FORT_VERSION_STRING);
            return FORT_EXIT_OK;
        } else if (takes_argument(arg)) {
            if (i + 1 >= argc) {
                return usage_error(err, "missing argument for option", arg);
            }
            i++;
        } else if (!stands_alone(arg)) {
            return usage_error(err, "unknown option", arg);
        }
    }
    if (entry == NULL) {
        (void)fprintf(err, "fort: error: no entry file\n%s\n", USAGE_LINE);
        return FORT_EXIT_USAGE;
    }
    (void)fprintf(err, "fort: error: not implemented\n");
    return FORT_EXIT_USAGE;
}
