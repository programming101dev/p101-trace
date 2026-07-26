# Standalone libFuzzer tree. clang-only (needs the fuzzer runtime) -- fuzz.sh
# selects a fuzzer-capable clang and passes it as CMAKE_C_COMPILER. Separate
# from the strict analysis build.
#
# TARGET: p101-trace's argument parser in src/cli.c. p101_exit() is redirected
# into a longjmp so usage paths are normal fuzz inputs, not process exits.
cmake_minimum_required(VERSION 3.14)
project(p101_fuzz C)

if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Fuzzing needs clang with libFuzzer (-fsanitize=fuzzer); got '${CMAKE_C_COMPILER_ID}'. Use ./fuzz.sh, which picks a fuzzer-capable clang.")
endif ()

set(P101_LIBS p101_error p101_env p101_c p101_posix)
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
        message(WARNING "p101 library '${_l}' not found in ${_P101_LIB_DIRS} -- install the p101 libs (setup.sh) or add the path.")
    endif ()
endforeach ()

# -fsanitize=fuzzer pulls in libFuzzer (and its own main); +address,undefined so
# the fuzzer's inputs are checked for memory errors and UB.
set(_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -g -O1)

add_executable(fuzz fuzz_parse.c "${CMAKE_CURRENT_SOURCE_DIR}/../src/cli.c")

# Redirect p101_exit() (usage() is _Noreturn) so the -h path is a longjmp, not
# a fake crash.
target_compile_definitions(fuzz PRIVATE p101_exit=p101_fuzz_exit P101_SUPPRESS_USAGE_TEXT)

target_include_directories(fuzz PRIVATE ${_P101_INC_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_compile_options(fuzz PRIVATE ${_FUZZ_FLAGS})
target_link_options(fuzz PRIVATE ${_FUZZ_FLAGS})
target_link_libraries(fuzz PRIVATE ${_P101_RESOLVED} m)
