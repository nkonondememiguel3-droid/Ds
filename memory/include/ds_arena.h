#ifndef ds_arena_h
#define ds_arena_h

#include "common.h"

#define ARENA_DEFAULT_CHUNK_SIZE (1024u * 1024u)

extern _ds_arena_t_ ds_arena_new(size_t chunk_size);
extern size_t ds_arena_align_up(size_t n);
extern void *ds_arena_alloc(_ds_arena_t_ *a, size_t size);
extern void ds_arena_destroy(_ds_arena_t_ *a);
extern void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr);

#define ARENA_NEW(a, T) ((T *)ds_arena_alloc((a), sizeof(T)))
#define ARENA_ARRAY(a, T, n) ((T *)ds_arena_alloc((a), sizeof(T) * (size_t)(n)))

typedef struct {
  _ds_arena_chunk_t_ *checkpoint_head;
  size_t checkpoint_size_used;
} _ds_arena_checkpoint_t_;

extern _ds_arena_checkpoint_t_ ds_arena_checkpoint(_ds_arena_t_ *a);
extern void ds_arena_reset_to(_ds_arena_t_ *a, _ds_arena_checkpoint_t_ cp);

_ds_arena_t_ ds_arena_new_with_allocator(size_t chunk_size, ds_mem_alloc_func m_alloc, ds_mem_realloc_func m_realloc,
                                         ds_mem_free_func m_free, void *context);

extern void ds_arena_print_stats(const _ds_arena_t_ *a);

#endif  // ds_arena_h
