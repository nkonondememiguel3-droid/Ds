#include "ds_stack_queue.h"

#include <stdlib.h>

#include "ds_arena.h"
#include "gc.h"

/* Both containers are thin wrappers over the circular list, so both mark
 * callbacks just forward to it. Without them the backing list was collected
 * out from under a live stack or queue. */
static void ds_stack_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_stack_t *s = (ds_stack_t *)ds_get_ptr(node);
  if (s && s->list) ds_gc_push_mark_stack_context(a, ds_tag_ptr(s->list, TYPE_NODE));
}

static void ds_queue_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_queue_t *q = (ds_queue_t *)ds_get_ptr(node);
  if (q && q->list) ds_gc_push_mark_stack_context(a, ds_tag_ptr(q->list, TYPE_NODE));
}

const ds_type_descriptor_t ds_stack_descriptor = {ds_stack_mark, NULL};
const ds_type_descriptor_t ds_queue_descriptor = {ds_queue_mark, NULL};

/* ------------------------------------------------------------------ */
/* Stack                                                               */
/* ------------------------------------------------------------------ */

ds_stack_t *ds_stack_new(_ds_arena_t_ *a) {
  ds_stack_t *stack;
  if (!a) return NULL;

  stack = (ds_stack_t *)ds_arena_alloc(a, sizeof(ds_stack_t), &ds_stack_descriptor);
  stack->list = ds_list_new(a);
  return stack;
}

void ds_stack_push(_ds_arena_t_ *a, ds_stack_t *stack, ds_node_t value) {
  if (!stack || !stack->list) return;
  ds_list_append(a, stack->list, value);
}

ds_node_t ds_stack_pop(_ds_arena_t_ *a, ds_stack_t *stack) {
  ds_list_node_t *tail_node;
  ds_node_t val;

  if (!stack || !stack->list || stack->list->length == 0 || !stack->list->head) return ds_make_nil();

  tail_node = stack->list->head->prev;
  val = tail_node->value;

  ds_list_remove(a, stack->list, tail_node);
  return val;
}

ds_node_t ds_stack_peek(const ds_stack_t *stack) {
  if (!stack || !stack->list || stack->list->length == 0 || !stack->list->head) return ds_make_nil();
  return stack->list->head->prev->value;
}

/* Null-safe on the inner list too; the original dereferenced stack->list
 * unconditionally once `stack` was non-NULL. */
size_t ds_stack_size(const ds_stack_t *stack) { return (stack && stack->list) ? stack->list->length : 0; }

/* ------------------------------------------------------------------ */
/* Queue                                                               */
/* ------------------------------------------------------------------ */

ds_queue_t *ds_queue_new(_ds_arena_t_ *a) {
  ds_queue_t *queue;
  if (!a) return NULL;

  queue = (ds_queue_t *)ds_arena_alloc(a, sizeof(ds_queue_t), &ds_queue_descriptor);
  queue->list = ds_list_new(a);
  return queue;
}

void ds_queue_enqueue(_ds_arena_t_ *a, ds_queue_t *queue, ds_node_t value) {
  if (!queue || !queue->list) return;
  ds_list_append(a, queue->list, value);
}

ds_node_t ds_queue_dequeue(_ds_arena_t_ *a, ds_queue_t *queue) {
  ds_list_node_t *head_node;
  ds_node_t val;

  if (!queue || !queue->list || queue->list->length == 0 || !queue->list->head) return ds_make_nil();

  head_node = queue->list->head;
  val = head_node->value;

  ds_list_remove(a, queue->list, head_node);
  return val;
}

ds_node_t ds_queue_peek(const ds_queue_t *queue) {
  if (!queue || !queue->list || queue->list->length == 0 || !queue->list->head) return ds_make_nil();
  return queue->list->head->value;
}

size_t ds_queue_size(const ds_queue_t *queue) { return (queue && queue->list) ? queue->list->length : 0; }
