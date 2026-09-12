# Sanitizer selection for the native build (AGENTS.md, Build and test).
#
# FORT_SANITIZER names the sanitizer, or is empty. The flags are applied with
# add_compile_options/add_link_options so that every native target (the
# compiler and the unit tests) is instrumented; the program the compiler
# builds is cross-compiled outside CMake's compiler settings and is
# never affected. FORT_SANITIZER_TEST_ENV is the environment the unit tests
# run with under ctest: every sanitizer allocator returns null on an impossible
# request instead of reporting, because a suite that tests an out-of-memory
# path asks for an impossible allocation.

set(FORT_SANITIZER "" CACHE STRING
    "Sanitizer for the native build: address, memory, thread, undefined or empty")
set_property(CACHE FORT_SANITIZER PROPERTY STRINGS "" address memory thread undefined)

set(FORT_SANITIZER_TEST_ENV "")

if(FORT_SANITIZER STREQUAL "")
    # Plain build.
elseif(FORT_SANITIZER STREQUAL "address")
    add_compile_options(
        -fsanitize=address
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
        -O1
        -g
    )
    add_link_options(-fsanitize=address)
    set(FORT_SANITIZER_TEST_ENV
        "ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:allocator_may_return_null=1")
elseif(FORT_SANITIZER STREQUAL "memory")
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "FORT_SANITIZER=memory needs clang; the C compiler is ${CMAKE_C_COMPILER_ID}")
    endif()
    add_compile_options(
        -fsanitize=memory
        -fsanitize-memory-track-origins=2
        -fno-omit-frame-pointer
        -O1
        -g
    )
    add_link_options(-fsanitize=memory)
    set(FORT_SANITIZER_TEST_ENV "MSAN_OPTIONS=allocator_may_return_null=1")
elseif(FORT_SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -O1 -g)
    add_link_options(-fsanitize=thread)
    set(FORT_SANITIZER_TEST_ENV "TSAN_OPTIONS=allocator_may_return_null=1")
elseif(FORT_SANITIZER STREQUAL "undefined")
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all -g)
    add_link_options(-fsanitize=undefined)
    set(FORT_SANITIZER_TEST_ENV "UBSAN_OPTIONS=print_stacktrace=1")
else()
    message(FATAL_ERROR "FORT_SANITIZER: unknown sanitizer: ${FORT_SANITIZER}")
endif()

if(NOT FORT_SANITIZER STREQUAL "")
    message(STATUS "Sanitizer: ${FORT_SANITIZER}")
endif()
