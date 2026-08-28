#ifndef ds_stack_queue_h
#define ds_stack_queue_h

#include "common.h"
#include "ds_arena.h"
#include "ds_linked_list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  DS_ALIGNAS(ARENA_ALIGN) ds_list_t *list;
} ds_stack_t;

typedef struct {
  DS_ALIGNAS(ARENA_ALIGN) ds_list_t *list;
} ds_queue_t;

extern const ds_type_descriptor_t ds_stack_descriptor;
extern const ds_type_descriptor_t ds_queue_descriptor;

ds_stack_t *ds_stack_new(_ds_arena_t_ *a);
void ds_stack_push(_ds_arena_t_ *a, ds_stack_t *stack, ds_node_t value);
ds_node_t ds_stack_pop(_ds_arena_t_ *a, ds_stack_t *stack);
ds_node_t ds_stack_peek(const ds_stack_t *stack);
size_t ds_stack_size(const ds_stack_t *stack);

ds_queue_t *ds_queue_new(_ds_arena_t_ *a);
void ds_queue_enqueue(_ds_arena_t_ *a, ds_queue_t *queue, ds_node_t value);
ds_node_t ds_queue_dequeue(_ds_arena_t_ *a, ds_queue_t *queue);
ds_node_t ds_queue_peek(const ds_queue_t *queue);
size_t ds_queue_size(const ds_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* ds_stack_queue_h */
