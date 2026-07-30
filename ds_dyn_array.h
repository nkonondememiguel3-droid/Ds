#ifndef ds_dynamic_array_h
#define ds_dynamic_array_h

#include <assert.h>
#include <stdio.h>

#include "common.h"

// En-tête aligné sur 16 octets pour correspondre parfaitement aux blocs de l'arène
typedef struct __attribute__((aligned(ARENA_ALIGN))) {
  size_t size_used;
  size_t size;
} _ds_dyn_array_t_;

extern void *ds_da_grow(_ds_arena_t_ *a, void *arr, size_t element_size, size_t new_cap);

#define ds_da_hdr(arr) ((_ds_dyn_array_t_ *)(arr) - 1)
#define ds_da_len(arr) ((arr) ? ds_da_hdr(arr)->size_used : (size_t)0)
#define ds_da_cap(arr) ((arr) ? ds_da_hdr(arr)->size : (size_t)0)

#define ds_da_reserve(a, arr, min_cap)                      \
  do {                                                      \
    size_t _mc = (size_t)(min_cap);                         \
                                                            \
    if (ds_da_cap(arr) < _mc) {                             \
      size_t _nc = ds_da_cap(arr) ? ds_da_cap(arr) * 2 : 8; \
      if (_nc < _mc) _nc = _mc;                             \
                                                            \
      (arr) = ds_da_grow((a), (arr), sizeof(*(arr)), _nc);  \
    }                                                       \
  } while (0)

#define ds_da_push(a, arr, val)                    \
  do {                                             \
    ds_da_reserve((a), (arr), ds_da_len(arr) + 1); \
    (arr)[ds_da_hdr(arr)->size_used++] = (val);    \
  } while (0)

#define ds_da_pop(arr) (assert(ds_da_len(arr) > 0), (arr)[--ds_da_hdr(arr)->size_used])

#define ds_da_clear(arr)                    \
  do {                                      \
    if (arr) ds_da_hdr(arr)->size_used = 0; \
  } while (0)

#define ds_da_last(arr) ((arr)[ds_da_len(arr) - 1])
#define ds_da_at(arr, i) ((arr)[(i)])

#endif  // ds_dynamic_array_h
