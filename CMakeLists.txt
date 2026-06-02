cmake_minimum_required(VERSION 3.20)
project(ds)

set(C_STANDARD 11)
set(C_STANDARD_REQUIRED ON)
set(C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(source
  ds.c ds.h
  memory/ds_arena.c memory/ds_arena.h
  dynamic_array/ds_dyn_array.c dynamic_array/ds_dyn_array.h
  main.c
)

add_executable(${PROJECT_NAME} ${source})

