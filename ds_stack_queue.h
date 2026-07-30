#ifndef ds_stack_queue_h
#define ds_stack_queue_h

#include "common.h"
#include "ds_linked_list.h"

typedef struct {
    ds_list_t *list;
} ds_stack_t;

typedef struct {
    ds_list_t *list;
} ds_queue_t;

extern ds_stack_t* ds_stack_new(_ds_arena_t_ *a);
extern void        ds_stack_push(_ds_arena_t_ *a, ds_stack_t *stack, ds_node_t value);
extern ds_node_t   ds_stack_pop(_ds_arena_t_ *a, ds_stack_t *stack);
extern ds_node_t   ds_stack_peek(const ds_stack_t *stack);
extern size_t      ds_stack_size(const ds_stack_t *stack);

extern ds_queue_t* ds_queue_new(_ds_arena_t_ *a);
extern void        ds_queue_enqueue(_ds_arena_t_ *a, ds_queue_t *queue, ds_node_t value);
extern ds_node_t   ds_queue_dequeue(_ds_arena_t_ *a, ds_queue_t *queue);
extern ds_node_t   ds_queue_peek(const ds_queue_t *queue);
extern size_t      ds_queue_size(const ds_queue_t *queue);

#endif // ds_stack_queue_h
