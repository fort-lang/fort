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
# outside CMake's compiler settings, so it is never sanitized.
set(FORT_TARGET_CC "x86_64-linux-gnu-gcc" CACHE STRING
    "C compiler that builds the target (x86-64) runtime object fort_rt.o")
find_program(FORT_TARGET_CC_PATH NAMES "${FORT_TARGET_CC}")
if(NOT FORT_TARGET_CC_PATH)
    message(FATAL_ERROR "FORT_TARGET_CC: compiler not found: ${FORT_TARGET_CC}")
endif()

set(FORT_RT_OBJECT "${FORT_STD_DIR}/fort_rt.o")
add_custom_command(
    OUTPUT "${FORT_RT_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FORT_STD_DIR}"
    COMMAND "${FORT_TARGET_CC_PATH}"
            -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g -fPIE ${FORT_C_WARNINGS}
            -c -o "${FORT_RT_OBJECT}" "${FORT_RUNTIME_DIR}/fort_rt.c"
    DEPENDS "${FORT_RUNTIME_DIR}/fort_rt.c" "${FORT_RUNTIME_DIR}/fort_rt.h"
    COMMENT "Cross-compiling runtime/fort_rt.c with ${FORT_TARGET_CC}"
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
# One executable and one ctest, labelled unit, per test/<component>_test.c
# (toolchain.md 7.5). The ctest is named unit-<component>.
file(GLOB FORT_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/*_test.c")
set(FORT_UNIT_TEST_TARGETS "")
foreach(test_source IN LISTS FORT_UNIT_TEST_SOURCES)
    get_filename_component(test_target "${test_source}" NAME_WE)
    string(REGEX REPLACE "_test$" "" test_component "${test_target}")
    add_executable("${test_target}" "${test_source}")
    target_include_directories("${test_target}" PRIVATE "${FORT_TEST_DIR}")
    target_link_libraries("${test_target}" PRIVATE fort_core fort_rt_native)
    set_target_properties("${test_target}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")
    add_test(NAME "unit-${test_component}" COMMAND "${test_target}")
    set_tests_properties("unit-${test_component}" PROPERTIES
        LABELS unit
        ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")
    list(APPEND FORT_UNIT_TEST_TARGETS "${test_target}")
endforeach()

# The cross pipeline: hand-written x86-64 programs under test/asm that follow
# the codegen contract (toolchain.md 6) are assembled and linked with the
# cross-compiled fort_rt.o and run under qemu (toolchain.md 2). It is a unit
# test so that check covers it; it needs the runtime object, not the compiler.
add_test(NAME pipeline
    COMMAND bash "${FORT_TEST_DIR}/pipeline_test.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(pipeline PROPERTIES
    LABELS unit
    ENVIRONMENT "FORT_TARGET_CC=${FORT_TARGET_CC_PATH}")

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

# The language-test harness arrives with ticket T-005; until then check-lang
# only builds what the harness will need.
add_custom_target(check-lang
    COMMAND "${CMAKE_COMMAND}" -E echo "check-lang: no harness yet"
    COMMENT "Running the language tests"
    USES_TERMINAL
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

find_program(FORT_PYTHON3 NAMES python3 REQUIRED)
add_custom_target(lines
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/lines.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Counting test lines per compiler line"
    VERBATIM
)
