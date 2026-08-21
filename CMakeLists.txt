cmake_minimum_required(VERSION 3.20)
project(ds LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-O3 -march=native)

add_subdirectory(memory)

set(source
  ds_dyn_array.c
  ds_dyn_array.h
  # ds_string.h
  # ds_string.c
  # ds_graph.h
  # ds_graph.c
  # ds_hash_map.c
  # ds_hash_map.h
  # ds_linked_list.h
  # ds_linked_list.c
  # ds_stack_queue.h
  # ds_stack_queue.c
  # ds_priority_queue.h
  # ds_priority_queue.c
)

add_library(${PROJECT_NAME} STATIC ${source})

target_compile_options(${PROJECT_NAME} PRIVATE)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(${PROJECT_NAME} PUBLIC ds_arena)

# debuging purposes
# add_executable(main main.c)
# target_link_libraries(main PRIVATE ${PROJECT_NAME})
# add_executable(benchmarck benchmarck.c)
# target_link_libraries(benchmarck PRIVATE ${PROJECT_NAME} rt)
add_executable(dynamic_array_benchmark dynamic_array_benchmark.c)
target_link_libraries(dynamic_array_benchmark PRIVATE ${PROJECT_NAME} rt)
add_executable(bench_dyn_array_performance tests/bench_dyn_array_performance.c)
target_link_libraries(bench_dyn_array_performance PRIVATE ${PROJECT_NAME} rt)
