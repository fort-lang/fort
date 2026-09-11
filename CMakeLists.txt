cmake_minimum_required(VERSION 3.25)
project(fort LANGUAGES C)

# ---- toolchain --------------------------------------------------------------
# C11 without extensions and with POSIX.1-2008 visible (AGENTS.md, C sources).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(FORT_CCACHE ccache)
if(FORT_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${FORT_CCACHE}")
endif()

# The warnings every C compilation gets, native and cross.
set(FORT_C_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wvla
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wundef
)

add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_options(${FORT_C_WARNINGS})

# FORT_SANITIZER instruments every native target (the compiler, the native
# runtime object and the unit tests) but never the cross-compiled fort_rt.o.
include(cmake/sanitizers.cmake)

enable_testing()

set(FORT_SRC_DIR "${CMAKE_SOURCE_DIR}/src/bootstrap")
set(FORT_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/runtime")
set(FORT_TEST_DIR "${CMAKE_SOURCE_DIR}/test")
set(FORT_STD_SRC_DIR "${CMAKE_SOURCE_DIR}/std")
set(FORT_EDITORS_DIR "${CMAKE_SOURCE_DIR}/editors")
set(FORT_STD_DIR "${CMAKE_BINARY_DIR}/std")

# ---- the bootstrap compiler ---------------------------------------------------
file(GLOB FORT_CORE_SOURCES CONFIGURE_DEPENDS "${FORT_SRC_DIR}/*.c")
list(REMOVE_ITEM FORT_CORE_SOURCES "${FORT_SRC_DIR}/main.c")

add_library(fort_core STATIC ${FORT_CORE_SOURCES})
target_include_directories(fort_core PUBLIC "${FORT_SRC_DIR}")

# The compiler binary sits at the root of the build directory and the
# standard library directory is <build>/std. The fallback --std-dir of
# toolchain.md 1 (../std relative to the binary) does not resolve to it; the
# test harness passes --std-dir explicitly.
add_executable(fort "${FORT_SRC_DIR}/main.c")
target_link_libraries(fort PRIVATE fort_core)
set_target_properties(fort PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- the C runtime -------------------------------------------------------------
# fort_rt.o is the runtime object linked into every generated program
# (toolchain.md 2, 5). It is built for the x86-64 target by FORT_TARGET_CC,
# outside CMake's compiler settings, so it is never sanitized. FORT_TARGET_CC
# must name a clang, since the compiler drives the same program over the LLVM
# IR it emits, with --target=${FORT_TARGET_TRIPLE} (D14.1, D14.3).
set(FORT_TARGET_CC "clang" CACHE STRING
    "clang that builds the target (x86-64) runtime object fort_rt.o")
set(FORT_TARGET_TRIPLE "x86_64-linux-gnu" CACHE STRING
    "target of FORT_TARGET_CC, passed as --target=")
# NO_CACHE: the path must follow FORT_TARGET_CC. A cached result would survive
# a change of FORT_TARGET_CC in an existing build directory and keep running
# the previous program, which then receives arguments meant for the new one.
find_program(FORT_TARGET_CC_PATH NAMES "${FORT_TARGET_CC}" NO_CACHE)
if(NOT FORT_TARGET_CC_PATH)
    message(FATAL_ERROR "FORT_TARGET_CC: clang not found: ${FORT_TARGET_CC}")
endif()

set(FORT_RT_OBJECT "${FORT_STD_DIR}/fort_rt.o")
add_custom_command(
    OUTPUT "${FORT_RT_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FORT_STD_DIR}"
    COMMAND "${FORT_TARGET_CC_PATH}" "--target=${FORT_TARGET_TRIPLE}"
            -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g -fPIE ${FORT_C_WARNINGS}
            -c -o "${FORT_RT_OBJECT}" "${FORT_RUNTIME_DIR}/fort_rt.c"
    DEPENDS "${FORT_RUNTIME_DIR}/fort_rt.c" "${FORT_RUNTIME_DIR}/fort_rt.h"
    COMMENT "Cross-compiling runtime/fort_rt.c with ${FORT_TARGET_CC} for ${FORT_TARGET_TRIPLE}"
    VERBATIM
)

# The standard library sources are copied next to fort_rt.o; the compiler
# reads them from <std-dir> (toolchain.md 2).
file(GLOB FORT_STD_SOURCES CONFIGURE_DEPENDS "${FORT_STD_SRC_DIR}/*.ft")
set(FORT_STD_OUTPUTS "")
foreach(std_source IN LISTS FORT_STD_SOURCES)
    get_filename_component(std_name "${std_source}" NAME)
    set(std_output "${FORT_STD_DIR}/${std_name}")
    add_custom_command(
        OUTPUT "${std_output}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${std_source}" "${std_output}"
        DEPENDS "${std_source}"
        COMMENT "Copying std/${std_name}"
        VERBATIM
    )
    list(APPEND FORT_STD_OUTPUTS "${std_output}")
endforeach()

add_custom_target(fort_rt ALL DEPENDS "${FORT_RT_OBJECT}" ${FORT_STD_OUTPUTS})

# The same runtime compiled natively, without main, for the unit tests and
# for clang-tidy.
add_library(fort_rt_native OBJECT "${FORT_RUNTIME_DIR}/fort_rt.c")
target_compile_definitions(fort_rt_native PRIVATE FORT_RT_NO_MAIN)
target_include_directories(fort_rt_native PUBLIC "${FORT_RUNTIME_DIR}")

# ---- unit tests ----------------------------------------------------------------
# The test-only helpers that are compiled rather than included: every
# test/*.c that is not a suite, such as the syntax tree's S-expression
# printer. They are linked into every suite so that -Werror and clang-tidy
# cover them once.
file(GLOB FORT_TEST_SUPPORT_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/*.c")
list(FILTER FORT_TEST_SUPPORT_SOURCES EXCLUDE REGEX "_test\\.c$")
add_library(fort_test_support OBJECT ${FORT_TEST_SUPPORT_SOURCES})
target_include_directories(fort_test_support PUBLIC "${FORT_TEST_DIR}")
target_link_libraries(fort_test_support PUBLIC fort_core)

# One executable and one ctest, labelled unit, per test/<component>_test.c
# (toolchain.md 7.5). The ctest is named unit-<component>.
file(GLOB FORT_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/*_test.c")
set(FORT_UNIT_TEST_TARGETS "")
foreach(test_source IN LISTS FORT_UNIT_TEST_SOURCES)
    get_filename_component(test_target "${test_source}" NAME_WE)
    string(REGEX REPLACE "_test$" "" test_component "${test_target}")
    add_executable("${test_target}" "${test_source}")
    target_include_directories("${test_target}" PRIVATE "${FORT_TEST_DIR}")
    target_link_libraries("${test_target}" PRIVATE fort_core fort_rt_native fort_test_support)
    set_target_properties("${test_target}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")
    add_test(NAME "unit-${test_component}" COMMAND "${test_target}")
    set_tests_properties("unit-${test_component}" PROPERTIES
        LABELS unit
        ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")
    list(APPEND FORT_UNIT_TEST_TARGETS "${test_target}")
endforeach()

# The driver tests spawn test/fake_cc.sh as their --cc: it records the command
# line the driver built (D14.3) instead of compiling it, so they need its path;
# the check suite needs it to assert that `--check` spawns nothing (D20.1) and
# the conformance suite to assert the same of the runs that stop before it.
foreach(driver_suite driver_test driver_check_test driver_conformance_test)
    if(TARGET "${driver_suite}")
        target_compile_definitions("${driver_suite}"
            PRIVATE FORT_FAKE_CC="${FORT_TEST_DIR}/fake_cc.sh")
    endif()
endforeach()

# The emitter suites run the LLVM verifier over every module they emit (D19.1)
# and compare two of them with the worked examples of toolchain.md 6, which
# live in test/ir, so they need the verifier's path and that directory. Every
# test/gen*_test.c gets them, so a new suite is covered by adding the file, and
# `opt` is a default rather than a configure-time requirement of every preset,
# as it is for the pipeline test and for the language harness.
find_program(FORT_OPT_PATH NAMES opt-18 opt)
if(NOT FORT_OPT_PATH)
    set(FORT_OPT_PATH "opt-18")
endif()
foreach(gen_suite IN LISTS FORT_UNIT_TEST_TARGETS)
    if(gen_suite MATCHES "^gen")
        target_compile_definitions("${gen_suite}"
            PRIVATE FORT_OPT="${FORT_OPT_PATH}" FORT_IR_DIR="${FORT_TEST_DIR}/ir")
    endif()
endforeach()

# The cross pipeline: the hand-written LLVM IR modules under test/ir, which
# are in the form of toolchain.md 6 and are its worked examples (D19.1), are
# verified with opt, compiled and linked with the cross-compiled fort_rt.o and
# run under qemu (toolchain.md 2). It is a unit test so that check covers it;
# it needs the runtime object, not the compiler.
add_test(NAME pipeline
    COMMAND bash "${FORT_TEST_DIR}/pipeline_test.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(pipeline PROPERTIES
    LABELS unit
    ENVIRONMENT "FORT_TARGET_CC=${FORT_TARGET_CC_PATH};FORT_TARGET_TRIPLE=${FORT_TARGET_TRIPLE}")

# The C helpers of the language tests are cross-compiled by the harness; they
# are also built natively here so that -Werror and clang-tidy cover them. They
# are FFI entry points declared by the tests, hence no prototypes.
file(GLOB FORT_LANG_FFI_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/lang/ffi/*.c")
add_library(lang_ffi_helpers OBJECT ${FORT_LANG_FFI_SOURCES})
target_compile_options(lang_ffi_helpers PRIVATE -Wno-missing-prototypes)

# ---- check targets ---------------------------------------------------------------
add_custom_target(check
    COMMAND "${CMAKE_CTEST_COMMAND}" -L unit --output-on-failure
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running the unit tests"
    USES_TERMINAL
)
add_dependencies(check fort_rt)
if(FORT_UNIT_TEST_TARGETS)
    add_dependencies(check ${FORT_UNIT_TEST_TARGETS})
endif()

# The language tests (toolchain.md 7, D14.4, D14.5): test/lang/run_tests.py
# drives the compiler over test/lang. The ctest `lang` (label lang) and the
# check-lang target run the same command, and `lang_check_json` the cross-check
# of the check mode (D20.2); `lang_lint` validates the directives and
# `lang_selftest` runs the harness's own unit tests (both label unit).
find_program(FORT_PYTHON3 NAMES python3 REQUIRED)
set(FORT_LANG_DIR "${FORT_TEST_DIR}/lang")

# The parser's recovery suite walks the fail corpus (D14.4) and asserts that
# every diagnostic lands on an annotated line, so it needs the corpus's path.
if(TARGET parser_recovery_test)
    target_compile_definitions(parser_recovery_test PRIVATE FORT_LANG_DIR="${FORT_LANG_DIR}")
endif()
set(FORT_LANG_HARNESS "${FORT_LANG_DIR}/run_tests.py")
include(ProcessorCount)
ProcessorCount(FORT_LANG_JOBS)
if(FORT_LANG_JOBS EQUAL 0)
    set(FORT_LANG_JOBS 1)
endif()
# --verify-ir runs opt -passes=verify over the `-S` output of every test that
# compiles, so every module the emitter writes is verified from here on
# (D19.1, toolchain.md 6).
set(FORT_LANG_COMMAND
    "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}"
    --fort "$<TARGET_FILE:fort>"
    --std-dir "${FORT_STD_DIR}"
    --cc "${FORT_TARGET_CC_PATH}"
    --opt "${FORT_OPT_PATH}"
    --verify-ir
    -j "${FORT_LANG_JOBS}"
)
add_test(NAME lang COMMAND ${FORT_LANG_COMMAND})
set_tests_properties(lang PROPERTIES
    LABELS lang
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")

# The check mode over the same corpus: `fort --check --json` on every fail
# test, held against the text form of D14.2 (D20.1, D20.2).
add_test(NAME lang_check_json COMMAND ${FORT_LANG_COMMAND} --check-json)
set_tests_properties(lang_check_json PROPERTIES
    LABELS lang
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")
add_test(NAME lang_lint COMMAND "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}" --lint)
set_tests_properties(lang_lint PROPERTIES LABELS unit)
add_test(NAME lang_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest run_tests_test
    WORKING_DIRECTORY "${FORT_LANG_DIR}")
set_tests_properties(lang_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The unit tests of tools/check_comments.py (label unit); the tool itself runs
# as part of format-check below.
add_test(NAME check_comments_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest check_comments_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(check_comments_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The unit tests of tools/lines.py (label unit): the glob matcher, which the
# guest's Python 3.12 cannot delegate to pathlib, and the --since mode that
# measures a branch's own diff (AGENTS.md, Build and test).
add_test(NAME lines_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest lines_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(lines_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The unit tests of the TextMate grammar in editors/vscode (label unit): they
# compare its keyword and operator lists with D2.4 and D2.10 and assert the
# scopes it gives the fixtures in test/highlight.
add_test(NAME highlight_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest highlight_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(highlight_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The unit tests of the VS Code extension in editors/vscode (label unit), under
# Node's built-in runner: the check document of D20.2 turned into editor shapes,
# the conversion of its 1-based byte columns to character offsets, the lookup in
# the index of D20.3, and the activation script itself driven against a fake
# editor (editors/vscode/test/fake_vscode.js), which leaves only what needs a
# real one to the manual smoke test in editors/README.md.
find_program(FORT_NODE NAMES node nodejs REQUIRED)
add_test(NAME extension_selftest
    COMMAND "${FORT_NODE}" --test
    WORKING_DIRECTORY "${FORT_EDITORS_DIR}/vscode")
set_tests_properties(extension_selftest PROPERTIES LABELS unit)

add_custom_target(check-lang
    COMMAND ${FORT_LANG_COMMAND}
    COMMAND ${FORT_LANG_COMMAND} --check-json
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running the language tests"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(check-lang fort fort_rt)

add_custom_target(check-all COMMENT "Running every test")
add_dependencies(check-all check check-lang)

# ---- format, tidy, lines -----------------------------------------------------------
file(GLOB_RECURSE FORT_LINT_SOURCES CONFIGURE_DEPENDS
    "${FORT_SRC_DIR}/*.c" "${FORT_SRC_DIR}/*.h"
    "${FORT_RUNTIME_DIR}/*.c" "${FORT_RUNTIME_DIR}/*.h"
    "${FORT_TEST_DIR}/*.c" "${FORT_TEST_DIR}/*.h"
)

# Block comments are not a comment form (D2.2), in fort or in the project's C
# sources; format-check depends on the check so a `/* */` fails the gate.
add_custom_target(check-comments
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/check_comments.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking that no C source uses a block comment"
    VERBATIM
)

find_program(FORT_CLANG_FORMAT NAMES clang-format clang-format-18)
if(FORT_CLANG_FORMAT)
    add_custom_target(format
        COMMAND "${FORT_CLANG_FORMAT}" -i ${FORT_LINT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Formatting the C sources"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${FORT_CLANG_FORMAT}" --dry-run --Werror ${FORT_LINT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking the formatting of the C sources"
        VERBATIM
    )
else()
    add_custom_target(format COMMAND false COMMENT "format: clang-format not found")
    add_custom_target(format-check COMMAND false COMMENT "format-check: clang-format not found")
endif()
add_dependencies(format-check check-comments)

find_program(FORT_RUN_CLANG_TIDY NAMES run-clang-tidy run-clang-tidy-18)
if(FORT_RUN_CLANG_TIDY)
    add_custom_target(tidy
        COMMAND "${FORT_RUN_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}" -quiet
                "${FORT_SRC_DIR}" "${FORT_RUNTIME_DIR}" "${FORT_TEST_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy over src, runtime and test"
        VERBATIM
    )
else()
    add_custom_target(tidy COMMAND false COMMENT "tidy: run-clang-tidy not found")
endif()

add_custom_target(lines
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/lines.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Counting test lines per compiler line"
    VERBATIM
)
