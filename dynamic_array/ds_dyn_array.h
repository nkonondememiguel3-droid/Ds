#ifndef ds_dynamic_array_h
#define ds_dynamic_array_h

#include <stdio.h>

typedef struct {
  size_t size_used;
  size_t size;
} _ds_dyn_array_t_;

// retrieve the header of the dynamic array given the user pointer.
#define ds_da_hdr(arr) ((_ds_dyn_array_t_ *)(arr) - 1)

// return the number of elements currently stored.
#define ds_da_len(arr) ((arr) ? ds_da_hdr(arr)->size_used : (size_t)0)

// return the current capacity of the dynamic array.
#define ds_da_cap(arr) ((arr) ? ds_da_hdr(arr)->size : (size_t)0)

// ensure that at least `min_cap` slots of memory are available.
#define ds_da_reserve(a, arr, min_cap)                                         \
  do {                                                                         \
    size_t _mc = (size_t)(min_cap);                                            \
                                                                               \
    if (ds_da_cap(arr) < _mc) {                                                \
      size_t _nc = ds_da_cap(arr) ? ds_da_cap(arr) * 2 : 8;                    \
      if (_nc < _mc)                                                           \
        _nc = _mc;                                                             \
                                                                               \
      (arr) = ds_da_grow((a), (arr), sizeof(*(arr)), _nc);                     \
    }                                                                          \
  } while (0)

// append one element.
#define ds_da_push(a, arr, val)                                                \
  do {                                                                         \
    ds_da_reserve((a), (arr), ds_da_len(arr) + 1);                             \
    (arr)[ds_da_hdr(arr)->size_used++] = (val);                                \
  } while (0)

// remove and return the last element.
#define ds_da_pop(arr)                                                         \
  (assert(ds_da_len(arr) > 0), (arr)[--ds_da_hdr(arr)->size_used])

// reset size_used(length) to zero without freeing size(capacity).
#define ds_da_clear(arr)                                                       \
  do {                                                                         \
    if (arr)                                                                   \
      ds_da_hdr(arr)->size_used = 0;                                           \
  } while (0)

// peek at the last element (does not remove).
#define ds_da_last(arr) ((arr)[ds_da_len(arr) - 1])

// access element using index.
#define ds_da_at(arr, i) ((arr)[(i)])

#endif // ds_dynamic_array_h
