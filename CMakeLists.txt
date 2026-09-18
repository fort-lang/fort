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

set(FORT_SRC_DIR "${CMAKE_SOURCE_DIR}/src/bootstrap")
set(FORT_TEST_DIR "${CMAKE_SOURCE_DIR}/test")
set(FORT_STD_SRC_DIR "${CMAKE_SOURCE_DIR}/std")
set(FORT_EDITORS_DIR "${CMAKE_SOURCE_DIR}/editors")
set(FORT_STD_DIR "${CMAKE_BINARY_DIR}/std")
set(FORT_FORT_SRC_DIR "${CMAKE_SOURCE_DIR}/src/fort")
set(FORT_BOOTSTRAP_DIR "${CMAKE_BINARY_DIR}/bootstrap")
option(FORT_ENABLE_BOOTSTRAP "Build fort through the C-started source chain" ON)
set(FORT_STAGE1_COMPILER "" CACHE FILEPATH
    "External fort compiler that builds working-tree HEAD")
set(FORT_BOOTSTRAP_REF "${CMAKE_SOURCE_DIR}/tools/bootstrap.ref" CACHE FILEPATH
    "Ordered source revisions for the C-started bootstrap")

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "fort bootstrap requires a native build")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(FORT_TARGET "linux")
    set(FORT_TARGET_TRIPLE "x86_64-linux-gnu")
    set(FORT_DEFAULT_TARGET_CC "clang")
    set(FORT_DEFAULT_OPT "opt-18")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(FORT_TARGET "darwin")
    # The deployment target is the first macOS for Apple Silicon. std/darwin/os.ft
    # names the same triple.
    set(FORT_TARGET_TRIPLE "arm64-apple-macosx11.0.0")
    set(FORT_DEFAULT_TARGET_CC "clang")
    set(FORT_DEFAULT_OPT "/opt/homebrew/bin/opt")
else()
    message(FATAL_ERROR
        "fort bootstrap supports only native Linux and Darwin hosts")
endif()

# Host tools use the target that CMake selected. This file is build data, not
# an input to the chain.
file(MAKE_DIRECTORY "${FORT_BOOTSTRAP_DIR}")
file(GENERATE OUTPUT "${FORT_BOOTSTRAP_DIR}/target"
    CONTENT "${FORT_TARGET_TRIPLE}\n")

if(NOT FORT_ENABLE_BOOTSTRAP AND NOT FORT_STAGE1_COMPILER)
    message(FATAL_ERROR
        "FORT_ENABLE_BOOTSTRAP=OFF requires FORT_STAGE1_COMPILER")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FORT_STAGE_MODE_ARGS "")
