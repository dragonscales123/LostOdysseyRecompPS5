# Refresh at build time, so a commit or dirty checkout is not mistaken for the
# revision from an old configure. Only changed content recompiles main.cpp.
execute_process(COMMAND git -C "${SOURCE_ROOT}" describe --always --dirty --abbrev=12 "--exclude=*"
    OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT revision MATCHES "^[0-9a-f]+(-dirty)?$")
    set(revision "unavailable")
endif()
set(contents "// Generated at build time. Do not edit.\n#define LO_BUILD_REVISION \"${revision}\"\n")
# PS5 port: the build number shown on the performance overlay (ps5/build-number.txt).
if(EXISTS "${SOURCE_ROOT}/ps5/build-number.txt")
    file(STRINGS "${SOURCE_ROOT}/ps5/build-number.txt" build_number LIMIT_COUNT 1 REGEX "^[0-9]+$")
    if(build_number)
        string(APPEND contents "#define LO_PS5_BUILD \"${build_number}\"\n")
    endif()
endif()
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
    if(previous STREQUAL contents)
        return()
    endif()
endif()
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${OUTPUT}" "${contents}")
