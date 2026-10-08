cmake_minimum_required(VERSION 3.25)
# An address-sanitized build on darwin uses Apple clang: the AddressSanitizer
# runtime of Homebrew clang 19 hangs at exit on darwin 25 (a five-line C program
# did not return in 20 s, 2026-09-25). The asan presets name `clang` on both
# hosts; this is the one place the choice differs. It must precede project().
if(CMAKE_HOST_APPLE AND FORT_SANITIZER STREQUAL "address")
    set(CMAKE_C_COMPILER /usr/bin/clang CACHE FILEPATH "The C compiler" FORCE)
endif()
project(fort LANGUAGES C)

# The gcc preset asks for GCC. On darwin `gcc` is Apple clang under another name,
# so the preset would build with clang and say it built with GCC: stop instead.
if(CMAKE_C_COMPILER MATCHES "(^|/)gcc(-[0-9]+)?$" AND NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR
        "CMAKE_C_COMPILER=${CMAKE_C_COMPILER} is ${CMAKE_C_COMPILER_ID}, not GCC; "
        "the gcc preset needs GCC, which this host does not provide")
endif()

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

set(FORT_TEST_DIR "${CMAKE_SOURCE_DIR}/test")
set(FORT_STD_SRC_DIR "${CMAKE_SOURCE_DIR}/std")
set(FORT_EDITORS_DIR "${CMAKE_SOURCE_DIR}/editors")
set(FORT_STD_DIR "${CMAKE_BINARY_DIR}/std")
set(FORT_FORT_SRC_DIR "${CMAKE_SOURCE_DIR}/src/fort")
option(FORT_ENABLE_BOOTSTRAP0 "Build fort through the C-started source chain" ON)
set(FORT_STAGE1_COMPILER "" CACHE FILEPATH
    "External fort compiler that builds working-tree HEAD")

if(FORT_ENABLE_BOOTSTRAP0 AND FORT_STAGE1_COMPILER)
    message(FATAL_ERROR
        "FORT_ENABLE_BOOTSTRAP0=ON does not accept FORT_STAGE1_COMPILER")
elseif(NOT FORT_ENABLE_BOOTSTRAP0 AND NOT FORT_STAGE1_COMPILER)
    message(FATAL_ERROR
        "FORT_ENABLE_BOOTSTRAP0=OFF requires FORT_STAGE1_COMPILER")
endif()

if(FORT_ENABLE_BOOTSTRAP0)
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
# bootstrap0/ builds the C compiler fort_bootstrap0 and its tests. Its binary
# directory is FORT_BOOTSTRAP_DIR, so the compiler is bootstrap/fort.
include(cmake/dev_targets.cmake)
if(FORT_ENABLE_BOOTSTRAP0)
    add_subdirectory(bootstrap0 "${FORT_BOOTSTRAP_DIR}")
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
if(FORT_ENABLE_BOOTSTRAP0)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FORT_BOOTSTRAP_REF}")
    set(FORT_PIN_NUMBERS "")
    set(FORT_PIN_SHAS "")
    if(NOT EXISTS "${FORT_BOOTSTRAP_REF}")
        message(FATAL_ERROR "bootstrap reference is missing: ${FORT_BOOTSTRAP_REF}")
    endif()
    file(STRINGS "${FORT_BOOTSTRAP_REF}" FORT_REF_LINES)
    set(FORT_EXPECTED_PIN 1)
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
        message(FATAL_ERROR "${FORT_BOOTSTRAP_REF}: bootstrap-1 is missing")
    endif()
endif()

# ---- the self-hosted compiler --------------------------------------------------
file(GLOB FORT_FORT_SOURCES CONFIGURE_DEPENDS "${FORT_FORT_SRC_DIR}/*.ft")
set(FORT_TREE_SCRIPT "${CMAKE_SOURCE_DIR}/bootstrap0/extract_pin.sh")

