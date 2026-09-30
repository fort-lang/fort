# The C bootstrap compiler (bootstrap-0) and its tests. The top-level project adds
# this directory only when FORT_ENABLE_BOOTSTRAP0 is ON, and this directory
# never configures alone. The parent gives these variables:
# - the target: FORT_TARGET, FORT_TARGET_TRIPLE and FORT_DEFAULT_TARGET_CC.
# - the tools: FORT_TARGET_CC_PATH, FORT_OPT_PATH and FORT_PYTHON3.
# - the tests: FORT_TEST_DIR and FORT_SANITIZER_TEST_ENV.
#
# The binary directory of this file is build/<Host>/<preset>/bootstrap, so the
# compiler is bootstrap/fort. The parent builds its chain of source compilers
# from fort_bootstrap0 (tools/bootstrap.ref).

include(CMakeDependentOption)
option(FORT_BOOTSTRAP0_BUILD_TESTS "Build and register the tests of the C compiler" OFF)

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
            LABELS "bootstrap0;unit"
            ENVIRONMENT "${FORT_SANITIZER_TEST_ENV}")
        list(APPEND FORT_UNIT_TEST_TARGETS "${TEST_TARGET}")
    endforeach()

    # The parent's `bootstrap0-unit` target depends on this target, so it builds each suite.
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
    # Both suites parse the standard root of bootstrap-1, the first pin of the
    # parent's chain. This makes them
    # contracts between the C implementation and its bootstrap library.
    foreach(std_reader runtime_sig_test check_conv_test)
        if(TARGET "${std_reader}")
            target_compile_definitions("${std_reader}"
                PRIVATE FORT_STD_SOURCE_DIR="${FORT_BOOTSTRAP_DIR}/bootstrap-1/std")
            add_dependencies("${std_reader}" fort_bootstrap_1_source)
            set_tests_properties("${std_reader}" PROPERTIES
                LABELS "bootstrap0;unit;bootstrap-contract")
        endif()
    endforeach()

    # The parser recovery suite walks the fail corpus and asserts that
    # every diagnostic lands on an annotated line, so it needs the corpus's path.
    if(TARGET parser_recovery_test)
        target_compile_definitions(parser_recovery_test
            PRIVATE FORT_LANG_DIR="${FORT_TEST_DIR}/lang")
    endif()

endif()

# ---- the development targets ----------------------------------------------------
# The bootstrap0 steps of cmake/dev_targets.cmake. ctest runs over this binary
# directory, so it sees the tests of bootstrap0 and nothing else.
file(GLOB_RECURSE FORT_BOOTSTRAP0_C_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
fort_format_targets("bootstrap0-" ${FORT_BOOTSTRAP0_C_SOURCES})
fort_tidy_target(bootstrap0-lint "${CMAKE_CURRENT_SOURCE_DIR}")
fort_ctest_target(bootstrap0-unit "${CMAKE_CURRENT_BINARY_DIR}" -L "^bootstrap0$" -L "^unit$")
if(TARGET fort_bootstrap0_unit_tests)
    add_dependencies(bootstrap0-unit fort_bootstrap0_unit_tests)
endif()
fort_sequence_target(bootstrap0-check bootstrap0-format-check bootstrap0-lint bootstrap0-unit)
