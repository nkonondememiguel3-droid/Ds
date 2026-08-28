#ifndef ds_dynamic_array_h
#define ds_dynamic_array_h

#include <stdbool.h>
#include <stddef.h>

#include "common.h"
#include "ds_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  1. Struct-based dynamic array (ds_array_t)                        */
/* ================================================================== */

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

ds_array_t *ds_array_new(_ds_arena_t_ *a, size_t element_size, size_t initial_cap, uint8_t flags);
void ds_array_reserve(_ds_arena_t_ *a, ds_array_t *arr, size_t min_cap);
void ds_array_push(_ds_arena_t_ *a, ds_array_t *arr, const void *element_ptr);
void *ds_array_get(const ds_array_t *arr, size_t index);
void ds_array_free_raw(_ds_arena_t_ *a, ds_array_t *arr);

static DS_INLINE void ds_array_push_node(_ds_arena_t_ *a, ds_array_t *arr, ds_node_t node) {
  if (!arr) return;
  if (arr->size_used >= arr->capacity) ds_array_reserve(a, arr, arr->size_used + 1);
  ((ds_node_t *)arr->data)[arr->size_used++] = node;
}

#define ds_array_len(arr) ((arr) ? (arr)->size_used : (size_t)0)
#define ds_array_cap(arr) ((arr) ? (arr)->capacity : (size_t)0)

/* ================================================================== */
/*  2. Header-in-front-of-data dynamic array (ds_da_*)                 */
/* ================================================================== */

/*
 * The "stb-style" array: the caller holds a plain `T *`, and the bookkeeping
 * lives in a header stored immediately before the first element. `NULL` is a
 * valid empty array, so no constructor call is needed.
 *
 *     int *xs = NULL;
 *     ds_da_push(arena, xs, 42);
 *     printf("%zu\n", ds_da_len(xs));
 *     ds_da_free(arena, xs);
 *
 * This API was used throughout ds_string.c, ds_priority_queue.c, ds_graph.c
 * and benchmarck.c but had never been written, which is why those modules
 * were commented out of the build.
 *
 * Note: the macros evaluate their array argument more than once, so do not
 * pass an expression with side effects (`ds_da_push(a, v[i++], x)`).
 */

typedef struct {
  size_t size;
  size_t capacity;
  size_t element_size;
  /* Pad the header out to a multiple of ARENA_ALIGN so that the element
   * data that follows it stays 16-byte aligned and therefore stays taggable
   * as a ds_node_t. */
  size_t reserved;
} _ds_dyn_array_t_;

DS_STATIC_ASSERT(sizeof(_ds_dyn_array_t_) % ARENA_ALIGN == 0,
                 "dynamic array header must preserve 16-byte payload alignment");

/* Grow `data` so it can hold `extra` more elements. Returns the (possibly
 * moved) element pointer. Not called directly -- use ds_da_push. */
void *ds__da_grow(_ds_arena_t_ *a, void *data, size_t element_size, size_t extra);
void ds__da_free(_ds_arena_t_ *a, void *data);

#define ds_da_hdr(arr) (((_ds_dyn_array_t_ *)(void *)(arr)) - 1)

#define ds_da_len(arr) ((arr) ? ds_da_hdr(arr)->size : (size_t)0)
#define ds_da_cap(arr) ((arr) ? ds_da_hdr(arr)->capacity : (size_t)0)

/* Append one element. Reassigns `arr`, which may move. */
#define ds_da_push(arena, arr, value) \
  ((arr) = ds__da_grow((arena), (arr), sizeof(*(arr)), (size_t)1), (arr)[ds_da_hdr(arr)->size++] = (value))

/* Remove and return the last element. Undefined on an empty array. */
#define ds_da_pop(arr) ((arr)[--ds_da_hdr(arr)->size])

/* Ensure room for `n` elements without changing the length. */
#define ds_da_reserve(arena, arr, n)                                                                \
  ((void)(((size_t)(n) > ds_da_cap(arr))                                                            \
              ? ((arr) = ds__da_grow((arena), (arr), sizeof(*(arr)), (size_t)(n) - ds_da_len(arr))) \
              : (arr)))

#define ds_da_clear(arr) ((void)((arr) ? (ds_da_hdr(arr)->size = 0) : (size_t)0))

#define ds_da_free(arena, arr) (ds__da_free((arena), (arr)), (arr) = NULL)

#ifdef __cplusplus
}
#endif

#endif
