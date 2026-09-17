# Extract one validated commit and write its standard root for the target.

foreach(required ROOT SHA OUTPUT TARGET)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "tree extraction needs ${required}")
    endif()
endforeach()
set(archive "${OUTPUT}.tar")
set(tree "${OUTPUT}/tree")
file(REMOVE_RECURSE "${OUTPUT}")
file(REMOVE "${archive}")
file(MAKE_DIRECTORY "${tree}")
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
    WORKING_DIRECTORY "${tree}"
    RESULT_VARIABLE extract_status
)
file(REMOVE "${archive}")
if(NOT extract_status EQUAL 0)
    message(FATAL_ERROR "archive extraction failed for ${SHA}")
endif()
execute_process(
    COMMAND bash "${tree}/tools/assemble_std.sh" "${TARGET}" "${tree}/std" "${OUTPUT}/std"
    RESULT_VARIABLE std_status
)
if(NOT std_status EQUAL 0)
    message(FATAL_ERROR "the standard root of ${SHA} failed for ${TARGET}")
endif()
file(WRITE "${OUTPUT}/.tree-${SHA}" "${SHA}\n")
