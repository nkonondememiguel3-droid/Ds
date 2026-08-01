#ifndef ds_dynamic_array_h
#define ds_dynamic_array_h

#include <assert.h>
#include <stdalign.h>
#include <stdatomic.h>  // Pour la barrière portable ISO C11
#include <stdio.h>

#include "common.h"

typedef struct {
  alignas(16) size_t size_used;
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
      arr = ds_da_grow((a), (arr), sizeof(*(arr)), _nc);    \
    }                                                       \
  } while (0)

// Barrière de synchronisation atomique standard C11 (éradique le Strict Aliasing sous -O3)
#define ds_da_push(a, arr, val)                    \
  do {                                             \
    ds_da_reserve((a), (arr), ds_da_len(arr) + 1); \
    atomic_signal_fence(memory_order_seq_cst);     \
    size_t _idx = ds_da_hdr(arr)->size_used;       \
    (arr)[_idx] = (val);                           \
    atomic_signal_fence(memory_order_seq_cst);     \
    ds_da_hdr(arr)->size_used = _idx + 1;          \
  } while (0)

#define ds_da_pop(arr) (assert(ds_da_len(arr) > 0), (arr)[--ds_da_hdr(arr)->size_used])
#define ds_da_clear(arr)                    \
  do {                                      \
    if (arr) ds_da_hdr(arr)->size_used = 0; \
  } while (0)

// Libération immédiate découplée du GC via le canal brut physique
#define ds_da_free(a, arr)                                                   \
  do {                                                                       \
    if (arr) {                                                               \
      _ds_dyn_array_t_ *_hdr = ds_da_hdr(arr);                               \
      size_t _sz = sizeof(_ds_dyn_array_t_) + (_hdr->size * sizeof(*(arr))); \
      ds_arena_recycle_raw((a), _hdr, _sz);                                  \
      (arr) = NULL;                                                          \
    }                                                                        \
  } while (0)

#endif  // ds_dynamic_array_h
