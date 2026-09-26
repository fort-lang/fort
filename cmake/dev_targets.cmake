# The tools and the helper functions of the development targets.
#
# The build has two components: `fort` (the compiler, the standard library, the
# language server and the editor extension) and `bootstrap0` (the C bootstrap
# compiler, bootstrap0/CMakeLists.txt). Each has the same steps:
#
#   format          clang-format rewrites the component's C sources
#   format-check    clang-format checks them
#   lint            clang-tidy for bootstrap0; tools/fort_lint.py for fort
#   unit            the component's ctests with the label `unit`
#   integration     the component's ctests with the label `integration`
#   check           format-check, lint, unit and integration, in that order
#
# The fort targets carry the step's name (`check`), the bootstrap0 targets the
# prefix `bootstrap0-` (`bootstrap0-check`), and `<step>-all` runs the step for
# both components (`check-all`). The top level includes this file before it adds
# bootstrap0/, so that the subdirectory sees the tools and the functions.

# The lint configuration is clang 18's (.clang-format, .clang-tidy). The
# versioned name wins where the host has one; the darwin host keeps llvm@18
# keg-only under /opt/homebrew/opt/llvm@18, where the names are unversioned.
# NO_CACHE, so that a tool installed later is found on the next configure.
set(FORT_CLANG_18_HINTS /opt/homebrew/opt/llvm@18/bin)
find_program(FORT_CLANG_FORMAT NAMES clang-format-18 clang-format
    HINTS ${FORT_CLANG_18_HINTS} NO_CACHE)
find_program(FORT_RUN_CLANG_TIDY NAMES run-clang-tidy-18 run-clang-tidy
    HINTS ${FORT_CLANG_18_HINTS} NO_CACHE)
# run-clang-tidy runs the `clang-tidy` of the PATH unless told otherwise, which
# on the darwin host is a newer one; name the clang-tidy 18 beside it.
find_program(FORT_CLANG_TIDY NAMES clang-tidy-18 clang-tidy
    HINTS ${FORT_CLANG_18_HINTS} NO_CACHE)
# clang-tidy does not find the SDK headers on its own as the darwin clang
# does: the compile commands carry no -isysroot, so the lint target adds it.
set(FORT_TIDY_EXTRA_ARGS "")
if(APPLE)
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE FORT_DARWIN_SDK OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(FORT_TIDY_EXTRA_ARGS -extra-arg=-isysroot "-extra-arg=${FORT_DARWIN_SDK}")
endif()

# fort_format_targets(<prefix> <sources>...): <prefix>format and
# <prefix>format-check. A missing clang-format is a failing target.
function(fort_format_targets prefix)
    if(FORT_CLANG_FORMAT)
        add_custom_target(${prefix}format
            COMMAND "${FORT_CLANG_FORMAT}" -i ${ARGN}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Formatting the ${prefix}C sources"
            VERBATIM)
        add_custom_target(${prefix}format-check
            COMMAND "${FORT_CLANG_FORMAT}" --dry-run --Werror ${ARGN}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Checking the formatting of the ${prefix}C sources"
            VERBATIM)
    else()
        foreach(name format format-check)
            add_custom_target(${prefix}${name} COMMAND false
                COMMENT "${prefix}${name}: clang-format not found")
        endforeach()
    endif()
endfunction()

# fort_tidy_target(<name> <path>): run-clang-tidy over the sources under <path>
# in the compile database of the build. A missing run-clang-tidy is a failing target.
function(fort_tidy_target name path)
    if(FORT_RUN_CLANG_TIDY)
        add_custom_target(${name}
            COMMAND "${FORT_RUN_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}" -quiet
                    -clang-tidy-binary "${FORT_CLANG_TIDY}" ${FORT_TIDY_EXTRA_ARGS} "${path}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Running clang-tidy over ${path}"
            VERBATIM)
    else()
        add_custom_target(${name} COMMAND false COMMENT "${name}: run-clang-tidy not found")
    endif()
endfunction()

# fort_ctest_target(<name> <test-dir> <ctest arguments>...): ctest over the tests
# registered under <test-dir>, filtered by the arguments.
function(fort_ctest_target name dir)
    add_custom_target(${name}
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${dir}" ${ARGN} --output-on-failure
        COMMENT "Running the ${name} tests"
        USES_TERMINAL
        VERBATIM)
endfunction()

# fort_sequence_target(<name> <target>...): build each target in order through
# `cmake --build`, so that the first failure stops the rest.
function(fort_sequence_target name)
    set(commands "")
    foreach(part IN LISTS ARGN)
        list(APPEND commands
            COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ${part})
    endforeach()
    add_custom_target(${name} ${commands} USES_TERMINAL VERBATIM)
endfunction()
