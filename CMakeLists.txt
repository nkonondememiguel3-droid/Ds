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
option(DS_BUILD_FUZZ   "Build the fuzz harnesses" ON)
option(DS_LIBFUZZER    "Drive the fuzzers with clang libFuzzer instead of the built-in driver" OFF)
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

target_link_libraries(${PROJECT_NAME} PUBLIC ds_gc)

# On Unix, linking libm is needed for the floating point helpers in some
# libc builds; it does not exist (and is not needed) on Windows.
if(UNIX)
    target_link_libraries(${PROJECT_NAME} PUBLIC m)
endif()

# --------------------------------------------------------------------
# Executables
# --------------------------------------------------------------------
# `lib` is the single library the program links. Most programs want the whole
# stack (ds -> ds_gc -> ds_arena), but the arena suites deliberately link
# ds_arena alone: if the allocator ever grows a dependency on the collector,
# those targets stop linking, which is the point of building them that way.
function(ds_add_program name lib)
    add_executable(${name} ${ARGN})
    ds_set_target_flags(${name})
    target_link_libraries(${name} PRIVATE ${lib})
endfunction()

# Suites that must build against the arena on its own.
set(DS_ARENA_ONLY test_arena bench_arena)

function(ds_program_library out name)
    if(${name} IN_LIST DS_ARENA_ONLY)
        set(${out} ds_arena PARENT_SCOPE)
    else()
        set(${out} ${PROJECT_NAME} PARENT_SCOPE)
    endif()
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
        ds_program_library(_lib test_${suite})
        ds_add_program(test_${suite} ${_lib} tests/test_${suite}.c)
        target_include_directories(test_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
endif()

set(DS_FUZZ_SUITES string arena containers)

if(DS_BUILD_FUZZ)
    foreach(suite IN LISTS DS_FUZZ_SUITES)
        ds_add_program(fuzz_${suite} ${PROJECT_NAME} tests/fuzz_${suite}.c)
        target_include_directories(fuzz_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        if(DS_LIBFUZZER)
            # Coverage-guided. Requires clang with the fuzzer runtime.
            target_compile_definitions(fuzz_${suite} PRIVATE DS_FUZZ_LIBFUZZER)
            target_compile_options(fuzz_${suite} PRIVATE -fsanitize=fuzzer)
            target_link_options(fuzz_${suite} PRIVATE -fsanitize=fuzzer)
        elseif(DS_BUILD_TESTS)
            # The built-in driver runs anywhere, so a short pass is part of
            # the ordinary test run. Longer campaigns: fuzz_string 5000000
            add_test(NAME fuzz_${suite} COMMAND fuzz_${suite} 3000 1)
        endif()
    endforeach()
endif()

if(DS_BUILD_BENCH)
    set(DS_BENCH_TARGETS "")
    foreach(suite IN LISTS DS_BENCH_SUITES)
        ds_program_library(_lib bench_${suite})
        ds_add_program(bench_${suite} ${_lib} tests/bench_${suite}.c)
        target_include_directories(bench_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        list(APPEND DS_BENCH_TARGETS bench_${suite})
    endforeach()

    ds_add_program(main ${PROJECT_NAME} main.c)

    # `cmake --build build --target bench` builds and runs the whole set.
    add_custom_target(bench DEPENDS ${DS_BENCH_TARGETS})
    foreach(t IN LISTS DS_BENCH_TARGETS)
        add_custom_command(TARGET bench POST_BUILD COMMAND $<TARGET_FILE:${t}>)
    endforeach()
endif()

# --------------------------------------------------------------------
# Install
# --------------------------------------------------------------------
# The public headers are listed explicitly rather than globbed. A
# glob over the source root makes CMake walk the whole tree at install
# time -- including the generator's own build directory, which holds
# thousands of files under MSVC -- and, because the install prefix
# usually sits inside the source tree, a second run starts rediscovering
# its own output. An explicit list is instant and deterministic.
set(DS_PUBLIC_HEADERS
    ds_dyn_array.h
    ds_string.h
    ds_graph.h
    ds_hash_map.h
    ds_linked_list.h
    ds_stack_queue.h
    ds_priority_queue.h
)

set(DS_MEMORY_HEADERS
    memory/include/common.h
    memory/include/ds_platform.h
    memory/include/ds_value.h
    memory/include/ds_arena.h
    memory/include/gc.h
)

install(TARGETS ${PROJECT_NAME} ds_gc ds_arena ARCHIVE DESTINATION lib)

# Flattened into one directory: the headers include each other by bare
# name, so a consumer needs them all on a single include path.
install(FILES ${DS_PUBLIC_HEADERS} ${DS_MEMORY_HEADERS} DESTINATION include)

# --------------------------------------------------------------------
# Uninstall
# --------------------------------------------------------------------
# CMake generates no uninstall rule of its own. This drives one from the
# install_manifest.txt that CMake writes into the build tree at install
# time, so it removes exactly what was installed and nothing else.
#
#   sudo cmake --build build --target uninstall
#   (or, from inside build/:  sudo make uninstall)
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        COMMENT "Removing every file listed in install_manifest.txt")
endif()