elseif(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(FORT_STAGE_MODE_ARGS --release)
else()
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE must be Debug, Release, RelWithDebInfo, or MinSizeRel")
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
add_executable(fort_stage1 "${FORT_SRC_DIR}/main.c")
target_link_libraries(fort_stage1 PRIVATE fort_core)
set_target_properties(fort_stage1 PROPERTIES
    OUTPUT_NAME fort
    RUNTIME_OUTPUT_DIRECTORY "${FORT_BOOTSTRAP_DIR}/stage1")

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
# CMake reads, validates, and orders this list. A shell script does not select
# a predecessor. An external stage1 skips the complete list.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FORT_BOOTSTRAP_REF}")
set(FORT_PIN_NUMBERS "")
set(FORT_PIN_SHAS "")
if(NOT FORT_STAGE1_COMPILER)
    if(NOT EXISTS "${FORT_BOOTSTRAP_REF}")
        message(FATAL_ERROR "bootstrap reference is missing: ${FORT_BOOTSTRAP_REF}")
    endif()
    file(STRINGS "${FORT_BOOTSTRAP_REF}" FORT_REF_LINES)
    set(FORT_EXPECTED_PIN 0)
    set(FORT_PREVIOUS_PIN "")
    foreach(ref_line IN LISTS FORT_REF_LINES)
        string(STRIP "${ref_line}" ref_line)
        if(ref_line STREQUAL "" OR ref_line MATCHES "^#")
            continue()
        endif()
        if(NOT ref_line MATCHES "^bootstrap-([0-9]+)[ \t]+([^ \t]+)$")
            message(FATAL_ERROR
                "${FORT_BOOTSTRAP_REF}: malformed line: ${ref_line}")
        endif()
        set(pin_number "${CMAKE_MATCH_1}")
        set(pin_sha "${CMAKE_MATCH_2}")
        if(NOT pin_number STREQUAL "${FORT_EXPECTED_PIN}")
            message(FATAL_ERROR
                "${FORT_BOOTSTRAP_REF}: bootstrap-${FORT_EXPECTED_PIN} is expected")
        endif()
        string(LENGTH "${pin_sha}" pin_sha_length)
        if(NOT pin_sha_length EQUAL 40 OR NOT pin_sha MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR
                "${FORT_BOOTSTRAP_REF}: bootstrap-${pin_number} needs one lowercase full SHA")
        endif()
        execute_process(
            COMMAND git cat-file -t "${pin_sha}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE pin_commit_status
            OUTPUT_VARIABLE pin_object_type
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT pin_commit_status EQUAL 0 OR NOT pin_object_type STREQUAL "commit")
            message(FATAL_ERROR
                "bootstrap-${pin_number}: ${pin_sha} is no commit object")
        endif()
        execute_process(
            COMMAND git merge-base --is-ancestor "${pin_sha}" HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE pin_ancestor_status
        )
        if(NOT pin_ancestor_status EQUAL 0)
            message(FATAL_ERROR
                "bootstrap-${pin_number}: ${pin_sha} is not an ancestor of HEAD")
        endif()
        if(FORT_PREVIOUS_PIN)
            execute_process(
                COMMAND git merge-base --is-ancestor "${FORT_PREVIOUS_PIN}" "${pin_sha}"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE pin_order_status
            )
            if(NOT pin_order_status EQUAL 0)
                message(FATAL_ERROR
                    "bootstrap-${pin_number}: ${pin_sha} does not follow its predecessor")
            endif()
        endif()
        execute_process(
            COMMAND git ls-tree -r --name-only "${pin_sha}" -- src/fort std tools/assemble_std.sh
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE pin_tree_status
            OUTPUT_VARIABLE pin_tree
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT pin_tree_status EQUAL 0 OR
           NOT "\n${pin_tree}\n" MATCHES "\nsrc/fort/main\\.ft\n" OR
           NOT "\n${pin_tree}\n" MATCHES "\nstd/rt\\.ft\n")
            message(FATAL_ERROR
                "bootstrap-${pin_number}: revision lacks required src/fort or std sources")
        endif()
        set(pin_has_darwin_pair FALSE)
        if("\n${pin_tree}\n" MATCHES "\nstd/darwin/libc\\.ft\n" AND
           "\n${pin_tree}\n" MATCHES "\nstd/darwin/net\\.ft\n")
            set(pin_has_darwin_pair TRUE)
        endif()
        if(NOT pin_has_darwin_pair)
            message(FATAL_ERROR
                "bootstrap-${pin_number}: revision cannot assemble Darwin sources")
        endif()
        if(NOT "\n${pin_tree}\n" MATCHES "\nstd/linux/libc\\.ft\n" OR
           NOT "\n${pin_tree}\n" MATCHES "\nstd/linux/net\\.ft\n" OR
           NOT "\n${pin_tree}\n" MATCHES "\ntools/assemble_std\\.sh\n")
            message(FATAL_ERROR
                "bootstrap-${pin_number}: revision cannot assemble Linux sources")
        endif()
        list(APPEND FORT_PIN_NUMBERS "${pin_number}")
        list(APPEND FORT_PIN_SHAS "${pin_sha}")
        set(FORT_LAST_PIN_NUMBER "${pin_number}")
        set(FORT_PREVIOUS_PIN "${pin_sha}")
        math(EXPR FORT_EXPECTED_PIN "${FORT_EXPECTED_PIN} + 1")
    endforeach()
    if(NOT FORT_PIN_SHAS)
        message(FATAL_ERROR "${FORT_BOOTSTRAP_REF}: bootstrap-0 is missing")
    endif()
endif()

# The C differential compiler uses this frozen root.
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
    COMMAND git cat-file -t "${FORT_ORACLE_SHA}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE FORT_ORACLE_VERIFY_STATUS
    OUTPUT_VARIABLE FORT_ORACLE_OBJECT_TYPE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FORT_ORACLE_VERIFY_STATUS EQUAL 0 OR
   NOT FORT_ORACLE_OBJECT_TYPE STREQUAL "commit")
    message(FATAL_ERROR "tools/bootstrap-oracle.ref names no usable commit object")
endif()
add_custom_command(
    OUTPUT "${FORT_ORACLE_STAMP}"
    COMMAND "${CMAKE_COMMAND}"
            -DROOT=${CMAKE_SOURCE_DIR}
            -DSHA=${FORT_ORACLE_SHA}
            -DOUTPUT=${FORT_ORACLE_DIR}
            -DTARGET=${FORT_TARGET}
            -P "${CMAKE_SOURCE_DIR}/cmake/extract_tree.cmake"
    DEPENDS "${FORT_ORACLE_REF}" "${CMAKE_SOURCE_DIR}/cmake/extract_tree.cmake"
    COMMENT "Extracting the C differential-oracle standard root"
    VERBATIM
)
add_custom_target(fort_oracle_tree ALL DEPENDS "${FORT_ORACLE_STAMP}")
set(FORT_ORACLE_STD_DIR "${FORT_ORACLE_DIR}/std")

add_test(NAME c-bootstrap
    COMMAND bash "${FORT_TEST_DIR}/c_bootstrap_test.sh" "${FORT_TARGET}"
            "$<TARGET_FILE:fort_stage1>" "${CMAKE_BINARY_DIR}/c-bootstrap"
            "${FORT_ORACLE_DIR}" "${FORT_TARGET_CC_PATH}" "${FORT_OPT_PATH}")
set_tests_properties(c-bootstrap PROPERTIES LABELS lang)

if(NOT FORT_STAGE1_COMPILER)
    add_test(NAME c-bootstrap-bridge
        COMMAND bash "${FORT_TEST_DIR}/c_bootstrap_bridge_test.sh" "${FORT_TARGET}"
                "$<TARGET_FILE:fort_stage1>"
                "${FORT_BOOTSTRAP_DIR}/bootstrap-0/fort"
                "${FORT_BOOTSTRAP_DIR}/bootstrap-0/target/std"
                "${FORT_TARGET_CC_PATH}" "${FORT_OPT_PATH}")
    set_tests_properties(c-bootstrap-bridge PROPERTIES LABELS lang)
endif()

