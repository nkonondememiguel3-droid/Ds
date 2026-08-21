#include "ds_dyn_array.h"

#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "gc.h"

static void ds_da_gc_mark(ds_node_t node, _ds_arena_t_ *a) {
  void *ptr = ds_get_ptr(node);
  if (!ptr) return;

  ds_array_t *arr = (ds_array_t *)ptr;
  if ((arr->flags & DS_ARRAY_ELEMENTS_ARE_GC) && arr->data) {
    ds_node_t *slots = (ds_node_t *)arr->data;
    for (size_t i = 0; i < arr->size_used; i++) {
      ds_gc_push_mark_stack_context(a, slots[i]);
    }
  }
}

static void ds_array_finalize(void *ptr, _ds_arena_t_ *a) {
  ds_array_t *arr = (ds_array_t *)ptr;
  if (arr && arr->data) {
    size_t data_bytes = ds_arena_align_up(arr->capacity * arr->element_size);
    ds_arena_recycle_raw(a, arr->data, data_bytes);

    arr->data = NULL;
    arr->capacity = 0;
    arr->size_used = 0;
    arr->element_size = 0;
  }
}

const ds_type_descriptor_t ds_da_descriptor = {.mark = ds_da_gc_mark, .finalize = ds_array_finalize};

ds_array_t *ds_array_new(_ds_arena_t_ *a, size_t element_size, size_t initial_cap, uint8_t flags) {
  if (initial_cap == 0) initial_cap = 8;

  ds_array_t *arr = NULL;
  if (flags & DS_ARRAY_HEADER_MANAGED) {
    arr = (ds_array_t *)ds_arena_alloc(a, sizeof(ds_array_t), &ds_da_descriptor);
  } else {
    arr = (ds_array_t *)ds_arena_alloc_raw(a, sizeof(ds_array_t));
  }

  arr->size_used = 0;
  arr->capacity = initial_cap;
  arr->element_size = element_size;
  arr->flags = flags;

  size_t initial_bytes = ds_arena_align_up(initial_cap * element_size);
  arr->data = ds_arena_alloc_raw(a, initial_bytes);

  return arr;
}

void ds_array_reserve(_ds_arena_t_ *a, ds_array_t *arr, size_t min_cap) {
  if (!arr || arr->capacity >= min_cap) return;

  size_t new_cap = arr->capacity ? arr->capacity * 2 : 8;
  if (new_cap < min_cap) new_cap = min_cap;

  size_t old_bytes = ds_arena_align_up(arr->capacity * arr->element_size);
  size_t new_bytes = ds_arena_align_up(new_cap * arr->element_size);

  void *new_data = ds_arena_alloc_raw(a, new_bytes);
  if (arr->size_used > 0) {
    memcpy(new_data, arr->data, arr->size_used * arr->element_size);
    ds_arena_recycle_raw(a, arr->data, old_bytes);
  }

  arr->data = new_data;
  arr->capacity = new_cap;
}

void ds_array_push(_ds_arena_t_ *a, ds_array_t *arr, const void *element_ptr) {
  if (!arr || !element_ptr) return;
  if (arr->size_used >= arr->capacity) ds_array_reserve(a, arr, arr->size_used + 1);

  char *target = (char *)arr->data + (arr->size_used * arr->element_size);
  memcpy(target, element_ptr, arr->element_size);
  arr->size_used++;
}

void *ds_array_get(const ds_array_t *arr, size_t index) {
  if (!arr || index >= arr->size_used) return NULL;
  return (char *)arr->data + (index * arr->element_size);
}

void ds_array_free_raw(_ds_arena_t_ *a, ds_array_t *arr) {
  if (!arr || (arr->flags & DS_ARRAY_HEADER_MANAGED)) return;
  size_t data_bytes = ds_arena_align_up(arr->capacity * arr->element_size);
  ds_arena_recycle_raw(a, arr->data, data_bytes);
  ds_arena_recycle_raw(a, arr, sizeof(ds_array_t));
}