if(NOT FORT_ENABLE_BOOTSTRAP0)
    set(FORT_PREVIOUS_COMPILER "${FORT_STAGE1_COMPILER}")
    set(FORT_PREVIOUS_DEPENDENCY "${FORT_STAGE1_COMPILER}")
else()
    set(FORT_PREVIOUS_COMPILER "$<TARGET_FILE:fort_bootstrap0>")
    set(FORT_PREVIOUS_DEPENDENCY fort_bootstrap0)
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
            COMMAND bash "${FORT_TREE_SCRIPT}" "${pin_sha}" "${pin_dir}" "${FORT_TARGET}"
            DEPENDS "${FORT_BOOTSTRAP_REF}" "${FORT_TREE_SCRIPT}"
            COMMENT "Extracting and assembling bootstrap-${pin_number}"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${pin_binary}"
            COMMAND "${FORT_PREVIOUS_COMPILER}" ${FORT_STAGE_MODE_ARGS}
                --std-dir "${pin_dir}/std"
                --cc "${FORT_TARGET_CC_PATH}"
                -o "${pin_binary}" "${pin_dir}/src/fort/main.ft"
            DEPENDS "${pin_stamp}" ${FORT_PREVIOUS_DEPENDENCY}
            COMMENT "Building bootstrap-${pin_number} with its predecessor"
            VERBATIM
        )
        add_custom_target(fort_bootstrap_${pin_number} DEPENDS "${pin_binary}")
        # bootstrap0/'s contract suites read the tree of the first pin.
        add_custom_target(fort_bootstrap_${pin_number}_source DEPENDS "${pin_stamp}")
        set(FORT_PREVIOUS_COMPILER "${pin_binary}")
        set(FORT_PREVIOUS_DEPENDENCY "${pin_binary}")
    endforeach()
endif()

