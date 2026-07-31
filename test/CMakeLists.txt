# Standalone Unity test tree for a p101-linked C program. Kept separate from the
# project's strict analysis build. Links the same p101 libraries the main target
# links (see ../config.cmake main_LINK_LIBRARIES), resolved from the usual
# install prefixes; add to _P101_LIB_DIRS/_P101_INC_DIRS if yours differ.
cmake_minimum_required(VERSION 3.14)
project(p101_tests C)
enable_testing()

add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/unity")
target_compile_options(unity PRIVATE -w)

option(P101_TEST_COVERAGE "Instrument the code under test" OFF)
set(_COV "")
if (P101_TEST_COVERAGE)
    set(_COV --coverage)
endif ()

set(P101_LIBS p101_error p101_tool_event p101_env p101_c p101_cli p101_filesystem p101_io p101_process p101_text)
set(P101_PUBLIC_INCLUDE_DIRS "" CACHE STRING "Extra p101 include dirs")
set(P101_PUBLIC_LINK_DIRS "" CACHE STRING "Extra p101 link dirs")
separate_arguments(P101_PUBLIC_INCLUDE_DIRS_LIST NATIVE_COMMAND "${P101_PUBLIC_INCLUDE_DIRS}")
separate_arguments(P101_PUBLIC_LINK_DIRS_LIST NATIVE_COMMAND "${P101_PUBLIC_LINK_DIRS}")
set(_P101_INC_DIRS ${P101_PUBLIC_INCLUDE_DIRS_LIST} /usr/local/include /opt/homebrew/include /opt/local/include)
set(_P101_LIB_DIRS ${P101_PUBLIC_LINK_DIRS_LIST} /usr/local/lib /usr/local/lib64 /opt/homebrew/lib /opt/local/lib)

set(_P101_RESOLVED "")
foreach (_l IN LISTS P101_LIBS)
    unset(_P101_LIB_${_l} CACHE)
    unset(_P101_LIB_${_l})
    find_library(_P101_LIB_${_l} NAMES ${_l} PATHS ${_P101_LIB_DIRS} NO_DEFAULT_PATH)
    if (NOT _P101_LIB_${_l})
        find_library(_P101_LIB_${_l} NAMES ${_l})
    endif ()
    if (_P101_LIB_${_l})
        list(APPEND _P101_RESOLVED "${_P101_LIB_${_l}}")
    else ()
        message(WARNING "p101 library '${_l}' not found in ${_P101_LIB_DIRS} — install the p101 libs (setup.sh) or add the path.")
    endif ()
endforeach ()

# p101_add_test(<name> <sources...>) — pass the test source plus any of the
# project's own sources it exercises (NOT main.c, which defines main()).
function(p101_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${_P101_INC_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/../include")
    target_link_libraries(${name} PRIVATE unity ${_P101_RESOLVED} m)
    target_compile_options(${name} PRIVATE ${_COV})
    target_link_options(${name} PRIVATE ${_COV})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

p101_add_test(test_arguments test_arguments.c ../src/cli.c ../src/model.c ../src/model_identity.c ../src/model_lifecycle.c ../src/parse.c ../src/report.c ../src/runner.c)
target_compile_definitions(test_arguments PRIVATE P101_TRACE_TESTING=1)

add_executable(p101_trace_coverage_cli
    ../src/main.c
    ../src/cli.c
    ../src/model.c
    ../src/model_identity.c
    ../src/model_lifecycle.c
    ../src/parse.c
    ../src/report.c
    ../src/runner.c)
target_include_directories(p101_trace_coverage_cli PRIVATE ${_P101_INC_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(p101_trace_coverage_cli PRIVATE ${_P101_RESOLVED} m)
target_compile_options(p101_trace_coverage_cli PRIVATE ${_COV})
target_link_options(p101_trace_coverage_cli PRIVATE ${_COV})
add_test(NAME test_cli COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/test_cli.sh" "$<TARGET_FILE:p101_trace_coverage_cli>")
