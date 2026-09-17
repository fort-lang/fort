cmake_minimum_required(VERSION 3.25)
project(fort LANGUAGES C)

# ---- toolchain --------------------------------------------------------------
# C11 without extensions and with POSIX.1-2008 visible (notes/style.md 2).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(FORT_CCACHE ccache)
if(FORT_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${FORT_CCACHE}")
endif()

# The warnings every host C compilation gets.
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

# FORT_SANITIZER instruments each host C target: the compiler and the unit
# tests. The programs the compiler builds are cross-compiled and are never
# instrumented.
include(cmake/sanitizers.cmake)

enable_testing()

find_program(FORT_PYTHON3 NAMES python3 REQUIRED)

# The seed contract has two target names. This test breaks each input and
# output rule with local executable fixtures. It does not need a compiler.
add_test(NAME verify_seed_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest verify_seed_test
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test")
set_tests_properties(verify_seed_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

set(FORT_SRC_DIR "${CMAKE_SOURCE_DIR}/src/bootstrap")
set(FORT_TEST_DIR "${CMAKE_SOURCE_DIR}/test")
set(FORT_STD_SRC_DIR "${CMAKE_SOURCE_DIR}/std")
set(FORT_EDITORS_DIR "${CMAKE_SOURCE_DIR}/editors")
set(FORT_STD_DIR "${CMAKE_BINARY_DIR}/std")
set(FORT_FORT_SRC_DIR "${CMAKE_SOURCE_DIR}/src/fort")
set(FORT_STAGE2_DIR "${CMAKE_BINARY_DIR}/stage2")
set(FORT_BOOTSTRAP_SEED "$ENV{FORT_BOOTSTRAP_SEED}" CACHE FILEPATH
    "User-supplied fort seed from tools/bootstrap.seed")
set(FORT_TARGET "linux" CACHE STRING "Production bootstrap target: linux or darwin")
set_property(CACHE FORT_TARGET PROPERTY STRINGS linux darwin)

if(FORT_TARGET STREQUAL "linux")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "the linux target requires a linux host")
    endif()
    set(FORT_TARGET_TRIPLE "x86_64-linux-gnu")
    set(FORT_DEFAULT_TARGET_CC "clang")
    set(FORT_DEFAULT_OPT "opt-18")
elseif(FORT_TARGET STREQUAL "darwin")
    if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "the darwin target requires a darwin arm64 host")
    endif()
    # The deployment target is the first macOS for Apple Silicon. std/darwin/os.ft
    # names the same triple.
    set(FORT_TARGET_TRIPLE "arm64-apple-macosx11.0.0")
    set(FORT_DEFAULT_TARGET_CC "clang")
    set(FORT_DEFAULT_OPT "/opt/homebrew/bin/opt")
else()
    message(FATAL_ERROR "FORT_TARGET must be linux or darwin")
endif()

set(FORT_TARGET_CC "${FORT_DEFAULT_TARGET_CC}" CACHE STRING
    "clang that compiles and links the selected target")
set(FORT_OPT "${FORT_DEFAULT_OPT}" CACHE FILEPATH
    "LLVM opt that verifies the selected target")
find_program(FORT_TARGET_CC_PATH NAMES "${FORT_TARGET_CC}" NO_CACHE REQUIRED)
find_program(FORT_OPT_PATH NAMES "${FORT_OPT}" NO_CACHE)
if(NOT FORT_OPT_PATH)
    message(FATAL_ERROR "FORT_OPT: verifier not found: ${FORT_OPT}")
endif()

# ---- the bootstrap compiler ---------------------------------------------------
file(GLOB FORT_CORE_SOURCES CONFIGURE_DEPENDS "${FORT_SRC_DIR}/*.c")
list(REMOVE_ITEM FORT_CORE_SOURCES "${FORT_SRC_DIR}/main.c")

add_library(fort_core STATIC ${FORT_CORE_SOURCES})
target_include_directories(fort_core PUBLIC "${FORT_SRC_DIR}")
target_compile_definitions(fort_core PUBLIC
    FORT_DEFAULT_CC="${FORT_DEFAULT_TARGET_CC}"
    FORT_DEFAULT_TARGET="${FORT_TARGET_TRIPLE}"
)
if(APPLE)
    # Darwin hides mkdtemp under its source feature macro in C11 mode.
    target_compile_definitions(fort_core PUBLIC _DARWIN_C_SOURCE)
endif()

# The compiler binary sits at the root of the build directory and the
# standard library directory is <build>/std. The fallback --std-dir of
# toolchain.md 1 (../std relative to the binary) does not resolve to it; the
# test harness passes --std-dir explicitly.
add_executable(fort "${FORT_SRC_DIR}/main.c")
target_link_libraries(fort PRIVATE fort_core)
set_target_properties(fort PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- the standard library ------------------------------------------------------
# The target root is flat: the modules of std/ and those of std/<target>/.
file(GLOB FORT_STD_SOURCES CONFIGURE_DEPENDS
    "${FORT_STD_SRC_DIR}/*.ft" "${FORT_STD_SRC_DIR}/${FORT_TARGET}/*.ft")
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

add_custom_target(fort_std ALL DEPENDS ${FORT_STD_OUTPUTS})

# ---- the chain of source compilers ---------------------------------------------
# The verified user seed builds pin 0. Each pin builds the next pin, and the
# last pin builds HEAD. The selected target stays equal through this graph.
# The C compiler is a linux differential oracle. It is not a chain input.
#
# tools/pin.sh verify runs here, at configure time, because a pin that names
# no commit is a broken tree and not a build that failed: it must be reported
# before anything is compiled, with the name of the pin that is wrong.
set(FORT_BOOTSTRAP_REF "${CMAKE_SOURCE_DIR}/tools/bootstrap.ref")
set(FORT_PIN_SH "${CMAKE_SOURCE_DIR}/tools/pin.sh")
set(FORT_PIN_DIR "${CMAKE_BINARY_DIR}/pin")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FORT_BOOTSTRAP_REF}")
execute_process(
    COMMAND bash "${FORT_PIN_SH}" verify --ref "${FORT_BOOTSTRAP_REF}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE FORT_PIN_VERIFY_STATUS
    OUTPUT_VARIABLE FORT_PIN_VERIFY_OUT
    ERROR_VARIABLE FORT_PIN_VERIFY_ERR
)
if(NOT FORT_PIN_VERIFY_STATUS EQUAL 0)
    message(FATAL_ERROR "tools/bootstrap.ref is not a chain this repository can build:\n"
                        "${FORT_PIN_VERIFY_ERR}")
endif()

file(STRINGS "${FORT_BOOTSTRAP_REF}" FORT_PIN_LINES REGEX "^bootstrap-")
if(NOT FORT_PIN_LINES)
    message(FATAL_ERROR "tools/bootstrap.ref names no pin")
endif()

# The C bootstrap uses this frozen root. It does not read HEAD's runtime.
set(FORT_ORACLE_REF "${CMAKE_SOURCE_DIR}/tools/bootstrap-oracle.ref")
file(STRINGS "${FORT_ORACLE_REF}" FORT_ORACLE_LINES REGEX "^bootstrap-0 ")
list(LENGTH FORT_ORACLE_LINES FORT_ORACLE_LINE_COUNT)
if(NOT FORT_ORACLE_LINE_COUNT EQUAL 1 OR
   NOT FORT_ORACLE_LINES MATCHES "^bootstrap-0[ \t]+([0-9a-f]+)$")
    message(FATAL_ERROR "tools/bootstrap-oracle.ref must name one bootstrap-0 SHA")
endif()
set(FORT_ORACLE_SHA "${CMAKE_MATCH_1}")
set(FORT_ORACLE_DIR "${CMAKE_BINARY_DIR}/oracle")
set(FORT_ORACLE_STAMP "${FORT_ORACLE_DIR}/.tree-${FORT_ORACLE_SHA}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FORT_ORACLE_REF}")
execute_process(
    COMMAND bash "${FORT_PIN_SH}" verify --ref "${FORT_ORACLE_REF}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE FORT_ORACLE_VERIFY_STATUS
    ERROR_VARIABLE FORT_ORACLE_VERIFY_ERR
)
if(NOT FORT_ORACLE_VERIFY_STATUS EQUAL 0)
    message(FATAL_ERROR "tools/bootstrap-oracle.ref is not usable:\n"
                        "${FORT_ORACLE_VERIFY_ERR}")
endif()
add_custom_command(
    OUTPUT "${FORT_ORACLE_STAMP}"
    COMMAND bash "${FORT_PIN_SH}" extract "${FORT_ORACLE_SHA}" "${FORT_ORACLE_DIR}"
            --ref "${FORT_ORACLE_REF}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${FORT_ORACLE_REF}" "${FORT_PIN_SH}"
    COMMENT "Extracting the C differential-oracle standard root"
    VERBATIM
)
add_custom_target(fort_oracle_tree ALL DEPENDS "${FORT_ORACLE_STAMP}")
set(FORT_ORACLE_STD_DIR "${FORT_ORACLE_DIR}/std")

add_test(NAME c-bootstrap
    COMMAND bash "${FORT_TEST_DIR}/c_bootstrap_test.sh" "${FORT_TARGET}"
            "$<TARGET_FILE:fort>" "${CMAKE_BINARY_DIR}/c-bootstrap"
            "${FORT_ORACLE_DIR}" "${FORT_TARGET_CC_PATH}" "${FORT_OPT_PATH}")
set_tests_properties(c-bootstrap PROPERTIES LABELS lang)

foreach(pin_line IN LISTS FORT_PIN_LINES)
    if(NOT pin_line MATCHES "^bootstrap-([0-9]+)[ \t]+([0-9a-f]+)$")
        message(FATAL_ERROR "tools/bootstrap.ref: malformed line: ${pin_line}")
    endif()
    set(pin_number "${CMAKE_MATCH_1}")
    set(FORT_LAST_PIN_NUMBER "${pin_number}")
endforeach()

# The source-chain script writes these paths. The fixed-point test starts with
# the last source pin.
set(FORT_BOOTSTRAP_BINARY "${FORT_PIN_DIR}/${FORT_LAST_PIN_NUMBER}/fort")

# ---- the self-hosted compiler --------------------------------------------------
# The one source-chain command verifies and snapshots the seed. It builds each
# source pin and HEAD. It also records each compiler's triple and file format.
file(GLOB FORT_FORT_SOURCES CONFIGURE_DEPENDS "${FORT_FORT_SRC_DIR}/*.ft")
set(FORT_STAGE2_BINARY "${FORT_STAGE2_DIR}/fort")
set(FORT_SEED_DEPENDENCY "")
if(FORT_BOOTSTRAP_SEED)
    set(FORT_SEED_DEPENDENCY "${FORT_BOOTSTRAP_SEED}")
endif()
add_custom_command(
    OUTPUT "${FORT_STAGE2_BINARY}"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/seed.identity"
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/bootstrap_chain.sh" "${FORT_TARGET}"
            "${FORT_BOOTSTRAP_SEED}" "${CMAKE_BINARY_DIR}"
            --cc "${FORT_TARGET_CC_PATH}" --opt "${FORT_OPT_PATH}"
    DEPENDS fort_std ${FORT_FORT_SOURCES} ${FORT_STD_SOURCES}
            ${FORT_SEED_DEPENDENCY}
            "${CMAKE_SOURCE_DIR}/tools/bootstrap_chain.sh"
            "${CMAKE_SOURCE_DIR}/tools/bootstrap.seed"
            "${CMAKE_SOURCE_DIR}/tools/bootstrap.ref"
            "${CMAKE_SOURCE_DIR}/tools/pin.sh"
            "${CMAKE_SOURCE_DIR}/tools/verify_seed.py"
    COMMENT "Building ${FORT_TARGET} stage2 through the source chain"
    VERBATIM
)
add_custom_target(fort_stage2 ALL DEPENDS "${FORT_STAGE2_BINARY}")

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
                "${FORT_SRC_DIR}" "${FORT_TEST_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy over src and test"
        VERBATIM
    )
else()
    add_custom_target(tidy COMMAND false COMMENT "tidy: run-clang-tidy not found")
endif()

# The same check as the ctest fort_lint, for running by hand over a file.
add_custom_target(fort-lint
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/fort_lint.py"
            --fort "${FORT_STAGE2_BINARY}" --std-dir "${FORT_STD_DIR}"
    COMMAND bash "${FORT_TEST_DIR}/darwin_fort_lint_test.sh" "${FORT_STAGE2_BINARY}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking the identifier conventions of D1.4 in the project's fort"
    VERBATIM
)
add_dependencies(fort-lint fort fort_std fort_stage2)

add_custom_target(lines
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/lines.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Counting test lines per compiler line"
    VERBATIM
)
