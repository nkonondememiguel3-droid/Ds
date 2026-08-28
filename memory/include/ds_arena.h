#ifndef ds_arena_h
#define ds_arena_h

#include "common.h"

#define ARENA_DEFAULT_CHUNK_SIZE (1024u * 1024u)

#ifdef __cplusplus
extern "C" {
#endif

_ds_arena_t_ *ds_arena_new(size_t chunk_size);
size_t ds_arena_align_up(size_t n);

/* Managed allocation: registered with the collector, swept when unreachable.
 * `desc` may be NULL for a leaf object with no outgoing references and no
 * cleanup to perform. */
void *ds_arena_alloc(_ds_arena_t_ *a, size_t size, const ds_type_descriptor_t *desc);

/* Unmanaged allocation: never swept, never traced. Use it for payload
 * buffers owned by a managed object, and free them from that object's
 * finalize callback. */
void *ds_arena_alloc_raw(_ds_arena_t_ *a, size_t size);
void *ds_arena_alloc_internal(_ds_arena_t_ *a, size_t size);

void ds_arena_recycle_raw(_ds_arena_t_ *a, void *dead_ptr, size_t size);
void ds_arena_recycle(_ds_arena_t_ *a, void *dead_ptr, size_t size);
void ds_arena_destroy(_ds_arena_t_ *a);
void ds_arena_print_stats(const _ds_arena_t_ *a);

#define ARENA_NEW(a, T, desc) ((T *)ds_arena_alloc((a), sizeof(T), (desc)))
#define ARENA_ARRAY(a, T, n) ((T *)ds_arena_alloc_raw((a), sizeof(T) * (size_t)(n)))

#ifdef __cplusplus
}
#endif

#endif
