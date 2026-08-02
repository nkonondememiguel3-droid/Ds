#ifndef ds_arena_h
#define ds_arena_h

#include "common.h"

#define ARENA_DEFAULT_CHUNK_SIZE (1024u * 1024u)

extern _ds_arena_t_ *ds_arena_new(size_t chunk_size);
extern size_t ds_arena_align_up(size_t n);
extern void *ds_arena_alloc(_ds_arena_t_ *a, size_t size);
extern void *ds_arena_alloc_untracked(_ds_arena_t_ *a, size_t size);
extern void ds_arena_recycle_raw(_ds_arena_t_ *a, void *dead_ptr, size_t size);
extern void *ds_arena_alloc_raw(_ds_arena_t_ *a, size_t size);
extern void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr, size_t size);
extern void ds_arena_destroy(_ds_arena_t_ *a);
extern void ds_arena_print_stats(const _ds_arena_t_ *a);

#define ARENA_NEW(a, T) ((T *)ds_arena_alloc((a), sizeof(T)))
#define ARENA_ARRAY(a, T, n) ((T *)ds_arena_alloc((a), sizeof(T) * (size_t)(n)))

#endif  // ds_arena_h
