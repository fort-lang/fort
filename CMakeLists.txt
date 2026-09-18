cmake_minimum_required(VERSION 3.25)
project(fort LANGUAGES C)

# ---- toolchain --------------------------------------------------------------
# Use C11 without extensions. Make POSIX.1-2008 visible.
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
option(FORT_ENABLE_BOOTSTRAP "Build fort through the C-started source chain" ON)
set(FORT_STAGE1_COMPILER "" CACHE FILEPATH
    "External fort compiler that builds working-tree HEAD")

if(FORT_ENABLE_BOOTSTRAP AND FORT_STAGE1_COMPILER)
    message(FATAL_ERROR
        "FORT_ENABLE_BOOTSTRAP=ON does not accept FORT_STAGE1_COMPILER")
elseif(NOT FORT_ENABLE_BOOTSTRAP AND NOT FORT_STAGE1_COMPILER)
    message(FATAL_ERROR
        "FORT_ENABLE_BOOTSTRAP=OFF requires FORT_STAGE1_COMPILER")
endif()

if(FORT_ENABLE_BOOTSTRAP)
    set(FORT_BOOTSTRAP_DIR "${CMAKE_BINARY_DIR}/bootstrap")
    set(FORT_BOOTSTRAP_REF "${CMAKE_SOURCE_DIR}/tools/bootstrap.ref" CACHE FILEPATH
        "Ordered source revisions for the C-started bootstrap")
else()
    get_filename_component(FORT_STAGE1_COMPILER
        "${FORT_STAGE1_COMPILER}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${FORT_STAGE1_COMPILER}")
        message(FATAL_ERROR
            "FORT_STAGE1_COMPILER does not exist: ${FORT_STAGE1_COMPILER}")
    endif()
endif()

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
set(FORT_TARGET_FILE "${CMAKE_BINARY_DIR}/bootstrap/target")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bootstrap")
file(GENERATE OUTPUT "${FORT_TARGET_FILE}"
    CONTENT "${FORT_TARGET_TRIPLE}\n")

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
if(FORT_ENABLE_BOOTSTRAP)
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

    # The compiler binary sits below the bootstrap directory. Product tests
    # pass their standard root explicitly.
    add_executable(fort_stage1 "${FORT_SRC_DIR}/main.c")
    target_link_libraries(fort_stage1 PRIVATE fort_core)
    set_target_properties(fort_stage1 PROPERTIES
        OUTPUT_NAME fort
        RUNTIME_OUTPUT_DIRECTORY "${FORT_BOOTSTRAP_DIR}/stage1")
endif()

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
if(FORT_ENABLE_BOOTSTRAP)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FORT_BOOTSTRAP_REF}")
    set(FORT_PIN_NUMBERS "")
    set(FORT_PIN_SHAS "")
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

