cmake_minimum_required(VERSION 3.20)
project(ds LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(memory)

set(source
  ds_dyn_array.c
  ds_dyn_array.h
  ds_string.h
  ds_string.c
  ds_graph.h
  ds_graph.c
  ds_hash_map.c
  ds_hash_map.h
  ds_linked_list.h
  ds_linked_list.c
  ds_stack_queue.h
  ds_stack_queue.c
  ds_priority_queue.h
  ds_priority_queue.c
)

add_library(${PROJECT_NAME} STATIC ${source})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(${PROJECT_NAME} PUBLIC ds_arena)

# debuging purposes
find_package(MKL REQUIRED CONFIG)
add_executable(main main.c)
target_link_libraries(main PRIVATE ${PROJECT_NAME} MKL::MKL)
