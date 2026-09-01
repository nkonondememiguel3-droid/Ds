# --------------------------------------------------------------------
# Two libraries, one dependency edge
# --------------------------------------------------------------------
#
#   ds_arena   the allocator on its own. Knows nothing about values,
#              roots or collection. Link this alone and no collector
#              code is compiled into the program.
#   ds_gc      mark and sweep, built on ds_arena's block hooks.
#
# The edge only ever points this way. Adding an #include of gc.h to
# ds_arena.c would create a cycle CMake cannot express, which is the
# tripwire that keeps the split honest.

add_library(ds_arena STATIC
    ds_arena.c
    ds_platform.c
    include/ds_arena.h
    include/ds_platform.h
)

ds_set_target_flags(ds_arena)

target_include_directories(ds_arena PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# clock_gettime lived in librt before glibc 2.17. Link it only where it
# actually exists -- the old unconditional `rt` link broke the build on
# Windows and macOS outright.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckLibraryExists)
    check_library_exists(rt clock_gettime "time.h" DS_HAVE_LIBRT)
    if(DS_HAVE_LIBRT)
        target_link_libraries(ds_arena PUBLIC rt)
    endif()
endif()

add_library(ds_gc STATIC
    gc.c
    include/gc.h
    include/ds_value.h
    include/common.h
)

ds_set_target_flags(ds_gc)

target_link_libraries(ds_gc PUBLIC ds_arena)
