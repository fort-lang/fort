# Select the sanitizer for the native build.
#
# FORT_SANITIZER names the sanitizer, or it is empty. Its flags instrument each
# native target. They do not affect cross-compiled programs.
# FORT_SANITIZER_TEST_ENV sets the ctest environment. Each sanitizer allocator
# returns null for an impossible request. This permits out-of-memory tests.

set(FORT_SANITIZER "" CACHE STRING
    "Sanitizer for the native build: address, memory, thread, undefined or empty")
set_property(CACHE FORT_SANITIZER PROPERTY STRINGS "" address memory thread undefined)

set(FORT_SANITIZER_TEST_ENV "")

# The presets are the same on every host. MemorySanitizer and ThreadSanitizer do
# not exist for arm64 darwin, so the msan and tsan presets stop here, at configure
# time, and not at the first link.
if(APPLE AND FORT_SANITIZER MATCHES "^(memory|thread)$")
    message(FATAL_ERROR
        "FORT_SANITIZER=${FORT_SANITIZER} is not available on darwin; use asan or ubsan")
endif()

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
    if(APPLE)
        # LeakSanitizer does not exist on arm64 darwin: the runtime aborts with
        # "detect_leaks is not supported on this platform". The Homebrew clang
        # AddressSanitizer runtime hangs at exit on darwin 25; CMakeLists.txt
        # selects Apple clang for an address build on darwin.
        set(FORT_SANITIZER_TEST_ENV
            "ASAN_OPTIONS=detect_stack_use_after_return=1:allocator_may_return_null=1")
    else()
        set(FORT_SANITIZER_TEST_ENV
            "ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:allocator_may_return_null=1")
    endif()
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
