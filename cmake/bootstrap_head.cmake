# Assemble the standard root that the predecessor uses to build HEAD.

foreach(required SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "bootstrap HEAD root needs ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY "${SOURCE}/" DESTINATION "${OUTPUT}")

# Some predecessors require rt_float.ft. HEAD defines its functions in std.rt.
# Create an empty compatibility module only while a predecessor builds HEAD.
file(WRITE "${OUTPUT}/rt_float.ft" "")
file(WRITE "${OUTPUT}/.assembled" "HEAD\n")
