cmake_minimum_required(VERSION 3.20)
project(axle LANGUAGES CXX ASM)

set(CMAKE_CXX_STANDARD 20)

enable_testing()


# Directories
set(AXLE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(AXLE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(AXLE_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
set(AXLE_BENCH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bench)
set(AXLE_EXAMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/examples)

set(AXLE_THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(AXLE_GOOGLETEST_DIR ${AXLE_THIRD_PARTY_DIR}/googletest)
set(AXLE_GOOGLEBENCHMARK_DIR ${AXLE_THIRD_PARTY_DIR}/benchmark)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

option(AXLE_ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(AXLE_ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(AXLE_ENABLE_TSAN "Enable Thread Sanitizer" OFF)

if(AXLE_ENABLE_ASAN)
    add_compile_options(
        -fsanitize=address
        -fno-omit-frame-pointer
        -g
    )
    add_link_options(-fsanitize=address)
    message("ASan enabled")
elseif(AXLE_ENABLE_UBSAN)
    add_compile_options(
        -fsanitize=undefined
        -g
    )
    add_link_options(-fsanitize=undefined)
    message("UBSan enabled")
elseif(AXLE_ENABLE_TSAN)
    add_compile_options(
        -fsanitize=thread
        -g
    )
    add_link_options(-fsanitize=thread)
    message("TSan enabled")
else()
    message("No sanitizers enabled")
endif()

if(AXLE_ENABLE_DEBUG)
    add_compile_definitions(AXLE_DEBUG_ENABLED)
endif()

# Dependencies
add_subdirectory(${AXLE_GOOGLETEST_DIR} EXCLUDE_FROM_ALL)
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(${AXLE_GOOGLEBENCHMARK_DIR} EXCLUDE_FROM_ALL)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project version
file(READ VERSION AXLE_PROJECT_VERSION)
string(STRIP ${AXLE_PROJECT_VERSION} AXLE_PROJECT_VERSION)

# and commit
execute_process(COMMAND git describe --always --dirty
    OUTPUT_VARIABLE AXLE_GIT_COMMIT_HASH
    ERROR_VARIABLE AXLE_GIT_COMMIT_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE AXLE_GIT_COMMIT_RESULT
)
if (NOT ${AXLE_GIT_COMMIT_RESULT} STREQUAL 0)
    message(WARNING "Could not retrieve git commit: " ${AXLE_GIT_COMMIT_ERROR})
    set(AXLE_GIT_COMMIT_HASH unknown)
endif()

message("axle version information: " ${AXLE_PROJECT_VERSION} " - " ${AXLE_GIT_COMMIT_HASH})
configure_file(${AXLE_SRC_DIR}/version.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/gen/version.cpp @ONLY)

# Source files
set(AXLE_SRC_LIST
    ${AXLE_SRC_DIR}/async_socket.cpp
    ${AXLE_SRC_DIR}/axle.cpp
    ${AXLE_SRC_DIR}/event.cpp
    ${AXLE_SRC_DIR}/fiber.cpp
    ${AXLE_SRC_DIR}/gate.cpp
    ${AXLE_SRC_DIR}/scheduler.cpp
    ${AXLE_SRC_DIR}/socket.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/gen/version.cpp
)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
    message("axle: target processor is x64")
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/asm/fiber_x64.S)
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/fiber_x64.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message("axle: target processor is arm64")
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/asm/fiber_arm64.S)
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/fiber_arm64.cpp)
else()
    message(FATAL_ERROR "Unknown target: " ${CMAKE_SYSTEM_PROCESSOR})
endif()

if (CMAKE_SYSTEM_NAME MATCHES "FreeBSD|Darwin")
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/poller_bsd.cpp)
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/poller_linux.cpp)
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/poller_iouring.cpp)
    list(APPEND AXLE_SRC_LIST ${AXLE_SRC_DIR}/iouring_socket.cpp)
endif ()

# Test files
set(AXLE_TEST_LIST
    ${AXLE_TEST_DIR}/async_socket_test.cpp
    ${AXLE_TEST_DIR}/axle_test.cpp
    ${AXLE_TEST_DIR}/event_test.cpp
    ${AXLE_TEST_DIR}/fiber_test.cpp
    ${AXLE_TEST_DIR}/gate_test.cpp
    ${AXLE_TEST_DIR}/poller_iouring_test.cpp
    ${AXLE_TEST_DIR}/scheduler_test.cpp
    ${AXLE_TEST_DIR}/status_test.cpp
)

# Benchmark files
set(AXLE_BENCH_LIST
    ${AXLE_BENCH_DIR}/async_echo_server.cpp
)

# Main library
add_library(axle ${AXLE_SRC_LIST})
target_include_directories(axle PUBLIC ${AXLE_INCLUDE_DIR} PRIVATE ${AXLE_SRC_DIR})
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(axle PUBLIC "uring")
endif()
if (AXLE_ENABLE_ASAN)
    target_compile_definitions(axle PUBLIC "AXLE_ASAN_ENABLED=1")
endif()

if (AXLE_ENABLE_TSAN)
    target_compile_definitions(axle PUBLIC "AXLE_TSAN_ENABLED=1")
endif()

# Unit tests
add_executable(axle_test ${AXLE_TEST_LIST})
target_include_directories(axle_test PRIVATE ${AXLE_SRC_DIR} ${AXLE_TEST_DIR})
target_link_libraries(axle_test axle gtest_main)

add_test(unit-tests axle_test)
if(AXLE_ENABLE_ASAN)
    if(APPLE)
        set(AXLE_ASAN_OPTIONS detect_leaks=1)
    endif()
    set(AXLE_LSAN_SUPP_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/sanitizers/asan/asan_leak_supp_darwin_arm64.txt
    )
    set(AXLE_LSAN_OPTIONS suppressions=${AXLE_LSAN_SUPP_FILE})
    set_tests_properties(unit-tests PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=${AXLE_ASAN_OPTIONS};LSAN_OPTIONS=${AXLE_LSAN_OPTIONS}"
    )
endif()

# Benchmarks
add_executable(axle_bench ${AXLE_BENCH_LIST})
target_link_libraries(axle_bench axle benchmark_main)


# Examples
add_executable(echo_server ${AXLE_EXAMPLES_DIR}/echo_server/main.cpp)
target_link_libraries(echo_server axle)

add_executable(async_echo_server ${AXLE_EXAMPLES_DIR}/async_echo_server/main.cpp)
target_link_libraries(async_echo_server axle)

file(GLOB_RECURSE HDR_FILES "${AXLE_SRC_DIR}/*.h" "${AXLE_INCLUDE_DIR}/*.h")
add_custom_target(lint
  # COMMAND /usr/local/bin/clang-tidy -p "${CMAKE_BINARY_DIR}" --checks="-*,misc-include-cleaner" ${HDR_FILES}
  COMMAND /usr/local/bin/run-clang-tidy -q -allow-no-checks -p=${CMAKE_BINARY_DIR}
  COMMENT "Running clang-tidy..."
)