# ---- the self-hosted compiler --------------------------------------------------
file(GLOB FORT_FORT_SOURCES CONFIGURE_DEPENDS "${FORT_FORT_SRC_DIR}/*.ft")
set(FORT_TREE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/bootstrap_tree.cmake")

if(FORT_STAGE1_COMPILER)
    get_filename_component(FORT_STAGE1_COMPILER
        "${FORT_STAGE1_COMPILER}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${FORT_STAGE1_COMPILER}")
        message(FATAL_ERROR
            "FORT_STAGE1_COMPILER does not exist: ${FORT_STAGE1_COMPILER}")
    endif()
    set(FORT_PREVIOUS_COMPILER "${FORT_STAGE1_COMPILER}")
    set(FORT_PREVIOUS_DEPENDENCY "${FORT_STAGE1_COMPILER}")
    set(FORT_BOOTSTRAP_BINARY "${FORT_STAGE1_COMPILER}")
else()
    set(FORT_PREVIOUS_COMPILER "$<TARGET_FILE:fort_stage1>")
    set(FORT_PREVIOUS_DEPENDENCY fort_stage1)
    list(LENGTH FORT_PIN_SHAS FORT_PIN_COUNT)
    math(EXPR FORT_LAST_PIN_INDEX "${FORT_PIN_COUNT} - 1")
    foreach(pin_index RANGE 0 ${FORT_LAST_PIN_INDEX})
        list(GET FORT_PIN_NUMBERS ${pin_index} pin_number)
        list(GET FORT_PIN_SHAS ${pin_index} pin_sha)
        set(pin_dir "${FORT_BOOTSTRAP_DIR}/bootstrap-${pin_number}")
        set(pin_stamp "${pin_dir}/.assembled-${pin_sha}-${FORT_TARGET}")
        set(pin_binary "${pin_dir}/fort")
        add_custom_command(
            OUTPUT "${pin_stamp}"
            COMMAND "${CMAKE_COMMAND}"
                -DROOT=${CMAKE_SOURCE_DIR}
                -DSHA=${pin_sha}
                -DOUTPUT=${pin_dir}
                -DTARGET=${FORT_TARGET}
                -P "${FORT_TREE_SCRIPT}"
            DEPENDS "${FORT_BOOTSTRAP_REF}" "${FORT_TREE_SCRIPT}"
            COMMENT "Extracting and assembling bootstrap-${pin_number}"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${pin_binary}"
            COMMAND "${FORT_PREVIOUS_COMPILER}" ${FORT_STAGE_MODE_ARGS}
                --std-dir "${pin_dir}/target/std"
                -I "${pin_dir}/src/fort"
                --cc "${FORT_TARGET_CC_PATH}"
                -o "${pin_binary}" "${pin_dir}/target/entry/main.ft"
            DEPENDS "${pin_stamp}" ${FORT_PREVIOUS_DEPENDENCY}
            COMMENT "Building bootstrap-${pin_number} with its predecessor"
            VERBATIM
        )
        add_custom_target(fort_bootstrap_${pin_number} DEPENDS "${pin_binary}")
        set(FORT_PREVIOUS_COMPILER "${pin_binary}")
        set(FORT_PREVIOUS_DEPENDENCY "${pin_binary}")
        set(FORT_BOOTSTRAP_BINARY "${pin_binary}")
    endforeach()
endif()

set(FORT_HEAD_ENTRY "${FORT_FORT_SRC_DIR}/main.ft")
set(FORT_HEAD_BINARY "${FORT_BOOTSTRAP_DIR}/head/fort")
set(FORT_HEAD_STD "${FORT_BOOTSTRAP_DIR}/head/std")
set(FORT_HEAD_STD_STAMP "${FORT_HEAD_STD}/.assembled")
add_custom_command(
    OUTPUT "${FORT_HEAD_STD_STAMP}"
    COMMAND "${CMAKE_COMMAND}"
        -DSOURCE=${FORT_STD_DIR}
        -DOUTPUT=${FORT_HEAD_STD}
        -P "${CMAKE_SOURCE_DIR}/cmake/bootstrap_head.cmake"
    DEPENDS fort_std ${FORT_STD_OUTPUTS}
            "${CMAKE_SOURCE_DIR}/cmake/bootstrap_head.cmake"
    COMMENT "Assembling the bootstrap standard root for HEAD"
    VERBATIM
)
add_custom_command(
    OUTPUT "${FORT_HEAD_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FORT_BOOTSTRAP_DIR}/head"
    COMMAND "${FORT_PREVIOUS_COMPILER}" ${FORT_STAGE_MODE_ARGS}
        --std-dir "${FORT_HEAD_STD}"
        -I "${FORT_FORT_SRC_DIR}"
        --cc "${FORT_TARGET_CC_PATH}"
        -o "${FORT_HEAD_BINARY}" "${FORT_HEAD_ENTRY}"
    DEPENDS "${FORT_HEAD_STD_STAMP}" ${FORT_FORT_SOURCES}
            ${FORT_PREVIOUS_DEPENDENCY}
    COMMENT "Building working-tree HEAD with the last bootstrap compiler"
    VERBATIM
)

# Keep the target name and output path distinct inside Ninja. The target copies
# the graph output to the public build/fort path.
set(FORT_GRAPH_BINARY "${FORT_HEAD_BINARY}")
add_custom_target(fort ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FORT_HEAD_BINARY}" "${CMAKE_BINARY_DIR}/fort"
    DEPENDS "${FORT_HEAD_BINARY}"
    COMMENT "Publishing build/fort"
    VERBATIM
)

