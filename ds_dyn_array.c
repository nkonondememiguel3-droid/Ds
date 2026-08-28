#include "ds_dyn_array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_arena.h"
#include "gc.h"

/* ================================================================== */
/*  1. Struct-based dynamic array (ds_array_t)                        */
/* ================================================================== */

static void ds_da_gc_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_array_t *arr = (ds_array_t *)ds_get_ptr(node);
  if (!arr) return;

  if ((arr->flags & DS_ARRAY_ELEMENTS_ARE_GC) && arr->data) {
    const ds_node_t *slots = (const ds_node_t *)arr->data;
    size_t i;
    for (i = 0; i < arr->size_used; i++) ds_gc_push_mark_stack_context(a, slots[i]);
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

const ds_type_descriptor_t ds_da_descriptor = {ds_da_gc_mark, ds_array_finalize};

/* Multiplication guard: `n * size` must not wrap. */
static size_t ds_checked_bytes(size_t n, size_t size) {
  if (size != 0 && n > (size_t)-1 / size) {
    fputs("ds: dynamic array size overflow\n", stderr);
    abort();
  }
  return n * size;
}

ds_array_t *ds_array_new(_ds_arena_t_ *a, size_t element_size, size_t initial_cap, uint8_t flags) {
  ds_array_t *arr;
  size_t initial_bytes;

  if (!a) return NULL;
  if (element_size == 0) return NULL;
  if (initial_cap == 0) initial_cap = 8;

  if (flags & DS_ARRAY_HEADER_MANAGED)
    arr = (ds_array_t *)ds_arena_alloc(a, sizeof(ds_array_t), &ds_da_descriptor);
  else
    arr = (ds_array_t *)ds_arena_alloc_raw(a, sizeof(ds_array_t));

  arr->size_used = 0;
  arr->capacity = initial_cap;
  arr->element_size = element_size;
  arr->flags = flags;

  initial_bytes = ds_arena_align_up(ds_checked_bytes(initial_cap, element_size));
  arr->data = ds_arena_alloc_raw(a, initial_bytes);

  return arr;
}

void ds_array_reserve(_ds_arena_t_ *a, ds_array_t *arr, size_t min_cap) {
  size_t new_cap, old_bytes, new_bytes;
  void *new_data;

  if (!a || !arr || arr->capacity >= min_cap) return;

  new_cap = arr->capacity ? arr->capacity * 2 : 8;
  if (new_cap < arr->capacity) new_cap = min_cap; /* doubling wrapped */
  if (new_cap < min_cap) new_cap = min_cap;

  old_bytes = ds_arena_align_up(ds_checked_bytes(arr->capacity, arr->element_size));
  new_bytes = ds_arena_align_up(ds_checked_bytes(new_cap, arr->element_size));

  new_data = ds_arena_alloc_raw(a, new_bytes);
  if (arr->data) {
    if (arr->size_used > 0) memcpy(new_data, arr->data, arr->size_used * arr->element_size);
    /* The old buffer is recycled whether or not anything was copied out of
     * it; the original only recycled it when size_used > 0, leaking every
     * empty-but-allocated buffer back into the arena forever. */
    ds_arena_recycle_raw(a, arr->data, old_bytes);
  }

  arr->data = new_data;
  arr->capacity = new_cap;
}

void ds_array_push(_ds_arena_t_ *a, ds_array_t *arr, const void *element_ptr) {
  char *target;

  if (!arr || !element_ptr) return;
  if (arr->size_used >= arr->capacity) ds_array_reserve(a, arr, arr->size_used + 1);
  if (!arr->data) return;

  target = (char *)arr->data + (arr->size_used * arr->element_size);
  memcpy(target, element_ptr, arr->element_size);
  arr->size_used++;
}

void *ds_array_get(const ds_array_t *arr, size_t index) {
  if (!arr || !arr->data || index >= arr->size_used) return NULL;
  return (char *)arr->data + (index * arr->element_size);
}

void ds_array_free_raw(_ds_arena_t_ *a, ds_array_t *arr) {
  size_t data_bytes;

  /* A managed header is owned by the collector; freeing it by hand would
   * leave a dangling tracking record behind. */
  if (!a || !arr || (arr->flags & DS_ARRAY_HEADER_MANAGED)) return;

  data_bytes = ds_arena_align_up(ds_checked_bytes(arr->capacity, arr->element_size));
  ds_arena_recycle_raw(a, arr->data, data_bytes);
  arr->data = NULL;
  ds_arena_recycle_raw(a, arr, sizeof(ds_array_t));
}

/* ================================================================== */
/*  2. Header-in-front-of-data dynamic array (ds_da_*)                 */
/* ================================================================== */

void *ds__da_grow(_ds_arena_t_ *a, void *data, size_t element_size, size_t extra) {
  _ds_dyn_array_t_ *hdr = NULL;
  size_t size = 0, cap = 0, needed, new_cap, new_bytes;
  _ds_dyn_array_t_ *new_hdr;
  char *new_data;

  if (!a || element_size == 0) return data;

  if (data) {
    hdr = ((_ds_dyn_array_t_ *)data) - 1;
    size = hdr->size;
    cap = hdr->capacity;
  }

  if (extra > (size_t)-1 - size) {
    fputs("ds: dynamic array length overflow\n", stderr);
    abort();
  }
  needed = size + extra;
  if (needed <= cap) return data;

  new_cap = cap ? cap * 2 : 8;
  if (new_cap < cap) new_cap = needed; /* doubling wrapped */
  if (new_cap < needed) new_cap = needed;

  new_bytes = ds_checked_bytes(new_cap, element_size);
  if (new_bytes > (size_t)-1 - sizeof(_ds_dyn_array_t_)) {
    fputs("ds: dynamic array size overflow\n", stderr);
    abort();
  }

  new_hdr = (_ds_dyn_array_t_ *)ds_arena_alloc_raw(a, sizeof(_ds_dyn_array_t_) + new_bytes);
  new_data = (char *)(new_hdr + 1);

  if (data && size > 0) memcpy(new_data, data, size * element_size);

  new_hdr->size = size;
  new_hdr->capacity = new_cap;
  new_hdr->element_size = element_size;
  new_hdr->reserved = 0;

  if (hdr) {
    size_t old_bytes = ds_arena_align_up(sizeof(_ds_dyn_array_t_) + ds_checked_bytes(cap, element_size));
    ds_arena_recycle_raw(a, hdr, old_bytes);
  }

  return new_data;
}

void ds__da_free(_ds_arena_t_ *a, void *data) {
  _ds_dyn_array_t_ *hdr;
  size_t bytes;

  if (!a || !data) return;

  hdr = ((_ds_dyn_array_t_ *)data) - 1;
  bytes = ds_arena_align_up(sizeof(_ds_dyn_array_t_) + ds_checked_bytes(hdr->capacity, hdr->element_size));
  ds_arena_recycle_raw(a, hdr, bytes);
}
