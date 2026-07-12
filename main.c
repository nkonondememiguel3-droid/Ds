#include <mkl_service.h>
#include <stdio.h>
#include <stdlib.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"

void *alloc_mkl(size_t bytes, void *user_data) {
  (void)user_data;
  return mkl_malloc(bytes, 64);
}

void *realloc_mkl(void *ptr, size_t new_size, void *user_data) {
  (void)user_data;
  return mkl_realloc(ptr, new_size);
}

void free_mkl(void *ptr, void *user_data) {
  (void)user_data;
  MKL_free(ptr);
}

int main(void) {
  /* _ds_arena_t_ arena = ds_arena_new(0); */
  _ds_arena_t_ arena = ds_arena_new_with_allocator(0, alloc_mkl, realloc_mkl, free_mkl, NULL);

  /* int *numbers = ARENA_NEW(&arena, int); */

  int *numbers = NULL;

  ds_da_push(&arena, numbers, 10);
  ds_da_push(&arena, numbers, 20);
  ds_da_push(&arena, numbers, 30);

  ds_da_reserve(&arena, numbers, 10);

  printf("len = %zu\n", ds_da_len(numbers));
  printf("cap = %zu\n", ds_da_cap(numbers));

  for (size_t i = 0; i < ds_da_len(numbers); ++i) printf("%d\n", numbers[i]);

  ds_arena_destroy(&arena);
}
