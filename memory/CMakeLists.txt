add_library(ds_arena STATIC
    ds_arena.c
    gc.c
    ds_platform.c
    include/ds_arena.h
    include/common.h
    include/gc.h
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
