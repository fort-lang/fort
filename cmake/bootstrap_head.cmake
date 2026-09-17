# Assemble the standard root that the predecessor uses to build HEAD.

foreach(required SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "bootstrap HEAD root needs ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY "${SOURCE}/" DESTINATION "${OUTPUT}")

# Bridge commit B asks for this module when it sees a float. HEAD defines all
# float runtime functions in std.rt. The empty compatibility module exists only
# while B builds HEAD. The published standard root does not contain this file.
file(WRITE "${OUTPUT}/rt_float.ft" "// T-155 bootstrap compatibility module.\n")
file(WRITE "${OUTPUT}/.assembled" "HEAD\n")