# ---- the self-hosted compiler --------------------------------------------------
file(GLOB FORT_FORT_SOURCES CONFIGURE_DEPENDS "${FORT_FORT_SRC_DIR}/*.ft")
set(FORT_TREE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/bootstrap_tree.cmake")

if(NOT FORT_ENABLE_BOOTSTRAP)
    set(FORT_PREVIOUS_COMPILER "${FORT_STAGE1_COMPILER}")
    set(FORT_PREVIOUS_DEPENDENCY "${FORT_STAGE1_COMPILER}")
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
    endforeach()
endif()

set(FORT_HEAD_ENTRY "${FORT_FORT_SRC_DIR}/main.ft")
if(FORT_ENABLE_BOOTSTRAP)
    set(FORT_HEAD_DIR "${FORT_BOOTSTRAP_DIR}/head")
    set(FORT_HEAD_STD "${FORT_HEAD_DIR}/std")
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
    set(FORT_HEAD_STD_DEPENDENCY "${FORT_HEAD_STD_STAMP}")
else()
    set(FORT_HEAD_DIR "${CMAKE_BINARY_DIR}/head")
    set(FORT_HEAD_STD "${FORT_STD_DIR}")
    set(FORT_HEAD_STD_DEPENDENCY fort_std)
endif()
set(FORT_HEAD_BINARY "${FORT_HEAD_DIR}/fort")
add_custom_command(
    OUTPUT "${FORT_HEAD_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FORT_HEAD_DIR}"
    COMMAND "${FORT_PREVIOUS_COMPILER}" ${FORT_STAGE_MODE_ARGS}
        --std-dir "${FORT_HEAD_STD}"
        -I "${FORT_FORT_SRC_DIR}"
        --cc "${FORT_TARGET_CC_PATH}"
        -o "${FORT_HEAD_BINARY}" "${FORT_HEAD_ENTRY}"
    DEPENDS ${FORT_HEAD_STD_DEPENDENCY} ${FORT_FORT_SOURCES}
            ${FORT_PREVIOUS_DEPENDENCY}
    COMMENT "Building working-tree HEAD with its predecessor compiler"
    VERBATIM
)

# Keep the target name and output path distinct inside Ninja. The target copies
# the graph output to the public build/fort path.
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

set(FORT_GRAPH_FIXPOINT_COMMAND
    bash "${CMAKE_SOURCE_DIR}/tools/fixpoint.sh" "${CMAKE_BINARY_DIR}/cmake-fixpoint"
    --compiler "${FORT_COMPILER_BINARY}"
    --cc "${FORT_TARGET_CC_PATH}"
    --target "${FORT_TARGET_TRIPLE}"
    --entry "${FORT_HEAD_ENTRY}"
    --std "${FORT_STD_DIR}"
    --source-root "${FORT_FORT_SRC_DIR}"
    --opt "${FORT_OPT_PATH}")
add_test(NAME fixpoint
    COMMAND ${FORT_GRAPH_FIXPOINT_COMMAND}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(fixpoint PROPERTIES LABELS lang)

# ---- the language server -------------------------------------------------------
# fort_lsp is src/lsp compiled by the production compiler. It stands at the
# build root. The C-started source chain builds that compiler for the native
# target. The linux binary is x86-64 ELF. The darwin binary is arm64 Mach-O.
#
# The search roots are src, so that a server module is `lsp.<name>` and never
# `<name>`. The other root is src/fort, which contains `flt`.
# The entry file's directory is the first root. No import names a module there.
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
    # Depend on the input files and the target. A custom target creates only an
    # order edge. Without the file inputs, a module edit would not rebuild this binary.
    DEPENDS fort "${FORT_COMPILER_BINARY}"
            "${CMAKE_SOURCE_DIR}/tools/build_lsp.sh"
            ${FORT_LSP_SOURCES} ${FORT_STD_OUTPUTS}
    COMMENT "Compiling src/lsp with the product compiler into fort-lsp"
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
    # The remaining targets build and test the C compiler. They do not belong
    # to the darwin graph.
    return()
endif()

# ---- unit tests ----------------------------------------------------------------
if(FORT_ENABLE_BOOTSTRAP)
# Compile test-only helpers that a suite does not include. Link each helper into
# each suite so that -Werror and clang-tidy inspect it once. The list is empty.
# Create the target only when the glob finds a helper. CMake rejects a library
# with no source. A new helper then needs no CMake edit.
file(GLOB FORT_TEST_SUPPORT_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/*.c")
list(FILTER FORT_TEST_SUPPORT_SOURCES EXCLUDE REGEX "_test\\.c$")
set(FORT_TEST_SUPPORT_LIB "")
if(FORT_TEST_SUPPORT_SOURCES)
    add_library(fort_test_support OBJECT ${FORT_TEST_SUPPORT_SOURCES})
    target_include_directories(fort_test_support PUBLIC "${FORT_TEST_DIR}")
    target_link_libraries(fort_test_support PUBLIC fort_core)
    set(FORT_TEST_SUPPORT_LIB fort_test_support)
endif()

# Each test/<component>_test.c gets one executable and one unit test.
# The test name is unit-<component>.
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
        LABELS "unit;bootstrap"
        ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")
    list(APPEND FORT_UNIT_TEST_TARGETS "${test_target}")
endforeach()

# The driver tests spawn test/fake_cc.sh as their --cc: it records the command
# line that the driver built. The check suite asserts that `--check` does not start it.
# The conformance suite makes the same assertion for runs that stop before it.
foreach(driver_suite driver_test driver_check_test driver_conformance_test driver_tokens_test
                     driver_ast_test)
    if(TARGET "${driver_suite}")
        target_compile_definitions("${driver_suite}"
            PRIVATE FORT_FAKE_CC="${FORT_TEST_DIR}/fake_cc.sh")
    endif()
endforeach()

# The emitter suites run the LLVM verifier over each emitted module.
# Each suite needs the verifier path. Every test/gen*_test.c gets it, so a new
# suite is covered by adding the file, and `opt` is a default rather than a
# configure-time requirement of every preset, as it is for the pipeline test
# and for the language harness.
foreach(gen_suite IN LISTS FORT_UNIT_TEST_TARGETS)
    if(gen_suite MATCHES "^gen")
        target_compile_definitions("${gen_suite}"
            PRIVATE FORT_OPT="${FORT_OPT_PATH}")
    endif()
endforeach()

# runtime_sig_test reads std/rt.ft. It checks each entry-point row against its
# fort signature. check_conv_test reads std/libc.ft and checks imported calls
# against the library declarations.
# Both suites need the sources.
#
# Both suites parse the standard root that bootstrap-0 uses. This makes them
# contracts between the C implementation and its bootstrap library.
foreach(std_reader runtime_sig_test check_conv_test)
    if(TARGET "${std_reader}")
        target_compile_definitions("${std_reader}"
            PRIVATE FORT_STD_SOURCE_DIR="${FORT_BOOTSTRAP_DIR}/bootstrap-0/target/std")
        string(REGEX REPLACE "_test$" "" std_reader_component "${std_reader}")
        set_tests_properties("unit-${std_reader_component}" PROPERTIES
            LABELS "unit;bootstrap;bootstrap-contract")
    endif()
endforeach()
endif()

# The cross pipeline: the hand-written LLVM IR modules under test/ir, which
# are complete modules. The pipeline verifies and compiles each module.
# It links each module alone and runs it under qemu. A module contains the runtime.
# The link takes no other object. This unit test needs the target clang, not fort.
add_test(NAME pipeline
    COMMAND bash "${FORT_TEST_DIR}/pipeline_test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(pipeline PROPERTIES
    LABELS unit
    ENVIRONMENT "FORT_TARGET_CC=${FORT_TARGET_CC_PATH};FORT_TARGET_TRIPLE=${FORT_TARGET_TRIPLE}")

# The linux compiler emits darwin IR with a caller-selected standard root.
# The verifier checks its form. Separate tests check the Darwin ABI and stack.
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
add_dependencies(check fort fort_std)
if(FORT_ENABLE_BOOTSTRAP)
    if(FORT_UNIT_TEST_TARGETS)
        add_dependencies(check ${FORT_UNIT_TEST_TARGETS})
    endif()
endif()

# The language test harness drives the compiler over test/lang.
# The ctests `lang` and `lang-json` use
# the product compiler. `lang_lint` validates the directives, and
# `lang_selftest` runs the harness's own unit tests (both label unit).
set(FORT_LANG_DIR "${FORT_TEST_DIR}/lang")

# The parser recovery suite walks the fail corpus and asserts that
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
# compiles. Thus, it verifies each module that the emitter writes.
#
# The compiler's own modules: test/fort/<x>_test.ft is a run test in the
# directive format. Each test imports a src/fort module with `-I` and asserts
# what its comments promise. The harness takes the corpus root, so the same
# run_tests.py judges both.
#
# The product compiler compiles these modules. They import the current source
# and use the current standard library.
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

# The product language test uses the current compiler and standard library.
# It verifies each module that the compiler emits.
set(FORT_LANG_COMMAND
    "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}"
    --fort "${FORT_COMPILER_BINARY}"
    --std-dir "${FORT_STD_DIR}"
    --cc "${FORT_TARGET_CC_PATH}"
    --opt "${FORT_OPT_PATH}"
    --xfail "${FORT_LANG_DIR}/xfail.txt"
    --verify-ir
    -j "${FORT_LANG_JOBS}"
)
add_test(NAME lang COMMAND ${FORT_LANG_COMMAND})
set_tests_properties(lang PROPERTIES LABELS lang)

# Product check mode runs over the same corpus. It compares JSON and text diagnostics.
# It runs `--index` on each test with a golden index.json file.
# The harness compares the index byte for byte and reports the measured count.
add_test(NAME lang-json COMMAND ${FORT_LANG_COMMAND} --check-json)
set_tests_properties(lang-json PROPERTIES LABELS lang)

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

# The interactive test checks line flushing when stdout is a terminal.
# Language tests capture stdout through a pipe and take the non-interactive path.
# test/tty_test.py drives one program through a real pty and a pipe.
# It needs the compiler and the standard root from this build.
add_test(NAME tty
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/tty_test.py"
            --fort "${FORT_COMPILER_BINARY}"
            --std-dir "${FORT_STD_DIR}"
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

# The tools/lines.py unit tests cover the custom glob matcher.
# They also cover --since measurement of a branch diff.
add_test(NAME lines_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest lines_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(lines_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The tools/mutate.py unit tests cover parsers, anchors, guards, restore, and timeouts.
# They also check the shipped table against its source anchors.
# The unit tests run no mutation round because a round rebuilds the compiler.
if(FORT_ENABLE_BOOTSTRAP)
    add_test(NAME mutate_selftest
        COMMAND "${FORT_PYTHON3}" -m unittest mutate_test
        WORKING_DIRECTORY "${FORT_TEST_DIR}")
    set_tests_properties(mutate_selftest PROPERTIES
        LABELS unit
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

# The shape of the decision log (label unit): every entry is `### Dn.m Title`
# with an owner field and a rule field. It also limits lines to 100 columns.
# The log is normative, so the gate must detect a malformed entry.
# Two other tests read rules through the same parser.
add_test(NAME check_decisions
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/check_decisions.py"
            "${CMAKE_SOURCE_DIR}/spec/decisions.md" --expect 148
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(check_decisions PROPERTIES LABELS unit)

# Its unit tests cover both readers and each reported problem.
# They also compare two log revisions.
add_test(NAME check_decisions_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest check_decisions_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(check_decisions_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The knowledge rules of notes/style.md and AGENTS.md (label unit): the line
# budget of AGENTS.md, an owner and a rule in each decision, no history note
# that reports a rule in the present tense without a date, and valid document
# paths. The test reads documentation and runs no build.
add_test(NAME knowledge_lint
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/knowledge_lint.py"
            --root "${CMAKE_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(knowledge_lint PROPERTIES LABELS unit)

# Its own unit tests (label unit): seeded history notes, the measured limits of
# the history rule, and each remaining rule on clean and broken seeded trees.
add_test(NAME knowledge_lint_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest knowledge_lint_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(knowledge_lint_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# The VM gate manifest detects ignored fort files and symlinks. It skips build outputs.
add_test(NAME gate_ft_manifest
    COMMAND bash "${FORT_TEST_DIR}/gate_ft_manifest_test.sh")
set_tests_properties(gate_ft_manifest PROPERTIES LABELS unit)

# The identifier check reads `fort --index` instead of tokenizing fort again.
# Thus, it needs a compiler.
#
# The product compiler reads all fort sources in the repository. The test has
# label lang because it runs an x86-64 binary under qemu. It names --std-dir
# because the library does not stand beside that binary.
add_test(NAME fort_lint
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/fort_lint.py"
            --fort "${FORT_COMPILER_BINARY}" --std-dir "${FORT_STD_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(fort_lint PROPERTIES LABELS lang)

# darwin declarations need a darwin standard root. The linux compiler checks
# their names.
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
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;FORT_BINARY=${FORT_COMPILER_BINARY};FORT_STD_DIR=${FORT_STD_DIR}")

# The unit tests of the TextMate grammar in editors/vscode (label unit): they
# compare its keyword and operator lists with the language lists. They also assert the
# scopes it gives the fixtures in test/highlight.
add_test(NAME highlight_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest highlight_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(highlight_selftest PROPERTIES
    LABELS unit
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# Node's built-in runner tests the VS Code extension in editors/vscode.
# It converts diagnostic documents into editor shapes.
# It also converts 1-based byte columns to character offsets.
# A fake editor drives activation. The manual smoke test covers real editor behavior.
find_program(FORT_NODE NAMES node nodejs REQUIRED)
add_test(NAME extension_selftest
    COMMAND "${FORT_NODE}" --test
    WORKING_DIRECTORY "${FORT_EDITORS_DIR}/vscode")
set_tests_properties(extension_selftest PROPERTIES LABELS unit)

add_custom_target(check-lang
    COMMAND ${FORT_MODULE_TEST_COMMAND}
    COMMAND ${FORT_LANG_COMMAND}
    COMMAND ${FORT_LANG_COMMAND} --check-json
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/fort_lint.py"
            --fort "${FORT_COMPILER_BINARY}" --std-dir "${FORT_STD_DIR}"
    COMMAND bash "${FORT_TEST_DIR}/darwin_fort_lint_test.sh" "${FORT_COMPILER_BINARY}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/lsp_binary_test.sh" "${FORT_LSP_BINARY}"
    COMMAND ${FORT_GRAPH_FIXPOINT_COMMAND}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running the language tests"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(check-lang fort fort_std fort_lsp)

add_custom_target(check-all COMMENT "Running every test")
add_dependencies(check-all check check-lang)

# ---- format, tidy, lines -----------------------------------------------------------
file(GLOB_RECURSE FORT_LINT_SOURCES CONFIGURE_DEPENDS
    "${FORT_SRC_DIR}/*.c" "${FORT_SRC_DIR}/*.h"
    "${FORT_TEST_DIR}/*.c" "${FORT_TEST_DIR}/*.h"
)

# Fort and project C sources do not permit block comments.
# format-check depends on this target, so a `/* */` fails the gate.
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
