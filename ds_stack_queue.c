#include "ds_stack_queue.h"

#include <stdlib.h>

#include "ds_arena.h"

ds_stack_t *ds_stack_new(_ds_arena_t_ *a) {
  ds_stack_t *stack = (ds_stack_t *)ds_arena_alloc(a, sizeof(ds_stack_t));
  stack->list = ds_list_new(a);
  return stack;
}

void ds_stack_push(_ds_arena_t_ *a, ds_stack_t *stack, ds_node_t value) {
  if (!stack) return;
  ds_list_append(a, stack->list, value);
}

ds_node_t ds_stack_pop(_ds_arena_t_ *a, ds_stack_t *stack) {
  if (!stack || !stack->list || stack->list->length == 0) {
    return ds_tag_ptr(NULL, TYPE_NIL);
  }
  ds_list_node_t *tail_node = stack->list->head->prev;
  ds_node_t val = tail_node->value;

  ds_list_remove(a, stack->list, tail_node);
  return val;
}

ds_node_t ds_stack_peek(const ds_stack_t *stack) {
  if (!stack || !stack->list || stack->list->length == 0) {
    return ds_tag_ptr(NULL, TYPE_NIL);
  }
  return stack->list->head->prev->value;
}

size_t ds_stack_size(const ds_stack_t *stack) { return stack ? stack->list->length : 0; }

ds_queue_t *ds_queue_new(_ds_arena_t_ *a) {
  ds_queue_t *queue = (ds_queue_t *)ds_arena_alloc(a, sizeof(ds_queue_t));
  queue->list = ds_list_new(a);
  return queue;
}

void ds_queue_enqueue(_ds_arena_t_ *a, ds_queue_t *queue, ds_node_t value) {
  if (!queue) return;
  ds_list_append(a, queue->list, value);
}

ds_node_t ds_queue_dequeue(_ds_arena_t_ *a, ds_queue_t *queue) {
  if (!queue || !queue->list || queue->list->length == 0) {
    return ds_tag_ptr(NULL, TYPE_NIL);
  }
  ds_list_node_t *head_node = queue->list->head;
  ds_node_t val = head_node->value;

  ds_list_remove(a, queue->list, head_node);
  return val;
}

ds_node_t ds_queue_peek(const ds_queue_t *queue) {
  if (!queue || !queue->list || queue->list->length == 0) {
    return ds_tag_ptr(NULL, TYPE_NIL);
  }
  return queue->list->head->value;
}

size_t ds_queue_size(const ds_queue_t *queue) { return queue ? queue->list->length : 0; }
