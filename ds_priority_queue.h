#ifndef ds_priority_queue_h
#define ds_priority_queue_h

#include "common.h"
#include "ds_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Binary min-heap over immediate integer nodes (TYPE_INT). */
typedef struct {
  DS_ALIGNAS(ARENA_ALIGN) ds_node_t *heap_array; /* ds_da_* dynamic array */
} ds_priority_queue_t;

extern const ds_type_descriptor_t ds_pq_descriptor;

ds_priority_queue_t *ds_pq_new(_ds_arena_t_ *a);
void ds_pq_push(_ds_arena_t_ *a, ds_priority_queue_t *pq, ds_node_t value);
ds_node_t ds_pq_pop(_ds_arena_t_ *a, ds_priority_queue_t *pq);
ds_node_t ds_pq_peek(const ds_priority_queue_t *pq);
size_t ds_pq_size(const ds_priority_queue_t *pq);
void ds_pq_free(_ds_arena_t_ *a, ds_priority_queue_t *pq);

#ifdef __cplusplus
}
#endif

#endif /* ds_priority_queue_h */