set(FORT_COMPILER_BINARY "${CMAKE_BINARY_DIR}/fort")

add_test(NAME target-host
    COMMAND bash "${FORT_TEST_DIR}/target_host_test.sh" "${FORT_TARGET}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(target-host PROPERTIES LABELS unit)

add_test(NAME target-workflow
    COMMAND bash "${FORT_TEST_DIR}/target_workflow_test.sh")
set_tests_properties(target-workflow PROPERTIES LABELS unit)

add_test(NAME lsp-target
    COMMAND bash "${FORT_TEST_DIR}/lsp_target_test.sh")
set_tests_properties(lsp-target PROPERTIES LABELS unit)

add_test(NAME darwin-probe-target
    COMMAND bash "${FORT_TEST_DIR}/abi/darwin_probe_target_test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(darwin-probe-target PROPERTIES LABELS unit)

add_test(NAME cmake-bootstrap
    COMMAND bash "${FORT_TEST_DIR}/cmake_bootstrap_test.sh" "${FORT_GRAPH_BINARY}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(cmake-bootstrap PROPERTIES LABELS unit)

set(FORT_GRAPH_FIXPOINT_COMMAND
    bash "${CMAKE_SOURCE_DIR}/tools/fixpoint.sh" "${CMAKE_BINARY_DIR}/cmake-fixpoint"
    --bootstrap "${FORT_BOOTSTRAP_BINARY}"
    --compiler "${FORT_COMPILER_BINARY}"
    --cc "${FORT_TARGET_CC_PATH}"
    --target "${FORT_TARGET_TRIPLE}"
    --entry "${FORT_HEAD_ENTRY}"
    --std "${FORT_HEAD_STD}"
    --source-root "${FORT_FORT_SRC_DIR}"
    --opt "${FORT_OPT_PATH}")
add_test(NAME bootstrap
    COMMAND ${FORT_GRAPH_FIXPOINT_COMMAND}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(bootstrap PROPERTIES LABELS lang)

# ---- the language server -------------------------------------------------------
# fort_lsp is src/lsp compiled by the production compiler. It stands at the
# build root. The C-started source chain builds that compiler for the native
# target. The linux binary is x86-64 ELF. The darwin binary is arm64 Mach-O.
#
# The search roots are src, so that a server module is `lsp.<name>` and never
# `<name>` (D9.1, D9.7, T-063), and src/fort, which is where `flt` stands.
# The entry file's own directory is the first root (D9.2) and holds no module
# any import names.
set(FORT_LSP_SRC_DIR "${CMAKE_SOURCE_DIR}/src/lsp")
file(GLOB FORT_LSP_SOURCES CONFIGURE_DEPENDS "${FORT_LSP_SRC_DIR}/*.ft")
# The binary is `fort-lsp` and the target is `fort_lsp`: ninja refuses a target
# whose name is also the path of an output in the same directory, and the
# hyphen is the name a language server carries in every other toolchain.
set(FORT_LSP_BINARY "${CMAKE_BINARY_DIR}/fort-lsp")
add_custom_command(
    OUTPUT "${FORT_LSP_BINARY}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/build_lsp.sh"
            "${FORT_COMPILER_BINARY}" "${FORT_TARGET_TRIPLE}"
            "${FORT_STD_DIR}" "${FORT_TARGET_CC_PATH}" "${FORT_LSP_BINARY}"
            "${FORT_LSP_SRC_DIR}/main.ft"
            "${CMAKE_SOURCE_DIR}/src" "${FORT_FORT_SRC_DIR}"
    # The file inputs and not the target alone: a
    # DEPENDS on a custom target is an order-only edge, so an edit to a module
    # of src/lsp would otherwise leave this binary as it was.
    DEPENDS fort "${FORT_COMPILER_BINARY}"
            "${CMAKE_SOURCE_DIR}/tools/build_lsp.sh"
            ${FORT_LSP_SOURCES} ${FORT_STD_OUTPUTS}
    COMMENT "Compiling src/lsp with stage2 into fort-lsp"
    VERBATIM
)
add_custom_target(fort_lsp ALL DEPENDS "${FORT_LSP_BINARY}")

# The binary itself uses one recorded script on stdin. Other tests drive the
# server modules inside one program. This test also checks the entry file and
# its standard input and output wiring.
add_test(NAME lsp-binary
    COMMAND bash "${FORT_TEST_DIR}/lsp_binary_test.sh" "${FORT_LSP_BINARY}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(lsp-binary PROPERTIES LABELS lang)

if(FORT_TARGET STREQUAL "darwin")
    # The remaining targets build the C compiler and its linux differential
    # tests. They do not belong to the darwin graph.
    return()
endif()

# ---- unit tests ----------------------------------------------------------------
# The test-only helpers that are compiled rather than included: every
# test/*.c that is not a suite. They are linked into every suite so that
# -Werror and clang-tidy cover them once. The list is empty today -- the
# syntax tree's S-expression printer, which used to be the one member, became
# what `fort --ast` writes and moved into src/bootstrap (D14.1) -- so the
# target is created only when the glob finds something, since a library with
# no source is a configure error. The glob and the link below stay: the next
# test/*.c that is not a suite is built and linked with no CMake edit.
file(GLOB FORT_TEST_SUPPORT_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/*.c")
list(FILTER FORT_TEST_SUPPORT_SOURCES EXCLUDE REGEX "_test\\.c$")
set(FORT_TEST_SUPPORT_LIB "")
if(FORT_TEST_SUPPORT_SOURCES)
    add_library(fort_test_support OBJECT ${FORT_TEST_SUPPORT_SOURCES})
    target_include_directories(fort_test_support PUBLIC "${FORT_TEST_DIR}")
    target_link_libraries(fort_test_support PUBLIC fort_core)
    set(FORT_TEST_SUPPORT_LIB fort_test_support)
endif()

# One executable and one ctest, labelled unit, per test/<component>_test.c
# (toolchain.md 7.5). The ctest is named unit-<component>.
file(GLOB FORT_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/*_test.c")
set(FORT_UNIT_TEST_TARGETS "")
foreach(test_source IN LISTS FORT_UNIT_TEST_SOURCES)
    get_filename_component(test_target "${test_source}" NAME_WE)
    string(REGEX REPLACE "_test$" "" test_component "${test_target}")
    add_executable("${test_target}" "${test_source}")
    target_include_directories("${test_target}" PRIVATE "${FORT_TEST_DIR}")
    target_link_libraries("${test_target}" PRIVATE fort_core ${FORT_TEST_SUPPORT_LIB})
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
foreach(driver_suite driver_test driver_check_test driver_conformance_test driver_tokens_test
                     driver_ast_test)
    if(TARGET "${driver_suite}")
        target_compile_definitions("${driver_suite}"
            PRIVATE FORT_FAKE_CC="${FORT_TEST_DIR}/fake_cc.sh")
    endif()
endforeach()

# The emitter suites run the LLVM verifier over every module they emit (D19.1)
# so they need the verifier's path. Every test/gen*_test.c gets it, so a new
# suite is covered by adding the file, and `opt` is a default rather than a
# configure-time requirement of every preset, as it is for the pipeline test
# and for the language harness.
foreach(gen_suite IN LISTS FORT_UNIT_TEST_TARGETS)
    if(gen_suite MATCHES "^gen")
        target_compile_definitions("${gen_suite}"
            PRIVATE FORT_OPT="${FORT_OPT_PATH}")
    endif()
endforeach()

# runtime_sig_test reads std/rt.ft and check_conv_test std/libc.ft: the first
# holds every row of the entry-point table against the fort signature that
# defines it (toolchain.md 5.1), the second holds D9.8's closure-wide rule
# against the library's own declarations (D9.10). Both need the sources.
#
# They read the C oracle root and not HEAD's. Both suites parse the file with
# the C front end. HEAD's std/rt.ft is held against the fort table by each
# program that stage2 builds.
foreach(std_reader runtime_sig_test check_conv_test)
    if(TARGET "${std_reader}")
        target_compile_definitions("${std_reader}"
            PRIVATE FORT_STD_SOURCE_DIR="${FORT_ORACLE_STD_DIR}")
    endif()
endforeach()

# The cross pipeline: the hand-written LLVM IR modules under test/ir, which
# are in the form of toolchain.md 6 (D19.1), are verified with opt, compiled
# and linked on their own and run under qemu (toolchain.md 2). A module holds
# the whole program, the runtime included (D9.10, D13.1), so the link takes no
# object beside it. It is a unit test so that check covers it; it needs the
# target clang, not the compiler.
add_test(NAME pipeline
    COMMAND bash "${FORT_TEST_DIR}/pipeline_test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(pipeline PROPERTIES
    LABELS unit
    ENVIRONMENT "FORT_TARGET_CC=${FORT_TARGET_CC_PATH};FORT_TARGET_TRIPLE=${FORT_TARGET_TRIPLE}")

# The linux compiler emits darwin IR with a caller-selected standard root.
# The verifier checks its form. T-142 owns darwin ABI and stack probes.
add_test(NAME target-ir
    COMMAND bash "${FORT_TEST_DIR}/target_ir_test.sh" "${FORT_COMPILER_BINARY}"
            "${FORT_STD_DIR}" "${FORT_OPT_PATH}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(target-ir PROPERTIES LABELS unit)

# The C helpers of the language tests are cross-compiled by the harness; they
# also build on the host so that -Werror and clang-tidy cover them. They
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
add_dependencies(check fort fort_std fort_oracle_tree)
if(FORT_UNIT_TEST_TARGETS)
    add_dependencies(check ${FORT_UNIT_TEST_TARGETS})
endif()

# The language tests (toolchain.md 7, D14.4, D14.5): test/lang/run_tests.py
# drives the compiler over test/lang. The ctest `lang` (label lang) and the
# check-lang target run the same command, and `lang_check_json` the cross-check
# of the check mode (D20.2); `lang_lint` validates the directives and
# `lang_selftest` runs the harness's own unit tests (both label unit).
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
#
# --std-dir is the C oracle library and not HEAD's. This keeps the frozen C
# language subset independent from the production source pins.
set(FORT_LANG_COMMAND
    "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}"
    --fort "$<TARGET_FILE:fort_stage1>"
    --std-dir "${FORT_ORACLE_STD_DIR}"
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

# The compiler's own modules: test/fort/<x>_test.ft is a run test in the
# directives of D14.5 that imports a module of src/fort with `-I` and asserts
# what its comments promise. The harness takes the corpus root, so the same
# run_tests.py judges both.
#
# stage2 compiles them and not stage1 (T-131). They import HEAD's src/fort,
# which may use any form the last pin implements, so stage1 is the wrong
# compiler for them from here on. That makes this test label lang: it runs an
# x86-64 binary under qemu.
set(FORT_MODULE_TEST_DIR "${FORT_TEST_DIR}/fort")
set(FORT_MODULE_TEST_COMMAND
    "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}"
    --root "${FORT_MODULE_TEST_DIR}"
    --fort "${FORT_COMPILER_BINARY}"
    --std-dir "${FORT_STD_DIR}"
    --cc "${FORT_TARGET_CC_PATH}"
    --opt "${FORT_OPT_PATH}"
    --verify-ir
    -j "${FORT_LANG_JOBS}"
)
add_test(NAME fort-modules COMMAND ${FORT_MODULE_TEST_COMMAND})
set_tests_properties(fort-modules PROPERTIES
    LABELS lang
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")

# The language corpus against stage2, with the expected failures of
# xfail-stage2.txt: the compiler being written in fort is held against the
# same tests as the one it replaces, and an entry leaves that list in the
# commit that makes its tests pass (D14.4). stage2 is an x86-64 binary, so
# every run of it here goes through qemu.
#
# It does not pass --no-unsupported, and T-038 measured why. stage2 rejects
# almost every feature bootstrap-unsupported.txt lists, for the reasons stage1
# rejects it. With the flag, stage2 passed 519 of 538 tests and nineteen
# entries stayed in xfail-stage2.txt for ever; without it, stage2 passed all
# 538 and the file is empty. The stronger property is the one asserted.
#
# It passes --verify-ir for a reason T-043 created (the other two commands
# always did). Until that ticket, every module stage2 emitted was emitted by
# stage1 as well and read by `opt` on the stage1 command; now the tests that
# use a nested array or span level, a `?:`, a `do`-`while`, a float, `$cfg`, or `$if`
# compile under stage2 alone. diff_ir.sh skips them because stage1 refuses them, and
# test/fort/support/gen_env.ft runs no `opt` by design. Without the flag no
# verifier would read that IR at all.
#
# It passes a list of its own instead (T-043). The two compilers no longer
# refuse the same set: src/fort reads the nested array and span levels of D3.6
# and src/bootstrap does not, so the tests that use them must be rejected
# under stage1 and must compile and run under stage2. One list per compiler
# says exactly that, and each name is judged by one rule or the other and
# never excused. T-044 is the second ticket of that shape, T-041 is the third,
# T-156 is the fourth, and T-157 is the fifth:
# stage2 implements `do`-`while` and `?:` (D6.6, D7.5), floats (D2.6, D3.1,
# D18), `$cfg` (D21.1), and `$if` (D21.2). Stage1 refuses all six families.
# Stage2's list is empty. Every entry stays in stage1's list, which is frozen.
set(FORT_LANG_STAGE2_COMMAND
    "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}"
    --fort "${FORT_COMPILER_BINARY}"
    --std-dir "${FORT_STD_DIR}"
    --cc "${FORT_TARGET_CC_PATH}"
    --opt "${FORT_OPT_PATH}"
    --xfail "${FORT_LANG_DIR}/xfail-stage2.txt"
    --unsupported "${FORT_LANG_DIR}/unsupported-stage2.txt"
    --verify-ir
    -j "${FORT_LANG_JOBS}"
)
add_test(NAME lang-stage2 COMMAND ${FORT_LANG_STAGE2_COMMAND})
set_tests_properties(lang-stage2 PROPERTIES LABELS lang)

# The check mode of stage2 over the same corpus: `fort --check --json` on every
# fail test, held against the text form of D14.2, and `--index` on every test
# with a golden index.json beside it, held against that file byte for byte
# (D20.2, D20.3). It is the oracle of the json and index ports (T-093).
#
# --check-json selects two groups of tests and no other. It selects every
# `fail` test, which checks a diagnostic document, and every test with a golden
# index.json beside it, which checks the clean compile document. The diagnostic
# half grows with each new `fail` test. The clean half stays small, because a
# short list of golden files covers it. Read the counts off the harness:
# `run_tests.py --list` prints the corpus and `run_tests.py --check-json --list`
# prints the selection. Each list ends with one summary line, so `wc -l` reports
# one more than the count. This comment held those three counts until T-133
# deleted them. Six tickets had found them stale, because no tool reads a
# comment and nothing goes red. `git show 7b5c625:CMakeLists.txt` holds the
# history those six tickets wrote here (T-133).
#
# Against a golden the comparison is stricter than a diff of two compilers for
# the value and the order of every record, since it holds both compilers
# against a file in the repository; it is weaker for whitespace, because
# run_tests.py re-serialises the records with separators of its own before it
# compares.
# --check-json ignores the expectation list, so the --xfail of the stage2
# command above does not apply here. It is a command of check-lang as well as
# a ctest, which is what makes the gate run it.
add_test(NAME lang_check_json-stage2 COMMAND ${FORT_LANG_STAGE2_COMMAND} --check-json)
set_tests_properties(lang_check_json-stage2 PROPERTIES LABELS lang)

# src/fort/main.ft carries the option table of toolchain.md 1 and the version
# string of src/bootstrap/driver.h as a second copy of what driver.c prints.
# Both binaries exist here, so the two copies are held against each other by
# running them: --help, --version and the usage line, byte for byte.
add_test(NAME stage-usage
    COMMAND bash "${FORT_TEST_DIR}/stage_usage_test.sh" "${CMAKE_BINARY_DIR}")
set_tests_properties(stage-usage PROPERTIES LABELS unit)

add_test(NAME stage2-ast-variadic
    COMMAND bash "${FORT_TEST_DIR}/stage2_ast_variadic_test.sh" "${FORT_COMPILER_BINARY}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(stage2-ast-variadic PROPERTIES LABELS unit)

add_test(NAME stage2-cfg
    COMMAND bash "${FORT_TEST_DIR}/stage2_cfg_test.sh" "${FORT_COMPILER_BINARY}"
            "${FORT_STD_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(stage2-cfg PROPERTIES LABELS unit)

add_test(NAME stage2-if
    COMMAND bash "${FORT_TEST_DIR}/stage2_if_test.sh" "${FORT_COMPILER_BINARY}"
            "${FORT_STD_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(stage2-if PROPERTIES LABELS unit)

add_test(NAME stage2-import-if
    COMMAND bash "${FORT_TEST_DIR}/stage2_import_if_test.sh" "${FORT_COMPILER_BINARY}"
            "${FORT_STD_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(stage2-import-if PROPERTIES LABELS unit)

# The two lexers over the whole repository: `fort --tokens` writes one line per
# token (D14.1, toolchain.md 1). tools/diff_tokens.sh compares dumps,
# diagnostics and exit statuses for 964 of 971 files. It separately measures
# three `$cfg` files and four `$if` files that the frozen C compiler cannot lex.
# It is the oracle for the common subset. It has label lang because it runs the
# x86-64 stage2 binary under qemu once per file.
add_test(NAME diff-tokens
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_tokens.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(diff-tokens PROPERTIES LABELS lang)

# The two parsers over the whole repository, the same comparison one pass
# later: `fort --ast` writes the file's syntax tree as one S-expression
# (D14.1, toolchain.md 1) and tools/diff_ast.sh compares stage1's with
# stage2's, together with their diagnostics and their exit statuses. It skips
# measured families for nested types, forms, floats, `$cfg`, and `$if`. It is the
# oracle for syntax that both parsers accept.
add_test(NAME diff-ast
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_ast.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(diff-ast PROPERTIES LABELS lang)

# The two checkers over the whole repository, one pass later again: `fort
# --check` runs the front end and stops (D20.1), and tools/diff_check.sh
# compares the diagnostics and the exit statuses of every .ft file stage1
# checks clean. It is the half of the checker's oracle the language corpus
# cannot see: a corpus of failing tests holds the diagnostics stage2 reports,
# while this holds the ones it must not, over the compiler's own sources and
# the standard library among the rest.
add_test(NAME diff-check
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_check.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(diff-check PROPERTIES LABELS lang)

# The two emitters over the whole repository, one pass later again: `fort -S`
# writes the program's LLVM IR module and stops before --cc (toolchain.md 2),
# and D19.5 makes that text a function of the program alone, so
# tools/diff_ir.sh compares stage1's module with stage2's byte for byte over
# every .ft file stage1 compiles -- every `run` and `programs` test, every
# module test of test/fort, and src/fort/main.ft, which is the compiler
# emitting itself. It is the oracle the transliterated emitter is judged
# against, and it sees the whole front end with it.
add_test(NAME diff-ir
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_ir.sh" "${CMAKE_BINARY_DIR}"
            --opt "${FORT_OPT_PATH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(diff-ir PROPERTIES LABELS lang)

# The interactive half of D11.5 (toolchain.md 5.3): a program whose stdout is
# a terminal flushes each line as it writes it. No language test can see that,
# because run_tests.py captures stdout through a pipe and every test therefore
# takes the non-interactive path, so test/tty_test.py compiles one program and
# drives it on a real pty and on a pipe. It is a unit test like `pipeline`, and
# it needs the compiler and the runtime object this build made.
add_test(NAME tty
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/tty_test.py"
            --fort "$<TARGET_FILE:fort_stage1>"
            --std-dir "${FORT_ORACLE_STD_DIR}"
            --cc "${FORT_TARGET_CC_PATH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(tty PROPERTIES
    LABELS unit
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")

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
# measures a branch's own diff (notes/testing.md 9).
add_test(NAME lines_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest lines_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(lines_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The unit tests of tools/mutate.py (label unit): the parts of a mutation round
# that fail silently -- the ctest failure parser, the anchor that must match
# once, the stale-binary guard, the restore, the timeout verdict, the exit
# status, the round loop and the guard that keeps this suite from judging a
# round -- and the anchors of tools/mutations/emitter_bootstrap.json against the
# sources they name, which is the one way a table of textual anchors rots
# (T-078). It runs no round: a round rebuilds the compiler.
add_test(NAME mutate_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest mutate_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(mutate_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The shape of the decision log (label unit): every entry is `### Dn.m Title`
# with an owner field and a rule field, and no line is over 100 columns
# (T-100). The log is normative, so a malformed entry is a defect the gate
# must see; test/highlight_test.py and test/fort_lint_test.py read rules out
# of it through the same parser.
add_test(NAME check_decisions
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/check_decisions.py"
            "${CMAKE_SOURCE_DIR}/spec/decisions.md" --expect 148
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(check_decisions PROPERTIES LABELS unit)

# Its own unit tests (label unit): the two readers, every problem the lint
# reports, and the before-and-after comparison that T-100 was accepted on.
add_test(NAME check_decisions_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest check_decisions_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(check_decisions_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The knowledge rules of notes/style.md and AGENTS.md (label unit): the line
# budget of AGENTS.md, the citation shape of notes/style.md 1.4 and the `///`
# of 1.3 over the sources 1.1 governs, an owner and a rule in every entry of
# spec/decisions.md, no history note of the log that reports a rule in the
# present tense with no date on the claim (T-121), and a document cited by a
# path that exists and by its section rather than by a line number (T-103). It
# reads the tree and runs no build, so it is the cheapest test in the suite.
add_test(NAME knowledge_lint
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/knowledge_lint.py"
            --root "${CMAKE_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(knowledge_lint PROPERTIES LABELS unit)

# Its own unit tests (label unit): the comment scanner, the four fixtures the
# `///` rule is measured against position by position, every citation shape the
# lint refuses with a control beside it, the seeded history notes the tense rule
# judges, the four real reports of the log before T-109 repaired them, the
# measurement behind each limit that rule states, and each of the five rules
# broken on a seeded tree, where the lint must print the problem and exit 1. A
# check that cannot fail is worse than no check.
add_test(NAME knowledge_lint_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest knowledge_lint_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(knowledge_lint_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The VM gate manifest must detect ignored fort files and symlinks; skip build outputs (T-148).
add_test(NAME gate_ft_manifest
    COMMAND bash "${FORT_TEST_DIR}/gate_ft_manifest_test.sh")
set_tests_properties(gate_ft_manifest PROPERTIES LABELS unit)

# The D1.4 identifier check over the project's own fort sources: it reads
# `fort --index` (D20.3) rather than tokenizing fort a second time, so it needs
# a compiler.
#
# stage2 is that compiler and stage1 is not (T-131). The lint must read every
# fort source of the repository, HEAD's src/fort and HEAD's std included, and
# those may use any form the last pin implements. Until T-131 stage1 ran it and
# a second ctest, fort_lint_float, ran stage2 over the two sources stage1
# cannot read; one compiler that reads them all replaces the pair, and
# `SKIPPED` in tools/fort_lint.py is empty. It is label lang rather than unit
# because it runs an x86-64 binary under qemu, and it names --std-dir because
# the library does not stand beside that binary.
add_test(NAME fort_lint
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/fort_lint.py"
            --fort "${FORT_COMPILER_BINARY}" --std-dir "${FORT_STD_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(fort_lint PROPERTIES LABELS lang)

# darwin declarations need a darwin standard root. The linux compiler checks
# their names (T-144).
add_test(NAME fort_lint_darwin
    COMMAND bash "${FORT_TEST_DIR}/darwin_fort_lint_test.sh" "${FORT_COMPILER_BINARY}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(fort_lint_darwin PROPERTIES LABELS lang)

# Its own unit tests (label unit): the rules over synthetic index records, the
# plumbing against a fake compiler, and the fixtures in test/fort_lint through
# the real one, which FORT_BINARY names.
add_test(NAME fort_lint_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest fort_lint_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(fort_lint_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;FORT_BINARY=$<TARGET_FILE:fort_stage1>;FORT_ORACLE_DIR=${FORT_ORACLE_DIR}")

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
# the conversion of its 1-based byte columns to character offsets, and the
# activation script itself driven against a fake editor
# (editors/vscode/test/fake_vscode.js), which leaves only what needs a real one
# to the manual smoke test in editors/README.md.
find_program(FORT_NODE NAMES node nodejs REQUIRED)
add_test(NAME extension_selftest
    COMMAND "${FORT_NODE}" --test
    WORKING_DIRECTORY "${FORT_EDITORS_DIR}/vscode")
set_tests_properties(extension_selftest PROPERTIES LABELS unit)

add_custom_target(check-lang
    COMMAND ${FORT_LANG_COMMAND}
    COMMAND ${FORT_LANG_COMMAND} --check-json
    COMMAND ${FORT_MODULE_TEST_COMMAND}
    COMMAND ${FORT_LANG_STAGE2_COMMAND}
    COMMAND ${FORT_LANG_STAGE2_COMMAND} --check-json
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/fort_lint.py"
            --fort "${FORT_COMPILER_BINARY}" --std-dir "${FORT_STD_DIR}"
    COMMAND bash "${FORT_TEST_DIR}/darwin_fort_lint_test.sh" "${FORT_COMPILER_BINARY}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_tokens.sh" "${CMAKE_BINARY_DIR}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_ast.sh" "${CMAKE_BINARY_DIR}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_check.sh" "${CMAKE_BINARY_DIR}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/tools/diff_ir.sh" "${CMAKE_BINARY_DIR}"
            --opt "${FORT_OPT_PATH}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/lsp_binary_test.sh" "${FORT_LSP_BINARY}"
    COMMAND ${FORT_GRAPH_FIXPOINT_COMMAND}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running the language tests"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(check-lang fort fort_std fort_lsp fort_oracle_tree)

add_custom_target(check-all COMMENT "Running every test")
add_dependencies(check-all check check-lang)

# ---- format, tidy, lines -----------------------------------------------------------
file(GLOB_RECURSE FORT_LINT_SOURCES CONFIGURE_DEPENDS
    "${FORT_SRC_DIR}/*.c" "${FORT_SRC_DIR}/*.h"
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
            --fort "${FORT_COMPILER_BINARY}" --std-dir "${FORT_STD_DIR}"
    COMMAND bash "${FORT_TEST_DIR}/darwin_fort_lint_test.sh" "${FORT_COMPILER_BINARY}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking the identifier conventions of D1.4 in the project's fort"
    VERBATIM
)
add_dependencies(fort-lint fort fort_std)

add_custom_target(lines
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/lines.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Counting test lines per compiler line"
    VERBATIM
)
