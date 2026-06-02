#include "ds_dyn_array.h"

#include "../memory/ds_arena.h"
#include <stdio.h>
#include <string.h>

inline void *ds_da_grow(_ds_arena_t_ *a, void *arr, size_t element_size, size_t new_cap) {
  size_t old_lenght = ds_da_len(arr);
  size_t alloc_size = sizeof(_ds_dyn_array_t_) + new_cap * element_size;
  alloc_size = ds_arena_align_up(alloc_size);

  _ds_dyn_array_t_ *hdr = (_ds_dyn_array_t_ *) ds_arena_alloc(a, alloc_size);
  hdr->size_used = old_lenght;
  hdr->size = new_cap;

  if (arr && old_lenght > 0) memcpy(hdr + 1, arr, old_lenght * element_size);

  return (void *)(hdr + 1);
}
