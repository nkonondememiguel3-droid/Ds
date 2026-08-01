#include "ds_dyn_array.h"

#include <string.h>

#include "ds_arena.h"

void *ds_da_grow(_ds_arena_t_ *a, void *arr, size_t element_size, size_t new_cap) {
  size_t old_length = ds_da_len(arr);
  _ds_dyn_array_t_ *old_hdr = arr ? ds_da_hdr(arr) : NULL;

  size_t alloc_size = sizeof(_ds_dyn_array_t_) + (new_cap * element_size);
  alloc_size = ds_arena_align_up(alloc_size);

  // Allocation de l'en-tête via le canal d'optimisation untracked brut
  _ds_dyn_array_t_ *new_hdr = (_ds_dyn_array_t_ *)ds_arena_alloc_untracked(a, alloc_size);
  new_hdr->size_used = old_length;
  new_hdr->size = new_cap;

  if (arr && old_length > 0) {
    memcpy(new_hdr + 1, arr, old_length * element_size);
    size_t old_alloc_size = sizeof(_ds_dyn_array_t_) + (old_hdr->size * element_size);

    // Libération directe sur le canal physique (Aucun impact ni recherche inutile dans le GC)
    ds_arena_recycle_raw(a, old_hdr, old_alloc_size);
  }

  return (void *)(new_hdr + 1);
}
