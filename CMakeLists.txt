cmake_minimum_required(VERSION 3.20)
project(ds LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-config generators (Makefiles, Ninja) default to no optimisation and
# no NDEBUG unless a build type is picked.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DS_NATIVE_ARCH  "Tune the build for the host CPU (not portable binaries)" OFF)
option(DS_WERROR       "Treat warnings as errors" OFF)
option(DS_BUILD_TESTS  "Build the test suite" ON)
option(DS_BUILD_BENCH  "Build the benchmark executables" ON)

# --------------------------------------------------------------------
# Per-compiler flags
# --------------------------------------------------------------------
#
# The previous version called add_compile_options(-O3 -march=native)
# unconditionally. Those are GCC/Clang spellings: MSVC rejects them (they are
# read as source file names after /-flag parsing), which is the first reason
# the Windows build failed. -march=native additionally emits instructions the
# build machine has but the target machine may not, so it is now opt-in.

function(ds_set_target_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /wd4324          # structure padded due to alignment specifier -- intentional
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:RelWithDebInfo>:/O2>
        )
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
        )
        if(DS_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:RelWithDebInfo>:-O2>
        )
        if(DS_NATIVE_ARCH)
            include(CheckCCompilerFlag)
            check_c_compiler_flag("-march=native" DS_HAS_MARCH_NATIVE)
            if(DS_HAS_MARCH_NATIVE)
                target_compile_options(${target} PRIVATE -march=native)
            endif()
        endif()
        if(DS_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(memory)

# --------------------------------------------------------------------
# Core library -- every module now compiles, so every module is built
# --------------------------------------------------------------------
set(source
    ds_dyn_array.c      ds_dyn_array.h
    ds_string.c         ds_string.h
    ds_graph.c          ds_graph.h
    ds_hash_map.c       ds_hash_map.h
    ds_linked_list.c    ds_linked_list.h
    ds_stack_queue.c    ds_stack_queue.h
    ds_priority_queue.c ds_priority_queue.h
)

add_library(${PROJECT_NAME} STATIC ${source})
ds_set_target_flags(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(${PROJECT_NAME} PUBLIC ds_arena)

# On Unix, linking libm is needed for the floating point helpers in some
# libc builds; it does not exist (and is not needed) on Windows.
if(UNIX)
    target_link_libraries(${PROJECT_NAME} PUBLIC m)
endif()

# --------------------------------------------------------------------
# Executables
# --------------------------------------------------------------------
function(ds_add_program name)
    add_executable(${name} ${ARGN})
    ds_set_target_flags(${name})
    target_link_libraries(${name} PRIVATE ${PROJECT_NAME})
endfunction()

# One test executable and one benchmark executable per data structure, so a
# failure names the structure it came from and any benchmark can be run on
# its own.
set(DS_TEST_SUITES
    common arena gc dyn_array string
    linked_list stack_queue priority_queue hash_map graph)

set(DS_BENCH_SUITES
    arena gc dyn_array string
    linked_list stack_queue priority_queue hash_map graph)

if(DS_BUILD_TESTS)
    enable_testing()
    foreach(suite IN LISTS DS_TEST_SUITES)
        ds_add_program(test_${suite} tests/test_${suite}.c)
        target_include_directories(test_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
endif()

if(DS_BUILD_BENCH)
    set(DS_BENCH_TARGETS "")
    foreach(suite IN LISTS DS_BENCH_SUITES)
        ds_add_program(bench_${suite} tests/bench_${suite}.c)
        target_include_directories(bench_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        list(APPEND DS_BENCH_TARGETS bench_${suite})
    endforeach()

    ds_add_program(main main.c)

    # `cmake --build build --target bench` builds and runs the whole set.
    add_custom_target(bench DEPENDS ${DS_BENCH_TARGETS})
    foreach(t IN LISTS DS_BENCH_TARGETS)
        add_custom_command(TARGET bench POST_BUILD COMMAND $<TARGET_FILE:${t}>)
    endforeach()
endif()

# --------------------------------------------------------------------
# Install
# --------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} ds_arena
        ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION include
        FILES_MATCHING PATTERN "*.h"
        PATTERN "build" EXCLUDE
        PATTERN "tests" EXCLUDE
        PATTERN "memory" EXCLUDE
        PATTERN ".git" EXCLUDE)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/memory/include/ DESTINATION include
        FILES_MATCHING PATTERN "*.h")
