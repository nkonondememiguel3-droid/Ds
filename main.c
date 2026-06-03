#include "ds_dyn_array.h"
#include "ds_arena.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  _ds_arena_t_ arena = ds_arena_new(0);

  int *numbers = NULL;

  ds_da_push(&arena, numbers, 10);
  ds_da_push(&arena, numbers, 20);
  ds_da_push(&arena, numbers, 30);

  ds_da_reserve(&arena, numbers, 10);

  printf("len = %zu\n", ds_da_len(numbers));
  printf("cap = %zu\n", ds_da_cap(numbers));

  for (size_t i = 0; i < ds_da_len(numbers); ++i)
    printf("%d\n", numbers[i]);

  ds_arena_destroy(&arena);
}
