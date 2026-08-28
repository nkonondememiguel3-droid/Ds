#include "ds_priority_queue.h"

#include <stdlib.h>

#include "ds_arena.h"
#include "ds_dyn_array.h"
#include "gc.h"

/* ------------------------------------------------------------------ */
/* GC integration                                                      */
/* ------------------------------------------------------------------ */

static void ds_pq_mark(ds_node_t node, _ds_arena_t_ *a) {
  ds_priority_queue_t *pq = (ds_priority_queue_t *)ds_get_ptr(node);
  size_t i, n;
  if (!pq || !pq->heap_array) return;

  n = ds_da_len(pq->heap_array);
  for (i = 0; i < n; i++) ds_gc_push_mark_stack_context(a, pq->heap_array[i]);
}

static void ds_pq_finalize(void *ptr, _ds_arena_t_ *a) {
  ds_priority_queue_t *pq = (ds_priority_queue_t *)ptr;
  if (pq && pq->heap_array) {
    ds__da_free(a, pq->heap_array);
    pq->heap_array = NULL;
  }
}

const ds_type_descriptor_t ds_pq_descriptor = {ds_pq_mark, ds_pq_finalize};

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

ds_priority_queue_t *ds_pq_new(_ds_arena_t_ *a) {
  ds_priority_queue_t *pq;
  if (!a) return NULL;

  pq = (ds_priority_queue_t *)ds_arena_alloc(a, sizeof(ds_priority_queue_t), &ds_pq_descriptor);
  pq->heap_array = NULL;
  return pq;
}

size_t ds_pq_size(const ds_priority_queue_t *pq) { return pq ? ds_da_len(pq->heap_array) : (size_t)0; }

ds_node_t ds_pq_peek(const ds_priority_queue_t *pq) {
  if (!pq || ds_pq_size(pq) == 0) return ds_make_nil();
  return pq->heap_array[0];
}

static void sift_up(ds_node_t *arr, size_t index) {
  while (index > 0) {
    size_t parent_idx = (index - 1) / 2;
    ds_node_t tmp;

    if (ds_unpack_int(arr[index]) >= ds_unpack_int(arr[parent_idx])) break;

    tmp = arr[index];
    arr[index] = arr[parent_idx];
    arr[parent_idx] = tmp;

    index = parent_idx;
  }
}

static void sift_down(ds_node_t *arr, size_t size, size_t index) {
  size_t left_child;

  while ((left_child = 2 * index + 1) < size) {
    size_t right_child = left_child + 1;
    size_t smallest = left_child;
    ds_node_t tmp;

    if (right_child < size && ds_unpack_int(arr[right_child]) < ds_unpack_int(arr[left_child])) smallest = right_child;

    if (ds_unpack_int(arr[index]) <= ds_unpack_int(arr[smallest])) break;

    tmp = arr[index];
    arr[index] = arr[smallest];
    arr[smallest] = tmp;

    index = smallest;
  }
}

void ds_pq_push(_ds_arena_t_ *a, ds_priority_queue_t *pq, ds_node_t value) {
  if (!a || !pq) return;

  ds_da_push(a, pq->heap_array, value);
  sift_up(pq->heap_array, ds_da_len(pq->heap_array) - 1);
}

ds_node_t ds_pq_pop(_ds_arena_t_ *a, ds_priority_queue_t *pq) {
  ds_node_t min_value, last;
  size_t current_len;

  DS_UNUSED(a);

  if (!pq || ds_pq_size(pq) == 0) return ds_make_nil();

  min_value = pq->heap_array[0];
  current_len = ds_da_len(pq->heap_array);

  if (current_len == 1) {
    (void)ds_da_pop(pq->heap_array);
    return min_value;
  }

  /* Move the last element into the root, then sift it down. The original
   * wrote `heap_array[0] = ds_da_pop(heap_array)`; because ds_da_pop
   * decrements the length as a side effect inside the same expression, the
   * order in which the two sides were evaluated was unspecified, so the
   * root could be overwritten with the wrong slot on some compilers. */
  last = ds_da_pop(pq->heap_array);
  pq->heap_array[0] = last;

  sift_down(pq->heap_array, ds_da_len(pq->heap_array), 0);

  return min_value;
}

void ds_pq_free(_ds_arena_t_ *a, ds_priority_queue_t *pq) {
  if (!a || !pq) return;
  ds_pq_finalize(pq, a);
}
