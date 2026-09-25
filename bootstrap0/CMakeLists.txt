# The C bootstrap compiler (bootstrap-0) and its tests. The top-level project adds
# this directory only when FORT_ENABLE_BOOTSTRAP0 is ON, and this directory
# never configures alone. The parent gives these variables:
# - the target: FORT_TARGET, FORT_TARGET_TRIPLE and FORT_DEFAULT_TARGET_CC.
# - the tools: FORT_TARGET_CC_PATH, FORT_OPT_PATH and FORT_PYTHON3.
# - the tests: FORT_TEST_DIR and FORT_SANITIZER_TEST_ENV.
#
# The binary directory of this file is build/<preset>/bootstrap, so the
# compiler is bootstrap/fort. The parent builds its chain of source compilers
# from fort_bootstrap0. The e2e test below does not use that chain.

include(CMakeDependentOption)
option(FORT_BOOTSTRAP0_BUILD_TESTS "Build and register the tests of the C compiler" OFF)
cmake_dependent_option(FORT_BOOTSTRAP0_E2E_TEST
    "Test the C compiler: it builds the bootstrap-1 compiler" ON
    "FORT_BOOTSTRAP0_BUILD_TESTS" OFF)

set(FORT_BOOTSTRAP0_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(FORT_BOOTSTRAP0_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test")

# ---- toolchain -----------------------------------------------------------------
# Use C11 without extensions. Make POSIX.1-2008 visible. These settings repeat
# the settings of the top-level file, so that the C compiler does not depend on
# them. FORT_SANITIZER comes from the parent.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The warnings every C compilation of this directory gets.
add_compile_options(
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
if(APPLE)
    # Darwin hides mkdtemp under its source feature macro in C11 mode.
    add_compile_definitions(_DARWIN_C_SOURCE)
endif()

# ---- the C compiler ------------------------------------------------------------
file(GLOB FORT_BOOTSTRAP0_CORE_LIST CONFIGURE_DEPENDS "${FORT_BOOTSTRAP0_SRC_DIR}/*.c")
list(REMOVE_ITEM FORT_BOOTSTRAP0_CORE_LIST "${FORT_BOOTSTRAP0_SRC_DIR}/main.c")

add_library(fort_core STATIC ${FORT_BOOTSTRAP0_CORE_LIST})
target_include_directories(fort_core PUBLIC "${FORT_BOOTSTRAP0_SRC_DIR}")
target_compile_definitions(fort_core PUBLIC
    FORT_DEFAULT_CC="${FORT_DEFAULT_TARGET_CC}"
    FORT_DEFAULT_TARGET="${FORT_TARGET_TRIPLE}"
)

add_executable(fort_bootstrap0 "${FORT_BOOTSTRAP0_SRC_DIR}/main.c")
target_link_libraries(fort_bootstrap0 PRIVATE fort_core)
set_target_properties(fort_bootstrap0 PROPERTIES
    OUTPUT_NAME fort
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

if(FORT_BOOTSTRAP0_BUILD_TESTS)
    # unit tests
    file(GLOB FORT_BOOTSTRAP0_TEST_LIST CONFIGURE_DEPENDS "${FORT_BOOTSTRAP0_TEST_DIR}/*_test.c")
    set(FORT_UNIT_TEST_TARGETS "")
    foreach(TEST_SRC_FILE IN LISTS FORT_BOOTSTRAP0_TEST_LIST)
        get_filename_component(TEST_TARGET "${TEST_SRC_FILE}" NAME_WE)
        add_executable("${TEST_TARGET}" "${TEST_SRC_FILE}")
        target_include_directories("${TEST_TARGET}" PRIVATE "${FORT_BOOTSTRAP0_TEST_DIR}")
        target_link_libraries("${TEST_TARGET}" PRIVATE fort_core)
        set_target_properties("${TEST_TARGET}" PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")
        add_test(NAME "${TEST_TARGET}" COMMAND "${TEST_TARGET}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
        set_tests_properties("${TEST_TARGET}" PROPERTIES
            LABELS "unit;bootstrap"
            ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")
        list(APPEND FORT_UNIT_TEST_TARGETS "${TEST_TARGET}")
    endforeach()

    # ---- the bootstrap-1 source ----------------------------------------------------
    # The contract suites and the e2e test read bootstrap-1 from a tree of their
    # own. The parent's chain extracts its pins from tools/bootstrap.ref.
    set(FORT_BOOTSTRAP1_SHA 0fe521c08a4541ec8f1a9563bc177630cb59316e)

    set(FORT_BOOTSTRAP1_DIR "${CMAKE_CURRENT_BINARY_DIR}/e2e/bootstrap1")
    set(FORT_BOOTSTRAP1_STAMP
        "${FORT_BOOTSTRAP1_DIR}/.assembled-${FORT_BOOTSTRAP1_SHA}-${FORT_TARGET}")
    add_custom_command(
        OUTPUT "${FORT_BOOTSTRAP1_STAMP}"
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/extract_pin.sh"
            "${FORT_BOOTSTRAP1_SHA}" "${FORT_BOOTSTRAP1_DIR}" "${FORT_TARGET}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/extract_pin.sh"
        COMMENT "Extracting bootstrap-1 for the tests of the C compiler"
        VERBATIM
    )
    add_custom_target(fort_bootstrap1_source ALL DEPENDS "${FORT_BOOTSTRAP1_STAMP}")

    # ---- the e2e test ---------------------------------------------------------------
    # The end-to-end test of the C compiler: bootstrap-0 builds the bootstrap-1 compiler
    # from the bootstrap-1 source. The test passes when that build exits 0.
    if(FORT_BOOTSTRAP0_E2E_TEST)
        add_test(NAME bootstrap-e2e
            COMMAND "$<TARGET_FILE:fort_bootstrap0>" ${FORT_STAGE_MODE_ARGS}
                --std-dir "${FORT_BOOTSTRAP1_DIR}/std"
                --cc "${FORT_TARGET_CC_PATH}"
                -o "${FORT_BOOTSTRAP1_DIR}/fort" "${FORT_BOOTSTRAP1_DIR}/src/fort/main.ft"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
        set_tests_properties(bootstrap-e2e PROPERTIES
            LABELS "unit;bootstrap"
            ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")
    endif()

    # The parent's `check` target depends on this target, so it builds each suite.
    add_custom_target(fort_bootstrap0_unit_tests)
    if(FORT_UNIT_TEST_TARGETS)
        add_dependencies(fort_bootstrap0_unit_tests ${FORT_UNIT_TEST_TARGETS})
    endif()

    # The driver tests spawn bootstrap0/test/common/fake_cc.sh as their --cc: it
    # records the command line that the driver built. The check suite asserts that
    # `--check` does not start it. The conformance suite makes the same assertion
    # for runs that stop before it.
    foreach(driver_suite driver_test driver_check_test driver_conformance_test driver_tokens_test
                        driver_ast_test)
        if(TARGET "${driver_suite}")
            target_compile_definitions("${driver_suite}"
                PRIVATE FORT_FAKE_CC="${FORT_BOOTSTRAP0_TEST_DIR}/common/fake_cc.sh")
        endif()
    endforeach()

    # The emitter suites run the LLVM verifier over each emitted module.
    # Each suite needs the verifier path. Every bootstrap0/test/gen*_test.c gets it.
    # Adding a file covers a new suite. `opt` is a default for these tests.
    # The pipeline test and language harness require it during configuration.
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
    # Both suites parse the standard root of the bootstrap-1 source above. This makes them
    # contracts between the C implementation and its bootstrap library.
    foreach(std_reader runtime_sig_test check_conv_test)
        if(TARGET "${std_reader}")
            target_compile_definitions("${std_reader}"
                PRIVATE FORT_STD_SOURCE_DIR="${FORT_BOOTSTRAP1_DIR}/std")
            add_dependencies("${std_reader}" fort_bootstrap1_source)
            set_tests_properties("${std_reader}" PROPERTIES
                LABELS "unit;bootstrap;bootstrap-contract")
        endif()
    endforeach()

    # The parser recovery suite walks the fail corpus and asserts that
    # every diagnostic lands on an annotated line, so it needs the corpus's path.
    if(TARGET parser_recovery_test)
        target_compile_definitions(parser_recovery_test
            PRIVATE FORT_LANG_DIR="${FORT_TEST_DIR}/lang")
    endif()

    # The tools/mutate.py unit tests cover parsers, anchors, guards, restore, and timeouts.
    # They also check the shipped table against its source anchors.
    # The unit tests run no mutation round because a round rebuilds the compiler.
    add_test(NAME mutate_selftest
        COMMAND "${FORT_PYTHON3}" -m unittest mutate_test
        WORKING_DIRECTORY "${FORT_TEST_DIR}")
    set_tests_properties(mutate_selftest PROPERTIES
        LABELS unit
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()
