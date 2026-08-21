#ifndef ds_priority_queue_h
#define ds_priority_queue_h

#include <stdalign.h>
#include "common.h"

typedef struct {
  alignas(16) ds_node_t *heap_array;
} ds_priority_queue_t;

extern ds_priority_queue_t *ds_pq_new(_ds_arena_t_ *a);
extern void ds_pq_push(_ds_arena_t_ *a, ds_priority_queue_t *pq, ds_node_t value);
extern ds_node_t ds_pq_pop(_ds_arena_t_ *a, ds_priority_queue_t *pq);
extern ds_node_t ds_pq_peek(const ds_priority_queue_t *pq);
extern size_t ds_pq_size(const ds_priority_queue_t *pq);

#endif  // ds_priority_queue_h