set(FORT_HEAD_ENTRY "${FORT_FORT_SRC_DIR}/main.ft")
if(FORT_ENABLE_BOOTSTRAP0)
    set(FORT_HEAD_DIR "${FORT_BOOTSTRAP_DIR}/head")
    set(FORT_HEAD_STD "${FORT_HEAD_DIR}/std")
    set(FORT_HEAD_STD_STAMP "${FORT_HEAD_STD}/.assembled")
    # Some predecessors load rt_float.ft for every closure; HEAD folded that module
    # into std.rt, so the last pin builds HEAD with HEAD's root plus an empty one.
    add_custom_command(
        OUTPUT "${FORT_HEAD_STD_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${FORT_HEAD_STD}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${FORT_STD_DIR}" "${FORT_HEAD_STD}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${FORT_HEAD_STD}/rt_float.ft"
        COMMAND "${CMAKE_COMMAND}" -E touch "${FORT_HEAD_STD_STAMP}"
        DEPENDS fort_std ${FORT_STD_OUTPUTS}
        COMMENT "Assembling the standard root the last pin builds HEAD with"
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

set(FORT_GRAPH_FIXPOINT_COMMAND
    bash "${CMAKE_SOURCE_DIR}/tools/fixpoint.sh" "${CMAKE_BINARY_DIR}/cmake-fixpoint"
    --compiler "${FORT_COMPILER_BINARY}"
    --cc "${FORT_TARGET_CC_PATH}"
    --entry "${FORT_HEAD_ENTRY}"
    --std "${FORT_STD_DIR}"
    --source-root "${FORT_FORT_SRC_DIR}"
    --opt "${FORT_OPT_PATH}")
add_test(NAME fixpoint
    COMMAND ${FORT_GRAPH_FIXPOINT_COMMAND}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(fixpoint PROPERTIES LABELS "fort;integration")

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
# The binary is `fort-lsp` and the target is `fort_lsp`. Ninja refuses a target
# whose name is also the path of an output in the same directory. And the
# hyphen is the name a language server carries in every other toolchain.
set(FORT_LSP_BINARY "${CMAKE_BINARY_DIR}/fort-lsp")
add_custom_command(
    OUTPUT "${FORT_LSP_BINARY}"
    # The compiler builds for its own target, so no --target is passed (D14.1).
    COMMAND "${FORT_COMPILER_BINARY}" --std-dir "${FORT_STD_DIR}" --cc "${FORT_TARGET_CC_PATH}"
            -I "${CMAKE_SOURCE_DIR}/src" -I "${FORT_FORT_SRC_DIR}"
            -o "${FORT_LSP_BINARY}" "${FORT_LSP_SRC_DIR}/main.ft"
    # Depend on the input files and the target. A custom target creates only an
    # order edge. Without the file inputs, a module edit would not rebuild this binary.
    DEPENDS fort "${FORT_COMPILER_BINARY}" ${FORT_LSP_SOURCES} ${FORT_STD_OUTPUTS}
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
set_tests_properties(lsp-binary PROPERTIES LABELS "lsp;integration")

# The C helpers of the language tests are compiled for the target by the
# harness; they also build on the host so that -Werror covers them. They are FFI entry points
# declared by the tests, hence no prototypes.
file(GLOB FORT_LANG_FFI_SOURCES CONFIGURE_DEPENDS "${FORT_TEST_DIR}/lang/ffi/*.c")
add_library(lang_ffi_helpers OBJECT ${FORT_LANG_FFI_SOURCES})
target_compile_options(lang_ffi_helpers PRIVATE -Wno-missing-prototypes)

# The language test harness drives the compiler over test/lang.
# The ctests `lang` and `lang-json` use the product compiler. The harness
# validates the directives of the corpus before every run.
set(FORT_LANG_DIR "${FORT_TEST_DIR}/lang")
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
    --target "${FORT_TARGET_TRIPLE}"
    --opt "${FORT_OPT_PATH}"
    --verify-ir
    -j "${FORT_LANG_JOBS}"
)
add_test(NAME fort-modules COMMAND ${FORT_MODULE_TEST_COMMAND})
set_tests_properties(fort-modules PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")

# The language server's modules: test/lsp/<x>_test.ft, in the format of
# test/fort. A corpus of its own, so the label `lsp` selects the server tests
# apart from the compiler tests.
set(FORT_LSP_MODULE_TEST_COMMAND
    "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}"
    --root "${FORT_TEST_DIR}/lsp"
    --fort "${FORT_COMPILER_BINARY}"
    --std-dir "${FORT_STD_DIR}"
    --cc "${FORT_TARGET_CC_PATH}"
    --target "${FORT_TARGET_TRIPLE}"
    --opt "${FORT_OPT_PATH}"
    --verify-ir
    -j "${FORT_LANG_JOBS}"
)
add_test(NAME lsp-modules COMMAND ${FORT_LSP_MODULE_TEST_COMMAND})
set_tests_properties(lsp-modules PROPERTIES
    LABELS "lsp;unit"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")

# The FIR tests: test/fir/<pass>/<x>.fir holds a function in the textual form of
# spec/fir.md 13, the pass to run and the expected text. `fort --fir-test` parses
# the file, runs the pass and writes the module; test/fir/run_tests.py compares it.
# The compiler runs natively, so the sanitizer presets instrument it.
add_test(NAME fir
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/fir/run_tests.py"
            --fort "${FORT_COMPILER_BINARY}"
            --std-dir "${FORT_STD_DIR}")
set_tests_properties(fir PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")

# The product language test uses the current compiler and standard library.
# It verifies each module that the compiler emits.
set(FORT_LANG_COMMAND
    "${FORT_PYTHON3}" "${FORT_LANG_HARNESS}"
    --fort "${FORT_COMPILER_BINARY}"
    --std-dir "${FORT_STD_DIR}"
    --cc "${FORT_TARGET_CC_PATH}"
    --target "${FORT_TARGET_TRIPLE}"
    --opt "${FORT_OPT_PATH}"
    --xfail "${FORT_LANG_DIR}/xfail.txt"
    --verify-ir
    -j "${FORT_LANG_JOBS}"
)
add_test(NAME lang COMMAND ${FORT_LANG_COMMAND})
set_tests_properties(lang PROPERTIES LABELS "fort;integration")

# Product check mode runs over the same corpus. It compares JSON and text diagnostics.
# It runs `--index` on each test with a golden index.json file.
# The harness compares the index byte for byte and reports the measured count.
add_test(NAME lang-json COMMAND ${FORT_LANG_COMMAND} --check-json)
set_tests_properties(lang-json PROPERTIES LABELS "fort;integration")
# Each harness run takes FORT_LANG_JOBS processors. Told so, `ctest -j` runs
# nothing beside it; untold, it ran seven tests beside a full-load harness on
# the darwin host and `lang_selftest` reported its 5 s fake compiler as timed
# out (2026-09-25, three presets).
set_tests_properties(fort-modules lsp-modules lang lang-json
    PROPERTIES PROCESSORS ${FORT_LANG_JOBS})

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
    LABELS "fort;integration"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")

# The unit tests of the TextMate grammar in editors/vscode (label unit): they
# compare its keyword and operator lists with the language lists. They also assert the
# scopes it gives the fixtures in test/highlight.
add_test(NAME highlight_selftest
    COMMAND "${FORT_PYTHON3}" -m unittest highlight_test
    WORKING_DIRECTORY "${FORT_TEST_DIR}")
set_tests_properties(highlight_selftest PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# Validate ownership audit evidence with synthetic report fixtures.
add_test(NAME ownership_audit_selftest
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/ownership_audit_test.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(ownership_audit_selftest PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# Test the ownership qualification matrix tool with a fake compiler.
# It also validates the checked-in manifest. It runs no ownership verdict.
add_test(NAME ownership_matrix_selftest
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/ownership_matrix_test.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(ownership_matrix_selftest PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# Hold each panic site of src/fort, src/lsp and std against its panic tests (D11.4).
# A site with no test needs a declaration or a line in test/panic_gaps.txt.
add_test(NAME panic_coverage
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/panic_coverage.py"
            --root "${CMAKE_SOURCE_DIR}")
set_tests_properties(panic_coverage PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# Seed each class of problem of tools/panic_coverage.py in a small tree.
add_test(NAME panic_coverage_selftest
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/panic_coverage_test.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(panic_coverage_selftest PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# Hold each anchor of each table in tools/mutations to its source file. One test for each
# table runs `tools/mutate.py --check --strict`, which builds nothing. It fails when an anchor
# does not match its file once, or when the table has no comment. With --strict, a row whose
# replacement text is in the file is stale too, so a mutation round fails this test. A stage
# of a table must not run it: the stage of emitter_bootstrap.json that runs `ctest -L unit`
# excludes it.
file(GLOB FORT_MUTATION_TABLES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/mutations/*.json")
if(NOT FORT_MUTATION_TABLES)
    message(FATAL_ERROR "fort: tools/mutations holds no mutation table")
endif()
foreach(table IN LISTS FORT_MUTATION_TABLES)
    get_filename_component(table_name "${table}" NAME_WE)
    add_test(NAME mutation_anchors_${table_name}
        COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/mutate.py" "${table}" --check
                --strict --root "${CMAKE_SOURCE_DIR}")
    set_tests_properties(mutation_anchors_${table_name} PROPERTIES
        LABELS "fort;unit"
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endforeach()

# Hold both readings of a row whose anchor is gone and whose replacement is in the file.
# The fixture in test/mutate is not in tools/mutations, so the loop above skips it. Plain
# --check reads the row as applied and exits 0; --check --strict reads it as stale and exits 1.
# A missing fixture makes both exit 1, so the pair holds only when the row reads as applied.
set(FORT_MUTATE_FIXTURE_DIR "${FORT_TEST_DIR}/mutate")
add_test(NAME mutate_check_applied
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/mutate.py" strict_table.json --check
            --root "${FORT_MUTATE_FIXTURE_DIR}")
add_test(NAME mutate_check_strict_stale
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/mutate.py" strict_table.json --check
            --strict --root "${FORT_MUTATE_FIXTURE_DIR}")
add_test(NAME mutate_strict_needs_check
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/mutate.py" strict_table.json --strict
            --root "${FORT_MUTATE_FIXTURE_DIR}")
set_tests_properties(mutate_check_applied mutate_check_strict_stale mutate_strict_needs_check
    PROPERTIES
    LABELS "fort;unit"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
set_tests_properties(mutate_check_strict_stale PROPERTIES WILL_FAIL TRUE)
set_tests_properties(mutate_strict_needs_check PROPERTIES
    PASS_REGULAR_EXPRESSION "error: --strict needs --check")

# Test ownership selection and report publication through the built compiler.
add_test(NAME ownership_driver
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/ownership_driver_test.py"
            --fort "${FORT_COMPILER_BINARY}"
            --std-dir "${FORT_STD_DIR}"
            --cc "${FORT_TARGET_CC_PATH}"
            --opt "${FORT_OPT_PATH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(ownership_driver PROPERTIES
    LABELS "fort;integration"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")

# Test three selected driver groups: check against build, no C compiler, pre-transform FIR.
add_test(NAME ownership_driver_groups
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/ownership_driver_groups_test.py"
            --fort "${FORT_COMPILER_BINARY}"
            --std-dir "${FORT_STD_DIR}"
            --cc "${FORT_TARGET_CC_PATH}"
            --opt "${FORT_OPT_PATH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(ownership_driver_groups PROPERTIES
    LABELS "fort;integration"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")

# Fail the close of the ownership report file through a seccomp probe. Linux is its named host:
# Darwin has no system call filter that a test can install.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME ownership_report_close
        COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/ownership_report_close_test.py"
                --fort "${FORT_COMPILER_BINARY}"
                --std-dir "${FORT_STD_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # A close that no supervisor answers would wait without end. The two cases take about 2 s.
    set_tests_properties(ownership_report_close PROPERTIES
        LABELS "fort;integration"
        TIMEOUT 300
        SKIP_RETURN_CODE 77
        ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")
endif()

# Test the comparison of selected and unselected IR with a stand-in and the built compiler.
add_test(NAME ownership_ir_compare
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/ownership_ir_compare_test.py"
            --fort "${FORT_COMPILER_BINARY}"
            --std-dir "${FORT_STD_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(ownership_ir_compare PROPERTIES
    LABELS "fort;integration"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")

# Check the deepest nesting of D2.11 under a 1 MB stack. The test gives each
# compiler of the build the same type of 254 nested groups.
set(FORT_STACK_DEPTH_COMPILERS --fort "${FORT_COMPILER_BINARY}")
if(TARGET fort_bootstrap0)
    list(APPEND FORT_STACK_DEPTH_COMPILERS --fort "$<TARGET_FILE:fort_bootstrap0>")
endif()
add_test(NAME stack_depth
    COMMAND "${FORT_PYTHON3}" "${FORT_TEST_DIR}/stack_depth_test.py"
            ${FORT_STACK_DEPTH_COMPILERS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(stack_depth PROPERTIES
    LABELS "fort;integration"
    ENVIRONMENT "${FORT_SANITIZER_TEST_ENV};PYTHONDONTWRITEBYTECODE=1")

# Node's built-in runner tests the VS Code extension in editors/vscode.
# It converts diagnostic documents into editor shapes.
# It also converts 1-based byte columns to character offsets.
# A fake editor drives activation. The manual smoke test covers real editor behavior.
# Node is optional: this test is its only user, and a build without node
# registers no extension_selftest.
find_program(FORT_NODE NAMES node nodejs)
if(FORT_NODE)
    add_test(NAME extension_selftest
        COMMAND "${FORT_NODE}" --test
        WORKING_DIRECTORY "${FORT_EDITORS_DIR}/vscode")
    set_tests_properties(extension_selftest PROPERTIES LABELS "fort;unit")
else()
    message(STATUS "node not found: extension_selftest is not registered")
endif()

# ---- the gate --------------------------------------------------------------------
# `gate` is the merge gate of the selected target, run from this build directory:
# `check-all`, then the workflow (configure, build, test) of each sanitizer preset of
# the target: asan-debug and ubsan-debug, which both hosts provide. `tools/vm gate` runs it
# under the debug preset in the VM.
set(FORT_GATE_PRESETS asan-debug ubsan-debug)
set(FORT_GATE_COMMANDS "")
foreach(gate_preset IN LISTS FORT_GATE_PRESETS)
    list(APPEND FORT_GATE_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E echo "gate: workflow ${gate_preset}"
        COMMAND "${CMAKE_COMMAND}" --workflow --preset "${gate_preset}")
endforeach()
add_custom_target(gate
    COMMAND "${CMAKE_COMMAND}" -E echo "gate: check-all"
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target check-all
    ${FORT_GATE_COMMANDS}
    COMMAND "${CMAKE_COMMAND}" -E echo "gate: green"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running the merge gate of the ${FORT_TARGET} target"
    USES_TERMINAL
    VERBATIM
)


# ---- the development targets -------------------------------------------------------
# cmake/dev_targets.cmake names the steps and the helper functions. These are the
# targets of the fort component and the `-all` targets of both components;
# bootstrap0/CMakeLists.txt defines the `bootstrap0-` targets.
file(GLOB_RECURSE FORT_FORT_C_SOURCES CONFIGURE_DEPENDS
    "${FORT_TEST_DIR}/*.c" "${FORT_TEST_DIR}/*.h")
fort_format_targets("" ${FORT_FORT_C_SOURCES})

# lint: the D1.4 names and the line width of every fort source. fort_lint.py
# reads `fort --index`, so it needs the compiler, and it names --std-dir because
# the library does not stand beside that binary. The C helpers of the corpus build
# with -Werror (lang_ffi_helpers) and have no lint of their own; the harness lints
# the directives of the corpus before every run.
add_custom_target(lint
    COMMAND "${FORT_PYTHON3}" "${CMAKE_SOURCE_DIR}/tools/fort_lint.py"
            --fort "${FORT_COMPILER_BINARY}" --std-dir "${FORT_STD_DIR}" --target "${FORT_TARGET}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Linting the fort sources"
    VERBATIM)
add_dependencies(lint fort fort_std)

# unit and integration: each test carries one component label, `fort`, `lsp`
# or `bootstrap0`. The top-level ctest also sees the tests of bootstrap0/, so
# these targets select the labels `fort` and `lsp`. Two -L options select the
# tests that match both.
fort_ctest_target(unit "${CMAKE_BINARY_DIR}" -L "^(fort|lsp)$" -L "^unit$")
fort_ctest_target(integration "${CMAKE_BINARY_DIR}" -L "^(fort|lsp)$" -L "^integration$")
add_dependencies(unit fort fort_std fort_lsp)
add_dependencies(integration fort fort_std fort_lsp)
fort_sequence_target(check format-check lint unit integration)

# <step>-all: the step of bootstrap0, when it is part of the build, then of fort.
foreach(step format format-check lint unit integration check)
    if(TARGET bootstrap0-${step})
        fort_sequence_target(${step}-all bootstrap0-${step} ${step})
    else()
        fort_sequence_target(${step}-all ${step})
    endif()
endforeach()
