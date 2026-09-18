# Extract and assemble one pinned compiler tree for the detected host.

foreach(required ROOT SHA OUTPUT TARGET)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "bootstrap tree needs ${required}")
    endif()
endforeach()

set(archive "${OUTPUT}.tar")
file(REMOVE_RECURSE "${OUTPUT}")
file(REMOVE "${archive}")
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(
    COMMAND git archive --format=tar "${SHA}" src/fort std tools/assemble_std.sh
    WORKING_DIRECTORY "${ROOT}"
    OUTPUT_FILE "${archive}"
    RESULT_VARIABLE archive_status
)
if(NOT archive_status EQUAL 0)
    message(FATAL_ERROR "git archive failed for ${SHA}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${archive}"
    WORKING_DIRECTORY "${OUTPUT}"
    RESULT_VARIABLE extract_status
)
file(REMOVE "${archive}")
if(NOT extract_status EQUAL 0)
    message(FATAL_ERROR "archive extraction failed for ${SHA}")
endif()

# The pin's own script writes its standard root for the target.
set(target_dir "${OUTPUT}/target")
execute_process(
    COMMAND bash "${OUTPUT}/tools/assemble_std.sh" "${TARGET}" "${OUTPUT}/std" "${target_dir}/std"
    RESULT_VARIABLE std_status
)
if(NOT std_status EQUAL 0)
    message(FATAL_ERROR "the standard root of ${SHA} failed for ${TARGET}")
endif()
# The bridge imports std.rt_float, while HEAD provides the functions in std.rt.
# Keep the compatibility module available while the bridge builds both modules.
if(NOT EXISTS "${target_dir}/std/rt_float.ft")
    file(WRITE "${target_dir}/std/rt_float.ft"
         "// Compatibility module for the C-built bootstrap bridge.\n")
endif()

file(MAKE_DIRECTORY "${target_dir}/entry")
file(COPY_FILE "${OUTPUT}/src/fort/main.ft" "${target_dir}/entry/main.ft")
file(WRITE "${OUTPUT}/.assembled-${SHA}-${TARGET}" "${SHA}\n")
