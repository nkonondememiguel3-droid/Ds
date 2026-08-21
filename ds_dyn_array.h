#ifndef ds_dynamic_array_h
#define ds_dynamic_array_h

#include <stdbool.h>
#include <stddef.h>

#include "common.h"

#define DS_ARRAY_HEADER_MANAGED 0x01
#define DS_ARRAY_ELEMENTS_ARE_GC 0x02

typedef struct {
  size_t size_used;
  size_t capacity;
  size_t element_size;
  uint8_t flags;
  void *data;
} ds_array_t;

extern const ds_type_descriptor_t ds_da_descriptor;

extern ds_array_t *ds_array_new(_ds_arena_t_ *a, size_t element_size, size_t initial_cap, uint8_t flags);
extern void ds_array_reserve(_ds_arena_t_ *a, ds_array_t *arr, size_t min_cap);
extern void ds_array_push(_ds_arena_t_ *a, ds_array_t *arr, const void *element_ptr);

static inline void ds_array_push_node(_ds_arena_t_ *a, ds_array_t *arr, ds_node_t node) {
  if (arr->size_used >= arr->capacity) ds_array_reserve(a, arr, arr->size_used + 1);
  ((ds_node_t *)arr->data)[arr->size_used++] = node;
}

extern void *ds_array_get(const ds_array_t *arr, size_t index);
extern void ds_array_free_raw(_ds_arena_t_ *a, ds_array_t *arr);

#define ds_array_len(arr) ((arr) ? (arr)->size_used : 0)
#define ds_array_cap(arr) ((arr) ? (arr)->capacity : 0)

#endif
