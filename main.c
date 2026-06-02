#include "dynamic_array/ds_dyn_array.h"
#include "memory/ds_arena.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {

  _ds_arena_t_ arena = ds_arena_new(0);
  _ds_dyn_array_t_ *numbers = {0};

  printf("size: %zu\n", ds_da_len(numbers));
  printf("size used: %zu\n", ds_da_cap(numbers));

  /* ds_da_push(arena, numbers, first_number); */

  ds_arena_destroy(&arena);
  return EXIT_SUCCESS;
}
